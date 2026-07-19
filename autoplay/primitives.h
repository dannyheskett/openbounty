// autoplay/primitives.h
//
// The executor entry (AP-080): execute_why dispatches a PlanStep by kind to
// its primitive helper (AP-081). Primitives orchestrate only; engine touches
// live in the shared helpers.

#ifndef OB_AUTOPLAY_PRIMITIVES_H
#define OB_AUTOPLAY_PRIMITIVES_H

#include "exec.h"
#include "goals.h"

// Runaway watchdog for the executor round loop (AP-102, R-A).
#define EXEC_MAX_ROUNDS 12

// Translate one PlanStep into one Primitive and run the bounded move/act loop,
// returning a display why-string and the machine-readable cause (AP-080).
bool execute_why(ExecCtx *ctx, const PlanStep *step,
                 char *why, int why_sz, ExecCause *out_cause);

// The stranding pre-gate (AP-051): skip an objective whose end tile offers no
// legal exit right now.
bool exec_step_strands(ExecCtx *ctx, const PlanStep *step);

#endif
