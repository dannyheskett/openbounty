// src/ai/ai_strategy.h
//
// Goal selection for the AI driver. Each frame the driver asks the
// strategy module for the next macro goal — a tile to walk toward and a
// short label that gets logged. The strategy reads game state and
// resource catalogs but never mutates anything; mutation happens in
// ai_driver.c via step_try() and prompt resolution.
//
// Goal priority (current):
//   1. If alive and have a contract villain whose castle is known →
//      go to that castle.
//   2. Otherwise, walk to nearest castle (gate tile) that isn't home.
//   3. Otherwise, walk to nearest unvisited town.
//   4. Otherwise, walk to a chest/artifact/dwelling visible on the map.
//   5. Otherwise, explore — pick the farthest reachable tile.

#ifndef OB_AI_STRATEGY_H
#define OB_AI_STRATEGY_H

#include <stdbool.h>
#include "game.h"
#include "map.h"
#include "fog.h"

typedef struct {
    int  gx, gy;            // goal tile in current zone
    char label[48];          // human-readable goal description
    bool ok;                 // false if no goal could be picked
} AiGoal;

// Choose a tactical goal for the current zone, given that the mission
// layer has decided we should be playing inside the zone (PLAY_ZONE).
// Reads g, m, and fog; never mutates. Returns ok=false when nothing
// productive is reachable on foot — that's the cue for the mission
// layer to transition.
AiGoal ai_strategy_pick(const Game *g, const Map *m, const Fog *fog);

// "Is the current zone fully exhausted on foot?" — the predicate the
// mission layer uses to leave PLAY_ZONE. True when there are no
// unvisited towns, no enemy castles, no live pickups (artifacts,
// chests, navmaps, orbs, telecaves, alcoves, fundable dwellings), and
// no fog-frontier tiles reachable on foot from the hero's current
// position. Pure water frontiers are excluded — sailing to nowhere
// doesn't count as progress in the current zone.
//
// Pre-computed once per tick by the driver and cached on the
// AiMissionCtx so we don't run the same BFS family twice in a frame.
bool ai_zone_exhausted(const Game *g, const Map *m, const Fog *fog);

// Count of villains whose home zone == `zone_id` and who have not yet
// been caught (per g->contract.villains_caught[]). The villain
// catalog groups villains by zone in ascending difficulty order — all
// of continentia first (easy), then forestria, archipelia, saharia.
// The mission layer uses this to keep the AI in a zone until all
// villains there are caught, rather than sailing onward to face
// villains tiers above the current army strength.
int ai_zone_uncaught_villains(const Game *g, const char *zone_id);

#endif
