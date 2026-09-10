// src/textsel.h
//
// The in-game letter selector: text entry without a keyboard. A grid of
// cells the player moves through with arrows, the keypad or a gamepad
// d-pad, picks with Enter or the A button, or taps. Drawn in the game
// buffer, not the window chrome, so it lays out with the panel it serves.
//
// Alpha grid, 6 columns by 5 rows (29 cells):
//   A B C D E F / G H I J K L / M N O P Q R / S T U V W X / Y Z SPC DEL OK
// Numeric grid, 4 by 3 (12 cells):
//   7 8 9 DEL / 4 5 6 OK / 1 2 3 0
//
// Modern only: the legacy pack keeps typed entry and its window keyboard.

#ifndef OB_TEXTSEL_H
#define OB_TEXTSEL_H

#include "raylib.h"
#include <stdbool.h>

typedef struct { int cursor; bool numeric; } TextSel;

#define TEXTSEL_DEL  '\b'
#define TEXTSEL_OK   '\n'

// The character a cell stands for: 'A'..'Z', '0'..'9', ' ', TEXTSEL_DEL,
// TEXTSEL_OK. Pure.
int  textsel_char(int cursor, bool numeric);
int  textsel_cols(bool numeric);
int  textsel_count(bool numeric);

// Cursor after a step of (dx, dy), wrapping on each axis; a step onto a
// missing cell in the last row clamps to the last cell. Pure.
int  textsel_move(int cursor, int dx, int dy, bool numeric);

// Reads this frame's input: arrows / WASD / keypad / gamepad d-pad move,
// Enter / KP Enter / gamepad A pick the cursor cell, Backspace / gamepad B
// delete, a tapped cell (touch_list) picks it. A pick appends to buf
// (cap includes the terminator), DEL removes the last character. Returns
// true when OK was picked. `allow` filters what may be appended (NULL:
// anything); it sees the candidate character.
bool textsel_input(TextSel *t, char *buf, int *len, int cap, int touch_list,
                   bool (*allow)(const char *buf, int len, int ch));

// Draws the grid with the cursor cell inverted and registers tap regions.
void textsel_draw(const TextSel *t, int x, int y, int cell_w, int cell_h,
                  Color fg, Color bg, int touch_list);

// The grid's size in pixels for a cell size.
int  textsel_w(bool numeric, int cell_w);
int  textsel_h(bool numeric, int cell_h);

#endif
