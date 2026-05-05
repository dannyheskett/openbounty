#include "hud.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "raylib.h"
#include <stdio.h>

// Same puzzle layout used by the full-screen view (views.c). Each cell
// shows a villain (>=0) or artifact (-id-1) face when the piece is
// still covered. Caught/found pieces are uncovered so the underlying
// grid background shows through.
static const signed char PUZZLE_MAP[5][5] = {
    { -1,  7, -2,  6, -3 },
    {  5, 15, 14, 13,  4 },
    { -4, 12, 16, 11, -5 },
    {  3, 10,  9,  8,  2 },
    { -6,  1, -7,  0, -8 },
};

// Sidebar panel sprites are all 48x34 (one tile). The sidebar column is
// CL_SIDEBAR_W wide (48), starts at CL_SIDEBAR_X. Five panels stack from
// CL_SIDEBAR_Y downward.
//
// draw_sidebar (game.c:1074) blits 5 panels from the `cursor`
// strip: frame 8 = contract, 9 = siege_empty, 10 = magic_empty,
// 11 = puzzle, 12 = gold_purse. When siege is owned it cycles through
// frames 0..3 of a siege-weapons strip (our hud_siege_anim). Same for
// knows_magic: cycle frames 4..7 of the same strip.

static void blit_panel(Texture2D t, int x, int y) {
    if (t.id == 0) return;
    Rectangle src = { 0, 0, (float)t.width, (float)t.height };
    Rectangle dst = { (float)x, (float)y,
                      (float)CL_SIDEBAR_W, (float)CL_TILE_H };
    DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

void hud_draw(const Game *g, const Sprites *s) {
    if (!s) return;
    int x = CL_SIDEBAR_X;
    int y = CL_SIDEBAR_Y;

    // 1. Contract panel (+ villain portrait overlay if there's an active
    //    contract). overlays the villain face right on top.
    blit_panel(s->hud_contract_silhouette, x, y);
    if (g && g->contract.active_id[0]) {
        const VillainDef *v = villain_by_id(g->contract.active_id);
        if (v && v->index >= 0 && v->index < 17) {
            // ticks the sidebar at frame speed (≈2/sec). Use the
            // 4-frame strip if loaded; fall back to the static portrait.
            int frame = ((int)(GetTime() * 2.0)) & 3;
            Texture2D face = s->villain_anim[v->index][frame];
            if (!face.id) face = s->villain_portrait[v->index];
            blit_panel(face, x, y);
        }
    }
    y += CL_TILE_H;

    // 2. Siege weapons: silhouette when not owned, animated cart when owned.
    if (g && g->stats.siege_weapons) {
        int frame = ((int)(GetTime() * 2.0)) & 3;
        blit_panel(s->hud_siege_anim[frame], x, y);
    } else {
        blit_panel(s->hud_siege_silhouette, x, y);
    }
    y += CL_TILE_H;

    // 3. Magic star: silhouette until knows_magic, then animated star.
    if (g && g->stats.knows_magic) {
        int frame = ((int)(GetTime() * 2.0)) & 3;
        blit_panel(s->hud_magic_anim[frame], x, y);
    } else {
        blit_panel(s->hud_magic_silhouette, x, y);
    }
    y += CL_TILE_H;

    // 4. Puzzle map: empty grid background + a cover sprite over each
    //    NOT-yet-uncovered cell ( draws GR_PIECE
    //    over uncaught/unfound cells; covers vanish as the player
    //    progresses, so the grid fills in toward the scepter location).
    blit_panel(s->hud_puzzle_grid, x, y);
    if (g && s->puzzle_cover.id) {
        // : stamp piece.png (9x6) over each
        // uncaught/unfound cell with a 2px inset within the panel,
        // leaving the underlying map-fragment art visible only on
        // caught/found cells.
        int cw = s->puzzle_cover.width;   // 9
        int ch = s->puzzle_cover.height;  // 6
        int inset = 2;
        for (int j = 0; j < 5; j++) {
            for (int i = 0; i < 5; i++) {
                signed char id = PUZZLE_MAP[j][i];
                bool caught;
                if (id < 0) caught = g->artifacts.found[-id - 1];
                else        caught = g->contract.villains_caught[id];
                if (caught) continue;
                int cx = x + inset + i * cw;
                int cy = y + inset + j * ch;
                Rectangle src = { 0, 0, (float)cw, (float)ch };
                Rectangle dst = { (float)cx, (float)cy,
                                  (float)cw, (float)ch };
                DrawTexturePro(s->puzzle_cover, src, dst,
                               (Vector2){ 0, 0 }, 0.0f, WHITE);
            }
        }
    }
    y += CL_TILE_H;

    // 5. Gold purse + numeric label.
    blit_panel(s->hud_gold_purse, x, y);
    if (g) {
        char gold_str[12];
        snprintf(gold_str, sizeof(gold_str), "%d", g->stats.gold);
        Vector2 gsz = bfont_measure(gold_str);
        int gx = x + CL_SIDEBAR_W - (int)gsz.x - 2;
        int gy = y + CL_TILE_H - BFONT_GLYPH_H - 2;
        bfont_draw(gold_str, gx, gy, PAL_CLR(YELLOW));
    }
}
