// src/shell_demo.h
//
// Shell adapter for DEMO MODE -- the human-like player agent (demo/). This is
// the one place allowed to know both the shell and demo/: the dependency
// arrow is shell -> demo -> engine. The agent plays the LIVE game forward
// (no plan, no replay); the shell adds rendering and a watchable pace.
//
// Lifecycle: main.c calls shell_demo_begin() once after the game is booted
// (when --demo was passed), then shell_demo_tick() once per frame in place of
// human adventure input. shell_demo_end() releases the agent state.

#ifndef OB_SHELL_DEMO_H
#define OB_SHELL_DEMO_H

#include <stdbool.h>

#include "shell_ctx.h"

bool shell_demo_begin(ShellCtx *ctx);

// True while demo mode owns the frames (combat_replay paces off demo's own
// beat instead of autoplay's when this is set).
bool shell_demo_active(void);
double shell_demo_step_delay(void);
double shell_demo_read_dwell(void);

// Drive one frame. Sets *out_done when the run ended (won / lost / stuck).
// Returns true while demo mode owns the frame.
bool shell_demo_tick(ShellCtx *ctx, double now_s, bool *out_done);

// One-line result summary for the hand-off dialog.
void shell_demo_summary(const ShellCtx *ctx, char *out, int cap);

void shell_demo_end(void);

#endif
