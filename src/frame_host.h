#ifndef OB_FRAME_HOST_H
#define OB_FRAME_HOST_H

#include <stdbool.h>
#include "gfx.h"

// Frame / time host shim. Thin wrappers around the raylib time/window
// calls used by game logic, so callers don't include raylib directly.

double frame_host_time(void);          // GetTime equivalent
double frame_host_delta(void);         // GetFrameTime equivalent
bool   frame_host_should_close(void);  // WindowShouldClose equivalent

// ---- the window -----------------------------------------------------------
//
// Opening and sizing the one window. On a platform that has no window to open
// -- iOS, where the OS hands the app a full-screen view, and Android, where
// raylib's NativeActivity owns it -- the open/size/fullscreen calls are
// accepted and ignored, and the size queries report the view.
//
// Sizes are in WINDOW pixels, which on a high-DPI display are device pixels:
// the game is presented at an integer scale of its buffer, so it needs the
// real count, not a logical one.
void frame_host_window_open(int w, int h, const char *title);
void frame_host_window_close(void);
void frame_host_window_min_size(int w, int h);
void frame_host_window_size_set(int w, int h);
int  frame_host_window_width(void);    // GetScreenWidth equivalent
int  frame_host_window_height(void);   // GetScreenHeight equivalent
bool frame_host_window_fullscreen(void);
bool frame_host_window_maximized(void);
void frame_host_window_fullscreen_toggle(void);
// The display the window is on, for deciding how large a buffer could be
// shown. On a phone this is the screen itself.
void frame_host_display_size(int *w, int *h);

// Poll the platform's event queue. Normally implicit in frame_host_end_frame;
// a loop that reads input WITHOUT drawing a frame has to call it explicitly
// (see src/startup.c).
void frame_host_poll_events(void);

// Quieten the platform's own logging: the shell reports its own conditions,
// and raylib's per-asset chatter is noise at the prompt. Warnings and errors
// still come through.
void frame_host_quiet_log(void);

// ---------------------------------------------------------------------------
// Web input ordering rule -- READ THIS BEFORE ADDING A gfx_frame_end() CALL.
//
// On web, raylib's PollInputEvents() (called from EndDrawing) only copies
// currentKeyState into previousKeyState; there is no glfwPollEvents() to
// fill currentKeyState. Keys arrive from asynchronous browser callbacks,
// which run ONLY while the wasm stack is unwound inside an emscripten_sleep.
//
// So an IsKeyPressed edge opens when the browser delivers the event, and is
// destroyed by the next PollInputEvents. Whether input works at all is
// decided by where the frame's yield sits relative to that poll:
//
//     poll -> YIELD -> read      keys register            (correct)
//     YIELD -> poll -> read      keys silently vanish     (broken)
//
// The rule: there is exactly ONE yield per frame and it comes immediately
// after the poll. That is why frame_host_should_close() must not yield on
// web, and why every gfx_frame_end() call site goes through
// frame_host_end_frame() below. Getting this wrong does not fail loudly --
// it drops keypresses, in proportion to how much of the frame is spent in
// the wrong yield.
// ---------------------------------------------------------------------------

// Yield to the host. No-op on desktop. Call immediately after an input poll,
// never before an input read.
void frame_host_yield(void);

// gfx_frame_end() + frame_host_yield(). Use this INSTEAD of a bare
// gfx_frame_end() in any loop that reads input, so the yield lands on the
// correct side of the poll raylib's EndDrawing performs underneath it.
void frame_host_end_frame(void);

#endif
