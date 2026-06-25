// src/shell_autoplay.h
//
// Shell-side adapter that drives the rendered game with the autoplay planner
// (visible mode). This is the ONE place allowed to know both
// the shell and autoplay: the dependency arrow is shell -> autoplay -> engine.
// It calls the same autoplay_step() + GameStep() the headless runner
// uses, so the visible run reproduces the headless decision sequence for a
// given seed; the shell only adds rendering and pacing.
//
// Lifecycle: main.c calls shell_autoplay_begin() once after the game is booted
// (when --autoplay was passed), then shell_autoplay_tick() once per frame in
// place of human adventure input. shell_autoplay_end() frees the planner.

#ifndef OB_SHELL_AUTOPLAY_H
#define OB_SHELL_AUTOPLAY_H

#include <stdbool.h>

#include "shell_ctx.h"
#include "combat.h"   // CombatMode, CombatTurnRecord (for the combat animator)
#include "view_kind.h"   // ViewKind (for the view presenter)

// True if --autoplay engaged this session (set by shell_autoplay_begin).
bool shell_autoplay_active(void);

// The visible-mode combat animator (PlanCombatAnimator, WS-7): draws a recorded
// fight via RenderCombatRecord. Wired into the executor by shell_autoplay_begin;
// NULL-equivalent (no draw) under headless. `ctx` is the ShellCtx.
void shell_autoplay_combat_animator(void *ctx, CombatMode mode,
                                    const CombatTurnRecord *rec);

// The visible-mode VIEW presenter (PlanViewPresenter): shows the screen a
// view-raising primitive opened (e.g. the town screen for a siege purchase),
// walks the menu cursor to the action's row, and holds a read beat — the way a
// player sees it. State-inert (shell view stack only). NULL-equivalent under
// headless. `ctx` is the ShellCtx; `chosen_row` is the menu row (or -1 = derive).
void shell_autoplay_view_presenter(void *ctx, ViewKind view, int chosen_row);

// Current per-step pacing delay (seconds); the combat animator scales its beat
// off this so the demo pace also speeds/slows rendered fights.
double shell_autoplay_step_delay(void);

// Seconds to hold a readable beat (dialog/prompt/victory banner) during a paced
// replay. 0 in full-speed (test) replay; the demo's tuned dwell otherwise. The
// combat replay uses it to hold the post-fight Victory banner so the demo does
// not skip the win screen.
double shell_autoplay_read_dwell(void);

// Initialize the planner against the live game in `ctx`. Enumerates the FULL
// objective universe (all zones) — the binary has exactly one behavior; the
// single-zone regression baseline is exercised by the test suite through the
// internal plan_build API, never by a CLI knob. Visible play replays at full
// speed with no prompt dwell. Returns false if the planner could not be
// created (autoplay then stays inactive).
bool shell_autoplay_begin(ShellCtx *ctx);

// Like shell_autoplay_begin, but `demo` selects DEMO (attract) mode: build a
// SHORT, CURATED zone-0 plan (a couple of chests, a fight, a misc pickup — see
// the DemoCap in shell_autoplay.c) and replay it at a TUNED, WATCHABLE pace with
// a read-dwell on each prompt, so a viewer can follow what's happening. The demo
// plan is PARTIAL by design (it does not solve the game) and never recovers the
// scepter. shell_autoplay_begin(ctx) == shell_autoplay_begin_ex(ctx, false).
bool shell_autoplay_begin_ex(ShellCtx *ctx, bool demo);

// True if the active run is the curated DEMO (attract) plan rather than the full
// autoplay solve. main.c uses this to LOOP the demo (re-boot + replay) instead of
// handing off when the run completes.
bool shell_autoplay_is_demo(void);

// Per-frame driver. Called from the main loop instead of human adventure
// input when autoplay is active. Advances the plan by at most one decision per
// `step_delay_s` window: answers a pending prompt (and dismisses the shell's
// prompt UI), or takes one GameStep toward the current objective. Returns true
// if autoplay consumed this frame's turn (the caller should skip human input).
// Sets *out_done true when the run has no actionable objectives left.
bool shell_autoplay_tick(ShellCtx *ctx, double now_s, bool *out_done);

// Fill a short human-readable summary of the run's outcome (objective counts;
// a verdict once the planner emits one) for the hand-off dialog.
// Writes "" if no planner is active. `cap` is the size of `out`.
void shell_autoplay_summary(const ShellCtx *ctx, char *out, int cap);

// Free the planner. Safe to call when inactive.
void shell_autoplay_end(void);

#endif
