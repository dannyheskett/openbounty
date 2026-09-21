#ifndef OB_INPUT_HOST_H
#define OB_INPUT_HOST_H

#include <stdbool.h>

// Input host shim. Thin wrappers around the raylib input calls used by
// game logic, so callers don't include raylib directly.

bool input_key_pressed(int key);     // IsKeyPressed
bool input_key_down(int key);        // IsKeyDown
int  input_get_key_pressed(void);    // GetKeyPressed
int  input_get_char_pressed(void);   // GetCharPressed

// ---- gamepad --------------------------------------------------------------
//
// One pad, id 0. Button and axis ids are raylib's enum values, so call sites
// keep naming GAMEPAD_BUTTON_* / GAMEPAD_AXIS_* (ob_types.h defines those for
// a build with no raylib). Every call answers false/0 when no pad is present,
// which is the whole story on a phone.

bool  input_pad_available(void);
bool  input_pad_pressed(int button);
int   input_pad_any_pressed(void);     // 0 when nothing was pressed this frame
float input_pad_axis(int axis);

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

// A new screen or the end of a load: throw away every key and character
// pressed before now (raylib's queues and the injected ones) and ignore
// presses for `guard_seconds`, so keys hit while the game was busy do not
// answer the screen that follows.
void input_host_flush(double guard_seconds);
// Inject `key` as a press on the NEXT frame. A key injected during a frame's
// update is cleared at that frame's end (touch_frame), before the next frame's
// input reads it; this holds one key across the clear, the way a tap arrives.
void input_host_inject_key_next_frame(int key);

// ---- touch ----------------------------------------------------------------
//
// Single touch, read through raylib's touch API only. There is NO mouse
// support: no mouse call is made anywhere in the game and no cursor is drawn,
// so a desktop mouse does nothing. Call input_touch_sample() once a frame
// (touch_frame does, and so does any loop that reads touch without it); the
// press and release edges are this frame's contact against the last one's.

void input_touch_sample(void);
bool input_touch_pressed(int *x, int *y);     // press edge this frame
bool input_touch_down(int *x, int *y);        // contact held
bool input_touch_released(void);              // release edge this frame

// True once any touch contact has ever been seen this session; gates the
// on-screen touch chrome.
bool input_touch_active(void);

// ---- text entry mode ----------------------------------------------------------
//
// Whether a keyboard is present is inferred from what has been used: a real
// key press latches it for the session; before any key, a desktop is assumed
// to have one unless touch or a gamepad was seen first. Text fields type
// when there is a keyboard and show the in-game letter selector when not.
typedef enum { TEXT_MODE_KEYBOARD = 0, TEXT_MODE_SELECTOR } InputTextMode;
bool          input_has_keyboard(void);
InputTextMode input_text_mode(void);
bool          input_pad_or_touch_seen(void);   // selector is offered alongside typing
void          input_host_note_gamepad(void);   // called by input.c on any pad event

#endif
