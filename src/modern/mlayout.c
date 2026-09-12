// src/modern/mlayout.c -- the five modern layouts (see mlayout.h).

#include "mlayout.h"
#include "layout.h"
#include "bfont.h"

ML_Rect ml_small(void) {
    ML_Rect r = { CL_MAP_X, CL_MAP_Y + CL_MAP_H - CL_TILE_H, CL_MAP_W, CL_TILE_H };
    return r;
}

ML_Rect ml_large(void) {
    // Six by four tiles, never larger than the pane.
    int w = 6 * CL_TILE_W, h = 4 * CL_TILE_H;
    if (w > CL_MAP_W) w = CL_MAP_W;
    if (h > CL_MAP_H) h = CL_MAP_H;
    ML_Rect r = { CL_MAP_X + (CL_MAP_W - w) / 2, CL_MAP_Y + (CL_MAP_H - h) / 2, w, h };
    return r;
}

ML_Rect ml_full(void) {
    ML_Rect r = { CL_MAP_X, CL_MAP_Y, CL_MAP_W + CL_SIDEBAR_W, CL_MAP_H };
    return r;
}

// The smallest integer scale at which the backdrop covers the pane's width.
// At 672 wide that is 3 (720): a whole-number scale keeps the pixel art
// square, and the overshoot is cropped evenly off the two sides.
int ml_loc_scale(void) {
    int s = (CL_MAP_W + ML_BACKDROP_W - 1) / ML_BACKDROP_W;
    return s < 1 ? 1 : s;
}

ML_Rect ml_loc_backdrop(void) {
    int h = ML_BACKDROP_H * ml_loc_scale();
    if (h > CL_MAP_H) h = CL_MAP_H;
    ML_Rect r = { CL_MAP_X, CL_MAP_Y, CL_MAP_W, h };
    return r;
}

ML_Rect ml_loc_text(void) {
    ML_Rect b = ml_loc_backdrop();
    ML_Rect r = { CL_MAP_X, b.y + b.h, CL_MAP_W, CL_MAP_Y + CL_MAP_H - (b.y + b.h) };
    return r;
}

int ml_cols(ML_Rect r) {
    int gw = BFONT_GLYPH_W;
    return gw > 0 ? (r.w - 2 * ML_PAD) / gw : 0;
}

int ml_lines(ML_Rect r) {
    int gh = BFONT_GLYPH_H;
    return gh > 0 ? (r.h - 2 * ML_PAD) / gh : 0;
}
