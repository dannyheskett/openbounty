#ifndef OB_AUTOPLAY_NAVIGATOR_H
#define OB_AUTOPLAY_NAVIGATOR_H

// THE NAVIGATOR — the Executor's movement sub-solver, and the ONLY navigation entry
// point in the whole codebase. There is exactly one public function: nav().
//
// nav() reaches a target tile (dest_zone, dest_x, dest_y) from the hero's CURRENT
// position by ANY means the game allows, solved end-to-end in one call:
//   - foot movement (8-dir A* over the zone),
//   - boat rent at a town, board, sail, and abandon,
//   - ocean->land and land->ocean transitions (implicit on stepping ashore/aboard),
//   - cross-zone travel (sail to a discovered zone, GameSwitchZone, continue there).
//
// It drives the LIVE engine (GameStep / GameRentBoat / GameCancelBoat / GameSwitchZone
// / GameSpendWeek) and EMITS the replayable engine prims (REC_MOVE / RA_*) into `rec`
// as it goes — recording is the navigator's own job, not the caller's.
//
// FAILURE IS LOUD — for GENUINE unreachability. If no path exists even with every
// interactive treated as passable (a missing navmap, a malformed target, a routing
// defect), that is a BUG, never a quietly accepted "game mechanic": nav() asserts,
// dumping hero/target/zone/boat state, impossible to ignore in development.
//
// The one soft case is DEFERRED capability: a target reachable only by passing
// THROUGH a foe (or other interactive nav cannot yet clear) is not "unreachable" —
// it is reachable once the Executor can fight. nav() returns false there, the
// planner's cue to defer it. That is the sole `false`; everything else asserts.
//
// Everything navigation collapses into this one function: it REPLACES driver_walk_to,
// planner_pursue_travel, recruit_excursion_target, the ~24 planner_* routers, and the
// nav_* A* wrappers. The A* search math is ported in as nav()'s file-private machinery
// (navigator.c); nav.c is deleted. No other navigation entry point exists.
//
// LAYER: nav() is called only by execute() (the Executor). It calls only the engine.

#include <stdbool.h>

#include "game.h"        // Game, Map, Fog, travel/boat state, GameStep, GameSwitchZone
#include "resources.h"   // Resources (zone lookup, town placements)
#include "recording.h"   // RecSink — where nav() appends REC_MOVE / RA_* prims

// Reach (dest_zone, dest_x, dest_y) from the hero's current position, driving the live
// engine and recording every prim into `rec`. dest_zone may equal the current zone
// (pure intra-zone walk/sail) or another DISCOVERED zone (cross-zone sail first).
//
// Returns true on arrival (hero stands on the target tile, in whatever travel mode the
// approach left him). Asserts (does not return) if the target is genuinely unreachable.
// MUTATES g / map / fog.
bool nav(Game *g, Map *map, Fog *fog, const Resources *res,
         const char *dest_zone, int dest_x, int dest_y, RecSink *rec);

// Route to the nearest reachable town and gate IN (sets position.in_town), driving
// the live engine and recording moves into `rec`. On success the hero stands at the
// town gate's approach tile with in_town set; *out_dock_x/y (may be NULL) receive
// the town's boat-dock coords. Returns false if no town is reachable. The Executor
// uses this for town services (buy siege weapons / spells, take a contract).
bool nav_enter_nearest_town(Game *g, Map *map, Fog *fog, const Resources *res,
                            int *out_dock_x, int *out_dock_y, RecSink *rec);

#endif
