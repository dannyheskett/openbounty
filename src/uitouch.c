// src/uitouch.c -- the widgets (see uitouch.h).

#include "uitouch.h"
#include "layout.h"
#include "touch.h"

// A target the player aims at on its own is worth a touch unit, whatever it
// is drawn at. A target that sits in a row of its own kind is not: inflating
// one inflates its neighbours, and the first registered would swallow them.
static void inflate_isolated(int *x, int *y, int *w, int *h) {
    if (!CL_IS_MODERN) return;              // legacy geometry is a spec
    int unit = touch_unit_design();
    if (*w < unit) { *x -= (unit - *w) / 2; *w = unit; }
    if (*h < unit) { *y -= (unit - *h) / 2; *h = unit; }
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
}

void ui_button(int x, int y, int w, int h, int key) {
    inflate_isolated(&x, &y, &w, &h);
    touch_region(x, y, w, h, key);
}

void ui_bar(int x, int y, int w, int h, int key) {
    inflate_isolated(&x, &y, &w, &h);
    touch_region_priority(x, y, w, h, key);
}

void ui_tile(int x, int y, int w, int h, int key) {
    touch_region(x, y, w, h, key);
}

void ui_tile_row(int x, int y, int w, int h, int list_id, int row) {
    touch_region_row(x, y, w, h, list_id, row);
}

void ui_map(int x, int y, int w, int h, int cell_x, int cell_y,
            int tile_w, int tile_h, int center_key) {
    touch_region_map(x, y, w, h, cell_x, cell_y, tile_w, tile_h, center_key);
}

void ui_grid(int x, int y, int w, int h, int tile_w, int tile_h, int grid_id) {
    touch_region_grid(x, y, w, h, tile_w, tile_h, grid_id);
}

void ui_scroll(int x, int y, int w, int h, int step) {
    touch_region_scroll(x, y, w, h, step);
}

void ui_dismiss_on_tap(int key) {
    touch_region_any(key);
}
