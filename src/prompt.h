#ifndef OB_PROMPT_H
#define OB_PROMPT_H

#include <stdbool.h>
#include "game.h"

// Bottom-frame modal prompts used by flows (ask_quit,
// ask_search, dismiss_army, navigate_continent, ...). One prompt active
// at a time. While a prompt is active, input and drawing are redirected
// to it; the overworld keeps rendering underneath.
//
// Flow:
//   prompt_yes_no_open(...) or prompt_numeric_open(...) -> state active
//   main loop calls prompt_update() every frame until it returns non-NONE
//   caller switches on the returned result and clears state

typedef enum {
    PROMPT_RESULT_NONE = 0,
    PROMPT_RESULT_YES,
    PROMPT_RESULT_NO,
    PROMPT_RESULT_CANCEL,   // ESC
    PROMPT_RESULT_1,        // numeric picker: 1..5 -> PROMPT_RESULT_1..5
    PROMPT_RESULT_2,
    PROMPT_RESULT_3,
    PROMPT_RESULT_4,
    PROMPT_RESULT_5,
} PromptResult;

// Open a yes/no prompt. `header` is bold/yellow; `body` is the question
// text (word-wrapped). A hint line from the pack (ui.prompt_yes_no_hint,
// "(y/n)?" in the reference pack) is centered at the panel bottom.
void prompt_yes_no_open(const char *header, const char *body);

// Open a 1..5 numeric picker with the given `header` and `body`.
// Useful for dismiss_army (pick a slot), instant_army targeting, etc.
void prompt_numeric_open(const char *header, const char *body, int max_choice);

// Open an A/B letter picker. Returns PROMPT_RESULT_1 for A, PROMPT_RESULT_2
// for B (so callers can share the numeric-picker dispatch path). Used by
//  gold_or_leadership chest prompt ( /
//  two_choices), where the body labels its options A) and B).
void prompt_ab_open(const char *header, const char *body);

// Open a multi-digit numeric entry prompt (0-9, Backspace, Enter, Esc).
// Accepts numbers up to `max_digits`. Max accepted value is `max_value`;
// typing beyond it is rejected.
void prompt_text_input_open(const char *header, const char *body,
                            int max_digits, int max_value);

// Read the typed value after prompt_update() returned PROMPT_RESULT_YES
// from a text-input prompt. Returns 0 if no input was available.
int  prompt_text_input_value(void);

bool prompt_is_active(void);
void prompt_dismiss(void);

// Read-only accessors for the harness / state serializer. The kind is
// returned as a stable string ("yes_no" | "numeric" | "ab" | "text") or
// "none" when no prompt is up.
const char *prompt_kind_str(void);
const char *prompt_header_text(void);
const char *prompt_body_text(void);

// Poll for input. Returns PROMPT_RESULT_NONE if still waiting; any other
// value means the prompt dispatched and state was cleared.
PromptResult prompt_update(void);

// Render the prompt (if active) in the bottom frame. Called from the
// overlay after dialogs.
void prompt_draw(void);

#endif
