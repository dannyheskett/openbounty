// src/modern/views_render.c
//
// The detail views for a pack that declared render.mode "modern", each a page
// of the page engine (src/modern/page.h): the sheets to read -- the hero, the
// army, the contract, the puzzle -- and the pages to choose on -- the world
// map, the spells and the gate.
//
// The DOS original's views are in src/legacy/views_render.c and are frozen.
//
// Called only through the dispatcher in src/views_render.c.

#include "views.h"
#include "gfx.h"
#include "views_render_impl.h"
#include "modern/mlayout.h"
#include "lattice.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include "modern/page.h"
#include "modern/gamemenu.h"
#include "map_render.h"
#include "present.h"
#include "touch.h"
#include "uitouch.h"
#include "select.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "ui.h"
#include "views.h"
#include "tile_cache.h"
#include "tilevar.h"
#include "tables.h"
#include "input_host.h"
#include "views_render.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define GW  BFONT_GLYPH_W
#define GH  BFONT_GLYPH_H

// ---------------------------------------------------------------------------
//  THE HERO -- the class portrait, the numbers in two headed columns, the
//  sacred artifacts and the continents, and the pack's honours
// ---------------------------------------------------------------------------

// A label in gold and its value in white at the column's right edge.
static void cv_row(const char *label, const char *value, int x, int w, int y) {
    bfont_draw(label, x, y, PAL_CLR(YELLOW));
    bfont_draw(value, x + w - bfont_text_width(value), y, PAL_CLR(WHITE));
}

// An icon slot: the icon when held; else its ghost, dark, so the set reads
// as a collection with pieces still to find.
static void cv_icon(Texture2D tex, bool have, int x, int y, int size) {
    gfx_rect(x, y, size, size, PAL_CLR(BLACK));
    if (tex.id) {
        Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
        Rectangle dst = { (float)x, (float)y, (float)size, (float)size };
        gfx_texture_draw(tex, src, dst, have ? WHITE : uk_ghost());
    }
    gfx_rect_lines(x, y, size, size, have ? uk_edge() : uk_edge_dim());
}

static void draw_character(const Game *g, const Sprites *s) {
    const ResUI *ui = &g->res->ui;
    char buf[96], nb[32], mb[32];
    // The strip carries the name and Close; the next rank is told under the
    // numbers at the left.
    snprintf(buf, sizeof buf, "%s the %s", g->character.name, g->character.cls.rank_title);
    const ML_Rect r = page_sheet(buf, NULL, KEY_ESCAPE);
    const int pad = UK_INSET, BAND = UK_BAND, THIN = 2;
    const int line = uk_line_h(), head = uk_line_h();
    const int tile = CL_TILE_W;
    const ClassDef *cls = class_by_id(g->character.cls.id);
    int rank = g->character.cls.rank_index;
    char right[96];
    if (cls && rank + 1 < cls->rank_count) {
        int need = cls->ranks[rank + 1].villains_needed - GameVillainsCaught(g);
        snprintf(nb, sizeof nb, "%d", need > 0 ? need : 0);
        ResTemplateVar v[] = { { "RANK", cls->ranks[rank + 1].name }, { "COUNT", nb } };
        resources_format_template(right, sizeof right, ui->cv_next, v, 2);
    } else {
        snprintf(right, sizeof right, "%s", ui->cv_top_rank);
    }
    int top = r.y;

    // The portrait at its authored size.
    Texture2D portrait = (cls && cls->index >= 0 && cls->index < s->class_count) ? s->class_portrait[cls->index]
                                                                                  : (Texture2D){ 0 };
    int pw = portrait.id ? portrait.width : 192, ph = portrait.id ? portrait.height : 204;
    if (portrait.id) ui_blit(portrait, r.x, top, pw, ph);
    else gfx_rect(r.x, top, pw, ph, PAL_CLR(BLACK));
    lattice_band_v(r.x + pw, top, BAND, ph);

    // Two columns of numbers under their headings.
    int cx = r.x + pw + BAND, cw = (r.x + r.w - cx) / 2;
    int lx = cx + pad, lw = cw - 2 * pad;
    int rx = cx + cw + pad, rw = r.x + r.w - rx - pad;
    lattice_band_v(cx + cw - 2, top, BAND, ph);
    int y = top + pad;
    bfont_draw(ui->cv_army, lx, y, PAL_CLR(YELLOW));                      y += head;
    snprintf(nb, sizeof nb, "%d", g->stats.leadership_current);  cv_row(ui->cv_leadership, nb, lx, lw, y); y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.commission_weekly);   cv_row(ui->cv_commission, nb, lx, lw, y); y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.gold);                cv_row(ui->cv_gold, nb, lx, lw, y);       y += line + line / 2;
    bfont_draw(ui->cv_magic, lx, y, PAL_CLR(YELLOW));                     y += head;
    snprintf(nb, sizeof nb, "%d", g->stats.spell_power);         cv_row(ui->cv_spell_power, nb, lx, lw, y); y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.max_spells);          cv_row(ui->cv_spell_capacity, nb, lx, lw, y); y += line + line / 2;
    uk_flow(lx, y, lw, lx, 0, top + ph, right, PAL_CLR(WHITE));

    int total_v = g->res->villains_count;
    int total_a = artifacts_count() < 8 ? artifacts_count() : 8;
    y = top + pad;
    bfont_draw(ui->cv_campaign, rx, y, PAL_CLR(YELLOW));                  y += head;
    snprintf(nb, sizeof nb, "%d/%d", GameVillainsCaught(g), total_v); cv_row(ui->cv_captured, nb, rx, rw, y);  y += line;
    snprintf(nb, sizeof nb, "%d/%d", GameArtifactsFound(g), total_a); cv_row(ui->cv_artifacts, nb, rx, rw, y); y += line;
    snprintf(nb, sizeof nb, "%d", GameCastlesOwned(g));          cv_row(ui->cv_castles, nb, rx, rw, y);    y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.followers_killed);    cv_row(ui->cv_followers, nb, rx, rw, y);  y += line;
    snprintf(nb, sizeof nb, "%d", GameComputeScore(g));          cv_row(ui->cv_score, nb, rx, rw, y);      y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.days_left);           cv_row(ui->cv_days, nb, rx, rw, y);

    // The sacred artifacts: eight icons across at 1x; one still to find is a ghost.
    y = top + ph;
    lattice_band_h(r.x, y, r.w, THIN);
    y += THIN;
    int ax = r.x + (r.w - 8 * tile) / 2;
    bfont_draw(ui->cv_sacred, ax, y + 1, PAL_CLR(YELLOW));
    y += head;
    for (int i = 0; i < 8; i++)
        cv_icon(i < s->view_icon_count ? s->view_icon[i] : (Texture2D){ 0 }, i < total_a && g->artifacts.found[i], ax + i * tile, y, tile);
    y += tile;

    // The continents, and beside them the pack's honours.
    lattice_band_h(r.x, y, r.w, THIN);
    y += THIN;
    bfont_draw(ui->cv_continents, ax, y + 1, PAL_CLR(YELLOW));
    int nz = g->res->zone_count < 4 ? g->res->zone_count : 4;
    const ResEconomy *ec = &g->res->economy;
    bool honours = ec->audiences || ec->rites_per_zone;
    int hx = ax + 4 * tile + BAND + pad;
    if (honours) bfont_draw(ui->cv_honours, hx, y + 1, PAL_CLR(YELLOW));
    y += head;
    for (int i = 0; i < 4; i++)
        cv_icon(i < nz && s->view_icon_extra_base + i < s->view_icon_count
                    ? s->view_icon[s->view_icon_extra_base + i] : (Texture2D){ 0 }, i < nz && g->world.zones_discovered[i],
                ax + i * tile, y, tile);
    if (honours) {
        lattice_band_v(ax + 4 * tile, y - head, BAND, head + tile);
        int hw = r.x + r.w - pad - hx, hy = y + 4;
        if (ec->audiences) {
            snprintf(mb, sizeof mb, "%s", g->stats.blessed ? ui->cv_yes : ui->cv_no);
            cv_row(ui->cv_blessed, mb, hx, hw, hy);
            hy += line;
            snprintf(nb, sizeof nb, "%d", g->stats.tributes);
            cv_row(ui->cv_tributes, nb, hx, hw, hy);
            hy += line;
        }
        if (ec->rites_per_zone) {
            char rites[160];
            size_t off = (size_t)snprintf(rites, sizeof rites, "%s:", ui->cv_rites);
            int shown = 0;
            for (int i = 0; i < g->res->zone_count && i < g->world.zone_count; i++) {
                if (!g->world.zone_rites[i]) continue;
                const char *zn = g->res->zones[i].name[0] ? g->res->zones[i].name : g->res->zones[i].id;
                off += (size_t)snprintf(rites + off, sizeof rites - off, "%s %s", shown ? "," : "", zn);
                if (off >= sizeof rites) break;
                shown++;
            }
            if (shown) uk_flow(hx, hy, hw, hx, 0, y + tile, rites, PAL_CLR(WHITE));
        }
    }
}

// ---------------------------------------------------------------------------
//  THE ARMY -- five rows, one troop each
// ---------------------------------------------------------------------------

// Compare this slot's group against
// every other occupied slot. Any 'L' => Low; all 'H' => High; else Normal.
// Single-stack armies always report High. Labels come from res.ui.morale_*.
static const char *army_slot_morale(const Game *g, int slot) {
    const ResUI *ui = &g->res->ui;
    const TroopDef *me = troop_by_id(g->army[slot].id);
    if (!me) return ui->morale_normal;

    int others = 0, low = 0, high = 0;
    for (int j = 0; j < GAME_ARMY_SLOTS; j++) {
        if (j == slot) continue;
        if (!g->army[j].id[0] || g->army[j].count == 0) continue;
        const TroopDef *o = troop_by_id(g->army[j].id);
        if (!o) continue;
        others++;
        char r = morale_result(me->morale_group, o->morale_group);
        if (r == 'L') low++;
        else if (r == 'H') high++;
    }
    if (others == 0)    return ui->morale_high;
    if (low > 0)        return ui->morale_low;
    if (high == others) return ui->morale_high;
    return ui->morale_normal;
}

static void draw_army(const Game *g, const Sprites *s) {
    // The title strip with Close, then five rows: each troop's portrait at
    // 1x (cut to its row), its name and how many, its morale at the right,
    // and its numbers in aligned columns under them. An empty slot is an
    // empty row.
    const ResUI *ui = &g->res->ui;
    char gold[48];
    uk_gold_text(g, gold, sizeof gold);
    ML_Rect r = page_sheet(ui->menu_army, gold, KEY_ESCAPE);
    const int row = r.h / 5, lh = uk_line_h();
    int col[3] = { 0, 0, 0 };
    int tx0 = r.x + CL_TILE_W + 2 + UK_INSET;
    int cw = (r.x + r.w - UK_INSET - tx0) / 3;
    for (int k = 0; k < 3; k++) col[k] = tx0 + k * cw;
    const int mark = views_army_marked();
    for (int i = 0; i < 5; i++) {
        int ry = r.y + i * row;
        if (i > 0) lattice_band_h(r.x, ry - 1, r.w, 2);
        bool filled = g->army[i].id[0] && g->army[i].count != 0;
        const TroopDef *t = filled ? troop_by_id(g->army[i].id) : NULL;
        if (!t) continue;   // an empty slot: nothing to draw
        Texture2D face = s->troop_portrait[t->index].id ? s->troop_portrait[t->index] : s->troop_sprite[t->index];
        uk_picture_cut(face, r.x + 1, ry + 1, CL_TILE_W, row - 3);
        int ty = ry + (row - 3 * lh) / 2 + 2;
        char buf[96];
        snprintf(buf, sizeof buf, "%d %s", g->army[i].count, t->name);
        int free_lead = g->stats.leadership_current - t->hit_points * g->army[i].count;
        const char *mor = free_lead < 0 ? ui->out_of_control : NULL;
        char mbuf[64];
        if (!mor) { snprintf(mbuf, sizeof mbuf, "%s%s", ui->army_morale, army_slot_morale(g, i)); mor = mbuf; }
        const char *mw = army_slot_morale(g, i);
        Color mc = free_lead < 0 ? PAL_CLR(RED)
                 : strcmp(mw, ui->morale_low) == 0 ? PAL_CLR(RED)
                 : strcmp(mw, ui->morale_high) == 0 ? PAL_CLR(GREEN) : PAL_CLR(WHITE);
        int mwid = bfont_text_width(mor);
        bfont_draw_right(mor, r.x + r.w - UK_INSET, ty, mc);
        uk_line(buf, tx0, ty, r.x + r.w - UK_INSET - mwid - UK_INSET - tx0, PAL_CLR(YELLOW));
        ty += lh;
        struct { const char *l; char v[32]; } cell[6];
        cell[0].l = ui->army_skill;      snprintf(cell[0].v, 32, "%d", t->skill_level);
        cell[1].l = ui->army_move;       snprintf(cell[1].v, 32, "%d", t->move_rate);
        cell[2].l = ui->army_g_cost;     snprintf(cell[2].v, 32, "%d", (t->recruit_cost / 10) * g->army[i].count);
        cell[3].l = ui->army_hit_points; snprintf(cell[3].v, 32, "%d", t->hit_points * g->army[i].count);
        cell[4].l = ui->army_damage;     snprintf(cell[4].v, 32, "%d-%d", t->melee_min * g->army[i].count,
                                                  t->melee_max * g->army[i].count);
        cell[5].l = "";                  cell[5].v[0] = '\0';
        for (int k = 0; k < 6; k++) {
            int cx = col[k % 3], cy = ty + (k / 3) * lh;
            if (!cell[k].l[0] && !cell[k].v[0]) continue;
            // The value one space after the column's longer label.
            int lw = bfont_text_width(cell[k % 3].l), lw2 = bfont_text_width(cell[k % 3 + 3].l);
            bfont_draw(cell[k].l, cx, cy, PAL_CLR(YELLOW));
            bfont_draw(cell[k].v, cx + (lw > lw2 ? lw : lw2) + GW, cy, PAL_CLR(WHITE));
        }
        // Opened from a fight: the troop whose turn it is, ringed in gold.
        if (t->index == mark) {
            gfx_rect_lines(r.x, ry + 1, r.w, row - 3, PAL_CLR(YELLOW));
            gfx_rect_lines(r.x + 1, ry + 2, r.w - 2, row - 5, PAL_CLR(YELLOW));
        }
    }
}

// ---------------------------------------------------------------------------
//  THE CONTRACT
// ---------------------------------------------------------------------------

static void draw_contract(const Game *g, const Sprites *s) {
    views_contract_set_active(g && g->contract.active_id[0] != '\0');
    const ResUI *ui = &g->res->ui;
    const ResBanners *bn = &g->res->banners;
    const int size = 2 * CL_TILE_W;
    const VillainDef *v = g->contract.active_id[0] ? villain_by_id(g->contract.active_id) : NULL;
    if (!v) {
        // No contract: the empty silhouette at 2x and where to get one.
        const ML_Rect r = page_sheet(ui->cv_title_no_contract, NULL, KEY_ESCAPE);
        int by = r.y + (r.h - size - 3 * uk_line_h()) / 2;
        uk_picture(s ? s->hud_contract_silhouette : (Texture2D){ 0 }, r.x + (r.w - size) / 2, by, size, size);
        int tw = r.w - 2 * UK_INSET, ty = by + size + UK_INSET;
        const char *p = bn->cv_no_contract_hint;
        char line[160];
        while (*p && bfont_take_line(&p, tw, line, (int)sizeof line) > 0) {
            bfont_draw_centered(line, r.x + r.w / 2, ty, PAL_CLR(WHITE));
            ty += uk_line_h();
        }
        return;
    }
    const ResVillainDesc *vd = resources_villain_desc(g->res, v->id);
    char title[96], reward[64], rb[16];
    ResTemplateVar tv[] = { { "NAME", v->name } };
    resources_format_template(title, sizeof title, bn->cv_wanted, tv, 1);
    snprintf(rb, sizeof rb, "%d", v->reward);
    ResTemplateVar rv[] = { { "VALUE", rb } };
    resources_format_template(reward, sizeof reward, ui->cv_label_reward, rv, 1);
    const ML_Rect r = page_sheet(title, reward, KEY_ESCAPE);

    Texture2D face = { 0 };
    if (v->index >= 0 && v->index < s->villain_count) {
        face = sprites_strip(s->villain_anim[v->index], s->villain_anim_frames[v->index],
                             (int)(ui_anim_time() * UK_FACE_FPS));
        if (!face.id) face = s->villain_portrait[v->index];
    }
    ML_Rect a = { r.x + UK_INSET, r.y + UK_INSET, r.w - 2 * UK_INSET, r.h - 2 * UK_INSET };
    uk_picture(face, a.x, a.y, size, size);

    UkDoc d = { 0 };
    uk_doc_labeled(&d, ui->cv_label_name, v->name);
    uk_doc_labeled(&d, ui->cv_label_alias, (vd && vd->alias[0]) ? vd->alias : ui->cv_alias_none);
    const ResZone *vz = resources_zone_by_id(g->res, v->zone);
    uk_doc_labeled(&d, ui->cv_label_last_seen, (vz && vz->name[0]) ? vz->name : v->zone);
    const char *castle_name = ui->cv_castle_unknown;
    for (int i = 0; i < g->castle_count; i++) {
        if (!g->castles[i].id[0] || g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
        if (strcmp(g->castles[i].villain_id, v->id) != 0) continue;
        if (g->castles[i].known) {
            const ResCastle *rc = resources_castle_by_id(g->res, g->castles[i].id);
            castle_name = (rc && rc->name[0]) ? rc->name : g->castles[i].id;
        }
        break;
    }
    uk_doc_labeled(&d, ui->cv_label_castle, castle_name);
    if (vd && vd->features[0]) {
        uk_doc_gap(&d);
        uk_doc_add(&d, ui->cv_features_header, PAL_CLR(YELLOW));
        uk_doc_add(&d, vd->features, PAL_CLR(WHITE));
    }
    if (vd && vd->crimes[0]) {
        uk_doc_gap(&d);
        uk_doc_add(&d, ui->cv_crimes_header, PAL_CLR(YELLOW));
        uk_doc_add(&d, vd->crimes, PAL_CLR(WHITE));
    }
    // One column beside the portrait all the way down, never under it.
    uk_doc_draw(&d, a, size, a.h, -1, true);
}

// ---------------------------------------------------------------------------
//  THE PUZZLE -- the 5x5 grid of tiles at the left, what it is beside it
// ---------------------------------------------------------------------------

// Position -> entity mapping lives in the engine (tables.c PUZZLE_GRID,
// accessor puzzle_grid_entity): negative values are artifact-index-minus-one
// (-1 = artifact 0, ...), non-negative values are villain indices.

// Lazy-loaded scepter-zone map. The puzzle-view background shows the
// scepter location with its 5x5 surroundings revealed cell-by-cell
// (). We load the scepter's continent into a
// scratch Map the first time the puzzle view opens; reload only if
// the scepter zone changes.
static Map  s_puzzle_scepter_map;
static char s_puzzle_scepter_zone[24] = { 0 };
static bool s_puzzle_scepter_loaded = false;

static void puzzle_load_scepter_map(const Game *g) {
    if (!g || !g->scepter.zone[0]) {
        s_puzzle_scepter_loaded = false;
        s_puzzle_scepter_zone[0] = '\0';
        return;
    }
    if (s_puzzle_scepter_loaded &&
        strncmp(s_puzzle_scepter_zone, g->scepter.zone,
                sizeof s_puzzle_scepter_zone) == 0) return;
    if (!MapLoadZone(&s_puzzle_scepter_map, g->res, g->scepter.zone)) {
        s_puzzle_scepter_loaded = false;
        return;
    }
    snprintf(s_puzzle_scepter_zone, sizeof s_puzzle_scepter_zone,
             "%s", g->scepter.zone);
    s_puzzle_scepter_loaded = true;
}

static void draw_puzzle(const Game *g, const Sprites *s) {
    // The grid is five tiles at 1x, as tall as the page, so the strip with
    // Close spans the words beside it: what the puzzle is and how much of it
    // is uncovered.
    const ResUI *ui = &g->res->ui;
    ML_Rect grid;
    ML_Rect w = page_sheet_beside(5 * CL_TILE_W, ui->gm_puzzle, NULL, &grid);
    {
        int x = w.x + UK_INSET, ww = w.w - 2 * UK_INSET;
        int y = uk_flow(x, w.y + UK_INSET, ww, x, 0, w.y + w.h, g->res->banners.puzzle_legend, PAL_CLR(WHITE));
        char nb[32];
        int total_v = g->res->villains_count;
        int total_a = artifacts_count() < 8 ? artifacts_count() : 8;
        y += uk_line_h();
        snprintf(nb, sizeof nb, "%d/%d", GameVillainsCaught(g), total_v);
        bfont_draw(ui->cv_captured, x, y, PAL_CLR(YELLOW)); bfont_draw_right(nb, x + ww, y, PAL_CLR(WHITE));
        y += uk_line_h();
        snprintf(nb, sizeof nb, "%d/%d", GameArtifactsFound(g), total_a);
        bfont_draw(ui->cv_artifacts, x, y, PAL_CLR(YELLOW)); bfont_draw_right(nb, x + ww, y, PAL_CLR(WHITE));
    }

    // Each cell is one map tile, so the scepter's land under it lines up with
    // the tile grid; the 5x5 stands in the page's full height.
    int cell_w = CL_TILE_W;
    int cell_h = CL_TILE_H;
    int grid_x = grid.x;
    int grid_y = grid.y + (grid.h - cell_h * 5) / 2;

    puzzle_load_scepter_map(g);

    // Centre the 5x5 viewport on the scepter, clamped to map bounds.
    int cam_x = g->scepter.x - 2;
    int cam_y = g->scepter.y - 2;
    if (s_puzzle_scepter_loaded) {
        const Map *m = &s_puzzle_scepter_map;
        if (cam_x < 0) cam_x = 0;
        if (cam_y < 0) cam_y = 0;
        if (cam_x > m->width  - 5) cam_x = m->width  - 5;
        if (cam_y > m->height - 5) cam_y = m->height - 5;
    }

    // Reveal cells one by one in two passes: artifacts first, then villains,
    // row-major within each pass, 150 ms a cell.
    static double s_open_time = 0.0;
    static bool   s_prev_active = false;
    bool active = (views_active() == VIEW_PUZZLE);
    if (active && !s_prev_active) s_open_time = ui_anim_time();
    s_prev_active = active;
    double elapsed = ui_anim_time() - s_open_time;
    int reveal_step = (int)(elapsed / 0.150);
    int anim_tick = (int)(ui_anim_time() * UK_FACE_FPS);

    int seq[5][5];
    {
        int n = 0;
        for (int j = 0; j < 5; j++)
            for (int i = 0; i < 5; i++)
                if (puzzle_grid_entity(j, i) < 0) seq[j][i] = n++;
        for (int j = 0; j < 5; j++)
            for (int i = 0; i < 5; i++)
                if (puzzle_grid_entity(j, i) >= 0) seq[j][i] = n++;
    }

    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 5; i++) {
            int id = puzzle_grid_entity(j, i);
            int x = grid_x + i * cell_w;
            int y = grid_y + j * cell_h;
            bool caught = false;
            Texture2D face = { 0 };
            if (id < 0) {
                int artifact_id = -id - 1;
                caught = g->artifacts.found[artifact_id];
                if (artifact_id < s->view_icon_extra_base) face = s->view_icon[artifact_id];
            } else {
                caught = g->contract.villains_caught[id];
                if (id < s->villain_count) {
                    face = sprites_strip(s->villain_anim[id], s->villain_anim_frames[id], anim_tick);
                    if (!face.id) face = s->villain_portrait[id];
                }
            }
            if (reveal_step < seq[j][i]) caught = false;
            Rectangle dst = { (float)x, (float)y, (float)cell_w, (float)cell_h };
            if (caught) {
                // Uncovered: the scepter's land under the piece, drawn as the
                // map draws it.
                gfx_rect(x, y, cell_w, cell_h, PAL_CLR(BLACK));
                if (s_puzzle_scepter_loaded)
                    map_render_cell(&s_puzzle_scepter_map, cam_x + i, cam_y + j, dst);
            } else if (face.id) {
                gfx_texture_draw(face, (Rectangle){ 0, 0, (float)face.width, (float)face.height }, dst, WHITE);
            } else {
                gfx_rect(x, y, cell_w, cell_h, PAL_CLR(DGREY));
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  THE WORLD MAP -- the continent, and the places visited on it
// ---------------------------------------------------------------------------

// Convert a packed 0xAARRGGBB color from res->colors into a raylib Color.
static Color color_from_packed(unsigned int v) {
    return (Color){
        (unsigned char)((v >> 16) & 0xFF),
        (unsigned char)((v >>  8) & 0xFF),
        (unsigned char)( v        & 0xFF),
        (unsigned char)((v >> 24) & 0xFF),
    };
}

static Color terrain_minimap_color(const ResColors *col, Terrain t) {
    if (!col) {
        switch (t) {
            case TERRAIN_GRASS:    return PAL_CLR(GREEN);
            case TERRAIN_FOREST:   return PAL_CLR(DGREEN);
            case TERRAIN_MOUNTAIN: return PAL_CLR(BROWN);
            case TERRAIN_WATER:    return PAL_CLR(BLUE);
            case TERRAIN_RIVER:    return PAL_CLR(BLUE);
            case TERRAIN_DESERT:   return PAL_CLR(YELLOW);
            default:               return PAL_CLR(BLACK);
        }
    }
    switch (t) {
        case TERRAIN_GRASS:    return color_from_packed(col->minimap_grass);
        case TERRAIN_FOREST:   return color_from_packed(col->minimap_forest);
        case TERRAIN_MOUNTAIN: return color_from_packed(col->minimap_mountain);
        case TERRAIN_WATER:    return color_from_packed(col->minimap_water);
        case TERRAIN_RIVER:    return color_from_packed(col->minimap_water);
        case TERRAIN_DESERT:   return color_from_packed(col->minimap_desert);
        default:               return color_from_packed(col->minimap_fog);
    }
}

// . Without the crystal orb the minimap
// is fog-limited and SPACE does nothing. With the orb, SPACE toggles
// between fog-limited view ("your map") and the continent-wide reveal
// ("whole map"). Status bar hints switch based on whether the orb has
// been picked up in the current zone.
static int worldmap_current_zone_index(const Game *g) {
    if (!g || !g->res) return -1;
    for (int i = 0; i < g->res->zone_count; i++) {
        if (strcmp(g->res->zones[i].id, g->position.zone) == 0) return i;
    }
    return -1;
}

static bool worldmap_has_orb(const Game *g) {
    int zi = worldmap_current_zone_index(g);
    if (zi < 0 || zi >= g->world.zone_count) return false;
    return g->world.orbs_found[zi];
}

// The places list: "All of <continent>", then the towns and castles of this
// continent the hero has visited, then Back. Choosing a place zooms the map in
// on it.
typedef struct { char name[64]; int x, y; bool castle; } WmPlace;
static int s_wm_cursor;
static bool s_wm_open;

static int worldmap_places(const Game *g, WmPlace *out, int cap) {
    const Resources *r = g->res;
    int n = 0;
    for (int i = 0; i < r->town_count && n < cap; i++) {
        const ResTown *tw = &r->towns[i];
        if (strcmp(tw->zone, g->position.zone) != 0 || tw->x < 0) continue;
        bool visited = false;
        for (int k = 0; k < g->town_count; k++)
            if (strcmp(g->towns[k].id, tw->id) == 0) { visited = g->towns[k].visited; break; }
        if (!visited) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", tw->name[0] ? tw->name : tw->id);
        out[n].x = tw->x; out[n].y = tw->y; out[n].castle = false; n++;
    }
    for (int i = 0; i < r->castle_count && n < cap; i++) {
        const ResCastle *c = &r->castles[i];
        if (strcmp(c->zone, g->position.zone) != 0 || c->x < 0) continue;
        const CastleRecord *cr = GameFindCastleConst(g, c->id);
        if (!cr || !cr->visited) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", c->name[0] ? c->name : c->id);
        out[n].x = c->x; out[n].y = c->y; out[n].castle = true; n++;
    }
    return n;
}

typedef struct { const Game *g; const WmPlace *p; int n; } WmRows;

static bool worldmap_row_fn(void *ctx, int i, char *label, char *right, int cap) {
    const WmRows *w = (const WmRows *)ctx;
    right[0] = '\0';
    if (i == w->n + 1) {                      // the page's exit, on the foot
        snprintf(label, (size_t)cap, "%s", w->g->res->ui.gm_close);
        ml_exit_hint(right);
        return true;
    }
    if (i == 0) {
        const ResZone *z = resources_zone_by_id(w->g->res, w->g->position.zone);
        ResTemplateVar v[] = { { "ZONE", (z && z->name[0]) ? z->name : w->g->position.zone } };
        resources_format_template(label, cap, w->g->res->banners.worldmap_all, v, 1);
    } else {
        snprintf(label, (size_t)cap, "%s", w->p[i - 1].name);
    }
    return true;
}

// The orb's row: the whole map, or yours.
static bool worldmap_orb_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)i;
    const Game *g = (const Game *)ctx;
    const ResUI *ui = &g->res->ui;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", views_render_worldmap_whole() ? ui->worldmap_row_your_map
                                                                     : ui->worldmap_row_whole_map);
    return true;
}

bool modern_worldmap_input(const Game *g) {
    if (views_active() != VIEW_WORLDMAP || !g || !g->res) { s_wm_open = false; return false; }
    if (!s_wm_open) { s_wm_open = true; s_wm_cursor = 0; }
    WmPlace places[64];
    int n = worldmap_places(g, places, 64) + 2;       // All, the places, Close
    int close_row = n - 1;
    MlList l = { n, s_wm_cursor, NULL, NULL };
    int row = -1;
    MlEvent ev = ml_list_input(&l, TOUCH_LIST_MENU, &row);
    s_wm_cursor = l.cursor;
    if (ev == ML_EV_BACK || (ev == ML_EV_ACT && row == close_row)) {
        views_dismiss(); s_wm_open = false; return true;
    }
    // With the orb, its row (or Space) swaps the whole map and yours.
    bool orb = worldmap_has_orb(g);
    if (orb && (touch_tapped_row(TOUCH_LIST_PROMPT) == 0 || input_key_pressed(KEY_SPACE)))
        views_render_worldmap_toggle_hero_only();
    return true;     // the view holds the keys; Escape or Close leaves
}

static void draw_worldmap(const Game *g, const Map *m, const Fog *f) {
    const ResBanners *bn = &g->res->banners;
    const Resources *r = g->res;
    const ResZone *z = resources_zone_by_id(r, g->position.zone);
    const ML_Rect page = page_full_body((z && z->name[0]) ? z->name : g->position.zone, NULL);
    if (!m || m->width <= 0 || m->height <= 0) return;
    bool orb = worldmap_has_orb(g);
    bool reveal_all = orb && views_render_worldmap_whole();

    WmPlace places[64];
    int np = worldmap_places(g, places, 64);
    int cursor = s_wm_open ? s_wm_cursor : 0;
    if (cursor > np + 1) cursor = np + 1;

    // The map at the left: the whole continent at the largest whole cell
    // size, or three times that over the chosen place.
    const int side_w = 16 * GW + 2 * UK_INSET;
    int map_w = page.w - side_w - UK_BAND;
    int avail_w = map_w - 2 * UK_INSET, avail_h = page.h - 2 * UK_INSET;
    int pix = (avail_w / m->width < avail_h / m->height) ? avail_w / m->width : avail_h / m->height;
    if (pix < 1) pix = 1;
    int cam_x = 0, cam_y = 0, cols = m->width, rows = m->height;
    const WmPlace *sel = (cursor > 0 && cursor <= np) ? &places[cursor - 1] : NULL;
    if (sel) {
        pix *= 3;
        cols = avail_w / pix; rows = avail_h / pix;
        if (cols > m->width) cols = m->width;
        if (rows > m->height) rows = m->height;
        cam_x = sel->x - cols / 2; cam_y = sel->y - rows / 2;
        if (cam_x > m->width - cols) cam_x = m->width - cols;
        if (cam_y > m->height - rows) cam_y = m->height - rows;
        if (cam_x < 0) cam_x = 0;
        if (cam_y < 0) cam_y = 0;
    }
    int grid_w = pix * cols, grid_h = pix * rows;
    int gx = page.x + (map_w - grid_w) / 2;
    int gy = page.y + (page.h - grid_h) / 2;
    gfx_rect(gx, gy, grid_w, grid_h, PAL_CLR(BLACK));
    const ResColors *mm_col = &g->res->colors;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            int mx = cam_x + x, my = cam_y + y;
            if (!reveal_all && !FogSeen(f, mx, my)) continue;
            const Tile *t = MapGetTile(m, mx, my);
            if (!t) continue;
            gfx_rect(gx + x * pix, gy + y * pix, pix, pix, terrain_minimap_color(mm_col, t->terrain));
        }
    }
    // A marker: a filled cell with a dark edge, so it reads on any terrain.
    #define MARK(mx, my, col) do { int _x = gx + ((mx) - cam_x) * pix, _y = gy + ((my) - cam_y) * pix; \
        gfx_rect(_x, _y, pix, pix, col); \
        if (pix >= 4) gfx_rect_lines(_x, _y, pix, pix, PAL_CLR(BLACK)); } while (0)
    #define IN_VIEW(px, py) ((px) >= cam_x && (px) < cam_x + cols && (py) >= cam_y && (py) < cam_y + rows)
    for (int i = 0; i < r->town_count; i++) {
        const ResTown *tw = &r->towns[i];
        if (strcmp(tw->zone, g->position.zone) != 0 || tw->x < 0 || !IN_VIEW(tw->x, tw->y)) continue;
        if (!reveal_all && !FogSeen(f, tw->x, tw->y)) continue;
        MARK(tw->x, tw->y, PAL_CLR(WHITE));
    }
    for (int i = 0; i < r->castle_count; i++) {
        const ResCastle *c = &r->castles[i];
        if (strcmp(c->zone, g->position.zone) != 0 || c->x < 0 || !IN_VIEW(c->x, c->y)) continue;
        if (!reveal_all && !FogSeen(f, c->x, c->y)) continue;
        MARK(c->x, c->y, PAL_CLR(RED));
    }
    unsigned k = (unsigned)(ui_anim_time() * 3.0);
    // The chosen place: a ring around it.
    if (sel) {
        int rx = gx + (sel->x - cam_x) * pix, ry = gy + (sel->y - cam_y) * pix;
        for (int t = 0; t < 3; t++)
            gfx_rect_lines(rx - pix - t, ry - pix - t, 3 * pix + 2 * t, 3 * pix + 2 * t,
                           t == 1 ? PAL_CLR(YELLOW) : PAL_CLR(BLACK));
    }
    // The boat, and the hero blinking, always.
    if (g->boat.has_boat && strcmp(g->boat.zone, g->position.zone) == 0 && IN_VIEW(g->boat.x, g->boat.y))
        MARK(g->boat.x, g->boat.y, PAL_CLR(CYAN));
    if (IN_VIEW(g->position.x, g->position.y))
        MARK(g->position.x, g->position.y, (k & 1) ? PAL_CLR(YELLOW) : PAL_CLR(MAGENTA));
    #undef IN_VIEW
    #undef MARK

    // The column at the right: where you and your boat are, the places, the
    // orb's row, and Close on the foot.
    int sx = page.x + map_w;
    lattice_band_v(sx, page.y, UK_BAND, page.h);
    int px = sx + UK_BAND, pw = page.x + page.w - px;
    const int lh = uk_line_h();
    char xb[12], yb[12], line[RES_BANNER_LEN];
    int ty = page.y + UK_INSET;
    snprintf(xb, sizeof xb, "%d", g->position.x);
    snprintf(yb, sizeof yb, "%d", g->position.y);
    ResTemplateVar yv[] = { { "X", xb }, { "Y", yb } };
    resources_format_template(line, sizeof line, bn->worldmap_you, yv, 2);
    uk_line(line, px + UK_INSET, ty, pw - 2 * UK_INSET, PAL_CLR(WHITE));
    ty += lh;
    if (!g->boat.has_boat) {
        snprintf(line, sizeof line, "%s", bn->worldmap_no_boat);
    } else if (strcmp(g->boat.zone, g->position.zone) != 0) {
        const ResZone *bz = resources_zone_by_id(r, g->boat.zone);
        ResTemplateVar bv[] = { { "ZONE", (bz && bz->name[0]) ? bz->name : g->boat.zone } };
        resources_format_template(line, sizeof line, bn->worldmap_boat_elsewhere, bv, 1);
    } else {
        snprintf(xb, sizeof xb, "%d", g->boat.x);
        snprintf(yb, sizeof yb, "%d", g->boat.y);
        ResTemplateVar bv[] = { { "X", xb }, { "Y", yb } };
        resources_format_template(line, sizeof line, bn->worldmap_boat, bv, 2);
    }
    uk_line(line, px + UK_INSET, ty, pw - 2 * UK_INSET, PAL_CLR(WHITE));
    ty += lh + UK_INSET;
    lattice_band_h(px, ty - UK_BAND, pw, UK_BAND);
    int foot = page.y + page.h;
    if (orb) {
        // The orb's row stands over the places' foot.
        int oy = foot - ml_list_height(1) - UK_BAND - ml_list_height(1);
        ml_list_draw(px, oy, pw, ml_list_height(1), 1, -1, worldmap_orb_row, (void *)g,
                     TOUCH_LIST_PROMPT, uk_ink());
        lattice_band_h(px, oy - UK_BAND, pw, UK_BAND);
        foot = oy - UK_BAND;
    }
    WmRows wr = { g, places, np };
    ML_Rect list = { px, ty, pw, page.y + page.h - ty };
    if (orb) {
        // The places and the orb's row, then Close alone on the foot.
        int close_y = page.y + page.h - ml_list_height(1);
        ml_list_draw_ex(px, ty, pw, foot - ty, np + 1, cursor <= np ? cursor : -1,
                        worldmap_row_fn, &wr, TOUCH_LIST_MENU, uk_ink(), 0);
        ml_list_draw_ex(px, close_y, pw, ml_list_height(1), 1, cursor == np + 1 ? 0 : -1,
                        worldmap_row_fn, &wr, TOUCH_LIST_MENU, uk_ink(), np + 1);
    } else {
        ml_rows_draw(list, np + 2, 1, cursor, worldmap_row_fn, &wr, TOUCH_LIST_MENU);
    }
}

// --gallery: the list's cursor.
void modern_worldmap_gallery(int cursor) { s_wm_open = true; s_wm_cursor = cursor; }

// ---------------------------------------------------------------------------
//  THE SPELLS -- one page on the map and in a fight
// ---------------------------------------------------------------------------

typedef struct { const Game *g; bool combat; } SpellsCtx;

// A spell's row: its name, the charges held at the right; it can be chosen
// where it can be cast.
static bool spell_row(void *ctx, int i, char *label, char *right, int cap) {
    const SpellsCtx *c = (const SpellsCtx *)ctx;
    const SpellDef *sp = spell_by_index(i);
    snprintf(label, (size_t)cap, "%s", sp ? sp->name : "");
    snprintf(right, 48, "%d", c->g->spells.counts[i]);
    return views_spell_castable(c->g, c->combat, i);
}

typedef struct { const char *label; } ExitCtx;

static bool exit_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)i;
    snprintf(label, (size_t)cap, "%s", ((const ExitCtx *)ctx)->label);
    ml_exit_hint(right);
    return true;
}

void modern_spells_draw(const Game *g, bool combat, int cur, const char *title, const char *right,
                        const char *exit_label) {
    // The two columns under their headings, what the spell under the cursor
    // does -- or why it cannot be cast here -- under them, and the exit on
    // the foot. Columns longer than their space scroll.
    const ResUI *ui = &g->res->ui;
    const ResBanners *bn = &g->res->banners;
    const int lh = uk_line_h();
    const ML_Rect r = page_full_body(title, right);
    int half = (r.w - UK_BAND) / 2;
    int hy = r.y + UK_INSET;
    uk_line(ui->sv_combat_col, r.x + UK_INSET, hy, half - 2 * UK_INSET, PAL_CLR(YELLOW));
    uk_line(ui->sv_adventure_col, r.x + half + UK_BAND + UK_INSET, hy, half - 2 * UK_INSET, PAL_CLR(YELLOW));
    int row_y = hy + lh + UK_INSET / 2;
    int exit_y = r.y + r.h - ml_list_height(1);
    const int desc_lines = 2;
    int desc_h = UK_BAND + 2 * UK_INSET + desc_lines * lh;
    int rows_h = exit_y - UK_BAND - desc_h - row_y;
    SpellsCtx c = { g, combat };
    ml_list_draw_ex(r.x, row_y, half, rows_h, 7, cur < 7 ? cur : -1,
                    spell_row, &c, TOUCH_LIST_SPELLS, uk_ink(), 0);
    lattice_band_v(r.x + half, r.y, UK_BAND, row_y + rows_h - r.y);
    ml_list_draw_ex(r.x + half + UK_BAND, row_y, r.w - half - UK_BAND, rows_h, 7,
                    cur >= 7 && cur < 14 ? cur - 7 : -1,
                    spell_row, &c, TOUCH_LIST_SPELLS, uk_ink(), 7);
    int fy = row_y + rows_h;
    lattice_band_h(r.x, fy, r.w, UK_BAND);
    // What the spell does: the pack's one-line brief, else its description,
    // else its lore; a spell not cast here says why.
    const SpellDef *sp = (cur >= 0 && cur < 14) ? spell_by_index(cur) : NULL;
    const char *desc = NULL;
    if (sp && (cur < 7) != combat)       desc = combat ? bn->gmr_spell_on_map : bn->gmr_spell_in_fight;
    else if (sp && g->spells.counts[cur] <= 0) desc = bn->gmr_no_spell_held;
    else if (sp) {
        desc = resources_spell_brief(g->res, sp->id);
        if (!desc || !desc[0]) desc = sp->description;
        if (!desc || !desc[0]) desc = resources_spell_lore(g->res, sp->id);
    }
    uk_lines_draw(desc, r.x + UK_INSET, fy + UK_BAND + UK_INSET, r.w - 2 * UK_INSET, desc_lines, PAL_CLR(WHITE));
    lattice_band_h(r.x, exit_y - UK_BAND, r.w, UK_BAND);
    ExitCtx ec = { exit_label };
    ml_list_draw_ex(r.x, exit_y, r.w, ml_list_height(1), 1, cur == 14 ? 0 : -1,
                    exit_row, &ec, TOUCH_LIST_SPELLS, uk_ink(), 14);
}

static void draw_spells(const Game *g) {
    // On the map the page is opened from the map: its exit is Close.
    char gold[48];
    uk_gold_text(g, gold, sizeof gold);
    modern_spells_draw(g, false, views_spells_cursor(), g->res->ui.sv_title, g->character.name,
                       g->res->ui.gm_close);
}

// ---------------------------------------------------------------------------
//  THE GATE -- where the Town or Castle Gate spell may take you
// ---------------------------------------------------------------------------

static bool gate_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)ctx;
    const GateDestination *d = views_gate_dest(i);
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", d ? d->name : "");
    return d != NULL;
}

typedef struct { const char *travel, *cancel; bool can_travel; } GateFoot;

static bool gate_foot_row(void *ctx, int i, char *label, char *right, int cap) {
    const GateFoot *f = (const GateFoot *)ctx;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", i == 0 ? f->travel : f->cancel);
    if (i == 1) ml_exit_hint(right);
    return i == 1 || f->can_travel;
}

// The destination's surroundings at 1x, as the map draws them, centred on it
// and cut at the preview's edges.
static Map  s_gate_map;
static char s_gate_zone[RES_ID_LEN];

static void draw_gate_map(const Resources *res, const GateDestination *d, ML_Rect a) {
    gfx_rect(a.x, a.y, a.w, a.h, PAL_CLR(BLACK));
    if (!d) return;
    if (strcmp(s_gate_zone, d->zone) != 0) {
        if (!MapLoadZone(&s_gate_map, res, d->zone)) { s_gate_zone[0] = '\0'; return; }
        snprintf(s_gate_zone, sizeof s_gate_zone, "%s", d->zone);
    }
    const Map *m = &s_gate_map;
    const int tw = CL_TILE_W, th = CL_TILE_H;
    int cx = a.x + (a.w - tw) / 2, cy = a.y + (a.h - th) / 2;     // the landing square
    int x0 = -((cx - a.x + tw - 1) / tw), x1 = (a.x + a.w - (cx + tw) + tw - 1) / tw;
    int y0 = -((cy - a.y + th - 1) / th), y1 = (a.y + a.h - (cy + th) + th - 1) / th;
    int z = present_get_zoom();
    gfx_clip_begin(a.x * z, a.y * z, a.w * z, a.h * z);
    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            int mx = d->x + tx, my = d->y + ty;
            if (mx < 0 || my < 0 || mx >= m->width || my >= m->height) continue;
            Rectangle dst = { (float)(cx + tx * tw), (float)(cy + ty * th), (float)tw, (float)th };
            map_render_cell(m, mx, my, dst);
        }
    }
    for (int t = 0; t < 3; t++)
        gfx_rect_lines(cx - t, cy - t, tw + 2 * t, th + 2 * t, t == 1 ? PAL_CLR(YELLOW) : PAL_CLR(BLACK));
    gfx_clip_end();
}

static void draw_gate(const Game *g) {
    // A menu page: the destinations at the left, the chosen one's
    // surroundings beside them, then Travel and Cancel on the foot.
    const Resources *res = resources_current();
    const ResUI *ui = &res->ui;
    const char *title = views_gate_is_town() ? ui->gate_title_town : ui->gate_title_castle;
    int n = views_gate_count();
    int cursor = views_gate_cursor();
    int row = views_gate_row();
    ML_Rect b = page_menu_body(title, g->character.name, 0);
    const GateDestination *d = views_gate_dest(cursor);
    char travel[RES_BANNER_LEN] = "";
    if (d) {
        const ResZone *z = resources_zone_by_id(res, d->zone);
        ResTemplateVar v[] = { { "TOWN", d->name }, { "ZONE", (z && z->name[0]) ? z->name : d->zone } };
        resources_format_template(travel, sizeof travel, res->banners.gate_travel, v, 2);
    }
    GateFoot foot = { travel, res->banners.count_cancel, d != NULL };
    int foot_y = b.y + b.h - ml_list_height(2);
    lattice_band_h(b.x, foot_y - UK_BAND, b.w, UK_BAND);
    ml_list_draw(b.x, foot_y, b.w, ml_list_height(2), 2, row >= n ? row - n : -1,
                 gate_foot_row, &foot, TOUCH_LIST_PROMPT, uk_ink());
    int top_h = foot_y - UK_BAND - b.y;
    int lw = 16 * GW + 2 * UK_INSET;
    ml_list_draw(b.x, b.y, lw, top_h, n, row < n ? row : -1, gate_row, NULL, TOUCH_LIST_GATE, uk_ink());
    lattice_band_v(b.x + lw, b.y, UK_BAND, top_h);
    ML_Rect a = { b.x + lw + UK_BAND, b.y, b.x + b.w - (b.x + lw + UK_BAND), top_h };
    draw_gate_map(res, d, a);
}

// ---------------------------------------------------------------------------
// Dispatcher.
// ---------------------------------------------------------------------------

void modern_views_render_draw(const Game *g, const Map *m, const Fog *f,
                        const Sprites *s) {
    ViewKind v = views_active();
    switch (v) {
        case VIEW_CHARACTER: draw_character(g, s);    break;
        case VIEW_ARMY:      draw_army(g, s);         break;
        case VIEW_CONTRACT:  draw_contract(g, s);     break;
        case VIEW_PUZZLE:    draw_puzzle(g, s);       break;
        case VIEW_WORLDMAP:  draw_worldmap(g, m, f);  break;
        case VIEW_SPELLS:    draw_spells(g);          break;
        case VIEW_GATE:      draw_gate(g);            break;
        default: break;
    }
}
