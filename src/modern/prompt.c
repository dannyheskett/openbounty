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

    int row_h = BFONT_GLYPH_H + CL_UI;
    int pad = CL_PANEL_PAD_X;
    // One panel rect, the same one every dialog and menu uses (REQ-430h).
    int x = CL_PANEL_X;
    int y = CL_PANEL_Y;
    int w = CL_PANEL_W;
    int h = CL_PANEL_H;
    // In pixels, so a proportional face wraps by its own advances.
    int max_w = w - 2 * pad;

    // Reserve rows at the bottom for hint chrome (rendered after the body).
    //   text-input      -> 2 (typed value + hint), or 5 for the digit grid
    //   yes/no          -> 2 (the Yes and No rows)
    //   numeric         -> 1 (hint only)
    //   A/B choice      -> 0 (body names the keys; chrome-less)
    int bottom_rows;
    if (p->kind == PK_TEXT_INPUT)      bottom_rows = p->selector ? 5 : 2;
    else if (p->kind == PK_AB_CHOICE)  bottom_rows = 0;
    else if (p->kind == PK_YES_NO)     bottom_rows = 2;
    else                               bottom_rows = 1;

    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));

    int tx = x + pad;
    int ty = y + pad;

    if (p->header[0]) {
        bfont_draw(p->header, tx, ty, PAL_CLR(YELLOW));
        ty += row_h + 2;
    }

    // Body: render every line that fits inside the inner rect, leaving
    // bottom_rows free for the hint chrome at the very bottom. Body lines
    // advance by the glyph height with no leading, matching the message
    // dialog, so equal-length bodies render identically in both.
    {
        const char *q = p->body;
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
                            x + w / 2, y + h - row_h * 2 - 2, PAL_CLR(WHITE));
        if (p->selector) {
            int cw = 2 * BFONT_GLYPH_W, chh = BFONT_GLYPH_H + 2 * CL_UI;
            textsel_draw(p->ts, x + w - pad - textsel_w(true, cw) - 2 * CL_UI,
                         y + h - pad - textsel_h(true, chh), cw, chh,
                         PAL_CLR(YELLOW), PAL_CLR(DBLUE), TOUCH_LIST_TEXTSEL);
        } else {
            bfont_draw_centered(ui ? ui->prompt_text_hint
                                   : "(Enter to confirm / ESC cancel)",
                                x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
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
                            x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
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
                            x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
    }
}
