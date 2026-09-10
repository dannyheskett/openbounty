// src/select.h
//
// One selection system for every menu in modern mode: the cursor row is
// drawn inverted (a bar in the row's colour with the text in the panel
// colour), up and down move it, Enter confirms, a tap selects and
// confirms, and the screen's old hotkeys (letters, digits) still answer.
//
// Legacy is untouched: sel_input reports nothing and sel_row draws the
// text exactly as the caller would, so every legacy screen keeps its own
// handling and its own pixels.

#ifndef OB_SELECT_H
#define OB_SELECT_H

#include "raylib.h"
#include <stdbool.h>

typedef struct { int count; int cursor; } SelList;

typedef enum { SEL_NONE = 0, SEL_MOVED, SEL_CONFIRM } SelEvent;

// Reads this frame's input for the list (modern only). Up/Down (also W/S,
// KP8/KP2) move and wrap; Enter, KP Enter and Space confirm the cursor
// row; a hotkey `hotkey_base + row` (0 = no hotkeys) or a tapped row of
// `touch_list` (0 = none) selects AND confirms that row. On SEL_CONFIRM
// *row is the row.
SelEvent sel_input(SelList *l, int touch_list, int hotkey_base, int *row);

// One row. Modern: selected rows are inverted; the row's tap region is
// registered when touch_list is not 0. Legacy: text in `fg`, nothing else.
void sel_row(int x, int y, int w, int h, int text_x, const char *text,
             bool selected, Color fg, Color bg, int touch_list, int row);

// Pure pieces, unit tested.
int sel_wrap(int cursor, int delta, int count);
int sel_hotkey_row(int key, int hotkey_base, int count);   // -1: not a hotkey

#endif
