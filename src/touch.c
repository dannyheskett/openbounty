#include "touch.h"
#include "gfx.h"
#include "input_host.h"
#include "present.h"
#include "layout.h"
#include "frame_host.h"
#include "ob_types.h"
#include "modern/mlayout.h"
#include <stddef.h>

// See touch.h for the frame shape. Everything here is per-frame state:
// regions and chrome requests are registered fresh each frame by whichever
// screen is active, consumed once in touch_frame, and cleared.

#define REGION_MAX 96

typedef enum {
    REGION_SCREEN,   // design-space rect -> key
    REGION_WINDOW,   // window-space rect (chrome button) -> key or char
    REGION_MAP,      // design-space tile viewport -> direction key
    REGION_ROW,      // design-space rect -> (list_id, row) for cursor lists
    REGION_GRID,     // design-space tile grid -> absolute (cx, cy)
    REGION_SCROLL,   // design-space list that a vertical drag scrolls
} RegionKind;

typedef struct {
    RegionKind kind;
    int x, y, w, h;
    bool priority;                // chrome: beats a squarely-hit world region
    int key;                      // REGION_SCREEN / REGION_WINDOW
    bool is_char;                 // REGION_WINDOW: inject as char, not key
    int tile_w, tile_h;           // REGION_MAP: the cell a tap steps from
    int cell_x, cell_y;
    int center_key;
    int list_id, row;             // REGION_ROW
    int step;                     // REGION_SCROLL: pixels of drag per row
} Region;

// Row / cell tapped last touch_frame, readable by this frame's update code.
static int s_tapped_list = 0;
static int s_tapped_row  = -1;
static int s_tapped_grid = 0;
static int s_tapped_cx, s_tapped_cy;

static Region   s_regions[REGION_MAX];
static int      s_region_count;

// The regions the last frame registered, kept when touch_frame clears the
// registry so a check (the --gallery's tap check) can ask what a drawn screen
// offered a finger. Nothing in play reads them.
static Region   s_last[REGION_MAX];
static int      s_last_count;
static int      s_any_key;        // touch_region_any, 0 = none
static int      s_last_any_key;

// The page on top (touch_page): what a tap nothing else took does. `first`:
// the regions registered before it -- under it -- are those below this index.
typedef struct { bool set, modal; int x, y, w, h, inside, outside, first; } PageTap;
static PageTap  s_page, s_last_page;
static unsigned s_chrome;         // requested this frame

// Hold-to-repeat over a map viewport. First step on the press edge, then
// repeats while held; each repeat is its own injected keypress.
static bool   s_press_on_map;
static double s_next_repeat;

// Prompt answer bar for this frame: 0 = none, 'y' = yes/no, 'n' = numeric
// (s_prompt_max buttons), 'a' = A/B.
static char s_prompt_bar;
static int  s_prompt_max;

#define REPEAT_FIRST_DELAY 0.35
#define REPEAT_INTERVAL    0.15

static void add_region(Region r) {
    if (s_region_count < REGION_MAX) s_regions[s_region_count++] = r;
}

void touch_region(int x, int y, int w, int h, int key) {
    Region r = { 0 };
    r.kind = REGION_SCREEN;
    r.x = x; r.y = y; r.w = w; r.h = h; r.key = key;
    add_region(r);
}

void touch_region_priority(int x, int y, int w, int h, int key) {
    Region r = { 0 };
    r.kind = REGION_SCREEN;
    r.x = x; r.y = y; r.w = w; r.h = h; r.key = key;
    r.priority = true;
    add_region(r);
}

void touch_region_any(int key) {
    s_any_key = key;
}

void touch_page(int x, int y, int w, int h, int inside_key, int outside_key, bool modal) {
    s_page.set = true;
    s_page.modal = modal;
    s_page.x = x; s_page.y = y; s_page.w = w; s_page.h = h;
    s_page.inside = inside_key;
    s_page.outside = outside_key;
    s_page.first = s_region_count;
}

static bool page_has(const PageTap *p, int sx, int sy) {
    return sx >= p->x && sy >= p->y && sx < p->x + p->w && sy < p->y + p->h;
}

static int page_key(const PageTap *p, int sx, int sy) {
    return page_has(p, sx, sy) ? p->inside : (p->modal ? p->outside : 0);
}

void touch_region_map(int x, int y, int w, int h,
                      int cell_x, int cell_y, int tile_w, int tile_h,
                      int center_key) {
    Region r = { 0 };
    r.kind = REGION_MAP;
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.tile_w = tile_w; r.tile_h = tile_h;
    r.cell_x = cell_x; r.cell_y = cell_y;
    r.center_key = center_key;
    add_region(r);
}

void touch_region_scroll(int x, int y, int w, int h, int step) {
    Region r = { 0 };
    r.kind = REGION_SCROLL;
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.step = step > 0 ? step : 1;
    add_region(r);
}

void touch_region_row(int x, int y, int w, int h, int list_id, int row) {
    Region r = { 0 };
    r.kind = REGION_ROW;
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.list_id = list_id; r.row = row;
    add_region(r);
}

int touch_tapped_row(int list_id) {
    return (s_tapped_list == list_id) ? s_tapped_row : -1;
}

void touch_region_grid(int x, int y, int w, int h,
                       int tile_w, int tile_h, int grid_id) {
    Region r = { 0 };
    r.kind = REGION_GRID;
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.tile_w = tile_w; r.tile_h = tile_h;
    r.list_id = grid_id;
    add_region(r);
}

bool touch_tapped_cell(int grid_id, int *cx, int *cy) {
    if (s_tapped_grid != grid_id) return false;
    if (cx) *cx = s_tapped_cx;
    if (cy) *cy = s_tapped_cy;
    return true;
}

// The window buttons are legacy's (touch_draw_chrome): a modern screen asks
// for none, so a request made on a path both modes share is dropped here.
void touch_request(unsigned chrome) {
    if (CL_IS_MODERN) return;
    s_chrome |= chrome;
}

void touch_request_prompt_yesno(void)      { if (!CL_IS_MODERN) s_prompt_bar = 'y'; }
void touch_request_prompt_ab(void)         { if (!CL_IS_MODERN) s_prompt_bar = 'a'; }
void touch_request_prompt_numeric(int max) {
    if (CL_IS_MODERN) return;
    s_prompt_bar = 'n';
    s_prompt_max = (max < 1) ? 1 : (max > 5) ? 5 : max;
}

// ---- hit testing -----------------------------------------------------------

static bool rect_has(const Region *r, int px, int py) {
    return px >= r->x && py >= r->y && px < r->x + r->w && py < r->y + r->h;
}

// Direction sign pair -> numpad key, the same keys poll_direction reads.
static int direction_key(int dx, int dy) {
    static const int keys[3][3] = {
        { KEY_KP_7, KEY_KP_8, KEY_KP_9 },
        { KEY_KP_4, 0,        KEY_KP_6 },
        { KEY_KP_1, KEY_KP_2, KEY_KP_3 },
    };
    return keys[dy + 1][dx + 1];
}

static int map_region_key(const Region *r, int sx, int sy) {
    int dx = (sx >= r->cell_x + r->tile_w) - (sx < r->cell_x);
    int dy = (sy >= r->cell_y + r->tile_h) - (sy < r->cell_y);
    if (dx == 0 && dy == 0) return r->center_key;
    return direction_key(dx, dy);
}

// How far outside a region a tap still counts, in DESIGN pixels: enough to
// bring anything smaller than a touch unit up to one. A 20px status band on a
// phone is 13pt tall against Apple's 44pt minimum, and no amount of aiming
// fixes that -- but a tap that lands just below it plainly meant it.
//
// Applied as a SECOND pass over the regions, nearest first, so exact hits are
// never stolen from a neighbour and the forgiveness only decides taps that
// would otherwise have hit nothing at all.
static int touch_slack(void) {
    // Half a row in modern, fixed for the session; half a unit in legacy.
    if (CL_IS_MODERN) return ml_row_h() / 2;
    return present_window_len_to_design(touch_unit()) / 2;
}

static int rect_distance(const Region *r, int px, int py) {
    int dx = 0, dy = 0;
    if (px < r->x)              dx = r->x - px;
    else if (px >= r->x + r->w) dx = px - (r->x + r->w) + 1;
    if (py < r->y)              dy = r->y - py;
    else if (py >= r->y + r->h) dy = py - (r->y + r->h) + 1;
    return (dx > dy) ? dx : dy;         // Chebyshev: a square of slack
}

// What a tap at design pixel (sx, sy) reaches, over a set of regions and the
// page on top: the design-space half of a tap, pure, so the frame's own tap
// and a check of the last frame (touch_last_resolve) are one rule.
typedef enum { HIT_NONE = 0, HIT_KEY, HIT_ROW, HIT_GRID, HIT_MAP } HitKind;
typedef struct { HitKind kind; int key, list, row, cx, cy; } Hit;

// A region under the page on top takes no tap: under a modal page, none of
// them; under one that is not modal, none inside its rect.
static bool under_page(const PageTap *page, int i, int sx, int sy) {
    if (!page || !page->set || i >= page->first) return false;
    return page->modal || page_has(page, sx, sy);
}

static Hit resolve(const Region *regs, int n, const PageTap *page, int any_key, int sx, int sy) {
    Hit h = { HIT_NONE, 0, 0, -1, 0, 0 };

    // Chrome first, whatever registered before it: a band grown to a touch
    // unit overlaps the map's top row, and the map is registered earlier in
    // the frame. Only inside the chrome's own rect -- no extra reach.
    for (int i = 0; i < n; i++) {
        const Region *r = &regs[i];
        if (!r->priority || !rect_has(r, sx, sy) || under_page(page, i, sx, sy)) continue;
        h.kind = HIT_KEY; h.key = r->key;
        return h;
    }

    for (int i = 0; i < n; i++) {
        const Region *r = &regs[i];
        if (r->kind == REGION_WINDOW || r->kind == REGION_SCROLL || !rect_has(r, sx, sy)) continue;
        if (under_page(page, i, sx, sy)) continue;
        if (r->kind == REGION_MAP) {
            h.kind = HIT_MAP; h.key = map_region_key(r, sx, sy);
            return h;
        }
        if (r->kind == REGION_ROW) {
            h.kind = HIT_ROW; h.list = r->list_id; h.row = r->row;
            return h;
        }
        if (r->kind == REGION_GRID) {
            h.kind = HIT_GRID; h.list = r->list_id;
            h.cx = (sx - r->x) / r->tile_w;
            h.cy = (sy - r->y) / r->tile_h;
            return h;
        }
        h.kind = HIT_KEY; h.key = r->key;
        return h;
    }

    // Nothing was hit squarely. Take the nearest region within the slack --
    // small targets (the menu band, a narrow row) then behave as though they
    // were a full touch unit, without growing and overlapping each other.
    // With a page up, only its own regions, and only for a tap inside it.
    if (!page || !page->set || !page->modal || page_has(page, sx, sy)) {
        int slack = touch_slack();
        const Region *best = NULL;
        int best_d = slack + 1;
        for (int i = 0; i < n; i++) {
            const Region *r = &regs[i];
            if (r->kind != REGION_SCREEN && r->kind != REGION_ROW) continue;
            if (under_page(page, i, sx, sy)) continue;
            int d = rect_distance(r, sx, sy);
            if (d < best_d) { best_d = d; best = r; }
        }
        if (best) {
            if (best->kind == REGION_ROW) { h.kind = HIT_ROW; h.list = best->list_id; h.row = best->row; }
            else                          { h.kind = HIT_KEY; h.key = best->key; }
            return h;
        }
    }

    // A page is up: the tap is the page's -- inside it, nothing or its one
    // action; outside a floating page, its exit.
    if (page && page->set) {
        int key = page_key(page, sx, sy);
        if (key) { h.kind = HIT_KEY; h.key = key; }
        return h;
    }

    if (any_key) { h.kind = HIT_KEY; h.key = any_key; }
    return h;
}

// Resolve a tap at window position (wx,wy). Chrome buttons sit on top, then
// design-space regions, then the page, then the any-key fallback. `*was_map`
// reports a map viewport hit so the caller can arm hold-to-repeat.
static void resolve_tap(int wx, int wy, bool *was_map) {
    *was_map = false;

    for (int i = 0; i < s_region_count; i++) {
        const Region *r = &s_regions[i];
        if (r->kind != REGION_WINDOW || !rect_has(r, wx, wy)) continue;
        if (r->is_char) input_host_inject_char(r->key);
        else            input_host_inject_key(r->key);
        return;
    }

    int sx, sy;
    if (!present_window_to_screen(wx, wy, &sx, &sy)) return;

    Hit h = resolve(s_regions, s_region_count, &s_page, s_any_key, sx, sy);
    switch (h.kind) {
    case HIT_MAP:
        if (h.key) { input_host_inject_key(h.key); *was_map = true; }
        break;
    case HIT_ROW:
        s_tapped_list = h.list;
        s_tapped_row  = h.row;
        break;
    case HIT_GRID:
        s_tapped_grid = h.list;
        s_tapped_cx = h.cx;
        s_tapped_cy = h.cy;
        break;
    case HIT_KEY:
        input_host_inject_key(h.key);
        break;
    case HIT_NONE:
        break;
    }
}

// Drag scrolling: a press inside a scrolling list waits. Moving the finger a
// row's height moves the list one row (the finger up shows later rows, as a
// Down press does); letting go without moving is the tap, resolved then.
#define DRAG_SLOP 10
static bool s_drag;           // a press is being held over a scrolling list
static bool s_drag_moved;
static int  s_drag_wx, s_drag_wy;   // where it went down (window)
static int  s_drag_last_sy;         // screen y at the last step
static int  s_drag_step;

static const Region *scroll_region_at(int sx, int sy) {
    for (int i = 0; i < s_region_count; i++)
        if (s_regions[i].kind == REGION_SCROLL && rect_has(&s_regions[i], sx, sy) &&
            !under_page(&s_page, i, sx, sy))
            return &s_regions[i];
    return NULL;
}

void touch_frame(void) {
    input_host_clear_injected();
    input_touch_sample();
    s_tapped_list = 0;
    s_tapped_row  = -1;
    s_tapped_grid = 0;

    int wx, wy;
    int sx, sy;
    if (s_drag) {
        if (input_touch_down(&wx, &wy)) {
            if (present_window_to_screen(wx, wy, &sx, &sy)) {
                int dy = sy - s_drag_last_sy;
                if (!s_drag_moved && (dy > DRAG_SLOP || dy < -DRAG_SLOP)) s_drag_moved = true;
                if (s_drag_moved && (dy >= s_drag_step || dy <= -s_drag_step)) {
                    input_host_inject_key(dy < 0 ? KEY_DOWN : KEY_UP);   // one row a frame
                    s_drag_last_sy += dy < 0 ? -s_drag_step : s_drag_step;
                }
            }
        } else {
            if (!s_drag_moved) {                       // no movement: it was a tap
                bool on_map;
                resolve_tap(s_drag_wx, s_drag_wy, &on_map);
            }
            s_drag = false;
        }
    } else if (input_touch_pressed(&wx, &wy) && present_window_to_screen(wx, wy, &sx, &sy) &&
               scroll_region_at(sx, sy)) {
        const Region *sr = scroll_region_at(sx, sy);
        s_drag = true;
        s_drag_moved = false;
        s_drag_wx = wx; s_drag_wy = wy;
        s_drag_last_sy = sy;
        s_drag_step = sr->step;
    } else if (input_touch_pressed(&wx, &wy) && present_layout_changed()) {
        // The screen changed under the finger this frame (the window was
        // resized): what it aimed at is not where it was, so the tap is
        // dropped rather than landing on whatever moved there.
        s_press_on_map = false;
    } else if (input_touch_pressed(&wx, &wy)) {
        resolve_tap(wx, wy, &s_press_on_map);
        s_next_repeat = frame_host_time() + REPEAT_FIRST_DELAY;
    } else if (s_press_on_map && input_touch_down(&wx, &wy)) {
        if (frame_host_time() >= s_next_repeat) {
            bool on_map;
            resolve_tap(wx, wy, &on_map);
            if (!on_map) s_press_on_map = false;   // finger drifted off
            s_next_repeat = frame_host_time() + REPEAT_INTERVAL;
        }
    } else if (!input_touch_down(NULL, NULL)) {
        s_press_on_map = false;
    }

    for (int i = 0; i < s_region_count; i++) s_last[i] = s_regions[i];
    s_last_count = s_region_count;
    s_last_page = s_page;
    s_last_any_key = s_any_key;
    s_page.set = false;
    s_region_count = 0;
    s_any_key = 0;
    s_chrome = 0;
    s_prompt_bar = 0;
}

// ---- the last frame's regions ------------------------------------------------

bool touch_last_hit(int sx, int sy, int *list_id, int *row, int *key) {
    // The design-space half of resolve_tap, over the last frame's regions: what
    // a finger at (sx, sy) would reach, the first region registered winning.
    if (list_id) *list_id = 0;
    if (row) *row = -1;
    if (key) *key = 0;
    for (int i = 0; i < s_last_count; i++) {          // chrome first, as resolve_tap does
        const Region *r = &s_last[i];
        if (!r->priority || !rect_has(r, sx, sy)) continue;
        if (key) *key = r->key;
        return true;
    }
    for (int i = 0; i < s_last_count; i++) {
        const Region *r = &s_last[i];
        if (r->kind == REGION_WINDOW || r->kind == REGION_SCROLL || !rect_has(r, sx, sy)) continue;
        if (r->kind == REGION_ROW) {
            if (list_id) *list_id = r->list_id;
            if (row) *row = r->row;
        } else if (r->kind == REGION_GRID) {
            if (list_id) *list_id = r->list_id;
        } else if (r->kind == REGION_MAP) {
            if (key) *key = map_region_key(r, sx, sy);
        } else if (key) {
            *key = r->key;
        }
        return true;
    }
    return false;
}

static bool last_rect(const Region *r, int *x, int *y, int *w, int *h) {
    if (x) *x = r->x;
    if (y) *y = r->y;
    if (w) *w = r->w;
    if (h) *h = r->h;
    return true;
}

bool touch_last_row_rect(int list_id, int row, int *x, int *y, int *w, int *h) {
    for (int i = 0; i < s_last_count; i++)
        if (s_last[i].kind == REGION_ROW && s_last[i].list_id == list_id && s_last[i].row == row)
            return last_rect(&s_last[i], x, y, w, h);
    return false;
}

int touch_last_page_key(int sx, int sy) {
    return s_last_page.set ? page_key(&s_last_page, sx, sy) : -1;
}

bool touch_last_resolve(int sx, int sy, int *list_id, int *row, int *key) {
    Hit h = resolve(s_last, s_last_count, &s_last_page, s_last_any_key, sx, sy);
    if (list_id) *list_id = (h.kind == HIT_ROW || h.kind == HIT_GRID) ? h.list : 0;
    if (row) *row = h.kind == HIT_ROW ? h.row : -1;
    if (key) *key = (h.kind == HIT_KEY || h.kind == HIT_MAP) ? h.key : 0;
    return h.kind != HIT_NONE;
}

bool touch_last_key_rect(int key, int *x, int *y, int *w, int *h) {
    for (int i = 0; i < s_last_count; i++)
        if (s_last[i].kind == REGION_SCREEN && s_last[i].key == key)
            return last_rect(&s_last[i], x, y, w, h);
    return false;
}

// ---- chrome ----------------------------------------------------------------
//
// Buttons draw with raylib's default font in window pixels: this is host
// furniture like the letterbox itself, not pack art, so it does not go
// through bfont or the render target.

typedef struct { const char *label; int key; } Button;

// A touch control's size in WINDOW pixels, from the one number that tracks
// physical size across every device we ship on: the short side of the window.
// Apple asks for 44pt and Android for 48dp; on the phones this runs on that is
// very close to 11% of the short side (iPhone 12: 44/390pt; Pixel 6:
// 44/411dp). The 44px floor keeps a small desktop window sane.
//
// Everything in this file sizes itself from this rather than from a pixel
// count, because a pixel count means something different on every screen --
// which is how the keyboard ended up with 15pt keys.
int touch_unit(void) {
    int w = frame_host_window_width(), h = frame_host_window_height();
    int shortest = (w < h) ? w : h;
    // A tablet's short side is two phones' worth; a finger is not. Modern's
    // unit stops growing at a large phone's short side (1284 device pixels,
    // an iPhone Pro Max: 141 px, 47 pt), or an iPad's rows are twice a
    // finger and its menus cannot show whole.
    if (CL_IS_MODERN && shortest > 1284) shortest = 1284;
    int u = shortest * 11 / 100;
    return (u < 44) ? 44 : u;
}

int touch_unit_design(void) {
    if (CL_IS_MODERN) return ml_row_h();
    int u = present_window_len_to_design(touch_unit());
    return u < 1 ? 1 : u;
}

static void chrome_button(int x, int y, int w, int h,
                          const char *label, int key, bool is_char) {
    gfx_rect(x, y, w, h, (Color){ 36, 36, 44, 230 });
    gfx_rect_lines(x, y, w, h, (Color){ 130, 130, 150, 255 });
    int fs = h / 2 < 10 ? 10 : h / 2;
    int tw = gfx_label_width(label, fs);
    while (tw > w - 6 && fs > 8) { fs -= 2; tw = gfx_label_width(label, fs); }
    gfx_label(label, x + (w - tw) / 2, y + (h - fs) / 2, fs,
              (Color){ 230, 230, 230, 255 });

    Region r = { 0 };
    r.kind = REGION_WINDOW;
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.key = key; r.is_char = is_char;
    add_region(r);
}

// Lay a button list out in rows across the bottom of the window. Prefers
// the letterbox margin below the game; if there is none, overlays the
// bottom edge of the game area instead.
static void chrome_bar(const Button *btns, int count) {
    int win_w = frame_host_window_width(), win_h = frame_host_window_height();
    int gx, gy, gw, gh;
    present_last_dst(&gx, &gy, &gw, &gh);

    int u = touch_unit();
    int bw = u * 3 / 2, bh = u, gap = u / 6;
    int per_row = (win_w - gap) / (bw + gap);
    if (per_row < 1) per_row = 1;
    if (per_row > count) per_row = count;
    int rows = (count + per_row - 1) / per_row;

    int bar_h = rows * (bh + gap) + gap;
    int margin_bottom = win_h - (gy + gh);
    int y0 = (margin_bottom >= bar_h) ? gy + gh + gap
                                      : win_h - bar_h + gap;

    for (int i = 0; i < count; i++) {
        int row = i / per_row;
        int in_row = (row == rows - 1) ? count - row * per_row : per_row;
        int row_w = in_row * (bw + gap) - gap;
        int x0 = (win_w - row_w) / 2;
        int col = i - row * per_row;
        chrome_button(x0 + col * (bw + gap), y0 + row * (bh + gap),
                      bw, bh, btns[i].label, btns[i].key, false);
    }
}

static void chrome_corner(void) {
    int win_w = frame_host_window_width();
    int u = touch_unit();
    int bw = u * 3 / 2, gap = u / 6;
    int x = win_w - bw - gap, y = gap;
    if (s_chrome & TOUCH_CHROME_BACK) {
        chrome_button(x, y, bw, u, "ESC", KEY_ESCAPE, false);
        x -= bw + gap;
    }
    if (s_chrome & TOUCH_CHROME_CONFIRM)
        chrome_button(x, y, bw, u, "OK", KEY_ENTER, false);
}

static void chrome_digits(void) {
    static const char *labels[4][3] = {
        { "7", "8", "9" }, { "4", "5", "6" },
        { "1", "2", "3" }, { "<", "0", "OK" },
    };
    int bw = touch_unit(), gap = bw / 6;
    int win_w = frame_host_window_width(), win_h = frame_host_window_height();
    int x0 = win_w - 3 * (bw + gap) - gap;
    int y0 = win_h - 4 * (bw + gap) - gap;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            const char *l = labels[r][c];
            int key;
            if      (l[0] == '<')  key = KEY_BACKSPACE;
            else if (l[0] == 'O')  key = KEY_ENTER;
            else                   key = KEY_ZERO + (l[0] - '0');
            chrome_button(x0 + c * (bw + gap), y0 + r * (bw + gap),
                          bw, bw, l, key, false);
        }
    }
}

static void chrome_keyboard(void) {
    static const char *rows[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    int win_w = frame_host_window_width(), win_h = frame_host_window_height();
    int u = touch_unit();
    int gap = u / 6;
    // Ten keys and eleven gaps across, but never below the touch unit: on a
    // phone in landscape the width is generous and the unit is what decides.
    int bw = (win_w - 11 * gap) / 10;
    if (bw < u) bw = u;
    int bh = u;
    int y0 = win_h - 4 * (bh + gap) - gap;

    char label[2] = { 0, 0 };
    for (int r = 0; r < 3; r++) {
        int n = 0;
        while (rows[r][n]) n++;
        int x0 = (win_w - (n * (bw + gap) - gap)) / 2;
        for (int c = 0; c < n; c++) {
            label[0] = rows[r][c];
            chrome_button(x0 + c * (bw + gap), y0 + r * (bh + gap),
                          bw, bh, label, rows[r][c], true);
        }
    }
    // Space / backspace / enter row.
    int y = y0 + 3 * (bh + gap);
    int sw = 4 * (bw + gap) - gap;
    int x0 = (win_w - (sw + 2 * (2 * bw + gap) + 2 * gap)) / 2;
    chrome_button(x0, y, 2 * bw, bh, "<-", KEY_BACKSPACE, false);
    chrome_button(x0 + 2 * bw + gap, y, sw, bh, "SPACE", ' ', true);
    chrome_button(x0 + 2 * bw + gap + sw + gap, y, 2 * bw, bh,
                  "OK", KEY_ENTER, false);
}

static void chrome_prompt_bar(void) {
    if (s_prompt_bar == 'y') {
        static const Button btns[] = { { "Yes", KEY_Y }, { "No", KEY_N } };
        chrome_bar(btns, 2);
    } else if (s_prompt_bar == 'a') {
        static const Button btns[] = { { "A", KEY_A }, { "B", KEY_B } };
        chrome_bar(btns, 2);
    } else if (s_prompt_bar == 'n') {
        static const Button btns[] = {
            { "1", KEY_ONE },   { "2", KEY_TWO }, { "3", KEY_THREE },
            { "4", KEY_FOUR }, { "5", KEY_FIVE },
        };
        chrome_bar(btns, s_prompt_max);
    }
}

void touch_draw_chrome(void) {
    if ((!s_chrome && !s_prompt_bar) || !input_touch_active()) return;
    // MODERN DRAWS NONE OF THIS. Every one of these is a window-pixel button
    // whose label goes through gfx_label, which is an empty stub on iOS: the
    // bars, the corner and the keyboard were blank boxes on the device.
    // Modern answers a finger with the screens themselves -- the band, the
    // rail, the rows, the letter grid -- all drawn in the buffer with the
    // pack's own font. Legacy keeps every one: it has no such screens.
    if (CL_IS_MODERN) return;

    if (s_chrome & TOUCH_CHROME_COMBAT) {
        static const Button btns[] = {
            { "Wait", KEY_SPACE }, { "Pass", KEY_KP_5 },
            { "Shot", KEY_S },     { "Fly",  KEY_F },
            { "Cast", KEY_U },     { "Give", KEY_G },
            { "Ctrl", KEY_C },     { "Opts", KEY_O },
        };
        chrome_bar(btns, (int)(sizeof btns / sizeof btns[0]));
    }
    if (s_chrome & TOUCH_CHROME_DIGITS)   chrome_digits();
    if (s_chrome & TOUCH_CHROME_KEYBOARD) chrome_keyboard();
    chrome_prompt_bar();
    chrome_corner();
}
