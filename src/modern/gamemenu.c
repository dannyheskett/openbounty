// src/modern/gamemenu.c -- the modern drill-down menus (see gamemenu.h).

#include "modern/gamemenu.h"
#include "modern/mlayout.h"
#include "modern/uikit.h"
#include "modern/saveslots.h"
#include "shell_cheats.h"
#include "input_host.h"
#include "lattice.h"
#include "palette.h"
#include "resources.h"
#include "touch.h"
#include "views.h"
#include "bfont.h"
#include <stdio.h>
#include <string.h>

// ---- a page ------------------------------------------------------------------------

GmEvent gm_page_input(const GmPage *p, int *cursor, int touch_list) {
    if (p->n <= 0) return GM_EV_BACK;
    if (*cursor >= p->n) *cursor = p->n - 1;
    if (*cursor < 0) *cursor = 0;
    touch_request(TOUCH_CHROME_BACK);
    int tapped = touch_tapped_row(touch_list);
    if (tapped >= 0 && tapped < p->n) {
        *cursor = tapped;
        return p->item[tapped].enabled ? GM_EV_ACT : GM_EV_NONE;
    }
    if (input_key_pressed(KEY_ESCAPE)) return GM_EV_BACK;
    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8))
        *cursor = (*cursor - 1 + p->n) % p->n;
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2))
        *cursor = (*cursor + 1) % p->n;
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) || input_key_pressed(KEY_SPACE))
        return p->item[*cursor].enabled ? GM_EV_ACT : GM_EV_NONE;
    return GM_EV_NONE;
}

static bool page_row(void *ctx, int i, char *label, char *right, int cap) {
    const GmPage *p = (const GmPage *)ctx;
    const GmItem *it = &p->item[i];
    bool sub = it->key >= GM_ACT_PAGE && it->key < GM_ACT_USER;
    snprintf(label, (size_t)cap, "%s%s", it->label ? it->label : "", sub ? " >" : "");
    snprintf(right, 48, "%s", (input_has_keyboard() && it->shortcut) ? it->shortcut : "");
    return it->enabled;
}

int gm_page_width(const GmPage *p, const char *path) {
    // The widest row (its label, the submenu mark and its key) and the path.
    int w = bfont_text_width(path ? path : "") + 2 * ML_PAD + 12 * BFONT_GLYPH_W;
    for (int i = 0; i < p->n; i++) {
        char label[96], right[48];
        page_row((void *)p, i, label, right, (int)sizeof label);
        int need = bfont_text_width(label) + (right[0] ? bfont_text_width(right) + 4 * BFONT_GLYPH_W : 0) + 4 * ML_PAD;
        if (need > w) w = need;
    }
    // And room for every description in two lines.
    for (int i = 0; i < p->n; i++) {
        int need = (p->item[i].desc ? bfont_text_width(p->item[i].desc) : 0) / 2 + 4 * BFONT_GLYPH_W + 2 * UK_INSET;
        if (need > w) w = need;
    }
    return w < 400 ? 400 : w;
}

void gm_draw_page(const GmPage *p, const char *path, const char *right_title,
                  int x, int y, int w, int h, int list_w, int cursor, int touch_list,
                  MlRowFn row_fn, void *row_ctx) {
    // An in-lay `w` wide over the dimmed screen: the path as its title, the
    // rows at full width, and the description of the row under the cursor
    // along the foot (yellow when it says why the row is greyed).
    (void)x; (void)y; (void)h; (void)list_w;
    const int GH = BFONT_GLYPH_H, lh = uk_line_h();
    int text_w = w - 2 * UK_INSET;
    int foot_lines = 1;
    for (int i = 0; i < p->n; i++) {
        int n = p->item[i].desc ? uk_lines(p->item[i].desc, text_w) : 0;
        if (n > foot_lines) foot_lines = n;
    }
    if (foot_lines > 3) foot_lines = 3;
    ML_Rect area = ml_area();
    int head = uk_title_h() + UK_BAND;
    int rows_h = ml_list_height(p->n);
    int pad = UK_INSET;
    int foot_h = UK_BAND + 2 * pad + foot_lines * lh - 2;
    int max_h = area.h - 2 * ml_space();
    if (head + rows_h + foot_h > max_h) {
        // A long page: the full height, and the description as few lines as fit.
        max_h = area.h;
        pad = ML_PAD;
        if (foot_lines > 2) foot_lines = 2;       // the description keeps two lines; the rows scroll
        foot_h = UK_BAND + 2 * pad + foot_lines * lh - 2;
        if (head + rows_h + foot_h > max_h) rows_h = ml_list_height(ml_list_fit(max_h - head - foot_h));
    }
    ML_Rect b = uk_inlay(w, head + rows_h + foot_h, path, right_title);
    ml_list_draw(b.x, b.y, b.w, rows_h, p->n, cursor,
                 row_fn ? row_fn : page_row, row_fn ? row_ctx : (void *)p, touch_list, uk_ink());
    int fy = b.y + rows_h;
    lattice_band_h(b.x, fy, b.w, UK_BAND);
    const char *d = (cursor >= 0 && cursor < p->n) ? p->item[cursor].desc : NULL;
    Color fg = (cursor >= 0 && cursor < p->n && !p->item[cursor].enabled) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
    char line[160];
    int ty = fy + UK_BAND + pad;
    for (int i = 0; d && *d && i < foot_lines; i++) {
        if (bfont_take_line(&d, text_w, line, (int)sizeof line) <= 0) break;
        bfont_draw(line, b.x + UK_INSET, ty, fg);
        ty += lh;
    }
    (void)GH;
}

// ---- the game menu ------------------------------------------------------------------

enum { ACT_CONTROLS = GM_ACT_USER, ACT_NEW, ACT_EXIT,
       ACT_SLOT = GM_ACT_USER + 100,      // + slot index
       ACT_CHEAT = GM_ACT_USER + 200 };   // + cheat index

typedef enum { ASK_NONE = 0, ASK_OVERWRITE, ASK_LOAD, ASK_EXIT } GmAsk;

static struct {
    GmPageId page[GM_DEPTH_MAX];
    int      cursor[GM_DEPTH_MAX];
    int      depth;
    bool     debug;
    SlotSet  slots;
    GmAsk    ask, asked;
    int      ask_slot;
    GmDo     act;
    int      act_slot;
    int      cheat;
} gm = { .cheat = -1 };

static bool s_quit_after_save;

void modern_gamemenu_open_save(bool then_quit) {
    // Straight to the save slots (Back closes the menu), and remember whether
    // this save was asked for by Save and Quit.
    gm.page[0] = GM_PAGE_SAVE;
    gm.cursor[0] = 0;
    gm.depth = 1;
    s_quit_after_save = then_quit;
}

bool modern_gamemenu_take_quit_after_save(void) {
    bool q = s_quit_after_save;
    s_quit_after_save = false;
    return q;
}

void modern_gamemenu_open(bool debug) {
    s_quit_after_save = false;
    memset(gm.page, 0, sizeof gm.page);
    memset(gm.cursor, 0, sizeof gm.cursor);
    gm.depth = 1;
    gm.debug = debug;
    gm.ask = gm.asked = ASK_NONE;
    gm.act = GM_DO_NONE;
    saveslots_scan(&gm.slots);
}

static void add(GmPage *p, const char *label, const char *desc, const char *reason,
                const char *shortcut, int key, bool enabled) {
    if (p->n >= GM_ROWS_MAX) return;
    p->item[p->n++] = (GmItem){ label, enabled ? desc : reason, shortcut, key, enabled };
}

void modern_gamemenu_page(const Game *g, GmPageId id, GmPage *p) {
    const ResUI *ui = &g->res->ui;
    const ResBanners *bn = &g->res->banners;
    memset(p, 0, sizeof *p);
    switch (id) {
    case GM_PAGE_ROOT:
        p->title = ui->gm_title;
        add(p, ui->gm_hero,  bn->gmd_hero,  NULL, "", GM_ACT_PAGE + GM_PAGE_HERO, true);
        add(p, ui->gm_world, bn->gmd_world, NULL, "", GM_ACT_PAGE + GM_PAGE_WORLD, true);
        add(p, ui->gm_game,  bn->gmd_game,  NULL, "", GM_ACT_PAGE + GM_PAGE_GAME, true);
        add(p, ui->gm_back,  bn->gmd_back,  NULL, "", GM_ACT_BACK, true);
        break;
    case GM_PAGE_HERO: {
        bool troops = false;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (g->army[i].id[0] && g->army[i].count > 0) troops = true;
        p->title = ui->gm_hero;
        add(p, ui->gm_army,      bn->gmd_army,      NULL, "A", KEY_A, true);
        add(p, ui->gm_character, bn->gmd_character, NULL, "V", KEY_V, true);
        add(p, ui->gm_contract,  bn->gmd_contract,  NULL, "I", KEY_I, true);
        add(p, ui->gm_puzzle,    bn->gmd_puzzle,    NULL, "P", KEY_P, true);
        add(p, ui->gm_dismiss,   bn->gmd_dismiss,   bn->gmr_no_troops, "D", KEY_D, troops);
        add(p, ui->gm_back,      bn->gmd_back_up,   NULL, "", GM_ACT_BACK, true);
        break;
    }
    case GM_PAGE_WORLD: {
        bool flying = g->character.mount == MOUNT_FLY;
        bool sailing = g->character.mount == MOUNT_SAIL;
        p->title = ui->gm_world;
        add(p, ui->gm_map,    bn->gmd_map,    NULL, "M", KEY_M, true);
        add(p, ui->gm_cast,   bn->gmd_cast,   NULL, "U", KEY_U, true);
        add(p, ui->gm_search, bn->gmd_search, NULL, "S", KEY_S, true);
        if (flying) add(p, ui->gm_land, bn->gmd_land, NULL, "L", KEY_L, true);
        else        add(p, ui->gm_fly,  bn->gmd_fly,  NULL, "F", KEY_F, true);
        add(p, ui->gm_end_week, bn->gmd_end_week, NULL, "W", KEY_W, true);
        add(p, ui->gm_rest,     bn->gmd_rest,     NULL, "5", KEY_KP_5, true);
        add(p, ui->gm_sail,     bn->gmd_sail,     bn->gmr_not_sailing, "N", KEY_N, sailing);
        add(p, ui->gm_back,     bn->gmd_back_up,  NULL, "", GM_ACT_BACK, true);
        break;
    }
    case GM_PAGE_GAME:
        p->title = ui->gm_game;
        if (gm.debug) add(p, ui->gm_debug, bn->gmd_debug, NULL, "", GM_ACT_PAGE + GM_PAGE_DEBUG, true);
        add(p, ui->gm_save,     bn->gmd_save,     NULL, "", GM_ACT_PAGE + GM_PAGE_SAVE, true);
        add(p, ui->gm_load,     bn->gmd_load,     bn->gmr_no_saves, "", GM_ACT_PAGE + GM_PAGE_LOAD,
            gm.slots.existing > 0);
        add(p, ui->gm_controls, bn->gmd_controls, NULL, "C", ACT_CONTROLS, true);
        add(p, ui->gm_new_game, bn->gmd_new_game, NULL, "", ACT_NEW, true);
        add(p, ui->gm_back,     bn->gmd_back_up,  NULL, "", GM_ACT_BACK, true);
        add(p, ui->gm_exit,     bn->gmd_exit,     NULL, "", ACT_EXIT, true);   // always last
        break;
    case GM_PAGE_SAVE:
    case GM_PAGE_LOAD:
        p->title = id == GM_PAGE_SAVE ? ui->gm_save : ui->gm_load;
        for (int i = 0; i < MODERN_SAVE_SLOTS; i++)
            add(p, "", id == GM_PAGE_SAVE ? bn->gmd_save : bn->gmd_load, bn->gmr_empty_slot, "",
                ACT_SLOT + i, id == GM_PAGE_SAVE || gm.slots.hdrs[i].exists);
        add(p, ui->gm_back, bn->gmd_back_up, NULL, "", GM_ACT_BACK, true);
        break;
    case GM_PAGE_DEBUG:
        p->title = ui->gm_debug;
        for (int i = 0; i < CHEAT_COUNT; i++)
            add(p, cheat_label((CheatAction)i), cheat_desc((CheatAction)i), NULL, "", ACT_CHEAT + i, true);
        add(p, ui->gm_back, bn->gmd_back_up, NULL, "", GM_ACT_BACK, true);
        break;
    }
}

void modern_gamemenu_gallery(int n, const GmPageId *pages, int cursor) {
    gm.depth = 0;
    for (int i = 0; i < n && i < GM_DEPTH_MAX; i++) { gm.page[i] = pages[i]; gm.cursor[i] = 0; gm.depth++; }
    if (gm.depth < 1) { gm.page[0] = GM_PAGE_ROOT; gm.depth = 1; }
    gm.cursor[gm.depth - 1] = cursor;
}

static void go_back(void) {
    if (gm.depth > 1) gm.depth--;
    else              views_dismiss();
}

void modern_gamemenu_update(Game *g) {
    if (!g || !g->res || gm.depth < 1) return;
    int d = gm.depth - 1;
    GmPage p;
    modern_gamemenu_page(g, gm.page[d], &p);
    GmEvent ev = gm_page_input(&p, &gm.cursor[d], TOUCH_LIST_MENU);
    if (ev == GM_EV_BACK) { go_back(); return; }
    if (ev != GM_EV_ACT) return;
    int key = p.item[gm.cursor[d]].key;
    if (key == GM_ACT_BACK) {
        go_back();
    } else if (key >= GM_ACT_PAGE && key < GM_ACT_USER) {
        if (gm.depth < GM_DEPTH_MAX) {
            gm.page[gm.depth] = (GmPageId)(key - GM_ACT_PAGE);
            gm.cursor[gm.depth] = 0;
            gm.depth++;
        }
    } else if (key >= ACT_CHEAT) {
        gm.cheat = key - ACT_CHEAT;
        views_dismiss();
    } else if (key >= ACT_SLOT) {
        int slot = key - ACT_SLOT;
        if (gm.page[d] == GM_PAGE_LOAD)      { gm.ask = ASK_LOAD; gm.ask_slot = slot; }
        else if (gm.slots.hdrs[slot].exists) { gm.ask = ASK_OVERWRITE; gm.ask_slot = slot; }
        else                                 { gm.act = GM_DO_SAVE; gm.act_slot = slot; }
    } else if (key == ACT_CONTROLS) {
        views_push(VIEW_CONTROLS);           // over the menu: closing it comes back here
    } else if (key == ACT_NEW) {
        gm.act = GM_DO_NEW;
    } else if (key == ACT_EXIT) {
        gm.ask = ASK_EXIT;
    } else {
        // A Hero or World row: close, then press its key next frame -- the
        // exact path the keypress runs (src/shell_actions.c).
        views_dismiss();
        input_host_inject_key_next_frame(key);
    }
}

bool modern_gamemenu_take_confirm(const Game *g, char *body, int cap) {
    if (gm.ask == ASK_NONE) return false;
    const ResBanners *bn = &g->res->banners;
    char sb[16];
    snprintf(sb, sizeof sb, "%d", gm.ask_slot + 1);
    ResTemplateVar v[] = { { "SLOT", sb } };
    resources_format_template(body, cap, gm.ask == ASK_OVERWRITE ? bn->gmc_overwrite
                                         : gm.ask == ASK_LOAD ? bn->gmc_load : bn->gmc_exit, v, 1);
    gm.asked = gm.ask;
    gm.ask = ASK_NONE;
    return true;
}

void modern_gamemenu_confirm_yes(void) {
    gm.act = gm.asked == ASK_OVERWRITE ? GM_DO_SAVE : gm.asked == ASK_LOAD ? GM_DO_LOAD
           : gm.asked == ASK_EXIT ? GM_DO_EXIT : GM_DO_NONE;
    gm.act_slot = gm.ask_slot;
    gm.asked = ASK_NONE;
}

GmDo modern_gamemenu_take_action(int *slot) {
    GmDo d = gm.act;
    if (slot) *slot = gm.act_slot;
    gm.act = GM_DO_NONE;
    if (d == GM_DO_SAVE) saveslots_scan(&gm.slots);
    return d;
}

int modern_gamemenu_take_cheat(void) {
    int c = gm.cheat;
    gm.cheat = -1;
    return c;
}

// Slot pages: the rows carry the save headers ("1  Name  Rank  600d").
static bool slot_row(void *ctx, int i, char *label, char *right, int cap) {
    const GmPage *p = (const GmPage *)ctx;
    if (i >= MODERN_SAVE_SLOTS) return page_row(ctx, i, label, right, cap);
    saveslots_row(&gm.slots, i, label, right, cap);
    return p->item[i].enabled;
}

void modern_gamemenu_draw(const Game *g) {
    if (!g || !g->res || gm.depth < 1) return;
    int d = gm.depth - 1;
    GmPage p;
    modern_gamemenu_page(g, gm.page[d], &p);
    // The path: "Menu > Game > Save".
    char path[128] = "";
    for (int i = 0; i < gm.depth; i++) {
        GmPage pi;
        modern_gamemenu_page(g, gm.page[i], &pi);
        size_t n = strlen(path);
        snprintf(path + n, sizeof path - n, "%s%s", i ? " > " : "", pi.title ? pi.title : "");
    }
    const ResZone *z = resources_zone_by_id(g->res, g->position.zone);
    bool slots = gm.page[d] == GM_PAGE_SAVE || gm.page[d] == GM_PAGE_LOAD;
    int cursor = gm.cursor[d] < p.n ? gm.cursor[d] : p.n - 1;
    ML_Rect r = ml_full();
    // One width for the whole menu (every page but the save slots, which are
    // wider rows), so it keeps its size as you go in and out of its pages.
    int menu_w = 400;
    for (int id = GM_PAGE_ROOT; id <= GM_PAGE_DEBUG; id++) {
        if (id == GM_PAGE_SAVE || id == GM_PAGE_LOAD || (id == GM_PAGE_DEBUG && !gm.debug)) continue;
        GmPage pi;
        modern_gamemenu_page(g, (GmPageId)id, &pi);
        int pw = gm_page_width(&pi, path);
        if (pw > menu_w) menu_w = pw;
    }
    gm_draw_page(&p, path, z ? z->name : "", r.x, r.y, slots ? 656 : menu_w, r.h, 0, cursor, TOUCH_LIST_MENU,
                 slots ? slot_row : NULL, &p);
}
