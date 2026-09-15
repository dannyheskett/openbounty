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
    return uk_scene_ex(title, right, bd, rows, 2 * uk_line_h() + 2 * ML_PAD);
}

UkScene uk_scene_for(const char *title, const char *right, Texture2D bd, int rows, const char *intro) {
    int n = uk_lines(intro, ml_full().w - 2 * ML_PAD);
    if (n < 2) n = 2;
    if (n > 3) n = 3;
    return uk_scene_ex(title, right, bd, rows, n * uk_line_h() + 2 * ML_PAD);
}

UkScene uk_scene_ex(const char *title, const char *right, Texture2D bd, int rows, int intro_min) {
    UkScene L;
    L.full = ml_full();
    ML_Rect r = L.full;
    L.rows = rows < 1 ? 1 : rows;
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
    int room = L.rows_y - ML_ROW_RULE - intro_min - UK_BAND - top;
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
    // A lattice divider between the backdrop and the words under it.
    lattice_band_h(r.x, L.scene.y + L.scene.h, r.w, UK_BAND);
    L.intro_y = L.scene.y + L.scene.h + UK_BAND;
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
    int fit = L->intro_h / uk_line_h();
    if (n > fit) n = fit;
    int y = L->intro_y + (L->intro_h - n * uk_line_h()) / 2;
    for (int i = 0; i < n; i++) bfont_draw(l[i], r.x + ML_PAD, y + i * uk_line_h(), PAL_CLR(WHITE));
}

void uk_scene_rows(const UkScene *L, int n, int cursor, MlRowFn fn, void *ctx, int touch_list) {
    ml_list_draw(L->full.x, L->rows_y, L->full.w, ml_list_height(L->rows), n, cursor, fn, ctx,
                 touch_list, uk_ink());
}

// ---- in-lays -----------------------------------------------------------------------

void uk_count_inlay(const char *title, Texture2D face, const char *lines[], Color colors[], int nlines,
                    int value, int max, const char *act_label, const char *cancel_label, int touch_list) {
    // A card: the troop at 2x and what is on offer beside it, the count row
    // across the card, then Recruit and Cancel.
    UkDoc d = { 0 };
    for (int i = 0; i < nlines; i++) {
        if (!lines[i]) continue;
        if (!lines[i][0]) { uk_doc_gap(&d); continue; }
        uk_doc_add(&d, lines[i], colors ? colors[i] : PAL_CLR(WHITE));
    }
    UkCard c = { .title = title, .face = face, .doc = &d, .answers = { act_label, cancel_label },
                 .n_answers = 2, .cursor = 0, .touch_list = touch_list,
                 .extra_h = ml_count_buttons_height(), .min_w = UK_INLAY_W };
    UkCardOut o;
    uk_card(&c, &o);
    ml_count_buttons(o.extra.x, o.extra.y, o.extra.w, value, max);
}

void uk_result_inlay(const char *title, Texture2D face, const char *text, const char *row_label,
                     int touch_list) {
    UkDoc d = { 0 };
    uk_doc_add(&d, text, PAL_CLR(WHITE));
    UkCard c = { .title = title, .face = face, .doc = &d, .answers = { row_label }, .n_answers = 1,
                 .cursor = 0, .touch_list = touch_list };
    uk_card(&c, NULL);
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

static int s_doc_h, s_doc_last_w, s_doc_prev_w;   // the last layout's height and its last two lines' widths

int uk_doc_height(const UkDoc *d, ML_Rect a, int pic_w, int pic_h) {
    a.h = 100000;
    s_doc_last_w = s_doc_prev_w = 0;
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
            s_doc_prev_w = s_doc_last_w;
            s_doc_last_w = (lx - a.x) + bfont_text_width(line);
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

// ---- the card --------------------------------------------------------------------

typedef struct { const UkCard *c; } CardRowsCtx;

static bool card_row_fn(void *ctx, int i, char *label, char *right, int cap) {
    const UkCard *c = ((const CardRowsCtx *)ctx)->c;
    int k = i - c->touch_base;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", (k >= 0 && k < c->n_answers && c->answers[k]) ? c->answers[k] : "");
    return k >= 0 && k < c->n_answers && !c->disabled[k];
}

// --gallery mock-up: the conforming card (see uk_card_conform).
static bool s_card_mockup;
void uk_card_mockup(bool on) { s_card_mockup = on; }
bool uk_card_mockup_on(void) { return s_card_mockup; }

// The conforming card: one width (576), a title strip always, the picture at
// top-left, the words from the same spot, the buttons in a row along the
// bottom at the right, centred on what is behind it, its body at least the
// picture's height and growing by whole lines.
static void uk_card_conform(const UkCard *c, UkCardOut *out) {
    ML_Rect a = ml_area();
    const Resources *res = resources_current();
    const int pic = c->face.id ? 2 * CL_TILE_W : 0;
    const int inset = UK_INSET, gap = ML_PAD + 4, lh = uk_line_h();
    const char *title = (c->title && c->title[0]) ? c->title
                      : !res ? "" : c->n_answers >= 2 ? res->banners.card_title_question : res->banners.card_title_message;
    int w = c->min_w > UK_INLAY_W ? c->min_w : UK_INLAY_W;
    int cx_off = inset + (pic ? pic + inset : 0);
    int col = w - cx_off - inset;
    ML_Rect probe = { 0, 0, col, 0 };
    int words_h = c->doc ? uk_doc_height(c->doc, probe, 0, 0) : 0;
    words_h = (words_h + lh - 1) / lh * lh;
    int body_h = words_h > pic ? words_h : pic;
    int head = uk_title_h() + UK_BAND;
    int extra_block = c->extra_h ? gap + c->extra_h : 0;
    int answers_h = c->n_answers > 0 ? UK_BAND + ml_list_height(c->n_answers) - inset : 0;
    int h = head + 2 * inset + body_h + extra_block + answers_h;
    if (h > a.h - 2 * ml_space()) h = a.h - 2 * ml_space();
    int x = a.x + (a.w - w) / 2, y = a.y + (a.h - h) / 2;
    if (!c->no_dim || c->at_foot) uk_dim();
    uk_panel(x, y, w, h);
    int top = uk_title(x, y, w, title, c->right, PAL_CLR(YELLOW));
    int bx = x + inset, by = top + inset;
    if (pic) uk_picture(c->face, bx, by, pic, pic);
    if (c->doc) {
        ML_Rect area = { x + cx_off, by, col, (y + h) - inset - by };
        uk_doc_draw(c->doc, area, 0, 0, -1, true);
    }
    int ey = by + body_h + gap;
    if (out) {
        out->card = (ML_Rect){ x, y, w, h };
        out->extra = (ML_Rect){ bx, ey, w - 2 * inset, c->extra_h };
    }
    if (c->n_answers > 0) {
        int ry = y + h - ml_list_height(c->n_answers);
        lattice_band_h(x, ry - UK_BAND, w, UK_BAND);
        CardRowsCtx rc = { c };
        ml_list_draw_ex(x, ry, w, ml_list_height(c->n_answers), c->n_answers, c->cursor, card_row_fn, &rc,
                        c->touch_list, uk_ink(), c->touch_base);
    }
}

void uk_card(const UkCard *c, UkCardOut *out) {
    if (s_card_mockup) { uk_card_conform(c, out); return; }
    ML_Rect a = ml_area();
    const int pic = c->face.id ? 2 * CL_TILE_W : 0;
    const int inset = UK_INSET, gap = ML_PAD + 4;
    int head = (c->title && c->title[0]) || (c->right && c->right[0]) ? uk_title_h() + UK_BAND : 0;
    // The answers: full-width rows along the card's foot, stacked.
    int rows_h = c->n_answers > 0 ? UK_BAND + ml_list_height(c->n_answers) : 0;
    int label_w = 0;
    for (int i = 0; i < c->n_answers; i++) {
        int lw = bfont_text_width(c->answers[i] ? c->answers[i] : "") + 2 * ML_PAD;
        if (lw > label_w) label_w = lw;
    }
    int max_w = a.w - 2 * ml_space();
    int col_max = max_w - 2 * inset - (pic ? pic + inset : 0);
    if (col_max > 40 * BFONT_GLYPH_W) col_max = 40 * BFONT_GLYPH_W;
    int col_min = 20 * BFONT_GLYPH_W;
    int want_min = c->min_w - 2 * inset - (pic ? pic + inset : 0);
    if (col_min < want_min) col_min = want_min;
    if (col_min > col_max) col_min = col_max;
    int natural = 0;
    for (int i = 0; c->doc && i < c->doc->n; i++) {
        int tw = bfont_text_width(c->doc->pool + c->doc->off[i]);
        if (tw > natural) natural = tw;
    }
    ML_Rect probe = { 0, 0, 0, 0 };
    int col = col_min;
    if (pic) {
        // The narrowest column whose words fit beside the picture; failing
        // that, the widest (the fewest lines).
        col = col_max;
        for (int w = col_min; w <= col_max; w += BFONT_GLYPH_W) {
            probe.w = w;
            if ((c->doc ? uk_doc_height(c->doc, probe, 0, 0) : 0) <= pic) { col = w; break; }
        }
    } else {
        col = natural + 2;
        if (col < col_min) col = col_min;
        if (col > col_max) col = col_max;
    }
    probe.w = col;
    int words_h = c->doc ? uk_doc_height(c->doc, probe, 0, 0) : 0;
    int body_h = words_h > pic ? words_h : pic;
    int w = 2 * inset + (pic ? pic + inset : 0) + col;
    if (w < label_w) w = label_w;
    // Room for the whole title and its right-hand words.
    int title_w = (c->title ? bfont_text_width(c->title) : 0) + (c->right && c->right[0] ? bfont_text_width(c->right) + 4 * BFONT_GLYPH_W : 0) + 2 * ML_PAD;
    if (w < title_w) w = title_w;
    if (w < c->min_w) w = c->min_w;
    if (w > max_w) w = max_w;
    int extra_block = c->extra_h ? gap + c->extra_h + inset : 0;
    int h = head + 2 * inset + body_h + extra_block + rows_h;
    if (h > a.h - 2 * ml_space()) h = a.h - 2 * ml_space();
    int x = a.x + (a.w - w) / 2;
    int y = c->at_foot ? a.y + a.h - ml_space() - h : a.y + (a.h - h) / 2;
    if (!c->no_dim && !c->at_foot) uk_dim();
    uk_panel(x, y, w, h);
    int top = head ? uk_title(x, y, w, c->title, c->right, PAL_CLR(YELLOW)) : y;
    int bx = x + inset, by = top + inset;
    if (pic) uk_picture(c->face, bx, by, pic, pic);
    int cx = bx + (pic ? pic + inset : 0), cw = x + w - inset - cx;
    if (c->doc) {
        ML_Rect area = { cx, by, cw, (y + h) - rows_h - inset - by };
        uk_doc_draw(c->doc, area, 0, 0, -1, true);
    }
    int ey = by + body_h + gap;
    if (out) {
        out->card = (ML_Rect){ x, y, w, h };
        out->extra = (ML_Rect){ bx, ey, w - 2 * inset, c->extra_h };
    }
    if (c->n_answers > 0) {
        int ry = y + h - ml_list_height(c->n_answers);
        lattice_band_h(x, ry - UK_BAND, w, UK_BAND);
        CardRowsCtx rc = { c };
        ml_list_draw_ex(x, ry, w, ml_list_height(c->n_answers), c->n_answers, c->cursor, card_row_fn, &rc,
                        c->touch_list, uk_ink(), c->touch_base);
    }
}
