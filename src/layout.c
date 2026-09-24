#include "layout.h"
#include "bfont.h"
#include "resources.h"
#include "combat.h"   // COMBAT_W / COMBAT_H -- the battlefield does not resize

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
    .frame_l = 16, .frame_r = 16, .frame_t = 8, .frame_b = 8,
    .sidebar_gap = 0,
    .is_native = 0,
};

// The chrome bands at their thinnest: the DOS_frame_ui[] strips times the
// pack's ui_scale. Every mode starts from these; a fixed buffer widens them.
static void set_base_frame(void) {
    g_layout.frame_l = 16 * g_layout.ui_scale;
    g_layout.frame_r = 16 * g_layout.ui_scale;
    g_layout.frame_t =  8 * g_layout.ui_scale;
    g_layout.frame_b =  8 * g_layout.ui_scale;
}

static int odd_clamp(int n);

// A declared buffer's screen at w x h: everything between the two columns is
// map. The map counts an odd number of whole tiles each way, never fewer than
// the pack declared; it draws them centred across, with a part tile either
// side, and down from its top, with a part row at its foot (map_render.c).
// Past the tile ceiling the screen stops growing and the surface's remainder
// is letterboxed.
static void native_fit(int w, int h) {
    int side = CL_FRAME_LEFT_W + g_layout.rail_w + g_layout.sidebar_gap;
    int pane_w = w - 2 * side;
    int pane_h = h - CL_FRAME_TOP_H - CL_FRAME_BOTTOM_H;
    int tw = odd_clamp(pane_w / g_layout.tile_w);
    int th = odd_clamp(pane_h / g_layout.tile_h);
    if (tw < g_layout.pack_tiles_w) tw = g_layout.pack_tiles_w;
    if (th < g_layout.pack_tiles_h) th = g_layout.pack_tiles_h;
    // At the ceiling a part tile either side is all the pane can show.
    if (tw == CL_TILES_MAX && pane_w > (tw + 1) * g_layout.tile_w)
        pane_w = (tw + 1) * g_layout.tile_w;
    if (th == CL_TILES_MAX && pane_h > (th + 1) * g_layout.tile_h)
        pane_h = (th + 1) * g_layout.tile_h;
    g_layout.tiles_w  = tw;
    g_layout.tiles_h  = th;
    g_layout.map_w    = pane_w;
    g_layout.map_h    = pane_h;
    g_layout.screen_w = pane_w + 2 * side;
    g_layout.screen_h = pane_h + CL_FRAME_TOP_H + CL_FRAME_BOTTOM_H;
}

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
    g_layout.sidebar_gap  = 0;
    g_layout.rail_w       = 0;
    g_layout.no_band      = 0;
    g_layout.native_w     = 0;
    g_layout.native_h     = 0;
    g_layout.is_native    = 0;
    set_base_frame();

    g_layout.map_w     = g_layout.tile_w * g_layout.tiles_w;
    g_layout.map_h     = g_layout.tile_h * g_layout.tiles_h;
    g_layout.sidebar_w = g_layout.tile_w;

    g_layout.screen_w  = CL_FRAME_LEFT_W + g_layout.map_w
                       + g_layout.sidebar_w + CL_FRAME_RIGHT_W;
    g_layout.screen_h  = CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H
                       + g_layout.map_h + CL_FRAME_BOTTOM_H;

    // A declared buffer: the smallest screen the pack is drawn on. Across it,
    // the frame, the left column (the rail), a band, the map, a band, the
    // right column (the HUD) and the frame; down it, the frame, the map and the
    // frame -- no status band. Rome's 800 x 504 is 12 + 96 + 4 + 576 + 4 + 96
    // + 12 by 12 + 480 + 12. resources_load has already rejected a buffer too
    // small to hold it.
    if (r->mode == RENDER_MODE_MODERN && r->native_w > 0 && r->native_h > 0) {
        int frame = RES_MODERN_FRAME * g_layout.ui_scale;
        g_layout.frame_l = g_layout.frame_r = frame;
        g_layout.frame_t = g_layout.frame_b = frame;
        g_layout.sidebar_gap = RES_MODERN_GAP * g_layout.ui_scale;
        g_layout.rail_w      = g_layout.tile_w;
        g_layout.no_band     = 1;
        g_layout.native_w    = r->native_w;
        g_layout.native_h    = r->native_h;
        g_layout.is_native   = 1;
        native_fit(r->native_w, r->native_h);
    }

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

void layout_min_window(int *out_w, int *out_h) {
    // A declared buffer is the smallest screen: the window never goes below it.
    if (g_layout.is_native) {
        if (out_w) *out_w = g_layout.native_w;
        if (out_h) *out_h = g_layout.native_h;
        return;
    }
    // Two things set the floor, and the pack's tile size moves both, so this
    // cannot be a constant:
    //
    //   The battlefield is a fixed COMBAT_W x COMBAT_H grid of one-tile cells.
    //   Unlike the map viewport it cannot shed cells to fit a smaller window --
    //   making it fit would mean scaling the combat screen separately, which is
    //   a whole rendering path that does not exist.
    //
    //   The map viewport will not shrink below CL_TILES_MIN tiles plus the
    //   one-tile sidebar.
    //
    // For the 48x34 pack these come out equal and give exactly 320x200 -- the
    // value that used to be hardcoded -- because the sidebar is one tile wide,
    // so CL_TILES_MIN + 1 == COMBAT_W. A pack that changes either number gets a
    // floor that still holds.
    int need_w = COMBAT_W * g_layout.tile_w;
    int alt_w  = CL_TILES_MIN * g_layout.tile_w + g_layout.sidebar_w;
    if (alt_w > need_w) need_w = alt_w;

    int need_h = COMBAT_H * g_layout.tile_h;
    int alt_h  = CL_TILES_MIN * g_layout.tile_h;
    if (alt_h > need_h) need_h = alt_h;

    if (out_w) *out_w = need_w + CL_FRAME_LEFT_W + CL_FRAME_RIGHT_W;
    if (out_h) *out_h = need_h + CL_FRAME_TOP_H + CL_STATUS_H
                      + CL_BAR_H + CL_FRAME_BOTTOM_H;
}

// A declared buffer takes the whole surface (layout.h). The declared size is
// the floor: below it the screen stays at the declared size and present.c fits
// it down to the surface.
bool layout_grow_native(int surface_w, int surface_h, int scale) {
    if (!g_layout.is_modern || !g_layout.is_native) return false;
    if (scale < 1) scale = 1;
    int w = surface_w / scale, h = surface_h / scale;
    if (w < g_layout.native_w) w = g_layout.native_w;
    if (h < g_layout.native_h) h = g_layout.native_h;
    int was_w = g_layout.screen_w, was_h = g_layout.screen_h;
    int was_tw = g_layout.tiles_w, was_th = g_layout.tiles_h;
    native_fit(w, h);
    return g_layout.screen_w != was_w || g_layout.screen_h != was_h ||
           g_layout.tiles_w != was_tw || g_layout.tiles_h != was_th;
}

bool layout_fit_window(int win_w, int win_h, int scale) {
    if (!g_layout.is_modern) return false;   // legacy geometry is fixed
    if (g_layout.is_native) return false;    // so is a declared buffer
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
