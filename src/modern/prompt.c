// src/modern/prompt.c
//
// The modal prompt for a pack that declared render.mode "modern" (REQ-430e,
// REQ-430n). A question with two answers -- Yes and No -- is the message box
// on the foot of the map (of the battlefield in a fight), with the face it
// names at its left; a question with a list of answers (a troop, a province,
// the treasure's two uses) or a count is a menu page. Every answer shows the
// key that gives it while the keyboard is in use.
//
// The DOS original's prompt is in src/legacy/prompt.c and is frozen.
//
// Called only through the dispatcher in src/prompt.c, which owns the state.

#include "prompt.h"
#include "prompt_impl.h"
#include "modern/mlayout.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include "modern/page.h"
#include "views.h"
#include "lattice.h"
#include "layout.h"
#include "ui.h"
#include "touch.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include "ob_types.h"
#include "pending.h"
#include "overlay_impl.h"
#include "tables.h"
#include <stdio.h>
#include <string.h>

typedef struct { const PromptView *p; int max_w; } RowCtx;

static bool prompt_row(void *ctx, int i, char *label, char *right, int cap) {
    const RowCtx *rc = (const RowCtx *)ctx;
    const PromptView *p = rc->p;
    const Resources *res = resources_current();
    right[0] = '\0';
    if (p->kind == PK_YES_NO) {
        snprintf(label, (size_t)cap, "%s", res ? (i == 0 ? res->ui.prompt_yes : res->ui.prompt_no)
                                               : (i == 0 ? "Yes" : "No"));
        if (i == 1) ml_exit_hint(right);
        else if (ml_keys_shown()) snprintf(right, 48, "Y");
        return true;
    }
    if (p->kind == PK_NUMERIC && i == p->choice_n) {      // Cancel, under the choices
        snprintf(label, (size_t)cap, "%s", res ? res->banners.count_cancel : "Cancel");
        ml_exit_hint(right);
        return true;
    }
    if (p->kind == PK_TEXT_INPUT) {                       // a count's answers
        snprintf(label, (size_t)cap, "%s", i == 0 ? (res ? res->banners.castle_continue : "Continue")
                                                  : (res ? res->banners.count_cancel : "Cancel"));
        if (i == 1) ml_exit_hint(right);
        return true;
    }
    // A choice takes up to two lines in its row; a second line is joined with
    // a newline, which the list draws below the first. Its key at the right:
    // its number, or its letter.
    if (ml_keys_shown()) {
        if (p->kind == PK_AB_CHOICE) snprintf(right, 48, "%c", 'A' + i);
        else                         snprintf(right, 48, "%d", i + 1);
    }
    const char *q = p->choices[i];
    char l1[96] = "", l2[96] = "";
    if (bfont_take_line(&q, rc->max_w, l1, (int)sizeof l1) <= 0) l1[0] = '\0';
    while (*q == ' ') q++;
    if (*q && bfont_take_line(&q, rc->max_w, l2, (int)sizeof l2) <= 0) l2[0] = '\0';
    if (*q && l2[0]) uk_mark_cut(l2, (int)sizeof l2, rc->max_w);
    if (l2[0]) snprintf(label, (size_t)cap, "%s\n%s", l1, l2);
    else       snprintf(label, (size_t)cap, "%s", l1);
    return true;
}

// An ask that named a face (PIO_ASK_FACE): the troop's picture.
static Texture2D ask_face(void) {
    if (prompt_req_kind() != PIO_ASK_FACE) return (Texture2D){ 0 };
    int idx = 0;
    int face = prompt_req_face(&idx);
    const Sprites *s = modern_overlay_sprites();
    if (!s || face != REQ_FACE_TROOP || idx < 0 || idx >= s->troop_count) return (Texture2D){ 0 };
    return s->troop_portrait[idx].id ? s->troop_portrait[idx] : s->troop_sprite[idx];
}

// A menu page's right-hand words: the hero's name.
static const char *hero_name(void) {
    const Game *g = modern_overlay_game();
    return g ? g->character.name : "";
}

void modern_prompt_draw(const PromptView *p) {
    if (!p || p->kind == PK_NONE) return;
    PageAnchor at = prompt_req_kind() == PIO_ASK_OVER_FIELD ? PAGE_FIELD_FOOT : PAGE_MAP_FOOT;
    RowCtx ctx = { p, page_menu_w() - 2 * UK_INSET - 4 * BFONT_GLYPH_W };
    if (p->kind == PK_YES_NO) {
        ctx.max_w = page_msg_w() - 2 * UK_INSET;
        page_question(p->header, p->body, p->yn_cursor, prompt_row, &ctx, TOUCH_LIST_PROMPT,
                      ask_face(), at);
        return;
    }
    // A list of answers, or a count: a menu page, its words where a menu's
    // description stands.
    GmPage q;
    memset(&q, 0, sizeof q);
    if (p->kind == PK_TEXT_INPUT) {
        ML_Rect b = page_menu_body(p->header, hero_name(), 0);
        ML_Rect a = { b.x + UK_INSET, b.y + UK_INSET, b.w - 2 * UK_INSET, b.h - 2 * UK_INSET };
        int y = uk_flow(a.x, a.y, a.w, a.x, 0, a.y + 3 * uk_line_h(), p->body, PAL_CLR(WHITE));
        ML_Rect c = { a.x, y + UK_INSET, a.w, 0 };
        uk_count(c, NULL, NULL, NULL, p->step_value, p->step_max > 0 ? p->step_max : 0);
        ML_Rect rows = { b.x, b.y, b.w, b.h };
        ml_rows_draw(rows, 2, 2, 0, prompt_row, &ctx, TOUCH_LIST_PROMPT);
        return;
    }
    int n = p->choice_n + (p->kind == PK_NUMERIC ? 1 : 0);
    q.n = n;
    q.foot = p->kind == PK_NUMERIC ? 1 : 0;
    for (int i = 0; i < n && i < GM_ROWS_MAX; i++)
        q.item[i] = (GmItem){ "", p->lead, "", 0, true };
    int cursor = p->choice_cursor;
    page_menu(&q, p->header, hero_name(), cursor, TOUCH_LIST_PROMPT, prompt_row, &ctx);
}
