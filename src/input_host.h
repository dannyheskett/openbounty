#ifndef OB_INPUT_HOST_H
#define OB_INPUT_HOST_H

#include <stdbool.h>

// Input host shim. Drop-in replacements for the raylib input calls
// used by game logic. In normal play these forward straight to raylib.
// In autoplay (scripted) mode, a single "live key" register drives
// what the engine sees this tick.

bool input_key_pressed(int key);     // IsKeyPressed
bool input_key_down(int key);        // IsKeyDown
int  input_get_key_pressed(void);    // GetKeyPressed (drains live key)
int  input_get_char_pressed(void);   // GetCharPressed (drains queue)

// ----- Mode control -------------------------------------------------------
void input_host_use_raylib(void);
void input_host_use_queue(void);

// ----- Autoplay live-key API (per-tick set/clear) -------------------------
// The autoplay dispatcher calls ap_set_key(K) each tick to set the
// engine-visible key for THIS tick. input_key_pressed(K) returns true
// iff K matches the live key. ap_clear_key sets the live key to "none."
//
// The live key is NOT auto-cleared by input_host_tick. Autoplay must
// set or clear on each per_tick invocation.
void ap_set_key(int key);
void ap_clear_key(void);

// ----- Tiny FIFO ----------------------------------------------------------
// Used ONLY for sequences that can't be driven one-per-tick by the
// dispatcher:
//   (a) startup wizard: pre-queued before the main loop starts.
//   (b) combat picker: a few direction keys + KEY_A confirm.
// input_host_tick pops one queued key into the live key. When the
// queue is empty, input_host_tick leaves the live key alone (so the
// dispatcher's per-tick ap_set_key keeps working).
void input_host_queue_key(int key);
int  input_host_queue_depth(void);

// Held-key support (KEY_DOWN-style for adventure-mode movement).
void input_host_queue_key_down(int key, int frames);

// Char queue for text-input prompts.
void input_host_queue_char(int codepoint);

// Per-tick advance. Pops one queued key into the live key if any;
// otherwise no-op on the live key. Always decays held-key counters.
void input_host_tick(void);

bool input_host_is_scripted(void);

#endif
