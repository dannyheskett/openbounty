// src/modern/uikit.c -- the modern screens' shared kit (see uikit.h).

#include "modern/uikit.h"
#include "gfx.h"
#include "game.h"
#include "lattice.h"
#include "layout.h"
#include "palette.h"
#include "resources.h"
#include "bfont.h"
#include "ui.h"
#include "touch.h"
#include "uitouch.h"
#include "overlay.h"
#include "overlay_impl.h"
#include "sprites.h"
#include <stdio.h>
#include <string.h>

#define GH BFONT_GLYPH_H

Color uk_fill(void)     { return (Color){  12,  14,  30, 255 }; }
Color uk_ink(void)      { return (Color){  16,  18,  36, 255 }; }
Color uk_edge(void)     { return (Color){ 150, 118,  48, 255 }; }
Color uk_edge_dim(void) { return (Color){  60,  52,  34, 255 }; }
Color uk_ghost(void)    { return (Color){  60,  60,  70, 255 }; }
Color uk_shade(void)    { return (Color){   0,   0,   0, 150 }; }
Color uk_hint_bg(void)  { return (Color){   0,   0,   0, 190 }; }
Color uk_button(void)   { return (Color){ 200, 160,  60, 255 }; }
Color uk_bar_edge(void) { return (Color){  90,  72,  30, 255 }; }
int   uk_title_h(void)  { return GH + 14; }
int   uk_line_h(void)   { return GH + 2; }

// ---- words -------------------------------------------------------------------------

void uk_mark_cut(char *buf, int cap, int w) {
    size_t n = strlen(buf);
    char probe[256];
    for (;;) {
        snprintf(probe, sizeof probe, "%.*s..", (int)n, buf);
        if (n == 0 || bfont_text_width(probe) <= w) break;
        n--;
        while (n > 0 && buf[n - 1] == ' ') n--;
    }
    snprintf(buf, (size_t)cap, "%s", probe);
}

void uk_line(const char *text, int x, int y, int w, Color fg) {
    if (!text || !text[0]) return;
    if (bfont_text_width(text) <= w) { bfont_draw(text, x, y, fg); return; }
    char buf[256];
    snprintf(buf, sizeof buf, "%s", text);
    uk_mark_cut(buf, sizeof buf, w);
    bfont_draw(buf, x, y, fg);
}

int uk_lines(const char *text, int w) {
    int n = 0;
    const char *p = text ? text : "";
    char line[200];
    while (*p && bfont_take_line(&p, w, line, (int)sizeof line) > 0) n++;
    return n;
}

int uk_lines_draw(const char *text, int x, int y, int w, int max_lines, Color fg) {
    const char *p = text ? text : "";
    char line[200];
    for (int i = 0; i < max_lines && *p; i++) {
        if (bfont_take_line(&p, w, line, (int)sizeof line) <= 0) break;
        if (*p && i + 1 == max_lines) uk_mark_cut(line, sizeof line, w);
        bfont_draw(line, x, y, fg);
        y += uk_line_h();
    }
    return y;
}

int uk_flow(int x, int y, int w, int pic_r, int pic_b, int max_y, const char *text, Color fg) {
    const char *p = text ? text : "";
    char line[200];
    int lh = uk_line_h();
    while (*p && y + GH <= max_y) {
        bool beside = y < pic_b && pic_r > x;
        int lx = beside ? pic_r : x;
        int lw = w - (lx - x);
        if (bfont_take_line(&p, lw, line, (int)sizeof line) <= 0) break;
        // The last line that fits, with more to come: marked as cut.
        if (*p && y + lh + GH > max_y) uk_mark_cut(line, sizeof line, lw);
        bfont_draw(line, lx, y, fg);
        y += lh;
    }
    return y;
}

int uk_words_centred(const char *text, int cx, int y, int w, int max_lines, Color fg) {
    const char *p = text ? text : "";
    char line[200];
    for (int n = 0; *p && n < max_lines; n++) {
        if (bfont_take_line(&p, w, line, (int)sizeof line) <= 0) break;
        if (*p && n + 1 == max_lines) uk_mark_cut(line, sizeof line, w);
        bfont_draw(line, cx - bfont_text_width(line) / 2, y, fg);
        y += uk_line_h();
    }
    return y;
}

// ---- the title strip ------------------------------------------------------------------

int uk_title(int x, int y, int w, const char *left, const char *right, const char *close) {
    const Resources *res = resources_current();
    const ResUI *ui = res ? &res->ui : NULL;
    int th = uk_title_h();
    int ty = y + (th - GH) / 2;
    const int room = w - 2 * UK_INSET, gap = 2 * UK_INSET;
    char cl[64] = "";
    if (close && close[0]) ml_hint_text(cl, sizeof cl, close, ui ? ui->key_esc : "Esc", ui ? ui->pad_back : "");
    int lw = (left && left[0]) ? bfont_text_width(left) : 0;
    int rw = (right && right[0]) ? bfont_text_width(right) : 0;
    // Close gives up its key name first, then the words at the right go; only
    // then is the title cut.
    if (cl[0] && lw + gap + bfont_text_width(cl) + (rw ? rw + gap : 0) > room)
        snprintf(cl, sizeof cl, "%s", close);
    int cw = cl[0] ? bfont_text_width(cl) : 0;
    if (rw && lw + gap + (cw ? cw + gap : 0) + rw > room) rw = 0;
    int end = x + w - UK_INSET;
    if (cw) {
        bfont_draw(cl, end - cw, ty, PAL_CLR(WHITE));
        ui_button(end - cw - UK_INSET, y, cw + 2 * UK_INSET, th, KEY_ESCAPE);
        end -= cw + gap;
    }
    if (rw) {
        bfont_draw(right, end - rw, ty, PAL_CLR(YELLOW));
        end -= rw + gap;
    }
    if (lw) uk_line(left, x + UK_INSET, ty, end - (x + UK_INSET), PAL_CLR(YELLOW));
    lattice_band_h(x, y + th, w, UK_BAND);
    return y + th + UK_BAND;
}

void uk_gold_text(const Game *g, char *out, int cap) {
    if (!g || !g->res) { out[0] = '\0'; return; }
    snprintf(out, (size_t)cap, "%s %d", g->res->ui.cv_gold, g->stats.gold);
}

// ---- pictures ---------------------------------------------------------------------------

void uk_picture(Texture2D t, int x, int y, int w, int h) {
    gfx_rect(x, y, w, h, PAL_CLR(BLACK));
    if (t.id) ui_blit(t, x, y, w, h);
    gfx_rect_lines(x - 1, y - 1, w + 2, h + 2, uk_edge());
}

void uk_picture_cut(Texture2D t, int x, int y, int w, int h) {
    gfx_rect(x, y, w, h, PAL_CLR(BLACK));
    if (t.id) {
        int sw = t.width < w ? t.width : w, sh = t.height < h ? t.height : h;
        Rectangle src = { 0, 0, (float)sw, (float)sh };
        Rectangle dst = { (float)x, (float)y, (float)sw, (float)sh };
        gfx_texture_draw(t, src, dst, WHITE);
    }
    gfx_rect_lines(x - 1, y - 1, w + 2, h + 2, uk_edge());
}

void uk_figure(Texture2D t, int x, int foot_y, int scale, int top_y, bool mirror) {
    if (!t.id || scale < 1) return;
    int w = t.width * scale, h = t.height * scale;
    int y = foot_y - h;
    int cut = 0;                                   // source rows cut off the top
    if (y < top_y) cut = (top_y - y + scale - 1) / scale;
    if (cut >= t.height) return;
    Rectangle src = { 0, (float)cut, (float)(mirror ? -t.width : t.width), (float)(t.height - cut) };
    Rectangle dst = { (float)x, (float)(y + cut * scale), (float)w, (float)((t.height - cut) * scale) };
    gfx_texture_draw(t, src, dst, WHITE);
}

// ---- rows --------------------------------------------------------------------------------

int uk_foot_rows(ML_Rect body, int n, int cursor, MlRowFn fn, void *ctx, int touch_list) {
    int rows_h = ml_list_height(n);
    int ry = body.y + body.h - rows_h;
    lattice_band_h(body.x, ry - UK_BAND, body.w, UK_BAND);
    ml_list_draw(body.x, ry, body.w, rows_h, n, cursor, fn, ctx, touch_list, uk_ink());
    return ry - UK_BAND;
}

bool uk_rows_fn(void *ctx, int i, char *label, char *right, int cap) {
    const UkRows *r = (const UkRows *)ctx;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", (i >= 0 && i < 8 && r->label[i]) ? r->label[i] : "");
    if (r->esc && i == r->esc - 1) ml_exit_hint(right);
    return i >= 0 && i < 8 && r->enabled[i];
}

// ---- the place scene ---------------------------------------------------------------

// One piece of the column, `h` rows of it from its top, at 1x; mirrored for the
// right bar so the ivy faces in on both sides.
static void column_piece(Texture2D t, int x, int y, int w, int h, bool mirror) {
    if (!t.id || h <= 0) return;
    if (h > t.height) h = t.height;
    Rectangle src = { 0, 0, (float)(mirror ? -t.width : t.width), (float)h };
    Rectangle dst = { (float)x, (float)y, (float)w, (float)h };
    gfx_texture_draw(t, src, dst, WHITE);
}

// A column exactly `h` tall: the capital at the top, the base at the bottom,
// the shaft repeated between them.
static void draw_column(const Sprites *sp, int x, int y, int w, int h, bool mirror) {
    Texture2D cap = sp->scene_column[0], shaft = sp->scene_column[1], base = sp->scene_column[2];
    column_piece(cap, x, y, w, cap.height, mirror);
    int end = y + h - base.height;
    for (int cy = y + cap.height; cy < end; cy += shaft.height)
        column_piece(shaft, x, cy, w, end - cy, mirror);
    column_piece(base, x, end, w, base.height, mirror);
}

// The backdrop as a band at `top`, `band_h` tall: its top trimmed in whole
// source pixels so the art stays square, the columns (or the lattice) in the
// bars beside it, and a lattice divider under it. Whatever the caller puts
// below starts at scene.y + scene.h + UK_BAND.
UkScene uk_scene_band(ML_Rect r, int top, Texture2D bd, int band_h) {
    return uk_scene_band_at(r, top, bd, band_h, r.w / ML_BACKDROP_W);
}

UkScene uk_scene_band_at(ML_Rect r, int top, Texture2D bd, int band_h, int scale) {
    UkScene L;
    memset(&L, 0, sizeof L);
    L.full = r;
    L.scale = scale;
    if (L.scale > r.w / ML_BACKDROP_W) L.scale = r.w / ML_BACKDROP_W;
    if (L.scale > 3) L.scale = 3;
    if (L.scale < 1) L.scale = 1;
    int bw = ML_BACKDROP_W * L.scale, bh = ML_BACKDROP_H * L.scale;
    L.trim = 0;
    if (bh > band_h) {
        int src_cut = (bh - band_h + L.scale - 1) / L.scale;
        L.trim = src_cut * L.scale;
    }
    L.scene = (ML_Rect){ r.x + (r.w - bw) / 2, top, bw, bh - L.trim };
    gfx_rect(L.scene.x, L.scene.y, L.scene.w, L.scene.h, PAL_CLR(BLACK));
    if (bd.id && bd.height > 0) {
        int per = bd.height / ML_BACKDROP_H > 0 ? bd.height / ML_BACKDROP_H : 1;   // source px per art px
        Rectangle src = { 0, (float)(L.trim / L.scale * per), (float)bd.width,
                          (float)((bh - L.trim) / L.scale * per) };
        Rectangle dst = { (float)L.scene.x, (float)L.scene.y, (float)L.scene.w, (float)L.scene.h };
        gfx_texture_draw(bd, src, dst, WHITE);
    }
    // The picture frame: a column in each bar beside the backdrop when the
    // pack has one (capital, shaft repeated, base; mirrored on the right),
    // else the lattice.
    int side = L.scene.x - r.x;
    if (side > 0) {
        const Sprites *sp = modern_overlay_sprites();
        int rx = L.scene.x + L.scene.w, rw = r.x + r.w - rx;
        if (sp && sp->scene_column[0].id && sp->scene_column[1].id && sp->scene_column[2].id) {
            // A column at its own width, standing against the picture; what
            // is left of a wider bar is the panel fill, never a stretched
            // column.
            int cw = sp->scene_column[0].width;
            if (cw > side) cw = side;
            gfx_rect(r.x, top, side, L.scene.h, uk_fill());
            gfx_rect(rx, top, rw, L.scene.h, uk_fill());
            draw_column(sp, L.scene.x - cw, top, cw, L.scene.h, false);
            draw_column(sp, rx, top, cw, L.scene.h, true);
        } else {
            lattice_band_v(r.x, top, side, L.scene.h);
            lattice_band_v(rx, top, rw, L.scene.h);
        }
    }
    lattice_band_h(r.x, L.scene.y + L.scene.h, r.w, UK_BAND);
    return L;
}

void uk_scene_figure(const UkScene *L, Texture2D t, int x) {
    uk_figure(t, L->scene.x + x, L->scene.y + L->scene.h, 2, L->scene.y, false);
}


// ---- paged words beside a picture ------------------------------------------------

void uk_doc_add(UkDoc *d, const char *text, Color fg) {
    if (!text || d->n >= UK_DOC_PARAS) return;
    int len = (int)strlen(text);
    if (d->used + len + 1 > (int)sizeof d->pool) return;
    d->off[d->n] = d->used;
    memcpy(d->pool + d->used, text, (size_t)len + 1);
    d->used += len + 1;
    d->label[d->n] = 0;
    d->fg[d->n] = fg;
    d->gap[d->n] = d->next_gap && d->n > 0;
    d->next_gap = false;
    d->n++;
}

void uk_doc_gap(UkDoc *d) { d->next_gap = true; }

void uk_doc_labeled(UkDoc *d, const char *tmpl, const char *value) {
    char buf[RES_BANNER_LEN];
    ResTemplateVar vars[] = { { "VALUE", value ? value : "" } };
    resources_format_template(buf, sizeof buf, tmpl, vars, 1);
    const char *at = tmpl ? strstr(tmpl, "%VALUE%") : NULL;
    int first = d->n;
    uk_doc_add(d, buf, PAL_CLR(WHITE));
    if (at && first < d->n) {
        int lab = (int)(at - tmpl), len = (int)strlen(buf);
        d->label[first] = lab < len ? lab : len;
    }
}

int uk_doc_draw(const UkDoc *d, ML_Rect a, int pic_w, int pic_h, int page, bool draw) {
    const int lh = uk_line_h();
    bool pager = page >= 0;          // page < 0: the first page, no pager
    if (page < 0) page = 0;
    int pic_r = pic_w > 0 ? a.x + pic_w + UK_INSET : a.x;
    // Beside a picture the words stay in one column all the way down; they
    // never wrap back under it.
    int pic_b = pic_w > 0 ? a.y + (a.h > pic_h ? a.h : pic_h) + UK_INSET : a.y + pic_h + UK_INSET;
    int top = a.y;
    if (pic_w > 0 && a.x + a.w - pic_r < 16 * BFONT_GLYPH_W) {
        // Too narrow beside the picture to read: the words start under it.
        top = a.y + pic_h + UK_INSET;
        pic_w = 0;
    }
    // Layout pass: walk every line, starting a new page when the next one
    // would pass the foot (less a line for the pager).
    int foot = a.y + a.h - (pager ? lh : 0);
    int pages = 1, y = top;
    bool cut = false;
    char line[200];
    for (int i = 0; i < d->n; i++) {
        const char *p = d->pool + d->off[i];
        bool first = true;
        if (d->gap[i] && y > top) y += lh / 2 + 2;
        while (*p) {
            if (y + GH > foot) {
                if (!pager) { cut = true; break; }     // one page: the rest is cut
                pages++;
                y = top;
            }
            bool beside = y < pic_b && pic_w > 0;
            int lx = beside ? pic_r : a.x;
            int lw = a.x + a.w - lx;
            const char *start = p;
            if (bfont_take_line(&p, lw, line, (int)sizeof line) <= 0) break;
            // The last line one page holds, with words after it: marked cut.
            bool more = *p || i + 1 < d->n;
            if (!pager && more && y + lh + GH > foot) { uk_mark_cut(line, sizeof line, lw); cut = true; }
            if (draw && pages - 1 == page) {
                int lab = first ? d->label[i] : 0;
                int n = (int)strlen(line);
                if (lab > 0 && lab <= n && line[0] == start[0]) {
                    char head[200];
                    snprintf(head, sizeof head, "%.*s", lab, line);
                    bfont_draw(head, lx, y, PAL_CLR(YELLOW));
                    bfont_draw(line + lab, lx + bfont_text_width(head), y, d->fg[i]);
                } else {
                    bfont_draw(line, lx, y, d->fg[i]);
                }
            }
            first = false;
            y += lh;
            if (cut) break;
        }
        if (cut) break;
    }
    if (draw && pager && pages > 1) {
        char pg[32];
        snprintf(pg, sizeof pg, "%d/%d", page + 1, pages);
        int py = a.y + a.h - GH, aw = GH;
        int nx = a.x + a.w - aw;
        int tx = nx - UK_INSET - bfont_text_width(pg);
        int px = tx - UK_INSET - aw;
        bfont_draw(pg, tx, py, PAL_CLR(YELLOW));
        Color off = PAL_CLR(DGREY);
        gfx_triangle((Vector2){ (float)(px + aw / 2), (float)py }, (Vector2){ (float)px, (float)(py + GH) },
                     (Vector2){ (float)(px + aw), (float)(py + GH) }, page > 0 ? PAL_CLR(YELLOW) : off);
        gfx_triangle((Vector2){ (float)nx, (float)py }, (Vector2){ (float)(nx + aw / 2), (float)(py + GH) },
                     (Vector2){ (float)(nx + aw), (float)py }, page + 1 < pages ? PAL_CLR(YELLOW) : off);
        if (page > 0) ui_button(px - 8, py - 8, aw + 16, GH + 16, KEY_UP);
        if (page + 1 < pages) ui_button(nx - 8, py - 8, aw + 16, GH + 16, KEY_DOWN);
    }
    return pages;
}

// ---- How many ----------------------------------------------------------------------

int uk_count(ML_Rect a, const char *heading, const char *sub, const char *cost, int value, int max) {
    int y = a.y;
    const int lh = uk_line_h();
    if (heading && heading[0]) { uk_line(heading, a.x, y, a.w, PAL_CLR(YELLOW)); y += lh; }
    if (sub && sub[0])         { uk_line(sub, a.x, y, a.w, PAL_CLR(WHITE));      y += lh; }
    if (cost && cost[0])       { uk_line(cost, a.x, y, a.w, PAL_CLR(YELLOW));    y += lh; }
    y += UK_INSET;
    return y + ml_count_buttons(a.x, y, a.w, value, max);
}
