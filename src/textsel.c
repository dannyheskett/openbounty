#include "textsel.h"
#include "gfx.h"
#include "bfont.h"
#include "input.h"
#include "input_host.h"
#include "layout.h"
#include "touch.h"
#include "uitouch.h"
#include <string.h>

static const char ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ \b\n";    // 29
static const char NUM[]   = "789\b456\n1230";                     // 12

// Six columns of text-sized cells, or ten of touch-sized ones: at a touch
// unit the letters are 66 px square, and six columns would stack five rows
// (330 px) into a panel that has to hold a name and a difficulty table as
// well. Ten columns is three rows, 660 x 198, inside the 800 x 532 buffer.
int textsel_cols(bool numeric) {
    if (numeric) return 4;
    return (CL_IS_MODERN && input_touch_active()) ? 10 : 6;
}
int textsel_count(bool numeric) { return numeric ? 12 : 29; }

int textsel_char(int cursor, bool numeric) {
    int n = textsel_count(numeric);
    if (cursor < 0 || cursor >= n) return 0;
    return numeric ? NUM[cursor] : ALPHA[cursor];
}

int textsel_move(int cursor, int dx, int dy, bool numeric) {
    int cols = textsel_cols(numeric), n = textsel_count(numeric);
    int rows = (n + cols - 1) / cols;
    if (cursor < 0 || cursor >= n) cursor = 0;
    int c = cursor % cols, r = cursor / cols;
    if (dx) {
        int in_row = (r == rows - 1) ? n - r * cols : cols;
        c = (c + dx) % in_row;
        if (c < 0) c += in_row;
    }
    if (dy) {
        r = (r + dy) % rows;
        if (r < 0) r += rows;
    }
    int idx = r * cols + c;
    if (idx >= n) idx = n - 1;
    return idx;
}

int textsel_w(bool numeric, int cell_w) { return textsel_cols(numeric) * cell_w; }
int textsel_h(bool numeric, int cell_h) {
    int cols = textsel_cols(numeric), n = textsel_count(numeric);
    return ((n + cols - 1) / cols) * cell_h;
}

static void apply(int ch, char *buf, int *len, int cap,
                  bool (*allow)(const char *, int, int)) {
    if (ch == TEXTSEL_DEL) {
        if (*len > 0) { (*len)--; buf[*len] = '\0'; }
        return;
    }
    if (*len + 1 >= cap) return;
    if (allow && !allow(buf, *len, ch)) return;
    buf[(*len)++] = (char)ch;
    buf[*len] = '\0';
}

bool textsel_input(TextSel *t, char *buf, int *len, int cap, int touch_list,
                   bool (*allow)(const char *buf, int len, int ch)) {
    if (!CL_IS_MODERN || !t || !buf || !len) return false;
    int n = textsel_count(t->numeric);
    if (t->cursor < 0 || t->cursor >= n) t->cursor = 0;

    if (touch_list) {
        int tapped = touch_tapped_row(touch_list);
        if (tapped >= 0 && tapped < n) {
            t->cursor = tapped;
            int ch = textsel_char(tapped, t->numeric);
            if (ch == TEXTSEL_OK) return true;
            apply(ch, buf, len, cap, allow);
            return false;
        }
    }
    int dx = 0, dy = 0;
    if (input_key_pressed(KEY_LEFT)  || input_key_pressed(KEY_KP_4)) dx = -1;
    if (input_key_pressed(KEY_RIGHT) || input_key_pressed(KEY_KP_6)) dx =  1;
    if (input_key_pressed(KEY_UP)    || input_key_pressed(KEY_KP_8)) dy = -1;
    if (input_key_pressed(KEY_DOWN)  || input_key_pressed(KEY_KP_2)) dy =  1;
    int gx = 0, gy = 0;
    if (input_gamepad_dir(&gx, &gy)) { dx = gx; dy = gy; }
    if (dx || dy) t->cursor = textsel_move(t->cursor, dx, dy, t->numeric);

    if (input_key_pressed(KEY_BACKSPACE) || gamepad_pressed_cancel()) {
        apply(TEXTSEL_DEL, buf, len, cap, allow);
        return false;
    }
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) || input_gamepad_confirm()) {
        int ch = textsel_char(t->cursor, t->numeric);
        if (ch == TEXTSEL_OK) return true;
        apply(ch, buf, len, cap, allow);
    }
    return false;
}

// The widest label the grid holds, in glyphs: "DEL" and "SPC" are three, and
// a cell sized for one letter drew them over their neighbours -- which is
// what the last row's "ZSPDELOK" mush was.
int textsel_min_cell_w(void) { return 4 * bfont_glyph_w(); }

// A cell is tiled, so it is never inflated after the fact (a neighbour would
// swallow it): the grid is drawn at the size a finger needs instead.
int textsel_cell_w(void) {
    int w = textsel_min_cell_w();
    if (CL_IS_MODERN && input_touch_active()) {
        int u = touch_unit_design();
        if (w < u) w = u;
    }
    return w;
}

int textsel_cell_h(void) {
    int h = bfont_glyph_h() + 6 * CL_UI;
    if (CL_IS_MODERN && input_touch_active()) {
        int u = touch_unit_design();
        if (h < u) h = u;
    }
    return h;
}

void textsel_draw(const TextSel *t, int x, int y, int cell_w, int cell_h,
                  Color fg, Color bg, int touch_list) {
    if (!CL_IS_MODERN || !t) return;
    int cols = textsel_cols(t->numeric), n = textsel_count(t->numeric);
    if (cell_w < textsel_min_cell_w()) cell_w = textsel_min_cell_w();
    for (int i = 0; i < n; i++) {
        int cx = x + (i % cols) * cell_w, cy = y + (i / cols) * cell_h;
        int ch = textsel_char(i, t->numeric);
        const char *label;
        char one[2] = { (char)ch, 0 };
        if (ch == TEXTSEL_DEL) label = "DEL";
        else if (ch == TEXTSEL_OK) label = "OK";
        else if (ch == ' ') label = "SPC";
        else label = one;
        bool sel = (i == t->cursor);
        if (sel) gfx_rect(cx, cy, cell_w, cell_h, fg);
        int tw = bfont_text_width(label);
        bfont_draw(label, cx + (cell_w - tw) / 2, cy + (cell_h - bfont_line_height()) / 2, sel ? bg : fg);
        if (touch_list) ui_tile_row(cx, cy, cell_w, cell_h, touch_list, i);
    }
}
