// src/prompt_impl.h
//
// The modal prompt's two draw paths. src/prompt.c owns the prompt state and
// the state machine -- which prompt is up, what the player has typed, what a
// key means -- and hands a read-only view of it to whichever panel draws:
//
//   src/legacy/prompt.c  -- FROZEN. The DOS original's bottom-frame prompt.
//   src/modern/prompt.c  -- where modern UI work happens.
//
// Only the DRAWING forks. The state machine does not: one prompt, one answer,
// whatever the pack.

#ifndef OB_PROMPT_IMPL_H
#define OB_PROMPT_IMPL_H

#include "textsel.h"
#include <stdbool.h>

typedef enum {
    PK_NONE = 0,
    PK_YES_NO,
    PK_NUMERIC,
    PK_AB_CHOICE,
    PK_TEXT_INPUT,
} PromptKind;

// A read-only window onto the live prompt, valid for the frame it is read in.
typedef struct {
    PromptKind     kind;
    const char    *header;
    const char    *body;
    int            max_choice;   // numeric picker: the top of the range
    int            yn_cursor;    // modern yes/no rows: 0 = Yes, 1 = No
    bool           selector;     // modern: the letter selector is up
    const char    *text_buf;     // text input: what has been typed
    int            text_len;
    const TextSel *ts;           // modern: the digit grid's cursor
    // Modern numeric and A/B prompts: one row per choice. `lead` is the body
    // without the choice lines; each choice is a label and the answer it gives
    // (1 = PROMPT_RESULT_1).
    const char    *lead;
    int            choice_n;
    const char   (*choices)[96];
    int            choice_cursor;
    // Modern count entry: the stepper's value and its top.
    int            step_value;
    int            step_max;
} PromptView;

const PromptView *prompt_view(void);

void legacy_prompt_draw(const PromptView *p);
void modern_prompt_draw(const PromptView *p);

#endif
