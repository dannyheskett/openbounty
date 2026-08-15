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
};

void layout_init(const struct Resources *res) {
    if (!res) return;
    const ResRender *r = &((const Resources *)res)->render;

    g_layout.tile_w    = r->tile_w;
    g_layout.tile_h    = r->tile_h;
    g_layout.tiles_w   = r->tiles_w;
    g_layout.tiles_h   = r->tiles_h;

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
