// src/modern/mlist.c -- the standard modern list, its reader and the count
// (see mlist.h).

#include "modern/mlist.h"
#include "modern/uikit.h"
#include "gfx.h"
#include "modern/mlayout.h"
#include "input_host.h"
#include "lattice.h"
#include "touch.h"
#include "uitouch.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

int ml_list_first(int count, int cursor, int vis) {
    if (vis < 1 || count <= vis) return 0;
    int first = cursor - vis + 1;
    if (first < 0) first = 0;
    if (first > count - vis) first = count - vis;
    return first;
}

int ml_list_fit(int h) {
    int n = (h + ML_ROW_RULE) / (ml_row_h() + ML_ROW_RULE);
    return n < 1 ? 1 : n;
}

int ml_list_height(int rows) {
    return rows * (ml_row_h() + ML_ROW_RULE);
}

bool ml_keys_shown(void) { return input_last_device() == INPUT_DEV_KEYS; }

void ml_exit_hint(char *right) {
    const Resources *res = resources_current();
    right[0] = '\0';
    if (ml_keys_shown()) snprintf(right, 48, "%s", (res && res->ui.key_esc[0]) ? res->ui.key_esc : "Esc");
}

int ml_list_draw(int x, int y, int w, int h, int count, int cursor,
                 MlRowFn fn, void *ctx, int touch_list, Color bg) {
    return ml_list_draw_ex(x, y, w, h, count, cursor, fn, ctx, touch_list, bg, 0);
}

// One row: lit when it is the cursor's (a row that cannot be chosen keeps its
// dark fill under a gold outline), its label at the inset -- two lines when
// it carries a newline -- and its value at the right, each cut with "..".
static void draw_row(int x, int y, int w, int h, const char *label, const char *right,
                     bool sel, bool enabled, Color bg) {
    Color fg = !enabled ? PAL_CLR(DGREY) : sel ? bg : PAL_CLR(WHITE);
    if (sel && enabled) {
        gfx_rect(x, y, w, h, PAL_CLR(YELLOW));
    } else if (sel) {
        gfx_rect(x, y, w, h, bg);
        gfx_rect_lines(x + 1, y + 1, w - 2, h - 2, PAL_CLR(YELLOW));
    }
    int rw = right[0] ? bfont_text_width(right) : 0;
    int lw = w - 2 * UK_INSET - (rw ? rw + UK_INSET : 0);
    const int gh = BFONT_GLYPH_H, lh = uk_line_h();
    const char *nl = strchr(label, '\n');
    if (nl) {
        char first[96];
        snprintf(first, sizeof first, "%.*s", (int)(nl - label), label);
        int ty = y + (h - lh - gh) / 2;
        uk_line(first, x + UK_INSET, ty, lw, fg);
        uk_line(nl + 1, x + UK_INSET, ty + lh, lw, fg);
    } else {
        uk_line(label, x + UK_INSET, y + (h - gh) / 2, lw, fg);
    }
    if (rw) bfont_draw(right, x + w - UK_INSET - rw, y + (h - gh) / 2, fg);
}

int ml_list_draw_ex(int x, int y, int w, int h, int count, int cursor,
                    MlRowFn fn, void *ctx, int touch_list, Color bg, int touch_base) {
    int rh = ml_row_h(), pitch = rh + ML_ROW_RULE;
    int vis = ml_list_fit(h);
    int first = ml_list_first(count, cursor < 0 ? 0 : cursor, vis);
    int shown = 0;
    // More rows above or below: the arrow at the row's right edge is a tap
    // target that moves the cursor (so the list scrolls) -- registered before
    // the rows, since the first region hit wins.
    if (touch_list && count > vis)
        ui_scroll(x, y, w, vis * pitch, pitch);              // drag to scroll
    if (touch_list) {
        int aw = 3 * UK_INSET;
        if (first > 0)
            ui_button(x + w - aw, y, aw, rh, KEY_UP);
        if (first + vis < count)
            ui_button(x + w - aw, y + (vis - 1) * pitch, aw, rh, KEY_DOWN);
    }
    for (int i = first; i < count && shown < vis; i++, shown++) {
        char label[96] = "", right[48] = "";
        bool enabled = fn ? fn(ctx, touch_base + i, label, right, (int)sizeof label) : true;
        int ry = y + shown * pitch;
        bool sel = (i == cursor);
        draw_row(x, ry, w, rh, label, right, sel, enabled, bg);
        // Every row takes its tap -- a greyed one too, which does nothing --
        // so a finger never lands on the row beside the one it aimed at.
        if (touch_list) ui_tile_row(x, ry, w, rh, touch_list, touch_base + i);
        // More rows above or below: a small arrow at the row's right edge, in
        // the panel's own colour on a lit row, gold otherwise.
        int ax = x + w - UK_INSET / 2 - 6;
        Color arrow = (sel && enabled) ? bg : PAL_CLR(YELLOW);
        if (shown == 0 && first > 0)
            gfx_triangle((Vector2){ (float)ax, (float)ry + 4 }, (Vector2){ (float)ax - 5, (float)ry + 12 },
                         (Vector2){ (float)ax + 5, (float)ry + 12 }, arrow);
        if (shown == vis - 1 && i + 1 < count)
            gfx_triangle((Vector2){ (float)ax - 5, (float)(ry + rh - 12) }, (Vector2){ (float)ax, (float)(ry + rh - 4) },
                         (Vector2){ (float)ax + 5, (float)(ry + rh - 12) }, arrow);
        lattice_band_h(x, ry + rh, w, ML_ROW_RULE);
    }
    return shown;
}

int ml_rows_top_h(ML_Rect a, int foot) {
    return foot > 0 ? a.h - ml_list_height(foot) - UK_BAND : a.h;
}

void ml_rows_draw(ML_Rect a, int n, int foot, int cursor, MlRowFn fn, void *ctx, int touch_list) {
    if (foot < 0 || foot > n) foot = 0;
    if (!foot) {
        ml_list_draw(a.x, a.y, a.w, a.h, n, cursor, fn, ctx, touch_list, uk_ink());
        return;
    }
    int top_n = n - foot;
    int foot_h = ml_list_height(foot);
    int fy = a.y + a.h - foot_h;
    // The rows above the foot scroll in what is left, less a gap.
    if (top_n > 0) {
        int top_h = ml_rows_top_h(a, foot);
        if (top_h < ml_list_height(1)) top_h = ml_list_height(1);
        ml_list_draw_ex(a.x, a.y, a.w, top_h, top_n, cursor < top_n ? cursor : -1,
                        fn, ctx, touch_list, uk_ink(), 0);
    }
    lattice_band_h(a.x, fy - UK_BAND, a.w, UK_BAND);
    ml_list_draw_ex(a.x, fy, a.w, foot_h, foot, cursor >= top_n ? cursor - top_n : -1,
                    fn, ctx, touch_list, uk_ink(), top_n);
}

// ---- the one reader --------------------------------------------------------------

int ml_key_code(const char *name) {
    if (!name || !name[0] || name[1]) return 0;
    char c = name[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
    if (c >= '0' && c <= '9') return KEY_ZERO + (c - '0');
    return 0;
}

MlEvent ml_list_input(MlList *l, int touch_list, int *row) {
    if (row) *row = -1;
    if (!l || l->n <= 0) return input_key_pressed(KEY_ESCAPE) ? ML_EV_BACK : ML_EV_NONE;
    if (l->cursor < 0 || l->cursor >= l->n) l->cursor = 0;
    #define ENABLED(i) (!l->enabled || l->enabled[i])
    if (touch_list) {
        int t = touch_tapped_row(touch_list);
        if (t >= 0 && t < l->n) {
            l->cursor = t;
            if (!ENABLED(t)) return ML_EV_MOVED;
            if (row) *row = t;
            return ML_EV_ACT;
        }
    }
    if (input_key_pressed(KEY_ESCAPE)) return ML_EV_BACK;
    for (int i = 0; l->keys && i < l->n; i++) {
        int k = ml_key_code(l->keys[i]);
        bool digit = k >= KEY_ZERO && k <= KEY_ZERO + 9;
        if (!k || !(input_key_pressed(k) || (digit && input_key_pressed(KEY_KP_0 + (k - KEY_ZERO)))))
            continue;
        l->cursor = i;
        if (!ENABLED(i)) return ML_EV_MOVED;
        if (row) *row = i;
        return ML_EV_ACT;
    }
    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8)) {
        l->cursor = (l->cursor - 1 + l->n) % l->n;
        return ML_EV_MOVED;
    }
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2)) {
        l->cursor = (l->cursor + 1) % l->n;
        return ML_EV_MOVED;
    }
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) || input_key_pressed(KEY_SPACE)) {
        if (!ENABLED(l->cursor)) return ML_EV_NONE;
        if (row) *row = l->cursor;
        return ML_EV_ACT;
    }
    #undef ENABLED
    return ML_EV_NONE;
}

// ---- keys on screen --------------------------------------------------------------

void ml_hint_text(char *out, int cap, const char *label, const char *kb_key, const char *pad_key) {
    InputDevice d = input_last_device();
    if (d == INPUT_DEV_KEYS && kb_key && kb_key[0])
        snprintf(out, (size_t)cap, "%s [%s]", label, kb_key);
    else if (d == INPUT_DEV_PAD && pad_key && pad_key[0])
        snprintf(out, (size_t)cap, "%s (%s)", label, pad_key);
    else
        snprintf(out, (size_t)cap, "%s", label);
}

// ---- How many --------------------------------------------------------------------

// The count, white on black in its box.
static void count_value(int x, int y, int w, int h, int value) {
    char nb[16];
    snprintf(nb, sizeof nb, "%d", value);
    gfx_rect(x, y, w, h, PAL_CLR(BLACK));
    gfx_rect_lines(x, y, w, h, PAL_CLR(WHITE));
    bfont_draw(nb, x + (w - bfont_text_width(nb)) / 2, y + (h - BFONT_GLYPH_H) / 2, PAL_CLR(WHITE));
}

int ml_count_buttons(int x, int y, int w, int value, int max) {
    // Row-tall buttons -- the least, -10, -1, +1, +10, the most -- round the
    // count, and under them a bar filled as far as the count goes. Every
    // label clears its button's double line and the count's box holds five
    // digits; where one row cannot hold them all, the count takes a row of
    // its own above the buttons.
    const int gh = BFONT_GLYPH_H, pad = ML_PAD, bh = ml_row_h();
    const Resources *res = resources_current();
    const char *lo = (res && res->banners.count_min[0]) ? res->banners.count_min : "1";
    const char *hi = (res && res->banners.count_max[0]) ? res->banners.count_max : "";
    const char *labels[6] = { lo, "-10", "-1", "+1", "+10", hi };
    static const int KEYS[6] = { KEY_HOME, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_END };
    int lw = 0;
    for (int b = 0; b < 6; b++) {
        int t = bfont_text_width(labels[b]);
        if (t > lw) lw = t;
    }
    int value_w = bfont_text_width("00000") + 4 * pad;
    int bw = (w - value_w - 6 * pad) / 6;
    bool own_row = bw < lw + 2 * pad;
    int by = y;                                   // the buttons' row
    if (own_row) {
        count_value(x, y, w, bh, value);
        by = y + bh + pad;
        bw = (w - 5 * pad) / 6;
    }
    // The row ends flush with the bar: the count's box, or the last button,
    // takes what the division leaves.
    value_w = w - 6 * bw - 6 * pad;
    int last_w = own_row ? w - 5 * (bw + pad) : bw;
    int cx = x;
    Color edge = uk_button();
    for (int k = 0; k < 7; k++) {
        if (k == 3) {
            if (!own_row) {
                count_value(cx, y, value_w, bh, value);
                cx += value_w + pad;
            }
            continue;
        }
        int b = k < 3 ? k : k - 1;
        int tw = b == 5 ? last_w : bw;
        if (!labels[b][0]) { cx += tw + pad; continue; }
        gfx_rect_lines(cx, by, tw, bh, edge);
        gfx_rect_lines(cx + 1, by + 1, tw - 2, bh - 2, edge);
        uk_line(labels[b], cx + (tw - bfont_text_width(labels[b])) / 2, by + (bh - gh) / 2, tw, PAL_CLR(YELLOW));
        ui_tile(cx, by, tw, bh, KEYS[b]);
        cx += tw + pad;
    }
    int bar_y = by + bh + pad, bar_h = 8;
    gfx_rect(x, bar_y, w, bar_h, PAL_CLR(BLACK));
    if (max > 0) gfx_rect(x, bar_y, (int)((long)w * value / max), bar_h, edge);
    gfx_rect_lines(x, bar_y, w, bar_h, uk_bar_edge());
    return bar_y + bar_h - y;
}

bool ml_stepper_keys(int *value, int lo, int hi) {
    int v = *value;
    if (input_key_pressed(KEY_LEFT))  v -= 1;
    if (input_key_pressed(KEY_RIGHT)) v += 1;
    if (input_key_pressed(KEY_DOWN))  v -= 10;
    if (input_key_pressed(KEY_UP))    v += 10;
    if (input_key_pressed(KEY_HOME))  v = lo;
    if (input_key_pressed(KEY_END))   v = hi;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    bool changed = (v != *value);
    *value = v;
    return changed;
}
