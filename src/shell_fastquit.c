// src/shell_fastquit.c

#include "input_host.h"
#include "shell_fastquit.h"

#include "raylib.h"
#include "input.h"

static bool s_active = false;

void fast_quit_open(void)         { s_active = true; }
bool fast_quit_is_active(void)    { return s_active; }

// Exposed under the main_* name because it is part of the engine->host
// surface (engine/include/ui_host.h), which engine/host_noop.c also stubs.
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
