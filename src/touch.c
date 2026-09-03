#include "touch.h"
#include "input_host.h"
#include "present.h"
#include "frame_host.h"
#include "raylib.h"
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
} RegionKind;

typedef struct {
    RegionKind kind;
    int x, y, w, h;
    int key;                      // REGION_SCREEN / REGION_WINDOW
    bool is_char;                 // REGION_WINDOW: inject as char, not key
    int tile_w, tile_h;           // REGION_MAP
    int center_tx, center_ty;
    int center_key;
    int list_id, row;             // REGION_ROW
} Region;

// Row / cell tapped last touch_frame, readable by this frame's update code.
static int s_tapped_list = 0;
static int s_tapped_row  = -1;
static int s_tapped_grid = 0;
static int s_tapped_cx, s_tapped_cy;

static Region   s_regions[REGION_MAX];
static int      s_region_count;
static int      s_any_key;        // touch_region_any, 0 = none
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

void touch_region_any(int key) {
    s_any_key = key;
}

void touch_region_map(int x, int y, int w, int h,
                      int tile_w, int tile_h,
                      int center_tx, int center_ty, int center_key) {
    Region r = { 0 };
    r.kind = REGION_MAP;
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.tile_w = tile_w; r.tile_h = tile_h;
    r.center_tx = center_tx; r.center_ty = center_ty;
    r.center_key = center_key;
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

void touch_request(unsigned chrome) {
    s_chrome |= chrome;
}

void touch_request_prompt_yesno(void)      { s_prompt_bar = 'y'; }
void touch_request_prompt_ab(void)         { s_prompt_bar = 'a'; }
void touch_request_prompt_numeric(int max) {
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
    int tx = (sx - r->x) / r->tile_w;
    int ty = (sy - r->y) / r->tile_h;
    int dx = (tx > r->center_tx) - (tx < r->center_tx);
    int dy = (ty > r->center_ty) - (ty < r->center_ty);
    if (dx == 0 && dy == 0) return r->center_key;
    return direction_key(dx, dy);
}

// Resolve a tap at window position (wx,wy). Chrome buttons sit on top, then
// design-space regions, then the any-key fallback. `*was_map` reports a map
// viewport hit so the caller can arm hold-to-repeat.
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

    for (int i = 0; i < s_region_count; i++) {
        const Region *r = &s_regions[i];
        if (r->kind == REGION_WINDOW || !rect_has(r, sx, sy)) continue;
        if (r->kind == REGION_MAP) {
            int key = map_region_key(r, sx, sy);
            if (key) { input_host_inject_key(key); *was_map = true; }
            return;
        }
        if (r->kind == REGION_ROW) {
            s_tapped_list = r->list_id;
            s_tapped_row  = r->row;
            return;
        }
        if (r->kind == REGION_GRID) {
            s_tapped_grid = r->list_id;
            s_tapped_cx = (sx - r->x) / r->tile_w;
            s_tapped_cy = (sy - r->y) / r->tile_h;
            return;
        }
        input_host_inject_key(r->key);
        return;
    }

    if (s_any_key) input_host_inject_key(s_any_key);
}

void touch_frame(void) {
    input_host_clear_injected();
    s_tapped_list = 0;
    s_tapped_row  = -1;
    s_tapped_grid = 0;

    int wx, wy;
    if (input_pointer_pressed(&wx, &wy)) {
        resolve_tap(wx, wy, &s_press_on_map);
        s_next_repeat = frame_host_time() + REPEAT_FIRST_DELAY;
    } else if (s_press_on_map && input_pointer_down(&wx, &wy)) {
        if (frame_host_time() >= s_next_repeat) {
            bool on_map;
            resolve_tap(wx, wy, &on_map);
            if (!on_map) s_press_on_map = false;   // finger drifted off
            s_next_repeat = frame_host_time() + REPEAT_INTERVAL;
        }
    } else if (!input_pointer_down(NULL, NULL)) {
        s_press_on_map = false;
    }

    s_region_count = 0;
    s_any_key = 0;
    s_chrome = 0;
    s_prompt_bar = 0;
}

// ---- chrome ----------------------------------------------------------------
//
// Buttons draw with raylib's default font in window pixels: this is host
// furniture like the letterbox itself, not pack art, so it does not go
// through bfont or the render target.

typedef struct { const char *label; int key; } Button;

static void chrome_button(int x, int y, int w, int h,
                          const char *label, int key, bool is_char) {
    DrawRectangle(x, y, w, h, (Color){ 36, 36, 44, 230 });
    DrawRectangleLines(x, y, w, h, (Color){ 130, 130, 150, 255 });
    int fs = h / 2 < 10 ? 10 : h / 2;
    int tw = MeasureText(label, fs);
    while (tw > w - 6 && fs > 8) { fs -= 2; tw = MeasureText(label, fs); }
    DrawText(label, x + (w - tw) / 2, y + (h - fs) / 2, fs,
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
    int win_w = GetScreenWidth(), win_h = GetScreenHeight();
    int gx, gy, gw, gh;
    present_last_dst(&gx, &gy, &gw, &gh);

    int bw = 62, bh = 42, gap = 5;
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
    int win_w = GetScreenWidth();
    int x = win_w - 56 - 6, y = 6;
    if (s_chrome & TOUCH_CHROME_BACK) {
        chrome_button(x, y, 56, 42, "ESC", KEY_ESCAPE, false);
        x -= 56 + 6;
    }
    if (s_chrome & TOUCH_CHROME_CONFIRM)
        chrome_button(x, y, 56, 42, "OK", KEY_ENTER, false);
}

static void chrome_digits(void) {
    static const char *labels[4][3] = {
        { "7", "8", "9" }, { "4", "5", "6" },
        { "1", "2", "3" }, { "<", "0", "OK" },
    };
    int bw = 48, gap = 5;
    int win_w = GetScreenWidth(), win_h = GetScreenHeight();
    int x0 = win_w - 3 * (bw + gap) - 6;
    int y0 = win_h - 4 * (bw + gap) - 6;
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
    int win_w = GetScreenWidth(), win_h = GetScreenHeight();
    int gap = 4;
    int bw = (win_w - 11 * gap) / 10;
    if (bw > 46) bw = 46;
    int bh = bw + 6;
    int y0 = win_h - 4 * (bh + gap) - 4;

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

    if (s_chrome & TOUCH_CHROME_ADVENTURE) {
        static const Button btns[] = {
            { "Srch", KEY_S }, { "Cast", KEY_U }, { "Wait", KEY_W },
            { "Fly",  KEY_F }, { "Land", KEY_L }, { "Army", KEY_A },
            { "Hero", KEY_V }, { "Info", KEY_I }, { "Map",  KEY_M },
            { "Puzl", KEY_P }, { "Ctrl", KEY_C }, { "Save", KEY_Q },
        };
        chrome_bar(btns, (int)(sizeof btns / sizeof btns[0]));
    }
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
