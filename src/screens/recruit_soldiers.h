// VIEW_RECRUIT_SOLDIERS — recruit soldiers screen at home castle.
//
// The screen owns its own input handling (state machine `whom`):
//   - whom == 0: listening for letter A..E and SYN ticks
//   - whom != 0: inline numeric count entry on the right column

#ifndef OB_SCREENS_RECRUIT_SOLDIERS_H
#define OB_SCREENS_RECRUIT_SOLDIERS_H

#include "../game.h"
#include "../sprites.h"
#include <stdbool.h>

// Initialise state and push VIEW_RECRUIT_SOLDIERS.
void screen_recruit_soldiers_open(Game *g);

// Polled each frame by main.c while VIEW_RECRUIT_SOLDIERS is on top.
// Returns true if the screen should be dismissed (player pressed ESC
// while idle). Main.c then calls views_dismiss().
bool screen_recruit_soldiers_update(Game *g);

void screen_recruit_soldiers_draw(const Game *g, const Sprites *s);

#endif
