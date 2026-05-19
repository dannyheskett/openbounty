// src/ai/ai_mission.h
//
// Mission state machine for the AI driver.
//
// The previous strategy was stateless — each tick picked the
// highest-value goal from a global priority list. That works inside a
// single zone but breaks the moment the player owns a boat: "go board
// the boat" (FOOT mode) and "drift to shore" (BOAT mode) become two
// equally high-value goals that contradict each other, and the AI
// oscillates every tick.
//
// Missions fix this by committing the AI to a multi-tick PLAN. While
// a mission is active the tactical goal picker can only choose goals
// that fit that mission's purpose — so the board-vs-shore conflict
// never arises (board only exists in BOARD_BOAT mission; shore-drift
// only exists in PLAY_ZONE when the hero arrived on water).
//
// Mission transitions happen at most once per tick, in
// ai_mission_update(), BEFORE the tactical goal is picked. Each
// mission stays active until its exit condition triggers — typically
// "I finished the multi-step plan" (boarded, sailed, arrived) or "the
// world changed under me" (boat lost, game over).
//
// PLAY_ZONE is the default. The AI only LEAVES it when the current
// zone is "exhausted" by ai_zone_exhausted (defined in ai_strategy.h)
// — no unvisited towns, no enemy castles, no pickups, no fog
// frontier worth chasing on foot. That predicate is the bedrock of
// the design: as long as something useful is reachable on foot,
// PLAY_ZONE keeps us tactically grounded.

#ifndef OB_AI_MISSION_H
#define OB_AI_MISSION_H

#include <stdbool.h>
#include "game.h"
#include "map.h"
#include "fog.h"

typedef enum {
    AI_MISSION_PLAY_ZONE = 0,   // default: pursue local goals on foot
    AI_MISSION_GO_TO_DOCK,      // walk to a coastal town to rent a boat
    AI_MISSION_RENT_BOAT,       // at the dock: invoke town BOAT row
    AI_MISSION_BOARD_BOAT,      // walk to the parked boat to board
    AI_MISSION_SAIL_NEXT,       // in BOAT mode: fire NEW_CONTINENT
    AI_MISSION_FIND_SCEPTER,    // late game: route to scepter tile
    AI_MISSION_SEARCH_SCEPTER,  // standing on scepter tile: fire SEARCH
    AI_MISSION_DONE,            // exit cleanly
} AiMission;

const char *ai_mission_name(AiMission m);

// Inputs the mission update / tactical layer share. The driver fills
// this in per tick from its own state and the game pointers; the
// mission layer reads it and never mutates the game.
typedef struct {
    const Game *g;
    const Map  *m;
    const Fog  *fog;

    // True when the strategy's "is this zone exhausted on foot" check
    // has returned true. Cached on the driver because the predicate is
    // moderately expensive (BFS to every town / castle / pickup).
    bool zone_exhausted;

    // Set by the driver when it loses its boat unexpectedly mid-plan
    // (e.g. weekly upkeep repo'd it between ticks while we were on
    // GO_TO_DOCK or BOARD_BOAT). Resets to false on every tick.
    bool boat_lost_recently;

    // For SAIL_NEXT cooldown: number of ticks since the last
    // successful navigate. Driver-maintained.
    int  ticks_since_sail;

    // True iff the hero's position.zone changed since the previous
    // tick (we successfully sailed). The mission layer uses this to
    // leave SAIL_NEXT regardless of post-arrival travel mode, so
    // arriving on water in the new zone doesn't trigger another
    // immediate NEW_CONTINENT.
    bool zone_changed;
} AiMissionCtx;

// Run the mission state machine once per tick. Returns the mission
// that should be active for this frame. The caller updates its stored
// mission with the returned value before dispatching the tactical
// goal.
//
// Implementation: a switch on the current mission, with the
// transition rules from the design doc. Stateless w.r.t. anything
// outside `ctx` and `current`.
AiMission ai_mission_update(AiMission current, const AiMissionCtx *ctx);

#endif
