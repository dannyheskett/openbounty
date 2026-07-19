// autoplay/primitives.c
//
// The executor entry (execute_why, AP-080), one primitive per PrimKind
// (AP-081), the pending-decision responder, the gold-chest answer (AP-085),
// the co-located-foe re-provoke (AP-086), and the stranding pre-gate (AP-051).

#include "primitives.h"

#include <stdio.h>
#include <string.h>

#include "adventure.h"
#include "diag.h"
#include "exec_ledger.h"
#include "flows.h"
#include "pending.h"
#include "player_io.h"
#include "recording.h"
#include "tables.h"
#include "tile.h"

// ---- calendar day accounting helper (AP-172) ----------------------------------

// Game-typed convenience over ledger_book_move: books a movement-tagged delta
// (before_days = days_left before the move; cross_before =
// ledger_committed(CROSSING) before it) with the crossing days spent during the
// move factored out, so the movement tags partition instead of double-counting.
static void acct_move(DayAcctTag tag, int before_days, long cross_before,
                      const Game *g) {
    ledger_book_move(tag, before_days, g->stats.days_left, cross_before);
}

// ---- the gold-chest answer (AP-085) -------------------------------------------

// Leadership while control is still the scarce resource; gold when cash-poor
// or with nothing left to control. "Cash-poor" = the wallet cannot cover one
// week's outgoings (upkeep + boat), from the engine's own arithmetic.
static void answer_chest_choice(ExecCtx *ctx) {
    Game *g = ctx->g;
    bool any_uncaught = GameVillainsCaught(g) < villains_count();
    int outgoings = GameWeeklyOutgoings(g);
    bool take_leadership = any_uncaught && g->stats.gold >= outgoings;
    FlowAnswer ans = { take_leadership ? FLOW_ANS_2 : FLOW_ANS_1, 0 };
    rec_push_answer(g, FLOW_CHEST_CHOICE, ans, PLAYER_IO_COMBAT_NOT_RUN);
    PlayerIoPresentation pres;
    player_io_answer(g, ctx->map, ctx->fog, ctx->res, ans,
                     PLAYER_IO_COMBAT_NOT_RUN, &pres);
}

// ---- generic pending-decision responder ----------------------------------------

bool exec_answer_pending(ExecCtx *ctx, bool fight_ok) {
    Game *g = ctx->g;
    int guard = 8;
    while (pending_flow != FLOW_NONE && guard-- > 0) {
        PendingFlow flow = pending_flow;
        switch (flow) {
        case FLOW_CHEST_CHOICE:
            answer_chest_choice(ctx);
            break;
        case FLOW_ATTACK_FOE: {
            // Opportunistic: fight a colliding foe the live army beats; else
            // decline (the foe is an objective; a later attempt returns).
            bool fight = false;
            if (fight_ok && pending_foe_id[0]) {
                const FoeState *f = GameFindFoeConst(g, pending_foe_id);
                if (f) {
                    long ai_hp = 0;
                    fight = exec_fight_winnable(ctx, COMBAT_MODE_FOE,
                                                pending_foe_id, "Hostile band",
                                                f->garrison, 0, &ai_hp);
                }
            }
            CombatResult r;
            if (!exec_fight(ctx, fight, &r)) return false;
            break;
        }
        case FLOW_SIEGE_MONSTER:
        case FLOW_SIEGE_VILLAIN: {
            // Only exec_siege fights castles deliberately.
            CombatResult r;
            if (!exec_fight(ctx, false, &r)) return false;
            break;
        }
        default: {
            // Decline anything else raised in passing (friendly join, recruit
            // offer, alcove offer, discard, ...). Free friendly troops are
            // declined to keep army composition the recruiter's decision.
            FlowAnswer no = { FLOW_ANS_NO, 0 };
            rec_push_answer(g, flow, no, PLAYER_IO_COMBAT_NOT_RUN);
            PlayerIoPresentation pres;
            player_io_answer(g, ctx->map, ctx->fog, ctx->res, no,
                             PLAYER_IO_COMBAT_NOT_RUN, &pres);
            break;
        }
        }
        exec_pump_passive(ctx);
    }
    return pending_flow == FLOW_NONE;
}

// ---- stranding pre-gate (AP-051) -----------------------------------------------

bool exec_step_strands(ExecCtx *ctx, const PlanStep *step) {
    // Only arrivals that STAND on the tile can strand (consumables + the dig).
    switch (step->kind) {
    case STEP_CHEST:
    case STEP_ARTIFACT:
    case STEP_NAVMAP:
    case STEP_ORB:
    case STEP_SCEPTER:
        break;
    default:
        return false;
    }
    if (step->zone_index != hero_zone_index(ctx)) return false;  // judged on arrival
    int exits = 0;
    int mouth_x = -1, mouth_y = -1;
    bool water_exit = false;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            const Tile *t = MapGetTile(ctx->map, step->x + dx, step->y + dy);
            if (!t) continue;
            if (adventure_walkable_on_foot(t) &&
                (t->interactive == INTERACT_NONE ||
                 t->interactive == INTERACT_SIGN)) {
                exits++;
                mouth_x = step->x + dx;
                mouth_y = step->y + dy;
            } else if (t->terrain == TERRAIN_WATER) {
                water_exit = true;   // a boat escape stays possible
            }
        }
    }
    if (exits == 0 && !water_exit) {
        // Zero open exits: before declaring the tile sealed, a beatable foe
        // camping the only door counts as a door, not a wall -- the drive
        // fights through it, and the fight removes the camper (AP-094).
        // Only the zero-open-exit case takes this path; open-exit geometry
        // is judged by the rules above.
        for (int dy = -1; dy <= 1 && exits == 0; dy++) {
            for (int dx = -1; dx <= 1 && exits == 0; dx++) {
                if (!dx && !dy) continue;
                const Tile *t = MapGetTile(ctx->map, step->x + dx,
                                           step->y + dy);
                if (!t || t->interactive != INTERACT_FOE) continue;
                for (int fi = 0; fi < ctx->g->foe_count; fi++) {
                    const FoeState *f = &ctx->g->foes[fi];
                    if (!f->alive) continue;
                    if (strcmp(f->zone, ctx->g->position.zone) != 0) continue;
                    if (f->x != step->x + dx || f->y != step->y + dy) continue;
                    if (f->friendly) {
                        // A FRIENDLY camper is a door unconditionally: the
                        // join flow resolves on contact, no fight needed.
                        exits = 1;
                        mouth_x = step->x + dx;
                        mouth_y = step->y + dy;
                        break;
                    }
                    long fhp = 0;
                    if (exec_fight_winnable(ctx, COMBAT_MODE_FOE,
                                            f->placement_id, "Hostile band",
                                            f->garrison, 0, &fhp)) {
                        exits = 1;
                        mouth_x = step->x + dx;
                        mouth_y = step->y + dy;
                    }
                    break;
                }
            }
        }
        if (exits == 0) {
            if (ob_diag_verbose()) {
                printf("[STRAND] pre-gate skip %s (%d,%d): sealed end tile\n",
                       step->label, step->x, step->y);
                for (int ndy = -1; ndy <= 1; ndy++)
                    for (int ndx = -1; ndx <= 1; ndx++) {
                        if (!ndx && !ndy) continue;
                        const Tile *nt = MapGetTile(ctx->map, step->x + ndx,
                                                    step->y + ndy);
                        if (!nt) continue;
                        printf("[STRAND]  n(%+d,%+d) art=%s terr=%d int=%d "
                               "foot=%d\n", ndx, ndy, nt->art,
                               (int)nt->terrain, (int)nt->interactive,
                               (int)adventure_walkable_on_foot(nt));
                    }
            }
            return true;
        }
    }
    // The JAW TRAP: a single-mouth pocket whose mouth a chasing hostile can
    // camp. Entering is safe only when every such foe is one the live army
    // beats (the drive fights through); an unbeatable foe within follow range
    // of the mouth would seal the hero in -- skip this cycle.
    if (exits == 1 && !water_exit) {
        for (int i = 0; i < ctx->g->foe_count; i++) {
            const FoeState *f = &ctx->g->foes[i];
            if (!f->alive || f->friendly) continue;
            if (strcmp(f->zone, ctx->g->position.zone) != 0) continue;
            int ddx = f->x - mouth_x, ddy = f->y - mouth_y;
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            int d = ddx > ddy ? ddx : ddy;
            if (d > AUTOPLAY_MOUTH_CAMP_RADIUS) continue;
            long hp = 0;
            if (!exec_fight_winnable(ctx, COMBAT_MODE_FOE, f->placement_id,
                                     "Hostile band", f->garrison, 0, &hp)) {
                if (ob_diag_verbose())
                    printf("[STRAND] pre-gate skip %s (%d,%d): jaw trap, "
                           "%s camps the mouth (%d,%d)\n",
                           step->label, step->x, step->y, f->placement_id,
                           mouth_x, mouth_y);
                return true;
            }
        }
    }
    return false;
}

// ---- co-located-foe re-provoke (AP-086) ------------------------------------------

static bool engage_reached_foe(ExecCtx *ctx, const FoeState *foe,
                               ExecCause *out_cause) {
    Game *g = ctx->g;
    if (ob_diag_verbose())
        printf("[SLAY-CONTACT] hero=(%d,%d) mode=%d mount=%d foe=%s(%d,%d) "
               "flow=%d\n",
               g->position.x, g->position.y, (int)g->travel_mode,
               (int)g->character.mount, foe->placement_id, foe->x, foe->y,
               (int)pending_flow);
    if (pending_flow != FLOW_NONE) return true;   // a flow is live: proceed
    if (g->position.x != foe->x || g->position.y != foe->y) return true;

    // Step OFF to an adjacent legal non-interactive tile, then back ON, so the
    // return step fires INTERACT_FOE (or the step-off collides with the
    // follower). Legality by the hero's LIVE travel state.
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int nx = g->position.x + dx, ny = g->position.y + dy;
            const Tile *t = MapGetTile(ctx->map, nx, ny);
            if (!t || t->interactive != INTERACT_NONE) continue;
            bool ok = (g->travel_mode == TRAVEL_BOAT)
                          ? adventure_walkable_in_boat(t)
                          : adventure_walkable_on_foot(t);
            if (!ok) continue;
            if (!exec_recorded_step(ctx, dx, dy)) continue;
            if (pending_flow != FLOW_NONE) return true;   // follower collided
            // Step back onto the foe's LIVE tile.
            const FoeState *live = GameFindFoeConst(g, foe->placement_id);
            if (!live || !live->alive) return true;
            int bdx = live->x - g->position.x, bdy = live->y - g->position.y;
            if (bdx < -1 || bdx > 1 || bdy < -1 || bdy > 1) return true;
            exec_recorded_step(ctx, bdx, bdy);
            if (ob_diag_verbose())
                printf("[SLAY-CONTACT] re-provoke -> flow=%d\n",
                       (int)pending_flow);
            return true;
        }
    }
    if (out_cause) *out_cause = EXEC_CAUSE_REACH;   // slay:contact-blocked
    return false;
}

// ---- contract (AP-061) -----------------------------------------------------------

static bool exec_ensure_contract(ExecCtx *ctx, const char *villain_id,
                                 ExecCause *out_cause, char *why, int why_sz) {
    Game *g = ctx->g;
    const VillainDef *v = villain_by_id(villain_id);
    if (!v) return false;
    if (v->index >= 0 && v->index < CAT_VILLAINS_MAX &&
        g->contract.villains_caught[v->index])
        return true;
    if (g->contract.active_id[0] &&
        strcmp(g->contract.active_id, villain_id) == 0)
        return true;
    if (!GameVillainContractObtainable(g, villain_id)) {
        if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
        snprintf(why, (size_t)why_sz, "contract-window:%s", villain_id);
        return false;
    }
    // Take contracts at the nearest town until the cycle lands on the villain.
    NavPoint towns[GAME_TOWNS];
    int n = 0;
    for (int i = 0; i < ctx->res->town_count && n < GAME_TOWNS; i++) {
        const ResTown *t = &ctx->res->towns[i];
        int zi = zone_index_of(ctx->res, t->zone);
        if (zi < 0) continue;
        towns[n].zone_index = zi;
        towns[n].x = t->x;
        towns[n].y = t->y;
        n++;
    }
    ExecCause cc = EXEC_CAUSE_NONE;
    int before = g->stats.days_left;
    long cross_before = ledger_committed(DAY_ACCT_CROSSING);
    if (move_to(ctx, towns, n, true, NULL, &cc) < 0) {
        if (out_cause) *out_cause = cc;
        snprintf(why, (size_t)why_sz, "contract:town-unreachable");
        return false;
    }
    acct_move(DAY_ACCT_CONTRACT, before, cross_before, g);
    int cyc = ctx->res->contract.cycle_length;
    if (cyc < 1) cyc = 1;
    for (int i = 0; i <= cyc; i++) {
        if (g->contract.active_id[0] &&
            strcmp(g->contract.active_id, villain_id) == 0)
            return true;
        if (!g->position.in_town[0]) break;
        // An emptied cycle slot (endgame: catches outnumber refills) returns
        // NULL but still advances the rotation -- take again, the target may
        // sit in the NEXT slot. The loop bound already covers a full cycle.
        // The NULL take is a real mutation (the rotation advanced), so it is
        // recorded too (empty id); skipping it left the replay's rotation one
        // slot behind and the next recorded take landed on the wrong villain.
        uint32_t pre_fp = rec_world_fp(g);
        const char *got = GameTakeNextContract(g);
        rec_push_action_fp(pre_fp, RA_TAKE_CONTRACT, got ? got : "", 0, 0);
    }
    if (g->contract.active_id[0] &&
        strcmp(g->contract.active_id, villain_id) == 0)
        return true;
    if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
    snprintf(why, (size_t)why_sz, "contract-window:%s", villain_id);
    return false;
}

// ---- primitives -------------------------------------------------------------------

// Field an all-flying army for a flight-only pocket (a grass hollow inside
// a blocking ring has no foot entry -- the mover's fly legs are the door).
// Dismiss the non-fliers (recorded, a human does the same); when no flier
// is held, recruit one from an in-zone dwelling first.
static bool fetch_field_fliers(ExecCtx *ctx) {
    Game *g = ctx->g;
    bool have_flier = false;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        if (GameTroopFlies(troop_by_id(g->army[s].id))) have_flier = true;
    }
    if (!have_flier) {
        int hz = hero_zone_index(ctx);
        for (int i = 0; i < g->dwelling_count; i++) {
            const DwellingState *d = &g->dwellings[i];
            if (!d->troop_id[0] || d->count <= 0) continue;
            const TroopDef *t = troop_by_id(d->troop_id);
            if (!t || !GameTroopFlies(t)) continue;
            if (zone_index_of(ctx->res, d->zone) != hz) continue;
            if (g->stats.gold < t->recruit_cost) continue;
            NavPoint dp = { hz, d->x, d->y };
            ExecCause cc2;
            int rm_before = g->stats.days_left;
            long rm_cross = ledger_committed(DAY_ACCT_CROSSING);
            int rm_r = move_to(ctx, &dp, 1, true, NULL, &cc2);
            acct_move(DAY_ACCT_RECRUITMOVE, rm_before, rm_cross, g);
            if (rm_r < 0) continue;
            if (pending_flow == FLOW_RECRUIT) {
                FlowAnswer one = { FLOW_ANS_YES, 1 };
                rec_push_answer(g, FLOW_RECRUIT, one,
                                PLAYER_IO_COMBAT_NOT_RUN);
                PlayerIoPresentation pres;
                player_io_answer(g, ctx->map, ctx->fog, ctx->res, one,
                                 PLAYER_IO_COMBAT_NOT_RUN, &pres);
                exec_pump_passive(ctx);
            } else {
                exec_answer_pending(ctx, true);
            }
            for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                if (g->army[s].id[0] && g->army[s].count > 0 &&
                    GameTroopFlies(troop_by_id(g->army[s].id)))
                    have_flier = true;
            if (have_flier) break;
        }
    }
    if (!have_flier) return false;
    for (int s = GAME_ARMY_SLOTS - 1; s >= 0; s--) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        if (GameTroopFlies(troop_by_id(g->army[s].id))) continue;
        exec_dismiss_slot(ctx, s);
    }
    GameCompactArmy(g);
    return GamePlayerCanFly(g) && GameArmyStackCount(g) > 0;
}

static bool exec_fetch(ExecCtx *ctx, const PlanStep *step, ExecCause *out_cause) {
    NavPoint t = { step->zone_index, step->x, step->y };
    bool tried_fliers = false;
    for (int round = 0; round < EXEC_MAX_ROUNDS; round++) {
        if (planstep_is_done(ctx->g, step)) return true;
        ExecCause cc = EXEC_CAUSE_NONE;
        int before = ctx->g->stats.days_left;
        long cross_before = ledger_committed(DAY_ACCT_CROSSING);
        int r = move_to(ctx, &t, 1, true, NULL, &cc);
        acct_move(DAY_ACCT_APPROACH, before, cross_before, ctx->g);
        if (!exec_answer_pending(ctx, true)) {
            if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
            return false;
        }
        if (planstep_is_done(ctx->g, step)) return true;
        if (r < 0) {
            // Foot-unreachable in the hero's own zone: the one remaining
            // door is the sky. Once per attempt, field an all-flying army
            // and retry the approach.
            if (!tried_fliers &&
                (cc == EXEC_CAUSE_REACH || cc == EXEC_CAUSE_NONE) &&
                step->zone_index == hero_zone_index(ctx)) {
                tried_fliers = true;
                if (fetch_field_fliers(ctx)) continue;
            }
            if (out_cause) *out_cause = cc != EXEC_CAUSE_NONE ? cc
                                                              : EXEC_CAUSE_REACH;
            return false;
        }
        // Parked ON the tile with nothing fired: a flight pass-over hovers
        // onto the goal without ENTERING it (interactives fire on foot entry;
        // landing on one is illegal, GameCanLandAt). Step aside, land, and
        // walk back in.
        if (ctx->g->position.x == step->x && ctx->g->position.y == step->y &&
            pending_flow == FLOW_NONE) {
            bool bounced = false;
            for (int dy = -1; dy <= 1 && !bounced; dy++) {
                for (int dx = -1; dx <= 1 && !bounced; dx++) {
                    if (!dx && !dy) continue;
                    if (!exec_recorded_step(ctx, dx, dy)) continue;
                    if (ctx->g->position.x == step->x &&
                        ctx->g->position.y == step->y)
                        continue;   // bumped back
                    if (exec_is_flying(ctx) && !exec_land_here(ctx)) {
                        // can't land here; try walking on from another tile
                        // next round anyway
                    }
                    if (!exec_answer_pending(ctx, true)) break;
                    if (!exec_is_flying(ctx))
                        exec_recorded_step(ctx, -dx, -dy);
                    bounced = true;
                }
            }
            if (ob_diag_verbose())
                printf("[NAV] hover bounce %s at %s pos=(%d,%d)\n",
                       bounced ? "done" : "failed", step->label,
                       ctx->g->position.x, ctx->g->position.y);
        }
    }
    watchdog_hit("exec-rounds");
    if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
    return false;
}

static bool exec_learn(ExecCtx *ctx, const PlanStep *step, ExecCause *out_cause,
                       char *why, int why_sz) {
    Game *g = ctx->g;
    int cost = ctx->res->economy.alcove_cost;
    if (g->stats.gold < cost) {
        // A broke arrival is a typed GOLD defer before stepping onto the tile
        // (AP-081); the funded wait covers the fee at fixpoint time.
        ExecCause gc = EXEC_CAUSE_NONE;
        if (!exec_ensure_gold(ctx, cost, &gc)) {
            if (out_cause) *out_cause = gc;
            snprintf(why, (size_t)why_sz, "learn:gold");
            return false;
        }
    }
    NavPoint t = { step->zone_index, step->x, step->y };
    for (int round = 0; round < EXEC_MAX_ROUNDS; round++) {
        if (planstep_is_done(g, step)) return true;
        ExecCause cc = EXEC_CAUSE_NONE;
        int before = g->stats.days_left;
        long cross_before = ledger_committed(DAY_ACCT_CROSSING);
        int r = move_to(ctx, &t, 1, true, NULL, &cc);
        acct_move(DAY_ACCT_APPROACH, before, cross_before, g);
        if (pending_flow == FLOW_ALCOVE) {
            FlowAnswer yes = { FLOW_ANS_YES, 0 };
            rec_push_answer(g, FLOW_ALCOVE, yes, PLAYER_IO_COMBAT_NOT_RUN);
            PlayerIoPresentation pres;
            player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes,
                             PLAYER_IO_COMBAT_NOT_RUN, &pres);
            exec_pump_passive(ctx);
        }
        if (!exec_answer_pending(ctx, true)) {
            if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
            return false;
        }
        if (planstep_is_done(g, step)) return true;
        if (r < 0) {
            if (out_cause) *out_cause = cc != EXEC_CAUSE_NONE ? cc
                                                              : EXEC_CAUSE_REACH;
            return false;
        }
    }
    watchdog_hit("exec-rounds");
    if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
    return false;
}

static bool exec_town_buy_siege(ExecCtx *ctx, ExecCause *out_cause,
                                char *why, int why_sz) {
    Game *g = ctx->g;
    if (g->stats.siege_weapons) return true;
    int cost = ctx->res->economy.siege_cost;
    if (g->stats.gold <= cost) {
        ExecCause gc = EXEC_CAUSE_NONE;
        if (!exec_ensure_gold(ctx, cost + 1, &gc)) {
            if (out_cause) *out_cause = gc;
            snprintf(why, (size_t)why_sz, "siege-weapons:gold");
            return false;
        }
    }
    NavPoint towns[GAME_TOWNS];
    int n = 0;
    for (int i = 0; i < ctx->res->town_count && n < GAME_TOWNS; i++) {
        const ResTown *t = &ctx->res->towns[i];
        int zi = zone_index_of(ctx->res, t->zone);
        if (zi < 0) continue;
        towns[n].zone_index = zi;
        towns[n].x = t->x;
        towns[n].y = t->y;
        n++;
    }
    ExecCause cc = EXEC_CAUSE_NONE;
    int before = g->stats.days_left;
    long cross_before = ledger_committed(DAY_ACCT_CROSSING);
    if (move_to(ctx, towns, n, true, NULL, &cc) < 0) {
        if (out_cause) *out_cause = cc;
        snprintf(why, (size_t)why_sz, "siege-weapons:town-unreachable");
        return false;
    }
    acct_move(DAY_ACCT_APPROACH, before, cross_before, g);
    exec_answer_pending(ctx, true);
    if (!g->position.in_town[0]) {
        if (out_cause) *out_cause = EXEC_CAUSE_REACH;
        return false;
    }
    int siege_mk = recsink_mark();
    rec_push_action(g, RA_BUY_SIEGE, NULL, 0, 0);
    if (GameBuySiege(g) != SIEGE_BUY_OK && !g->stats.siege_weapons) {
        recsink_rollback(siege_mk);
        if (out_cause) *out_cause = EXEC_CAUSE_GOLD;
        snprintf(why, (size_t)why_sz, "siege-weapons:gold");
        return false;
    }
    return true;
}

// Move to the fight, lift at the gate, verify live, step on, fight (AP-082).
static bool siege_or_slay(ExecCtx *ctx, const PlanStep *step,
                          ExecCause *out_cause, char *why, int why_sz) {
    Game *g = ctx->g;
    bool is_foe = (step->kind == STEP_FOE);
    CombatMode mode = is_foe ? COMBAT_MODE_FOE : COMBAT_MODE_CASTLE;

    // Resolve the live target garrison + identity.
    const Unit *garrison = NULL;
    const char *seed_key = NULL;
    char castle_id[32] = { 0 };
    if (is_foe) {
        const FoeState *f = plan_find_foe(g, step->handle, step->zone_index);
        if (!f || !f->alive) return true;   // already gone
        garrison = f->garrison;
        seed_key = step->handle;
    } else {
        // A villain step's handle is the villain id; find its castle.
        if (step->kind == STEP_VILLAIN) {
            for (int i = 0; i < GAME_CASTLES; i++) {
                if (g->castles[i].id[0] &&
                    g->castles[i].owner_kind == CASTLE_OWNER_VILLAIN &&
                    strcmp(g->castles[i].villain_id, step->handle) == 0) {
                    snprintf(castle_id, sizeof castle_id, "%s",
                             g->castles[i].id);
                    break;
                }
            }
            if (!castle_id[0]) {
                // The villain no longer holds a castle (prefought elsewhere):
                // his catch needs the contract + a re-fight at his rebuilt
                // castle; without a castle there is nothing to assault.
                if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
                snprintf(why, (size_t)why_sz, "villain:no-castle");
                return false;
            }
        } else {
            snprintf(castle_id, sizeof castle_id, "%s", step->handle);
        }
        const CastleRecord *cr = GameFindCastleConst(g, castle_id);
        if (!cr) return false;
        if (cr->owner_kind == CASTLE_OWNER_PLAYER &&
            step->kind == STEP_MONSTER_CASTLE)
            return true;
        garrison = cr->garrison;
        seed_key = castle_id;
    }

    // Hard gate: the villain's contract (AP-051 try-order; siege weapons are a
    // planner prerequisite).
    if (step->kind == STEP_VILLAIN) {
        if (!exec_ensure_contract(ctx, step->handle, out_cause, why, why_sz))
            return false;
    }

    // Simulate-first (AP-082): held army, then the raise ladder, then the
    // recruiter.
    RecruitRequest req = {
        RECRUIT_FOR_WIN, mode, seed_key, step->label, garrison, 0,
    };
    long ai_hp = 0;
    int lift_k = 0;
    if (!exec_fight_winnable(ctx, mode, seed_key, step->label, garrison, 0,
                             &ai_hp)) {
        int k = exec_raise_k_for_win(ctx, &req);
        if (k > 0) {
            if (!exec_raise_for_fight(ctx, &req, k)) k = -1;
            else lift_k = k;
        }
        if (k <= 0) {
            ExecCause rc = EXEC_CAUSE_NONE;
            if (!exec_recruit(ctx, &req, &rc)) {
                if (out_cause) *out_cause = rc;
                snprintf(why, (size_t)why_sz, "%s:%s", step->label,
                         exec_cause_name(rc));
                return false;
            }
            lift_k = exec_raise_k_for_win(ctx, &req);
            if (lift_k < 0) lift_k = 0;
            // The approach can RESET a standing lift entirely (a sail's week
            // boundary): the gate's true requirement is the ladder from BASE
            // leadership, not from the transient current -- else lift_k=0 and
            // no re-arm ever fires after the reset.
            if (g->stats.leadership_current > g->stats.leadership_base) {
                int amt = GameRaiseControlAmount(g);
                for (int k2 = lift_k; amt > 0 && k2 <= RAISE_K_MAX; k2++) {
                    if (predict_combat_cached(ctx, mode, seed_key,
                                              step->label, garrison, NULL,
                                              g->stats.leadership_base +
                                                  k2 * amt,
                                              NULL, 0, NULL)) {
                        lift_k = k2;
                        break;
                    }
                }
            }
        }
    }

    // A zero-day approach (AP-092): an off-zone siege whose approach SAILS
    // loses the pre-cast lift at the crossing's week boundary. With a castle
    // gate destination in the fight's zone, stock the transport FIRST (the
    // room-forced stocker -- a full endgame book refuses the plain buy), so
    // the whole approach rides day-free gates and the lift survives.
    if (!is_foe && hero_zone_index(ctx) != step->zone_index &&
        g->stats.knows_magic &&
        (g->stats.gold > AUTOPLAY_RICH_WALLET(ctx->res) ||
         exec_last_realize_reserve() > 0)) {
        for (int town3 = 0; town3 < 2; town3++) {
            int cg2 = gate_spell_index(town3 == 1);
            const SpellDef *gsd2 = cg2 >= 0 ? spell_by_index(cg2) : NULL;
            if (!gsd2 || g->stats.gold < 2 * gsd2->cost ||
                spell_charges(g, cg2) >= GATE_LAW_MIN_CHARGES)
                continue;
            GateDestination gd[GAME_GATE_DESTS_MAX];
            int gn = GameGateDestinations(g,
                                          town3 ? GATE_DEST_TOWN
                                                : GATE_DEST_CASTLE,
                                          gd, GAME_GATE_DESTS_MAX);
            bool in_zone = false;
            for (int gi4 = 0; gi4 < gn && !in_zone; gi4++)
                if (zone_index_of(ctx->res, gd[gi4].zone) ==
                    step->zone_index)
                    in_zone = true;
            if (in_zone)
                exec_stock_spell_charges_public(ctx, cg2,
                                                AUTOPLAY_GATE_RESTOCK_TARGET);
        }
    }

    // Approach the gate / the foe's live tile.
    bool re_recruited = false;
    for (int round = 0; round < EXEC_MAX_ROUNDS; round++) {
        if (planstep_is_done(g, step)) return true;
        NavPoint t;
        t.zone_index = step->zone_index;
        t.x = step->x;
        t.y = step->y;
        if (is_foe) {
            const FoeState *f = plan_find_foe(g, step->handle,
                                              step->zone_index);
            if (!f || !f->alive) return true;
            t.x = f->x;
            t.y = f->y;
            if (f->x == g->position.x && f->y == g->position.y &&
                pending_flow == FLOW_NONE) {
                if (!engage_reached_foe(ctx, f, out_cause)) {
                    snprintf(why, (size_t)why_sz, "slay:contact-blocked");
                    return false;
                }
            }
        }
        // In the fight's zone with the lift DOWN (the approach's crossings
        // reset it): re-arm HERE, where the seller trips ride zero-day gates
        // and time-stopped walks, so the cast lift survives to the gate. The
        // realize preserved the re-arm's gold by capping its tail stock at
        // book room.
        if (pending_flow == FLOW_NONE && lift_k > 0 &&
            hero_zone_index(ctx) == step->zone_index &&
            g->stats.leadership_current <
                g->stats.leadership_base +
                    lift_k * GameRaiseControlAmount(g)) {
            if (exec_rearm_raise_for(ctx, lift_k, 0) && ob_diag_verbose())
                printf("[RECRUIT] in-zone re-arm: lift=%d lead=%d gold=%d\n",
                       lift_k, g->stats.leadership_current, g->stats.gold);
        }
        ExecCause cc = EXEC_CAUSE_NONE;
        int before = g->stats.days_left;
        long cross_before = ledger_committed(DAY_ACCT_CROSSING);
        int r = -1;
        if (pending_flow == FLOW_NONE)
            r = move_to(ctx, &t, 1, true, NULL, &cc);
        acct_move(DAY_ACCT_APPROACH, before, cross_before, g);

        PendingFlow flow = pending_flow;
        bool ours = (is_foe && flow == FLOW_ATTACK_FOE &&
                     strcmp(pending_foe_id, step->handle) == 0) ||
                    (!is_foe &&
                     (flow == FLOW_SIEGE_MONSTER ||
                      flow == FLOW_SIEGE_VILLAIN) &&
                     strcmp(pending_castle_id, castle_id) == 0);
        if (ours) {
            // At-gate lift (AP-132): the raise stock was armed above. A
            // re-arm with the siege prompt PENDING must never travel (the
            // trip would answer the flow out from under the fight) -- cast
            // only what is HELD; the live verify below judges the result.
            if (lift_k > 0) {
                int ri = spell_index_by_adventure_effect(
                    ADV_EFFECT_RAISE_CONTROL);
                int held = ri >= 0 ? g->spells.counts[ri] : 0;
                int cast_n = lift_k < held ? lift_k : held;
                for (int c2 = 0; c2 < cast_n; c2++)
                    if (!exec_cast_raise(ctx)) break;
            }
            long hp2 = 0;
            if (!exec_fight_winnable(ctx, mode, seed_key, step->label,
                                     garrison, 0, &hp2)) {
                if (ob_diag_verbose()) {
                    printf("[RECRUIT] live-verify-lost %s: lead=%d army=",
                           step->label, g->stats.leadership_current);
                    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                        if (g->army[s].id[0])
                            printf("%s:%d ", g->army[s].id, g->army[s].count);
                    printf("vs ");
                    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                        if (garrison[s].id[0])
                            printf("%s:%d ", garrison[s].id,
                                   garrison[s].count);
                    printf("ai_hp=%ld\n", hp2);
                }
                CombatResult rr;
                exec_fight(ctx, false, &rr);   // decline: not a verdict
                // Second chance: the plan's decisive kit charges can be
                // evicted en route by later stocking (the sim approved a
                // book the gate no longer holds). Re-run the recruit once --
                // the held army is quote credit, so the re-search prices
                // little more than the kit top-up -- and re-approach for a
                // fresh verify.
                if (!re_recruited) {
                    re_recruited = true;
                    // Cheapest first: when the approach reset the lift (the
                    // arrival and the siege prompt land inside ONE move, so
                    // the pre-verify re-arm never got a turn), the decline
                    // above made travel legal again -- re-arm the ladder the
                    // CURRENT lead needs and re-approach.
                    int k3 = exec_raise_k_for_win(ctx, &req);
                    if (k3 > 0 && exec_rearm_raise_for(ctx, k3, 0)) {
                        if (ob_diag_verbose())
                            printf("[RECRUIT] verify-fail re-arm: lift=%d "
                                   "lead=%d gold=%d\n", k3,
                                   g->stats.leadership_current,
                                   g->stats.gold);
                        continue;
                    }
                    ExecCause rc2 = EXEC_CAUSE_NONE;
                    if (exec_recruit(ctx, &req, &rc2)) {
                        lift_k = exec_raise_k_for_win(ctx, &req);
                        if (lift_k < 0) lift_k = 0;
                        continue;
                    }
                }
                if (out_cause) *out_cause = EXEC_CAUSE_NO_WINNING_ARMY;
                snprintf(why, (size_t)why_sz, "%s:live-verify-lost",
                         step->label);
                return false;
            }
            CombatResult rr = COMBAT_RESULT_LOSS;
            if (!exec_fight(ctx, true, &rr)) return false;
            if (rr != COMBAT_RESULT_WIN) {
                if (out_cause) *out_cause = EXEC_CAUSE_NO_WINNING_ARMY;
                snprintf(why, (size_t)why_sz, "%s:lost", step->label);
                return false;
            }
            if (planstep_is_done(g, step)) return true;
            if (step->kind == STEP_VILLAIN) {
                // A win without the matching contract: the lord re-established
                // (PREFOUGHT) -- keep the world without admitting (AP-051).
                if (out_cause) *out_cause = EXEC_CAUSE_PREFOUGHT;
                snprintf(why, (size_t)why_sz, "%s:prefought", step->label);
                return false;
            }
            return planstep_is_done(g, step);
        }
        if (flow != FLOW_NONE) {
            if (!exec_answer_pending(ctx, true)) {
                if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
                return false;
            }
            continue;
        }
        if (r < 0) {
            if (out_cause) *out_cause = cc != EXEC_CAUSE_NONE ? cc
                                                              : EXEC_CAUSE_REACH;
            snprintf(why, (size_t)why_sz, "%s:%s", step->label,
                     exec_cause_name(cc));
            return false;
        }
    }
    watchdog_hit("exec-rounds");
    if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
    snprintf(why, (size_t)why_sz, "%s:rounds", step->label);
    return false;
}

static bool exec_dig(ExecCtx *ctx, const PlanStep *step, ExecCause *out_cause,
                     char *why, int why_sz) {
    Game *g = ctx->g;
    NavPoint t = { step->zone_index, step->x, step->y };
    bool tried_fliers = false;
    for (int round = 0; round < EXEC_MAX_ROUNDS; round++) {
        if (g->stats.won) return true;
        ExecCause cc = EXEC_CAUSE_NONE;
        int before = g->stats.days_left;
        long cross_before = ledger_committed(DAY_ACCT_CROSSING);
        int r = move_to(ctx, &t, 1, true, NULL, &cc);
        acct_move(DAY_ACCT_APPROACH, before, cross_before, g);
        if (!exec_answer_pending(ctx, true)) {
            if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
            return false;
        }
        // Foot-unreachable dig site in the hero's own zone (a gate landing
        // inside a sealed pocket): the sky is the remaining door -- the same
        // flight fallback the fetch primitive uses (AP-188).
        if (r < 0 && !tried_fliers &&
            (cc == EXEC_CAUSE_REACH || cc == EXEC_CAUSE_NONE) &&
            step->zone_index == hero_zone_index(ctx)) {
            tried_fliers = true;
            if (fetch_field_fliers(ctx)) continue;
        }
        if (g->position.x == step->x && g->position.y == step->y &&
            hero_zone_index(ctx) == step->zone_index) {
            pending_flow = FLOW_SEARCH;
            player_io_raise_decision(g, FLOW_SEARCH, REQ_PROMPT_YES_NO,
                                     NULL, NULL);
            rec_push_action(g, RA_SEARCH, NULL, 0, 0);
            FlowAnswer yes = { FLOW_ANS_YES, 0 };
            PlayerIoPresentation pres;
            player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes,
                             PLAYER_IO_COMBAT_NOT_RUN, &pres);
            exec_pump_passive(ctx);
            if (pres.won_game) {
                show_win_game(g, ctx->res);   // the engine's own win flow
                exec_pump_passive(ctx);
                return true;
            }
            if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
            snprintf(why, (size_t)why_sz, "dig:missed");   // impossible: exact tile
            return false;
        }
        if (r < 0) {
            if (out_cause) *out_cause = cc != EXEC_CAUSE_NONE ? cc
                                                              : EXEC_CAUSE_REACH;
            snprintf(why, (size_t)why_sz, "dig:%s", exec_cause_name(cc));
            return false;
        }
    }
    watchdog_hit("exec-rounds");
    if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
    return false;
}

bool execute_why(ExecCtx *ctx, const PlanStep *step,
                 char *why, int why_sz, ExecCause *out_cause) {
    if (out_cause) *out_cause = EXEC_CAUSE_NONE;
    if (why && why_sz > 0) why[0] = '\0';
    if (ctx->g->stats.game_over) {
        if (out_cause) *out_cause = EXEC_CAUSE_TIME;
        snprintf(why, (size_t)why_sz, "time");
        return false;
    }
    char scratch[96];
    scratch[0] = '\0';
    bool ok = false;
    ExecCause cause = EXEC_CAUSE_NONE;
    switch (step->kind) {
    case STEP_CHEST:
    case STEP_ARTIFACT:
    case STEP_NAVMAP:
    case STEP_ORB:
        ok = exec_fetch(ctx, step, &cause);
        break;
    case STEP_ALCOVE:
        ok = exec_learn(ctx, step, &cause, scratch, sizeof scratch);
        break;
    case STEP_SIEGE_WEAPONS:
        ok = exec_town_buy_siege(ctx, &cause, scratch, sizeof scratch);
        break;
    case STEP_MONSTER_CASTLE:
    case STEP_VILLAIN:
    case STEP_FOE:
        ok = siege_or_slay(ctx, step, &cause, scratch, sizeof scratch);
        break;
    case STEP_SCEPTER:
        ok = exec_dig(ctx, step, &cause, scratch, sizeof scratch);
        break;
    }
    if (ctx->g->stats.game_over && !ctx->g->stats.won) {
        cause = EXEC_CAUSE_TIME;
        ok = false;
    }
    if (!ok && why && !why[0]) {
        if (scratch[0])
            snprintf(why, (size_t)why_sz, "%s", scratch);
        else
            snprintf(why, (size_t)why_sz, "%s:%s", step->label,
                     exec_cause_name(cause));
    }
    if (out_cause) *out_cause = cause;
    return ok;
}
