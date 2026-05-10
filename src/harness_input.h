#ifndef OB_HARNESS_INPUT_H
#define OB_HARNESS_INPUT_H

#include <stdbool.h>

// OpenBounty input shim. ALL input call sites in src/ MUST go through
// these four wrappers, never raylib's IsKeyPressed/IsKeyDown/
// GetKeyPressed/GetCharPressed directly. This is what lets the harness
// inject synthesized input.
//
// In normal play (no --harness flag), each wrapper forwards to raylib.
// In harness mode, each wrapper consults a per-frame queue first; if
// the queue has a matching event, it returns that and clears it,
// otherwise it falls through to raylib.
//
// The queue is reset at the end of each frame by harness_frame_end().

bool harness_key_pressed(int key);
bool harness_key_down(int key);
int  harness_get_key_pressed(void);
int  harness_get_char_pressed(void);

// Harness-only API. No-ops when the harness isn't active.
bool harness_active(void);
void harness_queue_key_pressed(int key);   // one-shot edge for next frame
void harness_queue_key_down(int key);      // sustained until release
void harness_queue_key_release(int key);   // ends a queued hold
void harness_queue_char(int codepoint);    // one-shot char for next frame
void harness_frame_end(void);              // clear one-shot queues

// Compile-time poison. Files that include raylib.h must NOT call
// IsKeyPressed/IsKeyDown/GetKeyPressed/GetCharPressed directly. Defining
// these as undefined references catches any miss at link time, since
// gcc lacks a portable "function call forbidden" attribute. The shim
// itself defines OB_HARNESS_INPUT_INTERNAL_H before including raylib so
// it can call the real functions.
#ifndef OB_HARNESS_INPUT_INTERNAL_H
#define IsKeyPressed(k)   _openbounty_use_harness_key_pressed_instead
#define IsKeyDown(k)      _openbounty_use_harness_key_down_instead
#define GetKeyPressed()   _openbounty_use_harness_get_key_pressed_instead
#define GetCharPressed()  _openbounty_use_harness_get_char_pressed_instead
#endif

#endif
