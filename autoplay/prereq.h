// autoplay/prereq.h
//
// Hard prerequisite graph as static data + the intervention-candidate
// generator the intervention search consumes. The edges below are ENGINE-
// ENFORCED preconditions — an objective is *impossible* until its prereq
// holds — verified against engine/*.c and pinned by a test. This is distinct
// from the universal SOFT prereq "army strong enough to win the fight", which
// is not a discrete flag and is detected by combat prediction, not this table.
//
// Verified hard edges (each cites its engine proof):
//   - STEP_MONSTER_CASTLE  requires siege_weapons   (engine/step.c: hostile
//     monster gate silently bounces without siege weapons)
//   - STEP_VILLAIN         requires siege_weapons   (engine/step.c: villain gate
//     silently bounces without siege weapons)
//   - STEP_VILLAIN(V)      requires the contract for V active at siege time
//     (engine/flow_resolve.c: villains_caught[] set only when active_id == V)
//   - any across-water objective requires a positioned boat
//     (engine/adventure.c: water impassable on foot)  -- this one is positional,
//     handled by the planner's reachability, not enumerated as a discrete flag.
//
// Engine-only (no src/); links libobengine + engine headers.

#ifndef OB_AUTOPLAY_PREREQ_H
#define OB_AUTOPLAY_PREREQ_H

#include <stdbool.h>

#include "game.h"
#include "goals.h"   // PlanStep / PlanKind

// The most enabling primitives any single objective's hard prereqs can require.
// Sized to the worst case (a villain: siege + contract).
#define PREREQ_MAX 4

// A set of enabling-primitive candidates to satisfy an objective's UNMET hard
// prerequisites. Each entry is a fully-formed PlanStep of an enabling kind
// (STEP_TAKE_CONTRACT / STEP_SIEGE_WEAPONS), ready for the
// intervention search to simulate. Order is the documented try-order.
typedef struct {
    PlanStep step[PREREQ_MAX];
    int      count;
} PrereqCandidates;

// Does objective `obj` have any UNMET hard prerequisite in the current state?
// Reads `g` (siege_weapons, contract.active_id). Returns true and fills `*out`
// with the enabling primitives that would satisfy the unmet prereqs (in
// try-order) when at least one is unmet; returns false (and out->count == 0)
// when all hard prereqs are already satisfied. `obj` must be an objective
// primitive (planstep_is_objective); enabling primitives have no prereqs here.
//
// NOTE: this covers the DISCRETE hard gates only (siege weapons, contract). The
// soft army-strength prereq and water-positioning are NOT returned here —
// the search handles those via combat prediction and the boat sub-goal
// respectively. So `false` means "no discrete hard gate unmet", not "ready to
// admit".
bool prereq_unmet(const PlanStep *obj, const Game *g, PrereqCandidates *out);

// Build the enabling primitive that takes villain `villain_id`'s contract
// (STEP_TAKE_CONTRACT with handle = villain_id). Exposed for the search/executor.
PlanStep prereq_make_take_contract(const char *villain_id, int zone_index);

// Build the BUY_SIEGE objective primitive (it is also a hard prereq). zone_index
// is the hero's zone; the target town is resolved at execution like today.
PlanStep prereq_make_buy_siege(int zone_index);

#endif // OB_AUTOPLAY_PREREQ_H
