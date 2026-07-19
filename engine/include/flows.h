#ifndef OB_FLOWS_H
#define OB_FLOWS_H

#include "game.h"
#include "map.h"
#include "resources.h"

// Encounter / week-end / endgame entry points. Engine-side: composes
// the verdict text, opens the end-game view, schedules week-end work.
// The win cartoon is a render-layer effect -- callers that want it must
// invoke run_end_cartoon (src/end_cartoon.h) themselves *before*
// show_win_game.

void start_foe_friendly_flow(Game *game, Map *map, const Resources *res,
                             const char *foe_id, int nx, int ny);
void start_foe_hostile_flow(Game *game, const char *foe_id, int nx, int ny);
void schedule_week_end(const Game *g, int commission_paid);
void show_lose_game(const Game *g, const Resources *res);
void show_win_game(Game *g, const Resources *res);

#endif
