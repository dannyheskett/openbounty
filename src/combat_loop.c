// src/combat_loop.c -- shell-side rendered combat loop.
//
// RunCombat plus the modal player-input helpers (target picker,
// spell selection, action dispatch) and the per-frame present/anim
// helpers. Engine-side combat state, AI, and damage formula live in
// engine/combat.c.

#include "frame_host.h"
#include "gfx.h"
#include "input_host.h"
#include "touch.h"
#include "uitouch.h"
#include "combat.h"
#include "combat_loop.h"
#include "combat_render.h"
#include "tables.h"
#include "resources.h"
#include "ui.h"
#include "select.h"
#include "ob_types.h"
#include "recorder.h"
#include "audio.h"
#include "bfont.h"
#include "layout.h"
#include "modern/mlayout.h"
#include "overlay_impl.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include "modern/gamemenu.h"
#include "modern/page.h"
#include "map_render.h"
#include "hud.h"
#include "modern/rail.h"
#include "lattice.h"
#include "present.h"
#include "chrome.h"
#include "palette.h"
#include "overlay.h"
#include "prompt.h"
#include "screenshot.h"
#include "views.h"
#include "views_render.h"
#include <stdio.h>
#include <string.h>

// COMBAT_SPELL_* indices + spell helpers (spell_damage, combat_cast_spell,
// combat_spell_target_filter) are defined in engine combat.h / combat.c.

// Single-frame target-picker step. The outer combat loop calls this
// once per frame while c->picker_active is set. Reads at most one
// arrow (cursor move) or one confirm/cancel input. The render +
// audio + input-poll for THIS frame are handled by the outer loop;
// this function only consumes input that's already been polled.
bool combat_pick_step(Combat *c, const Game *g, const Sprites *sprites,
                      void *render_target,
                      int *out_x, int *out_y, bool *out_cancelled) {
    (void)g; (void)sprites; (void)render_target;
    if (out_cancelled) *out_cancelled = false;

    // Touch: tap a cell to jump the cursor there; if the cell passes the
    // pick filter it confirms in the same tap. ESC chrome cancels.
    touch_request(TOUCH_CHROME_BACK);
    ui_grid(CL_COMBAT_X, CL_COMBAT_Y,
                      COMBAT_W * CL_COMBAT_CELL_W, COMBAT_H * CL_COMBAT_CELL_H,
                      CL_COMBAT_CELL_W, CL_COMBAT_CELL_H, TOUCH_GRID_COMBAT);
    int tcx, tcy;
    if (touch_tapped_cell(TOUCH_GRID_COMBAT, &tcx, &tcy) &&
        combat_in_bounds(tcx, tcy)) {
        c->cursor_x = tcx;
        c->cursor_y = tcy;
        if (combat_cell_passes_filter(c, tcx, tcy, c->side, c->pick_filter)) {
            if (out_x) *out_x = tcx;
            if (out_y) *out_y = tcy;
            return true;
        }
        return false;
    }

    int dx = 0, dy = 0;
    if      (input_key_pressed(KEY_UP)    || input_key_pressed(KEY_KP_8)) dy = -1;
    else if (input_key_pressed(KEY_DOWN)  || input_key_pressed(KEY_KP_2)) dy =  1;
    if      (input_key_pressed(KEY_LEFT)  || input_key_pressed(KEY_KP_4)) dx = -1;
    else if (input_key_pressed(KEY_RIGHT) || input_key_pressed(KEY_KP_6)) dx =  1;
    if      (input_key_pressed(KEY_KP_7) || input_key_pressed(KEY_HOME))      { dx = -1; dy = -1; }
    else if (input_key_pressed(KEY_KP_9) || input_key_pressed(KEY_PAGE_UP))   { dx =  1; dy = -1; }
    else if (input_key_pressed(KEY_KP_1) || input_key_pressed(KEY_END))       { dx = -1; dy =  1; }
    else if (input_key_pressed(KEY_KP_3) || input_key_pressed(KEY_PAGE_DOWN)) { dx =  1; dy =  1; }
    if (dx || dy) {
        int nx = c->cursor_x + dx, ny = c->cursor_y + dy;
        if (combat_in_bounds(nx, ny)) {
            c->cursor_x = nx;
            c->cursor_y = ny;
        }
        return false;
    }

    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
        input_key_pressed(KEY_SPACE) ||
        input_key_pressed(KEY_A)     || input_key_pressed(KEY_C)) {
        if (combat_cell_passes_filter(c, c->cursor_x, c->cursor_y,
                                       c->side, c->pick_filter)) {
            if (out_x) *out_x = c->cursor_x;
            if (out_y) *out_y = c->cursor_y;
            return true;
        }
        // Filter rejected -- leave cursor for another step.
    }
    if (input_key_pressed(KEY_ESCAPE)) {
        if (out_cancelled) *out_cancelled = true;
    }
    return false;
}

// ----- Input + action dispatch -----------------------------------------------

static bool combat_read_dir(int *dx, int *dy) {
    *dx = 0; *dy = 0;
    if (input_key_pressed(KEY_KP_5)) return false;
    if (input_key_pressed(KEY_UP)        || input_key_pressed(KEY_KP_8)) { *dy = -1; return true; }
    if (input_key_pressed(KEY_DOWN)      || input_key_pressed(KEY_KP_2)) { *dy =  1; return true; }
    if (input_key_pressed(KEY_LEFT)      || input_key_pressed(KEY_KP_4)) { *dx = -1; return true; }
    if (input_key_pressed(KEY_RIGHT)     || input_key_pressed(KEY_KP_6)) { *dx =  1; return true; }
    if (input_key_pressed(KEY_HOME)      || input_key_pressed(KEY_KP_7)) { *dx = -1; *dy = -1; return true; }
    if (input_key_pressed(KEY_PAGE_UP)   || input_key_pressed(KEY_KP_9)) { *dx =  1; *dy = -1; return true; }
    if (input_key_pressed(KEY_END)       || input_key_pressed(KEY_KP_1)) { *dx = -1; *dy =  1; return true; }
    if (input_key_pressed(KEY_PAGE_DOWN) || input_key_pressed(KEY_KP_3)) { *dx =  1; *dy =  1; return true; }
    return false;
}

// One-frame cast workflow step. Dispatches on c->cast_phase:
//   PICK_SPELL  : draw the spell menu and read one A..G letter.
//                 Validates it's an owned, in-bounds spell; sets up
//                 the picker for the spell's target filter.
//   PICK_TARGET : feed one input into combat_pick_step. On confirm,
//                 capture the target into c->pick_t1_*. Spells that
//                 take two targets (teleport) advance the
//                 pick_reason and stay in PICK_TARGET.
//   APPLY       : invoke the spell-effect function with the captured
//                 target. Decrement spells.counts[idx], increment
//                 spells_this_round, log the spell name.
// Returns 1 when the casting unit's turn is consumed (effect applied
// successfully). 0 when still mid-cast or cancelled/no-effect.
// Resets c->cast_phase = NONE on success, cancel, and no-effect.
// Modern: the spell menu's cursor row. Shell state, not combat state.
static int s_cast_cursor = 0;

// Spell `idx` chosen: check the charge, then set the picker up for that
// spell's target filter -- shared with the engine dispatcher and the autoplay
// policy so shell and autoplay agree on legal targets. Entered from the combat
// menu's Cast page (modern) or the lettered picker (legacy).
void combat_begin_cast(Combat *c, Game *gw, int idx) {
    const ResCombatLog *cl_pre = combat_log_strings(c);
    if (!gw || idx < 0 || idx >= 7) { c->cast_phase = COMBAT_CAST_NONE; return; }
    if (gw->spells.counts[idx] <= 0) {
        combat_log_template(c, cl_pre->no_spell_type, NULL, 0);
        c->cast_phase = COMBAT_CAST_NONE;
        return;
    }
    c->cast_spell_idx = idx;
    c->pick_filter   = combat_spell_target_filter(idx);
    c->pick_reason   = COMBAT_PICK_REASON_SPELL_TARGET;
    c->picker_active = true;
    if (c->unit_id >= 0) {
        c->cursor_x = c->units[c->side][c->unit_id].x;
        c->cursor_y = c->units[c->side][c->unit_id].y;
    }
    c->cast_phase = COMBAT_CAST_PICK_TARGET;
}

int combat_cast_step(Combat *c, Game *g, const Sprites *sprites,
                     void *render_target) {
    (void)sprites; (void)render_target;
    Game *gw = c->heroes[c->side];
    if (!gw) {
        c->cast_phase = COMBAT_CAST_NONE;
        c->picker_active = false;
        return 0;
    }
    if (c->cast_phase == COMBAT_CAST_PICK_SPELL) {
        touch_request(TOUCH_CHROME_BACK);
        if (input_key_pressed(KEY_ESCAPE)) {
            c->cast_phase = COMBAT_CAST_NONE;
            return 0;
        }
        int picked = -1;
        {   // Modern: cursor rows, Enter or a tap picks; letters in both modes.
            SelList l = { 7, s_cast_cursor };
            int row = -1;
            SelEvent ev = sel_input(&l, TOUCH_LIST_COMBAT_SPELLS, 0, &row);
            s_cast_cursor = l.cursor;
            if (ev == SEL_CONFIRM) picked = row;
        }
        for (int i = 0; i < 7 && picked < 0; i++) {
            if (input_key_pressed(KEY_A + i)) { picked = i; break; }
        }
        if (picked < 0) return 0;
        combat_begin_cast(c, gw, picked);
        return 0;
    }
    if (c->cast_phase == COMBAT_CAST_PICK_TARGET) {
        int tx = 0, ty = 0;
        bool cancelled = false;
        if (!combat_pick_step(c, g, sprites, render_target,
                               &tx, &ty, &cancelled)) {
            if (cancelled) {
                c->cast_phase   = COMBAT_CAST_NONE;
                c->pick_reason  = COMBAT_PICK_REASON_NONE;
                c->picker_active = false;
                return 0;
            }
            return 0;
        }
        // Picker resolved a valid cell. Decide whether this was the
        // first target or (for teleport) the destination pick.
        if (c->cast_spell_idx == COMBAT_SPELL_TELEPORT &&
            c->pick_reason == COMBAT_PICK_REASON_SPELL_TARGET) {
            // First pick: remember the unit; second pick is the
            // destination cell.
            unsigned char uid = c->umap[ty][tx];
            c->pick_t1_x    = tx;
            c->pick_t1_y    = ty;
            c->pick_t1_side = (uid - 1) / COMBAT_SLOTS;
            c->pick_t1_slot = (uid - 1) % COMBAT_SLOTS;
            c->pick_filter  = PICK_FILTER_EMPTY;
            c->pick_reason  = COMBAT_PICK_REASON_TELEPORT_DEST;
            // Reset cursor to caster for the destination pick.
            if (c->unit_id >= 0) {
                c->cursor_x = c->units[c->side][c->unit_id].x;
                c->cursor_y = c->units[c->side][c->unit_id].y;
            }
            return 0;
        }
        // Single-target spell, or teleport's destination: capture
        // and advance to APPLY.
        if (c->pick_reason == COMBAT_PICK_REASON_TELEPORT_DEST) {
            c->cast_dest_x = tx;
            c->cast_dest_y = ty;
        } else {
            unsigned char uid = c->umap[ty][tx];
            c->pick_t1_x    = tx;
            c->pick_t1_y    = ty;
            c->pick_t1_side = (uid - 1) / COMBAT_SLOTS;
            c->pick_t1_slot = (uid - 1) % COMBAT_SLOTS;
        }
        c->picker_active = false;
        c->pick_reason   = COMBAT_PICK_REASON_NONE;
        c->cast_phase    = COMBAT_CAST_APPLY;
        // Fall through to APPLY this same frame.
    }
    if (c->cast_phase == COMBAT_CAST_APPLY) {
        // The index->effect dispatch now lives in the engine (combat_cast_spell)
        // -- shared with the autoplay casting policy so the spell dynamics
        // exist in exactly one place. The shell still owns the UI phases above
        // (PICK_SPELL / PICK_TARGET) that populated cast_spell_idx + the target;
        // here it just applies and clears its UI phase. Behavior is identical to
        // the old inline switch: same effects, logs, charge decrement, and the
        // spells_this_round latch.
        int r = combat_cast_spell(c, c->side, c->cast_spell_idx,
                                  c->pick_t1_side, c->pick_t1_slot,
                                  c->cast_dest_x, c->cast_dest_y);
        c->cast_phase = COMBAT_CAST_NONE;
        return (r == COMBAT_CAST_OK) ? 1 : 0;
    }
    return 0;
}

// ----- Modern: the action menu --------------------------------------------------
// Modern combat is menu driven: Enter, or a tap on the active unit, opens a
// menu of what the unit and the hero can do now. A row closes the menu and
// presses its key on the next frame, so it runs the exact path the key runs;
// the keys stay as shortcuts. Rows that cannot apply are left out.
static bool s_act_open = false;

// The menu's pages (REQ-430s): the top level and its three pages. It opens on
// the Unit page; Back from there goes to the top level, Back again closes.
enum { CM_ROOT = 0, CM_UNIT, CM_HERO, CM_GAME, CM_CAST };
static int s_act_page[3], s_act_cursor[3], s_act_depth = 0;
// The Unit page is always Shoot, Wait, Fly, Cast -- so the first row is
// often one this unit cannot use. The ORDER stays put and the cursor moves
// to the first row that can be chosen, once, as the page opens.
static bool s_act_open_on_enabled;

void combat_gallery_menu(bool open) {
    s_act_open = open;
    s_act_page[0] = CM_ROOT; s_act_cursor[0] = 0;
    s_act_page[1] = CM_UNIT; s_act_cursor[1] = 0;
    s_act_depth = open ? 2 : 0;
    s_act_open_on_enabled = open;   // as combat_menu_open: on the first usable command
}

// --gallery: the menu opened on its Cast page (Actions > Unit > Spells).
void combat_gallery_cast_page(void) {
    combat_gallery_menu(true);
    s_act_page[2] = CM_CAST; s_act_cursor[2] = 0;
    s_act_depth = 3;
    s_act_open_on_enabled = true;   // as the Cast row opens it: on the first spell held
}

static void combat_menu_open(void) {
    s_act_open = true;
    s_act_page[0] = CM_ROOT; s_act_cursor[0] = 0;
    s_act_page[1] = CM_UNIT; s_act_cursor[1] = 0;
    s_act_depth = 2;
    s_act_open_on_enabled = true;   // the rows never move; the cursor may
}

static void combat_menu_page(const Combat *c, const Game *g, int id, GmPage *p) {
    const ResUI *ui = &g->res->ui;
    const ResBanners *bn = &g->res->banners;
    memset(p, 0, sizeof *p);
    #define ROW(l, d, sc, k, en) do { if (p->n < GM_ROWS_MAX) p->item[p->n++] = (GmItem){ l, d, sc, k, en }; } while (0)
    switch (id) {
    case CM_ROOT:
        p->title = ui->gm_actions;
        ROW(ui->gm_unit, bn->gmd_unit, "", GM_ACT_PAGE + CM_UNIT, true);
        ROW(ui->gm_hero, bn->gmd_combat_army, "", GM_ACT_PAGE + CM_HERO, true);
        ROW(ui->gm_game, bn->gmd_controls, "", GM_ACT_PAGE + CM_GAME, true);
        ROW(ui->gm_close, bn->gmd_back, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    case CM_UNIT: {
        const CombatUnit *u = (c->unit_id >= 0) ? &c->units[c->side][c->unit_id] : NULL;
        const TroopDef *t = u ? troop_by_index(u->troop_idx) : NULL;
        const Game *hero = c->heroes[c->side];
        bool shots = u && u->shots > 0;
        bool close = u && combat_unit_surrounded(c, c->side, c->unit_id);
        bool fly = t && (t->abilities & TROOP_ABIL_FLY) && u->flights > 0;
        bool magic = hero && hero->stats.knows_magic;
        bool spell_left = c->spells_this_round < 1;
        p->title = ui->gm_unit;
        // One order, always: Shoot, Wait, Fly, Cast. The rows grey and light
        // with what the unit can do, but they never move -- a command has to
        // be in the same place every turn, on the menu and on the panel
        // beside the field alike.
        ROW(ui->gm_shoot, !shots ? bn->gmr_no_shots : close ? bn->gmr_adjacent : bn->gmd_shoot,
            "S", KEY_S, shots && !close);
        ROW(ui->gm_wait, bn->gmd_wait, "W", KEY_SPACE, true);
        ROW(ui->gm_fly, fly ? bn->gmd_unit_fly : bn->gmr_cannot_fly, "F", KEY_F, fly);
        ROW(ui->gm_cast, !magic ? bn->gmr_no_magic : !spell_left ? bn->gmr_one_spell : bn->gmd_combat_cast,
            "U", GM_ACT_PAGE + CM_CAST, magic && spell_left);
        ROW(ui->gm_back, bn->gmd_back_up, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    }
    case CM_HERO:
        p->title = ui->gm_hero;
        ROW(ui->gm_army, bn->gmd_combat_army, "A", KEY_A, true);
        ROW(ui->gm_character, bn->gmd_combat_character, "V", KEY_V, true);
        ROW(ui->gm_back, bn->gmd_back_up, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    case CM_CAST:
        // The spells: the one spells page (views_render.c), drawn and read by
        // its own functions. The menu keeps only its name, for the path.
        p->title = ui->combat_spells_title;
        break;
    case CM_GAME:
        p->title = ui->gm_game;
        ROW(ui->gm_controls, bn->gmd_controls, "C", KEY_C, true);
        ROW(ui->gm_back, bn->gmd_back_up, "", GM_ACT_BACK, true);
        ROW(ui->gm_give_up, bn->gmd_give_up, "G", KEY_G, true);   // last, like Exit
        p->foot = 2;
        break;
    }
    #undef ROW
}

// A page of the menu opens with its cursor on its first row that can be
// chosen (the spells: the first spell held).
static int combat_menu_first(const Combat *c, const Game *g, int id) {
    if (id == CM_CAST) return views_spells_first(c->heroes[c->side], true);
    GmPage p;
    combat_menu_page(c, g, id, &p);
    return gm_first_enabled(&p);
}

// Open page `id` over the menu's current page.
static void combat_menu_push(const Combat *c, const Game *g, int id) {
    if (s_act_depth >= 3) return;
    s_act_page[s_act_depth] = id;
    s_act_cursor[s_act_depth] = combat_menu_first(c, g, id);
    s_act_depth++;
}

// The menu page: the path as its title, the hero's name at its right, the
// page's rows -- or, on the Cast page, the one spells page.
static void combat_action_menu_draw(const Combat *c, const Game *g) {
    if (s_act_depth < 1) return;
    int d = s_act_depth - 1;
    if (s_act_open_on_enabled) {
        s_act_open_on_enabled = false;
        s_act_cursor[d] = combat_menu_first(c, g, s_act_page[d]);
    }
    GmPage p;
    combat_menu_page(c, g, s_act_page[d], &p);
    char path[96] = "";
    for (int i = 0; i < s_act_depth; i++) {
        GmPage pi;
        combat_menu_page(c, g, s_act_page[i], &pi);
        size_t n = strlen(path);
        snprintf(path + n, sizeof path - n, "%s%s", i ? " > " : "", pi.title ? pi.title : "");
    }
    const char *hero = g ? g->character.name : "";
    if (s_act_page[d] == CM_CAST) {
        modern_spells_draw(c->heroes[c->side], true, s_act_cursor[d], path, hero, g->res->ui.gm_back);
        return;
    }
    int cursor = s_act_cursor[d] < p.n ? s_act_cursor[d] : p.n - 1;
    page_menu(&p, path, hero, cursor, TOUCH_LIST_COMBAT_ACTIONS, NULL, NULL);
}

// ---- the battle's columns ---------------------------------------------------
//
// Modern: the battle takes the base screen's interior (page_combat,
// src/modern/page.c) -- the field left of one column, two tiles wide,
// against the right frame: whose turn it is (the unit's name, count, moves and shots),
// then the commands as tiles -- Menu and the Unit page's commands, always in
// that order, a command the unit cannot use greyed, never moved, and saying
// why on the menu -- and the round.

// The art for a command, by the key its row fires. Cast reuses the rail's lituus.
static Texture2D combat_panel_art(const Sprites *s, int key) {
    if (!s) { Texture2D none = { 0 }; return none; }
    if (key == KEY_S)     return s->combat_shoot;
    if (key == KEY_SPACE) return s->combat_wait;
    if (key == KEY_F)     return s->combat_fly;
    if (key == GM_ACT_PAGE + CM_CAST) return s->rail_cast;
    Texture2D none = { 0 };
    return none;
}

// The key each command answers to (the combat loop's own keys).
static const char *combat_panel_key(int key) {
    if (key == KEY_S)     return "S";
    if (key == KEY_SPACE) return "W";
    if (key == KEY_F)     return "F";
    if (key == GM_ACT_PAGE + CM_CAST) return "U";
    return NULL;
}

// The player's turn: the unit whose turn it is is theirs and answers to them.
static bool players_turn(const Combat *c) {
    return c->side == COMBAT_SIDE_PLAYER && c->unit_id >= 0 &&
           !c->units[c->side][c->unit_id].out_of_control;
}

// A label and its figure on one line, centred in the column.
static int turn_line(int cx, int y, const char *label, int value) {
    char nb[16];
    snprintf(nb, sizeof nb, "%d", value);
    int lw = bfont_text_width(label), sw = bfont_text_width(" "), nw = bfont_text_width(nb);
    int x = cx - (lw + sw + nw) / 2;
    bfont_draw(label, x, y, PAL_CLR(YELLOW));
    bfont_draw(nb, x + lw + sw, y, PAL_CLR(WHITE));
    return y + uk_line_h();
}

// A label over its number, centred on cx.
static int turn_stat(int cx, int y, const char *label, int value) {
    char nb[16];
    snprintf(nb, sizeof nb, "%d", value);
    bfont_draw_centered(label, cx, y, PAL_CLR(YELLOW));
    y += uk_line_h();
    bfont_draw_centered(nb, cx, y, PAL_CLR(WHITE));
    return y + uk_line_h() + UK_INSET;
}

// The log under the grid, newest first, as cards: each a plate of the panel
// fill the column's width with a thin edge and a bar down its left, its line
// wrapped inside (three lines at most). The newest is lit -- gold edge, gold
// bar, yellow words -- and the rest stand back with the dim edge and white
// words; a band of ground parts them, and as many stand as the column has
// room for.
static void combat_log_cards(const Combat *c, ML_Rect col, int y) {
    const int pad = 6, bar = 3, inset = 8, right = 4;
    int tx = col.x + 1 + bar + inset, tw = col.x + col.w - right - tx;
    int bottom = col.y + col.h - UK_BAND;
    for (int i = c->log_count - 1, k = 0; i >= 0; i--, k++) {
        const char *line = c->log_lines[i];
        if (!line[0]) continue;
        int n = uk_lines(line, tw);
        if (n < 1) n = 1;
        if (n > 3) n = 3;
        int h = 2 * pad + n * uk_line_h();
        if (y + h > bottom) break;
        bool lit = (k == 0);
        gfx_rect(col.x, y, col.w, h, uk_fill());
        gfx_rect_lines(col.x, y, col.w, h, lit ? uk_edge() : uk_edge_dim());
        gfx_rect(col.x + 1, y + 1, bar, h - 2, lit ? uk_edge() : uk_edge_dim());
        uk_lines_draw(line, tx, y + pad, tw, 3, lit ? PAL_CLR(YELLOW) : PAL_CLR(WHITE));
        y += h + UK_BAND;
    }
}

// The column: whose turn it is -- the unit's name, its count, its moves and
// shots left -- then the commands as a grid of tiles, two across and three
// down, with the round in the sixth cell. Menu, then Shoot, Wait, Fly and
// Cast, always in that order: a command the unit cannot use is shaded, never
// moved, and says why on the menu; on the foe's turn every command is shaded.
// A tap per usable tile is registered while `live` -- nothing else owns the
// screen.
static void combat_column_draw(const Combat *c, const Game *g, const Sprites *sprites,
                               ML_Rect col, bool live) {
    const int tw = CL_TILE_W, th = CL_TILE_H;
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    lattice_ground(col.x, col.y, col.w, col.h);
    int cx = col.x + col.w / 2, y = col.y + UK_INSET;
    const CombatUnit *u = (c->unit_id >= 0) ? &c->units[c->side][c->unit_id] : NULL;
    const TroopDef *t = (u && u->troop_idx >= 0) ? troop_by_index(u->troop_idx) : NULL;
    if (t) {
        // The name across the column, two lines at most; yellow for the
        // player's troop, red for the foe's. Its count under it.
        y = uk_words_centred(t->name, cx, y, col.w - 2 * UK_INSET, 2,
                             c->side == COMBAT_SIDE_PLAYER ? PAL_CLR(YELLOW) : PAL_CLR(RED));
        char nb[16];
        snprintf(nb, sizeof nb, "%d", u->count);
        bfont_draw_centered(nb, cx, y, PAL_CLR(WHITE));
        y += uk_line_h();
        if (ui) {
            y = turn_line(cx, y, ui->combat_moves, u->moves);
            if (u->shots > 0) y = turn_line(cx, y, ui->combat_shots, u->shots);
        }
    }
    // The commands, a grid under the words.
    int gy = y + UK_INSET;
    GmPage p;
    combat_menu_page(c, g, CM_UNIT, &p);
    bool mine = players_turn(c);
    int n = 0;
    bool enabled[5];
    int tx[5], ty[5];
    for (int i = 0; i < 5; i++) {
        tx[i] = col.x + (i % 2) * tw;
        ty[i] = gy + (i / 2) * th;
        if (ty[i] + th > col.y + col.h) break;
        Texture2D tex = { 0 };
        enabled[i] = mine;
        if (i == 0) {
            tex = sprites ? sprites->rail_menu : (Texture2D){ 0 };
        } else if (i - 1 < p.n) {
            tex = combat_panel_art(sprites, p.item[i - 1].key);
            enabled[i] = mine && p.item[i - 1].enabled;
        }
        if (tex.id) {
            Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
            Rectangle dst = { (float)tx[i], (float)ty[i], (float)tw, (float)th };
            gfx_texture_draw(tex, src, dst, WHITE);
        }
        if (!enabled[i]) gfx_rect(tx[i], ty[i], tw, th, uk_shade());   // it cannot be used now
        n++;
    }
    // The joins: a band across every tile edge of each grid column, one down
    // the middle, and the column's own ground below.
    int rows_l = (n + 1) / 2, rows_r = (n == 5) ? 3 : n / 2;
    hud_column_finish(col.x, gy, tw, col.y + col.h - gy, rows_l);
    hud_column_finish(col.x + tw, gy, tw, col.y + col.h - gy, rows_r);
    lattice_band_v(col.x + tw - CL_UI, gy - CL_UI, 2 * CL_UI, rows_l * th + 2 * CL_UI);
    for (int i = 0; i < n; i++) {
        const char *key = i == 0 ? (g && g->res ? g->res->ui.key_esc : "Esc")
                                 : (i - 1 < p.n ? combat_panel_key(p.item[i - 1].key) : NULL);
        // A command's key shows while it can be pressed.
        if (live && enabled[i]) {
            hud_key_hint(tx[i], ty[i], key);
            ui_tile_row(tx[i], ty[i], tw, th, TOUCH_LIST_COMBAT_PANEL, i);
        }
    }
    // The round, counted from one, in the sixth cell.
    if (ui && n == 5)
        turn_stat(col.x + tw + tw / 2, gy + 2 * th + (th - 2 * uk_line_h()) / 2,
                  ui->combat_round, c->turn + 1);
    // What has happened so far, under the grid.
    combat_log_cards(c, col, gy + rows_l * th + UK_BAND);
}

// The tap on a command tile, resolved the way its key resolves: Menu is
// Escape (the combat menu), each command the key its menu row fires.
static void combat_panel_tap(const Combat *c, const Game *g) {
    if (!CL_IS_MODERN) return;
    int row = touch_tapped_row(TOUCH_LIST_COMBAT_PANEL);
    if (row < 0) return;
    if (row == 0) { input_host_inject_key_next_frame(KEY_ESCAPE); return; }
    GmPage p;
    combat_menu_page(c, g, CM_UNIT, &p);
    int i = row - 1;
    if (i >= p.n || !p.item[i].enabled) return;
    int key = p.item[i].key;
    if (key == GM_ACT_PAGE + CM_CAST) {             // the spells are a page of the menu
        combat_menu_open();
        s_act_open_on_enabled = false;
        combat_menu_push(c, g, CM_CAST);
        return;
    }
    if (key) input_host_inject_key_next_frame(key);
}

static int combat_player_action_full(Combat *c, const Game *g,
                                     const Sprites *sprites,
                                     RenderTexture2D *target) {
    (void)sprites; (void)target;
    // Touch: tap a battlefield cell to step the active unit toward it (one
    // tap, one step, like the adventure map). Legacy: the bar carries the
    // verbs. Modern: a tap on the active unit opens the action menu.
    if (!CL_IS_MODERN) touch_request(TOUCH_CHROME_COMBAT);
    combat_panel_tap(c, g);
    if (CL_IS_MODERN && s_act_open) {
        int d = s_act_depth - 1;
        if (s_act_page[d] == CM_CAST) {
            // The spells page: a spell cast closes the menu and picks its target.
            int spell = -1;
            SpellsEvent ev = views_spells_input(c->heroes[c->side], true, &s_act_cursor[d], &spell);
            if (ev == SPELLS_BACK) {
                if (--s_act_depth < 1) s_act_open = false;
            } else if (ev == SPELLS_CAST) {
                s_act_open = false;
                combat_begin_cast(c, c->heroes[c->side], spell);
            }
            return 0;
        }
        GmPage p;
        combat_menu_page(c, g, s_act_page[d], &p);
        GmEvent ev = gm_page_input(&p, &s_act_cursor[d], TOUCH_LIST_COMBAT_ACTIONS);
        int key = (ev == GM_EV_ACT) ? p.item[s_act_cursor[d]].key : 0;
        if (ev == GM_EV_BACK || key == GM_ACT_BACK) {
            if (--s_act_depth < 1) s_act_open = false;
        } else if (key >= GM_ACT_PAGE && key < GM_ACT_USER) {
            combat_menu_push(c, g, key - GM_ACT_PAGE);
        } else if (key) {
            s_act_open = false;
            input_host_inject_key_next_frame(key);
        }
        return 0;
    }
    if (c->unit_id >= 0) {
        const CombatUnit *au = &c->units[c->side][c->unit_id];
        ui_map(CL_COMBAT_X, CL_COMBAT_Y,
                         COMBAT_W * CL_COMBAT_CELL_W,
                         COMBAT_H * CL_COMBAT_CELL_H,
                         CL_COMBAT_X + au->x * CL_COMBAT_CELL_W,
                         CL_COMBAT_Y + au->y * CL_COMBAT_CELL_H,
                         CL_COMBAT_CELL_W, CL_COMBAT_CELL_H,
                         CL_IS_MODERN ? KEY_ENTER : 0);
    }
    if (CL_IS_MODERN &&
        (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER))) {
        combat_menu_open();
        return 0;
    }
    int dx, dy;
    if (combat_read_dir(&dx, &dy)) {
        if (c->unit_id < 0) return 0;
        return combat_move_unit(c, c->side, c->unit_id, dx, dy);
    }
    if (input_key_pressed(KEY_SPACE) || input_key_pressed(KEY_W) ||
        input_key_pressed(KEY_KP_5)) {
        if (c->unit_id >= 0) c->units[c->side][c->unit_id].acted = true;
        return 1;
    }
    if (input_key_pressed(KEY_G)) {
        // Open a y/n give-up confirm; the outer combat loop polls
        // prompt_update() and writes c->result to 2 on YES.
        if (g && g->res) {
            prompt_yes_no_open(CL_IS_MODERN ? g->res->ui.give_up_header_modern
                                            : g->res->banners.combat_give_up_header,
                               g->res->banners.combat_give_up_body);
            prompt_set_req_kind(PIO_ASK_OVER_FIELD);
        }
        return 0;
    }
    if (input_key_pressed(KEY_S)) {
        // Shoot: arm the picker; the outer loop drives combat_pick_step
        // and dispatches combat_hit_unit when the pick resolves.
        if (c->unit_id < 0) return 0;
        CombatUnit *u = &c->units[c->side][c->unit_id];
        const ResCombatLog *cl_sh = combat_log_strings(c);
        if (u->shots <= 0) {
            combat_log_template(c, cl_sh->no_ammo, NULL, 0);
            return 0;
        }
        if (combat_unit_surrounded(c, c->side, c->unit_id)) {
            combat_log_template(c, cl_sh->cant_shoot, NULL, 0);
            return 0;
        }
        c->cursor_x = u->x;
        c->cursor_y = u->y;
        c->pick_filter   = PICK_FILTER_ENEMY;
        c->pick_reason   = COMBAT_PICK_REASON_SHOOT;
        c->picker_active = true;
        return 0;
    }
    if (input_key_pressed(KEY_F)) {
        // Fly: arm the picker for an empty cell anywhere; the outer
        // loop dispatches combat_fly_unit when the pick resolves.
        if (c->unit_id < 0) return 0;
        CombatUnit *u = &c->units[c->side][c->unit_id];
        const TroopDef *t = troop_by_index(u->troop_idx);
        const ResCombatLog *cl_fl = combat_log_strings(c);
        if (!t || !(t->abilities & TROOP_ABIL_FLY) || u->flights <= 0) {
            combat_log_template(c, cl_fl->cant_fly, NULL, 0);
            return 0;
        }
        c->cursor_x = u->x;
        c->cursor_y = u->y;
        c->pick_filter   = PICK_FILTER_EMPTY;
        c->pick_reason   = COMBAT_PICK_REASON_FLY;
        c->picker_active = true;
        return 0;
    }
    if (input_key_pressed(KEY_U)) {
        // Start the cast workflow. The outer loop drives
        // combat_cast_step on subsequent frames; this just transitions
        // into PICK_SPELL so the menu overlay draws next frame.
        const ResCombatLog *cl_pre = combat_log_strings(c);
        Game *gw = c->heroes[c->side];
        if (!gw) return 0;
        if (c->spells_this_round >= 1) {
            combat_log_template(c, cl_pre->only_one_spell, NULL, 0);
            return 0;
        }
        if (!gw->stats.knows_magic) {
            combat_log_template(c, cl_pre->cannot_cast, NULL, 0);
            return 0;
        }
        if (CL_IS_MODERN) {
            combat_menu_open();
            s_act_open_on_enabled = false;
            combat_menu_push(c, g, CM_CAST);
        } else {
            c->cast_phase = COMBAT_CAST_PICK_SPELL;
        }
        return 0;
    }
    if (input_key_pressed(KEY_C)) {
        // Opening a view pauses combat in the outer loop; the view
        // handles its own input and dismissal. The C<->O swap is
        // implemented there too.
        views_set(VIEW_CONTROLS);
        if (CL_IS_MODERN) views_controls_set_cursor(0);
        return 0;
    }
    return 0;
}

static void combat_present(const Combat *c, const Game *g, const Map *m, const Fog *f,
                           const Sprites *sprites, RenderTexture2D *target) {
    present_refit(target);
    present_begin(target);
    if (CL_IS_MODERN) {
        (void)m; (void)f;
        // The battle takes the base screen's interior, and nothing of the world
        // is drawn: the frame, the field flush left or centred in what the
        // column leaves, and the one column against the right frame.
        chrome_draw_ring(g, sprites);
        PageCombat pc = page_combat(c->castle);
        combat_render_set_field(pc.field.x, pc.field.y, pc.has_wall);
        combat_render_frame(c, g, sprites);
        bool live = !s_act_open && views_active() == VIEW_NONE &&
                    !prompt_is_active() && !dialog_is_active() &&
                    !c->picker_active && c->cast_phase == COMBAT_CAST_NONE;
        combat_column_draw(c, g, sprites, pc.column, live);
        // What just happened: a toast on the field's top edge, as a toast is
        // on the map, for as long.
        static char s_said[COMBAT_BANNER_LEN];
        if (strcmp(s_said, c->banner) != 0) {
            snprintf(s_said, sizeof s_said, "%s", c->banner);
            if (c->banner[0]) toast_show(c->banner);
        }
        // Then the pages, in the one order the map has too: the menu, an open
        // view, the question, the message, and the toast last.
        if (s_act_open && views_active() == VIEW_NONE) combat_action_menu_draw(c, g);
        overlay_draw(g, NULL, NULL, sprites);
        present_end();
        present_scaled(*target);
        frame_host_end_frame();
        return;
    }
    combat_render_frame(c, g, sprites);
    // Open view (Options / Controls / Army / Character) draws over the
    // battlefield, on top of the still-visible field. map/fog are NULL
    // because the views combat can open never read them (WORLDMAP isn't
    // reachable from combat).
    if (views_active() != VIEW_NONE) {
        overlay_draw(g, NULL, NULL, sprites);
    }
    // Spell-pick menu overlay. Drawn while the cast state machine is
    // in PICK_SPELL phase; the outer loop drives combat_cast_step one
    // input per frame.
    if (c->cast_phase == COMBAT_CAST_PICK_SPELL) {
        // Legacy: the historic 320x200 positions, untouched.
        gfx_rect(40, 30, 240, 130, PAL_CLR(DBLUE));
        legacy_window_frame(40, 30, 240, 130, PAL_CLR(YELLOW));
        const Game *gw = c->heroes[c->side];
        const ResUI *ui = &gw->res->ui;
        bfont_draw(ui->combat_spells_title,      140, 36, PAL_CLR(YELLOW));
        bfont_draw(ui->combat_spells_col_combat, 60, 50, PAL_CLR(YELLOW));
        char line[64];   // room for long pack spell names (e.g. Rome's Latin)
        // The seven combat spells are catalog indices 0..6 (COMBAT_SPELL_*),
        // so their display names come straight from the pack -- no hardcoded
        // list, and Rome shows its Latin names.
        for (int i = 0; i < 7; i++) {
            int count = gw->spells.counts[i];
            const SpellDef *sd = spell_by_index(i);
            snprintf(line, sizeof line, "%d %-12s %c",
                     count, sd->name, 'A' + i);
            bfont_draw(line, 56, 64 + i * 10, PAL_CLR(WHITE));
            ui_tile(56, 64 + i * 10, 224, 10, KEY_A + i);
        }
        bfont_draw(ui->combat_spells_prompt, 70, 144, PAL_CLR(WHITE));
    }
    // Victory dialog : centered modal
    // floating over the still-rendered battlefield. Defeat does not
    // draw here -- combat exits silently and perform_temp_death shows
    // the disgrace message at the home castle ().
    if (dialog_is_active()) overlay_draw_note();
    // Give-up confirm and any other y/n / numeric prompt draws on top
    // of everything else as a bottom-frame modal.
    if (prompt_is_active()) prompt_draw();
    present_end();

    present_scaled(*target);
    frame_host_end_frame();
}

// Public presenter for the visible-combat animator: a thin wrapper over
// the static combat_present so src/combat_replay.c can draw a throwaway Combat
// without duplicating the field-draw + scale-to-window blit.
void combat_present_public(const Combat *c, const Game *g, const Map *m, const Fog *f,
                           const Sprites *sprites, void *render_target) {
    combat_present(c, g, m, f, sprites, (RenderTexture2D *)render_target);
}

// Combat tick: decays damage-burst, advances the active unit's
// animation frame, signals AI/rollover. The original DOS KB 150ms SYN
// tick gates animation/AI so the human can see units walk between tiles;
// without the gate combat would look like instant teleportation.
static bool combat_tick_anim(Combat *c, double *next_tick,
                             bool *rolled_over, bool attack_playing) {
    *rolled_over = false;
    {
        double now = frame_host_time();
        if (now < *next_tick) return false;
        *next_tick = now + 0.15;
    }
    // Decay damage-burst on every stack (including dead ones, so the
    // splat plays out over a now-empty cell). Frozen while an attacker's
    // strip is playing: the splat is not drawn during the swing, so
    // counting it down there would spend it unseen.
    // Modern decays the splat on its own faster clock (splat_tick), from the
    // moment the blow lands; legacy keeps King's Bounty's 150 ms decay here.
    if (!CL_IS_MODERN && !attack_playing) {
        for (int s = 0; s < COMBAT_SIDES; s++) {
            for (int i = 0; i < COMBAT_SLOTS; i++) {
                CombatUnit *u = &c->units[s][i];
                if (u->troop_idx < 0) continue;
                if (u->hit_flash > 0) u->hit_flash--;
            }
        }
    }
    // Advance only the active unit's animation frame (visual cue for
    // whose turn it is). All other stacks stay on frame 0.
    if (c->unit_id >= 0) {
        CombatUnit *act = &c->units[c->side][c->unit_id];
        if (act->troop_idx >= 0 && act->count > 0) {
            act->frame++;
            if (act->frame > 3) {
                act->frame = 0;
                *rolled_over = true;
            }
        }
    }
    return true;
}

// Modern: the beat of one blow (REQ-398). Every number here is a length of
// time, not a count of ticks, so a 6-frame troop swings in the same time as a
// 4-frame one.
//
//   0 ms         the swing starts; the engine has already dealt the damage,
//                but the target still SHOWS its old count and no splat
//   last frame   the blow lands: the count drops and the splat appears
//   +300 ms      the splat is done; a stack the blow killed leaves the field
//                and the fight moves on
//
// About 0.6 s a blow. At the end of a fight the field is then held 0.5 s so
// the killing blow and the empty field are seen before victory or defeat.
#define ATTACK_STRIP_S  0.36   // the whole swing, whatever its frame count
#define SPLAT_TICK_S    0.10   // hit_flash is 3: a 300 ms splat
#define FIGHT_END_HOLD  0.50   // after the last splat, before the ending

// The attacker is found by side and cell, which survive the slot renumbering
// a death causes.
typedef struct { int side, x, y, frame, frames, seq; double start; bool impact; } AttackAnim;

static void attack_anim_start(AttackAnim *a, const Combat *c, const Sprites *sprites) {
    a->seq = c->attack_seq;
    a->frame = -1;
    a->impact = true;          // no strip: the blow lands at once
    if (!CL_IS_MODERN) return;
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const CombatUnit *u = &c->units[c->attack_side][i];
        // The attacker cannot be dead, but the cell test is what finds it.
        if (u->troop_idx < 0 || u->x != c->attack_x || u->y != c->attack_y)
            continue;
        if (u->troop_idx >= sprites->troop_count) break;
        a->frames = sprites->troop_anim_frames[u->troop_idx];
        if (a->frames <= 1) break;
        a->side = c->attack_side;
        a->x = c->attack_x;
        a->y = c->attack_y;
        a->frame = 0;
        a->start = frame_host_time();
        a->impact = false;
        break;
    }
    combat_render_set_impact(a->impact);
    combat_render_set_attack(a->side, a->x, a->y, a->frame);
}

// One frame of a playing swing. True while it still plays. The blow lands on
// the strip's last frame, not after it: the impact is part of the swing.
static bool attack_anim_step(AttackAnim *a) {
    if (a->frame < 0) return false;
    double t = frame_host_time() - a->start;
    int f = (int)(t / (ATTACK_STRIP_S / a->frames));
    if (f >= a->frames) {
        a->frame = -1;
    } else {
        a->frame = f;
        if (f == a->frames - 1 && !a->impact) {
            a->impact = true;
            combat_render_set_impact(true);
        }
    }
    combat_render_set_attack(a->side, a->x, a->y, a->frame);
    return a->frame >= 0;
}

// Modern: the splat's own clock, running only once the blow has landed.
static void splat_tick(Combat *c, const AttackAnim *a, double *next) {
    if (!CL_IS_MODERN || !a->impact) return;
    double now = frame_host_time();
    if (now < *next) return;
    *next = now + SPLAT_TICK_S;
    for (int s = 0; s < COMBAT_SIDES; s++)
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c->units[s][i];
            if (u->troop_idx >= 0 && u->hit_flash > 0) u->hit_flash--;
        }
}

// Any splat still on the field.
static bool splat_showing(const Combat *c) {
    for (int s = 0; s < COMBAT_SIDES; s++)
        for (int i = 0; i < COMBAT_SLOTS; i++)
            if (c->units[s][i].troop_idx >= 0 && c->units[s][i].hit_flash > 0) return true;
    return false;
}

// Block until the player acknowledges the open end-of-combat dialog,
// keeping the battlefield rendered behind it. Without this, RunCombat
// would return the moment c.result is set, callers would mutate world
// state (castle ownership, perform_temp_death), and the dialog would
// be drawn over the overworld instead of over the battle that produced
// it.
static void combat_wait_for_dialog_ack(const Combat *c, const Game *g,
                                       const Map *m, const Fog *f,
                                       const Sprites *sprites,
                                       RenderTexture2D *target) {
    while (dialog_is_active() && !frame_host_should_close()) {
        combat_present(c, g, m, f, sprites, target);
        // Allow screenshots while paused on the end-of-combat dialog
        // (main loop is suspended during RunCombat).
        screenshot_tick(*target, "shot");
        if (ui_any_key_pressed()) {
            if (!dialog_advance()) dialog_dismiss();
        }
    }
}

CombatResult RunCombat(Game *g, const Map *m, const Fog *f, const Sprites *sprites,
                       void *render_target,
                       CombatMode mode, const CombatTarget *target) {
    Combat c;
    combat_init(&c, g, mode, target);
    combat_seed_rng(&c, g, mode, target);
    combat_prepare_player(&c, g);
    if (mode == COMBAT_MODE_CASTLE) combat_prepare_castle(&c, target);
    else                            combat_prepare_foe(&c, target);
    combat_reset_match(&c);
    {
        char tag[64];
        snprintf(tag, sizeof tag, "combat:start:%s:%s",
                 mode == COMBAT_MODE_CASTLE ? "castle" : "foe",
                 (target && target->name) ? target->name : "?");
        recorder_capture(tag);
    }
    audio_set_track(AUDIO_TRACK_COMBAT);
    s_act_open = false;

    // Prime the turn machinery: pretend AI just finished, refresh
    // counters, find first actable player unit.
    combat_reset_turn(&c, COMBAT_SIDE_AI);
    c.unit_id = -1;
    int nxt = combat_next_unit(&c);
    if (nxt < 0) {
        recorder_capture("combat:end:loss");
        audio_play_tune(AUDIO_TUNE_DEFEAT);
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        return COMBAT_RESULT_LOSS;
    }
    c.unit_id = nxt;

    RenderTexture2D *rt = (RenderTexture2D *)render_target;
    double next_tick = frame_host_time() + 0.15;
    AttackAnim atk = { -1, 0, 0, -1, 0, c.attack_seq, 0.0, true };
    double next_splat = frame_host_time();
    int deferred_acted = 0;   // modern: a struck blow waiting for its swing
    combat_render_set_attack(-1, 0, 0, -1);
    combat_render_set_impact(true);

    while (c.result == 0 && !frame_host_should_close()) {
        // Keep audio and presentation ticking every frame while the battle
        // runs, so music and the rendered field stay live.
        audio_tick();
        combat_present(&c, g, m, f, sprites, rt);

        // Modal prompts (give-up confirm) take priority over every
        // other input path and pause combat -- a blocking call inside
        // the dispatch.
        if (prompt_is_active()) {
            PromptResult pr = prompt_update();
            if (pr == PROMPT_RESULT_YES) {
                c.result = 2;
                break;
            }
            // NO / CANCEL / NONE: stay in combat, swallow the frame.
            continue;
        }

        // ESC is cancel-only in combat: ESC inside a menu just closes it.
        // Take this *before* views eat their own input so dismissing always
        // works from the outer loop. BUT when a shoot/fly/cast picker is armed,
        // ESC is that picker's cancel key -- fall through so its branch below
        // backs the action out (no turn spent). Without this, ESC was swallowed
        // here and a no-target spell (e.g. Turn Undead with no undead on the
        // field) trapped the turn with no way out.
        if (input_key_pressed(KEY_ESCAPE)) {
            if (views_active() != VIEW_NONE) {
                views_dismiss();
                continue;
            }
            if (s_act_open) {           // modern action menu: ESC goes back a page
                if (--s_act_depth < 1) s_act_open = false;
                continue;
            }
            if (!c.picker_active && c.cast_phase == COMBAT_CAST_NONE) {
                // Nothing armed. Modern: ESC (or a tap on the top bar) opens the
                // action menu on the player's turn; legacy: a no-op.
                if (CL_IS_MODERN && c.side == COMBAT_SIDE_PLAYER && c.unit_id >= 0 &&
                    !c.units[c.side][c.unit_id].out_of_control) {
                    combat_menu_open();
                }
                continue;
            }
            // else: let the cast / shoot-fly picker branch handle the cancel.
        }

        // While a view is open, combat is paused: no AI advancement,
        // no animation frame ticks driving acts. The view handles its
        // own input (and number-key cycling for Controls -- see below).
        if (views_active() != VIEW_NONE && CL_IS_MODERN) {
            // Controls reads its own keys, as it does on the map; a sheet
            // closes on any key, as it does on the map.
            ViewKind v = views_active();
            if (v == VIEW_CONTROLS)                           views_controls_input(g);
            else if (views_closes_on_tap(v) && ui_any_key_pressed()) views_dismiss();
            continue;
        }
        if (views_active() != VIEW_NONE) {
            touch_request(TOUCH_CHROME_BACK);   // ESC dismisses the view
            // Swap between Options and Controls without leaving the menu.
            if (views_active() == VIEW_OPTIONS && input_key_pressed(KEY_C)) {
                views_set(VIEW_CONTROLS);
            } else if (views_active() == VIEW_CONTROLS && !CL_IS_MODERN &&
                       input_key_pressed(KEY_O)) {
                views_set(VIEW_OPTIONS);
            } else if (views_active() == VIEW_CONTROLS) {
                // Number keys 1..N cycle the corresponding control row.
                for (int i = 0; i < 9; i++) {
                    if (input_key_pressed(KEY_ONE + i)) {
                        views_controls_advance(g, i);
                        break;
                    }
                }
            }
            continue;
        }

        bool frame_rollover;
        (void)combat_tick_anim(&c, &next_tick, &frame_rollover, atk.frame >= 0);
        splat_tick(&c, &atk, &next_splat);
        // A blow is playing: nothing else happens until the swing has played
        // AND its splat is done, so the dead leave the field after you have
        // seen what killed them.
        if (attack_anim_step(&atk)) continue;
        if (CL_IS_MODERN && deferred_acted && splat_showing(&c)) continue;

        int acted = 0;
        // Modern: settle the blow that was struck at the start of the swing
        // -- the dead leave the field, the turn moves on -- before anyone acts
        // again.
        if (deferred_acted) {
            acted = deferred_acted;
            deferred_acted = 0;
            goto settle;
        }

        if (c.unit_id >= 0 && !c.picker_active &&
            c.cast_phase == COMBAT_CAST_NONE) {
            // Keep the cursor parked on the acting unit between
            // turns -- but NEVER clobber it while a picker is open,
            // or the player's cursor moves get reset every frame
            // and the pick can never resolve.
            const CombatUnit *act = &c.units[c.side][c.unit_id];
            if (act->troop_idx >= 0 && act->count > 0) {
                c.cursor_x = act->x;
                c.cursor_y = act->y;
            }
        }

        bool player_turn = (c.side == COMBAT_SIDE_PLAYER &&
                            c.unit_id >= 0 &&
                            !c.units[c.side][c.unit_id].out_of_control);

        // Top-level view openers. Player can open these even on AI's
        // turn: input is discarded for moves during AI turn, but menu
        // keys still resolve.
        //
        // Gate KEY_A on no-active-picker: A is also "confirm" for the
        // picker, and we don't want a picker confirm to accidentally
        // open the Army view.
        if (c.cast_phase == COMBAT_CAST_NONE && !c.picker_active && !s_act_open) {
            // Modern retires the Options panel; the action menu has the rest.
            if (!CL_IS_MODERN && input_key_pressed(KEY_O)) {
                views_set(VIEW_OPTIONS);
                continue;
            }
            if (input_key_pressed(KEY_A)) {
                views_set(VIEW_ARMY);
                // Modern: on your turn the sheet opens on the troop whose turn
                // it is (the combat menu's Hero page reaches this key too).
                if (CL_IS_MODERN && c.side == COMBAT_SIDE_PLAYER && c.unit_id >= 0)
                    views_army_mark(c.units[c.side][c.unit_id].troop_idx);
                continue;
            }
            if (input_key_pressed(KEY_V)) {
                views_set(VIEW_CHARACTER);
                continue;
            }
        }

        // Cast workflow takes precedence over the normal player-input
        // dispatch: while c.cast_phase != NONE the spell menu /
        // target picker is on screen and we step that state machine
        // one phase per frame.
        if (c.cast_phase != COMBAT_CAST_NONE) {
            acted = combat_cast_step(&c, g, sprites, rt);
            // No fall-through: even if not yet acted, we don't want
            // KEY_S / KEY_F / KEY_U etc. to re-fire on the same input
            // tick. Resume the next-unit pick if acted; otherwise
            // continue the frame loop.
            if (!acted) continue;
        } else if (c.picker_active &&
                   (c.pick_reason == COMBAT_PICK_REASON_SHOOT ||
                    c.pick_reason == COMBAT_PICK_REASON_FLY)) {
            // Shoot/Fly picker: one combat_pick_step per frame.
            int tx = 0, ty = 0;
            bool cancelled = false;
            bool resolved = combat_pick_step(&c, g, sprites, rt,
                                             &tx, &ty, &cancelled);
            if (cancelled) {
                c.picker_active = false;
                c.pick_reason   = COMBAT_PICK_REASON_NONE;
                continue;
            }
            if (!resolved) continue;
            CombatUnit *u = &c.units[c.side][c.unit_id];
            if (c.pick_reason == COMBAT_PICK_REASON_SHOOT) {
                unsigned char uid = c.umap[ty][tx];
                int t_side = (uid - 1) / COMBAT_SLOTS;
                int t_slot = (uid - 1) % COMBAT_SLOTS;
                combat_hit_unit(&c, c.side, c.unit_id,
                                t_side, t_slot, true);
                u->acted = true;
                acted = 1;
            } else { // FLY
                const TroopDef *t = troop_by_index(u->troop_idx);
                const ResCombatLog *cl_fl = combat_log_strings(&c);
                if (combat_fly_unit(&c, c.side, c.unit_id, tx, ty)) {
                    if (t) {
                        ResTemplateVar vars[] = { { "TROOP", t->name } };
                        combat_log_template(&c,
                            cl_fl ? cl_fl->fly : "%TROOP% fly", vars, 1);
                    }
                    acted = 1;
                }
            }
            c.picker_active = false;
            c.pick_reason   = COMBAT_PICK_REASON_NONE;
        } else if (player_turn) {
            acted = combat_player_action_full(&c, g, sprites, rt);
        } else if (frame_rollover) {
            // AI drives this turn, paced to the SYN-tick rollover so units
            // visibly walk between tiles.
            acted = combat_ai_action(&c);
        }

        // A troop attacked this frame: play its strip before the fight goes on.
        if (c.attack_seq != atk.seq) attack_anim_start(&atk, &c, sprites);

        // Modern: hold the blow's effect until the swing has played. The
        // engine deals damage in the same call that starts the strip, so
        // settling here would clear the dead and spend the splat under the
        // weapon. Legacy never starts a strip, so it settles as it always did.
        if (acted && atk.frame >= 0) {
            deferred_acted = acted;
            continue;
        }

settle:
        if (acted) {
            combat_compact(&c);
            if (combat_test_dead(&c, COMBAT_SIDE_AI))     { c.result = 1; break; }
            if (combat_test_dead(&c, COMBAT_SIDE_PLAYER)) { c.result = 2; break; }
            // Advance to the next unit ONLY when the acting unit's turn is
            // truly over (u->acted), not after every single action (issue #8).
            // combat_move_unit / combat_fly_unit set acted only once the unit's
            // moves/flights are spent (or it attacked); a hero spell never sets
            // it. So one action no longer ends the turn: a unit walks its full
            // move_rate, a flyer can attack the same turn it flies, and casting
            // a spell doesn't skip the current creature. The AI walks the same
            // way: combat_ai_action takes one step per call until its moves
            // run out (#54).
            const CombatUnit *cur =
                (c.unit_id >= 0) ? &c.units[c.side][c.unit_id] : NULL;
            bool unit_done = (!cur) || cur->troop_idx < 0 ||
                             cur->count == 0 || cur->acted;
            if (unit_done) {
                // Park the now-inactive unit on its idle frame so it stops
                // animating. The next unit picked starts cycling from frame 0.
                if (c.unit_id >= 0) {
                    CombatUnit *prev = &c.units[c.side][c.unit_id];
                    if (prev->troop_idx >= 0) prev->frame = 0;
                }
                int n = combat_next_unit(&c);
                if (n < 0) combat_next_turn(&c);
                else        c.unit_id = n;
            }
        }
    }

    // The blow that ended the fight plays out -- swing, then splat -- before
    // the ending shows.
    while ((atk.frame >= 0 || (CL_IS_MODERN && splat_showing(&c))) &&
           !frame_host_should_close()) {
        audio_tick();
        combat_present(&c, g, m, f, sprites, rt);
        bool rolled;
        (void)combat_tick_anim(&c, &next_tick, &rolled, atk.frame >= 0);
        attack_anim_step(&atk);
        splat_tick(&c, &atk, &next_splat);
    }
    combat_render_set_attack(-1, 0, 0, -1);
    combat_render_set_impact(true);

    if (CL_IS_MODERN) {
        double until = frame_host_time() + FIGHT_END_HOLD;
        while (frame_host_time() < until && !frame_host_should_close()) {
            audio_tick();
            combat_present(&c, g, m, f, sprites, rt);
            bool rolled;
            combat_tick_anim(&c, &next_tick, &rolled, false);
        }
    }

    // ----- End-of-combat ----------------------------------------------------

    // Victory: gold += AI-side spoils, show victory banner.
    // Defeat: show "disgraced" flavor (the temp_death and re-commission
    // flow lives in main.c's caller and stays there).
    if (c.result == 1) {
        g->stats.gold += c.spoils[COMBAT_SIDE_AI];
        char body[400], gbuf[16];
        snprintf(gbuf, sizeof gbuf, "%d", c.spoils[COMBAT_SIDE_AI]);
        const ResBanners *bn = &g->res->banners;
        if (c.target_name[0]) {
            ResTemplateVar vars[] = {
                { "NAME",   g->character.name },
                { "TARGET", c.target_name },
                { "GOLD",   gbuf },
            };
            resources_format_template(body, sizeof body,
                                      bn->combat_victory_named, vars, 3);
        } else {
            ResTemplateVar vars[] = {
                { "NAME", g->character.name },
                { "GOLD", gbuf },
            };
            resources_format_template(body, sizeof body,
                                      bn->combat_victory_unnamed, vars, 2);
        }
        open_dialog_kind(g->res->ui.dt_combat_victory, body, PIO_NOTE_OVER_FIELD);
        combat_wait_for_dialog_ack(&c, g, m, f, sprites, rt);
        // Write surviving troops back to g->army so the player keeps
        // their losses. Vacated slots get compacted afterwards so the
        // army view stays contiguous (no gaps where a stack was wiped).
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c.units[COMBAT_SIDE_PLAYER][i];
            if (u->troop_idx < 0 || u->count == 0) {
                g->army[i].id[0] = '\0';
                g->army[i].count = 0;
            } else {
                const TroopDef *t = troop_by_index(u->troop_idx);
                if (t) {
                    snprintf(g->army[i].id, sizeof g->army[i].id, "%s", t->id);
                    g->army[i].count = u->count;
                }
            }
        }
        GameCompactArmy(g);
        recorder_capture("combat:end:win");
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        return COMBAT_RESULT_WIN;
    }
    if (c.result == 2) {
        // Defeat returns silently. The "After being disgraced..." message
        // is shown by perform_temp_death (src/shell_tempdeath.c, called from
        // src/shell_promptdispatch.c) after the player has been teleported
        // back to the home castle. No battlefield-side dialog here.
        recorder_capture("combat:end:loss");
        audio_play_tune(AUDIO_TUNE_DEFEAT);
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        return COMBAT_RESULT_LOSS;
    }
    // Unreachable in normal play: the loop only exits via WIN or LOSS.
    // Kept as a defensive return so the function is total.
    recorder_capture("combat:end:loss");
    audio_set_track(AUDIO_TRACK_OPENWORLD);
    return COMBAT_RESULT_LOSS;
}
