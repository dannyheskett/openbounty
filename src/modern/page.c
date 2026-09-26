// src/modern/page.c -- the page engine (see page.h).

#include "modern/page.h"
#include "gfx.h"
#include "lattice.h"
#include "layout.h"
#include "palette.h"
#include "resources.h"
#include "bfont.h"
#include "ui.h"
#include "touch.h"
#include "uitouch.h"
#include "overlay.h"
#include "input_host.h"
#include "combat.h"      // COMBAT_W x COMBAT_H: the field's cells
#include <stdio.h>
#include <string.h>

#define GW BFONT_GLYPH_W
#define GH BFONT_GLYPH_H

#define PAGE_DEPTH 4

static struct {
    Page    stack[PAGE_DEPTH];
    int     n;
    bool    bare;
    bool    has_field;
    ML_Rect field;
} S;

// ---- the sizes ---------------------------------------------------------------------

// The declared screen: the smallest one the pack is drawn on.
static int native_w(void) { return g_layout.native_w > 0 ? g_layout.native_w : CL_SCREEN_W; }
static int native_h(void) { return g_layout.native_h > 0 ? g_layout.native_h : CL_SCREEN_H; }

int page_ring(void)   { return CL_FRAME_LEFT_W; }
int page_full_w(void) { return native_w() - CL_FRAME_LEFT_W - CL_FRAME_RIGHT_W; }
int page_full_h(void) { return native_h() - CL_FRAME_TOP_H - CL_FRAME_BOTTOM_H; }
int page_menu_w(void) { return page_full_w() - 4 * page_ring(); }
int page_menu_h(void) {
    // Tall enough for PAGE_MENU_ROWS rows and the exit at this device's row
    // height, never shorter than a full page less a ring and a gap on every
    // side, and never so tall that it cannot float.
    int need = uk_title_h() + UK_BAND + 2 * UK_INSET + 2 * uk_line_h() + UK_BAND
             + ml_list_height(PAGE_MENU_ROWS) - ML_ROW_RULE + UK_BAND + ml_list_height(1);
    int h = page_full_h() - 4 * page_ring();
    if (need > h) h = need;
    int cap = page_interior().h - 4 * page_ring();
    return h < cap ? h : cap;
}
int page_msg_w(void) {
    int map_w = native_w() - 2 * (CL_FRAME_LEFT_W + CL_RAIL_W + CL_SIDEBAR_GAP);
    return map_w - 4 * page_ring();
}

// ---- the engine ------------------------------------------------------------------

void page_frame_begin(void) {
    S.n = 0;
    S.bare = false;
    S.has_field = false;
}

void page_bare(void)             { S.bare = true; }
bool page_is_bare(void)          { return S.bare; }

int page_stack(Page *out, int cap) {
    int n = S.n < cap ? S.n : cap;
    for (int i = 0; i < n; i++) out[i] = S.stack[i];
    return n;
}

bool page_field(ML_Rect *out) {
    if (S.has_field && out) *out = S.field;
    return S.has_field;
}

ML_Rect page_interior(void) {
    if (S.bare) return (ML_Rect){ 0, 0, CL_SCREEN_W, CL_SCREEN_H };
    return (ML_Rect){ CL_FRAME_LEFT_W, CL_FRAME_TOP_H,
                      CL_SCREEN_W - CL_FRAME_LEFT_W - CL_FRAME_RIGHT_W,
                      CL_SCREEN_H - CL_FRAME_TOP_H - CL_FRAME_BOTTOM_H };
}

// A page of w x h fits `a` with its ring and a gap on every side.
static bool fits(ML_Rect a, int w, int h) {
    return a.w >= w + 4 * page_ring() && a.h >= h + 4 * page_ring();
}

bool page_full_floats(void) { return fits(page_interior(), page_full_w(), page_full_h()); }

// The pack's dim (render.dim; 0: none), once, over what is behind the page.
static void dim(ML_Rect r) {
    const Resources *res = resources_current();
    int a = overlay_dim_alpha(res ? res->render.dim : 0);
    if (a > 0) gfx_rect(r.x, r.y, r.w, r.h, (Color){ 0, 0, 0, (unsigned char)a });
}

// Open a page w x h. A page on a foot, and a page that fits, floats: centred
// on its area, or standing on its foot, in its ring. A centred page that does
// not fit fills everything inside the frame. `modal`: nothing registered
// under the page takes a tap (the bridge's message lets the map round it
// take them).
static Page open_page(int w, int h, PageAnchor anchor, int tap_key, int exit_key,
                      bool dims, bool modal) {
    const ML_Rect whole = { 0, 0, CL_SCREEN_W, CL_SCREEN_H };
    const int ring = page_ring(), room = 2 * ring;
    ML_Rect in = page_interior();
    // What the page is over: the page under it, or the base screen.
    ML_Rect behind = S.n > 0 ? S.stack[S.n - 1].outer : whole;
    ML_Rect area = in;
    bool foot = false, on_field = false;
    if (anchor == PAGE_MAP_FOOT) {
        // The map runs the interior's full height, centred between the
        // columns: its foot is the interior's. Bare, the screen's foot.
        foot = true;
    } else if (anchor == PAGE_FIELD_FOOT && S.has_field) {
        area = S.field;
        foot = true;
    } else if (anchor == PAGE_CENTER && S.has_field) {
        // Over a fight a page floats centred on the battlefield, not the
        // frame: the field sits beside the column, off the frame's centre.
        // Kept inside the frame with its ring and a gap.
        on_field = true;
    }
    Page p;
    memset(&p, 0, sizeof p);
    p.anchor = foot ? anchor : PAGE_CENTER;
    p.tap_key = tap_key;
    p.exit_key = exit_key;
    p.floats = foot || fits(in, w, h);
    if (p.floats) {
        int mw = area.w - 2 * room, mh = area.h - 2 * room;
        if (w > mw) w = mw;
        if (h > mh) h = mh;
        int x = area.x + (area.w - w) / 2;
        int y = foot ? area.y + area.h - room - h : area.y + (area.h - h) / 2;
        if (on_field) {
            x = S.field.x + (S.field.w - w) / 2;
            y = S.field.y + (S.field.h - h) / 2;
            if (x > in.x + in.w - room - w) x = in.x + in.w - room - w;
            if (y > in.y + in.h - room - h) y = in.y + in.h - room - h;
            if (x < in.x + room) x = in.x + room;
            if (y < in.y + room) y = in.y + room;
        }
        p.r = (ML_Rect){ x, y, w, h };
        p.outer = (ML_Rect){ x - ring, y - ring, w + 2 * ring, h + 2 * ring };
        if (dims) dim(behind);
        gfx_rect(x, y, w, h, uk_fill());
        lattice_ring(p.outer.x, p.outer.y, p.outer.w, p.outer.h, ring, ring, ring, ring);
    } else {
        if (w > in.w) w = in.w;
        if (h > in.h) h = in.h;
        p.r = (ML_Rect){ in.x + (in.w - w) / 2, in.y + (in.h - h) / 2, w, h };
        p.outer = in;
        gfx_rect(in.x, in.y, in.w, in.h, uk_fill());
    }
    // Its taps (touch.c): a single action on a tap anywhere; else nothing
    // inside, and the exit outside -- on the dimmed screen round a floating
    // page, on the frame round one that fills.
    if (tap_key) touch_page(whole.x, whole.y, whole.w, whole.h, tap_key, tap_key, modal);
    else         touch_page(p.outer.x, p.outer.y, p.outer.w, p.outer.h, 0, exit_key, modal);
    if (S.n < PAGE_DEPTH) S.stack[S.n++] = p;
    return p;
}

static const char *close_label(void) {
    const Resources *res = resources_current();
    return res ? res->ui.gm_close : "Close";
}

// ---- messages and questions ----------------------------------------------------

// The title wrapped to `w`, at most three lines (a title may carry its own
// newlines: the engine's artifact message).
#define TITLE_LINES 3
static int title_lines(const char *title, int w, char out[][200]) {
    if (!title || !title[0]) return 0;
    const char *p = title;
    char tmp[200];
    int n = 0;
    while (n < TITLE_LINES && *p && bfont_take_line(&p, w, tmp, (int)sizeof tmp) > 0) {
        if (out) snprintf(out[n], 200, "%s", tmp);
        n++;
    }
    return n;
}

int page_message_text_w(bool face) {
    return page_msg_w() - 2 * UK_INSET - (face ? CL_TILE_W + UK_INSET : 0);
}

int page_message_pages(const char *body, bool face) {
    int n = uk_lines(body, page_message_text_w(face));
    int pages = (n + PAGE_MSG_LINES - 1) / PAGE_MSG_LINES;
    return pages < 1 ? 1 : pages;
}

// The one message box: a picture at the left at 1x (none: none), the gold
// title and the white words beside it, a band, then the answers as full-width
// rows along the foot. As tall as what it holds.
static ML_Rect ask(const char *title, const char *const lines[], int n_lines, int n_rows, int cursor,
                   MlRowFn fn, void *ctx, int touch_list, Texture2D face, PageAnchor anchor,
                   int tap_key, int exit_key, bool modal, int extra_h) {
    const int lh = uk_line_h();
    const int w = page_msg_w();
    const int face_w = face.id ? CL_TILE_W + UK_INSET : 0;
    char tl[TITLE_LINES][200];
    int n_title = title_lines(title, w - 2 * UK_INSET - face_w, tl);
    int text_h = (n_title + n_lines) * lh;
    if (text_h < lh) text_h = lh;
    if (face.id && text_h < CL_TILE_H) text_h = CL_TILE_H;
    int rows_h = n_rows > 0 ? UK_BAND + ml_list_height(n_rows) : 0;
    int h = 2 * UK_INSET + text_h + extra_h + rows_h;
    Page p = open_page(w, h, anchor, tap_key, exit_key, true, modal);
    ML_Rect r = p.r;
    int ry = r.y + r.h - (n_rows > 0 ? ml_list_height(n_rows) : 0);
    int text_floor = (n_rows > 0 ? ry - UK_BAND - UK_INSET : r.y + r.h - UK_INSET) - extra_h;
    if (face.id) uk_picture(face, r.x + UK_INSET, r.y + UK_INSET, CL_TILE_W, CL_TILE_H);
    int tx = r.x + UK_INSET + face_w, tw = r.x + r.w - UK_INSET - tx;
    int ty = r.y + UK_INSET;
    for (int i = 0; i < n_title && ty + GH <= text_floor; i++, ty += lh)
        uk_line(tl[i], tx, ty, tw, PAL_CLR(YELLOW));
    for (int i = 0; i < n_lines && ty + GH <= text_floor; i++, ty += lh)
        uk_line(lines[i] ? lines[i] : "", tx, ty, tw, PAL_CLR(WHITE));
    if (n_rows > 0) {
        lattice_band_h(r.x, ry - UK_BAND, r.w, UK_BAND);
        ml_list_draw(r.x, ry, r.w, ml_list_height(n_rows), n_rows, cursor, fn, ctx, touch_list, uk_ink());
    }
    return (ML_Rect){ tx, text_floor + UK_INSET, tw, extra_h - UK_INSET };
}

void page_message(const char *title, const char *body, int page, const char *row,
                  Texture2D face, PageAnchor anchor) {
    int text_w = page_message_text_w(face.id != 0);
    const char *p = body ? body : "";
    char line[200];
    for (int i = 0; i < page * PAGE_MSG_LINES && *p; i++)
        if (bfont_take_line(&p, text_w, line, (int)sizeof line) <= 0) break;
    static char page_lines[PAGE_MSG_LINES][200];
    const char *lines[PAGE_MSG_LINES];
    int n = 0;
    while (n < PAGE_MSG_LINES && *p &&
           bfont_take_line(&p, text_w, page_lines[n], (int)sizeof page_lines[n]) > 0) {
        lines[n] = page_lines[n];
        n++;
    }
    // More pages to come: the last line says so.
    if (*p && n > 0) uk_mark_cut(page_lines[n - 1], sizeof page_lines[0], text_w);
    // Continue is the message's one action: a tap anywhere, or any key, is
    // it; Escape too, so it is the row Escape presses. With no row (the
    // bridge) the map round the message takes its taps.
    UkRows cont = { { row ? row : "" }, { true }, 1 };
    ask(title, lines, n, row ? 1 : 0, 0, uk_rows_fn, &cont, 0, face, anchor,
        row ? KEY_ENTER : 0, 0, row != NULL, 0);
}

void page_question(const char *title, const char *words, int cursor, MlRowFn fn, void *ctx,
                   int touch_list, Texture2D face, PageAnchor anchor) {
    int tw = page_message_text_w(face.id != 0);
    static char wl[12][200];
    const char *lines[12];
    int nl = 0;
    const char *q = words ? words : "";
    while (nl < 12 && *q && bfont_take_line(&q, tw, wl[nl], (int)sizeof wl[nl]) > 0) {
        lines[nl] = wl[nl];
        nl++;
    }
    if (*q && nl > 0) uk_mark_cut(wl[nl - 1], sizeof wl[0], tw);
    // A question with a title but no other words: the title is the question,
    // so it reads as white words, not a gold title.
    if (nl == 0 && title && title[0]) { lines[0] = title; nl = 1; title = NULL; }
    ask(title, lines, nl, 2, cursor, fn, ctx, touch_list, face, anchor, 0, KEY_ESCAPE, true, 0);
}

// ---- places ----------------------------------------------------------------------

// The rows a place's room holds above its exit without scrolling.
#define PAGE_PLACE_ROWS 3

PagePlace page_place(const char *title, const char *right, Texture2D bd, int n_rows) {
    Page p = open_page(page_full_w(), page_full_h(), PAGE_CENTER, n_rows == 1 ? KEY_ENTER : 0,
                       KEY_ESCAPE, true, true);
    ML_Rect r = p.r;
    int top = uk_title(r.x, r.y, r.w, title, right, NULL);
    PagePlace P;
    // The band is two tiles, top-trimmed where the rows need the room: a
    // touch device's taller rows keep PAGE_PLACE_ROWS rows and the exit under
    // it. Fixed for the session, as the rows are.
    int rows_h = ml_list_height(PAGE_PLACE_ROWS) - ML_ROW_RULE + UK_BAND + ml_list_height(1);
    int band_h = r.y + r.h - top - UK_BAND - rows_h;
    if (band_h > 2 * CL_TILE_H) band_h = 2 * CL_TILE_H;
    P.band = uk_scene_band(r, top, bd, band_h);
    int by = P.band.scene.y + P.band.scene.h + UK_BAND;
    int bh = r.y + r.h - by;
    int lw = 16 * GW + 2 * UK_INSET;
    P.rows = (ML_Rect){ r.x, by, lw, bh };
    lattice_band_v(r.x + lw, by, UK_BAND, bh);
    int wx = r.x + lw + UK_BAND + UK_INSET;
    P.words = (ML_Rect){ wx, by + UK_INSET, r.x + r.w - UK_INSET - wx, bh - 2 * UK_INSET };
    return P;
}

// The scene note's room without drawing it: where the words go and how many
// lines fit, so the pager and the drawer agree.
typedef struct { ML_Rect r; int top, scale, band, words_x, words_w, per; } SceneGeom;
static SceneGeom scene_geom(void) {
    SceneGeom G;
    memset(&G, 0, sizeof G);
    ML_Rect in = page_interior();
    int w = page_full_w(), h = page_full_h();
    const int room = 2 * page_ring();
    if (fits(in, w, h)) {
        int mw = in.w - 2 * room, mh = in.h - 2 * room;
        if (w > mw) w = mw;
        if (h > mh) h = mh;
        G.r = (ML_Rect){ in.x + (in.w - w) / 2, in.y + (in.h - h) / 2, w, h };
    } else {
        if (w > in.w) w = in.w;
        if (h > in.h) h = in.h;
        G.r = (ML_Rect){ in.x + (in.w - w) / 2, in.y + (in.h - h) / 2, w, h };
    }
    G.top = G.r.y + uk_title_h() + UK_BAND;
    G.scale = G.r.w / ML_BACKDROP_W;
    if (G.scale > 3) G.scale = 3;
    if (G.scale < 1) G.scale = 1;
    G.band = ML_BACKDROP_H * G.scale;
    int lw = 16 * GW + 2 * UK_INSET;
    G.words_x = G.r.x + lw + UK_BAND + UK_INSET;
    G.words_w = G.r.x + G.r.w - UK_INSET - G.words_x;
    int words_h = G.r.y + G.r.h - (G.top + G.band + UK_BAND) - 2 * UK_INSET;
    G.per = words_h / uk_line_h();
    if (G.per < 1) G.per = 1;
    return G;
}

int page_scene_pages(const char *words) {
    SceneGeom G = scene_geom();
    int n = uk_lines(words, G.words_w);
    int pages = (n + G.per - 1) / G.per;
    return pages < 1 ? 1 : pages;
}

PagePlace page_scene(const char *title, const char *right, Texture2D scene, const char *words, int page) {
    SceneGeom G = scene_geom();
    Page p = open_page(page_full_w(), page_full_h(), PAGE_CENTER, KEY_ENTER, KEY_ESCAPE, true, true);
    ML_Rect r = p.r;
    int top = uk_title(r.x, r.y, r.w, title, right, NULL);
    PagePlace P;
    P.band = uk_scene_band_at(r, top, scene, G.band, G.scale);
    int by = P.band.scene.y + P.band.scene.h + UK_BAND;
    int bh = r.y + r.h - by;
    int lw = 16 * GW + 2 * UK_INSET;
    P.rows = (ML_Rect){ r.x, by, lw, bh };
    lattice_band_v(r.x + lw, by, UK_BAND, bh);
    int wx = r.x + lw + UK_BAND + UK_INSET;
    P.words = (ML_Rect){ wx, by + UK_INSET, r.x + r.w - UK_INSET - wx, bh - 2 * UK_INSET };
    // The page's lines: skip the pages before, draw this one, and end its
    // last line with ".." when more follow.
    const int lh = uk_line_h();
    const char *q = words ? words : "";
    char line[200];
    for (int i = 0; i < page * G.per && *q; i++)
        if (bfont_take_line(&q, P.words.w, line, (int)sizeof line) <= 0) break;
    int y = P.words.y;
    for (int i = 0; i < G.per && *q; i++, y += lh) {
        if (bfont_take_line(&q, P.words.w, line, (int)sizeof line) <= 0) break;
        if (*q && i + 1 == G.per) uk_mark_cut(line, sizeof line, P.words.w);
        uk_line(line, P.words.x, y, P.words.w, PAL_CLR(WHITE));
    }
    return P;
}

PagePlace page_person(const char *title, const char *right, int n_rows) {
    Page p = open_page(page_full_w(), page_full_h(), PAGE_CENTER, n_rows == 1 ? KEY_ENTER : 0,
                       KEY_ESCAPE, true, true);
    ML_Rect r = p.r;
    int top = uk_title(r.x, r.y, r.w, title, right, NULL);
    PagePlace P;
    memset(&P, 0, sizeof P);
    int lw = 16 * GW + 2 * UK_INSET;
    P.rows = (ML_Rect){ r.x, top, lw, r.y + r.h - top };
    lattice_band_v(r.x + lw, top, UK_BAND, P.rows.h);
    int wx = r.x + lw + UK_BAND + UK_INSET;
    P.words = (ML_Rect){ wx, top + UK_INSET, r.x + r.w - UK_INSET - wx, r.y + r.h - UK_INSET - (top + UK_INSET) };
    return P;
}

UkScene page_foe(const char *title, const char *right, Texture2D bd) {
    Page p = open_page(page_full_w(), page_full_h(), PAGE_CENTER, 0, KEY_ESCAPE, true, true);
    ML_Rect r = p.r;
    int top = uk_title(r.x, r.y, r.w, title, right, NULL);
    // The band: the troops standing at 1x with a margin of sky over them. On a
    // touch device, where the two rows are taller, the sky gives way -- never
    // the troops -- and the cards close up their gaps.
    int cards_h = CL_TILE_H + PAGE_FOE_CARD_LINES * uk_line_h() + 3 * UK_INSET;
    int band_h = r.y + r.h - top - UK_BAND - cards_h - UK_BAND - ml_list_height(2);
    if (band_h > CL_TILE_H + 2 * UK_INSET) band_h = CL_TILE_H + 2 * UK_INSET;
    if (band_h < CL_TILE_H) band_h = CL_TILE_H;
    UkScene L = uk_scene_band(r, top, bd, band_h);
    L.rows = 2;
    L.rows_y = r.y + r.h - ml_list_height(2);
    L.intro_y = L.scene.y + L.scene.h + UK_BAND;
    L.intro_h = L.rows_y - UK_BAND - L.intro_y;
    lattice_band_h(r.x, L.rows_y - UK_BAND, r.w, UK_BAND);
    return L;
}

// ---- menus -------------------------------------------------------------------------

ML_Rect page_menu_body(const char *title, const char *right, int tap_key) {
    Page p = open_page(page_menu_w(), page_menu_h(), PAGE_CENTER, tap_key, KEY_ESCAPE, true, true);
    int top = uk_title(p.r.x, p.r.y, p.r.w, title, right, NULL);
    return (ML_Rect){ p.r.x, top, p.r.w, p.r.y + p.r.h - top };
}

void page_menu(const GmPage *p, const char *path, const char *right_title, int cursor, int touch_list,
               MlRowFn row_fn, void *row_ctx) {
    // Every menu page is one shape and one size: the path as its title, two
    // lines saying what the row under the cursor does or why it is greyed,
    // then the rows -- from the top, the last `foot` of them on the foot.
    const int lh = uk_line_h();
    ML_Rect b = page_menu_body(path, right_title, p->n == 1 ? KEY_ENTER : 0);
    int y = b.y;
    const int desc_lines = 2;          // page_menu_h counts on two
    int desc_h = 2 * UK_INSET + desc_lines * lh + UK_BAND;
    const char *d = (cursor >= 0 && cursor < p->n) ? p->item[cursor].desc : NULL;
    uk_lines_draw(d, b.x + UK_INSET, y + UK_INSET, b.w - 2 * UK_INSET, desc_lines, PAL_CLR(WHITE));
    y += desc_h;
    lattice_band_h(b.x, y - UK_BAND, b.w, UK_BAND);
    MlRowFn fn = row_fn ? row_fn : gm_row;
    void *ctx = row_fn ? row_ctx : (void *)p;
    ml_rows_draw((ML_Rect){ b.x, y, b.w, b.y + b.h - y }, p->n, p->foot, cursor, fn, ctx, touch_list);
}

void page_title_menu(const char *const *labels, int n, int cursor, int touch_list) {
    int w = 0;
    for (int i = 0; i < n; i++) {
        int tw = bfont_text_width(labels[i]);
        if (tw > w) w = tw;
    }
    w += 2 * UK_INSET + 4 * GW;
    // Nothing is under the title menu to go back to: a tap outside it does
    // nothing, and its exit is its own Exit row.
    Page pg = open_page(w, ml_list_height(n), PAGE_CENTER, 0, 0, true, true);
    UkRows rows = { { 0 }, { false }, 0 };
    for (int i = 0; i < n && i < 8; i++) { rows.label[i] = labels[i]; rows.enabled[i] = true; }
#if !defined(PLATFORM_IOS) && !defined(PLATFORM_ANDROID)
    rows.esc = n;          // Exit: the row Escape presses
#endif
    ml_list_draw(pg.r.x, pg.r.y, pg.r.w, pg.r.h, n, cursor, uk_rows_fn, &rows, touch_list, uk_ink());
}

// ---- pages to read --------------------------------------------------------------

ML_Rect page_full_body(const char *title, const char *right) {
    Page p = open_page(page_full_w(), page_full_h(), PAGE_CENTER, 0, KEY_ESCAPE, true, true);
    int top = uk_title(p.r.x, p.r.y, p.r.w, title, right, NULL);
    return (ML_Rect){ p.r.x, top, p.r.w, p.r.y + p.r.h - top };
}

ML_Rect page_sheet_beside(int pic_w, const char *title, const char *right, ML_Rect *picture) {
    Page p = open_page(page_full_w(), page_full_h(), PAGE_CENTER, KEY_ESCAPE, 0, true, true);
    if (picture) *picture = (ML_Rect){ p.r.x, p.r.y, pic_w, p.r.h };
    int wx = p.r.x + pic_w + UK_BAND;
    lattice_band_v(wx - UK_BAND, p.r.y, UK_BAND, p.r.h);
    int top = uk_title(wx, p.r.y, p.r.x + p.r.w - wx, title, right, close_label());
    return (ML_Rect){ wx, top, p.r.x + p.r.w - wx, p.r.y + p.r.h - top };
}

ML_Rect page_sheet(const char *title, const char *right, int tap_key) {
    Page p = open_page(page_full_w(), page_full_h(), PAGE_CENTER, tap_key, 0, true, true);
    int top = uk_title(p.r.x, p.r.y, p.r.w, title, right,
                       tap_key == KEY_ESCAPE ? close_label() : NULL);
    return (ML_Rect){ p.r.x, top, p.r.w, p.r.y + p.r.h - top };
}

ML_Rect page_sheet_small(const char *title, const char *right) {
    Page p = open_page(page_menu_w(), page_menu_h(), PAGE_CENTER, KEY_ESCAPE, 0, true, true);
    int top = uk_title(p.r.x, p.r.y, p.r.w, title, right, close_label());
    return (ML_Rect){ p.r.x, top, p.r.w, p.r.y + p.r.h - top };
}

Page page_caption(int h, const char *title, const char *back, int tap_key) {
    Page p = open_page(page_msg_w(), h, PAGE_MAP_FOOT, tap_key, 0, false, true);
    uk_title(p.r.x, p.r.y, p.r.w, title, NULL, back);
    return p;
}

// ---- full-bleed art ---------------------------------------------------------------

ML_Rect page_art(Texture2D t, int *scale) {
    int s = ui_fit_scale(t.width, t.height, CL_SCREEN_W, CL_SCREEN_H);
    ML_Rect a = { (CL_SCREEN_W - t.width * s) / 2, (CL_SCREEN_H - t.height * s) / 2,
                  t.width * s, t.height * s };
    if (t.id) ui_blit(t, a.x, a.y, a.w, a.h);
    if (scale) *scale = s;
    return a;
}

void page_art_margins(ML_Rect a) {
    // The screen round the art is the frame's lattice, railed against it.
    int l = a.x, r = CL_SCREEN_W - a.x - a.w, t = a.y, b = CL_SCREEN_H - a.y - a.h;
    if (l > 0 || r > 0 || t > 0 || b > 0)
        lattice_ring(0, 0, CL_SCREEN_W, CL_SCREEN_H, l > 0 ? l : 0, r > 0 ? r : 0,
                     t > 0 ? t : 0, b > 0 ? b : 0);
}

ML_Rect page_status(const char *title, const char *words) {
    int tw = page_message_text_w(false);
    static char wl[12][200];
    const char *lines[12];
    int nl = 0;
    const char *q = words ? words : "";
    while (nl < 12 && *q && bfont_take_line(&q, tw, wl[nl], (int)sizeof wl[nl]) > 0) {
        lines[nl] = wl[nl];
        nl++;
    }
    const int bar_h = GH;
    ML_Rect bar = ask(title, lines, nl, 0, 0, NULL, NULL, 0, (Texture2D){ 0 }, PAGE_MAP_FOOT,
                      0, 0, true, bar_h + UK_INSET);
    return bar;
}

// ---- the battle -----------------------------------------------------------------

PageCombat page_combat(bool siege) {
    const int field_w = COMBAT_W * CL_TILE_W, field_h = COMBAT_H * CL_TILE_H;
    PageCombat c;
    memset(&c, 0, sizeof c);
    // The castle's back wall shows where the map has a tile's more height.
    c.has_wall = siege && CL_MAP_H >= field_h + CL_TILE_H;
    int wall_h = c.has_wall ? CL_TILE_H : 0;
    // One column, two tiles wide, against the right frame. The field has the
    // interior left of it: centred when there is room for a band and ground
    // either side, flush with the left frame otherwise (the smallest screen),
    // when the band before the column takes what little is left.
    int col_w  = 2 * CL_TILE_W;
    c.column   = (ML_Rect){ CL_SCREEN_W - CL_FRAME_RIGHT_W - col_w, CL_MAP_Y, col_w, CL_MAP_H };
    int pane_x = CL_FRAME_LEFT_W;
    int pane_w = c.column.x - CL_SIDEBAR_GAP - pane_x;
    int spare  = pane_w - field_w;
    bool sides = spare >= 2 * (UK_BAND + CL_UI);
    int fx = sides ? pane_x + spare / 2 : pane_x;
    // Down the interior the same rule: the field (with its wall row) is
    // centred when there is room for a band and ground above and below,
    // flush with the top otherwise.
    int fh = field_h + wall_h;
    int spare_h = CL_MAP_H - fh;
    bool middle = spare_h >= 2 * (UK_BAND + CL_UI);
    int fy = middle ? CL_MAP_Y + spare_h / 2 : CL_MAP_Y;
    c.wall  = (ML_Rect){ fx, fy, field_w, wall_h };
    c.field = (ML_Rect){ fx, fy + wall_h, field_w, field_h };
    // Round the field the interior is the column's dark ground, and a band
    // marks the field's edge wherever the ground shows.
    bool above = fy > CL_MAP_Y;
    bool below = fy + fh < CL_MAP_Y + CL_MAP_H;
    lattice_ground(pane_x, CL_MAP_Y, pane_w, CL_MAP_H);
    if (sides) {
        int by = fy - (above ? UK_BAND : 0);
        int bh = fh + (above ? UK_BAND : 0) + (below ? UK_BAND : 0);
        lattice_band_v(fx - UK_BAND, by, UK_BAND, bh);
        lattice_band_v(fx + field_w, by, UK_BAND, bh);
        lattice_band_v(c.column.x - CL_SIDEBAR_GAP, CL_MAP_Y, CL_SIDEBAR_GAP, CL_MAP_H);
    } else {
        lattice_band_v(fx + field_w, CL_MAP_Y, c.column.x - (fx + field_w), CL_MAP_H);
    }
    int bx = fx - (sides ? UK_BAND : 0), bw = field_w + (sides ? 2 * UK_BAND : 0);
    if (above) lattice_band_h(bx, fy - UK_BAND, bw, UK_BAND);
    if (below) lattice_band_h(bx, fy + fh, bw, UK_BAND);
    S.field = c.field;
    S.has_field = true;
    return c;
}

// ---- the toast --------------------------------------------------------------------

void page_toast(const char *msg) {
    if (!msg || !msg[0]) return;
    // On the battlefield's top edge in a fight, the map's otherwise.
    const int ring = page_ring(), room = 2 * ring;
    ML_Rect a = S.has_field ? S.field : (ML_Rect){ CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H };
    int max_w = a.w - 2 * room;
    int w = bfont_text_width(msg) + 2 * UK_INSET;
    if (w > max_w) w = max_w;
    int h = GH + 2 * UK_INSET;
    int x = a.x + (a.w - w) / 2;
    int y = a.y + room;
    gfx_rect(x, y, w, h, uk_fill());
    lattice_ring(x - ring, y - ring, w + 2 * ring, h + 2 * ring, ring, ring, ring, ring);
    uk_line(msg, x + UK_INSET, y + UK_INSET, w - 2 * UK_INSET, PAL_CLR(YELLOW));
}
