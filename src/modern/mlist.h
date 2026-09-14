// src/modern/mlist.h
//
// The standard modern select list and count stepper (REQ-430l, REQ-430n).
// Rows are ml_row_h() tall with an ML_ROW_RULE rail under each, stacked from
// the top of their space and never stretched; the cursor row is inverted; a
// list longer than its space scrolls to keep the cursor row in view, with a
// small arrow on the edge rows when rows are hidden above or below. Input stays
// with sel_input (src/select.c): scrolling is only how the rows are drawn.
// Legacy never includes this header.

#ifndef OB_MODERN_MLIST_H
#define OB_MODERN_MLIST_H

#include "raylib.h"
#include <stdbool.h>

// Row i's label (and an optional value drawn at the row's right edge; leave it
// empty for none). Returns false for a row that cannot be chosen (drawn grey,
// not tappable).
typedef bool (*MlRowFn)(void *ctx, int i, char *label, char *right, int cap);

// The first row shown so `cursor` is in view, for `count` rows of which `vis`
// fit. Pure, unit tested.
int  ml_list_first(int count, int cursor, int vis);
// How many rows of the standard height fit in `h` pixels (at least 1).
int  ml_list_fit(int h);
// The height `rows` standard rows take, rails included.
int  ml_list_height(int rows);
// Draw the list into (x, y, w, h) on `bg`. Returns the rows shown.
int  ml_list_draw(int x, int y, int w, int h, int count, int cursor,
                  MlRowFn fn, void *ctx, int touch_list, Color bg);
// The same, for one column of a larger list: rows are numbered from
// `touch_base` for taps (and passed to `fn` as touch_base + i). A cursor
// outside this column is -1.
int  ml_list_draw_ex(int x, int y, int w, int h, int count, int cursor,
                     MlRowFn fn, void *ctx, int touch_list, Color bg, int touch_base);

// The count stepper (`<<  <   N of MAX   >  >>`) across (x, y, w), GH * 2
// tall: each arrow is a tap target for its key (Down -10, Left -1, Right +1,
// Up +10) and the count text is one for Enter. `text` is the count text.
int  ml_stepper_height(void);
void ml_stepper_draw(int x, int y, int w, const char *text);
// Apply one frame of stepper keys to *value within [lo, hi]. Returns true when
// the value changed.
bool ml_stepper_keys(int *value, int lo, int hi);

// Hint buttons: `label` with the key for the device in use -- " [Esc]" once a
// keyboard is in use, " (B)" for a gamepad, nothing on touch -- written to out.
void ml_hint_text(char *out, int cap, const char *label, const char *kb_key, const char *pad_key);
// A framed hint button GH + 8 tall at (x, y); a tap presses `key`. Returns its width.
int  ml_hint_width(const char *label, const char *kb_key, const char *pad_key);
int  ml_hint_button(int x, int y, const char *label, const char *kb_key, const char *pad_key, int key);

#endif
