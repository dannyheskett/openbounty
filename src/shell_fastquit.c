// src/shell_fastquit.c

#include "input_host.h"
#include "shell_fastquit.h"
#include "touch.h"
#include "layout.h"
#include "prompt.h"
#include "resources.h"

#include "ob_types.h"
#include "input.h"

static bool s_active = false;

// Legacy asks in the status band (chrome.c); modern asks with the ordinary
// yes/no prompt, rows and all.
void fast_quit_open(void) {
    s_active = true;
    if (CL_IS_MODERN) {
        const Resources *r = resources_current();
        prompt_yes_no_open(NULL, r ? r->ui.quit_to_dos_prompt : "");
    }
}
bool fast_quit_is_active(void)    { return s_active; }

// Exposed under the main_* name because it is part of the engine->host
// surface (engine/include/ui_host.h), which engine/host_noop.c also stubs.
// Only the legacy status-band question reads it.
bool main_fast_quit_active(void)  { return s_active && !CL_IS_MODERN; }

bool fast_quit_tick(void) {
    if (!s_active) return false;
    if (CL_IS_MODERN) {
        PromptResult r = prompt_update();
        if (r == PROMPT_RESULT_NONE) return false;
        s_active = false;
        return r == PROMPT_RESULT_YES;
    }
    touch_request_prompt_yesno();
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
