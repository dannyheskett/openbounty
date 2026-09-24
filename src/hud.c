#include "hud.h"
#include "gfx.h"
#include "layout.h"
#include "lattice.h"
#include "palette.h"
#include "bfont.h"
#include "ui.h"
#include "ob_types.h"
#include "touch.h"
#include "uitouch.h"
#include "views.h"
#include "overlay.h"
#include "prompt_impl.h"
#include "input_host.h"
#include "modern/page.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include <stdio.h>

// The puzzle's layout is the engine's (puzzle_grid_entity, tables.c), the
// one the full-screen view uses too: each cell a villain (>= 0) or an
// artifact (-id-1), covered until it is caught or found.

// Sidebar panel sprites are one tile each. The sidebar column is
// CL_SIDEBAR_W wide (48 in legacy), starts at CL_SIDEBAR_X. Five panels stack
// from CL_SIDEBAR_Y downward.
//
// OpenKB's draw_sidebar (its game.c:1074) blits 5 panels from the `cursor`
// strip: frame 8 = contract, 9 = siege_empty, 10 = magic_empty,
// 11 = puzzle, 12 = gold_purse. When siege is owned it cycles through
// frames 0..3 of a siege-weapons strip (our hud_siege_anim). Same for
// knows_magic: cycle frames 4..7 of the same strip.
//
// Legacy frames every panel. The modern columns do not: a column is one piece,
// its tiles parted by a band (hud_column_finish), so no edge is drawn twice.

static void blit_tile(Texture2D t, int x, int y, bool frame) {
    if (t.id == 0) return;
    Rectangle src = { 0, 0, (float)t.width, (float)t.height };
    Rectangle dst = { (float)x, (float)y,
                      (float)CL_SIDEBAR_W, (float)CL_TILE_H };
    gfx_texture_draw(t, src, dst, WHITE);
    if (frame) legacy_panel_frame(x, y, CL_SIDEBAR_W, CL_TILE_H);
}

static void siege_tile(const Game *g, const Sprites *s, int x, int y, bool frame) {
    if (g && g->stats.siege_weapons) {
        blit_tile(sprites_strip(s->hud_siege_anim, s->hud_siege_anim_frames,
                                (int)(ui_anim_time() * 2.0)), x, y, frame);
    } else {
        blit_tile(s->hud_siege_silhouette, x, y, frame);
    }
}

// A number on a tile's foot, at its right: shortened ("280k", "2m") when the
// tile is too narrow for it -- the gold and the days alike.
static void tile_number(long v, int x, int y, Color c) {
    char buf[24];
    snprintf(buf, sizeof buf, "%ld", v);
    int room = CL_SIDEBAR_W - 4 * CL_UI;
    if (bfont_text_width(buf) > room) {
        if (v >= 1000000) snprintf(buf, sizeof buf, "%ldm", v / 1000000);
        else              snprintf(buf, sizeof buf, "%ldk", v / 1000);
    }
    int w = bfont_text_width(buf);
    bfont_draw(buf, x + CL_SIDEBAR_W - w - 2 * CL_UI, y + CL_TILE_H - BFONT_GLYPH_H - 2 * CL_UI, c);
}

static void gold_tile(const Game *g, const Sprites *s, int x, int y, bool frame) {
    blit_tile(s->hud_gold_purse, x, y, frame);
    if (!g) return;
    if (CL_IS_MODERN) { tile_number(g->stats.gold, x, y, PAL_CLR(YELLOW)); return; }
    // Legacy's purse is the DOS one: the number as it is.
    char gold_str[16];
    snprintf(gold_str, sizeof(gold_str), "%d", g->stats.gold);
    Vector2 gsz = bfont_measure(gold_str);
    int gx = x + CL_SIDEBAR_W - (int)gsz.x - 2 * CL_UI;
    int gy = y + CL_TILE_H - BFONT_GLYPH_H - 2 * CL_UI;
    bfont_draw(gold_str, gx, gy, PAL_CLR(YELLOW));
}

void hud_draw_puzzle_tile(const Game *g, const Sprites *s, int x, int y, bool frame) {
    if (!s) return;
    // The empty grid, and a cover sprite over each piece NOT yet uncovered:
    // covers vanish as villains are caught and artifacts found, so the grid
    // fills in toward the scepter's location.
    blit_tile(s->hud_puzzle_grid, x, y, frame);
    if (g && s->puzzle_cover.id) {
        // : stamp piece.png (9x6) over each
        // uncaught/unfound cell with a 2px inset within the panel,
        // leaving the underlying map-fragment art visible only on
        // caught/found cells.
        // Legacy: the chip (9x6) and its 2 px inset on the 48x34 panel, as the
        // original. Modern: the chip at the largest whole multiple the panel
        // holds over that one, in five square cells a chip wide, centred --
        // never stretched by a different amount in x and y.
        int cw = 9, ch = 6, px = 9, py = 6, ins_x = 2, ins_y = 2;
        if (CL_IS_MODERN) {
            int k = CL_SIDEBAR_W / 48 < CL_TILE_H / 34 ? CL_SIDEBAR_W / 48 : CL_TILE_H / 34;
            if (k < 1) k = 1;
            cw = 9 * k; ch = 6 * k; px = py = cw;
            ins_x = (CL_SIDEBAR_W - 5 * px) / 2;
            ins_y = (CL_TILE_H - 5 * py) / 2 + (py - ch) / 2;
        }
        for (int j = 0; j < 5; j++) {
            for (int i = 0; i < 5; i++) {
                int id = puzzle_grid_entity(j, i);
                bool caught;
                if (id < 0) caught = g->artifacts.found[-id - 1];
                else        caught = g->contract.villains_caught[id];
                if (caught) continue;
                ui_blit(s->puzzle_cover, x + ins_x + i * px, y + ins_y + j * py, cw, ch);
            }
        }
        if (frame) legacy_panel_frame(x, y, CL_SIDEBAR_W, CL_TILE_H);
    }
}

// Modern: the days left on the sundial -- or, while Time Stop runs, the steps
// it has left, in its own colour.
static void days_tile(const Game *g, const Sprites *s, int x, int y) {
    blit_tile(s->hud_days, x, y, false);
    if (!g) return;
    bool stop = g->stats.time_stop > 0;
    tile_number(stop ? g->stats.time_stop : g->stats.days_left, x, y,
                stop ? PAL_CLR(CYAN) : PAL_CLR(YELLOW));
}

void hud_key_hint(int x, int y, const char *key) {
    if (!key || !key[0] || !ml_keys_shown()) return;
    int pad = 2 * CL_UI;
    int w = bfont_text_width(key) + 2 * pad;
    int h = BFONT_GLYPH_H + 2 * pad;
    gfx_rect(x + pad, y + pad, w, h, uk_hint_bg());
    bfont_draw(key, x + 2 * pad, y + 2 * pad, PAL_CLR(YELLOW));
}

void hud_column_finish(int x, int y, int w, int h, int tiles) {
    // A band across every edge of every tile -- the join between two tiles,
    // and the column's top and foot too -- each half on the tile, so every
    // tile gives the same share of its art: a pixel at its top and at its
    // foot. Below the last tile, the column's own dark fill.
    const int band = 2 * CL_UI;
    int end = y + tiles * CL_TILE_H;
    if (end < y + h) lattice_ground(x, end, w, y + h - end);
    for (int i = 0; i <= tiles; i++)
        lattice_band_h(x, y + i * CL_TILE_H - band / 2, w, band);
}

// Each panel stands for a screen, so each panel opens it: the same rule the
// rail on the other side follows, and the same list order the sidebar draws.
static const InputAction HUD_ACTIONS[5] = {
    INPUT_ACTION_VIEW_CONTRACT,     // the villain's face
    INPUT_ACTION_VIEW_CHARACTER,    // siege weapons are reported there
    INPUT_ACTION_CAST_SPELL,        // the magic star
    INPUT_ACTION_VIEW_CHARACTER,    // the purse
    INPUT_ACTION_VIEW_CHARACTER,    // days remaining
};
// The key each panel's screen answers to (src/input.c).
static const char *const HUD_KEYS[5] = { "I", "V", "U", "V", "V" };

InputAction hud_tapped(void) {
    if (!CL_IS_MODERN) return INPUT_ACTION_NONE;
    int row = touch_tapped_row(TOUCH_LIST_HUD);
    if (row < 0 || row >= 5) return INPUT_ACTION_NONE;
    return HUD_ACTIONS[row];
}

static void contract_tile(const Game *g, const Sprites *s, int x, int y, bool frame) {
    // Contract panel (+ villain portrait overlay if there's an active
    // contract). overlays the villain face right on top.
    blit_tile(s->hud_contract_silhouette, x, y, frame);
    if (g && g->contract.active_id[0]) {
        const VillainDef *v = villain_by_id(g->contract.active_id);
        if (v && v->index >= 0 && v->index < s->villain_count) {
            // ticks the sidebar at frame speed (~2/sec). Use the
            // animation strip if loaded; fall back to the static portrait.
            Texture2D face = sprites_strip(s->villain_anim[v->index],
                                           s->villain_anim_frames[v->index],
                                           (int)(ui_anim_time() * 2.0));
            if (!face.id) face = s->villain_portrait[v->index];
            blit_tile(face, x, y, frame);
        }
    }
}

static void magic_tile(const Game *g, const Sprites *s, int x, int y, bool frame) {
    // Magic star: silhouette until knows_magic, then animated star.
    // Lit for the rites of the zone the hero stands in (one magic: knowing it).
    if (g && GameHasRites(g, g->position.zone)) {
        blit_tile(sprites_strip(s->hud_magic_anim, s->hud_magic_anim_frames,
                                (int)(ui_anim_time() * 2.0)), x, y, frame);
    } else {
        blit_tile(s->hud_magic_silhouette, x, y, frame);
    }
}

void hud_draw(const Game *g, const Sprites *s) {
    if (!s) return;
    int x = CL_SIDEBAR_X;
    int y = CL_SIDEBAR_Y;

    if (!CL_IS_MODERN) {
        // Legacy: the original's five framed panels -- contract, siege,
        // magic, puzzle, gold.
        contract_tile(g, s, x, y, true);   y += CL_TILE_H;
        siege_tile(g, s, x, y, true);      y += CL_TILE_H;
        magic_tile(g, s, x, y, true);      y += CL_TILE_H;
        hud_draw_puzzle_tile(g, s, x, y, true);
        y += CL_TILE_H;
        gold_tile(g, s, x, y, true);
        return;
    }

    // Modern: contract, siege, magic, gold and the days, one column. The
    // puzzle is the left column's last tile (src/modern/rail.c). A page of its
    // own owns every tap while it is up.
    bool page = views_active() != VIEW_NONE || dialog_is_active() || prompt_is_active();
    contract_tile(g, s, x, y + 0 * CL_TILE_H, false);
    siege_tile(g, s, x, y + 1 * CL_TILE_H, false);
    magic_tile(g, s, x, y + 2 * CL_TILE_H, false);
    gold_tile(g, s, x, y + 3 * CL_TILE_H, false);
    days_tile(g, s, x, y + 4 * CL_TILE_H);
    hud_column_finish(x, y, CL_SIDEBAR_W, CL_SIDEBAR_H, 5);
    // Its keys and its taps while nothing is open over it: a page owns them.
    for (int i = 0; i < 5 && !page; i++) {
        hud_key_hint(x, y + i * CL_TILE_H, HUD_KEYS[i]);
        ui_tile_row(x, y + i * CL_TILE_H, CL_SIDEBAR_W, CL_TILE_H, TOUCH_LIST_HUD, i);
    }
}
