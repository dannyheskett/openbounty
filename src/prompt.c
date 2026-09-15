#include "input_host.h"
#include "ui_host.h"
#include "prompt.h"
#include "prompt_impl.h"
#include "pending.h"
#include "modern/mlist.h"
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
static bool g_step_open       = false; // modern dwelling: Recruit opened the stepper
static int  g_step_value      = 0;     // modern count entry: the stepper
static char g_text_buf[8];
static int  g_text_len = 0;

// Modern numeric and A/B prompts answer by rows. The rows come from the body's
// own choice lines ("1. Italia", "A) Take the gold"), or from
// prompt_set_choices when the body names none.
#define PROMPT_CHOICES_MAX 5
static char g_lead[256];
static char g_choice[PROMPT_CHOICES_MAX][96];
static int  g_choice_value[PROMPT_CHOICES_MAX];
static int  g_choice_n = 0;
static int  g_choice_cursor = 0;

// A choice line: optional spaces, then a digit 1-5 (numeric) or A/B (A/B),
// then '.' or ')'. Returns the answer (1-based) and sets *label, or 0.
static int choice_prefix(const char *s, bool ab, const char **label) {
    while (*s == ' ') s++;
    int v = 0;
    if (ab && (s[0] == 'A' || s[0] == 'B')) v = s[0] - 'A' + 1;
    else if (!ab && s[0] >= '1' && s[0] <= '5') v = s[0] - '0';
    if (!v || (s[1] != '.' && s[1] != ')')) return 0;
    s += 2;
    while (*s == ' ') s++;
    *label = s;
    return v;
}

static void append(char *dst, int cap, const char *src, int n) {
    int len = (int)strlen(dst);
    while (n-- > 0 && *src && len + 1 < cap) dst[len++] = *src++;
    dst[len] = '\0';
}

// Splits g_body into the lead text and the choice rows. A line that follows a
// choice without a prefix of its own continues it.
static void parse_choices(bool ab, int max_choice) {
    g_lead[0] = '\0';
    g_choice_n = 0;
    g_choice_cursor = 0;
    const char *p = g_body;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        char line[256];
        snprintf(line, sizeof line, "%.*s", n, p);
        const char *label = NULL;
        int v = choice_prefix(line, ab, &label);
        if (v && g_choice_n < PROMPT_CHOICES_MAX) {
            snprintf(g_choice[g_choice_n], sizeof g_choice[0], "%s", label);
            g_choice_value[g_choice_n++] = v;
        } else if (g_choice_n > 0 && line[0]) {
            append(g_choice[g_choice_n - 1], sizeof g_choice[0], " ", 1);
            append(g_choice[g_choice_n - 1], sizeof g_choice[0], line, n);
        } else if (g_choice_n == 0) {
            append(g_lead, sizeof g_lead, p, n);
            if (e) append(g_lead, sizeof g_lead, "\n", 1);
        }
        p = e ? e + 1 : p + n;
    }
    if (g_choice_n == 0) {
        // Nothing named: the answers themselves are the rows.
        int count = ab ? 2 : max_choice;
        for (int i = 0; i < count && i < PROMPT_CHOICES_MAX; i++) {
            if (ab) snprintf(g_choice[i], sizeof g_choice[0], "%c", 'A' + i);
            else    snprintf(g_choice[i], sizeof g_choice[0], "%d", i + 1);
            g_choice_value[i] = i + 1;
        }
        g_choice_n = count;
    }
}

void prompt_set_choices(const char *const *labels, const int *values, int n) {
    if (n > PROMPT_CHOICES_MAX) n = PROMPT_CHOICES_MAX;
    for (int i = 0; i < n; i++) {
        snprintf(g_choice[i], sizeof g_choice[0], "%s", labels[i]);
        g_choice_value[i] = values[i];
    }
    g_choice_n = n;
    g_choice_cursor = 0;
}

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
    parse_choices(false, max_choice);
    emit_open_trace("numeric");
}

void prompt_ab_open(const char *header, const char *body) {
    g_kind = PK_AB_CHOICE;
    copy_to(g_header, sizeof(g_header), header);
    copy_to(g_body,   sizeof(g_body),   body);
    parse_choices(true, 2);
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
    g_step_value = max_value > 0 ? max_value : 0;   // modern: the stepper starts at the most
    g_step_open = false;
    g_yn_cursor = 0;
    copy_to(g_header, sizeof(g_header), header);
    copy_to(g_body,   sizeof(g_body),   body);
    emit_open_trace("text");
}

int prompt_text_input_value(void) {
    if (g_text_len <= 0) return 0;
    return atoi(g_text_buf);
}

bool prompt_is_active(void) { return g_kind != PK_NONE; }
void prompt_gallery_step_open(bool open) { g_step_open = open; }

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

// Modern: up/down and Enter or a tap answer a numeric or A/B prompt by row.
// The digit and letter keys are read first, so keypad 2 and 8 still answer.
static PromptResult choice_rows_update(void) {
    if (!CL_IS_MODERN || g_choice_n <= 0) return PROMPT_RESULT_NONE;
    SelList l = { g_choice_n, g_choice_cursor };
    int row = -1;
    SelEvent ev = sel_input(&l, TOUCH_LIST_PROMPT, 0, &row);
    g_choice_cursor = l.cursor;
    if (ev != SEL_CONFIRM || row < 0 || row >= g_choice_n) return PROMPT_RESULT_NONE;
    int v = g_choice_value[row];
    prompt_dismiss();
    return (PromptResult)(PROMPT_RESULT_1 + v - 1);
}

PromptResult prompt_update(void) {
    if (g_kind == PK_NONE) return PROMPT_RESULT_NONE;

    // Touch: on-screen answer buttons for the keys read below, plus ESC.
    touch_request(TOUCH_CHROME_BACK);
    if      (g_kind == PK_YES_NO)     touch_request_prompt_yesno();
    else if (g_kind == PK_NUMERIC)    { if (!CL_IS_MODERN) touch_request_prompt_numeric(g_max_choice); }
    else if (g_kind == PK_AB_CHOICE)  { if (!CL_IS_MODERN) touch_request_prompt_ab(); }
    else if (g_kind == PK_TEXT_INPUT) {
        // Modern counts use the stepper (REQ-430n): no digit grid, no digit chrome.
        g_selector = false;
        if (!CL_IS_MODERN) touch_request(TOUCH_CHROME_DIGITS);
    }

    // Modern foe view: Fight / Evade. With nowhere to run, Evade (No, Esc) is
    // not an answer -- only Fight is.
    bool foe_view = CL_IS_MODERN && g_kind == PK_YES_NO && pending_flow == FLOW_ATTACK_FOE;
    bool no_evade = foe_view && pending_foe_evade_blocked;
    // Modern dwelling: Recruit / Leave rows first; Recruit opens the stepper,
    // and Esc puts the stepper away before it leaves.
    bool dwelling = CL_IS_MODERN && g_kind == PK_TEXT_INPUT && pending_flow == FLOW_RECRUIT;
    if (dwelling && g_step_open) {
        int tapped = touch_tapped_row(TOUCH_LIST_PROMPT);    // "Recruit 20" / Cancel
        if (input_key_pressed(KEY_ESCAPE) || tapped == 1) {
            g_step_open = false;
            return PROMPT_RESULT_NONE;
        }
        if (tapped == 0) {
            snprintf(g_text_buf, sizeof g_text_buf, "%d", g_step_value);
            g_text_len = (int)strlen(g_text_buf);
            g_kind = PK_NONE;
            return PROMPT_RESULT_YES;
        }
    }
    if (input_key_pressed(KEY_ESCAPE)) {
        if (no_evade) return PROMPT_RESULT_NONE;
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
            // The answers are buttons side by side: Left and Right move too.
            if (input_key_pressed(KEY_LEFT) || input_key_pressed(KEY_RIGHT)) l.cursor = 1 - l.cursor;
            if (no_evade) l.cursor = 0;          // Evade is not a row to rest on
            g_yn_cursor = l.cursor;
            if (ev == SEL_CONFIRM) {
                if (no_evade && row != 0) return PROMPT_RESULT_NONE;
                prompt_dismiss();
                return row == 0 ? PROMPT_RESULT_YES : PROMPT_RESULT_NO;
            }
            if (input_key_pressed(KEY_Y)) { prompt_dismiss(); return PROMPT_RESULT_YES; }
            if (input_key_pressed(KEY_N) && !no_evade) { prompt_dismiss(); return PROMPT_RESULT_NO; }
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
        return choice_rows_update();
    }

    if (g_kind == PK_AB_CHOICE) {
        if (input_key_pressed(KEY_A)) { prompt_dismiss(); return PROMPT_RESULT_1; }
        if (input_key_pressed(KEY_B)) { prompt_dismiss(); return PROMPT_RESULT_2; }
        return choice_rows_update();
    }

    if (dwelling && !g_step_open) {
        SelList l = { 2, g_yn_cursor };
        int row = -1;
        SelEvent ev = sel_input(&l, TOUCH_LIST_PROMPT, 0, &row);
        g_yn_cursor = l.cursor;
        if (ev == SEL_CONFIRM && row == 0 && g_text_max_value > 0) g_step_open = true;
        if (ev == SEL_CONFIRM && row == 1) { prompt_dismiss(); return PROMPT_RESULT_CANCEL; }
        return PROMPT_RESULT_NONE;
    }
    if (g_kind == PK_TEXT_INPUT && CL_IS_MODERN) {
        // The count stepper: Left/Right one, Down/Up ten, Enter commits the
        // value into the text buffer the flow reads (prompt_text_input_value).
        int lo = g_text_max_value > 0 ? 1 : 0;
        ml_stepper_keys(&g_step_value, lo, g_text_max_value > 0 ? g_text_max_value : 0);
        if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
            input_key_pressed(KEY_SPACE)) {
            snprintf(g_text_buf, sizeof g_text_buf, "%d", g_step_value);
            g_text_len = (int)strlen(g_text_buf);
            g_kind = PK_NONE;
            return PROMPT_RESULT_YES;
        }
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
    v.lead          = g_lead;
    v.choice_n      = g_choice_n;
    v.choices       = (const char (*)[96])g_choice;
    v.choice_cursor = g_choice_cursor;
    v.step_value = g_step_value;
    v.step_max   = g_text_max_value;
    v.step_open  = g_step_open;
    return &v;
}

void prompt_draw(void) {
    if (g_kind == PK_NONE) return;
    const PromptView *v = prompt_view();
    if (CL_IS_MODERN) modern_prompt_draw(v);
    else              legacy_prompt_draw(v);
}
