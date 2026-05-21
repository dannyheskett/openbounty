#ifndef OB_INPUT_HOST_H
#define OB_INPUT_HOST_H

#include <stdbool.h>

// Input host shim. Drop-in replacements for the raylib input calls
// used by game logic. In normal play these forward straight to raylib.
// In gameplay-test mode they pop events from a scripted queue, which
// lets a test scenario drive the real game through deterministic
// input without a human at the keyboard.
//
// One shim covers every game-logic input site in src/ (input.c,
// main.c, combat_loop.c, startup.c, views.c, prompt.c, pack_select.c,
// screens/*, ui.c). Cheat/debug screens (shell_cheats.c) and the
// encoder dialog stay raylib-native — they're not in the test path.
//
// The engine itself never sees these calls; engine code compiles
// against engine/headless/raylib.h and has no raylib dependency.

bool input_key_pressed(int key);     // IsKeyPressed
bool input_key_down(int key);        // IsKeyDown
int  input_get_key_pressed(void);    // GetKeyPressed (drains queue)
int  input_get_char_pressed(void);   // GetCharPressed (drains queue)

// ----- Mode control -------------------------------------------------------
// Default (set implicitly by the first call). Forwards everything to
// raylib.
void input_host_use_raylib(void);

// Test mode. The shim consumes from an internal queue fed by the
// gameplay-test runner. Any key that isn't in the queue reports
// "not pressed" — the test must script everything it expects.
void input_host_use_queue(void);

// Queue a key for IsKeyPressed / GetKeyPressed. Press events stay in
// the queue until consumed (each match consumes one event). This
// mirrors real raylib edge-triggered input — one press fires exactly
// one IsKeyPressed match. Queue order is preserved for
// GetKeyPressed.
void input_host_queue_key(int key);

// Queue a held key — IsKeyDown returns true for `frames` consecutive
// frames after this is called. Use frames=1 for a one-frame tap.
void input_host_queue_key_down(int key, int frames);

// Queue a unicode codepoint for GetCharPressed (prompt_text_input).
void input_host_queue_char(int codepoint);

// Current depth of the press-event queue (not counting the active
// key). Scenarios use this to avoid over-queuing the same key every
// frame when the engine hasn't drained yet.
int  input_host_queue_depth(void);

// Advance the frame counter — call once per logical frame in test
// mode. Decays held-down keys; drops the press-edge queue's "fresh
// this frame" flag.
void input_host_tick(void);

// Slow the queue's key-promotion to one key per N ticks. Default 1
// (one queued key per frame). Use higher values when paired with
// frame_host_set_test_fps() to slow scripted scenarios down to a
// human-watchable pace. The shim still TICKS every frame (so the
// "active key" stays current); only the *promotion of the next
// queued event* is rate-limited.
void input_host_set_promotion_period(int ticks_per_promotion);

// True iff the queue mode is active. Used by views/screens that need
// to know whether to skip raylib gamepad polling (gamepads aren't
// scriptable yet).
bool input_host_is_scripted(void);

#endif
