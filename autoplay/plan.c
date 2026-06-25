// autoplay/plan.c — plan construction by simulation. See plan.h.
//
// The admission loop:
//   - Capture the boot world as the cached "last-good" snapshot.
//   - Repeatedly scan the goal set in priority order (enumeration order
//     already encodes safe-first -> power -> combat) for a not-yet-resolved
//     candidate; to test it, RESTORE the last-good snapshot and simulate ONLY
//     that candidate onto the proven prefix (incremental replay).
//   - Admit iff the candidate's objective completed with the hero alive;
//     re-capture the snapshot as the new last-good and append it.
//     Otherwise record why it failed and move on.
//   - A candidate that hard-blocks on an unwinnable fight (planner_blocked)
//     is FAILED (committed-but-unwinnable); one that merely can't be
//     reached/completed is left unadmitted (-> PARTIAL).
//   - Terminate when a full scan admits nothing new. Restore the boot
//     world before returning so the live state is untouched.

#include "plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goals.h"
#include "report.h"
#include "prereq.h"
#include "step.h"        // GameStep (execute phase issues live moves)
#include "player_io.h"   // player_io_answer (apply recorded answers)
#include "pending.h"     // pending_reset (clear stale planning-sim pending flow)
#include "spells_adventure.h" // spells_adventure_reset_ui (scrub gate/bridge UI globals)
#include "tables.h"      // troop_by_id (army-power measure, criterion b2)
#include "ob_trace.h"    // TEMP OB_TRACE_PRES observational trace (AP-046)

// Outcome of simulating one candidate onto the proven prefix.
typedef enum {
    CAND_ADMITTED = 0,  // objective completed, hero alive -> admit
    CAND_UNREACHABLE,   // ran to terminal but objective not done -> PARTIAL
    CAND_UNWINNABLE,    // hard-blocked on an unwinnable fight -> FAILED
    CAND_DEAD,          // hero died during the simulation -> FAILED
} CandResult;

// Simulate admitting goal `gi` onto the proven prefix represented by `prefix`.
// Restores `prefix` into (g,map) + RNG, drives the planner constrained to `gi`
// to terminal, and reports the outcome. On admission, captures the resulting
// world into `*out_checkpoint`. When `sink` is non-NULL the proven primitive
// sequence for this candidate is recorded into it (WS-4); the caller commits the
// recorded tail into the Plan only on CAND_ADMITTED (a rejected candidate's
// recording is discarded by the caller).

void plan_free(Plan *p) {
    if (!p) return;
    recbuf_free(&p->rec);
    combatreclist_free(&p->combat);
}

// ---- Execute phase (WS-4) — the DUMB APPLIER --------------------------------
// The executor replays the proven primitive recording on the live world with no
// planner, no nav, no prediction, and no combat resolution. Each primitive is
// applied by the same engine call the planner made while proving the step, so —
// the world being byte-deterministic — the live world tracks the simulated one
// exactly (checked by boundary_fp at each step boundary). Headless and visible
// apply the identical recording; visible mode only ANIMATES a fight's record
// before its outcome is applied (a passive draw, state-inert).

void plan_exec_begin(PlanExecutor *ex, const Plan *plan,
                     PlanCombatAnimator animate, PlanViewPresenter present_view,
                     void *animate_ctx) {
    if (!ex) return;
    ex->plan = plan;
    ex->step = 0;
    ex->rec_cursor = 0;
    ex->turns = 0;
    ex->boundary_checked = false;
    ex->animate = animate;
    ex->present_view = present_view;
    ex->animate_ctx = animate_ctx;
    ex->done = (!plan || plan->rec.count == 0);
}

// The shell's town-menu row indices (src/views.c TownRow, kept in sync there).
// The executor is engine-pure and cannot include the shell header, but the
// PlanViewPresenter contract (plan.h) defines chosen_row in exactly these terms
// ("e.g. TOWN_ROW_SIEGE"), so this narrow mapping is part of that contract.
enum { TOWN_DEMO_ROW_CONTRACT = 0, TOWN_DEMO_ROW_BOAT = 1,
       TOWN_DEMO_ROW_SPELL = 3, TOWN_DEMO_ROW_SIEGE = 4 };

// Which town-menu row a town-gate visit is for: the town transaction is recorded
// as the primitive IMMEDIATELY AFTER the gate-step REC_MOVE, so peek it. Returns
// -1 (just present the menu, no cursor walk) when no town action follows. Used
// ONLY by the visible presenter to render the navigation a player would make; it
// never affects state (the real transaction is the recorded primitive itself).
static int town_demo_row_for_next_action(const Plan *plan, int cursor) {
    int next = cursor + 1;
    if (next >= plan->rec.count) return -1;
    const RecPrim *p = &plan->rec.items[next];
    if (p->kind != REC_ACTION) return -1;
    switch (p->action) {
        case RA_BUY_SIEGE:     return TOWN_DEMO_ROW_SIEGE;
        case RA_TAKE_CONTRACT: return TOWN_DEMO_ROW_CONTRACT;
        case RA_RENT_BOAT:     return TOWN_DEMO_ROW_BOAT;
        case RA_BUY_SPELLS:    return TOWN_DEMO_ROW_SPELL;
        default:               return -1;
    }
}

PlanExecStatus plan_exec_step(PlanExecutor *ex, Game *g, Map *map,
                              Fog *fog, const Resources *res) {
    if (!ex || ex->done) { if (ex) ex->done = true; return PLAN_EXEC_DONE; }
    if (!ex->plan || !g || !map || !fog || !res) {
        ex->done = true; return PLAN_EXEC_DONE;
    }
    const Plan *plan = ex->plan;

    // Hero death during execution ends the run (a proven plan never reaches this;
    // it is a determinism-regression backstop, not a normal outcome).
    if (g->stats.game_over) { ex->done = true; return PLAN_EXEC_DONE; }

    if (ex->rec_cursor >= plan->rec.count) { ex->done = true; return PLAN_EXEC_DONE; }

    // Step-boundary checkpoint: when the cursor reaches a step's
    // rec_begin, assert the live world matches the recorded boundary_fp. A
    // mismatch is a determinism regression — stop rather than apply a primitive
    // to the wrong world. Advance ex->step to the span containing the cursor.
    while (ex->step < plan->admitted_count &&
           ex->rec_cursor >= plan->step_span[ex->step].rec_end) {
        ex->step++;
        ex->boundary_checked = false;
    }
    if (ex->step < plan->admitted_count && !ex->boundary_checked &&
        ex->rec_cursor == plan->step_span[ex->step].rec_begin) {
        uint64_t live = worldsnap_fingerprint(g, map);
        if (live != plan->step_span[ex->step].boundary_fp) {
            // Determinism regression: the live prefix diverged from the proven
            // one. Stop (the verdict already reflects the plan; we never apply a
            // primitive against the wrong world).
            ex->done = true;
            return PLAN_EXEC_DONE;
        }
        ex->boundary_checked = true;
    }

    const RecPrim *p = &plan->rec.items[ex->rec_cursor];
    switch (p->kind) {
    case REC_MOVE:
        GameStep(g, map, fog, res, p->dx, p->dy);
        ex->turns++;
        // VISIBLE: a move that stepped onto a town gate just opened the town view
        // (engine entered_town path) and set position.in_town. Present it the way a
        // player sees it — render the town screen + walk the menu to the action's
        // row — synchronously, BEFORE the drain below clears the REQ_VIEW. The town
        // ACTION itself is a separate recorded primitive that runs next (in_town is
        // still set; only a GameStep clears it). HEADLESS: present_view is NULL, so
        // the REQ_VIEW is just drained → Game/queue identical across modes.
        if (ex->present_view && g->position.in_town[0])
            ex->present_view(ex->animate_ctx, VIEW_TOWN,
                             town_demo_row_for_next_action(plan, ex->rec_cursor));
        player_io_drain_messages(g);
        break;
    case REC_ANSWER: {
        if (p->outcome != PLAYER_IO_COMBAT_NOT_RUN) {
            // A fight. VISIBLE: first DRAW the battle (WS-7) — the animator builds
            // its own throwaway Combat from the live PRE-FIGHT state (the pending
            // flow + the hero's army, both still intact here) and renders the
            // recorded per-turn record; it never touches g. THEN re-run the
            // deterministic resolution on the live world to apply the army/spoils
            // writeback the flow-apply core does not do (combat.c owns it),
            // byte-identical to the recording (combat RNG is pure in
            // seed+identity+mode). HEADLESS: no animator, just apply.
            if (ex->animate)
                ex->animate(ex->animate_ctx, p->combat_mode,
                            &plan->combat.items[
                                p->rec_combat_index >= 0 &&
                                p->rec_combat_index < plan->combat.count
                                    ? p->rec_combat_index : 0]);
            autoplay_apply_recorded_combat(g, NULL);
        }
        // Apply the answer + flow consequence (consume foe tile / capture / siege
        // result) through the shared router. For a fight the army/spoils writeback
        // was just done by autoplay_apply_recorded_combat (matching sim order); for
        // non-combat flows (chest/recruit/alcove) this is the whole action.
        PlayerIoPresentation pres;
        player_io_answer(g, map, /*fog=*/NULL, g->res, p->ans, p->outcome, &pres);
        player_io_drain_messages(g);
        break;
    }
    case REC_ACTION:
        if (p->action == RA_SET_ARMY)
            autoplay_apply_set_army(g, p->set_army, p->set_army_n);
        else
            autoplay_apply_rec_action(g, map, fog, p->action,
                                      p->act_id, p->act_x, p->act_y);
        // (Town views are presented on the town-ENTERING REC_MOVE, not here — the
        // town action core just mutates state with in_town already set.)
        player_io_drain_messages(g);
        break;
    }
    ex->rec_cursor++;
    if (ex->rec_cursor >= plan->rec.count) { ex->done = true; return PLAN_EXEC_DONE; }
    return PLAN_EXEC_RUNNING;
}
