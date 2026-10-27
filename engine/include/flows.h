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

// A friendly foe at (nx, ny) asks to join: the offer (what joins, how many) is
// rolled from the tile and the seed, and raised as FLOW_ACCEPT_FRIENDLY.
void start_foe_friendly_flow(Game *game, Map *map, const Resources *res,
                             const char *foe_id, int nx, int ny);
// A hostile foe at (nx, ny) bars the way: its scene and FLOW_ATTACK_FOE, Fight
// or Evade (forced for a fixed guardian).
void start_foe_hostile_flow(Game *game, const char *foe_id, int nx, int ny);
// The week has turned: queue the week-end screens (the astrology, then the
// budget) with the commission just paid.
void schedule_week_end(const Game *g, int commission_paid);
// The game is lost: compose the pack's lose text and open VIEW_LOSE.
void show_lose_game(const Game *g, const Resources *res);
// The game is won: compose the pack's win text and open VIEW_WIN.
void show_win_game(Game *g, const Resources *res);

#endif
