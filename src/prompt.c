#include "input_host.h"
#include "ui_host.h"
#include "prompt.h"
#include "prompt_impl.h"
#include "touch.h"
#include "layout.h"
#include "ui.h"
#include "select.h"
#include "textsel.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include "recorder.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PromptKind g_kind = PK_NONE;
static char g_header[64];
static char g_body[256];
static int  g_yn_cursor = 0;   // modern yes/no rows: 0 = Yes, 1 = No
static TextSel g_ts = { 0, true };   // modern numeric selector
static bool g_selector = false;

// The same bound the typed path applies: room for the digit, and the
// number it makes must not pass max_value.
static bool prompt_digit_allowed(const char *buf, int len, int ch);
static int  g_max_choice = 5;
static int  g_text_max_digits = 4;
static int  g_text_max_value  = 9999;
static char g_text_buf[8];
static int  g_text_len = 0;

static void copy_to(char *dst, int dst_sz, const char *src) {
    int n = 0;
    if (src) while (n + 1 < dst_sz && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

// Single trace hook used by every prompt opener: emits prompt:open:<kind>
// so the harness consumer can see the modal go up without polling state.
static void emit_open_trace(const char *kind) {
    char tag[48];
    snprintf(tag, sizeof tag, "prompt:open:%s", kind);
    recorder_capture(tag);
}

void prompt_yes_no_open(const char *header, const char *body) {
    g_yn_cursor = 0;
    g_kind = PK_YES_NO;
    copy_to(g_header, sizeof(g_header), header);
    copy_to(g_body,   sizeof(g_body),   body);
    emit_open_trace("yes_no");
}

void prompt_numeric_open(const char *header, const char *body, int max_choice) {
    g_kind = PK_NUMERIC;
    if (max_choice < 1) max_choice = 1;
    if (max_choice > 5) max_choice = 5;
    g_max_choice = max_choice;
    copy_to(g_header, sizeof(g_header), header);
    copy_to(g_body,   sizeof(g_body),   body);
    emit_open_trace("numeric");
}

void prompt_ab_open(const char *header, const char *body) {
    g_kind = PK_AB_CHOICE;
    copy_to(g_header, sizeof(g_header), header);
    copy_to(g_body,   sizeof(g_body),   body);
    emit_open_trace("ab");
}

static bool prompt_digit_allowed(const char *buf, int len, int ch) {
    if (ch < '0' || ch > '9' || len >= g_text_max_digits) return false;
    char cand[8];
    int n = (len < 6) ? len : 6;
    for (int i = 0; i < n; i++) cand[i] = buf[i];
    cand[n] = (char)ch; cand[n + 1] = '\0';
    return atoi(cand) <= g_text_max_value;
}

void prompt_text_input_open(const char *header, const char *body,
                            int max_digits, int max_value) {
    g_ts.cursor = 0;
    g_kind = PK_TEXT_INPUT;
    if (max_digits < 1) max_digits = 1;
    if (max_digits > 6) max_digits = 6;
    g_text_max_digits = max_digits;
    g_text_max_value = max_value;
    g_text_len = 0;
    g_text_buf[0] = '\0';
    copy_to(g_header, sizeof(g_header), header);
    copy_to(g_body,   sizeof(g_body),   body);
    emit_open_trace("text");
}

int prompt_text_input_value(void) {
    if (g_text_len <= 0) return 0;
    return atoi(g_text_buf);
}

bool prompt_is_active(void) { return g_kind != PK_NONE; }

const char *prompt_kind_str(void) {
    switch (g_kind) {
        case PK_YES_NO:     return "yes_no";
        case PK_NUMERIC:    return "numeric";
        case PK_AB_CHOICE:  return "ab";
        case PK_TEXT_INPUT: return "text";
        case PK_NONE:
        default:            return "none";
    }
}

const char *prompt_header_text(void) { return g_header; }
const char *prompt_body_text(void)   { return g_body; }

void prompt_dismiss(void) {
    bool was_active = (g_kind != PK_NONE);
    g_kind = PK_NONE;
    g_header[0] = '\0';
    g_body[0] = '\0';
    if (was_active) recorder_capture("prompt:close");
}

PromptResult prompt_update(void) {
    if (g_kind == PK_NONE) return PROMPT_RESULT_NONE;

    // Touch: on-screen answer buttons for the keys read below, plus ESC.
    touch_request(TOUCH_CHROME_BACK);
    if      (g_kind == PK_YES_NO)     touch_request_prompt_yesno();
    else if (g_kind == PK_NUMERIC)    touch_request_prompt_numeric(g_max_choice);
    else if (g_kind == PK_AB_CHOICE)  touch_request_prompt_ab();
    else if (g_kind == PK_TEXT_INPUT) {
        g_selector = CL_IS_MODERN &&
                     (input_text_mode() == TEXT_MODE_SELECTOR || input_pad_or_touch_seen());
        if (!g_selector) touch_request(TOUCH_CHROME_DIGITS);
    }

    if (input_key_pressed(KEY_ESCAPE)) {
        prompt_dismiss();
        return PROMPT_RESULT_CANCEL;
    }

    if (g_kind == PK_YES_NO) {
        // A forced (static-guardian) foe fight cannot be declined: confirm it
        // immediately, without waiting for a keypress -- no decline offered.
        if (pending_foe_forced) { prompt_dismiss(); return PROMPT_RESULT_YES; }
        // Modern: two rows, Yes and No, with the cursor; Enter confirms the
        // cursor row (so Enter is no longer a blind yes), Y and N still answer.
        if (CL_IS_MODERN) {
            SelList l = { 2, g_yn_cursor };
            int row = -1;
            SelEvent ev = sel_input(&l, TOUCH_LIST_PROMPT, 0, &row);
            g_yn_cursor = l.cursor;
            if (ev == SEL_CONFIRM) { prompt_dismiss(); return row == 0 ? PROMPT_RESULT_YES : PROMPT_RESULT_NO; }
            if (input_key_pressed(KEY_Y)) { prompt_dismiss(); return PROMPT_RESULT_YES; }
            if (input_key_pressed(KEY_N)) { prompt_dismiss(); return PROMPT_RESULT_NO;  }
            return PROMPT_RESULT_NONE;
        }
        if (input_key_pressed(KEY_Y)) { prompt_dismiss(); return PROMPT_RESULT_YES; }
        if (input_key_pressed(KEY_N)) { prompt_dismiss(); return PROMPT_RESULT_NO;  }
        // also accepts Enter as "yes" in some prompts.
        if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER)) {
            prompt_dismiss();
            return PROMPT_RESULT_YES;
        }
        return PROMPT_RESULT_NONE;
    }

    if (g_kind == PK_NUMERIC) {
        // KEY_ONE..KEY_FIVE are contiguous in raylib.
        for (int i = 0; i < g_max_choice; i++) {
            if (input_key_pressed(KEY_ONE + i)) {
                prompt_dismiss();
                return (PromptResult)(PROMPT_RESULT_1 + i);
            }
            if (input_key_pressed(KEY_KP_1 + i)) {
                prompt_dismiss();
                return (PromptResult)(PROMPT_RESULT_1 + i);
            }
        }
        return PROMPT_RESULT_NONE;
    }

    if (g_kind == PK_AB_CHOICE) {
        if (input_key_pressed(KEY_A)) { prompt_dismiss(); return PROMPT_RESULT_1; }
        if (input_key_pressed(KEY_B)) { prompt_dismiss(); return PROMPT_RESULT_2; }
        return PROMPT_RESULT_NONE;
    }

    if (g_kind == PK_TEXT_INPUT && g_selector) {
        if (textsel_input(&g_ts, g_text_buf, &g_text_len, (int)sizeof g_text_buf,
                          TOUCH_LIST_TEXTSEL, prompt_digit_allowed)) {
            // Committed, the same way Enter commits the typed value: close
            // by kind only so prompt_text_input_value() still reads the buffer.
            g_kind = PK_NONE;
            return PROMPT_RESULT_YES;
        }
        return PROMPT_RESULT_NONE;
    }
    if (g_kind == PK_TEXT_INPUT) {
        // Digit keys -- append if room and the candidate number wouldn't
        // exceed max_value.
        for (int d = 0; d < 10; d++) {
            bool pressed = input_key_pressed(KEY_ZERO + d) ||
                           input_key_pressed(KEY_KP_0 + d);
            if (!pressed) continue;
            if (g_text_len >= g_text_max_digits) break;
            // Build candidate and test.
            char cand[8];
            for (int i = 0; i < g_text_len; i++) cand[i] = g_text_buf[i];
            cand[g_text_len] = (char)('0' + d);
            cand[g_text_len + 1] = '\0';
            if (atoi(cand) > g_text_max_value) break;
            g_text_buf[g_text_len++] = (char)('0' + d);
            g_text_buf[g_text_len] = '\0';
            break;
        }
        if (input_key_pressed(KEY_BACKSPACE) && g_text_len > 0) {
            g_text_len--;
            g_text_buf[g_text_len] = '\0';
        }
        if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER)) {
            // Value committed. Close the prompt by kind only, leaving the
            // text buffer intact, so the caller can still read it through
            // prompt_text_input_value() after this returns. The buffer is
            // static and is zeroed only by the next prompt_open.
            g_kind = PK_NONE;
            return PROMPT_RESULT_YES;
        }
        return PROMPT_RESULT_NONE;
    }

    return PROMPT_RESULT_NONE;
}

// A read-only window onto the state above, so the two draw paths can render
// the prompt without owning any of it.
const PromptView *prompt_view(void) {
    static PromptView v;
    v.kind       = g_kind;
    v.header     = g_header;
    v.body       = g_body;
    v.max_choice = g_max_choice;
    v.yn_cursor  = g_yn_cursor;
    v.selector   = g_selector;
    v.text_buf   = g_text_buf;
    v.text_len   = g_text_len;
    v.ts         = &g_ts;
    return &v;
}

void prompt_draw(void) {
    if (g_kind == PK_NONE) return;
    const PromptView *v = prompt_view();
    if (CL_IS_MODERN) modern_prompt_draw(v);
    else              legacy_prompt_draw(v);
}
