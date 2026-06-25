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
#include "worldsnap.h"   // worldsnap_capture/restore — roll back failed exec_build_for
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

// Try to recruit troops from one reachable source to flip a predicted LOSS to a WIN.
// Tries every reachable source ranked by survival ratio (best wins, not first-wins).
// Only recruits if the result is strictly better than the held army's own ratio.
// ADDS to an existing slot (or fills a free slot); never dismisses existing troops.
// Returns true if a recruit was performed (army now predicts a better fight).
static bool try_recruit_for_win(Game *g, Map *map, Fog *fog, const Resources *res,
                                CombatMode mode, const CombatTarget *tgt,
                                RecSink *rec, int min_survivors) {
    static const int SDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int SDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };
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

    long wr[64]; int wi[64], wn[64], nw = 0;
    int n_maxn_zero = 0, n_slot_full = 0, n_trial_loss = 0, n_unreach = 0;
    for (int i = 0; i < ns; i++) {
        if (!recruit_source_foot_ok(g, map, &srcs[i], SDX, SDY)) { n_unreach++; continue; }
        int maxn = GameMaxRecruitable(g, srcs[i].troop_id);
        if (maxn <= 0) { n_maxn_zero++; continue; }
        int try_n = (srcs[i].avail > 0 && srcs[i].avail < maxn) ? srcs[i].avail : maxn;
        int aff = recruit_affordable_count(g, srcs[i].troop_id);
        if (aff < try_n) try_n = aff;
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
        int b = -1;
        for (int k = 0; k < nw; k++) if (!used[k] && (b < 0 || wr[k] > wr[b])) b = k;
        if (b < 0) break;
        used[b] = true;
        if (!exec_recruit_one(g, map, fog, res, &srcs[wi[b]], wn[b], rec))
            continue;
        printf("try_recruit: OK vs '%s' — recruited %d %s (survival ratio -> %ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?", wn[b], srcs[wi[b]].troop_id, wr[b]);
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
    static const int SDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int SDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };
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
    long wr[64]; int wi[64], wn[64], nw = 0;
    for (int i = 0; i < ns; i++) {
        if (!recruit_source_foot_ok(g, map, &srcs[i], SDX, SDY)) continue;
        int maxn = GameMaxRecruitable(g, srcs[i].troop_id);
        if (maxn <= 0) continue;
        int try_n = (srcs[i].avail > 0 && srcs[i].avail < maxn) ? srcs[i].avail : maxn;
        int aff = recruit_affordable_count(g, srcs[i].troop_id);
        if (aff < try_n) try_n = aff;
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
        int b = -1;
        for (int k = 0; k < nw; k++) if (!used[k] && (b < 0 || wr[k] > wr[b])) b = k;
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
        if (!exec_recruit(g, map, fog, res, &target, rec)) continue;
        printf("try_recompose: OK vs '%s' — swapped weak slot for %s x%d "
               "(survival ratio -> %ld%%)\n",
               tgt->seed_key ? tgt->seed_key : "?", srcs[wi[b]].troop_id, wn[b], wr[b]);
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
    static const int SDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int SDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

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
            src_ok[i] = recruit_source_foot_ok(g, map, &srcs[i], SDX, SDY);
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
    // army can be built (PRIM_DWELL / PRIM_ARMY will eventually supply it).
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
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        bool in_trial = false;
        for (int t = 0; t < n_slots; t++)
            if (trial[t].id[0] && strcmp(trial[t].id, g->army[s].id) == 0)
                { in_trial = true; break; }
        if (in_trial) continue;
        if (exec_dismiss(g, s, rec)) s = -1;   // slots compact after dismiss; rescan
    }

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
    const Tile *here = MapGetTile(map, g->position.x, g->position.y);
    if (here && here->interactive != INTERACT_NONE) {
        for (int k = 0; k < 8; k++) {
            int nx = g->position.x + SDX[k], ny = g->position.y + SDY[k];
            const Tile *nt = MapGetTile(map, nx, ny);
            if (nt && nt->interactive == INTERACT_NONE && adventure_walkable_on_foot(nt)) {
                exec_step(g, map, fog, res, SDX[k], SDY[k], rec);
                // A walk-on chest on the step-off tile raises FLOW_CHEST_CHOICE.
                // Mirror the same handling exec_reach applies after exec_travel.
                if (pending_flow == FLOW_CHEST_CHOICE) {
                    FlowAnswer take_gold = { FLOW_ANS_1, 0 };
                    exec_answer(g, map, take_gold, rec);
                }
                break;
            }
        }
    }
    return true;
}

// SLAY: a hostile wandering foe at p->id.
// Navigate-first design: reach the foe, then check if the current army wins.
// If exec_fight declines (LOSS or Pyrrhic ratio), run the 3-tier pre-fight
// pipeline — try_recruit_for_win → try_recompose_for_win → exec_build_for —
// then re-approach and fight again. If nothing works, return false so the
// planner backtracks and defers this foe until the army is stronger.
static bool exec_slay(const Primitive *p, Game *g, Map *map, Fog *fog,
                      const Resources *res, RecSink *rec) {
    FoeState *foe = p->id[0] ? GameFindFoe(g, p->id) : NULL;
    if (!foe || !foe->alive) return true;

    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), foe->x, foe->y,
                    /*fight_through=*/true, rec))
        return false;
    foe = p->id[0] ? GameFindFoe(g, p->id) : NULL;
    if (!foe || !foe->alive) return true;   // cleared as fight-through side effect

    // Build combat target from foe now, before exec_fight might answer and mutate state.
    CombatTarget tgt; memset(&tgt, 0, sizeof tgt);
    tgt.name = "Hostile band"; tgt.seed_key = p->id;
    tgt.garrison = foe->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS;

    // Try the fight with the held army. On WIN returns true. On DECLINE, exec_fight
    // answers NO (clears pending_flow) and returns false — we can then navigate away.
    if (exec_fight(g, map, rec)) return true;

    // 3-tier pre-fight pipeline.
    bool recruited =
        try_recruit_for_win   (g, map, fog, res, COMBAT_MODE_FOE, &tgt, rec, /*min_surv=*/1)
     || try_recompose_for_win (g, map, fog, res, COMBAT_MODE_FOE, &tgt, rec, /*min_surv=*/1)
     || exec_build_for        (g, map, fog, res, COMBAT_MODE_FOE, &tgt, rec, /*min_surv=*/1);
    if (!recruited) return false;

    // Re-approach and fight with the improved army.
    foe = p->id[0] ? GameFindFoe(g, p->id) : NULL;
    if (!foe || !foe->alive) return true;   // cleared during recruit navigation
    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), foe->x, foe->y,
                    /*fight_through=*/true, rec))
        return false;
    foe = p->id[0] ? GameFindFoe(g, p->id) : NULL;
    if (!foe || !foe->alive) return true;
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

// SIEGE: assault the castle at (p->x, p->y).
// Navigate-first design: reach the gate first, then check combat prediction.
// If exec_fight declines (LOSS or insufficient survivors), run the 3-tier
// pre-fight pipeline — try_recruit_for_win → try_recompose_for_win →
// exec_build_for — then re-approach the gate and fight. find_siege_castle
// looks up the garrison for the pre-fight prediction (p->id is the castle id
// for MONSTER_CASTLE steps, the villain id for VILLAIN steps).
static bool exec_siege(const Primitive *p, Game *g, Map *map, Fog *fog,
                       const Resources *res, RecSink *rec) {
    // Navigate to the gate first.
    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                    /*fight_through=*/true, rec))
        return false;
    PendingFlow flow = pending_flow;
    if (flow != FLOW_SIEGE_MONSTER && flow != FLOW_SIEGE_VILLAIN)
        return false;                                  // gate bounced: no siege weapons
    bool monster = (flow == FLOW_SIEGE_MONSTER);
    char castle_id[24] = {0};
    snprintf(castle_id, sizeof castle_id, "%s", pending_castle_id);

    // Build CombatTarget from the castle's current garrison.
    int min_surv = monster ? CASTLE_GARRISON_MIN_SURVIVORS : 1;
    CombatTarget tgt; memset(&tgt, 0, sizeof tgt);
    tgt.name = monster ? "Castle garrison" : "Villain";
    tgt.seed_key = castle_id;
    const CastleRecord *cr = castle_id[0] ? GameFindCastleConst(g, castle_id) : NULL;
    if (cr) { tgt.garrison = (Unit *)cr->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS; }

    // Try the fight with the held army. On WIN, garrison (monster) or return.
    // On DECLINE, exec_fight answers NO (clears pending_flow) and returns false.
    if (exec_fight(g, map, rec)) {
        return monster ? exec_garrison(g, castle_id, rec) : true;
    }

    // 3-tier pre-fight pipeline.
    bool recruited =
        try_recruit_for_win   (g, map, fog, res, COMBAT_MODE_CASTLE, &tgt, rec, min_surv)
     || try_recompose_for_win (g, map, fog, res, COMBAT_MODE_CASTLE, &tgt, rec, min_surv)
     || exec_build_for        (g, map, fog, res, COMBAT_MODE_CASTLE, &tgt, rec, min_surv);
    if (!recruited) return false;

    // Re-approach the gate and try again.
    if (!exec_reach(g, map, fog, res, PRIM_DZ(p, g), p->x, p->y,
                    /*fight_through=*/true, rec))
        return false;
    flow = pending_flow;
    if (flow != FLOW_SIEGE_MONSTER && flow != FLOW_SIEGE_VILLAIN)
        return false;
    monster = (flow == FLOW_SIEGE_MONSTER);
    snprintf(castle_id, sizeof castle_id, "%s", pending_castle_id);

    if (!exec_fight(g, map, rec)) return false;
    return monster ? exec_garrison(g, castle_id, rec) : true;
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
