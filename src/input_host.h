#ifndef OB_INPUT_HOST_H
#define OB_INPUT_HOST_H

#include <stdbool.h>

// Input host shim. Thin wrappers around the raylib input calls used by
// game logic, so callers don't include raylib directly.

bool input_key_pressed(int key);     // IsKeyPressed
bool input_key_down(int key);        // IsKeyDown
int  input_get_key_pressed(void);    // GetKeyPressed
int  input_get_char_pressed(void);   // GetCharPressed

// ---- synthetic input ------------------------------------------------------
//
// The touch layer (src/touch.c) translates taps into key events injected
// here, so every screen keeps its existing key handling and the recorder /
// replay see the same input stream a keyboard produces. An injected key is
// a one-frame edge exactly like a real press: visible to the reads above
// until input_host_clear_injected(), which touch_frame() calls at the top
// of its per-frame tick (from frame_host_end_frame).

void input_host_inject_key(int key);   // seen by input_key_pressed/_down
                                       // and input_get_key_pressed
void input_host_inject_char(int ch);   // seen by input_get_char_pressed
void input_host_clear_injected(void);

// ---- pointer --------------------------------------------------------------
//
// Mouse and single-touch, unified. raylib's web backend feeds the first
// touch through the mouse position/buttons, so reading the mouse covers
// both a desktop click and a tap; the touch API is only consulted to latch
// input_touch_active(), which gates the on-screen touch chrome.

bool input_pointer_pressed(int *x, int *y);   // press edge this frame
bool input_pointer_down(int *x, int *y);      // held
bool input_pointer_released(void);            // release edge this frame

// True once any real touch contact has ever been seen this session.
// Desktop mouse users never flip it, so they get taps without the chrome.
bool input_touch_active(void);

#endif
