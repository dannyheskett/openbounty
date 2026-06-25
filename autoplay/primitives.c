// autoplay/primitives.c
//
// THE EXECUTOR: execute() runs ONE high-level primitive on a Game, problem-solving
// it into engine actions, recording the replayable engine prims it emits, and
// reporting success/failure. The backtracking planner calls it per step (see
// primitives.h for the search model).
//
// STATUS: the STATE primitives (WAIT, CAST) and every TARGET primitive that an
// objective enumerates are wired end-to-end on nav(): FETCH (walk-onto consume),
// LEARN (alcove), DWELL (recruit), TOWN (all four services: siege weapons, contract,
// spell, boat), SLAY (foe fight), SIEGE (monster-castle / villain assault + garrison),
// DIG (the scepter finale), and the ARMY recruiting contract. Each does exactly what
// its primitive asks or returns false — a do-or-fail oracle the planner reads as
// "can't carry this out from here now" and backtracks past. PRIM_HOME has no
// enumerated objective (the home pool is a PRIM_ARMY recruit source). Recruit
// sources on OTHER zones — the home-pool gate and off-zone dwellings — are reached
// by sailing there (exec_cross_to_zone), so the ARMY contract is fully cross-zone.
//
// NOTE: TOWN_SIEGE and TOWN_CONTRACT are emitted by the planner — TOWN_SIEGE as the
// siege-weapons objective, TOWN_CONTRACT as a villain siege's prerequisite (the
// planner sequences it before the SIEGE). TOWN_SPELL/TOWN_BOAT are executable here
// but no planstep emits them yet.

#include "primitives.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goals.h"        // PlanStep / PlanKind — the objective type execute() accepts
#include "exec.h"         // the flat executor helpers + shared sink emitters
#include "recording.h"
#include "game.h"
#include "spells_adventure.h"
#include "navigator.h"   // nav() — the movement sub-solver (the mover, used directly)
#include "nav.h"         // nav_reachable / nav_default_options — foot-reachability gate
#include "step.h"        // GameStep — the attack step onto a foe
#include "pending.h"     // pending_flow / pending_foe_id — the flow GameStep raised
#include "combat.h"      // combat_run_headless_* / CombatTarget / CombatResult
#include "combat_policy.h" // autoplay_combat_policy / autoplay_predict_combat_ex
#include "player_io.h"   // player_io_answer — apply the combat answer
#include "recruit.h"     // ArmyTarget / RecruitSource enumerate — the ARMY contract
#include "resources.h"   // ResCastle (locate the audience/home castle gate)
#include "flow_resolve.h" // flow_apply_dismiss_army — dismiss a substituted-out stack
#include "map.h"         // MapGetTile — locate the home castle's real gate tile
#include "tile.h"        // INTERACT_CASTLE_GATE
#include "adventure.h"   // adventure_walkable_on_foot — pick a foot-standable approach
#include "tables.h"      // troop_by_id — weakest-stack power for the garrison choice
// (recruit.h provides predict_combat_survivors; flow_resolve.h provides
// flow_apply_search — both already included above.)

// (CASTLE_GARRISON_MIN_SURVIVORS now lives in exec.h, next to exec_fight which
// applies the rule.)

const char *prim_kind_name(PrimKind k) {
    switch (k) {
    case PRIM_FETCH: return "FETCH";
    case PRIM_TOWN:  return "TOWN";
    case PRIM_DWELL: return "DWELL";
    case PRIM_HOME:  return "HOME";
    case PRIM_SIEGE: return "SIEGE";
    case PRIM_SLAY:  return "SLAY";
    case PRIM_DIG:   return "DIG";
    case PRIM_LEARN: return "LEARN";
    case PRIM_ARMY:  return "ARMY";
    case PRIM_CAST:  return "CAST";
    case PRIM_WAIT:  return "WAIT";
    default:         return "?";
    }
}

// Append a REC_ACTION primitive (a direct, replayable engine action) to the sink.
// Shared across the executor TUs (declared in exec.h).
void rec_push_action(RecSink *rec, RecActionKind a,
                     const char *id, int x, int y) {
    if (!rec || !rec->prims) return;
    RecPrim rp; memset(&rp, 0, sizeof rp);
    rp.kind = REC_ACTION; rp.action = a;
    if (id) snprintf(rp.act_id, sizeof rp.act_id, "%s", id);
    rp.act_x = x; rp.act_y = y;
    recbuf_push(rec->prims, rp);
}

// Append a REC_MOVE primitive (one adventure step). Used for the attack step onto
// a foe (nav() records its own approach moves; this records the final lunge).
// Shared across the executor TUs (declared in exec.h).
void rec_push_move(RecSink *rec, int dx, int dy) {
    if (!rec || !rec->prims) return;
    RecPrim rp; memset(&rp, 0, sizeof rp);
    rp.kind = REC_MOVE; rp.dx = (int8_t)dx; rp.dy = (int8_t)dy;
    recbuf_push(rec->prims, rp);
}

// =================================================================================
// Cross-cutting executor HELPERS (interaction / time / spell). These touch the
// engine directly and are shared by several primitives; they live here (the
// executor's primitive TU) rather than in a movement/combat/recruit/town group.
// Declared in exec.h.
// =================================================================================

// HELPER #16 — exec_answer. Apply the NON-combat flow GameStep just raised
// (alcove / dwelling recruit / chest choice) via the shared router, and record the
// REC_ANSWER. Returns false if no flow is pending. (Combat answers carry a fight
// outcome + combat-record index and are emitted inside exec_fight instead.)
bool exec_answer(Game *g, Map *map, FlowAnswer ans, RecSink *rec) {
    PendingFlow flow = pending_flow;
    if (flow == FLOW_NONE) return false;
    PlayerIoPresentation pres;
    player_io_answer(g, map, /*fog=*/NULL, g->res, ans,
                     PLAYER_IO_COMBAT_NOT_RUN, &pres);
    if (rec && rec->prims) {
        RecPrim rp; memset(&rp, 0, sizeof rp);
        rp.kind = REC_ANSWER;
        rp.ans = ans;
        rp.outcome = PLAYER_IO_COMBAT_NOT_RUN;
        rp.flow = flow;
        rp.rec_combat_index = -1;
        recbuf_push(rec->prims, rp);
    }
    return true;
}

// HELPER #17 — exec_cast. Apply the adventure spell at `spell_index`. Records
// RA_CAST_ADV_SPELL BEFORE applying (replay re-applies the same index), then
// returns the engine's result (false on a bad index / missing charge).
bool exec_cast(Game *g, int spell_index, RecSink *rec) {
    if (spell_index < 0) return false;
    rec_push_action(rec, RA_CAST_ADV_SPELL, NULL, spell_index, 0);
    return GameApplyAdventureSpellEffect(g, spell_index);
}

// HELPER #18 — exec_spend_week. Pass ONE week (RA_WAIT_WEEK + GameSpendWeek),
// banking commission income. Returns true if the game is still live afterward.
// (The WAIT primitive loops this for multi-week waits.)
bool exec_spend_week(Game *g, RecSink *rec) {
    if (g->stats.game_over) return false;
    rec_push_action(rec, RA_WAIT_WEEK, NULL, 0, 0);
    int commission = 0;
    GameSpendWeek(g, &commission);
    return !g->stats.game_over;
}

// (Combat resolution — foe AND castle, target + win-bar derived from the pending
// flow — now lives ONCE in exec_fight (exec_fight.c). Callers step onto the tile
// and call exec_fight(g, map, rec). Garrison-weakest likewise lives ONCE in
// exec_garrison.)

// (Movement now lives ONCE in exec_move.c: exec_travel (A->B by any means, the single
// fight-through router) and exec_reach (travel up to a bouncer and step ONTO it).
// They replace the old exec_nav / exec_reach_and_enter / exec_cross_to_zone /
// exec_clear_foe — which reused the same nav() this file used — with no duplication.)

// (Recruiting — dismiss, per-source buy, and the home-pool gate path — now lives
// ONCE in exec_recruit / exec_recruit_one (exec_recruit.c). PRIM_ARMY calls
// exec_recruit(g, map, fog, res, &p->army, rec).)

// =================================================================================
// THE 11 PRIMITIVES (orchestration only). The planner selects exactly one per
// objective; execute() dispatches to it. A primitive READS game state to decide and
// calls HELPERS to act — it never issues an engine MUTATION directly. (The lone
// exception is exec_dig's one flow_apply_search call: FLOW_SEARCH is the 'S'
// action, not raised by stepping on, and there is no search helper among the 18.)
// Each answers any flow it provokes via exec_answer / exec_fight, so pending_flow is
// never left dangling across the planner's snapshot boundary.
// =================================================================================

// A target primitive's zone may be empty, meaning "the hero's current zone".
#define PRIM_DZ(p, g) ((p)->zone[0] ? (p)->zone : (g)->position.zone)

// FETCH: a consumable on a tile (chest / artifact / navmap / orb). GameStep CONSUMES
// it by walking ONTO the tile, so reaching the tile IS the fetch — fight-through so a
// wandering foe on the road is cleared en route.
// Gold chests raise FLOW_CHEST_CHOICE (gold vs. leadership). Answer it; without an
// explicit answer neither reward is applied. Take leadership while below the threshold
// so the hero can field a stronger army; take gold above it so sieges and recruits
// stay funded. (player_accept_rank is a no-op stub, so rank kills don't grow
// leadership — chests are the only source.)
static bool exec_fetch(const Primitive *p, Game *g, Map *map, Fog *fog,
                       const Resources *res, RecSink *rec) {
    if (!exec_travel(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                     /*fight_through=*/true, rec))
        return false;
    if (pending_flow == FLOW_CHEST_CHOICE) {
        // Take leadership when no rank-ups have come from villain catches yet and
        // current leadership is below the first promoted rank's target. Once any
        // villain is caught (rank-up path is open), switch to gold to fund
        // recruits and sieges. Without this, a hero stuck at starting leadership
        // has no way to build a stronger army.
        const ClassDef *cls = class_by_id(g->character.cls.id);
        int next_rank_lead = 0;
        if (cls && cls->rank_count > 1)
            class_stats_at_rank(cls, 1, &next_rank_lead, NULL, NULL, NULL);
        bool take_lead = GameVillainsCaught(g) == 0
                         && next_rank_lead > 0
                         && g->stats.leadership_current < next_rank_lead;
        FlowAnswer ans = { take_lead ? FLOW_ANS_2 : FLOW_ANS_1, 0 };
        exec_answer(g, map, ans, rec);
    }
    return true;
}

// A recruit SOURCE is usable only if the hero can get to it WITHOUT a stranding boat
// detour. An IN-ZONE dwelling must be FOOT-reachable — the dwelling bounces, so reach a
// walkable NEIGHBOUR on foot ALONE (no boat in the travel state). Sailing to a boat-only
// ISLAND dwelling maroons the hero: he disembarks onto a one-tile pocket and the boat is
// left on a water tile he can no longer step to (and, broke after recruiting, cannot rent
// another) — the seed-1 ghost-dwelling strand. The home pool (sail to the home castle, a
// documented haven the hero can sail back from) and off-zone dwellings keep their own
// reachability handling and are not gated here. SDX/SDY are the 8-neighbour offsets.
static bool recruit_source_foot_ok(const Game *g, const Map *map,
                                   const RecruitSource *src,
                                   const int *SDX, const int *SDY) {
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

// Try each in-zone dwelling source to find one whose purchase flips the combat
// prediction from LOSS to WIN. Builds a trial army on the stack (copy of g->army plus
// the candidate troop), passes it to predict_combat_survivors as army_override so g
// is never mutated. Only considers dwellings that are foot-reachable from the hero's
// current position (no boat needed, via recruit_source_foot_ok), so exec_recruit_one
// cannot acquire a boat whose subsequent presence would disrupt the foe-approach
// navigation — or strand the hero on a boat-only island. Buys the best-ratio winning
// source for real; returns true if a recruit was made.
bool try_recruit_for_win(Game *g, Map *map, Fog *fog, const Resources *res,
                         CombatMode mode, const CombatTarget *tgt,
                         RecSink *rec, int min_survivors) {
    static const int SDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int SDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };
    RecruitSource srcs[64];
    int ns = exec_recruit_sources(g, srcs, 64);
    long held_worth = army_hp_worth(g->army);

    // Baseline: the held army's OWN survival ratio (surviving hp-worth as a % of
    // committed), or -1 if it cannot win/hold. A recruit must strictly beat this — no
    // point paying gold for a force that survives a fight worse than what we already hold.
    long held_ratio = -1;
    {
        int hs = 0; long hp = 0;
        if (predict_combat_eval(g, mode, tgt, NULL, &hs, NULL, NULL, &hp)
                == COMBAT_RESULT_WIN && hs >= min_survivors && held_worth > 0)
            held_ratio = hp * 100 / held_worth;
    }

    // Score EVERY winning recruit option by survival ratio (not first-win-wins): field
    // the army that wins BEST, not the cheapest. Collect the beats-held winners, then buy
    // them in descending-ratio order until one is actually reachable.
    long wr[64]; int wi[64], wn[64], nw = 0;
    int n_maxn_zero = 0, n_slot_full = 0, n_trial_loss = 0, n_unreach = 0;
    for (int i = 0; i < ns; i++) {
        if (!recruit_source_foot_ok(g, map, &srcs[i], SDX, SDY)) { n_unreach++; continue; }
        int maxn = GameMaxRecruitable(g, srcs[i].troop_id);
        if (maxn <= 0) { n_maxn_zero++; continue; }
        // Cap by dwelling supply so the trial matches what exec_recruit_one will
        // actually deliver. Home pool has avail=INT_MAX so this is a no-op there.
        int try_n = (srcs[i].avail > 0 && srcs[i].avail < maxn) ? srcs[i].avail : maxn;
        int aff = recruit_affordable_count(g, srcs[i].troop_id);
        if (aff < try_n) try_n = aff;           // can't field more than the hero can pay for
        if (try_n <= 0) { n_maxn_zero++; continue; }

        // Build a trial army: the live army + the candidate troop. Passed as
        // army_override so the prediction never touches g.
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

    // Buy the best-ratio winner that is reachable (selection over the small winner set).
    bool used[64]; for (int k = 0; k < nw; k++) used[k] = false;
    for (int picked = 0; picked < nw; picked++) {
        int b = -1;
        for (int k = 0; k < nw; k++) if (!used[k] && (b < 0 || wr[k] > wr[b])) b = k;
        if (b < 0) break;
        used[b] = true;
        if (!exec_recruit_one(g, map, fog, res, &srcs[wi[b]], wn[b], rec))
            continue;  // source unreachable (nav/boat); army unchanged; try next-best
        printf("try_recruit: OK vs '%s' — recruited %d %s (survival ratio -> %ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?", wn[b], srcs[wi[b]].troop_id, wr[b]);
        // Step off the dwelling tile so nav can start from a plain-walkable
        // position on the subsequent exec_reach for the fight.
        const Tile *here = MapGetTile(map, g->position.x, g->position.y);
        if (here && here->interactive != INTERACT_NONE) {
            for (int k = 0; k < 8; k++) {
                int nx = g->position.x + SDX[k], ny = g->position.y + SDY[k];
                const Tile *nt = MapGetTile(map, nx, ny);
                if (nt && nt->interactive == INTERACT_NONE &&
                    adventure_walkable_on_foot(nt)) {
                    exec_step(g, map, fog, res, SDX[k], SDY[k], rec);
                    break;
                }
            }
        }
        // The recruit STACKS via normal admission: if the fight we recruited FOR wins,
        // the whole attempt (this recruit included) is kept, so the army is present for
        // every later objective. We deliberately do NOT checkpoint-persist on FAILURE — a
        // recruit detour that then can't re-reach the foe is a WASTED buy at a stranding
        // spot; locking it in maroons the hero (seed-1: recruit at a far dwelling, then
        // every later objective DEFERs from there). A failed attempt rolls back fully.
        return true;
    }
    // Permanent diagnostic: nothing beat the held army's own survival ratio.
    if (ns > 0)
        printf("try_recruit: FAIL vs '%s' — maxn_zero=%d slot_full=%d trial_loss=%d "
               "unreach=%d winners=%d (total_srcs=%d, held_ratio=%ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?",
               n_maxn_zero, n_slot_full, n_trial_loss, n_unreach, nw, ns, held_ratio);
    return false;
}

// Swap the weakest held stack for the most powerful recruitable troop whose purchase
// flips the combat prediction from LOSS to WIN. Escapes the slot-full-with-chaff trap:
// when all 5 army slots are taken by different weak types, there is no room to ADD a
// stronger type — but dismissing the weakest stack frees a slot. Tries all recruitable
// source tiers (in-zone dwelling, home castle, off-zone dwelling); exec_recruit handles
// sailing when needed. Keeps all other held stacks intact. Returns true if a recompose
// was performed; false if no single-slot swap flips the prediction.
bool try_recompose_for_win(Game *g, Map *map, Fog *fog, const Resources *res,
                           CombatMode mode, const CombatTarget *tgt,
                           RecSink *rec, int min_survivors) {
    if (army_stack_count(g) < 2) return false;   // last-stack guard: can't dismiss
    static const int SDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int SDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };
    // Find weakest held slot by hit_points * count.
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
    long held_worth = army_hp_worth(g->army);
    long held_ratio = -1;
    {
        int hs = 0; long hp = 0;
        if (predict_combat_eval(g, mode, tgt, NULL, &hs, NULL, NULL, &hp)
                == COMBAT_RESULT_WIN && hs >= min_survivors && held_worth > 0)
            held_ratio = hp * 100 / held_worth;
    }
    // Rank every weakest-slot swap by survival ratio; field the BEST that beats held.
    long wr[64]; int wi[64], wn[64], nw = 0;
    for (int i = 0; i < ns; i++) {
        if (!recruit_source_foot_ok(g, map, &srcs[i], SDX, SDY)) continue;  // no stranding sail
        int maxn = GameMaxRecruitable(g, srcs[i].troop_id);
        if (maxn <= 0) continue;
        // Cap by dwelling supply so the trial matches what exec_recruit will deliver.
        int try_n = (srcs[i].avail > 0 && srcs[i].avail < maxn) ? srcs[i].avail : maxn;
        int aff = recruit_affordable_count(g, srcs[i].troop_id);
        if (aff < try_n) try_n = aff;           // can't field more than the hero can pay for
        if (try_n <= 0) continue;
        // Skip troops already in the army: swapping the weakest slot for a type that is
        // already held elsewhere produces the same total force as recruitment (which
        // try_recruit_for_win already tested), and would also create a duplicate-type
        // entry in the ArmyTarget.
        bool already_held = false;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++)
            if (g->army[s].id[0] && strcmp(g->army[s].id, srcs[i].troop_id) == 0)
                { already_held = true; break; }
        if (already_held) continue;
        // Trial: swap the weakest slot for the candidate; keep all other slots unchanged.
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
    // Realize the best-ratio swap that is reachable.
    bool used[64]; for (int k = 0; k < nw; k++) used[k] = false;
    for (int picked = 0; picked < nw; picked++) {
        int b = -1;
        for (int k = 0; k < nw; k++) if (!used[k] && (b < 0 || wr[k] > wr[b])) b = k;
        if (b < 0) break;
        used[b] = true;
        // Build exec_recruit target: keep all stacks except the weakest, add the winner.
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
        if (!exec_recruit(g, map, fog, res, &target, rec)) continue;  // unreachable; next-best
        printf("try_recompose: OK vs '%s' — swapped weak slot for %s x%d "
               "(survival ratio -> %ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?", srcs[wi[b]].troop_id, wn[b], wr[b]);
        // Step off bouncer tile so nav starts from a plain-walkable position.
        const Tile *here = MapGetTile(map, g->position.x, g->position.y);
        if (here && here->interactive != INTERACT_NONE) {
            for (int k = 0; k < 8; k++) {
                int nx = g->position.x + SDX[k], ny = g->position.y + SDY[k];
                const Tile *nt = MapGetTile(map, nx, ny);
                if (nt && nt->interactive == INTERACT_NONE &&
                    adventure_walkable_on_foot(nt)) {
                    exec_step(g, map, fog, res, SDX[k], SDY[k], rec);
                    break;
                }
            }
        }
        // Stacks via admission on a winning fight (see try_recruit_for_win) — no
        // checkpoint-persist on failure, so a wasted recompose detour cannot maroon the hero.
        return true;
    }
    return false;
}

// SLAY: a hostile wandering foe at p->id. The foe BOUNCES, so reach it (step ONTO it,
// raising FLOW_ATTACK_FOE) then resolve the fight (predict-gated in exec_fight).
static bool exec_slay(const Primitive *p, Game *g, Map *map, Fog *fog,
                      const Resources *res, RecSink *rec) {
    FoeState *foe = p->id[0] ? GameFindFoe(g, p->id) : NULL;
    if (!foe || !foe->alive) return true;              // already gone
    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), foe->x, foe->y,
                    /*fight_through=*/true, rec))
        return false;                                  // unreachable now (deferred)
    // exec_travel's fight-through may have killed the foe as a side effect of
    // clearing path blockers. If so, the SLAY objective is already achieved.
    if (!foe->alive) return true;
    // Post-navigation predict: runs at the same rng_state exec_fight will use, so
    // pre-fight recruitment decisions are always consistent with the actual fight.
    {
        CombatTarget tgt; memset(&tgt, 0, sizeof tgt);
        tgt.name = "Hostile band"; tgt.seed_key = p->id;
        tgt.garrison = foe->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS;
        // Recruit/recompose only when the held army would LOSE — and when it does, the
        // pipeline fields the best-SURVIVING composition (ranked by ratio), not the
        // cheapest win. A winnable-but-Pyrrhic win is NOT recruited around here (that
        // would detour the hero to a dwelling before every costly fight); it is instead
        // DECLINED by exec_fight's survival-ratio gate, preserving the held army.
        if (predict_combat_survivors(g, COMBAT_MODE_FOE, &tgt, NULL, NULL) != COMBAT_RESULT_WIN) {
            bool recruited = try_recruit_for_win(g, map, fog, res, COMBAT_MODE_FOE, &tgt, rec, 1);
            bool recomposed = !recruited &&
                              try_recompose_for_win(g, map, fog, res, COMBAT_MODE_FOE, &tgt, rec, 1);
            if (recruited || recomposed) {
                // Recruitment navigated away; re-approach the foe to re-raise FLOW_ATTACK_FOE.
                if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), foe->x, foe->y,
                                /*fight_through=*/true, rec))
                    return false;
                if (!foe->alive) return true;  // killed again as fight-through side effect
            } else if (predict_combat_survivors(g, COMBAT_MODE_FOE, &tgt, NULL, NULL)
                       != COMBAT_RESULT_WIN) {
                printf("exec_slay: UNWINNABLE foe '%s' at (%d,%d) leadership=%d gold=%d"
                       " — hero:", p->id, foe->x, foe->y,
                       g->stats.leadership_current, g->stats.gold);
                for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                    if (g->army[s].id[0]) {
                        const TroopDef *td = troop_by_id(g->army[s].id);
                        printf(" %dx%s(hp=%d)", g->army[s].count, g->army[s].id,
                               td ? td->hit_points : 0);
                    }
                printf(" — foe:");
                for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                    if (foe->garrison[s].id[0]) {
                        const TroopDef *td = troop_by_id(foe->garrison[s].id);
                        printf(" %dx%s(hp=%d)", foe->garrison[s].count, foe->garrison[s].id,
                               td ? td->hit_points : 0);
                    }
                printf("\n");
            }
        }
    }
    return exec_fight(g, map, rec);
}

// LEARN: an archmage alcove at (x,y). Step onto it (FLOW_ALCOVE) and accept the
// lesson. One-shot: knows_magic is the engine-wide flag (any alcove sets it).
static bool exec_learn(const Primitive *p, Game *g, Map *map, Fog *fog,
                       const Resources *res, RecSink *rec) {
    if (g->stats.knows_magic) return true;             // already learned
    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                    /*fight_through=*/true, rec))
        return false;
    if (pending_flow != FLOW_ALCOVE) return false;     // not an alcove here
    FlowAnswer yes = { FLOW_ANS_YES, 0 };
    exec_answer(g, map, yes, rec);
    return g->stats.knows_magic;
}

// DWELL: a troop dwelling at (x,y). Step onto it (FLOW_RECRUIT) and buy up to `count`
// of the troop it sells (or its leadership/gold max if count<=0). Exercised through
// the ARMY contract's recruit sources, not as a standalone objective — but wired.
static bool exec_dwell(const Primitive *p, Game *g, Map *map, Fog *fog,
                       const Resources *res, RecSink *rec) {
    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                    /*fight_through=*/true, rec))
        return false;
    if (pending_flow != FLOW_RECRUIT) return false;
    int maxn = pending_dwelling_troop[0]
                 ? GameMaxRecruitable(g, pending_dwelling_troop) : 0;
    if (maxn < 0) maxn = 0;
    int n = (p->count > 0 && p->count < maxn) ? p->count : maxn;
    FlowAnswer a; a.kind = (n > 0) ? FLOW_ANS_YES : FLOW_ANS_NO; a.number = n;
    exec_answer(g, map, a, rec);
    return n > 0;
}

// TOWN: a town service. Each routes to the nearest town (exec_enter_town, the single
// fight-through router that leaves the hero gated in) then transacts via the matching
// town helper. in_town persists across the contract-cycle loop.
static bool exec_town(const Primitive *p, Game *g, Map *map, Fog *fog,
                      const Resources *res, RecSink *rec) {
    switch (p->town) {
    case TOWN_SIEGE:
        if (g->stats.siege_weapons) return true;       // already owned (skip the walk)
        if (!exec_enter_town(g, map, fog, res, /*allow_castle=*/false, NULL, NULL, rec))
            return false;
        return exec_buy_siege(g, rec);
    case TOWN_CONTRACT:
        // Capturing a villain only COUNTS with their contract active, so this gates
        // every villain objective. Already active -> done; else enter a town and take.
        if (p->id[0] && strcmp(g->contract.active_id, p->id) == 0) return true;
        if (!exec_enter_town(g, map, fog, res, /*allow_castle=*/false, NULL, NULL, rec))
            return false;
        return exec_take_contract(g, res, p->id, rec);
    case TOWN_SPELL:
        if (!exec_enter_town(g, map, fog, res, /*allow_castle=*/false, NULL, NULL, rec))
            return false;
        return exec_buy_spell(g, rec);
    case TOWN_BOAT: {
        if (g->boat.has_boat) return true;             // already holds a boat
        int dx = -1, dy = -1;
        if (!exec_enter_town(g, map, fog, res, /*allow_castle=*/false, &dx, &dy, rec))
            return false;
        return exec_rent_boat(g, dx, dy, rec);
    }
    default:
        return false;                                  // unknown service
    }
}

// ARMY: the recruiting contract — make the held army contain p->army or fail. All the
// dismiss-then-buy-each-source logic lives in exec_recruit.
static bool exec_army(const Primitive *p, Game *g, Map *map, Fog *fog,
                      const Resources *res, RecSink *rec) {
    return exec_recruit(g, map, fog, res, &p->army, rec);
}

// SIEGE: a hostile castle whose gate is at (x,y). The gate BOUNCES (raising no flow
// unless siege weapons are owned), so reach + step ONTO it and confirm a siege flow
// rose, then resolve the fight (exec_fight derives the >= 2-survivor monster bar). On
// a monster win, garrison the weakest stack to HOLD the castle; a villain win is done.
static bool exec_siege(const Primitive *p, Game *g, Map *map, Fog *fog,
                       const Resources *res, RecSink *rec) {
    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                    /*fight_through=*/true, rec))
        return false;                                  // gate unreachable now (deferred)
    PendingFlow flow = pending_flow;
    if (flow != FLOW_SIEGE_MONSTER && flow != FLOW_SIEGE_VILLAIN)
        return false;                                  // bounced: no siege weapons
    bool monster = (flow == FLOW_SIEGE_MONSTER);
    char castle_id[24] = {0};                          // capture before the answer clears it
    snprintf(castle_id, sizeof castle_id, "%s", pending_castle_id);

    // Post-navigation predict: runs at the same rng_state exec_fight will use, so the
    // two predictions are always in agreement. If LOSS, try recruit/recompose before
    // fighting. Recruitment navigates away (overwriting pending_flow with FLOW_RECRUIT),
    // so a second exec_reach re-raises the siege flow at the correct rng_state.
    {
        int min_survivors = monster ? CASTLE_GARRISON_MIN_SURVIVORS : 1;
        const CastleRecord *cr = GameFindCastleConst(g, castle_id);
        if (cr) {
            CombatTarget tgt; memset(&tgt, 0, sizeof tgt);
            tgt.name = "Castle garrison"; tgt.seed_key = castle_id;
            tgt.garrison = (Unit *)cr->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS;
            int surv = 0;
            CombatResult pre = predict_combat_survivors(g, COMBAT_MODE_CASTLE, &tgt, NULL, &surv);
            if (pre != COMBAT_RESULT_WIN || surv < min_survivors) {
                printf("exec_siege: post-predict '%s' LOSS (surv=%d min=%d) — trying recruit/recompose\n",
                       castle_id, surv, min_survivors);
                bool recruited = try_recruit_for_win(g, map, fog, res, COMBAT_MODE_CASTLE,
                                                     &tgt, rec, min_survivors);
                if (!recruited)
                    recruited = try_recompose_for_win(g, map, fog, res, COMBAT_MODE_CASTLE,
                                                      &tgt, rec, min_survivors);
                if (recruited) {
                    // Re-approach the gate to re-raise the siege flow at the new rng_state.
                    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                                    /*fight_through=*/true, rec))
                        return false;
                    flow = pending_flow;
                    if (flow != FLOW_SIEGE_MONSTER && flow != FLOW_SIEGE_VILLAIN)
                        return false;
                    monster = (flow == FLOW_SIEGE_MONSTER);
                    snprintf(castle_id, sizeof castle_id, "%s", pending_castle_id);
                }
            }
        }
    }

    if (!exec_fight(g, map, rec)) return false;        // unwinnable now / declined
    if (monster) return exec_garrison(g, castle_id, rec);
    return true;                                       // villain caught
}

// DIG (PRIM_DIG): the buried scepter at (x,y) — the WIN. Its tile is plain ground
// (walk-onto), so reach it fight-through and SEARCH. flow_apply_search is the one
// engine action with no helper among the 18 (FLOW_SEARCH is the 'S' action, not
// raised by stepping on); it records RA_SEARCH and sets g->stats.won on the scepter.
static bool exec_dig(const Primitive *p, Game *g, Map *map, Fog *fog,
                        const Resources *res, RecSink *rec) {
    if (g->stats.won) return true;                     // already recovered
    if (!exec_travel(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                     /*fight_through=*/true, rec))
        return false;                                  // tile unreachable now
    if (g->position.x != p->x || g->position.y != p->y)
        return false;                                  // not standing on the dig tile
    FlowAnswer yes; memset(&yes, 0, sizeof yes); yes.kind = FLOW_ANS_YES;
    flow_apply_search(g, g->res, yes, NULL, NULL, NULL);
    rec_push_action(rec, RA_SEARCH, NULL, 0, 0);
    return g->stats.won;
}

// WAIT: bank `count` weeks of commission (one exec_spend_week per week).
static bool exec_wait(const Primitive *p, Game *g, RecSink *rec) {
    int weeks = p->count > 0 ? p->count : 1;
    for (int i = 0; i < weeks; i++)
        if (!exec_spend_week(g, rec)) return false;
    return true;
}

// HOME: vestigial. No enumerated objective maps to it (the home pool is reached as a
// PRIM_ARMY recruit source, not a standalone objective), so the planner never emits
// it. Left unwired until a planstep produces it.
static bool exec_home(const Primitive *p, Game *g, Map *map, Fog *fog,
                      const Resources *res, RecSink *rec) {
    (void)p; (void)g; (void)map; (void)fog; (void)res; (void)rec;
    return false;
}

// Translate one objective (PlanStep) to the Primitive the executor dispatches on.
// Zone id, tile, and entity handle are copied from the PlanStep; the PrimKind is
// derived from the PlanKind. Only objective PlanKinds appear here — enabling steps
// that the prereq search generates (STEP_TAKE_CONTRACT, STEP_SIEGE_WEAPONS) are
// also objectives in their own right and share the same mapping.
static void planstep_to_prim(const PlanStep *s, const Resources *res,
                             Primitive *out) {
    memset(out, 0, sizeof *out);
    if (s->zone_index >= 0 && s->zone_index < res->zone_count) {
        const char *zid = res->zones[s->zone_index].id;
        size_t n = strlen(zid);
        if (n >= sizeof out->zone) n = sizeof out->zone - 1;
        memcpy(out->zone, zid, n);
    }
    out->x = s->target.x;
    out->y = s->target.y;
    snprintf(out->id, sizeof out->id, "%s", s->handle);

    switch (s->kind) {
    case STEP_CHEST:
    case STEP_ARTIFACT:
    case STEP_NAVMAP:
    case STEP_ORB:           out->kind = PRIM_FETCH; break;
    case STEP_ALCOVE:        out->kind = PRIM_LEARN; break;
    case STEP_SIEGE_WEAPONS: out->kind = PRIM_TOWN; out->town = TOWN_SIEGE; break;
    case STEP_TAKE_CONTRACT: out->kind = PRIM_TOWN; out->town = TOWN_CONTRACT; break;
    case STEP_MONSTER_CASTLE:
    case STEP_VILLAIN:       out->kind = PRIM_SIEGE; break;
    case STEP_FOE:
    case STEP_CLEAR_FOE:     out->kind = PRIM_SLAY;  break;
    case STEP_SCEPTER:       out->kind = PRIM_DIG;   break;
    case STEP_WAIT_WEEKS:    out->kind = PRIM_WAIT; out->count = s->target.x; break;
    case STEP_RECOMPOSE_ARMY: out->kind = PRIM_ARMY; break;
    case STEP_RENT_BOAT:     out->kind = PRIM_TOWN; out->town = TOWN_BOAT; break;
    case STEP_BUY_SPELLS:    out->kind = PRIM_TOWN; out->town = TOWN_SPELL; break;
    case STEP_RECRUIT_HOME:  out->kind = PRIM_HOME; out->home = HOME_RECRUIT; break;
    case STEP_RECRUIT_DWELLING: out->kind = PRIM_DWELL; break;
    case STEP_CAST_SPELL:    out->kind = PRIM_CAST; out->count = s->target.x; break;
    default:                 out->kind = PRIM_FETCH; break;
    }
}

// THE EXECUTOR. Translate the objective to its Primitive, dispatch to the
// orchestration function, then enforce the town-adjacency rule. CAST is degenerate
// (its whole body is the exec_cast helper), so it dispatches there directly.
bool execute(const PlanStep *s, const Resources *res, Game *g, Map *map,
             Fog *fog, RecSink *rec) {
    if (!s || !g || g->stats.game_over) return false;
    Primitive prim;
    planstep_to_prim(s, res, &prim);
    const Primitive *p = &prim;
    bool ok;
    switch (p->kind) {
    case PRIM_FETCH: ok = exec_fetch(p, g, map, fog, res, rec); break;
    case PRIM_TOWN:  ok = exec_town (p, g, map, fog, res, rec); break;
    case PRIM_DWELL: ok = exec_dwell(p, g, map, fog, res, rec); break;
    case PRIM_HOME:  ok = exec_home (p, g, map, fog, res, rec); break;
    case PRIM_SIEGE: ok = exec_siege(p, g, map, fog, res, rec); break;
    case PRIM_SLAY:  ok = exec_slay (p, g, map, fog, res, rec); break;
    case PRIM_DIG:   ok = exec_dig(p, g, map, fog, res, rec); break;
    case PRIM_LEARN: ok = exec_learn(p, g, map, fog, res, rec); break;
    case PRIM_ARMY:  ok = exec_army (p, g, map, fog, res, rec); break;
    case PRIM_CAST:  ok = exec_cast (g, p->count, rec); break;
    case PRIM_WAIT:  ok = exec_wait (p, g, rec); break;
    default:         ok = false; break;   // PRIM_KIND_COUNT / unknown
    }
    if (!ok) return false;

    // RULE 6 (docs/EXECUTOR-REFACTOR.md): every objective ends with the hero adjacent to
    // a HAVEN. If carrying it out did not already leave him at one, route back to the
    // nearest (fighting through blockers); if he cannot return, the objective FAILS so
    // the planner never commits a step that would strand the hero in a dead-end. After a
    // BATTLE (SLAY/SIEGE) the home (audience) castle counts as a haven too, so a won
    // fight can retreat to whichever of {castle, nearest town} is fewer ticks — ensuring
    // the hero is never trapped past a foe he just beat.
    if (g->position.in_town[0]) return true;
    bool is_battle = (p->kind == PRIM_SLAY || p->kind == PRIM_SIEGE);
    return exec_enter_town(g, map, fog, res, /*allow_castle=*/is_battle, NULL, NULL, rec);
}
