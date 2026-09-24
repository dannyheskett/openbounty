// src/modern/mlist.h
//
// The standard modern list and its one reader (REQ-430l, REQ-430n). Rows are
// ml_row_h() tall with an ML_ROW_RULE rail under each, stacked from the top of
// their space and never stretched; the cursor row is lit; a list longer than
// its space scrolls to keep the cursor row in view, with a small arrow on the
// edge rows when rows are hidden above or below. Every modern list reads its
// keys and taps through ml_list_input. Legacy never includes this header.

#ifndef OB_MODERN_MLIST_H
#define OB_MODERN_MLIST_H

#include "ob_types.h"
#include "modern/mlayout.h"
#include <stdbool.h>

// Row i's label (and an optional value drawn at the row's right edge; leave it
// empty for none). Returns false for a row that cannot be chosen: drawn dark
// grey, it still takes a tap -- which does nothing -- so a finger that misses
// it never lands on its neighbour.
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
// The same, for one part of a larger list: rows are numbered from
// `touch_base` for taps (and passed to `fn` as touch_base + i). A cursor
// outside this part is -1.
int  ml_list_draw_ex(int x, int y, int w, int h, int count, int cursor,
                     MlRowFn fn, void *ctx, int touch_list, Color bg, int touch_base);
// `n` rows in `a`: the first n - foot from its top (scrolling when they do
// not fit), the last `foot` on its foot with a gap above them. The one way a
// column of rows with its exit, or its answers, on the foot is drawn.
void ml_rows_draw(ML_Rect a, int n, int foot, int cursor, MlRowFn fn, void *ctx, int touch_list);
// The height the rows above the foot scroll in.
int  ml_rows_top_h(ML_Rect a, int foot);

// ---- the one reader --------------------------------------------------------------

typedef enum { ML_EV_NONE = 0, ML_EV_MOVED, ML_EV_ACT, ML_EV_BACK } MlEvent;
typedef struct {
    int                n;
    int                cursor;
    const bool        *enabled;   // per row (NULL: every row)
    const char *const *keys;      // the key each row answers to ("A", "5"; NULL or "": none)
} MlList;
// One frame of a list's keys and taps. Up and Down (the keypad's 8 and 2
// too) move the cursor, wrapping; Enter, the keypad's Enter or Space act on
// the cursor's row; a tap on a row puts the cursor there and acts on it; a
// row's own key acts on it; Escape is Back. A row that cannot be chosen takes
// the cursor but does not act. *row: the row acted on.
MlEvent ml_list_input(MlList *l, int touch_list, int *row);
// The raylib key a row's key name stands for ("A" -> KEY_A, "5" -> KEY_FIVE).
int  ml_key_code(const char *name);

// ---- keys on screen --------------------------------------------------------------

// Key names are shown while the keyboard is the last thing used.
bool ml_keys_shown(void);
// The row Escape presses shows it: `right` gets the key's name while keys
// are shown, and stays empty otherwise.
void ml_exit_hint(char *right);
// `label` with the key for the device in use -- " [Esc]" for the keyboard,
// " (B)" for a gamepad, nothing on touch -- written to out.
void ml_hint_text(char *out, int cap, const char *label, const char *kb_key, const char *pad_key);

// ---- How many --------------------------------------------------------------------

// The count's buttons -- the least, -10, -1, the count, +1, +10, the most --
// across (x, y, w), each a tap for its key (Home, Down, Left, Right, Up, End),
// and under them a bar filled as far as the count goes; where one row cannot
// hold them with their labels clear, the count takes a row of its own above
// the buttons. Returns its height.
int  ml_count_buttons(int x, int y, int w, int value, int max);
// Apply one frame of the count's keys to *value within [lo, hi]. Returns true
// when the value changed.
bool ml_stepper_keys(int *value, int lo, int hi);

#endif
