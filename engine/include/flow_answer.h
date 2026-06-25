// engine/include/flow_answer.h
//
// The answer a resolved player decision carries — extracted into a leaf header
// so BOTH flow_resolve.h (the apply-cores) and player_io.h (the request queue,
// embedded in Game) can use it without an include cycle. This header pulls only
// <stdbool.h>; it has no back-edge to game.h.
//
// PromptAnswer mirrors the shell's PromptResult (src/prompt.h) 1:1; the shell
// adapter translates one to the other so the shell type never leaks into the
// engine.

#ifndef OB_ENGINE_FLOW_ANSWER_H
#define OB_ENGINE_FLOW_ANSWER_H

// Engine mirror of src/prompt.h's PromptResult. Same ordering/semantics; the
// shell adapter translates PromptResult -> PromptAnswer 1:1.
typedef enum {
    FLOW_ANS_NONE = 0,
    FLOW_ANS_YES,
    FLOW_ANS_NO,
    FLOW_ANS_CANCEL,    // ESC
    FLOW_ANS_1,         // numeric / AB picker: 1..5 -> FLOW_ANS_1..5; A=1, B=2
    FLOW_ANS_2,
    FLOW_ANS_3,
    FLOW_ANS_4,
    FLOW_ANS_5,
} PromptAnswer;

// The complete answer to a resolved prompt. `number` carries the typed value
// for FLOW_RECRUIT (the requested recruit count); ignored by other flows.
typedef struct {
    PromptAnswer kind;
    int          number;
} FlowAnswer;

#endif // OB_ENGINE_FLOW_ANSWER_H
