// src/modern/mlayout.h
//
// The modern UI's five named layouts (REQ-430j). Every modern panel draws into
// one of these, so every screen of a kind renders the same way:
//
//   small      six text lines tall, the pane's width, inset from its edges
//              by the screen's spacing -- prompts and any message that fits
//   large      six by four tiles, centred in the map pane -- long messages,
//              the game menu, combat's spell picker and victory
//   location   a backdrop across the pane top at an integer scale, and the
//              text area under it, both inset by the spacing -- the six
//              location screens
//   full       the map pane, the band and the HUD edge to edge, status band
//              left visible -- every detail view
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

// Text lines the small band holds (it is sized from these and the font).
#define ML_SMALL_LINES 6

// The backdrop art's authored size, and the integer scale it is drawn at.
#define ML_BACKDROP_W 240
#define ML_BACKDROP_H 102

// Select rows (the standard for every modern list of choices). A row is half a
// tile tall -- 48 on Rome's 96 tile, room for one text line centred or a 48 px
// icon beside it, and a comfortable touch target -- and never shorter than a
// text line plus its padding. Rows are stacked from the top of their column
// with a ML_ROW_RULE rail between them and under the last; they never stretch
// to fill the column, and whatever height is left below them stays empty.
#define ML_ROW_RULE 2
int     ml_row_h(void);

// The margin every map panel keeps from the map pane's edges.
int     ml_space(void);

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
