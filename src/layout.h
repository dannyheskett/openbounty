#ifndef OB_LAYOUT_H
#define OB_LAYOUT_H

#include "raylib.h"
#include <stdbool.h>

// Design-space coordinates for the internal render target. All values are in
// internal pixels; present.c blits that target to the window at an integer
// scale.
//
// The geometry is no longer fixed. A pack declares render.mode: "legacy" keeps
// the 320x200 / 48x34 / 5x5 layout the DOS original used, and "modern" allows
// square tiles and a larger viewport. layout_init() fills g_layout from the
// pack before the render target or window exist; every CL_ macro below reads
// from it, so the ~340 call sites across the shell are unchanged.
//
// The chrome bands (frame, status, bar) stay literal: they are text and border
// furniture measured in pixels, not in tiles, and do not scale with tile size.

typedef struct {
    int tile_w, tile_h;        // one map tile
    int tiles_w, tiles_h;      // viewport size, in tiles (odd, so the hero centres)
    int map_w, map_h;          // tile_* * tiles_*
    int sidebar_w;             // one tile wide, as in the original
    int screen_w, screen_h;    // derived from the map plus the chrome bands
    int default_scale;         // window scale at startup; modern is already large
    int is_modern;             // 1 in modern mode; legacy must behave as it always did
    int pack_tiles_w, pack_tiles_h;  // the viewport the pack declared: a
                                     // guaranteed minimum, not a fixed size
    int ui_scale;              // multiplies the font and the chrome bands
} ClLayout;

extern ClLayout g_layout;

// True only for a pack that declared render.mode "modern". Legacy packs must be
// indistinguishable from the build before render modes existed, so anything new
// -- the scale control included -- is gated on this.
#define CL_IS_MODERN (g_layout.is_modern)

// Fill g_layout from the pack's render block. Must be called after
// resources_load and before the render target and window are created.
struct Resources;
void layout_init(const struct Resources *res);

// Modern only: grow the viewport to as many whole tiles as fit the window at
// `scale`, and re-derive the screen size. Legacy is a no-op -- its geometry is
// fixed. Returns true when the screen size changed, so the caller knows to
// recreate the render target. The remainder is letterboxed by present_scaled.
bool layout_fit_window(int win_w, int win_h, int scale);


// Viewport bounds. The floor matches what fog reveals (a 5x5 square, see
// FogReveal): a wider viewport would show tiles the game never uncovers from
// where the hero stands. The ceiling just stops absurd windows.
#define CL_TILES_MIN 5
#define CL_TILES_MAX 63   // one under MAP_MAX_W, so the camera clamp holds

#define CL_SCREEN_W  (g_layout.screen_w)
#define CL_SCREEN_H  (g_layout.screen_h)
#define CL_SCALE     (g_layout.default_scale)
#define CL_WINDOW_W  (CL_SCREEN_W * CL_SCALE)
#define CL_WINDOW_H  (CL_SCREEN_H * CL_SCALE)

// Presentation scale bounds. The 320x200 target is blitted at an INTEGER
// scale so pixels stay square; these clamp how far that goes.
//
// The floor keeps the game legible in a small window. The ceiling exists
// because the scale otherwise grows to whatever the display allows: a 4K
// screen produced 10x, which is not blurry -- integer scaling holds -- but
// blows an 8x8 glyph up to 80px and looks grotesque. Browsers get a tighter
// ceiling than desktop: a tab is usually one window among many, and the
// hosted build should read as a game window rather than fill the screen.
//
// See src/present.c, which is the only place these are applied.
#define CL_SCALE_MIN      2   // 640x400
#define CL_SCALE_MAX      5   // desktop -> 1600x1000
#define CL_SCALE_MAX_WEB  3   // browser -> 960x600

// Tile dimensions. Pack-declared; 48x34 in legacy mode.
#define CL_TILE_W (g_layout.tile_w)
#define CL_TILE_H (g_layout.tile_h)

// Chrome strips from DOS_frame_ui[]:
//   [0] Top    { 0, 0, 320,   8 }
//   [1] Left   { 0, 0,  16, 200 }
//   [2] Right  { 300, 0, 16, 200 }
//   [3] Bottom { 0, 0, 320,   8 }  (positioned at y=192)
//   [4] Bar    { 0, 17, 280,  5 }  (positioned at y=17 absolute)
// Multiplied by the pack's ui_scale: a pack that doubles its tile doubles its
// furniture too, or it gets a 16px frame and an 8px font around 192px tiles.
// At ui_scale 1 each of these is the literal it replaced.
#define CL_UI             (g_layout.ui_scale)
#define CL_FRAME_TOP_H    ( 8 * CL_UI)
#define CL_FRAME_BOTTOM_H ( 8 * CL_UI)
#define CL_FRAME_LEFT_W   (16 * CL_UI)
#define CL_FRAME_RIGHT_W  (16 * CL_UI)
#define CL_BAR_H          ( 5 * CL_UI)
#define CL_BAR_Y          (17 * CL_UI)

// Status strip between top frame and bar.
// From OpenKB's game.c:111-114:
//   status.x = left_frame->w;
//   status.y = top_frame->h;                     (= 8)
//   status.w = screen->w - left - right;          (= 288)
//   status.h = font.h + zoom;                     (zoom=1, font.h=8 => 9)
#define CL_STATUS_X       CL_FRAME_LEFT_W
#define CL_STATUS_Y       CL_FRAME_TOP_H
#define CL_STATUS_W       (CL_SCREEN_W - CL_FRAME_LEFT_W - CL_FRAME_RIGHT_W)
#define CL_STATUS_H       (9 * CL_UI)

// Map viewport rect.
// From OpenKB's game.c:116-119:
//   map.x = left_frame->w;                        (= 16)
//   map.y = top.h + status.h + bar.h;             (= 8 + 9 + 5 = 22)
//   map.w = screen->w - purse->w - right - left;  (purse = one tile = 48
//                                                  => 320 - 48 - 16 - 16 = 240)
//   map.h = screen->h - top - bar - status - bot; (= 200 - 8 - 5 - 9 - 8 = 170)
#define CL_MAP_X          CL_FRAME_LEFT_W
#define CL_MAP_Y          (CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H)  // 22
#define CL_MAP_W          (g_layout.map_w)     // legacy: tile_w * tiles_w;
                                               // modern: the whole interior
#define CL_MAP_H          (g_layout.map_h)     // tile_h * tiles_h
#define CL_MAP_TILES_W    (g_layout.tiles_w)
#define CL_MAP_TILES_H    (g_layout.tiles_h)

// Sidebar column (between map.w end and right frame start).
#define CL_SIDEBAR_X      (CL_MAP_X + CL_MAP_W)                       // 256
#define CL_SIDEBAR_Y      CL_MAP_Y
#define CL_SIDEBAR_W      (g_layout.sidebar_w)                        // one tile
#define CL_SIDEBAR_H      CL_MAP_H

// Bottom chrome strip (the 8px decorative frame at the very bottom of the
// screen). Distinct from the dialog/prompt panel below.
#define CL_BOTTOM_Y       (CL_MAP_Y + CL_MAP_H)                       // 192
#define CL_BOTTOM_H       CL_FRAME_BOTTOM_H                           // 8

// Dialog/prompt panel rect -- the blue bottom box. Border is 30 chars x
// 8 chars plus a few extra pixels (font.h/2). Width matches the map
// area so the sidebar stays visible to the right. Anchored to the
// bottom of the map area.
//
//   30 chars x 8px = 240px wide  -> matches CL_MAP_W
//   8 rows x 9px (font.h+1) + 4 (font.h/2) + 2*pad ~ 80px tall
//
// All popups, location-screen menu panels, and modal banners draw into
// this rect at a fixed size; body text starts at the top of the inner
// area and short content leaves blank rows below.
// Widened 5px to the LEFT of the map so a full 30-character line fits WITH
// a normal text margin. The right edge stays flush with the sidebar at 256;
// the 5px is borrowed from the left frame chrome, which is decorative.
//
// At the map's own 240px a 30-char line was impossible: DrawRectangleLines
// puts a 1px border on each edge, leaving a 238px interior against 30*8 =
// 240px of cells. Widening by only 2px fit the text but left it jammed
// against the border with no margin.
//
//   col 11 = left border
//       12 13 14  = margin (CL_PANEL_PAD_X - 1)
//       15 ....................... 254  = 240px = exactly 30 cells
//      255 = right border
//
// The margin is one-sided by construction: the right edge is pinned to the
// sidebar, so a full-width line ends at 254 with only the border beyond it.
// That is why the column budget is the CL_PANEL_COLS constant below and NOT
// the symmetric (w - 2*pad)/GW -- that formula assumes equal margins and
// would hand back the 30th column.
// Content rect -- where fixed-size screens draw. Sized from its content (30
// columns of 8px glyphs, and the 170px the original gave them) times ui_scale,
// then CENTRED in the map pane. It must not be the pane itself: the pane grows
// with the window, and a 30-column screen stretched across it puts the labels
// at one edge and their values at the other.
//
// In legacy the pane is exactly 240x170 and the content rect is exactly 240x170,
// so both offsets are zero and this lands on the historic 16,22 unchanged.
#define CL_CONTENT_W      (240 * CL_UI)
#define CL_CONTENT_H      (170 * CL_UI)
#define CL_CONTENT_X      (CL_MAP_X + (CL_MAP_W - CL_CONTENT_W) / 2)
#define CL_CONTENT_Y      (CL_MAP_Y + (CL_MAP_H - CL_CONTENT_H) / 2)

#define CL_PANEL_X        (CL_CONTENT_X - 5 * CL_UI)
#define CL_PANEL_W        (CL_CONTENT_W + 5 * CL_UI)
#define CL_PANEL_H        (68 * CL_UI)
#define CL_PANEL_Y        (CL_CONTENT_Y + CL_CONTENT_H - CL_PANEL_H)
#define CL_PANEL_PAD_X    (4 * CL_UI)
#define CL_PANEL_COLS     30

#endif
