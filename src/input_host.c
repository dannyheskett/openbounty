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

bool input_key_pressed(int key) {
    return IsKeyPressed(key) || injected_has(key);
}

bool input_key_down(int key) {
    return IsKeyDown(key) || injected_has(key);
}

int input_get_key_pressed(void) {
    if (s_key_drain < s_key_count) return s_keys[s_key_drain++];
    return GetKeyPressed();
}

int input_get_char_pressed(void) {
    if (s_char_drain < s_char_count) return s_chars[s_char_drain++];
    return GetCharPressed();
}

// ---- pointer --------------------------------------------------------------

static bool s_touch_seen;

static void latch_touch(void) {
    if (!s_touch_seen && GetTouchPointCount() > 0) s_touch_seen = true;
}

bool input_pointer_pressed(int *x, int *y) {
    latch_touch();
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    if (x) *x = GetMouseX();
    if (y) *y = GetMouseY();
    return true;
}

bool input_pointer_down(int *x, int *y) {
    latch_touch();
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
