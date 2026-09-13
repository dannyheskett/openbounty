// src/modern/prompt.c
//
// The modal prompt for a pack that declared render.mode "modern": the body
// wraps by the face's own advances, yes/no answers become two cursor rows
// (REQ-430e), and a numeric entry can put up the letter selector's digit grid
// instead of a typed line (REQ-430f). Modern UI work happens here.
//
// The DOS original's prompt is in src/legacy/prompt.c and is frozen.
//
// Called only through the dispatcher in src/prompt.c, which owns the state.

#include "prompt_impl.h"
#include "modern/mlayout.h"
#include "layout.h"
#include "ui.h"
#include "select.h"
#include "textsel.h"
#include "touch.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include "raylib.h"
#include <stdio.h>

void modern_prompt_draw(const PromptView *p) {
    if (!p || p->kind == PK_NONE) return;

    int row_h = BFONT_GLYPH_H;
    int pad = ML_PAD;

    // Numeric and A/B prompts answer by rows, one per choice, each as many
    // lines as its label wraps to; the body above them is the lead text.
    bool choices = (p->kind == PK_NUMERIC || p->kind == PK_AB_CHOICE) && p->choice_n > 0;
    const char *body = choices ? p->lead : p->body;

    // Rows kept at the bottom for the answer chrome, drawn after the body.
    //   text-input      -> 2 (typed value + hint), or 5 for the digit grid
    //   yes/no          -> 2 (the Yes and No rows)
    //   numeric, A/B    -> the choice rows' lines
    int bottom_rows = 0;
    if (p->kind == PK_TEXT_INPUT)      bottom_rows = p->selector ? 5 : 2;
    else if (p->kind == PK_YES_NO)     bottom_rows = 2;

    // The small band along the bottom of the map pane when the whole prompt
    // fits it, so the hero at the centre tile stays in view while the question
    // is answered; the large rect when it does not (REQ-430j). The same rule
    // as a message dialog, with the answer rows counted in.
    ML_Rect rr = ml_small();
    int choice_lines[8] = { 0 };
    for (int pass = 0; pass < 2; pass++) {
        if (choices) {
            bottom_rows = 0;
            for (int i = 0; i < p->choice_n && i < 8; i++) {
                const char *q = p->choices[i];
                char probe[160];
                int n = 0;
                while (*q && bfont_take_line(&q, rr.w - 4 * pad, probe, (int)sizeof probe) > 0) n++;
                choice_lines[i] = n > 0 ? n : 1;
                bottom_rows += choice_lines[i];
            }
        }
        if (pass == 1) break;
        int max_w_small = rr.w - 2 * pad;
        int need = (p->header[0] ? 1 : 0) + bottom_rows;
        const char *q = body;
        char probe[160];
        while (*q && bfont_take_line(&q, max_w_small, probe, (int)sizeof probe) > 0) need++;
        if (need <= ml_lines(rr)) break;
        rr = ml_large();
    }
    int x = rr.x, y = rr.y, w = rr.w, h = rr.h;
    int max_w = w - 2 * pad;

    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));

    int tx = x + pad;
    int ty = y + pad;

    if (p->header[0]) {
        bfont_draw(p->header, tx, ty, PAL_CLR(YELLOW));
        ty += row_h;
    }

    // Body: render every line that fits inside the inner rect, leaving
    // bottom_rows free for the hint chrome at the very bottom. Body lines
    // advance by the glyph height with no leading, matching the message
    // dialog, so equal-length bodies render identically in both.
    {
        const char *q = body;
        char line[160];
        int body_step  = BFONT_GLYPH_H;
        int body_floor = y + h - pad - bottom_rows * row_h;
        while (*q && ty + body_step <= body_floor) {
            bfont_take_line(&q, max_w, line, (int)sizeof(line));
            bfont_draw(line, tx, ty, PAL_CLR(WHITE));
            ty += body_step;
        }
    }

    // Hint chrome at the bottom of the panel.
    const Resources *res = resources_current();
    const ResUI *ui = res ? &res->ui : NULL;
    if (p->kind == PK_TEXT_INPUT) {
        // Show current input value, a caret, and Enter/Esc hint.
        char typed[16];
        snprintf(typed, sizeof(typed), "%s_",
                 p->text_len > 0 ? p->text_buf : "");
        bfont_draw_centered(typed,
                            x + w / 2, y + h - pad - row_h * 2, PAL_CLR(WHITE));
        if (p->selector) {
            int cw = 2 * BFONT_GLYPH_W, chh = BFONT_GLYPH_H + 2 * CL_UI;
            textsel_draw(p->ts, x + w - pad - textsel_w(true, cw) - 2 * CL_UI,
                         y + h - pad - textsel_h(true, chh), cw, chh,
                         PAL_CLR(YELLOW), PAL_CLR(DBLUE), TOUCH_LIST_TEXTSEL);
        } else {
            bfont_draw_centered(ui ? ui->prompt_text_hint
                                   : "(Enter to confirm / ESC cancel)",
                                x + w / 2, y + h - pad - row_h, PAL_CLR(YELLOW));
        }
    } else if (choices) {
        // One row per choice; a long label wraps inside its own bar.
        int ry = y + h - pad - bottom_rows * row_h;
        for (int i = 0; i < p->choice_n && i < 8; i++) {
            int rh = choice_lines[i] * row_h;
            bool sel = (p->choice_cursor == i);
            int rx = x + pad, rw = w - 2 * pad;
            if (sel) DrawRectangle(rx, ry, rw, rh, PAL_CLR(YELLOW));
            const char *q = p->choices[i];
            char line[160];
            int ly = ry;
            while (*q && bfont_take_line(&q, w - 4 * pad, line, (int)sizeof line) > 0) {
                bfont_draw(line, rx + pad, ly, sel ? PAL_CLR(DBLUE) : PAL_CLR(YELLOW));
                ly += row_h;
            }
            touch_region_row(rx, ry, rw, rh, TOUCH_LIST_PROMPT, i);
            ry += rh;
        }
    } else if (p->kind == PK_NUMERIC && p->max_choice != 5) {
        char buf[32];
        if (ui) {
            char cbuf[12];
            snprintf(cbuf, sizeof cbuf, "%d", p->max_choice);
            ResTemplateVar v[] = { { "COUNT", cbuf } };
            resources_format_template(buf, sizeof buf,
                                      ui->prompt_numeric_range_hint, v, 1);
        } else {
            snprintf(buf, sizeof(buf), "(1-%d or ESC)", p->max_choice);
        }
        bfont_draw_centered(buf,
                            x + w / 2, y + h - pad - row_h, PAL_CLR(YELLOW));
    } else if (p->kind == PK_AB_CHOICE) {
        // Chrome-less -- the body already names A) / B).
    } else if (p->kind == PK_YES_NO) {
        // Two selectable rows in place of the "(y/n)?" hint.
        const char *labels[2] = { "Yes", "No" };
        int ry = y + h - pad - 2 * row_h;
        for (int i = 0; i < 2; i++) {
            sel_row(x + pad, ry + i * row_h, w - 2 * pad, row_h, x + pad + 2 * CL_UI, labels[i],
                    p->yn_cursor == i, PAL_CLR(YELLOW), PAL_CLR(DBLUE), TOUCH_LIST_PROMPT, i);
        }
    } else {
        const char *hint = ui ? ui->prompt_numeric_5_hint : "(1-5 or ESC)";
        bfont_draw_centered(hint,
                            x + w / 2, y + h - pad - row_h, PAL_CLR(YELLOW));
    }
}
