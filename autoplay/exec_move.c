// autoplay/exec_move.c
//
// Executor MOVEMENT helpers (see exec.h / docs/EXECUTOR-REFACTOR.md):
//   exec_step    — one recorded engine move                    (HELPER #3)  [here]
//   exec_path    — next A* step (folds nav.c's kernel)          (HELPER #4)  [P1]
//   exec_travel  — hero A->B by any means, fight-through        (HELPER #1)  [P1]
//   exec_reach   — travel up to a bouncer and step ONTO it      (HELPER #2)  [P1]
//
// These are the only place adventure movement touches the engine. They emit every
// step into the recording sink so headless == visible == planning, byte-for-byte.

#include "exec.h"

#include <string.h>      // strcmp / memset

#include "step.h"        // GameStep — the engine's one-tile walk primitive
#include "nav.h"         // A* kernel API for exec_path (the kernel is folded into this
                         // TU below; nav.h is now an executor API header)
#include "navigator.h"   // nav() — the single A->B mover exec_travel reuses (folded
                         // into this TU below)
#include "adventure.h"   // adventure_walkable_on_foot (approach-tile scan)
#include "map.h"         // MapGetTile / Tile
#include "pending.h"     // pending_flow / FLOW_CHEST_CHOICE — answered before exec_step

// 8-neighbour scan order (fixed, deterministic) for picking a bouncer's approach tile.
static const int EX_NDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int EX_NDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

// Forward declaration — nav_passable is defined in the nav internals section below.
static bool nav_passable(const Tile *t, const NavOptions *opts);

// HELPER #3 — exec_step. Record-then-apply: log the (dx,dy) the executor intends,
// then let GameStep carry it out (board/disembark/consume/bounce are all implicit
// in the move). The replay path re-applies the SAME recorded (dx,dy) on the
// byte-identical live world, so the effect recurs exactly. Returns true iff the
// hero's tile actually changed (GameStep's own result); a blocked or bounced step
// still records its REC_MOVE — fidelity does not depend on the outcome.
bool exec_step(Game *g, Map *map, Fog *fog, const Resources *res,
               int dx, int dy, RecSink *rec) {
    rec_push_move(rec, dx, dy);
    return GameStep(g, map, fog, res, dx, dy);
}

// HELPER #4 — exec_path. Next single A* step from `from` toward `to` in the hero's
// boat-aware travel state. Configures the standard executor options (bouncers
// blocked, diagonals on) with the goal tile optionally exempt, then drives the
// pathfinder. Pure and deterministic.
//
// FOLD NOTE: this delegates to the proven kernel (nav.c) for now — exactly the
// pattern navigator.c documents ("reused from the proven core for now; ported in
// verbatim at the nav.c deletion step"). At P6 the kernel's internals
// (path_passable / path_cost / path_heap_push / path_heap_pop / path_seen /
// path_search) move into this TU as private statics and this body calls path_search
// directly, with no change to the signature or behaviour.
NavStatus exec_path(const Map *map, NavPoint from, const NavTravel *travel,
                    NavPoint to, bool goal_is_bouncer, int *out_dx, int *out_dy) {
    NavOptions opts; nav_default_options(&opts);
    opts.goal_is_bouncer = goal_is_bouncer;
    return nav_next_step_travel(map, from, travel, to, &opts, out_dx, out_dy);
}

// HELPER #1 — exec_travel. Move the hero to (dz,tx,ty) by ANY means, REUSING the one
// mover (nav(): walk + board/rent boat + sail + cross-zone). Without fight_through a
// foe-blocked target simply defers (nav returns false). With fight_through, when the
// only route is blocked by a hostile foe, clear the nearest untried one (reach it +
// exec_fight) and retry — the single fight-through router (this replaces the old
// exec_nav). nav() asserts only on a genuinely geometric-unreachable target (a bug),
// never on a foe block. `tried[]` bounds the loop to the foe count.
//
// Two-sweep design: nav5_run (the A* core) uses global static tables and is not
// re-entrant, so exec_reach is always called with fight_through=false here (no
// nested exec_travel → nav chain). Instead, when the first sweep exhausts all
// directly-reachable foes without reaching the target, a second sweep is allowed:
// a foe cleared in the first sweep may have opened the approach to a foe whose
// exec_reach previously failed, so retrying with a fresh tried[] handles
// mutual-blocking clusters without recursion.
bool exec_travel(Game *g, Map *map, Fog *fog, const Resources *res,
                 const char *dz, int tx, int ty, bool fight_through, RecSink *rec) {
    if (!fight_through)
        return nav(g, map, fog, res, dz, tx, ty, rec);

    bool tried[GAME_MAX_FOES]; memset(tried, 0, sizeof tried);
    bool reset_used = false;
    for (int guard = 0; guard <= GAME_MAX_FOES * 2; guard++) {
        if (nav(g, map, fog, res, dz, tx, ty, rec)) return true;
        // Blocked: pick the nearest untried alive foe in the dest zone — hostile OR
        // friendly. A friendly (recruitable) army is an INTERACT_FOE tile too, so
        // nav bounces off it exactly like a hostile; when one walls off the only
        // route to the target it must be cleared the same way. Stepping onto a
        // friendly raises FLOW_ACCEPT_FRIENDLY, which — answered yes or no — ALWAYS
        // consumes the foe (flow_apply_accept_friendly), opening the tile: the
        // free-join analogue of winning a fight.
        int pick = -1, bestd = 0;
        for (int i = 0; i < g->foe_count && i < GAME_MAX_FOES; i++) {
            const FoeState *f = &g->foes[i];
            if (tried[i] || !f->alive) continue;
            if (strcmp(f->zone, dz) != 0) continue;
            int adx = f->x - g->position.x, ady = f->y - g->position.y;
            if (adx < 0) adx = -adx;
            if (ady < 0) ady = -ady;
            int d = adx > ady ? adx : ady;
            if (pick < 0 || d < bestd) { pick = i; bestd = d; }
        }
        if (pick < 0) {
            // First sweep exhausted: give one reset so foes cleared above may now
            // open approaches that were previously foe-blocked (mutual-blocking
            // cluster). A second exhaustion with no nav success → genuinely stuck.
            if (reset_used) return false;
            reset_used = true;
            memset(tried, 0, sizeof tried);
            continue;
        }
        tried[pick] = true;
        // Clear it: reach ONTO the foe (plain, so no fight-through recursion), which
        // raises the foe's flow, then resolve it. For a HOSTILE foe the flow is
        // FLOW_ATTACK_FOE: a won fight removes the overlay so the next nav() can
        // pass; a declined/lost one leaves it (and the foe is marked tried, so the
        // loop still terminates). For a FRIENDLY foe the step raises
        // FLOW_ACCEPT_FRIENDLY (the hero walks ONTO it, no bounce); answering YES
        // accepts the free join and consumes the foe, clearing the tile. (A no-slot
        // friendly auto-flees on contact and is consumed with no flow pending, so the
        // guard below simply skips the answer — the path is already open.) Only
        // resolve when exec_reach succeeded — if the foe couldn't be reached (another
        // foe blocked the approach), acting on a stale pending flow from an earlier
        // chase-collision would target the wrong foe.
        FoeState *foe = &g->foes[pick];
        if (foe->alive &&
            exec_reach(g, map, fog, res, foe->zone, foe->x, foe->y,
                       /*fight_through=*/false, rec)) {
            if (foe->friendly) {
                if (pending_flow == FLOW_ACCEPT_FRIENDLY) {
                    FlowAnswer join = { FLOW_ANS_YES, 0 };
                    exec_answer(g, map, join, rec);
                }
            } else {
                exec_fight(g, map, rec);
            }
        }
    }
    return false;
}

// HELPER #2 — exec_reach. Travel up to a BOUNCING interactive at (dz,tx,ty) — a foe,
// dwelling, alcove, castle/town gate — and step ONTO it, raising its flow (the
// primitive then answers/fights it). nav() cannot step onto a bouncer (the step
// bounces), so approach the nearest foot-standable NEIGHBOUR via exec_travel, then
// exec_step onto the tile. Cross-zone targets sail to dz FIRST (so the neighbour scan
// reads dz's map); an undiscovered destination defers rather than asserting. (This
// replaces the old exec_reach_and_enter; folds in exec_cross_to_zone.)
bool exec_reach(Game *g, Map *map, Fog *fog, const Resources *res,
                const char *dz, int tx, int ty, bool fight_through, RecSink *rec) {
    // Cross to dz first when off-zone, so the approach scan below reads dz's map
    // (nav reloads `map` in place on a cross-zone sail). Guard on discovery so an
    // undiscovered destination defers (nav would assert).
    if (dz && dz[0] && strcmp(g->position.zone, dz) != 0) {
        int zi = -1;
        for (int i = 0; i < res->zone_count; i++)
            if (strcmp(res->zones[i].id, dz) == 0) { zi = i; break; }
        if (zi < 0 || zi >= GAME_CONTINENTS || !g->world.zones_discovered[zi])
            return false;                    // can't cross to an undiscovered zone yet
        // The crossing is what matters; the walk-onto-bouncer leg fails (it bounces).
        exec_travel(g, map, fog, res, dz, tx, ty, /*fight_through=*/false, rec);
        if (strcmp(g->position.zone, dz) != 0) return false;   // crossing deferred
    }

    // Nearest nav-passable neighbour of the bouncer (Chebyshev to the hero).
    // nav_passable is stricter than adventure_walkable_on_foot: it excludes
    // telecaves, town gates, dwellings, alcoves, and foes — tiles that bounce the
    // hero rather than letting them stand there. adventure_walkable_on_foot returns
    // true for ANY interactive tile (including telecaves), which would cause the
    // hero to be teleported in a livelock loop instead of approaching the target.
    NavOptions ap_opts; nav_default_options(&ap_opts);
    if (fight_through) ap_opts.interact_policy = NAV_INTERACT_FOE_PASSABLE;
    int best = -1, ax = -1, ay = -1, bestd = 0;
    for (int k = 0; k < 8; k++) {
        int nx = tx + EX_NDX[k], ny = ty + EX_NDY[k];
        const Tile *t = MapGetTile(map, nx, ny);
        if (!t || !nav_passable(t, &ap_opts)) continue;
        // Reject a DEAD-END approach reachable ONLY by passing through the target
        // itself — a tile whose every passable neighbour IS the target (e.g. a chest
        // tile walled by mountains on all sides but the target foe). No human would
        // "approach" a foe by routing onto a tile they could only reach by stepping
        // through that very foe; the old strict-nearest pick often committed to such a
        // dead-end (it is frequently the Chebyshev-nearest passable neighbour) and the
        // hero could then never stand there. Requiring a non-target passable entrance
        // is a cheap, local test that drops only these geometric dead-ends.
        bool has_entrance = false;
        for (int j = 0; j < 8 && !has_entrance; j++) {
            int mx = nx + EX_NDX[j], my = ny + EX_NDY[j];
            if (mx == tx && my == ty) continue;            // target is not an entrance
            const Tile *mt = MapGetTile(map, mx, my);
            if (mt && nav_passable(mt, &ap_opts)) has_entrance = true;
        }
        if (!has_entrance) continue;                       // walled-in dead-end — skip
        int adx = nx - g->position.x, ady = ny - g->position.y;
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        int d = adx > ady ? adx : ady;
        if (best < 0 || d < bestd) { best = k; bestd = d; ax = nx; ay = ny; }
    }
    // Always pre-scan water/bridge neighbours. Coastal/island targets use them as
    // primary (no foot neighbours at all). Peninsula foes whose only land-side
    // neighbour is a dead-end accessible only via the foe tile also need this:
    // the foot approach fails because nav can't cross the foe to reach the
    // approach tile, and the boat route is the only viable path. Use a
    // terrain-only check so interactive tiles on water (e.g. a telecave on a
    // water cell) are excluded; exec_travel handles sailing and boat rental.
    int wbest = -1, wax = -1, way = -1, wbestd = 0;
    for (int k = 0; k < 8; k++) {
        int nx = tx + EX_NDX[k], ny = ty + EX_NDY[k];
        const Tile *t = MapGetTile(map, nx, ny);
        if (!t || !(t->terrain == TERRAIN_WATER || t->is_bridge)) continue;
        int adx = nx - g->position.x, ady = ny - g->position.y;
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        int d = adx > ady ? adx : ady;
        if (wbest < 0 || d < wbestd) { wbest = k; wbestd = d; wax = nx; way = ny; }
    }
    if (best < 0 && wbest < 0) return false;  // no approach tile at all
    if (best < 0) { ax = wax; ay = way; }     // no land: water is primary

    if (!exec_travel(g, map, fog, res, dz, ax, ay, fight_through, rec)) {
        // Land approach blocked (peninsula/dead-end) — fall back to boat approach.
        if (wbest < 0 || (ax == wax && ay == way)) return false;
        if (!exec_travel(g, map, fog, res, dz, wax, way, fight_through, rec))
            return false;
    }

    // If the approach tile had a walk-on consumable (chest), arriving on it raised
    // FLOW_CHEST_CHOICE. Answer it now: player_io_answer reads pending_flow (global)
    // to determine which flow to process, so the chest choice would otherwise be
    // left in the queue forever, and the hero collects neither gold nor leadership.
    if (pending_flow == FLOW_CHEST_CHOICE) {
        FlowAnswer take_gold = { FLOW_ANS_1, 0 };
        exec_answer(g, map, take_gold, rec);
    }
    int dx = tx - g->position.x, dy = ty - g->position.y;
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return false;   // not adjacent
    exec_step(g, map, fog, res, dx, dy, rec);   // bounce ONTO the tile -> raise its flow
    return true;
}

// ===================== folded from nav.c (P6: collapse sub-solver into the executor) =====================
//
// Deterministic A* over the in-memory Map. Pure: no Map/Game mutation. Foot
// travel for M1.
//
// Cost model (verified against engine/step.c + engine/game.c):
//   - A step's time cost is per-MOVE, not per-distance: GameOnStep charges one
//     of steps_left_today for a normal step and zeroes the day on desert.
//     Diagonal and cardinal moves cost the same. So edge cost depends only on
//     the DESTINATION terrain, and the admissible heuristic for 8-connectivity
//     is Chebyshev distance (max(|dx|,|dy|)) times the cheapest edge.
//   - Only GRASS (cost 1) and DESERT are walkable on foot; DESERT is modeled
//     at TerrainMoveCost's 40 so A* avoids it exactly as the game economy does.
//
// Passability matches the engine: adventure_walkable_on_foot() is the gate
// GameStep enforces, but GameStep BOUNCES off castle/town/dwelling/alcove/
// hostile-foe tiles, so under the default policy those are obstacles for
// pass-through (see nav.h).
//
// Algorithm: A* over the boat-aware travel state, with a binary min-heap open
// set (O(E log V), replacing an earlier O(V^2) linear scan that dominated
// runtime on boat searches). The heuristic is admissible Chebyshev*cheapest-edge
// to the goal cell, so paths stay shortest; point-to-point queries stop as soon
// as the goal is settled instead of exploring the whole reachable boat-state
// set. Ties resolve by (f, then per-search insertion seq), a fixed rule that
// makes the chosen path identical on every run for a given map.
// Distance-field queries (dist_out != NULL) pass no point goal, so the heuristic
// is 0 and the search degrades to the exact uniform-cost Dijkstra they require.
//
// Boat model (LAYERED GRAPH over (cell, mode), matching engine/step.c exactly):
// the held boat is ONE boat at a known rest cell (travel->boat_x/y). The search
// runs over state (hero_cell, mode) with two layers, FOOT and BOAT; board and
// disembark are transition EDGES. The transitions mirror engine/step.c:54-55:
//   - on FOOT, stepping onto the boat's REAL tile boards (-> BOAT), cost 1;
//   - in BOAT, a water/bridge step sails (stays BOAT), cost 1;
//   - in BOAT, a land step disembarks (-> FOOT) at that land tile's terrain cost.
// Boarding ONLY at the boat's real tile (not "any shore of the body") keeps the
// path step-for-step drivable and its distances equal to the engine drive, and
// keeps the state set cells x 2 so the exhaustive distance field stays cheap.
// A one-way shortest path boards the boat at most once at its real tile, so a
// single boarding point is reachability-exact for the query; multi-leg journeys
// still complete across the run because the engine repositions the boat on each
// real disembark and the caller re-queries the live boat position each turn.
// (Two earlier designs were rejected: tracking the boat's exact cell as an
// extra per-cell search dimension was correct but orders of magnitude slower;
// treating "has boat" as board-from-ANY-shore-of-the-body was fast but
// teleported the hero across water for cost 1, corrupting distances. The
// layered single-board model is both fast and correct.)

#include "nav.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "adventure.h"
#include "tile.h"

#define NAV_W MAP_MAX_W
#define NAV_H MAP_MAX_H
#define NAV_CELLS (NAV_W * NAV_H)
#define NAV_INF INT_MAX

// Cheapest possible edge cost (grass). Used to scale the admissible heuristic.
#define NAV_MIN_EDGE 1

// Per-destination-terrain edge cost. Returns 0 for non-walkable terrain (the
// caller never enters such a tile, so the value is only a guard).
static int nav_edge_cost(Terrain t) {
    int c = TerrainMoveCost(t);   // grass=1, desert=40, else 0
    return c;
}

// Is `t` enterable as a pass-through cell under `opts`? The goal cell is
// handled separately by the caller so a bounce tile can still be a target.
static bool nav_passable(const Tile *t, const NavOptions *opts) {
    if (!t) return false;
    // FLIGHT: every in-bounds tile is passable (engine/step.c flying branch);
    // interactives don't fire in the air, so nothing bounces either.
    if (opts->fly) return true;
    // Engine's foot gate first: rejects blocks_foot / non-walkable terrain.
    if (!adventure_walkable_on_foot(t)) return false;
    if (opts->interact_policy == NAV_INTERACT_ALL_WALKABLE) return true;
    // BLOCK_BOUNCERS: treat tiles GameStep would bounce off as obstacles.
    switch (t->interactive) {
    case INTERACT_CASTLE_GATE:
    case INTERACT_TOWN:
    case INTERACT_DWELLING_PLAINS:
    case INTERACT_DWELLING_FOREST:
    case INTERACT_DWELLING_HILLS:
    case INTERACT_DWELLING_DUNGEON:
    case INTERACT_ALCOVE:
        // Always bouncers for pass-through (a castle gate / town / dwelling /
        // alcove bounces the hero off — never walk THROUGH one). FOE_PASSABLE
        // does NOT make these passable; it only opens hostile foes, below.
        return false;
    case INTERACT_FOE:        // hostile foes bounce; friendly is resolved at
                              // contact. Normally an obstacle for pass-through.
                              // Under FOE_PASSABLE the planner deliberately
                              // routes through it: it steps onto the foe so the
                              // engine raises FLOW_ATTACK_FOE, and a won fight
                              // clears the overlay (see autoplay_step).
        return opts->interact_policy == NAV_INTERACT_FOE_PASSABLE;
    case INTERACT_TELECAVE:
        // A telecave is NOT a walk-onto consumable: stepping onto it TELEPORTS
        // the hero to its paired cave (engine/step.c, INTERACT_TELECAVE branch),
        // which derails any approach that merely routed ACROSS it (a route to
        // a nearby objective can walk the hero over a telecave and get flung
        // across the map, so the objective never admits). Telecaves
        // are never planner OBJECTIVES (goals.c does not enumerate them), so a
        // pass-through route never has a legitimate reason to step onto one —
        // treat it as a bouncer/obstacle, like the castle gate / town / dwelling
        // above. (FOE_PASSABLE does not open it; only hostile foes, above.)
        return false;
    default:
        // INTERACT_NONE and the walk-onto consumables (chest/artifact/orb/
        // navmap/sign) are passable.
        return true;
    }
}

void nav_default_options(NavOptions *opts) {
    if (!opts) return;
    opts->interact_policy = NAV_INTERACT_BLOCK_BOUNCERS;
    opts->allow_diagonal  = true;
    opts->goal_is_bouncer = false;
    opts->fly             = false;
}

static inline int nav_idx(int x, int y) { return y * NAV_W + x; }

static inline int nav_abs(int v) { return v < 0 ? -v : v; }


// Neighbor offsets in a FIXED order so tie-breaking is reproducible. Order is
// deliberate and documented: the linear min-scan already makes the result
// order-independent (it picks by cell index), but a fixed neighbor order keeps
// came_from deterministic when costs are exactly equal.
static const int NAV_DX[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };
static const int NAV_DY[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };


// ---------------------------------------------------------------------------
// Boat-aware search: a layered graph over (hero_cell, mode). ONE model, used by
// every nav entry point. Two layers (FOOT, BOAT); board/disembark are edges.
//
// Transitions mirror engine/step.c: on FOOT, stepping onto the boat's REAL tile
// (travel->boat_x/y) boards (-> BOAT); in BOAT, a water/bridge step sails on, a
// land step disembarks (-> FOOT). Boarding only at the boat's real tile keeps the
// path step-for-step drivable and its distances equal to the engine drive. State
// is cells x 2 (no boat-cell dimension), so the exhaustive distance field stays
// fast. A one-way path boards at most once at the boat's real tile; multi-leg
// journeys complete across the run because the engine repositions the boat on
// each real disembark and the caller re-queries the live boat position.
//
// Uniform-cost (Dijkstra) with an admissible heuristic for point goals (A*);
// per-move cost 1 for grass/water/bridge/board and TerrainMoveCost for desert.
// ---------------------------------------------------------------------------

// Per-move edge cost into tile `nt` given the mode AFTER the move.
static int nav_edge_cost_mode(const Tile *nt, NavMode after_mode) {
    if (!nt) return 0;
    if (after_mode == NAV_MODE_BOAT) {
        if (nt->terrain == TERRAIN_WATER || nt->is_bridge) return NAV_MIN_EDGE;
        return 0;   // BOAT only stays BOAT on water/bridge
    }
    // FOOT move (normal step OR a disembark onto land). The caller has already
    // vetted the tile as legal (nav_passable / walkable). TerrainMoveCost is 0
    // for non-grass/desert terrain (forest/mountain/water) — but such a tile can
    // still be a LEGAL foot move when it carries an interactive overlay the engine
    // walks onto (chest/artifact/orb/navmap on a forest/mountain tile) or is the
    // goal. Returning 0 here makes the search's `if (step <= 0) continue;` guard
    // silently DROP that legal disembark/walk-onto transition, severing the
    // boat->land step on those shores and falsely reporting the objective
    // unreachable. Floor a walkable foot move at NAV_MIN_EDGE so it is never
    // dropped; grass=1/desert=40 are preserved.
    int c = nav_edge_cost(nt->terrain);
    return c > 0 ? c : NAV_MIN_EDGE;
}

// State key is (hero_cell, mode): cell*2 + mode. The layered graph has two layers
// (FOOT, BOAT); board/disembark are transition edges. cells x 2 states.
static uint64_t nav_state_key(int hero_cell, int mode) {
    return ((uint64_t)hero_cell << 1) | (uint64_t)mode;
}

#define NAV5_CAP (1 << 20)   // 1M slots; far over the cells x 2 state set. (A
                             // point-goal A* settles only a path corridor; the
                             // exhaustive distance-field flood over one zone
                             // stays far under the cap (measured peak is far
                             // below it).
typedef struct {
    unsigned gen;     // search epoch this slot belongs to (0 = never used)
    uint64_t key;
    int      g;       // best gscore to this state
    int      h;       // heuristic to goal cell (0 for distance-field/exhaustive)
    int      parent;  // predecessor slot index, or -1
    int      px, py;  // this state's hero cell (for reconstruction)
    int      open;    // 1 if in the open set (a live, non-stale heap entry exists)
} Nav5Slot;
static Nav5Slot nav5_tab[NAV5_CAP];
static unsigned nav5_gen = 0;

static int nav5_find_or_add(uint64_t key, bool *created) {
    uint64_t h = (key * 1099511628211ULL) & (NAV5_CAP - 1);
    for (;;) {
        if (nav5_tab[h].gen != nav5_gen) {
            nav5_tab[h].gen = nav5_gen; nav5_tab[h].key = key;
            *created = true; return (int)h;
        }
        if (nav5_tab[h].key == key) { *created = false; return (int)h; }
        h = (h + 1) & (NAV5_CAP - 1);
    }
}

// ---- Binary min-heap open set (replaces the old O(n^2) linear scan) --------
//
// Entries are (f, seq, slot): f = g + h is the A* priority; seq is a per-search
// monotonic insertion counter that makes ties resolve in a FIXED order (lower f,
// then lower seq = inserted-earlier), so the chosen path is identical on every
// run for a given map and independent of heap-array layout.
//
// We never decrease-key in place: when a slot's g improves we push a FRESH entry
// and leave the old one to be discarded lazily on pop (an entry whose recorded f
// no longer matches the slot's current g+h, or whose slot is already closed, is
// stale and skipped). Heap size is bounded by total relaxations, well under CAP.
typedef struct { int f; int seq; int slot; } NavHeapEnt;
static NavHeapEnt nav_heap[NAV5_CAP];
static int        nav_heap_n = 0;
static int        nav_heap_seq = 0;

// Strict "a before b" ordering: lower f first, then lower seq (stable tie-break).
static inline bool nav_heap_less(const NavHeapEnt *a, const NavHeapEnt *b) {
    if (a->f != b->f) return a->f < b->f;
    return a->seq < b->seq;
}

static void nav_heap_push(int slot, int f) {
    NavHeapEnt e = { f, nav_heap_seq++, slot };
    int i = nav_heap_n++;
    nav_heap[i] = e;
    while (i > 0) {
        int parent = (i - 1) >> 1;
        if (!nav_heap_less(&nav_heap[i], &nav_heap[parent])) break;
        NavHeapEnt t = nav_heap[parent]; nav_heap[parent] = nav_heap[i];
        nav_heap[i] = t; i = parent;
    }
}

// Pop the min entry (caller checks staleness against the slot's current g/open).
static NavHeapEnt nav_heap_pop(void) {
    NavHeapEnt top = nav_heap[0];
    NavHeapEnt last = nav_heap[--nav_heap_n];
    if (nav_heap_n > 0) {
        int i = 0;
        nav_heap[0] = last;
        for (;;) {
            int l = 2 * i + 1, r = 2 * i + 2, m = i;
            if (l < nav_heap_n && nav_heap_less(&nav_heap[l], &nav_heap[m])) m = l;
            if (r < nav_heap_n && nav_heap_less(&nav_heap[r], &nav_heap[m])) m = r;
            if (m == i) break;
            NavHeapEnt t = nav_heap[m]; nav_heap[m] = nav_heap[i];
            nav_heap[i] = t; i = m;
        }
    }
    return top;
}

// Admissible heuristic: Chebyshev distance (8-connected) to the goal cell times
// the cheapest possible edge (grass=1). Never overestimates remaining cost
// (desert costs more, water/board cost >= 1), so A* stays optimal — step counts
// and path lengths are unchanged vs. uniform-cost Dijkstra. Depends only on the
// hero cell (boat position can't reduce remaining hero distance below this), so
// it is admissible across the full boat-aware state too. Zero when no point goal
// (distance-field mode), degrading A* to the exact Dijkstra those callers need.
static inline int nav_heuristic(int cell, int goal_cell, int neighbors) {
    if (goal_cell < 0) return 0;
    int cx = cell % NAV_W, cy = cell / NAV_W;
    int gx = goal_cell % NAV_W, gy = goal_cell / NAV_W;
    int ddx = cx > gx ? cx - gx : gx - cx;
    int ddy = cy > gy ? cy - gy : gy - cy;
    if (neighbors == 8) return (ddx > ddy ? ddx : ddy) * NAV_MIN_EDGE;
    return (ddx + ddy) * NAV_MIN_EDGE;   // 4-connected: Manhattan
}

// Core boat-aware search: a layered graph over (hero_cell, mode). On FOOT, the
// hero BOARDS only by stepping onto the boat's real tile (travel->boat_x/y) —
// matching engine/step.c:54-55; in BOAT, a water/bridge step sails (stays BOAT)
// and a land step DISEMBARKS to FOOT. board/disembark are transition edges, so
// state is cells x 2 (no boat-cell dimension): the exhaustive field stays cheap.
// A one-way shortest path boards the boat at most once at its real tile, so a
// single board point is exact; multi-leg journeys complete across the run because
// the engine repositions the boat on each real disembark and the planner
// re-queries the live position each turn.
//
// If `dist_out` is non-NULL it is filled as a per-cell min-cost field (cells
// unreached stay -1), the heuristic is disabled, and the search runs to
// exhaustion (uniform-cost Dijkstra); otherwise it runs as A* and stops at the
// goal cell, returning that slot, or -1. `goal_cell` is ignored when dist_out.
static int nav5_run(const Map *map, NavPoint from, const NavTravel *travel,
                    int goal_cell, const NavOptions *opts, int *dist_out) {
    const int neighbors = opts->allow_diagonal ? 8 : 4;
    const int goal = dist_out ? -1 : goal_cell;   // no point goal in field mode
    nav5_gen++;
    nav_heap_n = 0;
    nav_heap_seq = 0;
    bool has_boat = travel->has_boat;
    int bx = has_boat ? travel->boat_x : -1;   // the boat's real tile
    int by = has_boat ? travel->boat_y : -1;
    bool created;
    int s = nav5_find_or_add(
        nav_state_key(from.y * NAV_W + from.x, travel->mode),
        &created);
    nav5_tab[s].g = 0; nav5_tab[s].parent = -1;
    nav5_tab[s].px = from.x; nav5_tab[s].py = from.y; nav5_tab[s].open = 1;
    nav5_tab[s].h = nav_heuristic(from.y * NAV_W + from.x, goal, neighbors);
    nav_heap_push(s, nav5_tab[s].g + nav5_tab[s].h);

    for (;;) {
        if (nav_heap_n == 0) return -1;
        NavHeapEnt ent = nav_heap_pop();
        int best = ent.slot;
        // Lazy deletion: skip an entry that is stale — its slot was already
        // closed, or a better g was found after this entry was pushed (so its
        // recorded priority no longer matches). The live entry is still in the
        // heap and will be popped later.
        if (!nav5_tab[best].open) continue;
        if (ent.f != nav5_tab[best].g + nav5_tab[best].h) continue;
        nav5_tab[best].open = 0;

        uint64_t key = nav5_tab[best].key;
        int ccell  = (int)(key >> 1);
        int cmode  = (int)(key & 1u);
        int cx = ccell % NAV_W, cy = ccell / NAV_W;

        if (dist_out) {
            if (dist_out[ccell] < 0 || nav5_tab[best].g < dist_out[ccell])
                dist_out[ccell] = nav5_tab[best].g;
        } else if (ccell == goal) {
            // Popped with minimal f and h(goal)==0, so g is the optimal cost to
            // this cell (in whichever mode) — the same node the exact Dijkstra
            // would have settled. Admissible heuristic ⇒ path length unchanged.
            return best;
        }

        for (int k = 0; k < 8; k++) {
            if (neighbors == 4 && NAV_DX[k] != 0 && NAV_DY[k] != 0) continue;
            int nx = cx + NAV_DX[k], ny = cy + NAV_DY[k];
            if (nx < 0 || nx >= NAV_W || ny < 0 || ny >= NAV_H) continue;
            const Tile *nt = MapGetTile(map, nx, ny);
            if (!nt) continue;

            int ncell = ny * NAV_W + nx;
            int nmode = cmode;
            bool legal = false, boarding = false;
            bool is_goal = (!dist_out && ncell == goal);
            if (cmode == NAV_MODE_FOOT) {
                // Board only by stepping onto the boat's real tile
                // (engine/step.c:54-55).
                if (has_boat && nx == bx && ny == by) {
                    nmode = NAV_MODE_BOAT; legal = true; boarding = true;
                } else {
                    nmode = NAV_MODE_FOOT;
                    legal = (is_goal && opts->goal_is_bouncer)
                              ? adventure_walkable_on_foot(nt)
                              : nav_passable(nt, opts);
                }
            } else {
                if (!adventure_walkable_in_boat(nt)) { legal = false; }
                else if (nt->terrain == TERRAIN_WATER || nt->is_bridge) {
                    nmode = NAV_MODE_BOAT; legal = true;
                } else {
                    // Disembark onto land.
                    nmode = NAV_MODE_FOOT;
                    legal = (is_goal && opts->goal_is_bouncer)
                              ? adventure_walkable_on_foot(nt)
                              : nav_passable(nt, opts);
                }
            }
            if (!legal) continue;
            // FLIGHT: uniform edge cost — desert does not eat extra steps in
            // the air (engine charges flying steps flat), so the sim's day
            // accounting must match.
            int step = (boarding || opts->fly)
                         ? NAV_MIN_EDGE : nav_edge_cost_mode(nt, nmode);
            if (step <= 0) continue;
            int tentative = nav5_tab[best].g + step;

            uint64_t nk = nav_state_key(ncell, nmode);
            bool made;
            int ni = nav5_find_or_add(nk, &made);
            if (made) { nav5_tab[ni].g = NAV_INF; nav5_tab[ni].parent = -1;
                        nav5_tab[ni].open = 0;
                        nav5_tab[ni].h = nav_heuristic(ny * NAV_W + nx, goal,
                                                       neighbors); }
            if (tentative < nav5_tab[ni].g) {
                nav5_tab[ni].g = tentative;
                nav5_tab[ni].parent = best;
                nav5_tab[ni].px = nx; nav5_tab[ni].py = ny;
                nav5_tab[ni].open = 1;
                // Lazy decrease-key: push a fresh entry at the improved
                // priority; any older entry for this slot is skipped on pop.
                nav_heap_push(ni, nav5_tab[ni].g + nav5_tab[ni].h);
            }
        }
    }
}

// Validate endpoints and map; returns false (with status) on bad input.
static bool nav_check(const Map *map, NavPoint from, NavPoint to,
                      NavStatus *status) {
    if (!map) { *status = NAV_BADARGS; return false; }
    if (from.x < 0 || from.x >= map->width ||
        from.y < 0 || from.y >= map->height ||
        to.x   < 0 || to.x   >= map->width ||
        to.y   < 0 || to.y   >= map->height) {
        *status = NAV_BADARGS; return false;
    }
    return true;
}

// Foot-only entries: thin wrappers over the boat-aware search
// with no boat (has_boat = false), so there's a single search implementation.
NavStatus nav_next_step(const Map *map, NavPoint from, NavPoint to,
                        const NavOptions *opts, int *out_dx, int *out_dy) {
    NavTravel foot = { NAV_MODE_FOOT, false, -1, -1 };
    return nav_next_step_travel(map, from, &foot, to, opts, out_dx, out_dy);
}

bool nav_reachable(const Map *map, NavPoint from, NavPoint to,
                   const NavOptions *opts, int *out_steps) {
    NavTravel foot = { NAV_MODE_FOOT, false, -1, -1 };
    return nav_reachable_travel(map, from, &foot, to, opts, out_steps);
}

NavStatus nav_next_step_travel(const Map *map, NavPoint from,
                               const NavTravel *travel, NavPoint to,
                               const NavOptions *opts,
                               int *out_dx, int *out_dy) {
    NavOptions lo; NavTravel lt;
    if (!opts)   { nav_default_options(&lo); opts = &lo; }
    if (!travel) { lt.mode = NAV_MODE_FOOT; lt.has_boat = false;
                   lt.boat_x = lt.boat_y = -1; travel = &lt; }
    if (out_dx) *out_dx = 0;
    if (out_dy) *out_dy = 0;
    NavStatus bad;
    if (!nav_check(map, from, to, &bad)) return bad;
    if (from.x == to.x && from.y == to.y) return NAV_ARRIVED;

    int goal = nav5_run(map, from, travel, to.y * NAV_W + to.x, opts, NULL);
    if (goal < 0) return NAV_UNREACHABLE;
    int cur = goal;
    if (nav5_tab[cur].parent < 0) return NAV_UNREACHABLE;
    while (nav5_tab[nav5_tab[cur].parent].parent >= 0)
        cur = nav5_tab[cur].parent;
    if (out_dx) *out_dx = nav5_tab[cur].px - from.x;
    if (out_dy) *out_dy = nav5_tab[cur].py - from.y;
    return NAV_OK;
}

bool nav_reachable_travel(const Map *map, NavPoint from,
                          const NavTravel *travel, NavPoint to,
                          const NavOptions *opts, int *out_steps) {
    NavOptions lo; NavTravel lt;
    if (!opts)   { nav_default_options(&lo); opts = &lo; }
    if (!travel) { lt.mode = NAV_MODE_FOOT; lt.has_boat = false;
                   lt.boat_x = lt.boat_y = -1; travel = &lt; }
    if (out_steps) *out_steps = 0;
    NavStatus bad;
    if (!nav_check(map, from, to, &bad)) return false;
    if (from.x == to.x && from.y == to.y) return true;

    int goal = nav5_run(map, from, travel, to.y * NAV_W + to.x, opts, NULL);
    if (goal < 0) return false;
    if (out_steps) {
        int steps = 0, cur = goal;
        while (nav5_tab[cur].parent >= 0) { cur = nav5_tab[cur].parent; steps++; }
        *out_steps = steps;
    }
    return true;
}

void nav_distance_field_travel(const Map *map, NavPoint from,
                               const NavTravel *travel, const NavOptions *opts,
                               int *out_dist) {
    NavOptions lo; NavTravel lt;
    if (!opts)   { nav_default_options(&lo); opts = &lo; }
    if (!travel) { lt.mode = NAV_MODE_FOOT; lt.has_boat = false;
                   lt.boat_x = lt.boat_y = -1; travel = &lt; }
    if (!out_dist) return;
    for (int i = 0; i < NAV_CELLS; i++) out_dist[i] = -1;
    if (!map || from.x < 0 || from.x >= map->width ||
        from.y < 0 || from.y >= map->height) return;
    nav5_run(map, from, travel, -1, opts, out_dist);
}

// ===================== folded from navigator.c (P6: collapse sub-solver into the executor) =====================
//
// THE NAVIGATOR (see navigator.h). One public function, nav(), reaches a target
// tile (dest_zone, dest_x, dest_y) from the hero's current position by ANY means —
// foot, boat rent/board/abandon, ocean<->land, cross-zone sail — driving the LIVE
// engine and recording every prim (REC_MOVE / RA_*) into `rec`. Unreachable targets
// ASSERT (loud), never return a soft no.
//
// All the orchestration that used to be scattered across driver_walk_to,
// planner_pursue_travel, recruit_excursion_target and the planner_* routers lives
// here, in static helpers behind the single nav() entry point. The A* search math is
// reused from the proven core for now; at the nav.c deletion step it is ported in
// here verbatim (never re-derived) so the search is never duplicated.
//
// DEFERRED (one piece): COMBAT-BEARING steps — stepping onto a hostile foe, whether
// a path-blocker or a SLAY target — fire FLOW_ATTACK_FOE and need combat resolution,
// which is the Executor's domain. This skeleton handles all NON-combat movement
// (chests / towns / castles / dwellings / alcoves / dig sites / cross-zone sail).
// When a target is reachable ONLY by passing through a foe (a path that opens under
// ALL_WALKABLE but not under the bouncer block), nav() returns false — "reachable
// once the Executor can fight," the planner's cue to defer it — rather than
// asserting. The loud assert is reserved for a target with NO path even through
// every interactive: a genuine geometric wall or malformed target (a real bug).

#include "navigator.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nav.h"      // NavPoint/NavTravel/NavOptions + the A* search (ported in at
                      // the nav.c deletion step; reused here meanwhile)
#include "step.h"     // GameStep  (the engine's one-tile walk primitive)
#include "map.h"
#include "tile.h"

// Per-leg / per-journey livelock guards. The layered (FOOT + BOAT) state space on a
// 64x64 zone has 2*64*64 = 8192 states; a path visiting every state is 8192 steps.
// Budget is set to 2x that to safely cover any reachable target. Blowing the budget
// means a routing defect, so it asserts. NAV_MAX_LEGS bounds cross-zone hops.
#define NAV_MAX_STEPS 16384
#define NAV_MAX_LEGS  32

// 8-neighbour scan order (fixed, for deterministic gate/approach selection).
static const int NDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int NDY[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

// ---------------------------------------------------------------------------------
// Loud failure: dump the full nav context and abort. nav() never accepts a quiet
// "can't get there" — an unreachable real target is a BUG, surfaced immediately.
// ---------------------------------------------------------------------------------
static void nav_fail(const Game *g, const char *dz, int dx, int dy,
                     const char *why) {
    fprintf(stdout,
            "\n*** NAV FAILURE: %s ***\n"
            "  hero  = zone=%s (%d,%d) mode=%s\n"
            "  boat  = %s @ (%d,%d) zone=%s\n"
            "  target= zone=%s (%d,%d)\n\n",
            why,
            g->position.zone, g->position.x, g->position.y,
            g->travel_mode == TRAVEL_BOAT ? "BOAT" : "FOOT",
            g->boat.has_boat ? "held" : "none", g->boat.x, g->boat.y,
            g->boat.zone[0] ? g->boat.zone : "(none)",
            dz ? dz : "(?)", dx, dy);
    assert(!"nav: unreachable target (see the log above for hero/boat/target state)");
}

// ---------------------------------------------------------------------------------
// Engine-driving primitives (record then apply).
// ---------------------------------------------------------------------------------

// The hero's travel state for the boat-aware A*. The held boat is usable only when it
// is parked in the CURRENT zone (a cross-zone sail leaves it behind).
static NavTravel nav_travel(const Game *g) {
    bool here = g->boat.has_boat && strcmp(g->boat.zone, g->position.zone) == 0;
    NavTravel tv;
    tv.mode     = (g->travel_mode == TRAVEL_BOAT) ? NAV_MODE_BOAT : NAV_MODE_FOOT;
    tv.has_boat = here;
    tv.boat_x   = here ? g->boat.x : -1;
    tv.boat_y   = here ? g->boat.y : -1;
    return tv;
}

// Record a REC_MOVE then apply it (the executor replays this exact (dx,dy) on the
// byte-identical live world, reproducing board/disembark/consume/bounce).
static void nav_move(Game *g, Map *map, Fog *fog, const Resources *res,
                     int dx, int dy, RecSink *rec) {
    if (rec && rec->prims) {
        RecPrim rp; memset(&rp, 0, sizeof rp);
        rp.kind = REC_MOVE; rp.dx = (int8_t)dx; rp.dy = (int8_t)dy;
        recbuf_push(rec->prims, rp);
    }
    GameStep(g, map, fog, res, dx, dy);
}

// Record a REC_ACTION (a direct engine action: rent-boat, travel-zone, ...).
static void nav_action(RecSink *rec, RecActionKind a, const char *id,
                       int x, int y) {
    if (!rec || !rec->prims) return;
    RecPrim rp; memset(&rp, 0, sizeof rp);
    rp.kind = REC_ACTION; rp.action = a;
    if (id) snprintf(rp.act_id, sizeof rp.act_id, "%s", id);
    rp.act_x = x; rp.act_y = y;
    recbuf_push(rec->prims, rp);
}

// Index of `zone_id` in res->zones[], or -1.
static int nav_zone_index(const Resources *res, const char *zone_id) {
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, zone_id) == 0) return i;
    return -1;
}

// ---------------------------------------------------------------------------------
// Per-leg walker: step the hero to (tx,ty) in the CURRENT zone, boat-aware, one move
// at a time, recording each. The goal tile is exempt from the bouncer block so the
// hero is delivered ONTO the target object (town gate / castle / chest / boat tile).
// Returns true on arrival; false if no step is available (target unreachable from the
// current travel state, or the hero stopped making progress).
// ---------------------------------------------------------------------------------
static bool nav_walk_to(Game *g, Map *map, Fog *fog, const Resources *res,
                        int tx, int ty, RecSink *rec) {
    NavOptions opts; nav_default_options(&opts);
    opts.goal_is_bouncer = true;   // deliver onto the target tile, not beside it

    for (int guard = 0; guard < NAV_MAX_STEPS; guard++) {
        if (g->position.x == tx && g->position.y == ty) return true;

        NavTravel tv = nav_travel(g);
        NavPoint from = { g->position.x, g->position.y };
        NavPoint to   = { tx, ty };
        int dx = 0, dy = 0;
        NavStatus ns = nav_next_step_travel(map, from, &tv, to, &opts, &dx, &dy);
        if (ns == NAV_ARRIVED) return true;
        if (ns != NAV_OK || (dx == 0 && dy == 0)) return false;  // unreachable

        int px = g->position.x, py = g->position.y;
        nav_move(g, map, fog, res, dx, dy, rec);
        // No progress means a bounce we did not model (a foe gate, a town we are not
        // entering) — stop and let the caller decide (rent a boat / fail loudly).
        if (g->position.x == px && g->position.y == py) return false;
    }
    // Catch arrival exactly on the final step (off-by-one: arrival check fires at
    // the top of each iteration, so a step that lands on the target at guard==limit
    // is seen here, not at guard+1 which the loop never enters).
    if (g->position.x == tx && g->position.y == ty) return true;
    nav_fail(g, g->position.zone, tx, ty, "walk exceeded step budget (livelock)");
    return false;   // unreachable (nav_fail asserts)
}

// ---------------------------------------------------------------------------------
// Town + boat acquisition.
// ---------------------------------------------------------------------------------

// Nearest reachable town in the current zone (boat-aware path cost). Out: approach
// tile (a walkable neighbour to gate from) and the town's dock (boat spawn) coords.
// Ported from the old planner_nearest_town; self-contained.
static bool nav_nearest_town(const Game *g, const Map *map,
                             int *appr_x, int *appr_y,
                             int *dock_x, int *dock_y) {
    int zi = nav_zone_index(g->res, g->position.zone);
    if (zi < 0) return false;
    const ResZone *z = &g->res->zones[zi];
    NavTravel tv = nav_travel(g);
    NavPoint from = { g->position.x, g->position.y };
    NavOptions opts; nav_default_options(&opts);

    int best = -1, bx = -1, by = -1, bdx = -1, bdy = -1;
    for (int t = 0; t < z->town_count; t++) {
        const ResTown *zt = resources_zone_town(g->res, z, t);
        if (!zt) continue;
        for (int k = 0; k < 8; k++) {            // towns bounce; approach a neighbour
            int ax = zt->x + NDX[k], ay = zt->y + NDY[k];
            if (ax < 0 || ax >= map->width || ay < 0 || ay >= map->height) continue;
            int steps = 0;
            if (!nav_reachable_travel(map, from, &tv, (NavPoint){ax, ay},
                                      &opts, &steps)) continue;
            if (best < 0 || steps < best) {
                best = steps; bx = ax; by = ay; bdx = zt->boat_x; bdy = zt->boat_y;
            }
            break;   // first reachable approach for this town is enough
        }
    }
    if (best < 0) return false;
    *appr_x = bx; *appr_y = by; *dock_x = bdx; *dock_y = bdy;
    return true;
}

// From the approach tile, the single step onto the adjacent INTERACT_TOWN gate (which
// sets in_town and bounces back). Returns false if no adjacent gate (caller fails).
static bool nav_town_gate_step(const Game *g, const Map *map,
                               int appr_x, int appr_y, int *out_dx, int *out_dy) {
    (void)g;
    for (int k = 0; k < 8; k++) {
        const Tile *t = MapGetTile(map, appr_x + NDX[k], appr_y + NDY[k]);
        if (t && t->interactive == INTERACT_TOWN) {
            *out_dx = NDX[k]; *out_dy = NDY[k];
            return true;
        }
    }
    return false;
}

// Route to the nearest reachable town and gate IN (sets position.in_town). On
// success the hero stands at the gate's approach tile with in_town set; *out_dock_x/y
// (optional) receive the town's boat-dock coords for a subsequent rent. Returns
// false if no town is reachable or the gate-in failed. Public: the Executor uses it
// for town services (buy siege weapons / spells / take a contract).
bool nav_enter_nearest_town(Game *g, Map *map, Fog *fog, const Resources *res,
                            int *out_dock_x, int *out_dock_y, RecSink *rec) {
    int ax, ay, dx, dy;
    if (!nav_nearest_town(g, map, &ax, &ay, &dx, &dy)) return false;
    if (!nav_walk_to(g, map, fog, res, ax, ay, rec)) return false;   // reach approach
    if (!g->position.in_town[0]) {
        int gdx, gdy;
        if (!nav_town_gate_step(g, map, ax, ay, &gdx, &gdy)) return false;
        nav_move(g, map, fog, res, gdx, gdy, rec);   // step onto gate -> in_town set
    }
    if (!g->position.in_town[0]) return false;
    if (out_dock_x) *out_dock_x = dx;
    if (out_dock_y) *out_dock_y = dy;
    return true;
}

// Rent a boat: enter the nearest town, then GameRentBoat at its dock. The boat is
// parked at the dock in the current zone. Returns true on a successful rental.
static bool nav_rent_boat(Game *g, Map *map, Fog *fog, const Resources *res,
                          RecSink *rec) {
    int dx, dy;
    if (!nav_enter_nearest_town(g, map, fog, res, &dx, &dy, rec)) return false;
    nav_action(rec, RA_RENT_BOAT, NULL, dx, dy);
    return GameRentBoat(g, dx, dy, g->position.zone) == BOAT_RENT_OK;
}

// Ensure the hero is sailing (TRAVEL_BOAT): acquire a boat if none is held in this
// zone, then board it by walking onto its tile (GameStep boards automatically).
// If the held boat is stranded in open water (left there after a cross-zone trip,
// with no adjacent land to approach from), we fall back to renting a new boat at
// the nearest town — GameRentBoat overwrites the stranded reference with the dock.
static bool nav_ensure_sailing(Game *g, Map *map, Fog *fog, const Resources *res,
                               RecSink *rec) {
    if (g->travel_mode == TRAVEL_BOAT) return true;
    NavTravel tv = nav_travel(g);
    if (tv.has_boat) {
        // Try to board the held boat; succeed for the normal (coastal) case.
        if (nav_walk_to(g, map, fog, res, g->boat.x, g->boat.y, rec))
            return g->travel_mode == TRAVEL_BOAT;
        // Boat is stranded — rent a replacement (overwrites the old reference).
    }
    if (!nav_rent_boat(g, map, fog, res, rec)) return false;
    // Board: stepping onto the boat's tile flips travel_mode to BOAT (engine/step.c).
    if (!nav_walk_to(g, map, fog, res, g->boat.x, g->boat.y, rec)) return false;
    return g->travel_mode == TRAVEL_BOAT;
}

// Cross-zone sail to a DISCOVERED zone: get the hero sailing, then GameSwitchZone +
// GameSpendWeek (the same engine core and legality as the shell's zone picker). On
// arrival the hero stands at the destination's spawn (engine sets the travel mode to
// the spawn terrain; the boat is left behind in the origin zone).
// Returns false (soft defer) when:
//   - destination is undiscovered (navmap not yet collected),
//   - hero cannot get sailing (boat stranded + all towns blocked by unbeatable foes).
// Asserts only on GameSwitchZone refusal, which is a true engine bug.
static bool nav_sail_to_zone(Game *g, Map *map, Fog *fog, const Resources *res,
                              const char *dest, RecSink *rec) {
    int zi = nav_zone_index(res, dest);
    if (zi < 0 || !g->world.zones_discovered[zi])
        return false;   // need the navmap for this zone first
    if (!nav_ensure_sailing(g, map, fog, res, rec))
        return false;   // can't get sailing — foes blocking all towns, defer

    nav_action(rec, RA_TRAVEL_ZONE, dest, 0, 0);
    if (!GameSwitchZone(g, map, fog, dest))
        nav_fail(g, dest, -1, -1, "GameSwitchZone refused a legal crossing");
    int commission = 0;
    GameSpendWeek(g, &commission);   // crossing costs the rest of the week
    return true;
}


// ---------------------------------------------------------------------------------
// THE NAVIGATOR. Reach (dest_zone, dest_x, dest_y) end-to-end.
// ---------------------------------------------------------------------------------
bool nav(Game *g, Map *map, Fog *fog, const Resources *res,
         const char *dest_zone, int dest_x, int dest_y, RecSink *rec) {
    if (!g || !map || !res || !dest_zone || !dest_zone[0]) return false;

    for (int leg = 0; leg < NAV_MAX_LEGS; leg++) {
        // 1. Wrong zone? Sail there first, then re-evaluate. Returns false (defer)
        //    when the zone is undiscovered or the hero can't get sailing.
        if (strcmp(g->position.zone, dest_zone) != 0) {
            if (!nav_sail_to_zone(g, map, fog, res, dest_zone, rec))
                return false;
            continue;
        }
        // 2. Right zone — arrived?
        if (g->position.x == dest_x && g->position.y == dest_y) return true;
        // 3. Walk/sail to the tile within this zone.
        if (nav_walk_to(g, map, fog, res, dest_x, dest_y, rec)) return true;
        // 4. The boat-aware mover (step 3, which re-plans each step so it boards,
        //    sails, disembarks and RE-boards across multiple legs) could not reach the
        //    tile with the hero's current boat state. If he holds no boat, rent one at
        //    the nearest town and retry (now the mover can sail). If he already holds a
        //    boat and STILL could not get there, there is nothing more to try from here
        //    right now — DEFER (return false) so the planner retries later from a better
        //    spot. The hero keeps PLAYING; he never crashes mid-game.
        NavTravel tv = nav_travel(g);
        bool have_boat = (tv.mode == NAV_MODE_BOAT) || tv.has_boat;
        if (!have_boat && nav_rent_boat(g, map, fog, res, rec))
            continue;       // got a boat — let the mover try again
        return false;       // defer (no boat available, or boat did not help from here)
    }
    nav_fail(g, dest_zone, dest_x, dest_y, "exceeded cross-zone leg budget (livelock)");
    return false;   // unreachable (nav_fail asserts)
}
