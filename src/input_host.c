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

void input_host_clear_injected(void) {
    s_key_count = s_key_drain = 0;
    s_char_count = s_char_drain = 0;
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

bool input_key_pressed(int key) {
    return note_key(IsKeyPressed(key)) || injected_has(key);
}

bool input_key_down(int key) {
    return note_key(IsKeyDown(key)) || injected_has(key);
}

int input_get_key_pressed(void) {
    if (s_key_drain < s_key_count) return s_keys[s_key_drain++];
    int k = GetKeyPressed();
    if (k) s_key_seen = true;
    return k;
}

int input_get_char_pressed(void) {
    if (s_char_drain < s_char_count) return s_chars[s_char_drain++];
    int c = GetCharPressed();
    if (c) s_key_seen = true;
    return c;
}

// ---- pointer --------------------------------------------------------------

static bool s_touch_seen;

static void latch_touch(void) {
    if (!s_touch_seen && GetTouchPointCount() > 0) s_touch_seen = true;
}

// A pointer event is a TOUCH, never a mouse: the game has no mouse
// support and never shows a cursor. raylib's backends deliver a finger
// through the mouse API, so the mouse reads below are how a tap arrives,
// gated on a touch contact being present so a desktop mouse does nothing.
static bool touching(void) {
    latch_touch();
    return GetTouchPointCount() > 0;
}

bool input_pointer_pressed(int *x, int *y) {
    if (!touching()) return false;
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    if (x) *x = GetMouseX();
    if (y) *y = GetMouseY();
    return true;
}

bool input_pointer_down(int *x, int *y) {
    if (!touching()) return false;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return false;
    if (x) *x = GetMouseX();
    if (y) *y = GetMouseY();
    return true;
}

bool input_pointer_released(void) {
    return IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool input_touch_active(void) {
    latch_touch();
    return s_touch_seen;
}

bool input_has_keyboard(void) {
    latch_touch();
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
    latch_touch();
    return s_touch_seen || s_pad_seen;
}
