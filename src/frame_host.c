#include "frame_host.h"
#include "gfx.h"
#include "touch.h"
#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

// Thin pass-through wrappers around raylib time/window calls.

double frame_host_time(void) { return GetTime(); }

bool frame_host_should_close(void) {
#if defined(__EMSCRIPTEN__)
    // A browser tab has no close signal to poll -- raylib's web backend
    // hardcodes WindowShouldClose() to return false. We do NOT call it,
    // because its implementation is an emscripten_sleep(12), and a yield
    // HERE (before the frame's input poll) is exactly what silently eats
    // keypresses: any key the browser delivers during that sleep has its
    // IsKeyPressed edge wiped by the PollInputEvents inside the following
    // EndDrawing, before any code gets to read it.
    //
    // The single yield per frame lives in frame_host_end_frame(), after
    // the poll. See the header for the full ordering rule.
    return false;
#else
    return WindowShouldClose();
#endif
}

void frame_host_yield(void) {
#if defined(__EMSCRIPTEN__)
    // Unwind the wasm stack so the browser can run its event callbacks and
    // deliver this frame's input. The delay doubles as frame pacing: it is
    // real idle, whereas raylib's WaitTime() busy-waits under Emscripten
    // (it calls nanosleep, which cannot block the main thread). 10ms is in
    // the same ballpark as the 12ms raylib itself used.
    emscripten_sleep(10);
#endif
}

void frame_host_end_frame(void) {
    gfx_frame_end();
    frame_host_yield();
    // Touch runs here so it exists in every loop that reads input: the
    // pointer state is fresh (post-poll, post-yield), the frame just drawn
    // has registered its regions, and any key injected now is a clean edge
    // for the read at the top of the next loop iteration.
    touch_frame();
}

// ---- the window -----------------------------------------------------------
//
// Thin forwards, same as the time calls above. The flags are set here rather
// than by the caller so every platform's window is opened the same way:
// resizable with MSAA hinted, no cursor (this game has no mouse support at
// all -- a tap is a touch contact), no exit key (Escape is the game's own
// back, and raylib would otherwise close the window on it), and a 60fps cap.

// The size the game last gave the window (opening or placing it); 0: none.
static int s_set_w, s_set_h;

bool frame_host_window_at_set_size(void) {
    return s_set_w > 0 && GetScreenWidth() == s_set_w && GetScreenHeight() == s_set_h;
}

void frame_host_window_open(int w, int h, const char *title) {
#if defined(PLATFORM_ANDROID)
    // Two things differ on a phone, and both of them showed up as a smeared,
    // off-centre frame.
    //
    // No MSAA. The game is pixel art blitted at a whole-number scale, so
    // multisampling has nothing to smooth but the edges we want hard -- and
    // asking for it costs the whole app: raylib turns the hint into an EGL
    // request for four samples, and a device (the Android emulator, for one)
    // that offers no such configuration matches nothing at all, so the GL
    // context is never created and every frame goes nowhere.
    //
    // 0 x 0 means "the display". Give raylib a size of our own and it treats
    // it as a virtual screen: it keeps drawing at the declared size and stretches the
    // result to the display through a non-integer matrix with a bilinear
    // filter, while GetScreenWidth() keeps reporting 800 -- so present.c
    // cannot see the real screen either, and its own whole-number scale is
    // computed against a fiction. At 0x0 raylib's screen IS the display,
    // there is no matrix, and the scaling is ours alone.
    (void)w; (void)h;
    InitWindow(0, 0, title);
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(w, h, title);
    s_set_w = w; s_set_h = h;
#endif
    HideCursor();
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
}

void frame_host_window_close(void) { CloseWindow(); }

void frame_host_window_min_size(int w, int h) { SetWindowMinSize(w, h); }
void frame_host_window_size_set(int w, int h) { SetWindowSize(w, h); }

int  frame_host_window_width(void)  { return GetScreenWidth(); }
int  frame_host_window_height(void) { return GetScreenHeight(); }

bool frame_host_window_fullscreen(void) { return IsWindowFullscreen(); }
bool frame_host_window_maximized(void)  { return IsWindowMaximized(); }
void frame_host_window_fullscreen_toggle(void) { ToggleFullscreen(); }

void frame_host_display_size(int *w, int *h) {
    int mon = GetCurrentMonitor();
    if (w) *w = GetMonitorWidth(mon);
    if (h) *h = GetMonitorHeight(mon);
}

void frame_host_poll_events(void) { PollInputEvents(); }

double frame_host_delta(void) { return (double)GetFrameTime(); }

// The shell reports its own conditions, so raylib's running commentary is
// turned down to errors -- except on Android, where logcat is the ONLY channel
// out of the app (stdout goes nowhere) and raylib reports things like a failed
// EGL context at WARNING. Losing those leaves a black screen with no
// explanation, which is exactly what it did.
void frame_host_quiet_log(void) {
#if defined(PLATFORM_ANDROID)
    SetTraceLogLevel(LOG_WARNING);
#else
    SetTraceLogLevel(LOG_ERROR);
#endif
}
