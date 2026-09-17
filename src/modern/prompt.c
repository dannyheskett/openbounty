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

#include "prompt.h"
#include "prompt_impl.h"
#include "modern/mlayout.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
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
#include "pending.h"
#include "overlay_impl.h"
#include "tables.h"
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
    if (p->kind == PK_NUMERIC && i == p->choice_n) {      // modern: the Cancel row under the choices
        const Resources *res = resources_current();
        snprintf(label, (size_t)cap, "%s", res ? res->banners.count_cancel : "Cancel");
        return true;
    }
    if (p->kind == PK_YES_NO) {
        const Resources *res = resources_current();
        const char *s = res ? (i == 0 ? res->ui.prompt_yes : res->ui.prompt_no)
                            : (i == 0 ? "Yes" : "No");
        snprintf(label, (size_t)cap, "%s", s);
        return true;
    }
    // A choice takes up to two lines in its row (a row is tall enough); a
    // second line is joined with a newline, which the list draws below the first.
    const char *q = p->choices[i];
    char l1[96] = "", l2[96] = "";
    if (bfont_take_line(&q, rc->max_w, l1, (int)sizeof l1) <= 0) l1[0] = '\0';
    while (*q == ' ') q++;
    if (*q && bfont_take_line(&q, rc->max_w, l2, (int)sizeof l2) <= 0) l2[0] = '\0';
    if (l2[0]) snprintf(label, (size_t)cap, "%s\n%s", l1, l2);
    else       snprintf(label, (size_t)cap, "%s", l1);
    return true;
}

// A question raised with a face: the result note's panel -- the same size, the
// portrait at 2x, the words beside it -- with Yes and No along its foot.
static void draw_yes_no_card(const PromptView *p, Texture2D face) {
    const Resources *res = resources_current();
    const char *labels[2] = { res ? res->ui.prompt_yes : "Yes",
                              res ? res->ui.prompt_no  : "No" };
    uk_result_ask(p->header, face, p->body, labels, 2, p->yn_cursor, TOUCH_LIST_PROMPT);
}

// An ask that named a face (PIO_ASK_FACE): the picture at 2x beside the words.
static Texture2D ask_face(const PromptView *p) {
    (void)p;
    if (prompt_req_kind() != PIO_ASK_FACE) return (Texture2D){ 0 };
    int idx = 0;
    int face = prompt_req_face(&idx);
    const Sprites *s = modern_overlay_sprites();
    if (!s || face != REQ_FACE_TROOP || idx < 0 || idx >= s->troop_count) return (Texture2D){ 0 };
    return s->troop_portrait[idx].id ? s->troop_portrait[idx] : s->troop_sprite[idx];
}

void modern_prompt_draw(const PromptView *p) {
    if (!p || p->kind == PK_NONE) return;
    // A Yes/No question is a card: its words, then Yes and No as buttons.
    if (p->kind == PK_YES_NO && ask_face(p).id) {
        draw_yes_no_card(p, ask_face(p));
        return;
    }

    const int BAND = 4;
    const int INSET = UK_INSET;
    int line_h = BFONT_GLYPH_H + 2;
    int sp = ml_space();
    ML_Rect area = ml_area();     // centred on what is behind it
    {
        // One look for every question: uk_ask on the map pane's foot, the same
        // shape centred (uk_ask_over) over a place screen or the battlefield.
        bool over_map = !(area.w == ml_full().w && area.h == ml_full().h);
        bool choices_map = (p->kind == PK_NUMERIC || p->kind == PK_AB_CHOICE) && p->choice_n > 0;
        if (p->kind == PK_YES_NO || choices_map) {
            const char *words = choices_map ? p->lead : p->body;
            int tw = over_map ? uk_message_text_w() : uk_ask_over_text_w();
            static char wl[12][200];
            const char *lines[12];
            int nl = 0;
            const char *q = words ? words : "";
            while (nl < 12 && *q && bfont_take_line(&q, tw, wl[nl], (int)sizeof wl[nl]) > 0) { lines[nl] = wl[nl]; nl++; }
            RowCtx ctx = { p, tw };
            int n_rows = p->kind == PK_YES_NO ? 2 : p->choice_n + (p->kind == PK_NUMERIC ? 1 : 0);
            int cursor = p->kind == PK_YES_NO ? p->yn_cursor : p->choice_cursor;
            // A question with a title but no other words: the title is the
            // question, so it reads as white words, not a gold title.
            const char *title = p->header;
            if (nl == 0 && title && title[0]) { lines[0] = title; nl = 1; title = NULL; }
            if (over_map) uk_ask(title, lines, nl, n_rows, cursor, prompt_row, &ctx, TOUCH_LIST_PROMPT);
            else          uk_ask_over(title, lines, nl, n_rows, cursor, prompt_row, &ctx, TOUCH_LIST_PROMPT);
            return;
        }
    }
    // Over the map: a band along the pane's foot, so the hero stays in view.
    // Over a screen (a place, combat): an in-lay in its middle.
    bool inlay = area.w == ml_full().w && area.h == ml_full().h;
    int x = area.x + sp, w = area.w - 2 * sp;
    if (inlay) { w = UK_INLAY_W; x = area.x + (area.w - w) / 2; }
    int bottom = area.y + area.h - sp;
    int max_h = area.h - 2 * sp;
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
    int y = inlay ? area.y + (area.h - h) / 2 : bottom - h;

    if (inlay) uk_dim();
    uk_panel(x, y, w, h);

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
                 TOUCH_LIST_PROMPT, uk_ink());
}
