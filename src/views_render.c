#include "views.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "tile_cache.h"
#include "tables.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

#define GW  BFONT_GLYPH_W
#define GH  BFONT_GLYPH_H

// View panels render into the region below the status bar, spanning
// the map area + sidebar (not the frames). Width = CL_MAP_W + CL_SIDEBAR_W.
#define VIEW_X       CL_MAP_X
#define VIEW_Y       CL_MAP_Y
#define VIEW_W       CL_MAP_W
#define VIEW_H       CL_MAP_H
#define VIEW_PAD     4

// Full-content-width views (Character) cover map + sidebar.
#define FULL_VIEW_X  CL_MAP_X
#define FULL_VIEW_W  (CL_MAP_W + CL_SIDEBAR_W)

// Solid-fill background + 1px yellow border for a view panel.
static void draw_view_panel(void) {
    DrawRectangle(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, PAL_CLR(DGREY));
    DrawRectangleLines(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, PAL_CLR(DRED));
}

// Thin horizontal rule between rows.
static void draw_rule(int x, int y, int w) {
    DrawRectangle(x, y, w, 1, PAL_CLR(DRED));
}

// ---------------------------------------------------------------------------
//  CHARACTER VIEW — 
//  Portrait on left, stat table on right, artifact belt below.
// ---------------------------------------------------------------------------

// Pixel-exact port of .
// Layout constants are derived 's dynamic math with fs->h=8,
// sys->zoom=1, DOS_TILE_W=48, DOS_TILE_H=34. Every magic number here
// has a comment linking it back to  source.
static void draw_character(const Game *g, const Sprites *s) {
    // Full content width (covers HUD); solid black background per ref.
    int vx = FULL_VIEW_X;
    int vw = FULL_VIEW_W;
    DrawRectangle(vx, VIEW_Y, vw, VIEW_H, PAL_CLR(DGREY));

    const ClassDef *cls = class_by_id(g->character.cls.id);
    int portrait_w = 96;
    int portrait_h = 102;
    if (cls) {
        Texture2D portrait = s->class_portrait[cls->index];
        if (portrait.id) {
            portrait_w = portrait.width;
            portrait_h = portrait.height;
            Rectangle src = { 0, 0, (float)portrait_w, (float)portrait_h };
            Rectangle dst = { (float)vx, (float)VIEW_Y,
                              (float)portrait_w, (float)portrait_h };
            DrawTexturePro(portrait, src, dst, (Vector2){0,0}, 0.0f, WHITE);
        }
    }

    int sx = vx + portrait_w;
    int lh = 8;       // row height = font glyph height; no overlap
    int bh = 4;       // blank-row gap
    char buf[64];
    const ResUI *ui = &g->res->ui;

    // Render label + value rows. Value column right-aligned within the
    // stats area; blank string when value == 0 (per page 1 reference).
    int label_pad = 1;
    int label_x = sx + label_pad;
    int val_right = vx + vw - 2;     // right edge of stats area, 2px inset

    int y = VIEW_Y + 1;

    // Row helper: draw label left-aligned and value right-aligned. value
    // string is empty when count is 0.
    #define ROW_LABEL_VAL(label, count) do { \
        bfont_draw((label), label_x, y, PAL_CLR(WHITE)); \
        if ((count) != 0) { \
            snprintf(buf, sizeof(buf), "%d", (count)); \
            int tw = (int)bfont_measure(buf).x; \
            bfont_draw(buf, val_right - tw, y, PAL_CLR(WHITE)); \
        } \
        y += lh; \
    } while (0)
    #define ROW_BLANK() do { y += bh; } while (0)
    #define ROW_NAME() do { \
        snprintf(buf, sizeof(buf), "%s the %s", \
                 g->character.name, g->character.cls.rank_title); \
        bfont_draw(buf, label_x, y, PAL_CLR(WHITE)); \
        y += lh; \
    } while (0)

    ROW_NAME();
    ROW_LABEL_VAL(ui->stat_leadership,         g->stats.leadership_current);
    ROW_LABEL_VAL(ui->stat_commission,         g->stats.commission_weekly);
    ROW_LABEL_VAL(ui->stat_gold,               g->stats.gold);
    ROW_BLANK();
    ROW_LABEL_VAL(ui->stat_spell_power,        g->stats.spell_power);
    ROW_LABEL_VAL(ui->stat_max_spells,         g->stats.max_spells);
    ROW_BLANK();
    ROW_LABEL_VAL(ui->stat_villains_caught,    GameVillainsCaught(g));
    ROW_LABEL_VAL(ui->stat_artifacts_found,    GameArtifactsFound(g));
    ROW_BLANK();
    ROW_LABEL_VAL(ui->stat_castles_garrisoned, GameCastlesOwned(g));
    ROW_LABEL_VAL(ui->stat_followers_killed,   g->stats.followers_killed);
    ROW_LABEL_VAL(ui->stat_current_score,      GameComputeScore(g));

    #undef ROW_LABEL_VAL
    #undef ROW_BLANK
    #undef ROW_NAME

    // Inventory belt (full content width). Yellow outline; dark-red empty
    // slots inside; artifact icons / zone tiles overlay when found.
    int inv_x = vx;
    int inv_y = VIEW_Y + portrait_h;
    int item_w = vw / 6;             // 288/6 = 48
    int item_h = CL_TILE_H;          // 34
    int belt_h = item_h * 2;

    // Inner fill: dark red (empty-slot color).
    DrawRectangle(inv_x, inv_y, vw, belt_h, PAL_CLR(DRED));
    // Light-grey outline + grid lines.
    DrawRectangleLines(inv_x, inv_y, vw, belt_h, PAL_CLR(GREY));
    for (int c = 1; c < 6; c++) {
        DrawRectangle(inv_x + c * item_w, inv_y, 1, belt_h, PAL_CLR(GREY));
    }
    DrawRectangle(inv_x, inv_y + item_h, vw, 1, PAL_CLR(GREY));

    // Artifact grid: 4 cols × 2 rows. Only stamp icon when found.
    for (int i = 0; i < 8; i++) {
        if (!g->artifacts.found[i]) continue;
        Texture2D tex = s->view_icon[i];
        if (!tex.id) continue;
        int col = i % 4;
        int row = i / 4;
        int ix = inv_x + col * item_w;
        int iy = inv_y + row * item_h;
        Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
        Rectangle dst = { (float)ix, (float)iy,
                          (float)item_w, (float)item_h };
        DrawTexturePro(tex, src, dst, (Vector2){0,0}, 0.0f, WHITE);
    }

    // Map grid: 2 cols × 2 rows starting at col 4. Only stamp tile when
    // zone discovered.
    int map_x = inv_x + 4 * item_w;
    for (int i = 0; i < 4; i++) {
        if (!g->world.zones_discovered[i]) continue;
        Texture2D tex = s->view_icon[8 + i];
        if (!tex.id) continue;
        int col = i % 2;
        int row = i / 2;
        int ix = map_x + col * item_w;
        int iy = inv_y + row * item_h;
        Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
        Rectangle dst = { (float)ix, (float)iy,
                          (float)item_w, (float)item_h };
        DrawTexturePro(tex, src, dst, (Vector2){0,0}, 0.0f, WHITE);
    }
}

// ---------------------------------------------------------------------------
//  ARMY VIEW — 
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
    // view_army covers the full screen including the HUD area
    // — the right-column stats need the extra width to lay out cleanly.
    int vx = FULL_VIEW_X;
    int vw = FULL_VIEW_W;
    DrawRectangle(vx, VIEW_Y, vw, VIEW_H, PAL_CLR(DGREY));
    DrawRectangleLines(vx, VIEW_Y, vw, VIEW_H, PAL_CLR(DRED));

    int row_h = 34;
    int pad = 2;

    // view_army tick-animates each troop's 4-frame idle strip.
    // ~8 Hz matches the HUD villain anim cadence.
    int anim_frame = ((int)(GetTime() * 8.0)) & 3;

    for (int i = 0; i < 5; i++) {
        int ry = VIEW_Y + pad + i * row_h;
        int sprite_w = 48;
        int sprite_h = 34;

        DrawRectangle(vx + pad, ry, sprite_w, sprite_h, PAL_CLR(DGREEN));

        if (!g->army[i].id[0] || g->army[i].count == 0) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        if (!t) continue;

        Texture2D tex = s->troop_anim[t->index][anim_frame];
        if (!tex.id) tex = s->troop_sprite[t->index];
        if (tex.id) {
            Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
            Rectangle dst = { (float)(vx + pad), (float)ry,
                              (float)sprite_w, (float)sprite_h };
            DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }

        // Two columns of stats to the right.
        int tx = vx + pad + sprite_w + 4;
        int ty = ry + 2;
        char buf[96];

        const ResUI *ui = &g->res->ui;
        // Army Size option (4): 0 = exact count, 1 = fuzzy display
        if (g->stats.options[4] == 0) {
            snprintf(buf, sizeof(buf), "%3d %s", g->army[i].count, t->name);
        } else {
            const char *count_str = resources_count_bucket_label(
                ui->count_buckets_army_view,
                ui->count_buckets_army_view_n,
                g->army[i].count, "");
            snprintf(buf, sizeof(buf), "%s %s", count_str, t->name);
        }
        bfont_draw(buf, tx, ty, PAL_CLR(WHITE));
        snprintf(buf, sizeof(buf), "%s%2d %s%2d",
                 ui->army_skill, t->skill_level,
                 ui->army_move,  t->move_rate);
        bfont_draw(buf, tx, ty + GH, PAL_CLR(WHITE));
        // OOC test, per-row (matches unit_under_control in combat.c):
        //   hp * count > leadership  -> out of control
        // Boundary (hp*count == leadership) is NOT OOC: a Knight
        // (lead=100) can fully recruit up to 10 Pikemen (10*10=100)
        // and they remain under control.
        Color morale_color = PAL_CLR(WHITE);
        int free_lead = g->stats.leadership_current
                      - t->hit_points * g->army[i].count;
        if (free_lead < 0) {
            morale_color = PAL_CLR(RED);
            snprintf(buf, sizeof(buf), "%s", ui->out_of_control);
        } else {
            snprintf(buf, sizeof(buf), "%s%s",
                     ui->army_morale, army_slot_morale(g, i));
        }
        bfont_draw(buf, tx, ty + GH * 2, morale_color);

        // Right column: hit points, damage, cost.
        int rx = tx + 16 * GW;
        int total_hp = t->hit_points * g->army[i].count;
        snprintf(buf, sizeof(buf), "%s%d", ui->army_hit_points, total_hp);
        bfont_draw(buf, rx, ty, PAL_CLR(WHITE));
        snprintf(buf, sizeof(buf), "%s%d-%d", ui->army_damage,
                 t->melee_min * g->army[i].count,
                 t->melee_max * g->army[i].count);
        bfont_draw(buf, rx, ty + GH, PAL_CLR(WHITE));
        snprintf(buf, sizeof(buf), "%s%d", ui->army_g_cost,
                 (t->recruit_cost / 10) * g->army[i].count);
        bfont_draw(buf, rx, ty + GH * 2, PAL_CLR(WHITE));

        if (i < 4) draw_rule(vx + pad, ry + row_h - 1,
                             vw - 2 * pad);
    }
}

// ---------------------------------------------------------------------------
//  CONTRACT VIEW — 
// ---------------------------------------------------------------------------

// Draw a multi-line text block starting at (x, y); returns the y after the
// last line drawn. Respects embedded newlines in the source text.
static int draw_text_block(const char *text, int x, int y, Color c) {
    if (!text) return y;
    const char *p = text;
    char line[96];
    int ly = y;
    while (*p) {
        int n = 0;
        while (*p && *p != '\n' && n + 1 < (int)sizeof(line)) {
            line[n++] = *p++;
        }
        line[n] = '\0';
        if (*p == '\n') p++;
        bfont_draw(line, x, ly, c);
        ly += GH;
    }
    return ly;
}

static void draw_contract(const Game *g, const Sprites *s) {
    views_contract_set_active(g && g->contract.active_id[0] != '\0');

    // Contract view: blue panel covers map cols 0-4, rows 1-3
    // (3 tiles tall = 102px). Top map row + bottom map row remain visible;
    // HUD sidebar untouched. Don't override the status bar — it stays
    // normal ("Options / Controls / Days Left:NNN").
    int panel_x = VIEW_X + 2;
    int panel_y = VIEW_Y + CL_TILE_H;        // 1 tile down
    int panel_w = FULL_VIEW_W - 4;           // 2px inset on each side
    int panel_h = CL_TILE_H * 3;             // 3 tiles tall

    // Rounded-corner blue panel with yellow border .
    {
        Rectangle r = { (float)panel_x, (float)panel_y,
                        (float)panel_w, (float)panel_h };
        float roundness = 0.05f;
        int segments = 6;
        DrawRectangleRounded(r, roundness, segments, PAL_CLR(DBLUE));
        DrawRectangleRoundedLines(r, roundness, segments, PAL_CLR(YELLOW));
    }

    int pad = VIEW_PAD;
    int tx = panel_x + pad;
    int ty = panel_y + pad;

    const ResUI *ui = &g->res->ui;
    if (!g->contract.active_id[0]) {
        // No contract: silhouette box top-left, "You have no Contract!"
        // centered in the remaining space.
        int box_w = 48;
        int box_h = 34;
        DrawRectangleLines(tx - 1, ty - 1, box_w + 2, box_h + 2,
                           PAL_CLR(YELLOW));
        if (s && s->hud_contract_silhouette.id) {
            Rectangle src = { 0, 0,
                              (float)s->hud_contract_silhouette.width,
                              (float)s->hud_contract_silhouette.height };
            Rectangle dst = { (float)tx, (float)ty,
                              (float)box_w, (float)box_h };
            DrawTexturePro(s->hud_contract_silhouette, src, dst,
                           (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
        bfont_draw_centered(ui->cv_title_no_contract,
                            panel_x + panel_w / 2,
                            panel_y + panel_h / 2 - GH / 2,
                            PAL_CLR(WHITE));
        return;
    }

    // ACTIVE CONTRACT case: taller blue panel (4 tiles tall) covering
    // rows 1-4. Top map row stays visible (row 0); HUD slot at top-right
    // shows the villain face; gold purse at bottom-right of HUD.
    panel_h = CL_TILE_H * 4;
    {
        Rectangle r = { (float)panel_x, (float)panel_y,
                        (float)panel_w, (float)panel_h };
        float roundness = 0.05f;
        int segments = 6;
        DrawRectangleRounded(r, roundness, segments, PAL_CLR(DBLUE));
        DrawRectangleRoundedLines(r, roundness, segments, PAL_CLR(YELLOW));
    }
    tx = panel_x + pad;
    ty = panel_y + pad;

    const VillainDef *v = villain_by_id(g->contract.active_id);
    if (!v) return;

    // Villain portrait on left (animated when strip is available).
    int frame = ((int)(GetTime() * 2.0)) & 3;
    Texture2D face = s->villain_anim[v->index][frame];
    if (!face.id) face = s->villain_portrait[v->index];
    int face_w = 48;
    int face_h = 34;
    if (face.id) {
        face_w = face.width;
        face_h = face.height;
        Rectangle src = { 0, 0, (float)face_w, (float)face_h };
        Rectangle dst = { (float)tx, (float)ty, (float)face_w, (float)face_h };
        DrawTexturePro(face, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }
    DrawRectangleLines(tx - 1, ty - 1, face_w + 2, face_h + 2,
                       PAL_CLR(YELLOW));

    // Pull the description from game.json resources.
    const ResVillainDesc *vd = resources_villain_desc(g->res, v->id);

    int sx = tx + face_w + BFONT_GLYPH_W;
    int sy = ty;
    char buf[96];

    // All header labels in YELLOW (). Lines: Name,
    // Alias, Reward, Last Seen, Castle.
    {
        ResTemplateVar v_[] = { { "VALUE", v->name } };
        resources_format_template(buf, sizeof buf, ui->cv_label_name, v_, 1);
        bfont_draw(buf, sx, sy, PAL_CLR(YELLOW)); sy += GH;
    }
    {
        const char *alias = (vd && vd->alias[0]) ? vd->alias : ui->cv_alias_none;
        ResTemplateVar v_[] = { { "VALUE", alias } };
        resources_format_template(buf, sizeof buf, ui->cv_label_alias, v_, 1);
        bfont_draw(buf, sx, sy, PAL_CLR(YELLOW)); sy += GH;
    }
    {
        char rbuf[16];
        snprintf(rbuf, sizeof rbuf, "%d", v->reward);
        ResTemplateVar v_[] = { { "VALUE", rbuf } };
        resources_format_template(buf, sizeof buf, ui->cv_label_reward, v_, 1);
        bfont_draw(buf, sx, sy, PAL_CLR(YELLOW)); sy += GH;
    }
    {
        const ResZone *vz = resources_zone_by_id(g->res, v->zone);
        const char *zone_label = (vz && vz->name[0]) ? vz->name : v->zone;
        ResTemplateVar v_[] = { { "VALUE", zone_label } };
        resources_format_template(buf, sizeof buf, ui->cv_label_last_seen,
                                  v_, 1);
        bfont_draw(buf, sx, sy, PAL_CLR(YELLOW)); sy += GH;
    }

    // Castle — populated from castle catalog when contract.active has a
    // known castle (owner_kind == CASTLE_OWNER_VILLAIN, villain_id ==).
    const char *castle_name = ui->cv_castle_unknown;
    for (int i = 0; i < GAME_CASTLES; i++) {
        if (!g->castles[i].id[0]) continue;
        if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
        if (strcmp(g->castles[i].villain_id, v->id) != 0) continue;
        if (g->castles[i].known) castle_name = g->castles[i].id;
        break;
    }
    {
        ResTemplateVar v_[] = { { "VALUE", castle_name } };
        resources_format_template(buf, sizeof buf, ui->cv_label_castle, v_, 1);
        bfont_draw(buf, sx, sy, PAL_CLR(YELLOW)); sy += GH + 2;
    }

    // Features block (use the full panel width below the header band).
    if (vd && vd->features[0]) {
        bfont_draw(ui->cv_features_header,
                   panel_x + pad, sy, PAL_CLR(YELLOW));
        sy += GH;
        sy = draw_text_block(vd->features, panel_x + pad, sy, PAL_CLR(WHITE));
        sy += 2;
    }

    // Crimes block.
    if (vd && vd->crimes[0]) {
        bfont_draw(ui->cv_crimes_header,
                   panel_x + pad, sy, PAL_CLR(YELLOW));
        sy += GH;
        sy = draw_text_block(vd->crimes, panel_x + pad, sy, PAL_CLR(WHITE));
    }
}

// ---------------------------------------------------------------------------
//  PUZZLE VIEW — 
//  5x5 grid; each cell is a villain face or artifact icon, covered with
//  a tile sprite until found/caught.
// ---------------------------------------------------------------------------

// puzzle_map[5][5] — position -> entity.
// Negative values are artifact-index-minus-one (so -1 = artifact 0, -2 = artifact 1, etc.);
// Non-negative values are villain indices.
static const signed char PUZZLE_MAP[5][5] = {
    { -1,  7, -2,  6, -3 },
    {  5, 15, 14, 13,  4 },
    { -4, 12, 16, 11, -5 },
    {  3, 10,  9,  8,  2 },
    { -6,  1, -7,  0, -8 },
};

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
    draw_view_panel();

    // Cells span ONLY the map area (240x170), NOT the sidebar — matches
    // . Each cell is 48x34, same as a map tile, so
    // the scepter terrain we draw underneath aligns to the tile grid.
    int cell_w = CL_MAP_W / 5;   // 48
    int cell_h = CL_MAP_H / 5;   // 34
    int grid_x = CL_MAP_X;
    int grid_y = CL_MAP_Y;

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
    int anim_frame = ((int)(GetTime() * 2.0)) & 3;

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
                if (PUZZLE_MAP[j][i] < 0) seq[j][i] = n++;
        // Pass 1: villains.
        for (int j = 0; j < 5; j++)
            for (int i = 0; i < 5; i++)
                if (PUZZLE_MAP[j][i] >= 0) seq[j][i] = n++;
    }

    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 5; i++) {
            signed char id = PUZZLE_MAP[j][i];
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
                face = s->villain_anim[id][anim_frame];
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
                    if (t && t->art[0]) {
                        Texture2D tex = tile_cache_get(t->art);
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
//  WORLDMAP VIEW — 
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
static bool s_worldmap_whole_map = false;
void views_render_worldmap_toggle_hero_only(void) {
    s_worldmap_whole_map = !s_worldmap_whole_map;
}

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
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    const char *txt;
    if (!worldmap_has_orb(g)) {
        txt = ui ? ui->press_esc_to_exit : "Press 'ESC' to exit";
    } else if (s_worldmap_whole_map) {
        txt = ui ? ui->worldmap_hint_your_map : "'ESC' to exit / 'SPC' your map";
    } else {
        txt = ui ? ui->worldmap_hint_whole_map : "'ESC' to exit / 'SPC' whole map";
    }
    bfont_draw_centered(txt,
                        CL_STATUS_X + CL_STATUS_W / 2,
                        CL_STATUS_Y + 1,
                        PAL_CLR(WHITE));
}

static void draw_worldmap(const Game *g, const Map *m, const Fog *f) {
    draw_view_panel();
    draw_worldmap_exit_hint(g);

    if (!m || m->width <= 0 || m->height <= 0) return;

    bool reveal_all = worldmap_has_orb(g) && s_worldmap_whole_map;

    // Compute pixel-per-tile that fits the view panel, integer scaling.
    int avail_w = VIEW_W - 2 * VIEW_PAD;
    int avail_h = VIEW_H - 2 * VIEW_PAD;
    int pix = (avail_w / m->width < avail_h / m->height)
              ? avail_w / m->width : avail_h / m->height;
    if (pix < 1) pix = 1;
    int grid_w = pix * m->width;
    int grid_h = pix * m->height;
    int gx = VIEW_X + (VIEW_W - grid_w) / 2;
    int gy = VIEW_Y + (VIEW_H - grid_h) / 2;

    DrawRectangle(gx, gy, grid_w, grid_h, PAL_CLR(BLACK));

    const ResColors *mm_col = (g && g->res) ? &g->res->colors : NULL;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            if (!reveal_all && !FogSeen(f, x, y)) continue;
            const Tile *t = MapGetTile(m, x, y);
            if (!t) continue;
            DrawRectangle(gx + x * pix, gy + y * pix, pix, pix,
                          terrain_minimap_color(mm_col, t->terrain));
        }
    }

    // Castle/town markers for the current zone: towns = green,
    // castles = red. Fog-gated in normal view, always shown when the
    // orb has revealed the whole map.
    const Resources *r = g->res;
    if (r) {
        for (int i = 0; i < r->town_count; i++) {
            const ResTown *tw = &r->towns[i];
            if (strcmp(tw->zone, g->position.zone) != 0) continue;
            if (tw->x < 0 || tw->y < 0) continue;
            if (!reveal_all && !FogSeen(f, tw->x, tw->y)) continue;
            DrawRectangle(gx + tw->x * pix, gy + tw->y * pix,
                          pix, pix, PAL_CLR(GREEN));
        }
        for (int i = 0; i < r->castle_count; i++) {
            const ResCastle *c = &r->castles[i];
            if (strcmp(c->zone, g->position.zone) != 0) continue;
            if (c->x < 0 || c->y < 0) continue;
            if (!reveal_all && !FogSeen(f, c->x, c->y)) continue;
            DrawRectangle(gx + c->x * pix, gy + c->y * pix,
                          pix, pix, PAL_CLR(RED));
        }
    }

    // Hero position as a blinking yellow/red pixel.
    unsigned k = (unsigned)(GetTime() * 3.0);
    Color blink = (k & 1) ? PAL_CLR(YELLOW) : PAL_CLR(RED);
    DrawRectangle(gx + g->position.x * pix,
                  gy + g->position.y * pix,
                  pix, pix, blink);

    char buf[48];
    snprintf(buf, sizeof(buf), "X=%d Y=%d",
             g->position.x, g->position.y);
    bfont_draw(buf, gx, gy + grid_h + 2, PAL_CLR(WHITE));
}

// ---------------------------------------------------------------------------
//  SPELLS VIEW — combat + adventure spell lists
//  Two columns: Combat (0..6) on left, Adventuring (7..13) on right.
// ---------------------------------------------------------------------------

static void draw_spells(const Game *g) {
    draw_view_panel();

    const ResUI *ui = &g->res->ui;
    bfont_draw_centered(ui->sv_title,
                        VIEW_X + VIEW_W / 2,
                        VIEW_Y + VIEW_PAD,
                        PAL_CLR(YELLOW));

    int head_y = VIEW_Y + VIEW_PAD + GH + 4;
    int col_l = VIEW_X + VIEW_PAD + 4;
    int col_r = VIEW_X + VIEW_W / 2 + 4;
    bfont_draw(ui->sv_combat_col,    col_l, head_y, PAL_CLR(YELLOW));
    bfont_draw(ui->sv_adventure_col, col_r, head_y, PAL_CLR(YELLOW));

    int row_y = head_y + GH + 4;
    int row_h = GH + 1;
    for (int i = 0; i < 7; i++) {
        const SpellDef *sc = spell_by_index(i);
        const SpellDef *sa = spell_by_index(i + 7);
        int cc = g->spells.counts[i];
        int ca = g->spells.counts[i + 7];
        Color lc = (cc > 0) ? PAL_CLR(WHITE) : PAL_CLR(DGREY);
        Color rc = (ca > 0) ? PAL_CLR(WHITE) : PAL_CLR(DGREY);

        char buf[64];
        if (sc) {
            snprintf(buf, sizeof(buf), "%2d %c %s",
                     cc, (char)('A' + i), sc->name);
            bfont_draw(buf, col_l, row_y + i * row_h, lc);
        }
        if (sa) {
            snprintf(buf, sizeof(buf), "%c %-12s %2d",
                     (char)('A' + i), sa->name, ca);
            bfont_draw(buf, col_r, row_y + i * row_h, rc);
        }
    }
}

// ---------------------------------------------------------------------------
// Dispatcher.
// ---------------------------------------------------------------------------

void views_render_draw(const Game *g, const Map *m, const Fog *f,
                        const Sprites *s) {
    ViewKind v = views_active();
    switch (v) {
        case VIEW_CHARACTER: draw_character(g, s);    break;
        case VIEW_ARMY:      draw_army(g, s);         break;
        case VIEW_CONTRACT:  draw_contract(g, s);     break;
        case VIEW_PUZZLE:    draw_puzzle(g, s);       break;
        case VIEW_WORLDMAP:  draw_worldmap(g, m, f);  break;
        case VIEW_SPELLS:    draw_spells(g);          break;
        default: break;
    }
}
