#include "select.h"
#include "gfx.h"
#include "palette.h"
#include "bfont.h"
#include "input_host.h"
#include "layout.h"
#include "touch.h"
#include "uitouch.h"
#include "modern/mlist.h"
#include <stddef.h>

int sel_wrap(int cursor, int delta, int count) {
    if (count <= 0) return 0;
    int c = (cursor + delta) % count;
    if (c < 0) c += count;
    return c;
}

int sel_hotkey_row(int key, int hotkey_base, int count) {
    if (!hotkey_base || key < hotkey_base) return -1;
    int r = key - hotkey_base;
    return (r < count) ? r : -1;
}

SelEvent sel_input(SelList *l, int touch_list, int hotkey_base, int *row) {
    if (!CL_IS_MODERN || !l || l->count <= 0) return SEL_NONE;
    if (hotkey_base) {
        for (int r = 0; r < l->count; r++) {
            if (input_key_pressed(hotkey_base + r)) { l->cursor = r; if (row) *row = r; return SEL_CONFIRM; }
        }
    }
    MlList m = { l->count, l->cursor, NULL, NULL };
    MlEvent ev = ml_list_input(&m, touch_list, row);
    l->cursor = m.cursor;
    return ev == ML_EV_ACT ? SEL_CONFIRM : ev == ML_EV_MOVED ? SEL_MOVED : SEL_NONE;
}

void sel_row(int x, int y, int w, int h, int text_x, const char *text,
             bool selected, Color fg, Color bg, int touch_list, int row) {
    (void)selected; (void)bg;
    bfont_draw(text, text_x, y, fg);
    if (touch_list) ui_tile_row(x, y, w, h, touch_list, row);
}
