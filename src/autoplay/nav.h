#ifndef OB_AUTOPLAY_NAV_H
#define OB_AUTOPLAY_NAV_H

// Generic goal navigator for autoplay.
//
// Given the current zone, hero position, travel mode, and boat
// availability, returns the next direction key to press to make
// progress toward (goal_x, goal_y). Returns 0 when the hero is already
// on the goal tile.
//
// Replans from the hero's current position on every call — robust to
// modal interruptions, blocked steps, dialog-absorbed keys, etc. No
// state is cached across ticks; the caller just keeps calling until 0
// is returned.
//
// Mode awareness:
//   - On foot: BFS over walkable land tiles. If the hero has a boat
//     parked somewhere, stepping onto the boat tile is allowed (it
//     boards).
//   - In a boat: BFS sails water tiles; the goal tile (if it's
//     walkable land) is allowed as a disembark target.
//
// Town routing:
//   - If the hero has no boat and the goal is unreachable on foot,
//     the navigator routes to the nearest reachable town tile. Once
//     the hero steps onto it, the engine's town flow takes over (the
//     autoplay GRIND phase handles the modal sequence and ends with a
//     rented boat). On the next tick the navigator re-plans, now with
//     a boat, and continues toward the original goal.
//
// Returns 0 if no path exists.

#include "game.h"
#include "map.h"

int ap_nav_step(const Game *g, const Map *m, int goal_x, int goal_y);

// Same as ap_nav_step, but routes around every alive hostile foe's
// chebyshev-<=2 pursuit envelope (the foe-follow trigger range, see
// engine/game.c:GameFoesFollow). The goal tile itself is exempt from
// the exclusion (allows stepping onto a chest adjacent to a foe).
int ap_nav_step_avoiding_foes(const Game *g, const Map *m,
                              int goal_x, int goal_y);

// Same as ap_nav_step_avoiding_foes, but also refuses to step onto
// any TERRAIN_DESERT tile (each desert step zeros the day's step
// budget — see engine/tile.c:TerrainMoveCost and engine/step.c:542
// GameOnStep — so even a single one consumes a full day). Useful
// for boat tours where the destination is grass-adjacent and we'd
// otherwise plow through a desert peninsula to save a few tiles.
int ap_nav_step_avoiding_foes_and_desert(const Game *g, const Map *m,
                                         int goal_x, int goal_y);

#endif
