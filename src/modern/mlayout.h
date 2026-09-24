// src/modern/mlayout.h
//
// The modern UI's measures: the rect type every page and content builder
// works in, the padding inside a panel, the backdrop art's authored size, and
// a row's height. Where a panel goes is the page engine's alone
// (src/modern/page.h). Legacy never includes this header; its geometry is the
// frozen CL_CONTENT_* / CL_PANEL_*.

#ifndef OB_MODERN_MLAYOUT_H
#define OB_MODERN_MLAYOUT_H

#include <stdbool.h>

typedef struct { int x, y, w, h; } ML_Rect;

// The gap between parts that are not words (buttons, a bar), in pixels. Words
// are inset UK_INSET (src/modern/uikit.h).
#define ML_PAD 8

// The backdrop art's authored size (drawn at a whole scale, uk_scene_band).
#define ML_BACKDROP_W 240
#define ML_BACKDROP_H 102

// Select rows (the standard for every modern list of choices). A row and its
// ML_ROW_RULE rail are half a tile -- two to a tile -- never shorter than a
// text line plus its padding; on a touch device (a phone or tablet build, or
// --touch) two thirds of a tile. The height is fixed for the session. Rows are
// stacked with the rail between them and under the last; they never stretch
// to fill the column, and a list longer than its space scrolls
// (src/modern/mlist.h).
#define ML_ROW_RULE 2
int     ml_row_h(void);

#endif
