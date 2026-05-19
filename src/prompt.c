#include "prompt.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include "recorder.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PK_NONE = 0,
    PK_YES_NO,
    PK_NUMERIC,
    PK_AB_CHOICE,
    PK_TEXT_INPUT,
} PromptKind;

static PromptKind g_kind = PK_NONE;
static char g_header[64];
static char g_body[256];
static int  g_max_choice = 5;
static int  g_text_max_digits = 4;
static int  g_text_max_value  = 9999;
static char g_text_buf[8];
static int  g_text_len = 0;

// One-shot synthetic result. When non-NONE, the next prompt_update()
// returns this value (and dismisses the prompt) without consulting
// raylib input. Used by the AI driver to resolve prompts without
// faking keystrokes; cleared on consumption.
static PromptResult g_forced = PROMPT_RESULT_NONE;
static int          g_forced_text_value = 0;

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

void prompt_text_input_open(const char *header, const char *body,
                            int max_digits, int max_value) {
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

void prompt_force_resolve(PromptResult r) {
    g_forced = r;
}

void prompt_force_resolve_text(int value) {
    g_forced_text_value = value;
    g_forced = PROMPT_RESULT_YES;
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

    // Synthetic resolution (AI driver). Consumes the forced result
    // and dismisses the prompt; for text-input prompts the caller
    // staged the value via prompt_force_resolve_text first.
    if (g_forced != PROMPT_RESULT_NONE) {
        PromptResult r = g_forced;
        g_forced = PROMPT_RESULT_NONE;
        if (g_kind == PK_TEXT_INPUT && r == PROMPT_RESULT_YES) {
            // Stage the typed value so prompt_text_input_value() returns it.
            snprintf(g_text_buf, sizeof g_text_buf, "%d", g_forced_text_value);
            g_text_len = (int)strlen(g_text_buf);
            // Mirror the keyboard path: dispatcher reads value, then
            // dismiss clears state.
            recorder_capture("prompt:close:forced");
            g_kind = PK_NONE;
            return PROMPT_RESULT_YES;
        }
        prompt_dismiss();
        return r;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        prompt_dismiss();
        return PROMPT_RESULT_CANCEL;
    }

    if (g_kind == PK_YES_NO) {
        if (IsKeyPressed(KEY_Y)) { prompt_dismiss(); return PROMPT_RESULT_YES; }
        if (IsKeyPressed(KEY_N)) { prompt_dismiss(); return PROMPT_RESULT_NO;  }
        // also accepts Enter as "yes" in some prompts.
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            prompt_dismiss();
            return PROMPT_RESULT_YES;
        }
        return PROMPT_RESULT_NONE;
    }

    if (g_kind == PK_NUMERIC) {
        // KEY_ONE..KEY_FIVE are contiguous in raylib.
        for (int i = 0; i < g_max_choice; i++) {
            if (IsKeyPressed(KEY_ONE + i)) {
                prompt_dismiss();
                return (PromptResult)(PROMPT_RESULT_1 + i);
            }
            if (IsKeyPressed(KEY_KP_1 + i)) {
                prompt_dismiss();
                return (PromptResult)(PROMPT_RESULT_1 + i);
            }
        }
        return PROMPT_RESULT_NONE;
    }

    if (g_kind == PK_AB_CHOICE) {
        if (IsKeyPressed(KEY_A)) { prompt_dismiss(); return PROMPT_RESULT_1; }
        if (IsKeyPressed(KEY_B)) { prompt_dismiss(); return PROMPT_RESULT_2; }
        return PROMPT_RESULT_NONE;
    }

    if (g_kind == PK_TEXT_INPUT) {
        // Digit keys — append if room and the candidate number wouldn't
        // exceed max_value.
        for (int d = 0; d < 10; d++) {
            bool pressed = IsKeyPressed(KEY_ZERO + d) ||
                           IsKeyPressed(KEY_KP_0 + d);
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
        if (IsKeyPressed(KEY_BACKSPACE) && g_text_len > 0) {
            g_text_len--;
            g_text_buf[g_text_len] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            // Value committed; caller reads via prompt_text_input_value()
            // BEFORE dismissal clears the buffer. We leave the state
            // intact and have prompt_dismiss() clear it afterwards — so
            // return here without dismissing, let the caller dismiss.
            // Simpler: cache then dismiss.
            // (Buffer is preserved across dismiss via static storage; we
            // zero it only on a new prompt_open. So this is fine.)
            g_kind = PK_NONE;
            return PROMPT_RESULT_YES;
        }
        return PROMPT_RESULT_NONE;
    }

    return PROMPT_RESULT_NONE;
}

// Word-wrap helper: fills `out` with as much of `*p` as fits in
// `max_chars` characters at the 8-pixel glyph width, stopping at word
// boundaries. Advances *p.
static int take_line(const char **p, int max_chars,
                     char *out, int out_sz) {
    int n = 0;
    while (**p == ' ' || **p == '\t') (*p)++;
    while (**p && **p != '\n' && n + 1 < out_sz && n < max_chars) {
        out[n++] = **p; (*p)++;
    }
    if (**p && **p != '\n' && n >= max_chars) {
        int back = n;
        while (back > 0 && out[back - 1] != ' ') back--;
        if (back > 0) {
            int over = n - back;
            *p -= over;
            n = back;
        }
    }
    out[n] = '\0';
    if (**p == '\n') (*p)++;
    return n;
}

void prompt_draw(void) {
    if (g_kind == PK_NONE) return;

    int row_h = BFONT_GLYPH_H + 1;
    int pad = 4;
    //  draws KB_BottomFrame at a FIXED size: 30 chars
    // wide × 8 chars tall + a few extra pixels. Width matches the map
    // area; sidebar stays visible to the right. Body text starts at the
    // top of the inner area; short content leaves blank rows below.
    int x = CL_PANEL_X;
    int y = CL_PANEL_Y;
    int w = CL_PANEL_W;
    int h = CL_PANEL_H;
    int max_chars = (w - 2 * pad) / BFONT_GLYPH_W;

    // Reserve rows at the bottom for hint chrome (rendered after the body).
    //   text-input      → 2 (typed value + hint)
    //   yes/no, numeric → 1 (hint only)
    //   A/B choice      → 0 (body names the keys; chrome-less)
    int bottom_rows;
    if (g_kind == PK_TEXT_INPUT)      bottom_rows = 2;
    else if (g_kind == PK_AB_CHOICE)  bottom_rows = 0;
    else                              bottom_rows = 1;

    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    DrawRectangleLines(x, y, w, h, PAL_CLR(YELLOW));

    int tx = x + pad;
    int ty = y + pad;

    if (g_header[0]) {
        bfont_draw(g_header, tx, ty, PAL_CLR(YELLOW));
        ty += row_h + 2;
    }

    // Body: render every line that fits inside the inner rect, leaving
    // bottom_rows free for the hint chrome at the very bottom.
    {
        const char *p = g_body;
        char line[160];
        int body_floor = y + h - pad - bottom_rows * row_h;
        while (*p && ty + row_h <= body_floor) {
            take_line(&p, max_chars, line, (int)sizeof(line));
            bfont_draw(line, tx, ty, PAL_CLR(WHITE));
            ty += row_h;
        }
    }

    // Hint line at the bottom of the panel.
    const Resources *res = resources_current();
    const ResUI *ui = res ? &res->ui : NULL;
    if (g_kind == PK_TEXT_INPUT) {
        // Show current input value, a caret, and Enter/Esc hint.
        char typed[16];
        snprintf(typed, sizeof(typed), "%s_",
                 g_text_len > 0 ? g_text_buf : "");
        bfont_draw_centered(typed,
                            x + w / 2, y + h - row_h * 2 - 2, PAL_CLR(WHITE));
        bfont_draw_centered(ui ? ui->prompt_text_hint
                               : "(Enter to confirm / ESC cancel)",
                            x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
    } else if (g_kind == PK_NUMERIC && g_max_choice != 5) {
        char buf[32];
        if (ui) {
            char cbuf[12];
            snprintf(cbuf, sizeof cbuf, "%d", g_max_choice);
            ResTemplateVar v[] = { { "COUNT", cbuf } };
            resources_format_template(buf, sizeof buf,
                                      ui->prompt_numeric_range_hint, v, 1);
        } else {
            snprintf(buf, sizeof(buf), "(1-%d or ESC)", g_max_choice);
        }
        bfont_draw_centered(buf,
                            x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
    } else if (g_kind == PK_AB_CHOICE) {
        // Chrome-less  — body already names A) / B).
    } else {
        const char *hint;
        if (ui) {
            hint = (g_kind == PK_YES_NO) ? ui->prompt_yes_no_hint
                                         : ui->prompt_numeric_5_hint;
        } else {
            hint = (g_kind == PK_YES_NO) ? "(y/n)?" : "(1-5 or ESC)";
        }
        bfont_draw_centered(hint,
                            x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
    }
}
