// autoplay/autoplay.c
//
// Top-level autoplay loop. The boot sequence mirrors
// tests/library/consumer.c, the verified engine-pure consumer pattern. No
// raylib, no shell — this file compiles against engine headers only and links
// libobengine.a.
//
// Phase 2 drives the first objective type end to end: enumerate chest
// goals omnisciently, walk to each via the driver, let GameStep open it on
// arrival, answer the gold-vs-leadership prompt deterministically, and tick the
// checklist. Combat objectives (castles/villains) and the remaining non-combat
// kinds land in later phases behind the same goal model.

#include "autoplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "pack.h"
#include "resources.h"
#include "pending.h"
#include "flow_resolve.h"
#include "combat.h"
#include "tables.h"

#include "goals.h"
#include "combat_policy.h"
#include "recording.h"
#include "plan.h"     // committed-plan executor (two-phase run)
#include "planner.h"  // planner(): primitive backtracking over execute()/nav()

// Default pack when AutoplayConfig.pack_dir is NULL. Matches the shipped
// asset tree; the headless runner resolves it relative to cwd like the
// boundary-check consumer does.
#define AUTOPLAY_DEFAULT_PACK "assets/kings-bounty"

// Default manifest filename inside a pack (same constant the shell uses).
#define AUTOPLAY_MANIFEST "game.json"

const char *autoplay_verdict_str(AutoplayVerdict v) {
    switch (v) {
    case AUTOPLAY_VERDICT_CLEARED: return "CLEARED";
    case AUTOPLAY_VERDICT_FAILED:  return "FAILED";
    case AUTOPLAY_VERDICT_PARTIAL: return "PARTIAL";
    case AUTOPLAY_VERDICT_NOOP:    return "NOOP";
    default:                       return "?";
    }
}


void autoplay_apply_recorded_combat(Game *g, CombatTurnRecord *out_rec) {
    CombatMode mode; CombatTarget tgt = { 0 };
    if (pending_flow == FLOW_SIEGE_MONSTER || pending_flow == FLOW_SIEGE_VILLAIN) {
        mode = COMBAT_MODE_CASTLE;
        CastleRecord *cr = GameFindCastle(g, pending_castle_id);
        const ResCastle *rc = resources_castle_by_id(g->res, pending_castle_id);
        const VillainDef *v = (cr && cr->villain_id[0])
                                ? villain_by_id(cr->villain_id) : NULL;
        tgt.name = v && v->name[0] ? v->name
                 : (rc && rc->name[0] ? rc->name : pending_castle_id);
        tgt.seed_key = pending_castle_id;
        if (cr) { tgt.garrison = cr->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS; }
    } else if (pending_flow == FLOW_ATTACK_FOE) {
        mode = COMBAT_MODE_FOE;
        FoeState *foe = pending_foe_id[0] ? GameFindFoe(g, pending_foe_id) : NULL;
        tgt.name = "Hostile band";
        tgt.seed_key = pending_foe_id;
        if (foe) { tgt.garrison = foe->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS; }
    } else {
        return;   // not a combat flow — nothing to re-run
    }
    combat_run_headless_rec(g, mode, &tgt, 256, autoplay_combat_policy, NULL,
                            out_rec);
}

void autoplay_apply_rec_action(Game *g, Map *map, Fog *fog, RecActionKind kind,
                               const char *id, int x, int y) {
    if (!g) return;
    switch (kind) {
    case RA_WAIT_WEEK: {
        int paid = 0;
        GameSpendWeek(g, &paid);
        break;
    }
    case RA_TRAVEL_ZONE: {
        // Cross-zone travel: same engine core and same legality as the shell's
        // zone picker — sailing, target known (navmap collected), and a real
        // move (not the current zone). On any failed gate this is a no-op; the
        // step-boundary fingerprint check catches a diverged replay.
        if (!map || !fog || !id || !id[0]) break;
        if (g->travel_mode != TRAVEL_BOAT) break;
        if (strcmp(g->position.zone, id) == 0) break;
        int zi = -1;
        for (int i = 0; i < g->res->zone_count; i++)
            if (strcmp(g->res->zones[i].id, id) == 0) { zi = i; break; }
        if (zi < 0 || !g->world.zones_discovered[zi]) break;
        if (GameSwitchZone(g, map, fog, id)) {
            int paid = 0;
            GameSpendWeek(g, &paid);   // travel costs the rest of the week
        }
        break;
    }
    case RA_GARRISON_WEAKEST: {
        int weakest = -1; long worst = -1;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
            const TroopDef *td = troop_by_id(g->army[s].id);
            long power = (long)g->army[s].count * (td ? td->hit_points : 1);
            if (worst < 0 || power < worst) { worst = power; weakest = s; }
        }
        if (weakest >= 0 && id) GameGarrisonTroop(g, id, weakest);
        break;
    }
    case RA_RENT_BOAT:
        GameRentBoat(g, x, y, g->position.zone);
        break;
    case RA_TAKE_CONTRACT:
        GameTakeNextContract(g);
        break;
    case RA_BUY_SIEGE:
        GameBuySiege(g);
        break;
    case RA_RECRUIT_TYPE_N: {
        // Buy EXACTLY x of one troop type (the minimal garrisonable 2nd stack
        // before a monster-castle siege, WS-1 RC#1). The planner proved this exact
        // count buys-and-survives; replaying the same count reproduces it. Bounded
        // by GameBuyTroop's own gold/leadership checks (a no-op if state diverged).
        if (id && id[0] && x > 0) GameBuyTroop(g, id, x);
        break;
    }
    case RA_DISMISS_TYPE: {
        // Dismiss the army stack whose troop id == act_id, freeing a slot for an
        // upgrade. Routed through the engine dismiss core (flow_apply_dismiss_army):
        // whole-stack only, compacts, and refuses to empty the army — the only dismiss
        // a player has. No raw army write.
        if (id && id[0]) {
            int slot = -1;
            for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                if (g->army[s].id[0] && g->army[s].count > 0 &&
                    strcmp(g->army[s].id, id) == 0) { slot = s; break; }
            if (slot >= 0) {
                FlowAnswer ans; memset(&ans, 0, sizeof ans);
                ans.kind = (PromptAnswer)(FLOW_ANS_1 + slot);
                flow_apply_dismiss_army(g, ans, NULL);
            }
        }
        break;
    }
    case RA_SEARCH:
        // The scepter dig — same engine core as the shell's S action. On the
        // buried-scepter tile this sets g->stats.won (and the run terminates
        // on the win); on any other tile it spends search days and reveals
        // nothing. FlowAnswer YES = the player chose to search.
        if (g->res) {
            FlowAnswer yes; memset(&yes, 0, sizeof yes);
            yes.kind = FLOW_ANS_YES;
            flow_apply_search(g, g->res, yes, NULL, NULL, NULL);
        }
        break;
    }
}
static bool boot(const AutoplayConfig *cfg, Game **out_game, Map **out_map,
                 Fog **out_fog, Resources **out_res) {
    const char *pack_dir = (cfg->pack_dir && cfg->pack_dir[0])
                             ? cfg->pack_dir : AUTOPLAY_DEFAULT_PACK;
    Pack *pack = pack_open(pack_dir);
    if (!pack) {
        fprintf(stdout, "autoplay: pack_open(%s) failed\n", pack_dir);
        return false;
    }
    pack_stack_push(pack);

    Resources *res = calloc(1, sizeof *res);
    if (!res || !resources_load(res, AUTOPLAY_MANIFEST)) {
        fprintf(stdout, "autoplay: resources_load failed\n");
        free(res); pack_stack_pop();
        return false;
    }

    Game *game = calloc(1, sizeof *game);
    Map  *map  = calloc(1, sizeof *map);
    Fog  *fog  = calloc(1, sizeof *fog);
    if (!game || !map || !fog) {
        fprintf(stdout, "autoplay: alloc failed\n");
        free(fog); free(map); free(game); resources_free(res); free(res);
        pack_stack_pop();
        return false;
    }

    game->res  = res;
    game->seed = cfg->seed;   // BEFORE GameInit so the seed is honored.
    // FIXED TARGET: class 0, difficulty 1 (NORMAL) — a deliberate coupling
    // (calendar horizon / scaling / heuristics all assume NORMAL), not a tweak.
    GameInit(game, "Autoplay", /*pclass=*/0, /*difficulty=*/1, NULL);
    FogInit(fog);

    if (!MapLoadZoneWithPlacements(map, res, res->world.starting_zone, game)) {
        fprintf(stdout, "autoplay: MapLoadZoneWithPlacements(%s) failed\n",
                res->world.starting_zone);
        free(fog); free(map); free(game); resources_free(res); free(res);
        pack_stack_pop();
        return false;
    }

    printf("autoplay: booted seed=%llu pack=%s zone=%s hero=(%d,%d) gold=%d\n",
           (unsigned long long)cfg->seed, pack_dir, game->position.zone,
           game->position.x, game->position.y, game->stats.gold);

    *out_game = game; *out_map = map; *out_fog = fog; *out_res = res;
    return true;
}

// output_run(): dump a planner() run for MANUAL REVIEW (the DIE point — we stop
// after planning, before any live execution). Lists the admitted objectives and
// the recorded engine-prim count that realizes them.
static void output_run(const PrimRun *run, const Resources *res) {
    // objectives met (obj_done) can exceed objectives issued (plan.count): a chest
    // walked over en route to another target is consumed incidentally by GameStep.
    printf("autoplay: ===== RUN  verdict=%s  objectives_met=%d/%d  "
           "objectives=%d  recorded=%d =====\n",
           autoplay_verdict_str(run->verdict), run->obj_done, run->obj_total,
           run->plan.count, run->rec.count);
    int by_kind[STEP_KIND_COUNT] = { 0 };
    for (int i = 0; i < run->plan.count; i++) {
        const PlanStep *s = &run->plan.items[i];
        if (s->kind >= 0 && s->kind < STEP_KIND_COUNT) by_kind[s->kind]++;
        const char *zone_str = "(cur)";
        if (res && s->zone_index >= 0 && s->zone_index < res->zone_count)
            zone_str = res->zones[s->zone_index].id;
        printf("autoplay:   %3d. %-12s zone=%s (%d,%d) %s\n", i + 1,
               planstep_kind_str(s->kind), zone_str,
               s->target.x, s->target.y, s->handle);
    }
    printf("autoplay: ----- admitted by kind:");
    for (int k = 0; k < STEP_KIND_COUNT; k++)
        if (by_kind[k]) printf(" %s=%d", planstep_kind_str((PlanKind)k), by_kind[k]);
    printf("\n");
    // Recorded engine-prim mix (REC_MOVE adventure steps vs REC_ACTION direct calls).
    int n_move = 0, n_action = 0, n_answer = 0;
    for (int i = 0; i < run->rec.count; i++) {
        switch (run->rec.items[i].kind) {
        case REC_MOVE:   n_move++;   break;
        case REC_ACTION: n_action++; break;
        case REC_ANSWER: n_answer++; break;
        default: break;
        }
    }
    printf("autoplay: ----- recorded prims: move=%d action=%d answer=%d\n",
           n_move, n_action, n_answer);
}

bool autoplay(const AutoplayConfig *cfg, AutoplayResult *out) {
    if (!cfg || !out) return false;
    memset(out, 0, sizeof *out);
    out->seed = cfg->seed;
    out->verdict = AUTOPLAY_VERDICT_NOOP;

    Game *game; Map *map; Fog *fog; Resources *res;
    if (!boot(cfg, &game, &map, &fog, &res))
        return false;

    // NEW PATH: planner() orders the objectives and drives execute()/nav() over
    // them, leaving an admitted PrimPlan + a replayable recording. It mutates the
    // (throwaway) boot world forward; we DIE after for manual review (no live run).
    PrimRun *run = malloc(sizeof *run);
    if (!run || !planner(game, map, fog, res, cfg->zone_scope, run)) {
        fprintf(stdout, "autoplay: planner failed\n");
        if (run) primrun_free(run);
        free(run);
        free(fog); free(map); free(game);
        resources_free(res); free(res);
        pack_stack_pop();
        return false;
    }

    // DIE HERE (manual review): output the planned run and STOP — no execution.
    output_run(run, res);
    out->verdict = run->verdict;
    out->objectives_total = run->obj_total;
    out->objectives_done  = run->obj_done;

    // ---- Teardown ---------------------------------------------------------
    primrun_free(run);
    free(run);
    free(fog);
    free(map);
    free(game);
    resources_free(res);
    free(res);
    // Pop+close ONLY the pack we pushed (pack_stack_pop closes it), restoring
    // any pre-existing stack. We deliberately do NOT pack_stack_clear(): that
    // would close packs this call never opened — e.g. a long-lived pack the
    // host (or the test fixture) pushed earlier, leaving a dangling pointer.
    pack_stack_pop();
    return true;
}
