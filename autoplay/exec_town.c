// autoplay/exec_town.c
//
// Executor TOWN-service helpers (see exec.h):
//   exec_enter_town   — reach the nearest town and gate in        (HELPER #11) [P1]
//   exec_buy_siege    — buy siege weapons                         (HELPER #12) [here]
//   exec_take_contract— take a villain's contract                 (HELPER #13) [here]
//
// The transaction helpers below assume the hero is already gated into a town
// (exec_enter_town, built later, does the fight-through routing). Each issues one
// engine town-core call and records its replayable RA_* on success.

#include "exec.h"

#include <string.h>
#include <stdio.h>       // printf — the no-haven-reachable run-log diagnostic (stdout,
                         // the one unified autoplay/planner log stream)

#include "map.h"         // MapGetTile / Tile (town approach scan)
#include "adventure.h"   // adventure_walkable_on_foot

// 8-neighbour scan order for finding a town gate's foot-standable approach tile.
static const int TWN_NDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int TWN_NDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

// HELPER #11 — exec_enter_town. Reach the nearest HAVEN in the current zone and stop
// at it: a town (gate IN — sets position.in_town, yields the dock for a later rent) or,
// when `allow_castle` is set, the home (audience) castle's gate-landing tile if it is a
// faster refuge. A candidate is scored by a FOE-PASSABLE reachability check (so a haven
// reachable only by fighting THROUGH a blocker still counts) and reached with the ONE
// fight-through router; the foe-free tier is preferred, fewest ticks within a tier.
// Rule 6 passes allow_castle after a battle (so a won fight can retreat to the castle
// when no town is reachable, or one is closer); TOWN services pass false (they need a
// real town's dock). Returns false only if no haven in the zone has any path at all.
bool exec_enter_town(Game *g, Map *map, Fog *fog, const Resources *res,
                     bool allow_castle, int *out_dock_x, int *out_dock_y, RecSink *rec) {
    int zi = -1;
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, g->position.zone) == 0) { zi = i; break; }
    if (zi < 0) return false;
    const ResZone *z = &res->zones[zi];

    // Hero travel state for a boat-aware reachability query.
    bool boat_here = g->boat.has_boat && strcmp(g->boat.zone, g->position.zone) == 0;
    NavTravel tv = { (g->travel_mode == TRAVEL_BOAT) ? NAV_MODE_BOAT : NAV_MODE_FOOT,
                     boat_here, boat_here ? g->boat.x : -1, boat_here ? g->boat.y : -1 };
    NavPoint from = { g->position.x, g->position.y };
    NavOptions opt_nofight; nav_default_options(&opt_nofight);   // BLOCK_BOUNCERS: no fight
    NavOptions opt_fight;   nav_default_options(&opt_fight);
    opt_fight.interact_policy = NAV_INTERACT_FOE_PASSABLE;       // path THROUGH foes

    // Prefer the nearest town reachable WITHOUT fighting (a foe-free path); only if
    // none is, fall back to the nearest reachable by fighting THROUGH a blocking foe
    // (exec_reach clears those foes en route). This avoids picking a fight-through town
    // when a clean one is at hand, while still reaching a foe-pocketed town.
    int best_nf = -1, pick_nf = -1, best_ft = -1, pick_ft = -1;
    for (int t = 0; t < z->town_count; t++) {
        const ResTown *zt = resources_zone_town(res, z, t);
        if (!zt) continue;
        for (int k = 0; k < 8; k++) {                  // towns bounce; approach a neighbour
            int ax = zt->x + TWN_NDX[k], ay = zt->y + TWN_NDY[k];
            const Tile *tl = MapGetTile(map, ax, ay);
            if (!tl || !adventure_walkable_on_foot(tl)) continue;
            NavPoint ap = { ax, ay };
            int steps = 0;
            if (nav_reachable_travel(map, from, &tv, ap, &opt_nofight, &steps)) {
                if (best_nf < 0 || steps < best_nf) { best_nf = steps; pick_nf = t; }
            } else if (nav_reachable_travel(map, from, &tv, ap, &opt_fight, &steps)) {
                if (best_ft < 0 || steps < best_ft) { best_ft = steps; pick_ft = t; }
            }
            break;                                     // first walkable approach is enough
        }
    }

    // Optional home-castle haven. Its gate-landing tile (gate_x/gate_y, where the hero
    // lands FROM the gate) is plain walkable ground, so it is a direct exec_travel
    // target — no bouncer approach scan. Score it on the same two tiers as the towns.
    int castle_steps = -1; bool castle_nf = false; int cgx = -1, cgy = -1;
    if (allow_castle) {
        for (int i = 0; i < res->castle_count; i++) {
            const ResCastle *c = &res->castles[i];
            if (strcmp(c->special.flow, "audience") != 0) continue;
            if (strcmp(c->zone, g->position.zone) != 0) continue;
            if (c->gate_x < 0 || c->gate_y < 0) break;             // no landing tile
            NavPoint ap = { c->gate_x, c->gate_y };
            int steps = 0;
            if (nav_reachable_travel(map, from, &tv, ap, &opt_nofight, &steps)) {
                castle_steps = steps; castle_nf = true;
            } else if (nav_reachable_travel(map, from, &tv, ap, &opt_fight, &steps)) {
                castle_steps = steps; castle_nf = false;
            }
            cgx = c->gate_x; cgy = c->gate_y;
            break;
        }
    }

    int sx = g->position.x, sy = g->position.y, smode = g->travel_mode;  // start state
    bool sboat = g->boat.has_boat; int sbx = g->boat.x, sby = g->boat.y;

    // Pick town vs castle: best safety tier first (foe-free over fight-through), fewest
    // ticks within a tier. "whichever is fastest ticks-wise", with the castle in the mix.
    int  pick = (pick_nf >= 0) ? pick_nf : pick_ft;
    bool town_nf = (pick_nf >= 0);
    int  town_steps = town_nf ? best_nf : (pick_ft >= 0 ? best_ft : -1);
    bool go_castle = false;
    if (castle_steps >= 0) {
        if (pick < 0) go_castle = true;                            // no town at all
        else if (castle_nf != town_nf) go_castle = castle_nf;      // safer tier wins
        else go_castle = (castle_steps < town_steps);             // same tier: fewer ticks
    }

    if (go_castle) {
        // Walk to the castle's gate-landing tile (fight-through); arriving there leaves
        // the hero adjacent to the home castle — a valid retreat haven.
        if (exec_travel(g, map, fog, res, g->position.zone, cgx, cgy,
                        /*fight_through=*/true, rec) &&
            g->position.x == cgx && g->position.y == cgy)
            return true;
    } else if (pick >= 0) {
        const ResTown *zt = resources_zone_town(res, z, pick);
        // Route to the town gate fighting through blockers, step ONTO it (sets in_town).
        if (zt && exec_reach(g, map, fog, res, g->position.zone, zt->x, zt->y,
                             /*fight_through=*/true, rec) && g->position.in_town[0]) {
            if (out_dock_x) *out_dock_x = zt->boat_x;
            if (out_dock_y) *out_dock_y = zt->boat_y;
            return true;
        }
    }

    // No haven reachable from here. A VALID terminal state (e.g. the hero foe-pocketed
    // with no reachable town or castle) — autoplay simply can't proceed past the
    // achievable objectives — NOT a routing bug, so log and return false (the planner
    // reads false as a DEFER) rather than asserting like nav_fail. nearest{foe-free,
    // fight} are the nearest town indices (-1 = none); castle{steps,nf} the home-castle
    // candidate (steps -1 = none / not allowed) — dumped so a stalled run reproduces.
    printf("exec_enter_town: no haven reachable in %s from (%d,%d) mode=%s "
           "boat=%s@(%d,%d) -- towns=%d nearest{foe-free=%d fight=%d} castle{steps=%d nf=%d}\n",
           g->position.zone, sx, sy, smode == TRAVEL_BOAT ? "BOAT" : "FOOT",
           sboat ? "held" : "none", sbx, sby, z->town_count, pick_nf, pick_ft,
           castle_steps, allow_castle ? (int)castle_nf : -1);
    return false;
}

// HELPER #12 — exec_buy_siege. Buy siege weapons (the gate to assaulting any hostile
// castle). True if owned afterward (incl. already-owned, a no-op). Records
// RA_BUY_SIEGE on a fresh purchase. False if not in a town / no gold.
bool exec_buy_siege(Game *g, RecSink *rec) {
    if (g->stats.siege_weapons) return true;            // already owned: no re-buy
    SiegeBuyResult r = GameBuySiege(g);
    if (r != SIEGE_BUY_OK && r != SIEGE_BUY_ALREADY) return false;
    rec_push_action(rec, RA_BUY_SIEGE, NULL, 0, 0);
    return g->stats.siege_weapons != 0;
}

// HELPER #13 — exec_take_contract. Cycle GameTakeNextContract until `villain_id`'s
// contract is active (empty id = take any next contract), bounded to one full pass
// over the cycle so a villain not yet in the cycle fails cleanly rather than
// looping. Records RA_TAKE_CONTRACT per take. True once the wanted contract is active.
bool exec_take_contract(Game *g, const Resources *res, const char *villain_id,
                        RecSink *rec) {
    if (villain_id && villain_id[0] && strcmp(g->contract.active_id, villain_id) == 0)
        return true;                                    // already active
    int cyc = res ? res->contract.cycle_length : 5;
    if (cyc < 1) cyc = 1;
    for (int step = 0; step < cyc; step++) {
        rec_push_action(rec, RA_TAKE_CONTRACT, NULL, 0, 0);
        const char *got = GameTakeNextContract(g);
        if (!villain_id || !villain_id[0]) return got != NULL;   // any: first take wins
        if (got && strcmp(g->contract.active_id, villain_id) == 0) return true;
    }
    return false;   // wanted villain not in the cycle yet (capture an earlier one)
}

