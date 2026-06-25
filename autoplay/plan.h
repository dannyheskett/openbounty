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
#include "diag.h"       // DiagSink — structured decision trace (NULL = off)
#include "view_kind.h"  // ViewKind (visible-mode view presenter hook)

// The committed plan IS the ordered sequence of admitted goal indices. The
// per-objective world checkpoints used during construction live in a
// single rolling snapshot inside plan_build, not here — embedding a full
// WorldSnapshot (~950 KB, dominated by Map) per slot would make Plan hundreds
// of MB. Phase 10's checkpoint assertion, if it needs per-objective
// snapshots, regenerates them by replaying the committed sequence rather than
// storing them all.
//
// A recorded span for one admitted step: [rec_begin, rec_end) into Plan.rec,
// plus a checkpoint fingerprint of the world at the step boundary (WS-4).
typedef struct {
    int      rec_begin, rec_end;   // primitive range in Plan.rec for this step
    uint64_t boundary_fp;          // FNV of (Game+Map) at the start of this step
} PlanStepSpan;

// Why an in-scope objective could not be admitted (cited honest PARTIAL).
typedef enum {
    UNADM_NONE = 0,        // (admitted / done — not unadmitted)
    UNADM_NO_DOCK,         // tile-bound but no reachable dock reaches it (multi-trip boat)
    UNADM_NAV,             // not reachable on foot/one-boat and not dock-reachable
    UNADM_PREREQ,          // a hard prereq (e.g. contract) could not be chained
    UNADM_OTHER,           // unclassified
    UNADM_COMBAT,          // reached, but the fight is a predicted LOSS (army too weak)
    UNADM_NO_GOLD,         // a reaching dock exists but the boat fee is unaffordable
    UNADM_NO_GARRISON,     // siege WON but a single-stack army cannot garrison
    UNADM_MOBILITY,        // reached and completed, but the hero can't route home
    UNADM_OTHER_ZONE,      // lives in another zone; no committed travel reached it
    UNADM_TIME,            // not achievable within the remaining calendar days
    UNADM_FLY,             // sealed off from every surface route; flight (an
                           // all-flying army) is the only way in
    UNADM_SCEPTER_DEFER,   // the scepter finale, deliberately deferred until every
                           // other objective resolves (AP-066) — never a nav default
} UnadmReason;


// The committed plan and the run verdict.
typedef struct {
    PlanStepSet       set;                       // the enumerated objective universe
    int           admitted[STEP_MAX];        // ordered admitted goal indices
    int           admitted_count;
    int           unadmitted[STEP_MAX];      // goal indices left out (-> PARTIAL)
    UnadmReason   unadmitted_reason[STEP_MAX];// why each unadmitted[] is stranded
    int           unadmitted_count;
    AutoplayVerdict verdict;                 // CLEARED / PARTIAL / FAILED
    int           failed_goal_index;         // committed-but-unwinnable goal, or -1
    // Terminal world after simulating the full committed plan (the last
    // admitted objective's checkpoint, or the boot world if nothing admitted).
    // One snapshot, not one-per-objective — the proof that every admitted
    // objective is done lives here, and the executor reproduces this terminal
    // state.
    WorldSnapshot terminal;

    // RECORDED PRIMITIVE SEQUENCE (WS-4). Captured during plan_build's
    // single proving simulation: the exact moves / answers / direct actions that
    // completed each admitted step, plus the per-fight CombatTurnRecords. The
    // executor APPLIES this with no planner / nav / prediction / combat at execute
    // time, so headless and visible reproduce the identical resolution. Heap-owned
    // — freed by plan_free. step_span[k] is the range for admitted[k].
    RecBuf        rec;
    CombatRecList combat;
    PlanStepSpan  step_span[STEP_MAX];

    // Planning-cost meter: total simulated turns plan_build spent proving this
    // plan (every candidate + intervention probe). Observability only.
    long          sim_turns;
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

// Apply an RA_SET_ARMY recruit-optimizer re-composition (WS/RO): atomically
// replace the army with `army[0..n)` — dismiss every current stack, then
// GameBuyTroop each target stack at its exact count. Deterministic (pure engine
// calls on a byte-identical world), so executor + visible reproduce it. In autoplay.c.
void autoplay_apply_set_army(Game *g, const RecArmyStack *army, int n);


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
    int   step;                  // current admitted-step index (for boundary_fp)
    int   rec_cursor;            // index into plan->rec.items[]
    int   turns;                 // adventure steps issued so far
    bool  done;
    bool  boundary_checked;      // has the current step's boundary_fp been asserted?
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
