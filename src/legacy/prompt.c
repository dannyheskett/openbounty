// src/legacy/prompt.c -- FROZEN.
//
// The modal prompt exactly as the DOS original draws it: KB_BottomFrame at a
// fixed 30 characters by 8 rows, the body wrapped to the column budget, and a
// single centred hint line. No cursor rows, no letter selector -- those are
// modern additions and live in src/modern/prompt.c. Its behaviour is the spec,
// so nothing here changes to serve a modern need.
//
// Called only through the dispatcher in src/prompt.c, which owns the state.

#include "prompt_impl.h"
#include "layout.h"
#include "ui.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include "raylib.h"
#include <stdio.h>

void legacy_prompt_draw(const PromptView *p) {
    if (!p || p->kind == PK_NONE) return;

    int row_h = BFONT_GLYPH_H + CL_UI;
    int pad = CL_PANEL_PAD_X;   // 1px: the panel holds exactly CL_PANEL_COLS glyphs
    // KB_BottomFrame at a FIXED size: 30 chars wide x 8 chars tall + a few
    // extra pixels. Width matches the map area; the sidebar stays visible to
    // the right. Body text starts at the top of the inner area; short content
    // leaves blank rows below.
    int x = CL_PANEL_X;
    int y = CL_PANEL_Y;
    int w = CL_PANEL_W;
    int h = CL_PANEL_H;
    // Fixed by layout, not (w - 2*pad): the panel's margin is one-sided.
    // See CL_PANEL_COLS in layout.h.
    int max_w = CL_PANEL_COLS * BFONT_GLYPH_W;

    // Reserve rows at the bottom for hint chrome (rendered after the body).
    //   text-input      -> 2 (typed value + hint)
    //   yes/no, numeric -> 1 (hint only)
    //   A/B choice      -> 0 (body names the keys; chrome-less)
    int bottom_rows;
    if (p->kind == PK_TEXT_INPUT)      bottom_rows = 2;
    else if (p->kind == PK_AB_CHOICE)  bottom_rows = 0;
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
    // bottom_rows free for the hint chrome at the very bottom.
    //
    // Body lines advance by BFONT_GLYPH_H (8px), NOT row_h (9px). The panel's
    // 60px content area (68 - 2*pad) holds exactly 7 glyph rows at 8px but only 6
    // at 9px -- the 1px-per-line leading of row_h clipped the 7th line of the
    // 7-line chest "gold / distribute to peasants" choice. The message-dialog
    // path already steps body text by GH=8; match it so equal-length bodies
    // render identically in both. row_h still spaces the header + hint.
    {
        const char *q = p->body;
        char line[160];
        int body_step  = BFONT_GLYPH_H;                 // 8px (no leading)
        int body_floor = y + h - pad - bottom_rows * row_h;
        while (*q && ty + body_step <= body_floor) {
            bfont_take_line(&q, max_w, line, (int)sizeof(line));
            bfont_draw(line, tx, ty, PAL_CLR(WHITE));
            ty += body_step;
        }
    }

    // Hint line at the bottom of the panel.
    const Resources *res = resources_current();
    const ResUI *ui = res ? &res->ui : NULL;
    if (p->kind == PK_TEXT_INPUT) {
        // Show current input value, a caret, and Enter/Esc hint.
        char typed[16];
        snprintf(typed, sizeof(typed), "%s_",
                 p->text_len > 0 ? p->text_buf : "");
        bfont_draw_centered(typed,
                            x + w / 2, y + h - row_h * 2 - 2, PAL_CLR(WHITE));
        bfont_draw_centered(ui ? ui->prompt_text_hint
                               : "(Enter to confirm / ESC cancel)",
                            x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
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
    } else {
        const char *hint;
        if (ui) {
            hint = (p->kind == PK_YES_NO) ? ui->prompt_yes_no_hint
                                          : ui->prompt_numeric_5_hint;
        } else {
            hint = (p->kind == PK_YES_NO) ? "(y/n)?" : "(1-5 or ESC)";
        }
        bfont_draw_centered(hint,
                            x + w / 2, y + h - row_h - 2, PAL_CLR(YELLOW));
    }
}
