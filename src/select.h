// src/select.h
//
// The legacy screens' row and the modern lists' old entry point.
//
// Modern: every list reads its keys and taps through the one reader,
// ml_list_input (src/modern/mlist.h); sel_input is that reader for a list
// with no greyed rows and no row keys. Legacy is untouched: sel_input reports
// nothing and sel_row draws the text exactly as the caller would, so every
// legacy screen keeps its own handling and its own pixels.

#ifndef OB_SELECT_H
#define OB_SELECT_H

#include "ob_types.h"
#include <stdbool.h>

typedef struct { int count; int cursor; } SelList;

typedef enum { SEL_NONE = 0, SEL_MOVED, SEL_CONFIRM } SelEvent;

// Reads this frame's input for the list (modern only), through ml_list_input:
// Up/Down (the keypad's 8 and 2 too) move and wrap; Enter, the keypad's Enter
// and Space confirm the cursor row; a tapped row of `touch_list` (0 = none)
// selects AND confirms it. `hotkey_base` (0 = none): the key `hotkey_base +
// row` does the same for its row. On SEL_CONFIRM *row is the row.
SelEvent sel_input(SelList *l, int touch_list, int hotkey_base, int *row);

// Legacy's row: the text in `fg`, and its tap region when touch_list is not 0.
void sel_row(int x, int y, int w, int h, int text_x, const char *text,
             bool selected, Color fg, Color bg, int touch_list, int row);

// Pure pieces, unit tested.
int sel_wrap(int cursor, int delta, int count);
int sel_hotkey_row(int key, int hotkey_base, int count);   // -1: not a hotkey

#endif
