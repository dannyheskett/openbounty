#ifndef FLOWS_H
#define FLOWS_H

#include "raylib.h"

#include "game.h"
#include "map.h"
#include "resources.h"
#include "sprites.h"

// Encounter / week-end / endgame entry points. Originally inline in
// main.c; lifted into a shared TU so step.c (in the unit-test build,
// which excludes main.c) can call them.

void start_foe_friendly_flow(Game *game, Map *map, const Resources *res,
                             const char *foe_id, int nx, int ny);
void start_foe_hostile_flow(Game *game, const char *foe_id, int nx, int ny);
void schedule_week_end(const Game *g, int commission_paid);
void show_lose_game(const Game *g, const Resources *res);
void show_win_game(Game *g, const Resources *res,
                   RenderTexture2D *rt, const Sprites *sprites);

#endif
