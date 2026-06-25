// autoplay/nav.h
//
// Deterministic A* pathfinding for autoplay. Operates on the in-memory Map
// with NO side effects and yields the next single step toward a target tile,
// which the driver feeds to GameStep one move per turn.
//
// Engine-only: this header pulls in engine map/tile/adventure headers and
// nothing from src/. Foot travel only for M1 (zone 0); boat/flight modes are
// future work (the passability hook leaves room for them).
//
// Movement model matches the engine: 8-directional (the shell's input layer
// drives diagonals via the numpad, and GameStep accepts any dx,dy), with
// corner-cutting permitted (the engine checks only the destination tile). A*
// edge weights come from TerrainMoveCost so desert detours cost what they
// cost in-engine.

#ifndef OB_AUTOPLAY_NAV_H
#define OB_AUTOPLAY_NAV_H

#include <stdbool.h>

#include "map.h"

// A single tile coordinate.
typedef struct {
    int x, y;
} NavPoint;

// How A* treats interactive overlays during pathfinding. The engine's
// adventure_walkable_on_foot() returns true for EVERY interactive tile, but
// GameStep then BOUNCES the hero off castle gates, towns, dwellings, the
// alcove, and hostile foes (it walks ONTO chests / artifacts / orb / navmap /
// signs, consuming them). So pass-through planning must treat bounce tiles as
// obstacles, while still allowing a bounce tile to be the GOAL's neighbor.
//
// NAV_INTERACT_BLOCK_BOUNCERS is the correct default: a path it returns is one
// GameStep will actually walk, tile for tile (the goal aside).
typedef enum {
    NAV_INTERACT_BLOCK_BOUNCERS = 0, // bounce tiles are obstacles (default)
    NAV_INTERACT_ALL_WALKABLE,       // trust adventure_walkable_on_foot verbatim
    NAV_INTERACT_FOE_PASSABLE,       // like BLOCK_BOUNCERS, but INTERACT_FOE
                                     // tiles are traversable. Used to plan a
                                     // route THROUGH a hostile foe that blocks
                                     // the way: the planner steps onto the foe,
                                     // the engine raises FLOW_ATTACK_FOE, and a
                                     // won fight clears the overlay so the path
                                     // opens. Gates/towns/dwellings/alcove stay
                                     // blocked (unlike ALL_WALKABLE).
} NavInteractPolicy;

// Options controlling a path search. Zero-initialized gives the M1 default:
// foot travel, bouncers blocked, diagonals allowed.
typedef struct {
    NavInteractPolicy interact_policy;
    bool              allow_diagonal;  // when false, 4-neighbor only
    // When true, the goal tile itself is exempt from the bouncer block (you
    // are pathing TO a bounce tile, e.g. to read its adjacency). Normally the
    // caller targets an adjacent tile instead; left here for completeness.
    bool              goal_is_bouncer;
    // FLIGHT routing: when true, every in-bounds tile is passable (the
    // engine's flight rule — terrain, walls, water, and interactives all
    // ignore an airborne hero) and every edge costs the minimum (desert does
    // not eat extra steps in flight). Callers pass a FOOT travel with no boat;
    // the boat layer is meaningless in the air. Used by the fly pursuit to
    // route between take-off and the landing site.
    bool              fly;
} NavOptions;

// Outcome of a next-step query.
typedef enum {
    NAV_OK = 0,        // *dx,*dy set to the next step toward the goal
    NAV_ARRIVED,       // from == to already; no step needed
    NAV_UNREACHABLE,   // no path exists under the passability policy
    NAV_BADARGS,       // null map / off-map endpoint
} NavStatus;

// Travel mode for mode-aware (foot + boat) search. Mirrors the engine's
// TravelMode but kept local so nav stays a leaf module.
typedef enum {
    NAV_MODE_FOOT = 0,
    NAV_MODE_BOAT,
} NavMode;

// The hero's current travel state for a boat-aware search. `mode` is how the
// hero is moving NOW; if `has_boat`, a boat sits at (boat_x,boat_y) and the
// hero can board it by stepping onto that tile (mirrors GameStep). When
// already NAV_MODE_BOAT the hero IS the boat (boat coords track the hero).
typedef struct {
    NavMode mode;
    bool    has_boat;
    int     boat_x, boat_y;
} NavTravel;

// Fill the default options (foot, block bouncers, diagonals on).
void nav_default_options(NavOptions *opts);

// ---------------------------------------------------------------------------
// Boat-aware (multi-mode) navigation (Phase 3b).
//
// A layered-graph search over (x, y, travel_mode): two layers (FOOT, BOAT) with
// board/disembark as transition edges, matching GameStep (engine/step.c): from
// FOOT, stepping onto the boat's real tile (travel->boat_x/y) boards (-> BOAT);
// in BOAT, a water/bridge step sails on and a land step disembarks (-> FOOT).
// Boarding only at the boat's real tile makes the returned path one GameStep
// walks tile-for-tile AND keeps its distances equal to the engine drive (the
// planner relies on the latter for decisions and turn-budgeting). State is
// cells x 2 — no boat-cell dimension — so the distance field stays cheap. A
// one-way path boards at most once at the boat's real tile; multi-leg journeys
// complete across the run as the engine repositions the boat on each disembark
// and the caller re-queries the live position. Goal reached in EITHER mode.
//
// `travel` is the hero's current mode + boat location. The returned step is
// one tile move the caller feeds to GameStep; the engine performs the same
// board/disembark transition, so the live walk matches the plan (parity).
// Pure and deterministic, same tie-break as the foot search.
// ---------------------------------------------------------------------------
NavStatus nav_next_step_travel(const Map *map, NavPoint from,
                               const NavTravel *travel, NavPoint to,
                               const NavOptions *opts,
                               int *out_dx, int *out_dy);

// Boat-aware reachability + path length.
bool nav_reachable_travel(const Map *map, NavPoint from,
                          const NavTravel *travel, NavPoint to,
                          const NavOptions *opts, int *out_steps);

#endif
