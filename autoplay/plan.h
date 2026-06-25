// autoplay/plan.h
//
// Plan construction by simulation. The planner produces
// an ORDERED, PROVEN-WINNABLE sequence of objectives — the committed plan —
// before any of it touches the live world. Objectives are admitted one at a
// time: to consider a candidate, simulate the resulting plan on a discarded
// copy of the world (worldsnap.h, §10.1) using the SAME decision code the
// executor runs (autoplay_step + GameStep), and admit it iff that
// simulation succeeds (hero never dies, no committed fight lost, the objective
// is completed). Incremental replay: cache the snapshot after each
// admitted objective and simulate only the new tail.
//
// The phase emits the committed plan plus the run verdict (§2):
//   CLEARED  — every in-scope objective admitted.
//   PARTIAL  — some objectives could not be admitted, but nothing admitted is
//              unwinnable (not a pack defect; "as far as the planner got").
//   FAILED   — a committed objective's required fight proved unwinnable.
//
// This is the PLAN phase only. Executing the committed plan (and removing the
// in-line entry-time prediction gate) is Phase 10; until then the live
// run loop is unchanged. plan_build is standalone and testable: a seed produces
// a logged committed plan + verdict.
//
// Engine-only: Game/Map/Fog/Resources are engine types.

#ifndef OB_AUTOPLAY_PLAN_H
#define OB_AUTOPLAY_PLAN_H

#include <stdbool.h>

#include "autoplay.h"   // AutoplayVerdict
#include "goals.h"      // PlanStepSet / STEP_MAX
#include "worldsnap.h"  // WorldSnapshot (per-objective checkpoints)
#include "recording.h"  // RecBuf / CombatRecList / RecSink (WS-4)
#include "view_kind.h"  // ViewKind (visible-mode view presenter hook)

// The committed plan IS the ordered sequence of admitted goal indices. The
// per-objective world checkpoints used during construction live in a
// single rolling snapshot inside plan_build, not here — embedding a full
// WorldSnapshot (~950 KB, dominated by Map) per slot would make Plan hundreds
// of MB. Phase 10's checkpoint assertion, if it needs per-objective
// snapshots, regenerates them by replaying the committed sequence rather than
// storing them all.
//
// The committed plan and the run verdict.
typedef struct {
    AutoplayVerdict verdict;                 // CLEARED / PARTIAL / FAILED

    // RECORDED PRIMITIVE SEQUENCE (WS-4). Captured during the planner's single
    // proving simulation: the exact moves / answers / direct actions that completed
    // each admitted step, plus the per-fight CombatTurnRecords. The executor APPLIES
    // this with no planner / nav / prediction / combat at execute time, so headless
    // and visible reproduce the identical resolution. Heap-owned — freed by plan_free.
    RecBuf        rec;
    CombatRecList combat;
} Plan;

// Release the heap the Plan owns (rec, combat records). Safe with NULL / a
// zeroed Plan. Every autoplay / shell_autoplay_end / test teardown must call
// it after it is done with the committed plan.
void plan_free(Plan *p);

// Re-run the pending combat on the live world during execution (WS-4): rebuilds
// the target from the live pending flow and runs the deterministic resolver with
// the autoplay policy, reproducing the IDENTICAL army/spoils writeback + outcome
// the simulation recorded (the part the flow-apply core does not do). Must be
// called BEFORE player_io_answer, matching the sim order. *out_rec (optional)
// receives the per-turn record for the visible animator. Implemented in autoplay.c.
void autoplay_apply_recorded_combat(Game *g, CombatTurnRecord *out_rec);

// Apply a recorded direct engine action on the live world (WS-4 executor). Calls
// the same engine function the planner used (garrison-weakest / recruit-home /
// rent-boat / take-contract / buy-siege / travel-zone); reproduces exactly
// because the executor reaches the byte-identical world state. `map`/`fog` are
// needed only by RA_TRAVEL_ZONE (GameSwitchZone reloads the map and swaps the
// per-zone fog); every other action ignores them. Implemented in autoplay.c.
void autoplay_apply_rec_action(Game *g, Map *map, Fog *fog, RecActionKind kind,
                               const char *id, int x, int y);

// ---- Execute phase (§5.3) -------------------------------------------------
// Replay the committed plan on the LIVE world as a DUMB APPLIER (WS-4). The plan
// recorded its proven primitive sequence during simulation (Plan.rec / .combat),
// so execution performs NO planner, NO nav search, NO prediction, and NO combat
// resolution: it applies each recorded primitive (move via GameStep, answer via
// player_io_answer with the recorded combat outcome, direct action via the same
// engine call). Because the world is byte-deterministic, the live world tracks
// the simulated world exactly — checked at each step boundary by boundary_fp.
//
// Driven ONE TURN AT A TIME so headless and visible share it verbatim:
// headless calls plan_exec_step in a tight loop; visible calls it once per pacing
// window and renders between turns. The only mode difference is that visible mode
// ANIMATES a fight's CombatTurnRecord before its outcome is applied (a passive
// draw that never touches world state). There is no second decision path.

// Visible-mode combat animator hook (WS-7). Called by the executor when a fight's
// recorded outcome is about to be applied, so the shell can draw the recorded
// per-turn battle. Engine-pure: the body lives in src/ and is set by the shell;
// NULL in headless (no draw). MUST be state-inert (draws a throwaway Combat).
typedef void (*PlanCombatAnimator)(void *ctx, CombatMode mode,
                                   const CombatTurnRecord *rec);

// Visible-mode VIEW presenter hook. Called by the executor right after a recorded
// MOVE stepped the hero onto a town gate (the engine raised VIEW_TOWN through the
// uniform queue, exactly as for a human), so the shell can SHOW the screen the way
// a player sees it: render the view and walk the menu cursor to `chosen_row`, hold
// a read beat, then dismiss — all on the shell's view stack (NOT in Game). NULL in
// headless (no draw; the executor just drains the REQ_VIEW). Like the combat
// animator it MUST be state-inert: it renders ONLY and NEVER executes a menu row —
// the transaction is a SEPARATE recorded primitive (e.g. RA_BUY_SIEGE). So no
// presenter-only mutation can make visible diverge from headless. `chosen_row` is
// the menu row the recorded action corresponds to (e.g. TOWN_ROW_SIEGE), or -1 for
// "no selection, just present".
typedef void (*PlanViewPresenter)(void *ctx, ViewKind view, int chosen_row);

// Per-turn executor state. Holds the committed plan + a flat cursor into the
// recorded primitive sequence. Caller allocates one (stack or heap). No planner.
typedef struct {
    const Plan *plan;            // committed plan being replayed (borrowed)
    int   rec_cursor;            // index into plan->rec.items[]
    bool  done;
    // Visible-mode animator (WS-7); NULL in headless.
    PlanCombatAnimator animate;
    void              *animate_ctx;
    // Visible-mode view presenter; NULL in headless. Shares animate_ctx.
    PlanViewPresenter  present_view;
} PlanExecutor;

// What one execute turn did.
typedef enum {
    PLAN_EXEC_RUNNING = 0,  // applied a primitive; call again next turn
    PLAN_EXEC_DONE,         // the whole recorded sequence has been applied
} PlanExecStatus;

// Begin executing `plan` on the live world. Resets the cursor to the first
// recorded primitive. `animate` and `present_view` (+ shared ctx) are the
// visible-mode combat animator and view presenter (BOTH NULL in headless). Does
// not itself touch the world.
void plan_exec_begin(PlanExecutor *ex, const Plan *plan,
                     PlanCombatAnimator animate, PlanViewPresenter present_view,
                     void *animate_ctx);

// Advance execution by exactly one recorded primitive against the live
// (g,map,fog,res): REC_MOVE -> GameStep; REC_ANSWER -> player_io_answer with the
// recorded outcome (visible mode animates a fight first); REC_ACTION -> the same
// engine call the planner made. Asserts boundary_fp at each step boundary.
// Returns PLAN_EXEC_RUNNING until the recording is exhausted, then DONE.
PlanExecStatus plan_exec_step(PlanExecutor *ex, Game *g, Map *map,
                              Fog *fog, const Resources *res);

#endif // OB_AUTOPLAY_PLAN_H
