// autoplay/primitives.c
//
// THE EXECUTOR: execute() runs ONE high-level primitive on a Game, problem-solving
// it into engine actions, recording the replayable engine prims it emits, and
// reporting success/failure. The backtracking planner calls it per step (see
// primitives.h for the search model).
//
// STATUS: every TARGET primitive that an
// objective enumerates is wired end-to-end on nav(): FETCH (walk-onto consume),
// LEARN (alcove), TOWN (siege weapons, contract), SLAY (foe fight), SIEGE
// (monster-castle / villain assault + garrison), DIG (the scepter finale). Each does
// exactly what its primitive asks or returns false — a do-or-fail oracle the planner
// reads as "can't carry this out from here now" and backtracks past. Army recruitment
// is NOT a standalone objective; it happens reactively inside SLAY/SIEGE through the
// exec_recruit() seam, which reaches off-zone sources (home-pool gate / other-zone
// dwellings) by sailing.
//
// NOTE: TOWN_SIEGE and TOWN_CONTRACT are emitted by the planner — TOWN_SIEGE as the
// siege-weapons objective, TOWN_CONTRACT as a villain siege's prerequisite (the
// planner sequences it before the SIEGE).

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
#include "nav.h"         // nav_default_options — foot-reachability vocabulary
#include "step.h"        // GameStep — the attack step onto a foe
#include "pending.h"     // pending_flow / pending_foe_id — the flow GameStep raised
#include "combat.h"      // combat_run_headless_* / CombatTarget / CombatResult
#include "combat_policy.h" // autoplay_combat_policy
#include "player_io.h"   // player_io_answer — apply the combat answer
#include "recruit.h"     // ArmyTarget / RecruitSource enumerate — the ARMY contract
#include "resources.h"   // ResCastle (locate the audience/home castle gate)
#include "flow_resolve.h" // flow_apply_dismiss_army — dismiss a substituted-out stack
#include "map.h"         // MapGetTile — locate the home castle's real gate tile
#include "tile.h"        // INTERACT_CASTLE_GATE
#include "adventure.h"   // adventure_walkable_on_foot — pick a foot-standable approach
#include "tables.h"      // troop_by_id — weakest-stack power for the garrison choice
// (flow_resolve.h provides flow_apply_search — already included above.)

// (CASTLE_GARRISON_MIN_SURVIVORS now lives in exec.h, next to exec_fight which
// applies the rule.)

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
        rp.rec_combat_index = -1;
        recbuf_push(rec->prims, rp);
    }
    return true;
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

// (Recruiting — the search, dismiss, per-source buy, and the home-pool gate path —
// lives ONCE behind the exec_recruit() seam (exec_recruit.c); SLAY/SIEGE invoke it.)

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
    RecruitRequest add = { .mode = RECRUIT_ADD_FOR_WIN,       .combat_mode = COMBAT_MODE_FOE, .tgt = &tgt, .min_survivors = 1 };
    RecruitRequest rcp = { .mode = RECRUIT_RECOMPOSE_FOR_WIN, .combat_mode = COMBAT_MODE_FOE, .tgt = &tgt, .min_survivors = 1 };
    RecruitRequest bld = { .mode = RECRUIT_BUILD_FOR_WIN,     .combat_mode = COMBAT_MODE_FOE, .tgt = &tgt, .min_survivors = 1 };
    bool recruited =
        exec_recruit(g, map, fog, res, &add, rec)
     || exec_recruit(g, map, fog, res, &rcp, rec)
     || exec_recruit(g, map, fog, res, &bld, rec);
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
    default:
        return false;                                  // unknown service
    }
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
    RecruitRequest add = { .mode = RECRUIT_ADD_FOR_WIN,       .combat_mode = COMBAT_MODE_CASTLE, .tgt = &tgt, .min_survivors = min_surv };
    RecruitRequest rcp = { .mode = RECRUIT_RECOMPOSE_FOR_WIN, .combat_mode = COMBAT_MODE_CASTLE, .tgt = &tgt, .min_survivors = min_surv };
    RecruitRequest bld = { .mode = RECRUIT_BUILD_FOR_WIN,     .combat_mode = COMBAT_MODE_CASTLE, .tgt = &tgt, .min_survivors = min_surv };
    bool recruited =
        exec_recruit(g, map, fog, res, &add, rec)
     || exec_recruit(g, map, fog, res, &rcp, rec)
     || exec_recruit(g, map, fog, res, &bld, rec);
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
    case STEP_FOE:           out->kind = PRIM_SLAY;  break;
    case STEP_SCEPTER:       out->kind = PRIM_DIG;   break;
    default:                 out->kind = PRIM_FETCH; break;
    }
}

// THE EXECUTOR. Translate the objective to its Primitive, dispatch to the
// orchestration function, then enforce the town-adjacency rule.
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
    case PRIM_SIEGE: ok = exec_siege(p, g, map, fog, res, rec); break;
    case PRIM_SLAY:  ok = exec_slay (p, g, map, fog, res, rec); break;
    case PRIM_DIG:   ok = exec_dig(p, g, map, fog, res, rec); break;
    case PRIM_LEARN: ok = exec_learn(p, g, map, fog, res, rec); break;
    default:         ok = false; break;   // PRIM_KIND_COUNT / unknown
    }
    if (!ok) return false;

    // RULE 6: every objective ends with the hero adjacent to
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
