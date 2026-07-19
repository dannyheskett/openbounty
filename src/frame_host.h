#ifndef OB_FRAME_HOST_H
#define OB_FRAME_HOST_H

#include <stdbool.h>

// Frame / time host shim. Thin wrappers around the raylib time/window
// calls used by game logic, so callers don't include raylib directly.

double frame_host_time(void);          // GetTime equivalent
bool   frame_host_should_close(void);  // WindowShouldClose equivalent

// ---------------------------------------------------------------------------
// Web input ordering rule -- READ THIS BEFORE ADDING AN EndDrawing() CALL.
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
// web, and why every EndDrawing() call site goes through
// frame_host_end_frame() below. Getting this wrong does not fail loudly --
// it drops keypresses, in proportion to how much of the frame is spent in
// the wrong yield.
// ---------------------------------------------------------------------------

// Yield to the host. No-op on desktop. Call immediately after an input poll,
// never before an input read.
void frame_host_yield(void);

// EndDrawing() + frame_host_yield(). Use this INSTEAD of a bare EndDrawing()
// in any loop that reads input, so the yield lands on the correct side of
// the poll that EndDrawing performs.
void frame_host_end_frame(void);

#endif
