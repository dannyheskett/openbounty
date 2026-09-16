// src/modern/views_render.c
//
// The detail views for a pack that declared render.mode "modern": the ringed
// card, the inverted cursor row (REQ-430e), text wrapped to the panel width.
// Modern UI work happens here.
//
// The DOS original's views are in src/legacy/views_render.c and are frozen.
//
// Called only through the dispatcher in src/views_render.c.

#include "views.h"
#include "views_render_impl.h"
#include "modern/mlayout.h"
#include "lattice.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include "touch.h"
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

// Every modern detail view takes the full-screen layout (REQ-430j): the map
// pane plus the HUD, the status band left visible above it. One rect for
// all of them, computed from the pane -- the content rect times ui_scale this
// used to read is the DOS original's 240x170 and held 14 characters a line.
#define VIEW_X       (ml_full().x)
#define VIEW_Y       (ml_full().y)
#define VIEW_W       (ml_full().w)
#define VIEW_H       (ml_full().h)
#define VIEW_PAD     ML_PAD

// The "wide" views (Character, Army, Gate) were the content rect plus a
// sidebar; with every view full screen, wide and narrow are the same rect.
#define FULL_VIEW_W  VIEW_W
#define FULL_VIEW_X  VIEW_X

// ---------------------------------------------------------------------------
//  CHARACTER VIEW -- 
//  Portrait on left, stat table on right, artifact belt below.
// ---------------------------------------------------------------------------

// The character sheet (modern): the class portrait at its full 192x204, the
// numbers in headed groups in two columns beside it, then the sacred artifacts
// and the continents as full 96 px icons, and the pack's honours (blessing,
// tributes, rites) where it has them. On Rome's 776x480 the rows add up to the
// height exactly.
static void cv_row(const char *label, const char *value, int x, int w, int y) {
    bfont_draw(label, x, y, PAL_CLR(WHITE));
    bfont_draw(value, x + w - (int)bfont_measure(value).x, y, PAL_CLR(WHITE));
}

// An icon slot: the icon when held; else its ghost, dark, so the set reads
// as a collection with pieces still to find.
static void cv_icon(Texture2D tex, bool have, int x, int y, int size) {
    DrawRectangle(x, y, size, size, PAL_CLR(BLACK));
    if (tex.id) {
        Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
        Rectangle dst = { (float)x, (float)y, (float)size, (float)size };
        DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, have ? WHITE : (Color){ 60, 60, 70, 255 });
    }
    DrawRectangleLines(x, y, size, size, have ? (Color){ 150, 118, 48, 255 } : (Color){ 60, 52, 34, 255 });
}

static void draw_character(const Game *g, const Sprites *s) {
    const ResUI *ui = &g->res->ui;
    const ML_Rect r = ml_full();
    const int pad = UK_INSET, BAND = UK_BAND, THIN = 2;
    const int line = GH + 2, head = GH + 6;
    const int tile = CL_TILE_W;
    uk_sheet();
    char buf[96], nb[32], mb[32];

    // Title: name and rank; the next rank and how far off it is at the right.
    snprintf(buf, sizeof buf, "%s the %s", g->character.name, g->character.cls.rank_title);
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
    int top = uk_title(r.x, r.y, r.w, buf, right, PAL_CLR(YELLOW));

    // Portrait at its authored size.
    int pw = 192, ph = 204;
    if (cls && s->class_portrait[cls->index].id) ui_blit(s->class_portrait[cls->index], r.x, top, pw, ph);
    else DrawRectangle(r.x, top, pw, ph, PAL_CLR(BLACK));
    lattice_band_v(r.x + pw, top, BAND, ph);

    // Two columns of numbers under their headings.
    int cx = r.x + pw + BAND, cw = (r.x + r.w - cx) / 2;
    int lx = cx + pad, lw = cw - 2 * pad;
    int rx = cx + cw + pad, rw = r.x + r.w - rx - pad;
    lattice_band_v(cx + cw - 2, top, BAND, ph);
    int y = top + 6;
    bfont_draw(ui->cv_army, lx, y, PAL_CLR(YELLOW));                      y += head;
    snprintf(nb, sizeof nb, "%d", g->stats.leadership_current);  cv_row(ui->cv_leadership, nb, lx, lw, y); y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.commission_weekly);   cv_row(ui->cv_commission, nb, lx, lw, y); y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.gold);                cv_row(ui->cv_gold, nb, lx, lw, y);       y += line + 6;
    bfont_draw(ui->cv_magic, lx, y, PAL_CLR(YELLOW));                     y += head;
    snprintf(nb, sizeof nb, "%d", g->stats.spell_power);         cv_row(ui->cv_spell_power, nb, lx, lw, y); y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.max_spells);          cv_row(ui->cv_spell_capacity, nb, lx, lw, y);

    int total_v = 0;
    for (int i = 0; i < g->res->villains_count && i < CAT_VILLAINS_MAX; i++) total_v++;
    int total_a = artifacts_count() < 8 ? artifacts_count() : 8;
    y = top + 6;
    bfont_draw(ui->cv_campaign, rx, y, PAL_CLR(YELLOW));                  y += head;
    snprintf(nb, sizeof nb, "%d/%d", GameVillainsCaught(g), total_v); cv_row(ui->cv_captured, nb, rx, rw, y);  y += line;
    snprintf(nb, sizeof nb, "%d/%d", GameArtifactsFound(g), total_a); cv_row(ui->cv_artifacts, nb, rx, rw, y); y += line;
    snprintf(nb, sizeof nb, "%d", GameCastlesOwned(g));          cv_row(ui->cv_castles, nb, rx, rw, y);    y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.followers_killed);    cv_row(ui->cv_followers, nb, rx, rw, y);  y += line;
    snprintf(nb, sizeof nb, "%d", GameComputeScore(g));          cv_row(ui->cv_score, nb, rx, rw, y);      y += line;
    snprintf(nb, sizeof nb, "%d", g->stats.days_left);           cv_row(ui->cv_days, nb, rx, rw, y);

    // The sacred artifacts: eight full icons across; one still to find is a ghost.
    y = top + ph;
    lattice_band_h(r.x, y, r.w, THIN);
    y += THIN;
    int ax = r.x + (r.w - 8 * tile) / 2;
    bfont_draw(ui->cv_sacred, ax, y + 3, PAL_CLR(YELLOW));
    y += head;
    for (int i = 0; i < 8; i++)
        cv_icon(s->view_icon[i], i < total_a && g->artifacts.found[i], ax + i * tile, y, tile);
    y += tile;

    // The continents, and beside them the pack's honours.
    lattice_band_h(r.x, y, r.w, THIN);
    y += THIN;
    bfont_draw(ui->cv_continents, ax, y + 3, PAL_CLR(YELLOW));
    int nz = g->res->zone_count < 4 ? g->res->zone_count : 4;
    const ResEconomy *ec = &g->res->economy;
    bool honours = ec->audiences || ec->rites_per_zone;
    int hx = ax + 4 * tile + BAND + pad;
    if (honours) bfont_draw(ui->cv_honours, hx, y + 3, PAL_CLR(YELLOW));
    y += head;
    for (int i = 0; i < 4; i++)
        cv_icon(i < nz ? s->view_icon[8 + i] : (Texture2D){ 0 }, i < nz && g->world.zones_discovered[i],
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
            for (int i = 0; i < g->res->zone_count && i < GAME_CONTINENTS; i++) {
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
//  ARMY VIEW -- 
//  5 rows, each showing one troop stack.
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
    // Five rows of a full tile, exactly the 480: each troop's portrait, then
    // its name and how many, its morale at the right, and its numbers in
    // aligned columns under them.
    const ML_Rect r = ml_full();
    const ResUI *ui = &g->res->ui;
    const int tile = CL_TILE_W, lh = GH + 4;
    uk_sheet();
    int col[3] = { 0, 0, 0 };
    int tx0 = r.x + tile + UK_INSET;
    int cw = (r.x + r.w - UK_INSET - tx0) / 3;
    for (int k = 0; k < 3; k++) col[k] = tx0 + k * cw;
    for (int i = 0; i < 5; i++) {
        int ry = r.y + i * tile;
        if (i > 0) lattice_band_h(r.x, ry - 1, r.w, 2);
        bool filled = g->army[i].id[0] && g->army[i].count != 0;
        const TroopDef *t = filled ? troop_by_id(g->army[i].id) : NULL;
        if (!t) {
            DrawRectangle(r.x + 2, ry + 2, tile - 4, tile - 4, PAL_CLR(BLACK));
            DrawRectangleLines(r.x + 2, ry + 2, tile - 4, tile - 4, (Color){ 60, 52, 34, 255 });
            continue;
        }
        Texture2D face = s->troop_portrait[t->index].id ? s->troop_portrait[t->index] : s->troop_sprite[t->index];
        uk_picture(face, r.x + 1, ry + 1, tile - 2, tile - 2);
        int ty = ry + (tile - 3 * lh) / 2 + 2;
        char buf[96];
        snprintf(buf, sizeof buf, "%d %s", g->army[i].count, t->name);
        bfont_draw(buf, tx0, ty, PAL_CLR(YELLOW));
        int free_lead = g->stats.leadership_current - t->hit_points * g->army[i].count;
        const char *mor = free_lead < 0 ? ui->out_of_control : NULL;
        char mbuf[64];
        if (!mor) { snprintf(mbuf, sizeof mbuf, "%s%s", ui->army_morale, army_slot_morale(g, i)); mor = mbuf; }
        const char *mw = army_slot_morale(g, i);
        Color mc = free_lead < 0 ? PAL_CLR(RED)
                 : strcmp(mw, ui->morale_low) == 0 ? PAL_CLR(RED)
                 : strcmp(mw, ui->morale_high) == 0 ? PAL_CLR(GREEN) : PAL_CLR(WHITE);
        bfont_draw_right(mor, r.x + r.w - UK_INSET, ty, mc);
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
            bfont_draw(cell[k].l, cx, cy, PAL_CLR(GREY));
            bfont_draw(cell[k].v, cx + (lw > lw2 ? lw : lw2) + GW, cy, PAL_CLR(WHITE));
        }
    }
}

// ---------------------------------------------------------------------------
//  CONTRACT VIEW -- 
// ---------------------------------------------------------------------------

static void draw_contract(const Game *g, const Sprites *s) {
    views_contract_set_active(g && g->contract.active_id[0] != '\0');
    const ML_Rect r = ml_full();
    const ResUI *ui = &g->res->ui;
    const ResBanners *bn = &g->res->banners;
    const int size = 2 * CL_TILE_W;
    uk_sheet();
    const VillainDef *v = g->contract.active_id[0] ? villain_by_id(g->contract.active_id) : NULL;
    if (!v) {
        // No contract: the empty silhouette at 2x and where to get one.
        int top = uk_title(r.x, r.y, r.w, ui->cv_title_no_contract, NULL, PAL_CLR(YELLOW));
        int by = top + (r.y + r.h - top - size - 3 * uk_line_h()) / 2;
        uk_picture(s ? s->hud_contract_silhouette : (Texture2D){ 0 }, r.x + (r.w - size) / 2, by, size, size);
        int tw = 34 * GW, ty = by + size + UK_INSET;
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
    int top = uk_title(r.x, r.y, r.w, title, reward, PAL_CLR(YELLOW));

    int frame = sprites_frame((int)(GetTime() * 2.0), s->villain_anim_frames[v->index]);
    Texture2D face = s->villain_anim[v->index][frame];
    if (!face.id) face = s->villain_portrait[v->index];
    ML_Rect a = { r.x + UK_INSET, top + UK_INSET, r.w - 2 * UK_INSET, r.y + r.h - UK_INSET - (top + UK_INSET) };
    uk_picture(face, a.x, a.y, size, size);

    UkDoc d = { 0 };
    uk_doc_labeled(&d, ui->cv_label_name, v->name);
    uk_doc_labeled(&d, ui->cv_label_alias, (vd && vd->alias[0]) ? vd->alias : ui->cv_alias_none);
    const ResZone *vz = resources_zone_by_id(g->res, v->zone);
    uk_doc_labeled(&d, ui->cv_label_last_seen, (vz && vz->name[0]) ? vz->name : v->zone);
    const char *castle_name = ui->cv_castle_unknown;
    for (int i = 0; i < GAME_CASTLES; i++) {
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
//  PUZZLE VIEW -- 
//  5x5 grid; each cell is a villain face or artifact icon, covered with
//  a tile sprite until found/caught.
// ---------------------------------------------------------------------------

// Position -> entity mapping lives in the engine (tables.c PUZZLE_GRID,
// accessor puzzle_grid_entity): negative values are artifact-index-minus-one
// (-1 = artifact 0, ...), non-negative values are villain indices. Demo mode's
// scepter deduction reads the same table.

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
    // The grid of five tiles at the left, whole; beside it what it is and how
    // much of it is uncovered.
    uk_sheet();
    {
        const ML_Rect r = ml_full();
        const ResUI *ui = &g->res->ui;
        int px = r.x + 5 * CL_TILE_W + UK_BAND;
        lattice_band_v(px - UK_BAND, r.y, UK_BAND, r.h);
        int top = uk_title(px, r.y, r.x + r.w - px, g->res->ui.gm_puzzle, NULL, PAL_CLR(YELLOW));
        int x = px + UK_INSET, w = r.x + r.w - UK_INSET - x;
        int y = uk_flow(x, top + UK_INSET, w, x, 0, r.y + r.h, g->res->banners.puzzle_legend, PAL_CLR(WHITE));
        char nb[32];
        int total_v = g->res->villains_count < CAT_VILLAINS_MAX ? g->res->villains_count : CAT_VILLAINS_MAX;
        int total_a = artifacts_count() < 8 ? artifacts_count() : 8;
        y += uk_line_h();
        snprintf(nb, sizeof nb, "%d/%d", GameVillainsCaught(g), total_v);
        bfont_draw(ui->cv_captured, x, y, PAL_CLR(YELLOW)); bfont_draw_right(nb, x + w, y, PAL_CLR(WHITE));
        y += uk_line_h();
        snprintf(nb, sizeof nb, "%d/%d", GameArtifactsFound(g), total_a);
        bfont_draw(ui->cv_artifacts, x, y, PAL_CLR(YELLOW)); bfont_draw_right(nb, x + w, y, PAL_CLR(WHITE));
    }

    // Cells span ONLY the map area (240x170), NOT the sidebar -- matches
    // . Each cell is 48x34, same as a map tile, so
    // the scepter terrain we draw underneath aligns to the tile grid.
    // Each cell holds tile-shaped art -- a villain portrait, an artifact icon,
    // or a literal map tile from the scepter's zone -- so the cell is a tile,
    // and the 5x5 is centred in the panel. Dividing the panel by 5 instead only
    // agreed with the tile while the tile was exactly a fifth of it. In legacy
    // 5*48 == 240 == VIEW_W and 5*34 == 170 == VIEW_H, so both offsets are zero
    // and this lands on the historic grid.
    int cell_w = CL_TILE_W;
    int cell_h = CL_TILE_H;
    int grid_x = VIEW_X;
    int grid_y = VIEW_Y + (VIEW_H - cell_h * 5) / 2;

    puzzle_load_scepter_map(g);

    // Center the 5x5 viewport on the scepter, clamped to map bounds.
    int sx = g->scepter.x;
    int sy = g->scepter.y;
    int cam_x = sx - 2;
    int cam_y = sy - 2;
    if (s_puzzle_scepter_loaded) {
        const Map *m = &s_puzzle_scepter_map;
        if (cam_x < 0) cam_x = 0;
        if (cam_y < 0) cam_y = 0;
        if (cam_x > m->width  - 5) cam_x = m->width  - 5;
        if (cam_y > m->height - 5) cam_y = m->height - 5;
    }

    // Reveal cells one-by-one in two passes: artifacts first,
    // then villains, row-major within each pass, 150ms per cell.
    static double s_open_time = 0.0;
    static bool   s_prev_active = false;
    bool active = (views_active() == VIEW_PUZZLE);
    if (active && !s_prev_active) s_open_time = GetTime();
    s_prev_active = active;
    double elapsed = GetTime() - s_open_time;
    int reveal_step = (int)(elapsed / 0.150);   // 150ms / cell

    // Tick villain faces at ~2 Hz on the puzzle page, same as
    // the HUD contract panel (hud.c:51).
    int anim_tick = (int)(GetTime() * 2.0);

    // Two-pass cell ordering: pass 0 = artifacts (id<0), pass 1 = villains.
    // Within each pass, row-major (j, then i). cell_seq counts only
    // matching-pass cells, so the animation actually reveals one cell
    // per 150ms regardless of how many of each kind there are.
    int seq[5][5];
    {
        int n = 0;
        // Pass 0: artifacts.
        for (int j = 0; j < 5; j++)
            for (int i = 0; i < 5; i++)
                if (puzzle_grid_entity(j, i) < 0) seq[j][i] = n++;
        // Pass 1: villains.
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
                face = s->view_icon[artifact_id];
            } else {
                caught = g->contract.villains_caught[id];
                face = s->villain_anim[id]
                        [sprites_frame(anim_tick, s->villain_anim_frames[id])];
                if (!face.id) face = s->villain_portrait[id];
            }
            // Animation gate.
            if (reveal_step < seq[j][i]) caught = false;

            if (caught) {
                // Reveal: show the underlying scepter-location terrain
                // (). Each puzzle cell maps to a
                // map tile at (cam_x + i, cam_y + j).
                bool drew = false;
                if (s_puzzle_scepter_loaded) {
                    int mx = cam_x + i;
                    int my = cam_y + j;
                    const Tile *t = MapGetTile(&s_puzzle_scepter_map, mx, my);
                    if (t && t->art) {
                        Texture2D tex = tile_cache_get(TileArt(&s_puzzle_scepter_map, t));
                        if (tex.id) {
                            Rectangle src = { 0, 0,
                                              (float)tex.width,
                                              (float)tex.height };
                            Rectangle dst = { (float)x, (float)y,
                                              (float)cell_w, (float)cell_h };
                            DrawTexturePro(tex, src, dst,
                                           (Vector2){ 0, 0 }, 0.0f, WHITE);
                            drew = true;
                        }
                    }
                }
                if (!drew) {
                    DrawRectangle(x, y, cell_w, cell_h, PAL_CLR(BLACK));
                }
            } else if (face.id) {
                // Cover: show the entity face (villain portrait or
                // artifact icon).
                Rectangle src = { 0, 0, (float)face.width, (float)face.height };
                Rectangle dst = { (float)x, (float)y,
                                  (float)cell_w, (float)cell_h };
                DrawTexturePro(face, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
            } else {
                DrawRectangle(x, y, cell_w, cell_h, PAL_CLR(DGREY));
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  WORLDMAP VIEW -- 
//  Full-continent overview with fog, hero position blinking.
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
            case TERRAIN_DESERT:   return PAL_CLR(YELLOW);
            default:               return PAL_CLR(BLACK);
        }
    }
    switch (t) {
        case TERRAIN_GRASS:    return color_from_packed(col->minimap_grass);
        case TERRAIN_FOREST:   return color_from_packed(col->minimap_forest);
        case TERRAIN_MOUNTAIN: return color_from_packed(col->minimap_mountain);
        case TERRAIN_WATER:    return color_from_packed(col->minimap_water);
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
    if (zi < 0 || zi >= GAME_CONTINENTS) return false;
    return g->world.orbs_found[zi];
}

static void draw_worldmap_exit_hint(const Game *g) {
    // KB_TopBox strings.
    DrawRectangle(CL_STATUS_X, CL_STATUS_Y, CL_STATUS_W, CL_STATUS_H,
                  PAL_CLR(DRED));
    // The reveal is a row under the map, so the band only says how to leave.
    const ResUI *ui = &g->res->ui;
    char txt[96];
    ml_hint_text(txt, sizeof txt, ui->hint_back, ui->key_esc, ui->pad_back);
    touch_region(CL_STATUS_X, CL_STATUS_Y, CL_STATUS_W, CL_STATUS_H, KEY_ESCAPE);
    bfont_draw_centered(txt,
                        CL_STATUS_X + CL_STATUS_W / 2,
                        CL_STATUS_Y + 1,
                        PAL_CLR(WHITE));
}

// The places list: "All of <continent>", then the towns and castles of this
// continent the hero has visited. Choosing one zooms the map in on it.
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
        for (int k = 0; k < GAME_TOWNS; k++)
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
    if (i == 0) {
        const ResZone *z = resources_zone_by_id(w->g->res, w->g->position.zone);
        ResTemplateVar v[] = { { "ZONE", (z && z->name[0]) ? z->name : w->g->position.zone } };
        resources_format_template(label, cap, w->g->res->banners.worldmap_all, v, 1);
    } else {
        snprintf(label, (size_t)cap, "%s", w->p[i - 1].name);
    }
    return true;
}

// Rows the side panel's list shows, given whether the orb row is under it.
static int worldmap_list_h(bool orb) {
    const int foot = 2 * (GH + 4) + 2 * ML_PAD + UK_BAND;
    return VIEW_H - uk_title_h() - UK_BAND - foot - (orb ? ml_row_h() + UK_BAND : 0);
}

bool modern_worldmap_input(const Game *g) {
    if (views_active() != VIEW_WORLDMAP || !g || !g->res) { s_wm_open = false; return false; }
    if (!s_wm_open) { s_wm_open = true; s_wm_cursor = 0; }
    WmPlace places[64];
    int n = worldmap_places(g, places, 64) + 1;
    if (s_wm_cursor >= n) s_wm_cursor = n - 1;
    touch_request(TOUCH_CHROME_BACK);
    int tapped = touch_tapped_row(TOUCH_LIST_MENU);
    if (tapped >= 0 && tapped < n) { s_wm_cursor = tapped; return true; }
    if (input_key_pressed(KEY_ESCAPE)) { views_dismiss(); s_wm_open = false; return true; }
    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8)) { s_wm_cursor = (s_wm_cursor - 1 + n) % n; return true; }
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2)) { s_wm_cursor = (s_wm_cursor + 1) % n; return true; }
    bool orb = worldmap_has_orb(g);
    if (orb && (touch_tapped_row(TOUCH_LIST_PROMPT) == 0 || input_key_pressed(KEY_SPACE) ||
                input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER))) {
        views_render_worldmap_toggle_hero_only();
        return true;
    }
    return true;     // the view holds the keys; Esc or Back leaves
}

static void draw_worldmap(const Game *g, const Map *m, const Fog *f) {
    uk_sheet();
    (void)draw_worldmap_exit_hint;
    if (!m || m->width <= 0 || m->height <= 0) return;
    const ResUI *ui = &g->res->ui;
    const ResBanners *bn = &g->res->banners;
    bool orb = worldmap_has_orb(g);
    bool reveal_all = orb && views_render_worldmap_whole();

    WmPlace places[64];
    int np = worldmap_places(g, places, 64);
    int cursor = s_wm_open ? s_wm_cursor : 0;
    if (cursor > np) cursor = np;

    // The map at the left: the whole continent at the largest whole scale,
    // or three times that over the chosen place.
    const int side_w = 280;
    int map_w = VIEW_W - side_w - UK_BAND;
    int avail_w = map_w - 2 * VIEW_PAD, avail_h = VIEW_H - 2 * VIEW_PAD;
    int pix = (avail_w / m->width < avail_h / m->height) ? avail_w / m->width : avail_h / m->height;
    if (pix < 1) pix = 1;
    int cam_x = 0, cam_y = 0, cols = m->width, rows = m->height;
    const WmPlace *sel = cursor > 0 ? &places[cursor - 1] : NULL;
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
    int gx = VIEW_X + (map_w - grid_w) / 2;
    int gy = VIEW_Y + (VIEW_H - grid_h) / 2;
    DrawRectangle(gx, gy, grid_w, grid_h, PAL_CLR(BLACK));
    const ResColors *mm_col = &g->res->colors;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            int mx = cam_x + x, my = cam_y + y;
            if (!reveal_all && !FogSeen(f, mx, my)) continue;
            const Tile *t = MapGetTile(m, mx, my);
            if (!t) continue;
            DrawRectangle(gx + x * pix, gy + y * pix, pix, pix, terrain_minimap_color(mm_col, t->terrain));
        }
    }
    const Resources *r = g->res;
    // A marker: a filled cell with a dark edge, so it reads on any terrain.
    #define MARK(mx, my, col) do { int _x = gx + ((mx) - cam_x) * pix, _y = gy + ((my) - cam_y) * pix; \
        DrawRectangle(_x, _y, pix, pix, col); \
        if (pix >= 4) DrawRectangleLines(_x, _y, pix, pix, PAL_CLR(BLACK)); } while (0)
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
    unsigned k = (unsigned)(GetTime() * 3.0);
    // The chosen place: a ring around it.
    if (sel) {
        int rx = gx + (sel->x - cam_x) * pix, ry = gy + (sel->y - cam_y) * pix;
        for (int t = 0; t < 3; t++)
            DrawRectangleLines(rx - pix - t, ry - pix - t, 3 * pix + 2 * t, 3 * pix + 2 * t,
                               t == 1 ? PAL_CLR(YELLOW) : PAL_CLR(BLACK));
    }
    // The boat (white) and the hero (blinking), always.
    if (g->boat.has_boat && strcmp(g->boat.zone, g->position.zone) == 0 && IN_VIEW(g->boat.x, g->boat.y))
        MARK(g->boat.x, g->boat.y, PAL_CLR(CYAN));
    if (IN_VIEW(g->position.x, g->position.y))
        MARK(g->position.x, g->position.y, (k & 1) ? PAL_CLR(YELLOW) : PAL_CLR(MAGENTA));
    #undef IN_VIEW
    #undef MARK

    // The side panel: the continent, the places, where you and your boat are.
    int sx = VIEW_X + map_w;
    lattice_band_v(sx, VIEW_Y, UK_BAND, VIEW_H);
    int px = sx + UK_BAND, pw = VIEW_X + VIEW_W - px;
    const ResZone *z = resources_zone_by_id(r, g->position.zone);
    int top = uk_title(px, VIEW_Y, pw, (z && z->name[0]) ? z->name : g->position.zone, NULL, PAL_CLR(YELLOW));
    WmRows wr = { g, places, np };
    int list_h = worldmap_list_h(orb);
    ml_list_draw(px, top, pw, list_h, np + 1, cursor, worldmap_row_fn, &wr, TOUCH_LIST_MENU, uk_ink());
    int y = VIEW_Y + VIEW_H;
    if (orb) {
        const char *label = views_render_worldmap_whole() ? ui->worldmap_row_your_map : ui->worldmap_row_whole_map;
        y -= ml_row_h();
        lattice_band_h(px, y - UK_BAND, pw, UK_BAND);
        sel_row(px, y, pw, ml_row_h(), px + ML_PAD, label, false, PAL_CLR(WHITE), uk_ink(), TOUCH_LIST_PROMPT, 0);
        y -= UK_BAND;
    }
    int foot_h = 2 * (GH + 4) + 2 * ML_PAD;
    y -= foot_h;
    lattice_band_h(px, y - UK_BAND, pw, UK_BAND);
    char xb[12], yb[12], line[RES_BANNER_LEN];
    snprintf(xb, sizeof xb, "%d", g->position.x);
    snprintf(yb, sizeof yb, "%d", g->position.y);
    ResTemplateVar yv[] = { { "X", xb }, { "Y", yb } };
    resources_format_template(line, sizeof line, bn->worldmap_you, yv, 2);
    bfont_draw(line, px + ML_PAD, y + ML_PAD, PAL_CLR(WHITE));
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
    bfont_draw(line, px + ML_PAD, y + ML_PAD + GH + 4, PAL_CLR(WHITE));
}

// --gallery: the list's cursor.
void modern_worldmap_gallery(int cursor) { s_wm_open = true; s_wm_cursor = cursor; }

// ---------------------------------------------------------------------------
//  SPELLS VIEW -- combat + adventure spell lists
//  Two columns: Combat (0..6) on left, Adventuring (7..13) on right.
// ---------------------------------------------------------------------------

typedef struct { const Game *g; } SpellsCtx;

static bool spell_row(void *ctx, int i, char *label, char *right, int cap) {
    const Game *g = ((const SpellsCtx *)ctx)->g;
    const SpellDef *sp = spell_by_index(i);
    int cnt = g->spells.counts[i];
    snprintf(label, (size_t)cap, "%s", sp ? sp->name : "");
    snprintf(right, 48, "%d", cnt);
    return cnt > 0;
}

static void draw_spells(const Game *g) {
    // The two lists side by side under their headings, and the spell under the
    // cursor described along the foot.
    const ML_Rect r = ml_full();
    const ResUI *ui = &g->res->ui;
    uk_sheet();
    int top = uk_title(r.x, r.y, r.w, ui->sv_title, NULL, PAL_CLR(YELLOW));
    int half = r.w / 2;
    bfont_draw(ui->sv_combat_col,    r.x + ML_PAD, top + 4, PAL_CLR(YELLOW));
    bfont_draw(ui->sv_adventure_col, r.x + half + UK_BAND + ML_PAD, top + 4, PAL_CLR(YELLOW));
    int row_y = top + GH + 8;
    int rows_h = ml_list_height(7);
    int cur = views_spells_cursor();
    SpellsCtx c = { g };
    ml_list_draw_ex(r.x, row_y, half, rows_h, 7, cur < 7 ? cur : -1,
                    spell_row, &c, TOUCH_LIST_SPELLS, uk_ink(), 0);
    lattice_band_v(r.x + half, top, UK_BAND, row_y + rows_h - top);
    ml_list_draw_ex(r.x + half + UK_BAND, row_y, r.w - half - UK_BAND, rows_h, 7, cur >= 7 ? cur - 7 : -1,
                    spell_row, &c, TOUCH_LIST_SPELLS, uk_ink(), 7);
    int fy = row_y + rows_h;
    lattice_band_h(r.x, fy, r.w, UK_BAND);
    const SpellDef *sp = (cur >= 0) ? spell_by_index(cur) : NULL;
    const char *desc = sp ? sp->description : NULL;
    if ((!desc || !desc[0]) && sp) desc = resources_spell_lore(g->res, sp->id);
    if (desc) {
        // As many whole sentences as the foot holds. A sentence ends after its
        // full stop AND any quotation mark closing it, so "bridge-builder."
        // keeps its quote.
        int tw = r.w - 2 * UK_INSET, fit = (r.y + r.h - (fy + UK_BAND + 6)) / uk_line_h();
        char text[512];
        snprintf(text, sizeof text, "%s", desc);
        if (uk_lines(text, tw) > fit) {
            int keep = 0;
            for (int i = 0; text[i]; i++) {
                if (text[i] != '.' && text[i] != '!' && text[i] != '?') continue;
                int e = i + 1;
                if (text[e] == '"' || text[e] == '\'') e++;
                if (text[e] && text[e] != ' ' && text[e] != '\n') continue;
                char save = text[e];
                text[e] = '\0';
                bool fits = uk_lines(text, tw) <= fit;
                text[e] = save;
                if (!fits) break;
                keep = e;
            }
            if (keep > 0) text[keep] = '\0';   // nothing whole fits: let it clip
        }
        uk_flow(r.x + UK_INSET, fy + UK_BAND + 6, tw, r.x, 0, r.y + r.h, text, PAL_CLR(WHITE));
    }
}

// ---------------------------------------------------------------------------
//  GATE PICKER VIEW (VIEW_GATE)
//  Town/Castle Gate destination chooser. Two lettered columns with an
//  arrow-key cursor + highlight, modeled on the Spells view but cursored like
//  the Game Menu. Data comes from the view-state snapshot (views_gate_*), never
//  from game state.
// ---------------------------------------------------------------------------

#define GATE_NAME_COL 14

// Columns of standard select rows the modern gate picker uses; views.c reads
// the same number for Left/Right.
static bool gate_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)ctx;
    const GateDestination *d = views_gate_dest(i);
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", d ? d->name : "");
    return d != NULL;
}

// The destination's surroundings, drawn from its continent's map (loaded once
// per continent, like the puzzle's).
static Map  s_gate_map;
static char s_gate_zone[24];

static void draw_gate_map(const Resources *res, const GateDestination *d, ML_Rect a) {
    DrawRectangle(a.x, a.y, a.w, a.h, PAL_CLR(BLACK));
    if (!d) return;
    if (strcmp(s_gate_zone, d->zone) != 0) {
        if (!MapLoadZone(&s_gate_map, res, d->zone)) { s_gate_zone[0] = '\0'; return; }
        snprintf(s_gate_zone, sizeof s_gate_zone, "%s", d->zone);
    }
    const Map *m = &s_gate_map;
    const int cell = CL_TILE_W / 2;
    int cols = a.w / cell, rows = a.h / cell;
    int ox = a.x + (a.w - cols * cell) / 2, oy = a.y + (a.h - rows * cell) / 2;
    int cam_x = d->x - cols / 2, cam_y = d->y - rows / 2;
    for (int ty = 0; ty < rows; ty++) {
        for (int tx = 0; tx < cols; tx++) {
            int mx = cam_x + tx, my = cam_y + ty;
            if (mx < 0 || my < 0 || mx >= m->width || my >= m->height) continue;
            const Tile *t = MapGetTile(m, mx, my);
            if (!t) continue;
            Rectangle dst = { (float)(ox + tx * cell), (float)(oy + ty * cell), (float)cell, (float)cell };
            char va[TILE_ART_NAME_LEN];
            if (t->interactive != INTERACT_NONE) {
                char ga[TILE_ART_NAME_LEN];
                const char *gart = t->ground ? TileGround(m, t) : MapTerrainArt(m, TerrainName(t->terrain), ga, sizeof ga);
                Texture2D ground = tile_cache_get(tilevar_art(gart, mx, my, va, sizeof va));
                if (ground.id) DrawTexturePro(ground, (Rectangle){ 0, 0, (float)ground.width, (float)ground.height },
                                              dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
            }
            Texture2D tex = tile_cache_get(tilevar_art(TileArt(m, t), mx, my, va, sizeof va));
            if (tex.id) DrawTexturePro(tex, (Rectangle){ 0, 0, (float)tex.width, (float)tex.height },
                                       dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }
    // The landing square ringed.
    int rx = ox + (d->x - cam_x) * cell, ry = oy + (d->y - cam_y) * cell;
    for (int t = 0; t < 3; t++)
        DrawRectangleLines(rx - t, ry - t, cell + 2 * t, cell + 2 * t, t == 1 ? PAL_CLR(YELLOW) : PAL_CLR(BLACK));
}

static void draw_gate(void) {
    // An in-lay over the dimmed map: the destinations as one list at the left,
    // the chosen one's surroundings at the right, and Travel along the foot.
    // Back is the top bar.
    const Resources *res = resources_current();
    const ResUI *ui = &res->ui;
    const char *title = views_gate_is_town() ? ui->gate_title_town : ui->gate_title_castle;
    int n = views_gate_count();
    int cursor = views_gate_cursor();
    ML_Rect b = uk_inlay(ml_full().w, UK_TALL_H, title, NULL);
    const GateDestination *d = views_gate_dest(cursor);
    char travel[RES_BANNER_LEN] = "";
    if (d) {
        const ResZone *z = resources_zone_by_id(res, d->zone);
        ResTemplateVar v[] = { { "TOWN", d->name }, { "ZONE", (z && z->name[0]) ? z->name : d->zone } };
        resources_format_template(travel, sizeof travel, res->banners.gate_travel, v, 2);
    }
    UkRows foot_rows = { { travel }, { d != NULL } };
    int foot = uk_foot_rows(b, 1, 0, uk_rows_fn, &foot_rows, TOUCH_LIST_PROMPT);
    int lw = 14 * GW + 2 * ML_PAD;
    ml_list_draw(b.x, b.y, lw, foot - b.y, n, cursor, gate_row, NULL, TOUCH_LIST_GATE, uk_ink());
    lattice_band_v(b.x + lw, b.y, UK_BAND, foot - b.y);
    ML_Rect a = { b.x + lw + UK_BAND, b.y, b.x + b.w - (b.x + lw + UK_BAND), foot - b.y };
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
        case VIEW_GATE:      draw_gate();             break;
        default: break;
    }
}
