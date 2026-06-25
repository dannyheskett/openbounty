// autoplay/prereq.c — hard prerequisite graph. See prereq.h.
//
// The table is tiny and explicit on purpose: only the engine-enforced HARD
// gates live here (siege weapons for any castle assault; the matching active
// contract for a villain capture). Each edge is asserted against the engine by
// a pinning test, so this data cannot silently drift from engine/*.c.

#include "prereq.h"

#include <string.h>
#include <stdio.h>

PlanStep prereq_make_buy_siege(int zone_index) {
    PlanStep s;
    memset(&s, 0, sizeof s);
    s.kind = STEP_SIEGE_WEAPONS;       // an objective AND a hard prereq
    s.tile_bound = false;              // resolved to the nearest town at exec
    s.arrival = ARRIVE_BUY_SIEGE;
    s.zone_index = zone_index;
    snprintf(s.label, sizeof s.label, "buy siege weapons (at a town)");
    return s;
}

PlanStep prereq_make_take_contract(const char *villain_id, int zone_index) {
    PlanStep s;
    memset(&s, 0, sizeof s);
    s.kind = STEP_TAKE_CONTRACT;
    s.tile_bound = false;              // taken at a town in normal play; the
                                       // engine cycle needs no tile for the oracle
    s.arrival = ARRIVE_TAKE_CONTRACT;
    s.zone_index = zone_index;
    if (villain_id) snprintf(s.handle, sizeof s.handle, "%s", villain_id);
    snprintf(s.label, sizeof s.label, "take contract for %s",
             villain_id && villain_id[0] ? villain_id : "(villain)");
    return s;
}

bool prereq_unmet(const PlanStep *obj, const Game *g, PrereqCandidates *out) {
    out->count = 0;
    if (!obj || !g) return false;
    if (!planstep_is_objective(obj->kind)) return false;

    switch (obj->kind) {
    case STEP_MONSTER_CASTLE:
        // Hard gate: stepping onto the gate without siege weapons silently
        // bounces (engine/step.c). Insert BUY_SIEGE if not yet owned.
        if (!g->stats.siege_weapons)
            out->step[out->count++] = prereq_make_buy_siege(obj->zone_index);
        break;

    case STEP_VILLAIN:
        // Same siege gate, PLUS the villain's contract must be active at siege
        // time or the capture does not count (engine/flow_resolve.c). Try-order:
        // siege first (the gate is the first wall), then the contract.
        if (!g->stats.siege_weapons)
            out->step[out->count++] = prereq_make_buy_siege(obj->zone_index);
        // obj->handle is the villain id for a villain objective.
        if (obj->handle[0] &&
            strcmp(g->contract.active_id, obj->handle) != 0)
            out->step[out->count++] =
                prereq_make_take_contract(obj->handle, obj->zone_index);
        break;

    default:
        // Chests / artifacts / navmap / orb / alcove / siege-weapons-itself have
        // no DISCRETE hard prereq here. (Across-water positioning and army
        // strength are handled by the planner's reachability + combat
        // prediction, not this table — see prereq.h.)
        break;
    }
    return out->count > 0;
}
