// VIEW_OWN_CASTLE -- player-owned castle interior.
//
//   Persistent screen. Two modes: GARRISON (player -> castle) and
//   REMOVE (castle -> player). SPACE toggles modes. A..E moves the
//   matching slot. ESC returns to overworld.
//
//   Errors:
//     - "You cannot garrison your\n        last army!"  (rc==2 from
//       GameGarrisonTroop -- would strip the player of their last army)
//     - "There are no open slots\nor any of this army type!" (rc==1
//       from GameGarrisonTroop / GameUngarrisonTroop -- no fitting slot)

#ifndef OB_SCREENS_OWN_CASTLE_H
#define OB_SCREENS_OWN_CASTLE_H

#include "game.h"
#include "sprites.h"
#include <stdbool.h>

// Open the screen for the given castle. Stashes castle_id internally
// so the input dispatcher can act on slot picks. Idempotent.
void screen_own_castle_open(Game *g, const char *castle_id);

// Render: castle backdrop + bottom panel showing mode label + 5 rows.
void screen_own_castle_draw(const Game *g, const Sprites *s);

// Returns true if the screen is currently in GARRISON mode (player ->
// castle). Used by the input dispatcher in main.c to know which list
// of 5 troops the A-E key refers to.
bool screen_own_castle_is_garrison_mode(void);

// Toggle Garrison/Remove (called from main.c when SPACE pressed).
void screen_own_castle_toggle_mode(void);

// Castle id this screen is bound to. Used by main.c to call
// GameGarrisonTroop / GameUngarrisonTroop on key A..E.
const char *screen_own_castle_castle_id(void);

#endif
