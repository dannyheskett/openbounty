// src/frame_host.h and src/input_host.h for iOS. The raylib versions of both
// (src/frame_host.c, src/input_host.c) are not in this build; everything they
// wrapped is either UIKit state published by ios/plat_ios.mm or simply absent
// on a phone.
//
// What is absent, and why answering "no" is right rather than lazy:
//   * the window -- the OS hands the app a full-screen view, so open/close,
//     min-size, fullscreen and the display query are all no-ops or report the
//     view;
//   * the keyboard -- there is none, so every key read is false EXCEPT the
//     synthetic ones src/touch.c injects, which is exactly how the web and
//     Android builds already behave once a finger has been seen;
//   * the gamepad -- no GameController wiring yet (it is not needed to ship).
//
// The injected-key queue is the real input path on this platform, so it is
// implemented here in full rather than deferred: a tap becomes a keypress and
// every existing screen keeps its key handling.

#include "frame_host.h"
#include "input_host.h"
#include "touch.h"
#include "gfx.h"
#include "plat_ios.h"

#if defined(PLATFORM_IOS)

#include <stdbool.h>
#include <unistd.h>  // usleep, the game thread's pacing

// ---------------------------------------------------------------------------
// frame_host
// ---------------------------------------------------------------------------

double frame_host_time(void)  { return plat_ios_time(); }
double frame_host_delta(void) { return plat_ios_delta(); }

// The OS owns the app's lifetime: there is no close to poll for, and returning
// true would end the game's loops for no reason.
bool frame_host_should_close(void) { return false; }

// The game thread yields here. The display link runs independently on the main
// thread, so this is only pacing: a frame's worth of sleep keeps the thread
// from spinning through its loops faster than anything can be seen.
void frame_host_yield(void) {
    // A coarse frame-length pause. nanosleep's struct timespec and usleep's
    // declaration both sit behind a POSIX feature test that -std=c99 turns
    // off, so the declaration is made here rather than widening the dialect
    // for one call.
    extern int usleep(unsigned int);
    usleep(16 * 1000);   // ~16 ms
}

void frame_host_end_frame(void) {
    gfx_frame_end();
    frame_host_yield();
    touch_frame();
}

void frame_host_window_open(int w, int h, const char *title) {
    (void)w; (void)h; (void)title;   // the view already exists
}
void frame_host_window_close(void) {}
void frame_host_window_min_size(int w, int h) { (void)w; (void)h; }
void frame_host_window_size_set(int w, int h) { (void)w; (void)h; }

int frame_host_window_width(void) {
    int w = 0, h = 0;
    plat_ios_screen(&w, &h);
    return w;
}

int frame_host_window_height(void) {
    int w = 0, h = 0;
    plat_ios_screen(&w, &h);
    return h;
}

// Always fullscreen, never maximised in the desktop sense, and the "display"
// is the view itself.
bool frame_host_window_fullscreen(void) { return true; }
bool frame_host_window_maximized(void)  { return false; }
void frame_host_window_fullscreen_toggle(void) {}

void frame_host_display_size(int *w, int *h) { plat_ios_screen(w, h); }
bool frame_host_window_room(int *w, int *h) { (void)w; (void)h; return false; }
void frame_host_window_place(int w, int h) { (void)w; (void)h; }

// UIKit delivers events on the main thread; the game thread has nothing to
// poll for.
void frame_host_poll_events(void) {}

void frame_host_quiet_log(void) {}

// The engine's recorder callback (engine/include/ui_host.h). The movie
// recorder is a desktop subsystem, but the engine calls this at every step,
// hit and flow trigger, so the symbol has to exist.
void recorder_capture(const char *trigger) { (void)trigger; }

// ---------------------------------------------------------------------------
// input_host
// ---------------------------------------------------------------------------

#define INJECT_MAX 32

static int  s_keys[INJECT_MAX];
static int  s_key_count;
static int  s_key_drain;
static int  s_chars[INJECT_MAX];
static int  s_char_count;
static int  s_char_drain;
static int  s_next_key;
static double s_guard_until;

static bool guarded(void) { return frame_host_time() < s_guard_until; }

void input_host_inject_key(int key) {
    if (key != 0 && s_key_count < INJECT_MAX) s_keys[s_key_count++] = key;
}

void input_host_inject_char(int ch) {
    if (ch != 0 && s_char_count < INJECT_MAX) s_chars[s_char_count++] = ch;
}

void input_host_inject_key_next_frame(int key) { s_next_key = key; }

void input_host_clear_injected(void) {
    s_key_count = s_key_drain = 0;
    s_char_count = s_char_drain = 0;
    if (s_next_key) { input_host_inject_key(s_next_key); s_next_key = 0; }
}

void input_host_flush(double guard_seconds) {
    s_key_count = s_key_drain = 0;
    s_char_count = s_char_drain = 0;
    s_next_key = 0;
    s_guard_until = frame_host_time() + guard_seconds;
}

static bool injected_has(int key) {
    for (int i = 0; i < s_key_count; i++) if (s_keys[i] == key) return true;
    return false;
}

bool input_key_pressed(int key) {
    if (guarded()) return false;
    return injected_has(key);
}

bool input_key_down(int key) { return injected_has(key); }

int input_get_key_pressed(void) {
    if (guarded()) return 0;
    if (s_key_drain < s_key_count) return s_keys[s_key_drain++];
    return 0;
}

int input_get_char_pressed(void) {
    if (guarded()) return 0;
    if (s_char_drain < s_char_count) return s_chars[s_char_drain++];
    return 0;
}

// No pad: every answer is "absent", and input.c's gamepad paths fall away.
bool  input_pad_available(void)     { return false; }
bool  input_pad_pressed(int b)      { (void)b; return false; }
int   input_pad_any_pressed(void)   { return 0; }
float input_pad_axis(int a)         { (void)a; return 0.0f; }
void  input_host_note_gamepad(void) {}

// ---- touch ----------------------------------------------------------------
//
// One contact, published by the view in the game's own pixel space. Sampled
// once a frame like the raylib path, so the press and release edges are this
// frame's contact against the last one's.

static bool s_touch_seen;
static bool s_touch_now, s_touch_prev;
static int  s_touch_x, s_touch_y;

void input_touch_sample(void) {
    s_touch_prev = s_touch_now;
    int x = 0, y = 0;
    s_touch_now = plat_ios_touch(&x, &y);
    if (s_touch_now) {
        s_touch_x = x;
        s_touch_y = y;
        s_touch_seen = true;
    }
}

bool input_touch_pressed(int *x, int *y) {
    if (!(s_touch_now && !s_touch_prev)) return false;
    if (x) *x = s_touch_x;
    if (y) *y = s_touch_y;
    return true;
}

bool input_touch_down(int *x, int *y) {
    if (!s_touch_now) return false;
    if (x) *x = s_touch_x;
    if (y) *y = s_touch_y;
    return true;
}

bool input_touch_released(void) { return s_touch_prev && !s_touch_now; }
bool input_touch_active(void)   { return s_touch_seen; }

// No keyboard, ever: text fields use the in-game letter selector.
bool input_has_keyboard(void) { return false; }
InputTextMode input_text_mode(void) { return TEXT_MODE_SELECTOR; }
bool input_pad_or_touch_seen(void) { return true; }
bool input_touch_device(void) { return true; }
InputDevice input_last_device(void) { return INPUT_DEV_TOUCH; }
// --touch: a phone is a touch device already.
void input_host_force_touch(void) {}

#endif // PLATFORM_IOS
