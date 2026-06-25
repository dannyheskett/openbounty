// autoplay/exec_recruit.c
//
// Executor RECRUIT — THE SINGLE RECRUITMENT SEAM (see exec.h; docs/RECRUIT-CONSOLIDATION.md).
// ALL substantive recruitment — real and trial — lives behind exec_recruit():
//   exec_recruit          — the polymorphic seam: dispatch on RecruitRequest.mode  (HELPER #7)
//   recruit_apply_target  — APPLY a precomputed ArmyTarget (dismiss-then-buy)       [private]
//   try_recruit_for_win / try_recompose_for_win / exec_build_for                    [private]
//                         — the three pre-fight army SEARCHES (decide via prediction)
//   exec_recruit_one      — buy n of a troop at one source                          (HELPER #8)
//   exec_recruit_sources  — enumerate recruitable troops + sources                  (HELPER #9)
//   exec_dismiss          — dismiss a held stack                                    (HELPER #10)
//
// The three searches were MOVED here from primitives.c so the executor's primitives
// never re-implement recruitment; their once-duplicated snippets are folded into the
// shared private statics below (dismiss_held_not_in / rec_step_off_tile / held_ratio_for
// / recruit_trial_count / pick_best_winner). Source enumeration + combat prediction
// (the old recruit.c) are exec_recruit's / exec_fight's internals.

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
#include "adventure.h"      // adventure_walkable_on_foot (moved searchers' step-off / foot gate)
#include "tables.h"         // troop_by_id / TroopDef (moved searchers' stack-power reads)

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

// ---- shared recruit-search internals (Stage-2 dedup of the moved searches) ----

// 8-neighbour offsets, shared by the foot-reachability scan and the step-off scan.
static const int SDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int SDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

// Dismiss every held stack whose troop id is NOT among keep_id[0..keep_n) (rescans
// after each dismiss — slots compact). Shared by the APPLY core and the rebuild commit.
static void dismiss_held_not_in(Game *g, RecSink *rec, char keep_id[][24], int keep_n) {
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        bool kept = false;
        for (int i = 0; i < keep_n; i++)
            if (keep_id[i][0] && strcmp(keep_id[i], g->army[s].id) == 0) { kept = true; break; }
        if (kept) continue;
        if (exec_dismiss(g, s, rec)) s = -1;   // dismissed; slots shifted -> rescan
    }
}

// After a recruit commits while the hero stands on a dwelling (an interactive tile), step
// onto an adjacent walkable empty tile so the next exec_reach starts from walkable ground;
// a walk-on chest there raises FLOW_CHEST_CHOICE — take the gold. Mirrors exec_reach's
// post-travel chest handling. Shared by all three searches.
static void rec_step_off_tile(Game *g, Map *map, Fog *fog, const Resources *res, RecSink *rec) {
    const Tile *here = MapGetTile(map, g->position.x, g->position.y);
    if (here && here->interactive != INTERACT_NONE) {
        for (int k = 0; k < 8; k++) {
            int nx = g->position.x + SDX[k], ny = g->position.y + SDY[k];
            const Tile *nt = MapGetTile(map, nx, ny);
            if (nt && nt->interactive == INTERACT_NONE &&
                adventure_walkable_on_foot(nt)) {
                exec_step(g, map, fog, res, SDX[k], SDY[k], rec);
                if (pending_flow == FLOW_CHEST_CHOICE) {
                    FlowAnswer take_gold = { FLOW_ANS_1, 0 };
                    exec_answer(g, map, take_gold, rec);
                }
                break;
            }
        }
    }
}

// The held army's own survival ratio vs (mode,tgt): surviving hp-worth as a % of committed
// worth, or -1 if the held army does not itself win with >= min_survivors.
static long held_ratio_for(const Game *g, CombatMode mode, const CombatTarget *tgt,
                           int min_survivors) {
    long held_worth = army_hp_worth(g->army);
    int hs = 0; long hp = 0;
    if (predict_combat_eval(g, mode, tgt, NULL, &hs, NULL, NULL, &hp)
            == COMBAT_RESULT_WIN && hs >= min_survivors && held_worth > 0)
        return hp * 100 / held_worth;
    return -1;
}

// Trial recruit count for a search: the source's stock cap (avail vs leadership maxn), then
// clamped by what gold affords. SEARCH clamp only — exec_recruit_one keeps its own n-aware
// buy clamp and exec_build_for keeps its running-budget variant.
static int recruit_trial_count(const Game *g, const RecruitSource *src, int maxn) {
    int try_n = (src->avail > 0 && src->avail < maxn) ? src->avail : maxn;
    int aff = recruit_affordable_count(g, src->troop_id);
    if (aff < try_n) try_n = aff;
    return try_n;
}

// Highest-ratio not-yet-used winner index (strict >, ascending-k tiebreak -> lowest index),
// or -1 if all used. Shared by the add/recompose winner-commit loops.
static int pick_best_winner(const long *wr, const bool *used, int nw) {
    int b = -1;
    for (int k = 0; k < nw; k++) if (!used[k] && (b < 0 || wr[k] > wr[b])) b = k;
    return b;
}

// recruit_apply_target — the APPLY core (RECRUIT_APPLY_TARGET). Assemble the held army
// to `target`: dismiss held stacks the target dropped (freeing leadership), then buy
// each target stack's shortfall at its preferred source. Do-or-fail — any unreachable/
// unbuyable source fails the whole contract. PRIVATE: reached only through exec_recruit().
static bool recruit_apply_target(Game *g, Map *map, Fog *fog, const Resources *res,
                  const ArmyTarget *target, RecSink *rec) {
    if (!target || target->n <= 0) return true;        // empty target: trivially met

    // 1. Dismiss held stacks whose type is NOT in the target (substituted away).
    char keep[GAME_ARMY_SLOTS][24];
    int keep_n = 0;
    for (int i = 0; i < target->n; i++)
        if (target->slot[i].id[0])
            snprintf(keep[keep_n++], sizeof keep[0], "%.23s", target->slot[i].id);
    dismiss_held_not_in(g, rec, keep, keep_n);

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
//     used to re-derive an army the home pool can field at the home gate.
//   - otherwise ALL tiers are offered, including off-zone dwellings (acquirable by
//     sailing) — the army the search adopts is then realized by the router's
//     current-zone walk / home-gate sail / off-zone-dwelling sail.
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

// ===================== THE RECRUITMENT SEAM (polymorphic exec_recruit) =====================
//
// The pre-fight army SEARCHES (add-to-slot / swap-weakest / full greedy rebuild) and the
// APPLY core (recruit_apply_target, above) are PRIVATE to this TU and reached ONLY through
// exec_recruit(). Moved here verbatim from primitives.c (Stage 1): the substantive
// "which army to recruit" logic now lives behind this one seam, not in the executor's
// primitives. exec_slay / exec_siege drive the 3-tier escalation as an explicit ladder of
// ADD -> RECOMPOSE -> BUILD seam calls (RECRUIT_APPLY_TARGET commits a precomputed target).
// docs/RECRUIT-CONSOLIDATION.md.

// A recruit SOURCE is usable only if the hero can get to it WITHOUT a stranding boat
// detour. An IN-ZONE dwelling must be FOOT-reachable — the dwelling bounces, so reach a
// walkable NEIGHBOUR on foot ALONE (no boat in the travel state). Sailing to a boat-only
// ISLAND dwelling maroons the hero: he disembarks onto a one-tile pocket and the boat is
// left on a water tile he can no longer step to (and, broke after recruiting, cannot rent
// another) — the seed-1 ghost-dwelling strand. The home pool (sail to the home castle, a
// documented haven the hero can sail back from) and off-zone dwellings keep their own
// reachability handling and are not gated here. SDX/SDY are the 8-neighbour offsets.
static bool recruit_source_foot_ok(const Game *g, const Map *map,
                                   const RecruitSource *src) {
    if (src->tier != RSRC_INZONE_DWELLING) return true;
    NavOptions o; nav_default_options(&o);
    NavTravel foot = { NAV_MODE_FOOT, false, -1, -1 };
    NavPoint from = { g->position.x, g->position.y };
    for (int k = 0; k < 8; k++) {
        NavPoint nb = { src->x + SDX[k], src->y + SDY[k] };
        const Tile *t = MapGetTile(map, nb.x, nb.y);
        if (!t || !adventure_walkable_on_foot(t)) continue;
        if (nav_reachable_travel(map, from, &foot, nb, &o, NULL)) return true;
    }
    return false;
}

// Try to recruit troops from one reachable source to flip a predicted LOSS to a WIN.
// Tries every reachable source ranked by survival ratio (best wins, not first-wins).
// Only recruits if the result is strictly better than the held army's own ratio.
// ADDS to an existing slot (or fills a free slot); never dismisses existing troops.
// Returns true if a recruit was performed (army now predicts a better fight).
static bool try_recruit_for_win(Game *g, Map *map, Fog *fog, const Resources *res,
                                CombatMode mode, const CombatTarget *tgt,
                                RecSink *rec, int min_survivors) {
    RecruitSource srcs[64];
    int ns = exec_recruit_sources(g, srcs, 64);
    long held_ratio = held_ratio_for(g, mode, tgt, min_survivors);

    long wr[64]; int wi[64], wn[64], nw = 0;
    int n_maxn_zero = 0, n_slot_full = 0, n_trial_loss = 0, n_unreach = 0;
    for (int i = 0; i < ns; i++) {
        if (!recruit_source_foot_ok(g, map, &srcs[i])) { n_unreach++; continue; }
        int maxn = GameMaxRecruitable(g, srcs[i].troop_id);
        if (maxn <= 0) { n_maxn_zero++; continue; }
        int try_n = recruit_trial_count(g, &srcs[i], maxn);
        if (try_n <= 0) { n_maxn_zero++; continue; }

        ArmyStack trial[GAME_ARMY_SLOTS];
        memcpy(trial, g->army, sizeof trial);
        int slot = -1;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++)
            if (trial[s].id[0] && strcmp(trial[s].id, srcs[i].troop_id) == 0)
                { slot = s; break; }
        if (slot < 0)
            for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                if (!trial[s].id[0]) { slot = s; break; }
        if (slot < 0) { n_slot_full++; continue; }
        snprintf(trial[slot].id, sizeof trial[slot].id, "%.23s", srcs[i].troop_id);
        trial[slot].count += try_n;
        int surv = 0; long post_hp = 0;
        if (predict_combat_eval(g, mode, tgt, trial, &surv, NULL, NULL, &post_hp)
                != COMBAT_RESULT_WIN || surv < min_survivors) { n_trial_loss++; continue; }
        long committed = army_hp_worth(trial);
        long ratio = committed > 0 ? post_hp * 100 / committed : 0;
        if (ratio > held_ratio && nw < 64) { wr[nw] = ratio; wi[nw] = i; wn[nw] = try_n; nw++; }
    }

    bool used[64]; for (int k = 0; k < nw; k++) used[k] = false;
    for (int picked = 0; picked < nw; picked++) {
        int b = pick_best_winner(wr, used, nw);
        if (b < 0) break;
        used[b] = true;
        if (!exec_recruit_one(g, map, fog, res, &srcs[wi[b]], wn[b], rec))
            continue;
        printf("try_recruit: OK vs '%s' — recruited %d %s (survival ratio -> %ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?", wn[b], srcs[wi[b]].troop_id, wr[b]);
        rec_step_off_tile(g, map, fog, res, rec);
        return true;
    }
    if (ns > 0)
        printf("try_recruit: FAIL vs '%s' — maxn_zero=%d slot_full=%d trial_loss=%d "
               "unreach=%d winners=%d (total_srcs=%d, held_ratio=%ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?",
               n_maxn_zero, n_slot_full, n_trial_loss, n_unreach, nw, ns, held_ratio);
    return false;
}

// Swap the weakest held stack for the most powerful recruitable troop whose purchase
// flips the combat prediction from LOSS to WIN. Dismisses ONE stack, keeps the rest.
// Returns true if a recompose was performed.
static bool try_recompose_for_win(Game *g, Map *map, Fog *fog, const Resources *res,
                                  CombatMode mode, const CombatTarget *tgt,
                                  RecSink *rec, int min_survivors) {
    if (army_stack_count(g) < 2) return false;
    int weak_slot = -1; long weak_power = -1;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0]) continue;
        const TroopDef *td = troop_by_id(g->army[s].id);
        long power = (long)(td ? td->hit_points : 1) * g->army[s].count;
        if (weak_slot < 0 || power < weak_power) { weak_power = power; weak_slot = s; }
    }
    if (weak_slot < 0) return false;
    RecruitSource srcs[64];
    int ns = exec_recruit_sources(g, srcs, 64);
    long held_ratio = held_ratio_for(g, mode, tgt, min_survivors);
    long wr[64]; int wi[64], wn[64], nw = 0;
    for (int i = 0; i < ns; i++) {
        if (!recruit_source_foot_ok(g, map, &srcs[i])) continue;
        int maxn = GameMaxRecruitable(g, srcs[i].troop_id);
        if (maxn <= 0) continue;
        int try_n = recruit_trial_count(g, &srcs[i], maxn);
        if (try_n <= 0) continue;
        bool already_held = false;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++)
            if (g->army[s].id[0] && strcmp(g->army[s].id, srcs[i].troop_id) == 0)
                { already_held = true; break; }
        if (already_held) continue;
        ArmyStack trial[GAME_ARMY_SLOTS];
        memcpy(trial, g->army, sizeof trial);
        snprintf(trial[weak_slot].id, sizeof trial[weak_slot].id, "%.23s", srcs[i].troop_id);
        trial[weak_slot].count = try_n;
        int surv = 0; long post_hp = 0;
        if (predict_combat_eval(g, mode, tgt, trial, &surv, NULL, NULL, &post_hp)
                != COMBAT_RESULT_WIN || surv < min_survivors) continue;
        long committed = army_hp_worth(trial);
        long ratio = committed > 0 ? post_hp * 100 / committed : 0;
        if (ratio > held_ratio && nw < 64) { wr[nw] = ratio; wi[nw] = i; wn[nw] = try_n; nw++; }
    }
    bool used[64]; for (int k = 0; k < nw; k++) used[k] = false;
    for (int picked = 0; picked < nw; picked++) {
        int b = pick_best_winner(wr, used, nw);
        if (b < 0) break;
        used[b] = true;
        ArmyTarget target; memset(&target, 0, sizeof target);
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!g->army[s].id[0] || s == weak_slot) continue;
            snprintf(target.slot[target.n].id, sizeof target.slot[0].id,
                     "%.23s", g->army[s].id);
            target.slot[target.n].count = g->army[s].count;
            target.n++;
        }
        snprintf(target.slot[target.n].id, sizeof target.slot[0].id,
                 "%.23s", srcs[wi[b]].troop_id);
        target.slot[target.n].count = wn[b];
        target.n++;
        if (!recruit_apply_target(g, map, fog, res, &target, rec)) continue;
        printf("try_recompose: OK vs '%s' — swapped weak slot for %s x%d "
               "(survival ratio -> %ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?", srcs[wi[b]].troop_id, wn[b], wr[b]);
        rec_step_off_tile(g, map, fog, res, rec);
        return true;
    }
    return false;
}

// Pre-fight army builder (fallback). If the current army already wins (mode, tgt) with
// >= min_survivors, returns true immediately (no-op). Otherwise greedily fills
// all GAME_ARMY_SLOTS from available sources — in-zone dwellings (foot_ok),
// home castle (already gated in only) — dismissing current troops that don't fit
// the winning composition and buying the rest via exec_recruit_one. Off-zone
// dwellings are excluded (too slow; consistently fail exec_reach). Returns true
// iff the army now predicts WIN with a non-Pyrrhic survival ratio.
// Gates: (1) trial must not weaken the army overall; (2) survival ratio >= PRESERVE_MIN_RATIO_PCT.
static bool exec_build_for(Game *g, Map *map, Fog *fog, const Resources *res,
                           CombatMode mode, const CombatTarget *tgt,
                           RecSink *rec, int min_survivors) {

    // Already winning with enough survivors? Nothing to do.
    int cur_surv = 0;
    if (predict_combat_eval(g, mode, tgt, NULL, &cur_surv, NULL, NULL, NULL)
            == COMBAT_RESULT_WIN && cur_surv >= min_survivors)
        return true;

    RecruitSource srcs[64];
    int ns = exec_recruit_sources(g, srcs, 64);
    if (ns == 0) return false;

    bool src_ok[64];
    int  src_maxn[64], src_cost[64];
    for (int i = 0; i < ns; i++) {
        src_ok[i]   = false;
        src_maxn[i] = 0;
        src_cost[i] = 0;
        if (srcs[i].tier == RSRC_INZONE_DWELLING)
            src_ok[i] = recruit_source_foot_ok(g, map, &srcs[i]);
        else if (srcs[i].tier == RSRC_HOME_CASTLE)
            src_ok[i] = true;   // exec_recruit_one sails there if needed
        // RSRC_OFFZONE_DWELLING: remain false

        if (!src_ok[i]) continue;
        int mn = GameMaxRecruitable(g, srcs[i].troop_id);
        if (mn <= 0) { src_ok[i] = false; continue; }
        src_maxn[i] = (srcs[i].avail > 0 && srcs[i].avail < mn) ? srcs[i].avail : mn;
        const TroopDef *td = troop_by_id(srcs[i].troop_id);
        src_cost[i] = td ? td->recruit_cost : 0;
    }

    // Simulation-guided greedy: at each slot try every remaining reachable source,
    // commit the one that produces the best predicted outcome (WIN > LOSS; highest
    // surviving hero hp-worth within equal result). Stop when the partial trial wins.
    ArmyStack trial[GAME_ARMY_SLOTS];
    memset(trial, 0, sizeof trial);
    int slot_src[GAME_ARMY_SLOTS];
    memset(slot_src, -1, sizeof slot_src);
    int  n_slots  = 0;
    long remaining = g->stats.gold;
    bool used[64]; memset(used, 0, sizeof used);

    while (n_slots < GAME_ARMY_SLOTS) {
        int  best_src = -1, best_try_n = 0, best_surv = 0;
        long best_post = -1;
        CombatResult best_r = COMBAT_RESULT_LOSS;

        for (int i = 0; i < ns; i++) {
            if (!src_ok[i] || used[i]) continue;
            int aff    = src_cost[i] > 0 ? (int)(remaining / src_cost[i]) : src_maxn[i];
            int try_n  = (src_maxn[i] < aff) ? src_maxn[i] : aff;
            if (try_n <= 0) continue;

            ArmyStack cand[GAME_ARMY_SLOTS];
            memcpy(cand, trial, sizeof cand);
            snprintf(cand[n_slots].id, sizeof cand[n_slots].id, "%.23s", srcs[i].troop_id);
            cand[n_slots].count = try_n;

            int surv = 0; long post_hp = 0;
            CombatResult r = predict_combat_eval(g, mode, tgt, cand,
                                                 &surv, NULL, NULL, &post_hp);
            bool better = (best_src < 0)
                       || (r == COMBAT_RESULT_WIN && best_r != COMBAT_RESULT_WIN)
                       || (r == best_r && post_hp > best_post);
            if (better) {
                best_src = i; best_try_n = try_n;
                best_r = r; best_post = post_hp; best_surv = surv;
            }
        }
        if (best_src < 0) break;

        snprintf(trial[n_slots].id, sizeof trial[n_slots].id,
                 "%.23s", srcs[best_src].troop_id);
        trial[n_slots].count = best_try_n;
        slot_src[n_slots]    = best_src;
        n_slots++;
        remaining -= (long)best_try_n * src_cost[best_src];
        used[best_src] = true;

        if (best_r == COMBAT_RESULT_WIN && best_surv >= min_survivors) break;
    }

    if (n_slots == 0) return false;

    int final_surv = 0; long post_hp = 0;
    if (predict_combat_eval(g, mode, tgt, trial, &final_surv, NULL, NULL, &post_hp)
            != COMBAT_RESULT_WIN || final_surv < min_survivors) {
        printf("exec_build_for: FAIL vs '%s' — %d-slot trial LOSES (surv=%d min=%d)\n",
               tgt->seed_key ? tgt->seed_key : "?", n_slots, final_surv, min_survivors);
        return false;
    }

    // Gate 1: the trial must commit at least as much hp-worth as the current army.
    // Rebuilding to a weaker composition and then fighting leaves the hero weaker
    // for all subsequent objectives. Prefer deferring until a genuinely stronger
    // army can be built (a later, stronger recruit pass).
    long pre_build_worth = army_hp_worth(g->army);
    long trial_hp_worth  = 0;
    for (int s = 0; s < n_slots; s++) {
        if (!trial[s].id[0] || trial[s].count <= 0) continue;
        const TroopDef *td = troop_by_id(trial[s].id);
        if (td) trial_hp_worth += (long)trial[s].count * td->hit_points;
    }
    if (pre_build_worth > 0 && trial_hp_worth < pre_build_worth) {
        printf("exec_build_for: SKIP vs '%s' — trial weaker (trial=%ld current=%ld)\n",
               tgt->seed_key ? tgt->seed_key : "?", trial_hp_worth, pre_build_worth);
        return false;
    }

    // Gate 2: the trial army must not fight Pyrrhically — same PRESERVE_MIN_RATIO_PCT
    // bar as exec_fight uses on the live army, but checked on the trial ahead of time.
    // This catches builds that are stronger in total but still fight inefficiently.
    if (trial_hp_worth > 0 && post_hp * 100 < trial_hp_worth * PRESERVE_MIN_RATIO_PCT) {
        printf("exec_build_for: SKIP vs '%s' — trial Pyrrhic (ratio=%ld%% min=%d%%)\n",
               tgt->seed_key ? tgt->seed_key : "?",
               post_hp * 100 / trial_hp_worth, PRESERVE_MIN_RATIO_PCT);
        return false;
    }

    // Dismiss troops that are not in the trial composition.
    char keep[GAME_ARMY_SLOTS][24];
    int keep_n = 0;
    for (int t = 0; t < n_slots; t++)
        if (trial[t].id[0])
            snprintf(keep[keep_n++], sizeof keep[0], "%.23s", trial[t].id);
    dismiss_held_not_in(g, rec, keep, keep_n);

    // Buy each trial slot's shortfall from the validated source.
    for (int s = 0; s < n_slots; s++) {
        if (!trial[s].id[0] || trial[s].count <= 0) continue;
        int held = 0;
        for (int st = 0; st < GAME_ARMY_SLOTS; st++)
            if (g->army[st].id[0] && strcmp(g->army[st].id, trial[s].id) == 0)
                { held = g->army[st].count; break; }
        int need = trial[s].count - held;
        if (need <= 0) continue;
        if (!exec_recruit_one(g, map, fog, res, &srcs[slot_src[s]], need, rec)) {
            printf("exec_build_for: FAIL vs '%s' — slot %d %s tier=%d gold=%d\n",
                   tgt->seed_key ? tgt->seed_key : "?", s,
                   trial[s].id, srcs[slot_src[s]].tier, g->stats.gold);
            return false;
        }
    }

    long committed = army_hp_worth(g->army);
    printf("exec_build_for: OK vs '%s' — %d slots (survival ratio %ld%%)\n",
           tgt->seed_key ? tgt->seed_key : "?", n_slots,
           committed > 0 ? post_hp * 100 / committed : 0);

    // Step off the dwelling tile so the subsequent exec_reach starts from walkable ground.
    rec_step_off_tile(g, map, fog, res, rec);
    return true;
}

// exec_recruit — the single public recruitment entry. Dispatches on req->mode to the
// APPLY core or one of the three pre-fight searches and returns that path's OWN verdict
// (no post-commit re-prediction). The only recruitment surface the executor calls.
bool exec_recruit(Game *g, Map *map, Fog *fog, const Resources *res,
                  const RecruitRequest *req, RecSink *rec) {
    if (!req) return false;
    switch (req->mode) {
    case RECRUIT_APPLY_TARGET:
        return recruit_apply_target(g, map, fog, res, req->target, rec);
    case RECRUIT_ADD_FOR_WIN:
        return try_recruit_for_win(g, map, fog, res, req->combat_mode, req->tgt,
                                   rec, req->min_survivors);
    case RECRUIT_RECOMPOSE_FOR_WIN:
        return try_recompose_for_win(g, map, fog, res, req->combat_mode, req->tgt,
                                     rec, req->min_survivors);
    case RECRUIT_BUILD_FOR_WIN:
        return exec_build_for(g, map, fog, res, req->combat_mode, req->tgt,
                              rec, req->min_survivors);
    default:
        return false;
    }
}
