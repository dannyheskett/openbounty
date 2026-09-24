// src/uitouch.h
//
// The one place the shell hands a rect to the touch layer.
//
// Every screen used to call touch_region* itself: 25 sites in 12 files, each
// deciding its own size, its own order and its own dismissal rule. The
// widgets here own those decisions instead, so a screen says what a thing IS
// and never how a finger finds it.
//
// Three modes, every widget, no exceptions:
//
//   legacy          -- registers exactly the rect it draws, never inflated;
//                      the DOS layout is a spec and its pitch is part of it
//   modern + touch  -- an ISOLATED target is inflated to a touch unit; tiled
//                      targets (list rows, grid cells) are not, because
//                      inflating neighbours makes them steal each other
//   modern + keys   -- unchanged: every widget injects the key the screen
//                      already reads, so the keyboard path is the same path
//
// Rows, cells and the letter grid keep registering through sel_row(),
// ml_list_draw() and textsel_draw(): they are already draw-and-register, and
// they are tiled, so they are correct as they stand.

#ifndef OB_UITOUCH_H
#define OB_UITOUCH_H

#include <stdbool.h>

// An isolated button: an arrow, a corner, a single row of its own. Injects
// `key`. Inflated to a touch unit in modern; drawn rect in legacy.
void ui_button(int x, int y, int w, int h, int key);

// A screen's top band. The bar is the button: a tap anywhere on it injects
// `key` (Escape on a page, the menu key on the map). Inflated like a button,
// and marked so a near miss beats what lies under it.
void ui_bar(int x, int y, int w, int h, int key);

// A TILED target: one of a run of neighbours (a list row, a letter cell, an
// icon in a column, a legacy menu row). Registered exactly as drawn, never
// inflated -- inflating one inflates its neighbours and the first registered
// would swallow the rest. Size these where they are DRAWN instead.
void ui_tile(int x, int y, int w, int h, int key);
void ui_tile_row(int x, int y, int w, int h, int list_id, int row);

// The map viewport and the combat grid: a tap steps from the cell at
// (cell_x, cell_y) toward it (touch_region_map).
void ui_map(int x, int y, int w, int h, int cell_x, int cell_y,
            int tile_w, int tile_h, int center_key);
void ui_grid(int x, int y, int w, int h, int tile_w, int tile_h, int grid_id);

// A list taller than its space: a vertical drag scrolls it.
void ui_scroll(int x, int y, int w, int h, int step);

// This screen has nothing to choose: a tap anywhere dismisses it.
void ui_dismiss_on_tap(int key);

#endif
