// src/modern/mlist.c -- the standard modern select list and count stepper
// (see mlist.h).

#include "modern/mlist.h"
#include "gfx.h"
#include "modern/mlayout.h"
#include "input_host.h"
#include "lattice.h"
#include "select.h"
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

int ml_list_draw(int x, int y, int w, int h, int count, int cursor,
                 MlRowFn fn, void *ctx, int touch_list, Color bg) {
    return ml_list_draw_ex(x, y, w, h, count, cursor, fn, ctx, touch_list, bg, 0);
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
        int aw = ML_PAD * 3;
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
        Color fg = !enabled ? PAL_CLR(DGREY) : sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        char *nl = strchr(label, '\n');
        if (nl) {
            // Two lines: the fill and tap region from sel_row, the lines drawn here.
            *nl = '\0';
            sel_row(x, ry, w, rh, x + ML_PAD, "", sel, fg, bg, enabled ? touch_list : 0, touch_base + i);
            int lh2 = bfont_line_height();
            int ty2 = ry + (rh - 2 * lh2) / 2;
            Color tc = sel ? (enabled ? bg : PAL_CLR(GREY)) : fg;
            bfont_draw(label, x + ML_PAD, ty2, tc);
            bfont_draw(nl + 1, x + ML_PAD, ty2 + lh2, tc);
        } else {
            sel_row(x, ry, w, rh, x + ML_PAD, label, sel, fg, bg,
                    enabled ? touch_list : 0, touch_base + i);
        }
        int ty = ry + (rh - bfont_line_height()) / 2;
        if (right[0]) {
            int tw = (int)bfont_measure(right).x;
            bfont_draw(right, x + w - ML_PAD - tw, ty, (sel && enabled) ? bg : sel ? PAL_CLR(GREY) : fg);
        }
        // More rows above or below: a small arrow at the row's right edge. On
        // the lit cursor row it is drawn in the panel's own colour; a greyed
        // cursor row keeps its dark fill, so the arrow stays gold there.
        int ax = x + w - ML_PAD / 2 - 6;
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

int ml_stepper_height(void) { return BFONT_GLYPH_H * 2; }

void ml_stepper_draw(int x, int y, int w, const char *text) {
    int gh = BFONT_GLYPH_H;
    int bw = gh * 2, bh = gh * 2, pad = ML_PAD;
    int xs[4] = { x, x + bw + pad, x + w - 2 * bw - pad, x + w - bw };
    int keys[4] = { KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_UP };
    for (int k = 0; k < 4; k++) {
        int bx = xs[k];
        gfx_rect_lines(bx, y, bw, bh, PAL_CLR(YELLOW));
        int cy = y + bh / 2;
        bool leftward = (k < 2);
        int arrows = (k == 0 || k == 3) ? 2 : 1;
        for (int a = 0; a < arrows; a++) {
            float ax = (float)(bx + bw / 2 + (arrows == 2 ? (a == 0 ? -6 : 6) : 0));
            if (leftward)
                gfx_triangle((Vector2){ ax + 5, (float)cy - 7 }, (Vector2){ ax - 5, (float)cy },
                             (Vector2){ ax + 5, (float)cy + 7 }, PAL_CLR(YELLOW));
            else
                gfx_triangle((Vector2){ ax - 5, (float)cy - 7 }, (Vector2){ ax - 5, (float)cy + 7 },
                             (Vector2){ ax + 5, (float)cy }, PAL_CLR(YELLOW));
        }
        ui_button(bx, y, bw, bh, keys[k]);
    }
    int tw = (int)bfont_measure(text).x;
    int mid = x + w / 2;
    bfont_draw(text, mid - tw / 2, y + (bh - gh) / 2, PAL_CLR(YELLOW));
    ui_button(mid - tw / 2 - pad, y, tw + 2 * pad, bh, KEY_ENTER);
}

void ml_hint_text(char *out, int cap, const char *label, const char *kb_key, const char *pad_key) {
    if (input_has_keyboard() && kb_key && kb_key[0])
        snprintf(out, (size_t)cap, "%s [%s]", label, kb_key);
    else if (!input_touch_active() && input_pad_or_touch_seen() && pad_key && pad_key[0])
        snprintf(out, (size_t)cap, "%s (%s)", label, pad_key);
    else
        snprintf(out, (size_t)cap, "%s", label);
}

int ml_hint_width(const char *label, const char *kb_key, const char *pad_key) {
    char t[96];
    ml_hint_text(t, sizeof t, label, kb_key, pad_key);
    return (int)bfont_measure(t).x + 2 * ML_PAD;
}

int ml_hint_button(int x, int y, const char *label, const char *kb_key, const char *pad_key, int key) {
    char t[96];
    ml_hint_text(t, sizeof t, label, kb_key, pad_key);
    int w = (int)bfont_measure(t).x + 2 * ML_PAD, h = BFONT_GLYPH_H + 8;
    gfx_rect_lines(x, y, w, h, PAL_CLR(YELLOW));
    bfont_draw(t, x + ML_PAD, y + 4, PAL_CLR(YELLOW));
    ui_button(x, y, w, h, key);
    return w;
}

int ml_count_buttons(int x, int y, int w, int value, int max) {
    // One row of touch-sized buttons -- the least, -10, -1, the count, +1,
    // +10, the most -- and under it a bar filled as far as the count goes.
    const int gh = BFONT_GLYPH_H, pad = ML_PAD, bh = ml_row_h();
    const Resources *res = resources_current();
    const char *lo = (res && res->banners.count_min[0]) ? res->banners.count_min : "1";
    const char *hi = (res && res->banners.count_max[0]) ? res->banners.count_max : "";
    const char *labels[6] = { lo, "-10", "-1", "+1", "+10", hi };
    static const int KEYS[6] = { KEY_HOME, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_END };
    int value_w = (int)bfont_measure("00000").x + 4 * pad;
    int bw = (w - value_w - 6 * pad) / 6;
    int cx = x;
    Color edge = (Color){ 200, 160, 60, 255 };
    for (int k = 0; k < 7; k++) {
        if (k == 3) {
            // The count, white on black.
            char nb[16];
            snprintf(nb, sizeof nb, "%d", value);
            gfx_rect(cx, y, value_w, bh, PAL_CLR(BLACK));
            gfx_rect_lines(cx, y, value_w, bh, PAL_CLR(WHITE));
            bfont_draw(nb, cx + (value_w - (int)bfont_measure(nb).x) / 2, y + (bh - gh) / 2, PAL_CLR(WHITE));
            cx += value_w + pad;
            continue;
        }
        int b = k < 3 ? k : k - 1;
        if (!labels[b][0]) { cx += bw + pad; continue; }
        gfx_rect_lines(cx, y, bw, bh, edge);
        gfx_rect_lines(cx + 1, y + 1, bw - 2, bh - 2, edge);
        int lw = (int)bfont_measure(labels[b]).x;
        bfont_draw(labels[b], cx + (bw - lw) / 2, y + (bh - gh) / 2, PAL_CLR(YELLOW));
        ui_button(cx, y, bw, bh, KEYS[b]);
        cx += bw + pad;
    }
    int bar_y = y + bh + pad, bar_h = 8;
    gfx_rect(x, bar_y, w, bar_h, PAL_CLR(BLACK));
    if (max > 0) gfx_rect(x, bar_y, (int)((long)w * value / max), bar_h, edge);
    gfx_rect_lines(x, bar_y, w, bar_h, (Color){ 90, 72, 30, 255 });
    return bh + pad + bar_h;
}

int ml_count_buttons_height(void) { return ml_row_h() + ML_PAD + 8; }

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
