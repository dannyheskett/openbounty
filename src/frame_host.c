#include "frame_host.h"
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
    EndDrawing();
    frame_host_yield();
}
