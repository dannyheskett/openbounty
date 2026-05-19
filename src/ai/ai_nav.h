// src/ai/ai_nav.h
//
// Path planning for the AI driver. The map is at most 64x64 so a flood
// fill (BFS) is fast enough; we use it both to compute path-length to a
// goal and to recover the first-step direction toward that goal.
//
// All checks treat the AI's current travel mode: walking, sailing, or
// flying. Castle gates, towns, dwellings, etc. are walkable destinations
// even though stepping onto them triggers a dialog — the dialog dismissal
// is handled in ai_driver.c.

#ifndef OB_AI_NAV_H
#define OB_AI_NAV_H

#include <stdbool.h>
#include "game.h"
#include "map.h"

// Move mode for walkability tests.
typedef enum {
    AI_MOVE_FOOT = 0,
    AI_MOVE_BOAT,
    AI_MOVE_FLY,
} AiMoveMode;

// Compute the player's current move mode from Game state.
AiMoveMode ai_move_mode_for(const Game *g);

// True if the tile at (x,y) is enterable in `mode`. Out-of-bounds is
// always false. Interactive tiles (castles, towns, dwellings, chests,
// foes, etc.) count as enterable because stepping on them is how the
// AI fires triggers — except when `treat_interact_as_blocker` is set,
// which is useful when planning a path that should NOT trigger anything
// (e.g. routing around a hostile foe).
bool ai_walkable(const Map *m, int x, int y, AiMoveMode mode,
                 bool treat_interact_as_blocker);

// One step of the next move. (0,0) means "no path".
typedef struct {
    int dx, dy;          // -1/0/+1 each
    int dist;            // BFS distance from start to goal in tile-steps
    bool ok;             // true iff a path was found
} AiStep;

// BFS from (sx,sy) toward (gx,gy), then trace back to recover the first
// step. Returns dx,dy in [-1,1] for one 8-connected move toward the
// goal. Diagonal moves are allowed iff both orthogonal components are
// walkable (parity with the player step machinery in src/main.c).
//
// `avoid_interact` blocks interactive tiles along the route except for
// the goal tile itself — letting the AI walk past chests/foes if asked,
// but still arrive at a destination that happens to be a castle.
AiStep ai_path_step(const Map *m, AiMoveMode mode,
                    int sx, int sy, int gx, int gy,
                    bool avoid_interact);

#endif
