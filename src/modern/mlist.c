// src/modern/mlist.c -- the standard modern select list and count stepper
// (see mlist.h).

#include "modern/mlist.h"
#include "modern/mlayout.h"
#include "input_host.h"
#include "lattice.h"
#include "select.h"
#include "touch.h"
#include "palette.h"
#include "bfont.h"
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
        touch_region_scroll(x, y, w, vis * pitch, pitch);    // drag to scroll
    if (touch_list) {
        int aw = ML_PAD * 3;
        if (first > 0)
            touch_region(x + w - aw, y, aw, rh, KEY_UP);
        if (first + vis < count)
            touch_region(x + w - aw, y + (vis - 1) * pitch, aw, rh, KEY_DOWN);
    }
    for (int i = first; i < count && shown < vis; i++, shown++) {
        char label[96] = "", right[48] = "";
        bool enabled = fn ? fn(ctx, touch_base + i, label, right, (int)sizeof label) : true;
        int ry = y + shown * pitch;
        bool sel = (i == cursor);
        Color fg = !enabled ? PAL_CLR(DGREY) : sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        sel_row(x, ry, w, rh, x + ML_PAD, label, sel, fg, bg,
                enabled ? touch_list : 0, touch_base + i);
        int ty = ry + (rh - bfont_line_height()) / 2;
        if (right[0]) {
            int tw = (int)bfont_measure(right).x;
            bfont_draw(right, x + w - ML_PAD - tw, ty, sel ? bg : fg);
        }
        // More rows above or below: a small arrow at the row's right edge.
        int ax = x + w - ML_PAD / 2 - 6;
        if (shown == 0 && first > 0)
            DrawTriangle((Vector2){ (float)ax, (float)ry + 4 }, (Vector2){ (float)ax - 5, (float)ry + 12 },
                         (Vector2){ (float)ax + 5, (float)ry + 12 }, sel ? bg : PAL_CLR(YELLOW));
        if (shown == vis - 1 && i + 1 < count)
            DrawTriangle((Vector2){ (float)ax - 5, (float)(ry + rh - 12) }, (Vector2){ (float)ax, (float)(ry + rh - 4) },
                         (Vector2){ (float)ax + 5, (float)(ry + rh - 12) }, sel ? bg : PAL_CLR(YELLOW));
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
        DrawRectangleLines(bx, y, bw, bh, PAL_CLR(YELLOW));
        int cy = y + bh / 2;
        bool leftward = (k < 2);
        int arrows = (k == 0 || k == 3) ? 2 : 1;
        for (int a = 0; a < arrows; a++) {
            float ax = (float)(bx + bw / 2 + (arrows == 2 ? (a == 0 ? -6 : 6) : 0));
            if (leftward)
                DrawTriangle((Vector2){ ax + 5, (float)cy - 7 }, (Vector2){ ax - 5, (float)cy },
                             (Vector2){ ax + 5, (float)cy + 7 }, PAL_CLR(YELLOW));
            else
                DrawTriangle((Vector2){ ax - 5, (float)cy - 7 }, (Vector2){ ax - 5, (float)cy + 7 },
                             (Vector2){ ax + 5, (float)cy }, PAL_CLR(YELLOW));
        }
        touch_region(bx, y, bw, bh, keys[k]);
    }
    int tw = (int)bfont_measure(text).x;
    int mid = x + w / 2;
    bfont_draw(text, mid - tw / 2, y + (bh - gh) / 2, PAL_CLR(YELLOW));
    touch_region(mid - tw / 2 - pad, y, tw + 2 * pad, bh, KEY_ENTER);
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
    DrawRectangleLines(x, y, w, h, PAL_CLR(YELLOW));
    bfont_draw(t, x + ML_PAD, y + 4, PAL_CLR(YELLOW));
    touch_region(x, y, w, h, key);
    return w;
}

int ml_count_panel_height(void) {
    int gh = BFONT_GLYPH_H;
    return ML_PAD + gh + ML_PAD + gh * 2 + ML_PAD / 2 + gh + ML_PAD + gh + ML_PAD;
}

int ml_count_panel(int x, int y, int w, const char *heading, int value, const char *sub,
                   const char *left, const char *right) {
    const int gh = BFONT_GLYPH_H, pad = ML_PAD;
    int ty = y + pad;
    bfont_draw(heading ? heading : "", x + pad, ty, PAL_CLR(YELLOW));
    ty += gh + pad;
    // The buttons and the number.
    static const char *const LABELS[4] = { "-10", "-1", "+1", "+10" };
    static const int KEYS[4] = { KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_UP };
    int bh = gh * 2, bw = (int)bfont_measure("+10").x + 2 * pad;
    int mid = x + w / 2;
    int box_w = (int)bfont_measure("00000").x + 4 * pad;
    int xs[4] = { mid - box_w / 2 - 2 * pad - 2 * bw, mid - box_w / 2 - pad - bw,
                  mid + box_w / 2 + pad, mid + box_w / 2 + 2 * pad + bw };
    for (int k = 0; k < 4; k++) {
        DrawRectangleLines(xs[k], ty, bw, bh, PAL_CLR(YELLOW));
        int lw = (int)bfont_measure(LABELS[k]).x;
        bfont_draw(LABELS[k], xs[k] + (bw - lw) / 2, ty + (bh - gh) / 2, PAL_CLR(YELLOW));
        touch_region(xs[k], ty, bw, bh, KEYS[k]);
    }
    char nb[16];
    snprintf(nb, sizeof nb, "%d", value);
    DrawRectangle(mid - box_w / 2, ty, box_w, bh, PAL_CLR(BLACK));
    DrawRectangleLines(mid - box_w / 2, ty, box_w, bh, PAL_CLR(WHITE));
    bfont_draw(nb, mid - (int)bfont_measure(nb).x / 2, ty + (bh - gh) / 2, PAL_CLR(WHITE));
    ty += bh + pad / 2;
    if (sub && sub[0]) bfont_draw(sub, mid - (int)bfont_measure(sub).x / 2, ty, PAL_CLR(WHITE));
    ty += gh + pad;
    if (left && left[0])   bfont_draw(left, x + pad, ty, PAL_CLR(WHITE));
    if (right && right[0]) bfont_draw(right, x + w - pad - (int)bfont_measure(right).x, ty, PAL_CLR(WHITE));
    return ml_count_panel_height();
}

bool ml_stepper_keys(int *value, int lo, int hi) {
    int v = *value;
    if (input_key_pressed(KEY_LEFT))  v -= 1;
    if (input_key_pressed(KEY_RIGHT)) v += 1;
    if (input_key_pressed(KEY_DOWN))  v -= 10;
    if (input_key_pressed(KEY_UP))    v += 10;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    bool changed = (v != *value);
    *value = v;
    return changed;
}
