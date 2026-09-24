// src/modern/mlayout.c -- the modern UI's measures (see mlayout.h).

#include "mlayout.h"
#include "layout.h"
#include "bfont.h"
#include "input_host.h"

int ml_row_h(void) {
    // A row and its rule are half a tile -- two rows to a tile, room for one
    // text line centred -- and never less than a text line and its padding.
    // On a touch device, two thirds of a tile: the height a finger needs. The
    // device is fixed for the session, so a row is one height from the first
    // frame to the last.
    int h = CL_TILE_H / 2 - ML_ROW_RULE;
    int min = BFONT_GLYPH_H + ML_PAD;
    if (h < min) h = min;
    if (input_touch_device()) {
        int t = CL_TILE_H * 2 / 3 - ML_ROW_RULE;
        if (h < t) h = t;
    }
    return h;
}
