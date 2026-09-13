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
#include "modern/mlist.h"
#include "lattice.h"
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

// Question dialogs (REQ-430n): the header and body, a lattice band, then the
// answers -- standard select rows (Yes/No, or one row per choice), or the count
// stepper for a count. The panel runs the width of the map pane inside its
// margin and sits on the pane's bottom edge, so the hero at the centre stays in
// view; it is as tall as its text and answers, and when that would pass the top
// of the pane the answer rows scroll instead.

typedef struct { const PromptView *p; int max_w; } RowCtx;

static bool prompt_row(void *ctx, int i, char *label, char *right, int cap) {
    const RowCtx *rc = (const RowCtx *)ctx;
    const PromptView *p = rc->p;
    right[0] = '\0';
    if (p->kind == PK_YES_NO) {
        const Resources *res = resources_current();
        const char *s = res ? (i == 0 ? res->ui.prompt_yes : res->ui.prompt_no)
                            : (i == 0 ? "Yes" : "No");
        snprintf(label, (size_t)cap, "%s", s);
        return true;
    }
    // A choice keeps to one line: the part that fits the row.
    const char *q = p->choices[i];
    if (bfont_take_line(&q, rc->max_w, label, cap) <= 0) label[0] = '\0';
    return true;
}

void modern_prompt_draw(const PromptView *p) {
    if (!p || p->kind == PK_NONE) return;

    const int BAND = 4;
    const int INSET = ML_PAD + 4;
    int line_h = BFONT_GLYPH_H + 2;
    int sp = ml_space();
    int x = CL_MAP_X + sp, w = CL_MAP_W - 2 * sp;
    int bottom = CL_MAP_Y + CL_MAP_H - sp;
    int max_h = CL_MAP_H - 2 * sp;
    int text_w = w - 2 * INSET;

    bool choices = (p->kind == PK_NUMERIC || p->kind == PK_AB_CHOICE) && p->choice_n > 0;
    const char *body = choices ? p->lead : p->body;

    // Text height: header, then the wrapped body.
    int text_lines = p->header[0] ? 1 : 0;
    {
        const char *q = body;
        char probe[160];
        while (*q && bfont_take_line(&q, text_w, probe, (int)sizeof probe) > 0) text_lines++;
    }
    int rows = (p->kind == PK_YES_NO) ? 2 : choices ? p->choice_n : 0;
    bool stepper = (p->kind == PK_TEXT_INPUT);
    int answers_h = stepper ? 2 * INSET + ml_stepper_height()
                  : rows > 0 ? ml_list_height(rows) : 0;
    int text_h = 2 * INSET + text_lines * line_h;
    int h = text_h + (answers_h > 0 ? BAND + answers_h : 0);
    if (h > max_h) {
        // Keep at least one answer row; the rest scroll.
        int room = max_h - text_h - BAND;
        int min_ans = stepper ? answers_h : ml_list_height(1);
        if (room < min_ans) room = min_ans;
        if (answers_h > room) answers_h = room;
        h = text_h + BAND + answers_h;
        if (h > max_h) { text_h -= h - max_h; h = max_h; }
    }
    int y = bottom - h;

    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));

    // Text.
    int tx = x + INSET, ty = y + INSET;
    int text_floor = y + text_h - INSET;
    if (p->header[0] && ty + line_h <= text_floor) {
        bfont_draw(p->header, tx, ty, PAL_CLR(YELLOW));
        ty += line_h;
    }
    {
        const char *q = body;
        char line[160];
        while (*q && ty + line_h <= text_floor + 2) {
            if (bfont_take_line(&q, text_w, line, (int)sizeof line) <= 0) break;
            bfont_draw(line, tx, ty, PAL_CLR(WHITE));
            ty += line_h;
        }
    }
    if (answers_h <= 0) return;

    int ay = y + text_h;
    lattice_band_h(x, ay, w, BAND);
    ay += BAND;

    if (stepper) {
        const Resources *res = resources_current();
        char nb[16], mb[16], text[64];
        snprintf(nb, sizeof nb, "%d", p->step_value);
        snprintf(mb, sizeof mb, "%d", p->step_max > 0 ? p->step_max : 0);
        ResTemplateVar v[] = { { "COUNT", nb }, { "MAX", mb } };
        resources_format_template(text, sizeof text,
                                  res ? res->banners.castle_count_of : "%COUNT% of %MAX%", v, 2);
        ml_stepper_draw(x + INSET, ay + INSET, w - 2 * INSET, text);
        return;
    }
    RowCtx ctx = { p, w - 2 * ML_PAD };
    int cursor = (p->kind == PK_YES_NO) ? p->yn_cursor : p->choice_cursor;
    ml_list_draw(x, ay, w, answers_h, rows, cursor, prompt_row, &ctx,
                 TOUCH_LIST_PROMPT, PAL_CLR(DBLUE));
}
