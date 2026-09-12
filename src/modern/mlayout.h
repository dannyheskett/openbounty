// src/modern/mlayout.h
//
// The modern UI's five named layouts (REQ-430j). Every modern panel draws into
// one of these, so every screen of a kind renders the same way:
//
//   small      full pane width, one tile tall, bottom of the map pane --
//              prompts and any message that fits
//   large      six by four tiles, centred in the map pane -- long messages,
//              the game menu, combat's spell picker and victory
//   location   a backdrop across the pane top at an integer scale, and the
//              text area under it, reaching the HUD -- the six location screens
//   full       the map pane plus the HUD, status band left visible -- every
//              detail view
//   (toast     one line at the top of the pane, unchanged)
//
// All of it is computed from the map pane, the sidebar and the tile, never
// from ui_scale: the DOS original's 240x170 content rect times ui_scale is
// what made a 672 px pane hold 14 characters a line. Legacy never includes
// this header; its geometry is the frozen CL_CONTENT_* / CL_PANEL_*.

#ifndef OB_MODERN_MLAYOUT_H
#define OB_MODERN_MLAYOUT_H

typedef struct { int x, y, w, h; } ML_Rect;

// Horizontal and vertical padding inside every modern panel, in pixels.
#define ML_PAD 8

// The backdrop art's authored size, and the integer scale it is drawn at.
#define ML_BACKDROP_W 240
#define ML_BACKDROP_H 102

ML_Rect ml_small(void);
ML_Rect ml_large(void);
ML_Rect ml_full(void);
// The location backdrop as drawn (already cropped to the pane width) and the
// scale it was drawn at, so art placed in backdrop units lands on it.
ML_Rect ml_loc_backdrop(void);
int     ml_loc_scale(void);
ML_Rect ml_loc_text(void);

// Text capacity of a rect with ML_PAD on every side, from the live font.
int ml_cols(ML_Rect r);
int ml_lines(ML_Rect r);

#endif
