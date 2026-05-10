#ifndef OB_LAYOUT_H
#define OB_LAYOUT_H

#include "raylib.h"

// Design-space coordinates for the 320×200 internal render target.
// All values in internal pixels; the renderer scales 2x to the window.

// Internal design resolution.
#define CL_SCREEN_W   320
#define CL_SCREEN_H   200
#define CL_SCALE        2
#define CL_WINDOW_W  (CL_SCREEN_W * CL_SCALE)
#define CL_WINDOW_H  (CL_SCREEN_H * CL_SCALE)

// Tile dimensions.
#define CL_TILE_W 48
#define CL_TILE_H 34

// Chrome strips from DOS_frame_ui[]:
//   [0] Top    { 0, 0, 320,   8 }
//   [1] Left   { 0, 0,  16, 200 }
//   [2] Right  { 300, 0, 16, 200 }
//   [3] Bottom { 0, 0, 320,   8 }  (positioned at y=192)
//   [4] Bar    { 0, 17, 280,  5 }  (positioned at y=17 absolute)
#define CL_FRAME_TOP_H    8
#define CL_FRAME_BOTTOM_H 8
#define CL_FRAME_LEFT_W  16
#define CL_FRAME_RIGHT_W 16
#define CL_BAR_H          5
#define CL_BAR_Y         17

// Status strip between top frame and bar.
// From game.c:111-114:
//   status.x = left_frame->w;
//   status.y = top_frame->h;                     (= 8)
//   status.w = screen->w - left - right;          (= 288)
//   status.h = font.h + zoom;                     (zoom=1, font.h=8 => 9)
#define CL_STATUS_X       CL_FRAME_LEFT_W
#define CL_STATUS_Y       CL_FRAME_TOP_H
#define CL_STATUS_W       (CL_SCREEN_W - CL_FRAME_LEFT_W - CL_FRAME_RIGHT_W)
#define CL_STATUS_H       9

// Map viewport rect.
// From game.c:116-119:
//   map.x = left_frame->w;                        (= 16)
//   map.y = top.h + status.h + bar.h;             (= 8 + 9 + 5 = 22)
//   map.w = screen->w - purse->w - right - left;  (purse = one tile = 48
//                                                  => 320 - 48 - 16 - 16 = 240)
//   map.h = screen->h - top - bar - status - bot; (= 200 - 8 - 5 - 9 - 8 = 170)
#define CL_MAP_X          CL_FRAME_LEFT_W
#define CL_MAP_Y          (CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H)  // 22
#define CL_MAP_W          (CL_TILE_W * 5)                            // 240
#define CL_MAP_H          (CL_TILE_H * 5)                            // 170
#define CL_MAP_TILES_W    5
#define CL_MAP_TILES_H    5

// Sidebar column (between map.w end and right frame start).
#define CL_SIDEBAR_X      (CL_MAP_X + CL_MAP_W)                       // 256
#define CL_SIDEBAR_Y      CL_MAP_Y
#define CL_SIDEBAR_W      (CL_SCREEN_W - CL_SIDEBAR_X - CL_FRAME_RIGHT_W)  // 48
#define CL_SIDEBAR_H      CL_MAP_H

// Bottom chrome strip (the 8px decorative frame at the very bottom of the
// screen). Distinct from the dialog/prompt panel below.
#define CL_BOTTOM_Y       (CL_MAP_Y + CL_MAP_H)                       // 192
#define CL_BOTTOM_H       CL_FRAME_BOTTOM_H                           // 8

// Dialog/prompt panel rect — the blue bottom box. Border is 30 chars ×
// 8 chars plus a few extra pixels (font.h/2). Width matches the map
// area so the sidebar stays visible to the right. Anchored to the
// bottom of the map area.
//
//   30 chars × 8px = 240px wide  → matches CL_MAP_W
//   8 rows × 9px (font.h+1) + 4 (font.h/2) + 2*pad ≈ 80px tall
//
// All popups, location-screen menu panels, and modal banners draw into
// this rect at a fixed size; body text starts at the top of the inner
// area and short content leaves blank rows below.
#define CL_PANEL_X        CL_MAP_X
#define CL_PANEL_W        CL_MAP_W
#define CL_PANEL_H        68
#define CL_PANEL_Y        (CL_MAP_Y + CL_MAP_H - CL_PANEL_H)

#endif
