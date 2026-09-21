#include "input_host.h"
#include "raylib.h"

// Thin pass-through wrappers around raylib input, plus the synthetic-key
// queue the touch layer injects into (see input_host.h).

#define INJECT_MAX 32

static int  s_keys[INJECT_MAX];
static int  s_key_count;
static int  s_key_drain;        // next index input_get_key_pressed hands out
static int  s_chars[INJECT_MAX];
static int  s_char_count;
static int  s_char_drain;

void input_host_inject_key(int key) {
    if (key != 0 && s_key_count < INJECT_MAX) s_keys[s_key_count++] = key;
}

void input_host_inject_char(int ch) {
    if (ch != 0 && s_char_count < INJECT_MAX) s_chars[s_char_count++] = ch;
}

static int s_next_key = 0;

// Screen changes and loads: keys pressed while the game was busy sit in
// raylib's queue and would answer the next screen ("skip you forward").
static double s_guard_until = 0.0;
static bool guarded(void) { return GetTime() < s_guard_until; }

void input_host_flush(double guard_seconds) {
    while (GetKeyPressed() != 0) {}
    while (GetCharPressed() != 0) {}
    s_key_count = s_key_drain = 0;
    s_char_count = s_char_drain = 0;
    s_next_key = 0;
    s_guard_until = GetTime() + guard_seconds;
}

void input_host_inject_key_next_frame(int key) { s_next_key = key; }

void input_host_clear_injected(void) {
    s_key_count = s_key_drain = 0;
    s_char_count = s_char_drain = 0;
    if (s_next_key) { input_host_inject_key(s_next_key); s_next_key = 0; }
}

static bool injected_has(int key) {
    for (int i = 0; i < s_key_count; i++)
        if (s_keys[i] == key) return true;
    return false;
}

// ---- input sources ----------------------------------------------------------
//
// Which physical devices have been seen this session. A real key event
// (not an injected one) latches the keyboard; the touch layer latches
// touch; input.c latches the gamepad. input_has_keyboard decides whether
// text fields are typed or use the in-game letter selector.

static bool s_key_seen;
static bool s_pad_seen;

static bool note_key(bool real) {
    if (real) s_key_seen = true;
    return real;
}

void input_host_note_gamepad(void) { s_pad_seen = true; }

// ---- gamepad ----------------------------------------------------------------
//
// Pad 0 only. Deliberately NOT routed through note_key: a pad is not a
// keyboard, and text entry must stay on the letter selector for a player
// holding one.
#define PAD_ID 0

bool input_pad_available(void) { return IsGamepadAvailable(PAD_ID); }

bool input_pad_pressed(int button) {
    return IsGamepadButtonPressed(PAD_ID, button);
}

int input_pad_any_pressed(void) { return GetGamepadButtonPressed(); }

float input_pad_axis(int axis) {
    return GetGamepadAxisMovement(PAD_ID, axis);
}

bool input_key_pressed(int key) {
    if (guarded()) return false;
#if defined(PLATFORM_ANDROID)
    // The system Back gesture is Android's universal "go back", so it answers
    // wherever the shell asks about Escape -- every menu, view and prompt
    // dismiss path gets it without a platform branch of its own. Deliberately
    // NOT through note_key: Back is not a keyboard, and latching one would
    // switch text entry from the letter selector to typing on a device with
    // no keys.
    if (key == KEY_ESCAPE && IsKeyPressed(KEY_BACK)) return true;
#endif
    return note_key(IsKeyPressed(key)) || injected_has(key);
}

bool input_key_down(int key) {
    return note_key(IsKeyDown(key)) || injected_has(key);
}

int input_get_key_pressed(void) {
    if (guarded()) { while (GetKeyPressed() != 0) {} return 0; }
    if (s_key_drain < s_key_count) return s_keys[s_key_drain++];
    int k = GetKeyPressed();
    if (k) s_key_seen = true;
    return k;
}

int input_get_char_pressed(void) {
    if (guarded()) { while (GetCharPressed() != 0) {} return 0; }
    if (s_char_drain < s_char_count) return s_chars[s_char_drain++];
    int c = GetCharPressed();
    if (c) s_key_seen = true;
    return c;
}

// ---- touch ----------------------------------------------------------------
//
// The game has NO mouse support: no mouse call is made anywhere, and no
// cursor is drawn. A finger is read through raylib's touch API alone
// (GetTouchPointCount / GetTouchPosition), so a desktop mouse does nothing at
// all. The touch API reports whether a contact is down and where, not press
// and release edges, so the contact is sampled once a frame
// (input_touch_sample) and the edges come from this frame against the last.

static bool    s_touch_seen;
static bool    s_touch_now, s_touch_prev;
static Vector2 s_touch_pos;

void input_touch_sample(void) {
    s_touch_prev = s_touch_now;
    s_touch_now  = GetTouchPointCount() > 0;
    if (s_touch_now) {
        s_touch_pos  = GetTouchPosition(0);
        s_touch_seen = true;
    }
}

bool input_touch_pressed(int *x, int *y) {
    if (!(s_touch_now && !s_touch_prev)) return false;
    if (x) *x = (int)s_touch_pos.x;
    if (y) *y = (int)s_touch_pos.y;
    return true;
}

bool input_touch_down(int *x, int *y) {
    if (!s_touch_now) return false;
    if (x) *x = (int)s_touch_pos.x;
    if (y) *y = (int)s_touch_pos.y;
    return true;
}

bool input_touch_released(void) {
    return s_touch_prev && !s_touch_now;
}

bool input_touch_active(void) {
    return s_touch_seen;
}

bool input_has_keyboard(void) {
    if (s_key_seen) return true;
    // Nothing typed yet: a desktop is assumed to have a keyboard until a
    // touch or a gamepad shows up first; the web build assumes none once
    // either has been seen.
    return !(s_touch_seen || s_pad_seen);
}

InputTextMode input_text_mode(void) {
    return input_has_keyboard() ? TEXT_MODE_KEYBOARD : TEXT_MODE_SELECTOR;
}

bool input_pad_or_touch_seen(void) {
    return s_touch_seen || s_pad_seen;
}
