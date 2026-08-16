#include "layout.h"
#include "resources.h"

// Screen geometry is derived from the tile size, not declared alongside it.
// The original 320x200 is exactly what this arithmetic produces for a 48x34
// tile and a 5x5 viewport, so legacy packs land on the historic numbers
// without them being written down anywhere:
//
//   map_w    = 48 * 5                     = 240
//   screen_w = 16 + 240 + 48 + 16         = 320
//   map_h    = 34 * 5                     = 170
//   screen_h = 8 + 9 + 5 + 170 + 8        = 200
//
// The sidebar is one tile wide because it is the purse column, which the
// original sized to a tile. It therefore grows with the tile like the map does.

ClLayout g_layout = {
    // Legacy defaults, so anything that reads the layout before layout_init
    // sees a coherent 320x200 rather than zeroes.
    .tile_w = 48, .tile_h = 34,
    .tiles_w = 5, .tiles_h = 5,
    .map_w = 240, .map_h = 170,
    .sidebar_w = 48,
    .screen_w = 320, .screen_h = 200,
    .default_scale = 2,
    .is_modern = 0,
    .pack_tiles_w = 5, .pack_tiles_h = 5,
    .ui_scale = 1,
};

void layout_init(const struct Resources *res) {
    if (!res) return;
    const ResRender *r = &((const Resources *)res)->render;

    g_layout.tile_w    = r->tile_w;
    g_layout.tile_h    = r->tile_h;
    g_layout.tiles_w   = r->tiles_w;
    g_layout.tiles_h   = r->tiles_h;
    g_layout.pack_tiles_w = r->tiles_w;
    g_layout.pack_tiles_h = r->tiles_h;
    g_layout.ui_scale     = (r->ui_scale > 0) ? r->ui_scale : 1;

    g_layout.map_w     = g_layout.tile_w * g_layout.tiles_w;
    g_layout.map_h     = g_layout.tile_h * g_layout.tiles_h;
    g_layout.sidebar_w = g_layout.tile_w;

    g_layout.screen_w  = CL_FRAME_LEFT_W + g_layout.map_w
                       + g_layout.sidebar_w + CL_FRAME_RIGHT_W;
    g_layout.screen_h  = CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H
                       + g_layout.map_h + CL_FRAME_BOTTOM_H;

    // Legacy opens at 2x because 320x200 is tiny on a modern display. A modern
    // pack is already large -- 800x702 at 2x would be 1600x1404 and taller than
    // a 1080p screen -- so it opens 1:1 and the user scales up if they want to.
    g_layout.default_scale = (r->mode == RENDER_MODE_MODERN) ? 1 : 2;
    g_layout.is_modern     = (r->mode == RENDER_MODE_MODERN);
}

// The buffer IS the window, divided by the scale. The chrome frame therefore
// stretches to the window edge rather than a small buffer being centred in a
// field of black, and the sub-tile remainder lives INSIDE the frame as black
// map pane. The map pane draws whole tiles only: it is cleared to black and
// tiles are laid over it, so leftover space is simply never drawn into.
static int odd_clamp(int n) {
    if (n % 2 == 0) n -= 1;              // odd keeps the hero centred
    if (n < CL_TILES_MIN) n = CL_TILES_MIN;
    if (n > CL_TILES_MAX) n = CL_TILES_MAX;
    return n;
}

int layout_auto_scale(int win_w, int win_h) {
    if (!g_layout.is_modern) return 0;
    // Measure against the SMALLEST viewport, not the pack's declared one. The
    // declared viewport is a preference; the minimum is the guarantee. Using
    // the declared one made ui_scale self-cancelling: doubling the chrome grew
    // the space the declared viewport needed, which dropped the scale back to
    // 1, which left the tiles exactly as small as before.
    int need_w = CL_FRAME_LEFT_W + g_layout.tile_w * CL_TILES_MIN
               + g_layout.sidebar_w + CL_FRAME_RIGHT_W;
    int need_h = CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H
               + g_layout.tile_h * CL_TILES_MIN + CL_FRAME_BOTTOM_H;
    int sx = win_w / need_w, sy = win_h / need_h;
    int s = (sx < sy) ? sx : sy;
    if (s < 1) s = 1;
    if (s > CL_SCALE_MAX) s = CL_SCALE_MAX;
    return s;
}

bool layout_fit_window(int win_w, int win_h, int scale) {
    if (!g_layout.is_modern) return false;   // legacy geometry is fixed
    if (scale < 1) scale = 1;

    // Chrome bands are fixed pixel furniture and do not scale with the tile,
    // so the pane is the buffer minus them.
    int pane_w = win_w / scale - CL_FRAME_LEFT_W - g_layout.sidebar_w
               - CL_FRAME_RIGHT_W;
    int pane_h = win_h / scale - CL_FRAME_TOP_H - CL_STATUS_H - CL_BAR_H
               - CL_FRAME_BOTTOM_H;

    // Floor the pane at the smallest viewport, so a window too small to hold
    // the minimum falls back to the old behaviour: a buffer larger than the
    // window, which present_scaled centres.
    int min_w = g_layout.tile_w * CL_TILES_MIN;
    int min_h = g_layout.tile_h * CL_TILES_MIN;
    if (pane_w < min_w) pane_w = min_w;
    if (pane_h < min_h) pane_h = min_h;

    int tw = odd_clamp(pane_w / g_layout.tile_w);
    int th = odd_clamp(pane_h / g_layout.tile_h);
    // At the tile ceiling the pane would be mostly black, so give back the
    // space the viewport cannot use.
    if (tw == CL_TILES_MAX) pane_w = g_layout.tile_w * tw;
    if (th == CL_TILES_MAX) pane_h = g_layout.tile_h * th;

    int sw = CL_FRAME_LEFT_W + pane_w + g_layout.sidebar_w + CL_FRAME_RIGHT_W;
    int sh = CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H + pane_h
           + CL_FRAME_BOTTOM_H;
    if (sw == g_layout.screen_w && sh == g_layout.screen_h &&
        tw == g_layout.tiles_w && th == g_layout.tiles_h) return false;

    g_layout.tiles_w  = tw;
    g_layout.tiles_h  = th;
    g_layout.map_w    = pane_w;   // full interior; the slack stays black
    g_layout.map_h    = pane_h;
    g_layout.screen_w = sw;
    g_layout.screen_h = sh;
    return true;
}
