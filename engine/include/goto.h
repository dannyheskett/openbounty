#ifndef OB_GOTO_H
#define OB_GOTO_H

// Goto (#70): the route the hero travels to a tile the player picked. A
// player's route, not an oracle's: it uses only tiles the player has seen, it
// never steps onto an object on the way (the target may be one), and a sea
// crossing uses only the boat parked in this province. Modern shells walk it
// one GameStep at a time (src/shell_goto.c).

#include <stdbool.h>
#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

#define GOTO_MAX 1024   // longest route, in steps

typedef struct {
    int n;                       // steps
    signed char dx[GOTO_MAX];    // each step, -1..1
    signed char dy[GOTO_MAX];
    int days;                    // days the route takes to finish, 0 = today
} GotoPath;

// The cheapest route from the hero to (tx, ty) in the current province: 1 a
// step, a desert step the rest of a day (res->time.day_steps). On foot it
// walks; stepping onto the parked boat sails, and land beyond the water lands
// it; flying flies straight over anything. False when the target is unseen,
// is the hero's own tile, or no route reaches it.
bool GamePlanGoto(const Game *g, const Map *m, const Fog *f, const Resources *res,
                  int tx, int ty, GotoPath *out);

#endif
