// src/modern/uikit.c -- the modern screens' shared kit (see uikit.h).

#include "modern/uikit.h"
#include "game.h"
#include "lattice.h"
#include "layout.h"
#include "palette.h"
#include "resources.h"
#include "bfont.h"
#include "ui.h"
#include "touch.h"
#include <stdio.h>
#include <string.h>

#define GH BFONT_GLYPH_H

Color uk_fill(void) { return (Color){ 12, 14, 30, 255 }; }
Color uk_ink(void)  { return (Color){ 16, 18, 36, 255 }; }
int   uk_title_h(void) { return GH + 14; }
int   uk_line_h(void)  { return GH + 2; }

void uk_panel(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, uk_fill());
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));
}

void uk_sheet(void) {
    ML_Rect r = ml_full();
    DrawRectangle(r.x, r.y, r.w, r.h, uk_fill());
}

int uk_title(int x, int y, int w, const char *left, const char *right, Color right_c) {
    int th = uk_title_h();
    int ty = y + (th - GH) / 2;
    int rw = (right && right[0]) ? bfont_text_width(right) : 0;
    if (rw) bfont_draw(right, x + w - ML_PAD - rw, ty, right_c);
    if (left && left[0]) {
        // A long title gives way to the right text: cut at a word, with "..".
        int room = w - 2 * ML_PAD - (rw ? rw + 2 * ML_PAD : 0);
        if (bfont_text_width(left) <= room) {
            bfont_draw(left, x + ML_PAD, ty, PAL_CLR(YELLOW));
        } else {
            char buf[160];
            snprintf(buf, sizeof buf, "%s", left);
            size_t n = strlen(buf);
            while (n > 0) {
                buf[--n] = '\0';
                char probe[164];
                snprintf(probe, sizeof probe, "%s..", buf);
                if (bfont_text_width(probe) <= room) { bfont_draw(probe, x + ML_PAD, ty, PAL_CLR(YELLOW)); break; }
            }
        }
    }
    lattice_band_h(x, y + th, w, UK_BAND);
    return y + th + UK_BAND;
}

void uk_gold_text(const Game *g, char *out, int cap) {
    if (!g || !g->res) { out[0] = '\0'; return; }
    snprintf(out, (size_t)cap, "%s %d", g->res->ui.cv_gold, g->stats.gold);
}

void uk_dim(void) {
    ML_Rect r = ml_full();
    DrawRectangle(r.x, r.y, r.w, r.h, (Color){ 0, 0, 0, 120 });
}

ML_Rect uk_inlay(int w, int h, const char *title, const char *right) {
    ML_Rect a = ml_area();
    int sp = ml_space();
    bool titled = (title && title[0]) || (right && right[0]);
    if (!titled) h -= uk_title_h() + UK_BAND;    // no title: no strip
    if (w > a.w - 2 * sp) w = a.w - 2 * sp;
    if (h > a.h) h = a.h;             // the tallest may meet the area's edges
    int x = a.x + (a.w - w) / 2, y = a.y + (a.h - h) / 2;
    uk_dim();
    uk_panel(x, y, w, h);
    if (!titled) return (ML_Rect){ x, y, w, h };
    int top = uk_title(x, y, w, title, right, PAL_CLR(YELLOW));
    return (ML_Rect){ x, top, w, y + h - top };
}

void uk_picture(Texture2D t, int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, PAL_CLR(BLACK));
    if (t.id) ui_blit(t, x, y, w, h);
    DrawRectangleLines(x - 1, y - 1, w + 2, h + 2, (Color){ 150, 118, 48, 255 });
}

int uk_lines(const char *text, int w) {
    int n = 0;
    const char *p = text ? text : "";
    char line[200];
    while (*p && bfont_take_line(&p, w, line, (int)sizeof line) > 0) n++;
    return n;
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
        bfont_draw(line, lx, y, fg);
        y += lh;
    }
    return y;
}

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
    return i >= 0 && i < 8 && r->enabled[i];
}

// ---- the place scene ---------------------------------------------------------------

UkScene uk_scene(const char *title, const char *right, Texture2D bd, int rows) {
    return uk_scene_ex(title, right, bd, rows, 2 * GH);
}

UkScene uk_scene_for(const char *title, const char *right, Texture2D bd, int rows, const char *intro) {
    int n = uk_lines(intro, ml_full().w - 2 * ML_PAD);
    if (n < 2) n = 2;
    if (n > 3) n = 3;
    return uk_scene_ex(title, right, bd, rows, n * GH);
}

UkScene uk_scene_ex(const char *title, const char *right, Texture2D bd, int rows, int intro_min) {
    UkScene L;
    L.full = ml_full();
    ML_Rect r = L.full;
    L.rows = rows < 2 ? 2 : rows;
    DrawRectangle(r.x, r.y, r.w, r.h, uk_fill());
    int top = uk_title(r.x, r.y, r.w, title, right, PAL_CLR(YELLOW));
    L.scale = r.w / ML_BACKDROP_W;
    if (L.scale > 3) L.scale = 3;
    if (L.scale < 1) L.scale = 1;
    int bw = ML_BACKDROP_W * L.scale, bh = ML_BACKDROP_H * L.scale;
    int rows_h = ml_list_height(L.rows);
    L.rows_y = r.y + r.h - rows_h;
    // Whatever the rows and the introduction need comes off the backdrop's top,
    // in whole source pixels so the art stays square.
    int room = L.rows_y - ML_ROW_RULE - intro_min - top;
    L.trim = 0;
    if (bh > room) {
        int src_cut = (bh - room + L.scale - 1) / L.scale;
        L.trim = src_cut * L.scale;
    }
    L.scene = (ML_Rect){ r.x + (r.w - bw) / 2, top, bw, bh - L.trim };
    DrawRectangle(L.scene.x, L.scene.y, L.scene.w, L.scene.h, PAL_CLR(BLACK));
    if (bd.id && bd.height > 0) {
        float per = (float)bd.height / (float)bh;            // source px per screen px
        Rectangle src = { 0, L.trim * per, (float)bd.width, (bh - L.trim) * per };
        Rectangle dst = { (float)L.scene.x, (float)L.scene.y, (float)L.scene.w, (float)L.scene.h };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }
    // The picture frame: the lattice in the bars either side of the backdrop.
    int side = L.scene.x - r.x;
    if (side > 0) {
        lattice_band_v(r.x, top, side, L.scene.h);
        lattice_band_v(L.scene.x + L.scene.w, top, r.x + r.w - (L.scene.x + L.scene.w), L.scene.h);
    }
    L.intro_y = L.scene.y + L.scene.h;
    L.intro_h = L.rows_y - ML_ROW_RULE - L.intro_y;
    lattice_band_h(r.x, L.rows_y - ML_ROW_RULE, r.w, ML_ROW_RULE);
    return L;
}

void uk_scene_blit(const UkScene *L, Texture2D t, int bx, int by, int bw, int bh) {
    if (!t.id) return;
    int x = L->scene.x + bx * L->scale, y = L->scene.y + by * L->scale - L->trim;
    int w = bw * L->scale, h = bh * L->scale;
    // Clip what the trim cut off.
    int cut = L->scene.y - y;
    if (cut <= 0) { ui_blit(t, x, y, w, h); return; }
    if (cut >= h) return;
    float per = (float)t.height / (float)h;
    Rectangle src = { 0, cut * per, (float)t.width, (h - cut) * per };
    Rectangle dst = { (float)x, (float)L->scene.y, (float)w, (float)(h - cut) };
    DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

void uk_scene_figure(const UkScene *L, Texture2D t, int x) {
    if (!t.id) return;
    int s = 2 * CL_TILE_W;
    int h = s < L->scene.h ? s : L->scene.h;
    ui_blit(t, L->scene.x + x, L->scene.y + L->scene.h - h, s, h);
}

void uk_scene_intro(const UkScene *L, const char *text) {
    const ML_Rect r = L->full;
    int w = r.w - 2 * ML_PAD;
    const char *p = text ? text : "";
    char l[3][200];
    int n = 0;
    while (n < 3 && *p && bfont_take_line(&p, w, l[n], (int)sizeof l[n]) > 0) n++;
    int fit = L->intro_h / GH;
    if (n > fit) n = fit;
    int y = L->intro_y + (L->intro_h - n * GH) / 2;
    for (int i = 0; i < n; i++) bfont_draw(l[i], r.x + ML_PAD, y + i * GH, PAL_CLR(WHITE));
}

void uk_scene_rows(const UkScene *L, int n, int cursor, MlRowFn fn, void *ctx, int touch_list) {
    ml_list_draw(L->full.x, L->rows_y, L->full.w, ml_list_height(L->rows), n, cursor, fn, ctx,
                 touch_list, uk_ink());
}

void uk_scene_split(const UkScene *L, const char *text, const char *labels[2], int cursor, int touch_list) {
    uk_scene_split_n(L, text, labels, 2, cursor, touch_list);
}

void uk_scene_split_n(const UkScene *L, const char *text, const char *labels[], int n, int cursor, int touch_list) {
    const ML_Rect r = L->full;
    int top = L->intro_y, bottom = r.y + r.h;
    DrawRectangle(r.x, top, r.w, bottom - top, uk_fill());
    lattice_band_h(r.x, top, r.w, UK_BAND);
    top += UK_BAND;
    if (n < 1) n = 1;
    if (n > 4) n = 4;
    // The buttons: two side by side; three or more stacked, so the words keep
    // most of the width. Each is as wide as its label needs, at least.
    int ws[4], widest = 0, sum = 0;
    for (int i = 0; i < n; i++) {
        ws[i] = bfont_text_width(labels[i] ? labels[i] : "") + 2 * UK_INSET;
        if (ws[i] > widest) widest = ws[i];
        sum += ws[i];
    }
    bool stacked = n > 2;
    int buttons_w = stacked ? widest + 2 * UK_INSET : sum + (n - 1) * UK_INSET + 2 * UK_INSET;
    int half = r.w - buttons_w - UK_BAND;
    if (!stacked) {
        if (half < r.w * 2 / 5) half = r.w * 2 / 5;
        if (half > r.w / 2) half = r.w / 2;
    }
    // The words, in a fixed area: the same place whichever button is chosen.
    int tw = half - 2 * UK_INSET;
    int fit = (bottom - top - 2 * ML_PAD) / uk_line_h();
    int lines = uk_lines(text, tw);
    if (lines > fit) lines = fit;
    int ty = top + UK_INSET;            // top-aligned: the words never move
    uk_flow(r.x + UK_INSET, ty, tw, r.x, 0, bottom - ML_PAD, text, PAL_CLR(WHITE));
    lattice_band_v(r.x + half, top, UK_BAND, bottom - top);
    int area_x = r.x + half + UK_BAND + UK_INSET, area_w = r.x + r.w - UK_INSET - area_x;
    int xs[4], ys[4], bws[4], bh;
    if (stacked) {
        int gap = 4;
        bh = (bottom - top - 2 * gap - (n - 1) * gap) / n;
        if (bh > ml_row_h()) bh = ml_row_h();
        int y0 = top + (bottom - top - (n * bh + (n - 1) * gap)) / 2;
        for (int i = 0; i < n; i++) { xs[i] = area_x; ys[i] = y0 + i * (bh + gap); bws[i] = area_w; }
    } else {
        bh = ml_row_h();
        int spare = area_w - (sum + (n - 1) * UK_INSET), x = area_x;
        for (int i = 0; i < n; i++) {
            bws[i] = ws[i] + (spare > 0 ? spare / n + (i == n - 1 ? spare % n : 0) : 0);
            xs[i] = x; ys[i] = top + (bottom - top - bh) / 2;
            x += bws[i] + UK_INSET;
        }
    }
    for (int i = 0; i < n; i++) {
        bool sel = i == cursor;
        const char *lab = labels[i] ? labels[i] : "";
        if (sel) DrawRectangle(xs[i], ys[i], bws[i], bh, PAL_CLR(YELLOW));
        else     DrawRectangleLines(xs[i], ys[i], bws[i], bh, (Color){ 200, 160, 60, 255 });
        int lw = bfont_text_width(lab);
        bfont_draw(lab, xs[i] + (bws[i] - lw) / 2, ys[i] + (bh - bfont_line_height()) / 2,
                   sel ? uk_ink() : PAL_CLR(WHITE));
        if (touch_list) touch_region_row(xs[i], ys[i], bws[i], bh, touch_list, i);
    }
}

// ---- in-lays -----------------------------------------------------------------------

void uk_count_inlay(const char *title, Texture2D face, const char *lines[], Color colors[], int nlines,
                    int value, int max, const char *act_label, const char *cancel_label, int touch_list) {
    // The picture and its words, the count row across the width under them,
    // then the two rows.
    const int size = 2 * CL_TILE_W;
    int ch = ml_count_buttons_height();
    int h = uk_title_h() + UK_BAND + UK_INSET + size + UK_INSET + ch + UK_INSET + UK_BAND + ml_list_height(2);
    ML_Rect b = uk_inlay(UK_INLAY_W, h, title, NULL);
    int px = b.x + UK_INSET, py = b.y + UK_INSET;
    int text_x = px + size + UK_INSET, text_w = b.x + b.w - UK_INSET - text_x;
    UkRows rows = { { act_label, cancel_label }, { true, true } };
    int foot = uk_foot_rows(b, 2, 0, uk_rows_fn, &rows, touch_list);
    int by = foot - UK_INSET - ch;
    uk_picture(face, px, py, size, size);
    int y = py;
    for (int i = 0; i < nlines; i++) {
        if (!lines[i]) continue;
        if (!lines[i][0]) { y += GH / 2; continue; }
        y = uk_flow(text_x, y, text_w, text_x, 0, py + size, lines[i], colors ? colors[i] : PAL_CLR(WHITE));
    }
    ml_count_buttons(b.x + UK_INSET, by, b.w - 2 * UK_INSET, value, max);
}

void uk_result_inlay(const char *title, Texture2D face, const char *text, const char *row_label,
                     int touch_list) {
    // As tall as the picture or the words, whichever is taller.
    const int size = 2 * CL_TILE_W;
    UkDoc d = { 0 };
    uk_doc_add(&d, text, PAL_CLR(WHITE));
    int w = UK_INLAY_W - 2 * UK_INSET;
    ML_Rect probe = { 0, 0, w, 0 };
    int body = uk_doc_height(&d, probe, face.id ? size : 0, size);
    if (face.id && body < size) body = size;
    if (body < 3 * uk_line_h()) body = 3 * uk_line_h();
    int h = uk_title_h() + UK_BAND + 2 * UK_INSET + body + ML_PAD + UK_BAND + ml_list_height(1);
    if (h > UK_TALL_H) h = UK_TALL_H;
    ML_Rect b = uk_inlay(UK_INLAY_W, h, title, NULL);
    UkRows rows = { { row_label }, { true } };
    int foot = uk_foot_rows(b, 1, 0, uk_rows_fn, &rows, touch_list);
    ML_Rect a = { b.x + UK_INSET, b.y + UK_INSET, w, foot - ML_PAD - (b.y + UK_INSET) };
    int pic = size < a.h ? size : a.h;
    if (face.id) uk_picture(face, a.x, a.y, pic, pic);
    uk_doc_draw(&d, a, face.id ? pic : 0, pic, -1, true);
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

static int s_doc_h;

int uk_doc_height(const UkDoc *d, ML_Rect a, int pic_w, int pic_h) {
    a.h = 100000;
    uk_doc_draw(d, a, pic_w, pic_h, -1, false);
    return s_doc_h;
}

int uk_doc_draw(const UkDoc *d, ML_Rect a, int pic_w, int pic_h, int page, bool draw) {
    const int lh = uk_line_h();
    bool pager = page >= 0;          // page < 0: the first page, no pager
    if (page < 0) page = 0;
    int pic_r = pic_w > 0 ? a.x + pic_w + UK_INSET : a.x;
    // Beside a picture the words stay in one column all the way down; they
    // never wrap back under it.
    int pic_b = pic_w > 0 ? a.y + (a.h > pic_h ? a.h : pic_h) + ML_PAD : a.y + pic_h + ML_PAD;
    int top = a.y;
    if (pic_w > 0 && a.x + a.w - pic_r < 16 * BFONT_GLYPH_W) {
        // Too narrow beside the picture to read: the words start under it.
        top = a.y + pic_h + ML_PAD;
        pic_w = 0;
    }
    // Layout pass: walk every line, starting a new page when the next one
    // would pass the foot (less a line for the pager).
    int foot = a.y + a.h - (pager ? lh : 0);
    int pages = 1, y = top, max_y = top;
    char line[200];
    for (int i = 0; i < d->n; i++) {
        const char *p = d->pool + d->off[i];
        bool first = true;
        if (d->gap[i] && y > top) y += lh / 2 + 2;
        while (*p) {
            if (y + GH > foot) { pages++; y = top; }
            bool beside = y < pic_b && pic_w > 0;
            int lx = beside ? pic_r : a.x;
            int lw = a.x + a.w - lx;
            const char *start = p;
            if (bfont_take_line(&p, lw, line, (int)sizeof line) <= 0) break;
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
            if (y > max_y) max_y = y;
        }
    }
    s_doc_h = (pages > 1 ? a.h : max_y - a.y);
    if (draw && pager && pages > 1) {
        char pg[32];
        snprintf(pg, sizeof pg, "%d/%d", page + 1, pages);
        int py = a.y + a.h - GH, aw = GH;
        int nx = a.x + a.w - aw;
        int tx = nx - ML_PAD - bfont_text_width(pg);
        int px = tx - ML_PAD - aw;
        bfont_draw(pg, tx, py, PAL_CLR(YELLOW));
        Color dim = (Color){ 90, 90, 90, 255 };
        DrawTriangle((Vector2){ (float)(px + aw / 2), (float)py }, (Vector2){ (float)px, (float)(py + GH) },
                     (Vector2){ (float)(px + aw), (float)(py + GH) }, page > 0 ? PAL_CLR(YELLOW) : dim);
        DrawTriangle((Vector2){ (float)nx, (float)py }, (Vector2){ (float)(nx + aw / 2), (float)(py + GH) },
                     (Vector2){ (float)(nx + aw), (float)py }, page + 1 < pages ? PAL_CLR(YELLOW) : dim);
        if (page > 0) touch_region(px - 8, py - 8, aw + 16, GH + 16, KEY_UP);
        if (page + 1 < pages) touch_region(nx - 8, py - 8, aw + 16, GH + 16, KEY_DOWN);
    }
    return pages;
}
