#ifndef OB_FRAME_HOST_H
#define OB_FRAME_HOST_H

#include <stdbool.h>

// Frame / time host shim. Replaces direct raylib time/window calls in
// game logic so a gameplay test can step the simulation
// deterministically without depending on wall-clock pacing.
//
// In normal play these forward to raylib (GetTime / WindowShouldClose).
// In test mode, frame_host_time() returns a monotonic counter that
// advances by a fixed dt per frame_host_tick(), and
// frame_host_should_close() returns the value of an internal "quit"
// flag the runner can flip.

double frame_host_time(void);          // GetTime equivalent
bool   frame_host_should_close(void);  // WindowShouldClose equivalent

void frame_host_use_raylib(void);
void frame_host_use_test(void);

// In test mode, sleep at the start of each frame so the simulation
// runs at roughly the given frames-per-second instead of as fast as
// the CPU allows. `fps` <= 0 disables the throttle (default).
// Used by --gameplay-test --visible so a human can actually follow
// the run on screen; CI / headless runs leave it off.
void frame_host_set_test_fps(int fps);

// Advance the test clock by one logical frame (default dt = 1/60s).
// Also no-op in raylib mode so callers can call unconditionally.
void frame_host_tick(void);

// Cause the next frame_host_should_close() call to return true.
void frame_host_request_close(void);

// Per-frame callback fired by frame_host_should_close() right before
// it ticks the input/frame clock. Test scenarios use this to inspect
// global screen state (dialog_is_active(), views_active()) and queue
// the next key when the right screen is up — without needing to be
// inserted into every modal loop. NULL clears it.
typedef void (*FrameHostBeforeFrameFn)(void *user);
void frame_host_set_before_frame(FrameHostBeforeFrameFn fn, void *user);

#endif
