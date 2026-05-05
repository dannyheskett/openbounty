#ifndef STEP_H
#define STEP_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

// Attempt to walk the hero one tile in (dx, dy). Returns true if the
// hero's tile changed (a step was taken). False on edge-of-map, blocked
// terrain, or "bounced" interactive tiles (towns, castles, hostile
// foes). Performs all post-step bookkeeping: fog reveal, interactive
// tile dispatch, foes_follow advancement, day/week tick, week-end
// scheduling, and lose-game on day exhaustion.
//
// Extracted from main.c (try_step) on the main_refactor branch. The
// signature is preserved 1:1 so the refactor is purely structural.
bool step_try(Game *game, Map *map, Fog *fog,
              const Resources *res, int dx, int dy);

#endif
