// autoplay/report.h
//
// Run reporting: the human-readable decision log and the objective
// checklist. Phase 2 emits decision-log lines (e.g. "moved toward chest",
// "opened chest: gold gained") and a checklist summary
// (done/outstanding per objective).
//
// Engine-only; depends only on goals.h + libc stdio.

#ifndef OB_AUTOPLAY_REPORT_H
#define OB_AUTOPLAY_REPORT_H

#include <stdbool.h>

#include "game.h"
#include "goals.h"

// Reporting sink. The per-turn decision log is ALWAYS streamed to stdout (no
// verbose gate); the counter accumulates for the final summary.
typedef struct {
    int  log_lines;     // number of decision-log lines emitted
} Report;

// Initialize a report.
void report_init(Report *r);

// Emit one decision-log line (printf-style). Always counted and always printed.
// A newline is appended.
void report_log(Report *r, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

// Print the objective checklist (each item ticked / outstanding)
// for `set` against the current Game state. Always prints (it's the run's
// summary, not per-turn chatter); used at run end.
void report_checklist(const Report *r, const PlanStepSet *set, const Game *g);

#endif
