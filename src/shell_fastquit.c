// src/shell_fastquit.c

#include "input_host.h"
#include "shell_fastquit.h"

#include "raylib.h"
#include "input.h"

static bool s_active = false;

void fast_quit_open(void)         { s_active = true; }
bool fast_quit_is_active(void)    { return s_active; }

// chrome.c queries this via the same symbol; expose under the legacy
// name as well so we don't have to sweep that one call site.
bool main_fast_quit_active(void)  { return s_active; }

bool fast_quit_tick(void) {
    if (!s_active) return false;
    if (input_key_pressed(KEY_Y)) {
        s_active = false;
        return true;
    }
    if (input_key_pressed(KEY_N) || input_key_pressed(KEY_ESCAPE)
        || gamepad_pressed_cancel()) {
        s_active = false;
    }
    return false;
}
