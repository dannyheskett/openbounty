// autoplay/exec_recruit.c
//
// Executor RECRUIT helpers (see exec.h / docs/EXECUTOR-REFACTOR.md):
//   exec_recruit         — assemble the army to a composition (folds recruit.c)  [P1]
//   exec_recruit_one     — buy n of a troop at one source                        [P1]
//   exec_recruit_sources — enumerate recruitable troops + sources  (HELPER #9)  [here]
//   exec_dismiss         — dismiss a held stack                    (HELPER #10) [here]
//
// recruit.c is folded into this TU below — its source enumeration + combat
// prediction are exec_recruit's / exec_fight's internals. (recruit.c's old army
// optimizer was deleted with the AutoplayPlanner at P6, not folded.)

#include "exec.h"

#include <stdio.h>
#include <string.h>

#include "game.h"           // GameMaxRecruitable / GameBuyTroop / army / position / world
#include "resources.h"      // ResCastle (the audience/home castle)
#include "map.h"            // MapGetTile / Tile (home gate scan)
#include "tile.h"           // INTERACT_CASTLE_GATE
#include "pending.h"        // pending_flow / FLOW_RECRUIT
#include "flow_resolve.h"   // flow_apply_dismiss_army
#include "flow_answer.h"    // FlowAnswer / PromptAnswer / FLOW_ANS_1 / FLOW_ANS_YES

// HELPER #9 — exec_recruit_sources. Enumerate every recruitable troop and where to
// get it, in preference-tier order. Read-only query. Delegates to the recruit
// enumerator for now (the enumeration body folds in when recruit.c is collapsed).
int exec_recruit_sources(const Game *g, RecruitSource *out, int cap) {
    return recruit_sources_enumerate(g, out, cap);
}

// HELPER #8 — exec_recruit_one. Buy up to `n` of source->troop_id at ONE source.
// A dwelling (in- or off-zone) is reached fight-through and its FLOW_RECRUIT answered;
// the home pool sails to the audience-castle gate (which sets position.home_castle in
// the gate step, so GameBuyTroop is immediately legal — no flow). Records the buy for
// exact replay. (This folds the old exec_recruit_home_troop home-pool path in.)
bool exec_recruit_one(Game *g, Map *map, Fog *fog, const Resources *res,
                      const RecruitSource *src, int n, RecSink *rec) {
    if (!src || n <= 0) return false;

    if (src->tier == RSRC_HOME_CASTLE) {
        const ResCastle *home = NULL;
        for (int i = 0; i < res->castle_count; i++)
            if (strcmp(res->castles[i].special.flow, "audience") == 0) {
                home = &res->castles[i]; break;
            }
        if (!home) { printf("exec_recruit_one: home castle not found\n"); return false; }

        // Sail to the home zone first (so the gate scan reads its map); defer if the
        // home zone is undiscovered or the crossing did not land.
        if (strcmp(home->zone, g->position.zone) != 0) {
            int ctx = (home->gate_x >= 0) ? home->gate_x : home->x;
            int cty = (home->gate_y >= 0) ? home->gate_y : home->y;
            int zi = -1;
            for (int i = 0; i < res->zone_count; i++)
                if (strcmp(res->zones[i].id, home->zone) == 0) { zi = i; break; }
            if (zi < 0 || zi >= GAME_CONTINENTS || !g->world.zones_discovered[zi]) {
                printf("exec_recruit_one: home zone '%s' undiscovered (zi=%d)\n", home->zone, zi);
                return false;
            }
            exec_travel(g, map, fog, res, home->zone, ctx, cty, /*fight_through=*/false, rec);
            if (strcmp(home->zone, g->position.zone) != 0) {
                printf("exec_recruit_one: zone crossing to '%s' failed\n", home->zone);
                return false;
            }
        }

        if (!g->position.home_castle[0]) {
            // Find the INTERACT_CASTLE_GATE tile (the bouncer the hero must step onto).
            // gate_x/gate_y from the resource is the landing/approach tile — not the gate
            // itself — so we scan near the castle body tile for the actual gate tile.
            int gx = -1, gy = -1;
            for (int dy = -2; dy <= 2 && gy < 0; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    const Tile *t = MapGetTile(map, home->x + dx, home->y + dy);
                    if (t && t->interactive == INTERACT_CASTLE_GATE) {
                        gx = home->x + dx; gy = home->y + dy; break;
                    }
                }
            if (gx < 0) {
                printf("exec_recruit_one: home gate not found (body=%d,%d)\n",
                       home->x, home->y);
                return false;
            }
            if (!exec_reach(g, map, fog, res, home->zone, gx, gy, /*fight_through=*/true, rec)) {
                printf("exec_recruit_one: reach to home gate (%d,%d) FAIL\n", gx, gy);
                return false;
            }
        }
        if (!g->position.home_castle[0]) {
            printf("exec_recruit_one: at gate but home_castle unset\n");
            return false;
        }

        int maxn = GameMaxRecruitable(g, src->troop_id);
        int buy = (n < maxn) ? n : maxn;
        if (buy <= 0) {
            printf("exec_recruit_one: home castle buy=0 for '%s' (maxn=%d n=%d)\n",
                   src->troop_id, maxn, n);
            return false;
        }
        if (GameBuyTroop(g, src->troop_id, buy) != 0) {
            printf("exec_recruit_one: GameBuyTroop FAIL '%s' buy=%d\n", src->troop_id, buy);
            return false;
        }
        rec_push_action(rec, RA_RECRUIT_TYPE_N, src->troop_id, buy, 0);
        return true;
    }

    // A dwelling, in the current zone or another: reach it (sailing first when
    // off-zone — exec_reach handles the crossing) and answer its recruit flow.
    // For in-zone dwellings the foot-stranding filter (recruit_source_foot_ok)
    // already confirmed a foot-only path exists; exec_reach uses the full
    // boat-aware A* regardless (hero always has a return path if they board).
    bool reached = exec_reach(g, map, fog, res, src->zone, src->x, src->y, /*fight_through=*/true, rec);
    if (!reached) {
        printf("exec_recruit_one: reach FAIL '%s' tier=%d at (%d,%d) zone=%s hero=(%d,%d,%s)\n",
               src->troop_id, src->tier, src->x, src->y, src->zone,
               g->position.x, g->position.y, g->position.zone);
        return false;
    }
    if (pending_flow != FLOW_RECRUIT) {
        printf("exec_recruit_one: flow FAIL '%s' tier=%d pending=%d (want FLOW_RECRUIT)\n",
               src->troop_id, src->tier, (int)pending_flow);
        return false;
    }
    int maxn = GameMaxRecruitable(g, src->troop_id);
    int buy = (n < maxn) ? n : maxn;
    int aff = recruit_affordable_count(g, src->troop_id);
    if (aff < buy) buy = aff;                          // GameBuyTroop is all-or-nothing on gold
    // Count the held stack before/after so we report the ACTUAL purchase: a buy the
    // engine rejects (gold / dwelling population) must NOT look like a successful recruit
    // to the caller — else the planner "commits" an army that was never bought.
    int before = 0;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
        if (strcmp(g->army[s].id, src->troop_id) == 0) { before = g->army[s].count; break; }
    FlowAnswer a; a.kind = (buy > 0) ? FLOW_ANS_YES : FLOW_ANS_NO; a.number = buy;
    exec_answer(g, map, a, rec);                       // answer either way (clear the flow)
    int after = 0;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
        if (strcmp(g->army[s].id, src->troop_id) == 0) { after = g->army[s].count; break; }
    return after > before;
}

// HELPER #7 — exec_recruit. Assemble the held army to `target`: dismiss held stacks
// the target dropped (freeing leadership), then buy each target stack's shortfall at
// its preferred source. Do-or-fail — any unreachable/unbuyable source fails the whole
// contract. (The optimizer that DECIDES `target` folds into this helper's internals
// at P6; today the planner supplies it.)
bool exec_recruit(Game *g, Map *map, Fog *fog, const Resources *res,
                  const ArmyTarget *target, RecSink *rec) {
    if (!target || target->n <= 0) return true;        // empty target: trivially met

    // 1. Dismiss held stacks whose type is NOT in the target (substituted away).
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        bool in_comp = false;
        for (int i = 0; i < target->n; i++)
            if (target->slot[i].id[0] &&
                strcmp(target->slot[i].id, g->army[s].id) == 0) { in_comp = true; break; }
        if (in_comp) continue;
        if (exec_dismiss(g, s, rec)) s = -1;           // dismissed; slots shifted -> rescan
    }

    // 2. Acquire each target stack's shortfall from its source.
    RecruitSource srcs[64];
    int ns = exec_recruit_sources(g, srcs, 64);
    for (int i = 0; i < target->n; i++) {
        const char *id = target->slot[i].id;
        int want = target->slot[i].count;
        if (!id[0] || want <= 0) continue;
        int held = 0;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++)
            if (g->army[s].id[0] && strcmp(g->army[s].id, id) == 0) {
                held = g->army[s].count; break;
            }
        int need = want - held;
        if (need <= 0) continue;                        // already at/over target

        const RecruitSource *src = NULL;
        for (int k = 0; k < ns; k++)
            if (strcmp(srcs[k].troop_id, id) == 0) { src = &srcs[k]; break; }
        if (!src) return false;                         // no source: contract fails

        if (!exec_recruit_one(g, map, fog, res, src, need, rec)) return false;
    }
    return true;
}

// HELPER #10 — exec_dismiss. Dismiss the held stack in `slot`. Captures the troop id
// FIRST (the engine compacts the slot away), answers with FLOW_ANS_1+slot, then
// records RA_DISMISS_TYPE keyed by that id (replay dismisses by id, not slot).
// Returns false if the slot is empty/invalid or nothing was dismissed (the engine
// refuses to empty the army).
bool exec_dismiss(Game *g, int slot, RecSink *rec) {
    if (slot < 0 || slot >= GAME_ARMY_SLOTS) return false;
    if (!g->army[slot].id[0] || g->army[slot].count <= 0) return false;

    // Capture the id before the dismiss compacts the slot. Bounded copy into a
    // zeroed buffer (army ids fit [24]).
    char dis_id[24] = {0};
    {
        const char *aid = g->army[slot].id;
        size_t m = strlen(aid);
        if (m >= sizeof dis_id) m = sizeof dis_id - 1;
        memcpy(dis_id, aid, m);
    }

    int before = army_stack_count(g);
    FlowAnswer ans; memset(&ans, 0, sizeof ans);
    ans.kind = (PromptAnswer)(FLOW_ANS_1 + slot);
    flow_apply_dismiss_army(g, ans, NULL);
    if (army_stack_count(g) >= before) return false;   // nothing dismissed

    rec_push_action(rec, RA_DISMISS_TYPE, dis_id, 0, 0);
    return true;
}

// ===================== folded from recruit.c (P6: collapse sub-solver into the executor) =====================
//
// Simulation-driven recruit optimizer: given a game state and a blocked fight,
// compute the strongest reachable army. Extracted from autoplay.c. Every
// function here is pure with respect to the planner — it operates on a Game /
// ArmyTarget / CombatMode and runs combat sims on a discarded copy (RNG
// snapshot/restore), so the live world is untouched. See recruit.h for the
// module contract.

#include "recruit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tables.h"          // troop_by_id / troop_by_index / troops_count
#include "resources.h"       // DwellingState, economy
#include "combat_policy.h"   // autoplay_combat_policy

static const char *recruit_home_zone(const Game *g) {
    if (!g || !g->res) return "";
    for (int i = 0; i < g->res->castle_count; i++)
        if (strcmp(g->res->castles[i].special.flow, "audience") == 0)
            return g->res->castles[i].zone;
    return "";
}

int recruit_sources_enumerate(const Game *g, RecruitSource *out, int cap) {
    if (!g || !out || cap <= 0) return 0;
    int n = 0;
    const char *cz = g->position.zone;
    const char *hz = recruit_home_zone(g);

    // TIER 1 — every dwelling in the CURRENT zone (cheapest: just walk). Stable
    // order = g->dwellings[] index, so any truncation is deterministic.
    for (int i = 0; i < g->dwelling_count && n < cap; i++) {
        const DwellingState *d = &g->dwellings[i];
        if (d->count <= 0 || !d->troop_id[0]) continue;
        if (strcmp(d->zone, cz) != 0) continue;
        if (!troop_by_id(d->troop_id)) continue;
        snprintf(out[n].troop_id, sizeof out[n].troop_id, "%.23s", d->troop_id);
        out[n].avail = d->count;
        out[n].tier  = RSRC_INZONE_DWELLING;
        snprintf(out[n].zone, sizeof out[n].zone, "%.23s", d->zone);
        out[n].x = d->x; out[n].y = d->y;
        n++;
    }

    // TIER 2 — the HOME CASTLE pool: the catalog troops with dwelling=="castle"
    // (militia/archers/pikemen/knights/cavalry), effectively unlimited (gold/
    // leadership bounded), reached by SAILING to the home zone's gate. Always
    // available regardless of the hero's current zone — this is the cross-zone
    // guarantee the router used to (wrongly) lose when off the home zone.
    {
        int tc = troops_count();
        for (int i = 0; i < tc && n < cap; i++) {
            const TroopDef *td = troop_by_index(i);
            if (!td || !td->id[0]) continue;
            if (strcmp(td->dwelling, "castle") != 0) continue;
            snprintf(out[n].troop_id, sizeof out[n].troop_id, "%.23s", td->id);
            out[n].avail = 1 << 28;   // home pool: effectively unlimited
            out[n].tier  = RSRC_HOME_CASTLE;
            snprintf(out[n].zone, sizeof out[n].zone, "%.23s", hz);
            out[n].x = -1; out[n].y = -1;   // gate resolved by the router
            n++;
        }
    }

    // TIER 3 — every dwelling in ANOTHER zone: acquirable by sailing to that map.
    // Last preference (a whole-map trip), but never EXCLUDED — a winning army may
    // need a troop sold only off-zone.
    for (int i = 0; i < g->dwelling_count && n < cap; i++) {
        const DwellingState *d = &g->dwellings[i];
        if (d->count <= 0 || !d->troop_id[0]) continue;
        if (strcmp(d->zone, cz) == 0) continue;   // tier 1 handled it
        if (!troop_by_id(d->troop_id)) continue;
        snprintf(out[n].troop_id, sizeof out[n].troop_id, "%.23s", d->troop_id);
        out[n].avail = d->count;
        out[n].tier  = RSRC_OFFZONE_DWELLING;
        snprintf(out[n].zone, sizeof out[n].zone, "%.23s", d->zone);
        out[n].x = d->x; out[n].y = d->y;
        n++;
    }
    return n;
}

// The reachable recruit menu (the OPTIMIZER's view): distinct troop types the hero
// can field, deduped from recruit_sources_enumerate. A thin adapter over the single
// source of truth, so the menu and the physical router (recruit_excursion_target)
// can never disagree about what is acquirable.
//   - home_pool_only restricts to the cross-zone-DELIVERABLE set (home pool only),
//     used to re-derive an army a STEP_RECRUIT_HOME can field at the home gate.
//   - otherwise ALL tiers are offered, including off-zone dwellings (acquirable by
//     sailing) — the army the search adopts is then realized by the router's
//     current-zone walk / home-gate sail / off-zone-dwelling sail.
static int stack_upkeep(const TroopDef *td, int count) {
    if (!td || count <= 0) return 0;
    return count * (td->recruit_cost / 10);
}

// Current army's total weekly upkeep.
int army_upkeep(const Game *g) {
    int u = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
        u += stack_upkeep(troop_by_id(g->army[i].id), g->army[i].count);
    }
    return u;
}

// SUSTAINABLE weekly-upkeep ceiling for the hero (the economy guard). The
// engine charges army upkeep every week-end and REPOSSESSES the boat if gold then
// falls below boat_cost (game.c:922-931). An army whose upkeep dwarfs income
// drives gold negative and strands the hero (boat lost) — the dominant cause of
// false "unreachable" deferrals. Cap total upkeep at the weekly commission income
// PLUS a few weeks' worth of the current gold surplus (keeping a boat_cost
// reserve), so the army is large but its upkeep never bankrupts the run. Pure
// read of live economy; deterministic.
int army_stack_count(const Game *g) {
    int n = 0;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
        if (g->army[s].id[0] && g->army[s].count > 0) n++;
    return n;
}

long army_hp_worth(const ArmyStack *army) {
    long w = 0;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!army[s].id[0] || army[s].count <= 0) continue;
        const TroopDef *td = troop_by_id(army[s].id);
        if (td) w += (long)army[s].count * td->hit_points;
    }
    return w;
}

int recruit_affordable_count(const Game *g, const char *troop_id) {
    const TroopDef *td = troop_by_id(troop_id);
    if (!td || td->recruit_cost <= 0) return 1 << 20;   // free / unpriced: no gold limit
    if (!g || g->stats.gold <= 0) return 0;
    return g->stats.gold / td->recruit_cost;
}

// Simulate (mode,tgt) on a COPY of `g` and report the outcome plus survivor
// metrics. Side-effect-free: runs on a heap copy, snapshots/restores the world
// RNG. The DEFENDER metrics come from a metrics-only combat record (entries=NULL,
// so no per-action recording cost — see combat.h CombatTurnRecord).
CombatResult predict_combat_eval(const Game *g, CombatMode mode,
                                 const CombatTarget *tgt,
                                 const ArmyStack *army_override,
                                 int *out_hero_stacks,
                                 int *out_def_stacks, long *out_def_hp,
                                 long *out_hero_hp) {
    if (out_hero_stacks) *out_hero_stacks = 0;
    if (out_def_stacks)  *out_def_stacks  = 0;
    if (out_def_hp)      *out_def_hp       = 0;
    if (out_hero_hp)     *out_hero_hp      = 0;
    if (!g) return COMBAT_RESULT_LOSS;
    {
        static long s_sims = 0;
        s_sims++;
        if ((s_sims % 2000) == 0) fprintf(stdout, "COMBAT_SIMS=%ld\n", s_sims);
    }
    uint64_t rng = GameRngSnapshot();
    Game *tmp = malloc(sizeof *tmp);
    if (!tmp) { GameRngRestore(rng); return COMBAT_RESULT_LOSS; }
    *tmp = *g;                       // value copy; res pointer shared (read-only)
    if (army_override)
        memcpy(tmp->army, army_override, sizeof tmp->army);
    CombatTurnRecord rec;            // metrics-only: entries=NULL, cap=0
    memset(&rec, 0, sizeof rec);
    CombatResult r = combat_run_headless_rec(tmp, mode, tgt, 256,
                                             autoplay_combat_policy, NULL, &rec);
    if (out_hero_stacks) *out_hero_stacks = army_stack_count(tmp);
    if (out_def_stacks)  *out_def_stacks  = rec.ai_stacks;
    if (out_def_hp)      *out_def_hp       = rec.ai_hp;
    if (out_hero_hp)     *out_hero_hp      = army_hp_worth(tmp->army);
    free(tmp);
    GameRngRestore(rng);
    return r;
}

// Outcome + hero surviving stacks (the historical contract). Thin wrapper.
CombatResult predict_combat_survivors(const Game *g, CombatMode mode,
                                      const CombatTarget *tgt,
                                      const ArmyStack *army_override,
                                      int *out_survivors) {
    return predict_combat_eval(g, mode, tgt, army_override, out_survivors, NULL, NULL, NULL);
}
