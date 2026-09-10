#include "select.h"
#include "bfont.h"
#include "input_host.h"
#include "layout.h"
#include "touch.h"

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
    if (l->cursor < 0 || l->cursor >= l->count) l->cursor = 0;
    if (touch_list) {
        int t = touch_tapped_row(touch_list);
        if (t >= 0 && t < l->count) { l->cursor = t; if (row) *row = t; return SEL_CONFIRM; }
    }
    if (hotkey_base) {
        for (int r = 0; r < l->count; r++) {
            if (input_key_pressed(hotkey_base + r)) { l->cursor = r; if (row) *row = r; return SEL_CONFIRM; }
        }
    }
    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_W) || input_key_pressed(KEY_KP_8)) {
        l->cursor = sel_wrap(l->cursor, -1, l->count);
        return SEL_MOVED;
    }
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_S) || input_key_pressed(KEY_KP_2)) {
        l->cursor = sel_wrap(l->cursor, 1, l->count);
        return SEL_MOVED;
    }
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) || input_key_pressed(KEY_SPACE)) {
        if (row) *row = l->cursor;
        return SEL_CONFIRM;
    }
    return SEL_NONE;
}

void sel_row(int x, int y, int w, int h, int text_x, const char *text,
             bool selected, Color fg, Color bg, int touch_list, int row) {
    if (CL_IS_MODERN && selected) {
        DrawRectangle(x, y, w, h, fg);
        bfont_draw(text, text_x, y + (h - bfont_line_height()) / 2, bg);
    } else {
        bfont_draw(text, text_x, CL_IS_MODERN ? y + (h - bfont_line_height()) / 2 : y, fg);
    }
    if (touch_list) touch_region_row(x, y, w, h, touch_list, row);
}
