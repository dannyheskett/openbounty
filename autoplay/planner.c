// autoplay/planner.c
//
// THE PLANNER (see planner.h). Orders the objectives and drives execute() over
// them with a backtracking forward simulation, leaving an admitted GoalLog and a
// replayable recording. It snapshots/restores the world (the only component that
// does) and never touches the engine directly.

#include "planner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goals.h"        // PlanStepSet + the objective enumerators; GoalLog
#include "primitives.h"   // execute() — the objective-to-engine oracle
#include "prereq.h"       // prereq_unmet — hard gates (siege weapons, contract)
#include "worldsnap.h"    // worldsnap_capture / worldsnap_restore (backtrack)

// Straight-line (Chebyshev) distance from the hero to objective `s`'s tile — a CHEAP
// ordering heuristic with NO reachability query (nothing is genuinely unreachable, so
// the mover handles the actual route). Used to schedule nearest-first so the hero
// clusters nearby work instead of crisscrossing the zone.
static int planner_obj_distance(const Game *g, const PlanStep *s) {
    int adx = s->target.x - g->position.x, ady = s->target.y - g->position.y;
    if (adx < 0) adx = -adx;
    if (ady < 0) ady = -ady;
    return adx > ady ? adx : ady;
}

// Grow-on-demand append to a GoalLog. Returns false on OOM.
static bool goallog_push(GoalLog *gl, const PlanStep *s) {
    if (gl->count == gl->cap) {
        int ncap = gl->cap ? gl->cap * 2 : 16;
        PlanStep *ni = realloc(gl->items, (size_t)ncap * sizeof *ni);
        if (!ni) return false;
        gl->items = ni; gl->cap = ncap;
    }
    gl->items[gl->count++] = *s;
    return true;
}

// Resolve the hero's current zone to its res->zones[] index, or -1.
static int planner_current_zone_index(const Game *g, const Resources *res) {
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, g->position.zone) == 0) return i;
    return -1;
}

// Attempt objective `idx`: snapshot, execute, keep-or-restore. The Executor is
// do-or-fail — it carries out the objective's primitive exactly or returns false (a
// planning failure, e.g. a foe unwinnable with the held army). On success admit the
// primitive; on failure restore the world and drop the recorded tail in full. A recruit
// the executor made mid-attempt STACKS only when the attempt SUCCEEDS (it is kept with
// the admitted attempt); a failed attempt rolls everything back, so a wasted recruit
// detour cannot leave the hero marooned at a far dwelling with spent gold.
static bool planner_attempt(Game *g, Map *map, Fog *fog, const Resources *res,
                            RecSink *sink, const PlanStep *s, int idx, int pass,
                            WorldSnapshot *snap, PrimRun *out) {
    int rec_before = out->rec.count;
    int plan_before = out->plan.count;
    worldsnap_capture(snap, g, map, fog);

    // Satisfy the objective's UNMET hard gates FIRST, in the documented try-order
    // (siege weapons, then the matching active contract for a villain). Each gate
    // is an enabling primitive executed under THIS snapshot, so if any gate — or
    // the objective itself — fails, the restore below rolls the whole chain back.
    // `why` names the failing step for the DEFER log so a partial run shows
    // WHERE each unmet objective stalled (prereq vs the objective itself).
    const char *why = "executor";
    PrereqCandidates pc;
    if (prereq_unmet(s, g, &pc)) {
        for (int k = 0; k < pc.count; k++) {
            if (!execute(&pc.step[k], res, g, map, fog, sink)) { why = pc.step[k].label; goto fail; }
            goallog_push(&out->plan, &pc.step[k]);
            printf("planner:   p%d [%3d] PREREQ %-7s %-22s\n",
                   pass, idx, planstep_kind_str(pc.step[k].kind), pc.step[k].label);
        }
    }

    if (execute(s, res, g, map, fog, sink)) {
        goallog_push(&out->plan, s);
        printf("planner:   p%d [%3d] ADMIT  %-7s %-22s (%d prims) gold=%d\n",
               pass, idx, planstep_kind_str(s->kind), s->label,
               out->rec.count - rec_before, g->stats.gold);
        return true;
    }
fail:
    printf("planner:   p%d [%3d] DEFER  %-7s %-22s (blocked: %s) gold=%d\n",
           pass, idx, planstep_kind_str(s->kind), s->label, why, g->stats.gold);
    worldsnap_restore(snap, g, map, fog);
    out->rec.count = rec_before;
    out->plan.count = plan_before;
    return false;
}

bool planner(Game *g, Map *map, Fog *fog, const Resources *res,
             int zone_scope, PrimRun *out) {
    (void)zone_scope;
    if (!g || !map || !fog || !res || !out) return false;
    memset(out, 0, sizeof *out);
    out->verdict = AUTOPLAY_VERDICT_PARTIAL;

    // 1. Enumerate the in-scope objective universe. FIRST INCREMENT: the hero's
    //    current zone only (intra-zone nav + boat); multi-zone scope is a
    //    follow-up once cross-zone FETCH is confirmed.
    int zi = planner_current_zone_index(g, res);
    if (zi < 0) {
        fprintf(stdout, "planner: hero zone '%s' not in resources\n", g->position.zone);
        return false;
    }
    PlanStepSet *set = calloc(1, sizeof *set);
    if (!set) return false;
    plansteps_enumerate_noncombat(set, g, map, zi);
    plansteps_enumerate_combat(set, g, zi);
    plansteps_enumerate_foes(set, g, zi);

    // Enumeration breakdown by objective kind — the planner's full work-list at a
    // glance (so a run's log shows WHAT was on the table, not just what was admitted).
    int kc[STEP_MAX] = {0};
    for (int i = 0; i < set->count; i++) kc[set->steps[i].kind]++;
    printf("planner: zone=%s objectives=%d  chest=%d artifact=%d navmap=%d orb=%d "
           "alcove=%d siege=%d castle=%d villain=%d foe=%d scepter=%d\n",
           g->position.zone, set->count,
           kc[STEP_CHEST], kc[STEP_ARTIFACT], kc[STEP_NAVMAP], kc[STEP_ORB],
           kc[STEP_ALCOVE], kc[STEP_SIEGE_WEAPONS], kc[STEP_MONSTER_CASTLE],
           kc[STEP_VILLAIN], kc[STEP_FOE], kc[STEP_SCEPTER]);

    out->obj_total = set->count;

    // 2. The recording sink: nav()/execute() append every engine prim here.
    RecSink sink = { &out->rec, &out->combats };

    // 3. Forward passes to a FIXPOINT, each with per-candidate snapshot/restore
    //    (the backtracking loop's inner mechanism: try, keep-or-restore). One pass
    //    is not enough once combat lands: clearing a foe in pass N unblocks the
    //    chests it gated, which only become reachable on pass N+1. Repeat until a
    //    whole pass admits nothing new. `done[]` tracks per-objective completion so
    //    each is counted once and already-done ones (incidental pickups, prior
    //    passes) are skipped cheaply.
    WorldSnapshot *snap = malloc(sizeof *snap);
    bool *done = calloc((size_t)set->count, sizeof *done);
    if (!snap || !done) { free(snap); free(done); free(set); return false; }

    bool progressed = true;
    for (int pass = 0; progressed; pass++) {
        progressed = false;

        // (a) LOCATION-FREE chores (buy siege weapons at the nearest town): no tile,
        //     so do them first, from wherever the hero stands, before the tile-bound
        //     objectives walk him across the zone.
        for (int i = 0; i < set->count; i++) {
            if (done[i]) continue;
            const PlanStep *s = &set->steps[i];
            if (s->tile_bound) continue;
            if (planstep_is_done(s, g)) { done[i] = true; continue; }
            if (planner_attempt(g, map, fog, res, &sink, s, i, pass, snap, out)) {
                done[i] = true; progressed = true;
            }
        }

        // (b) TILE-BOUND objectives, NEAREST-FIRST by straight-line distance (a cheap
        //     ordering heuristic — no reachability query; the mover handles the route).
        //     Each step pick the closest remaining objective and drive the mover to it,
        //     clustering nearby work so the hero does not crisscross the zone. `tried[]`
        //     (reset per pass) drops one the mover could not reach from here right now,
        //     so it is not re-picked this sweep; the next pass retries from a new spot.
        bool *tried = calloc((size_t)set->count, sizeof *tried);
        if (!tried) break;
        for (;;) {
            int idx = -1, best = -1;
            for (int i = 0; i < set->count; i++) {
                if (done[i] || tried[i]) continue;
                const PlanStep *s = &set->steps[i];
                if (!s->tile_bound) continue;
                if (planstep_is_done(s, g)) { done[i] = true; continue; }
                int d = planner_obj_distance(g, s);
                if (best < 0 || d < best) { best = d; idx = i; }
            }
            if (idx < 0) break;                        // nothing left this pass
            if (planner_attempt(g, map, fog, res, &sink, &set->steps[idx], idx,
                                pass, snap, out)) {
                done[idx] = true; progressed = true;
            } else {
                tried[idx] = true;     // mover could not reach it now — retry next pass
            }
        }
        free(tried);
    }

    for (int i = 0; i < set->count; i++) if (done[i]) out->obj_done++;
    out->verdict = (out->obj_done == out->obj_total)
                       ? AUTOPLAY_VERDICT_CLEARED
                       : AUTOPLAY_VERDICT_PARTIAL;

    free(done);
    free(snap);
    free(set);
    return true;
}

void primrun_free(PrimRun *r) {
    if (!r) return;
    free(r->plan.items);
    r->plan.items = NULL; r->plan.count = r->plan.cap = 0;
    recbuf_free(&r->rec);
    combatreclist_free(&r->combats);
}

