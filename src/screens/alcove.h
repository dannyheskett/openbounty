// VIEW_ALCOVE — Archmage Aurange's spell-teaching alcove.
//
//   Hill/cave backdrop with an animated Gnomes sprite. Bottom panel
//   shows the greeting banner; a yes/no prompt overlays it.
//
//     y + gold >= cost  →  knows_magic = 1, spend_gold, clear tile
//     y + gold <  cost  →  "Begone" banner
//     n                 →  exit
//
// The screen lives only as long as the prompt — it pops as soon as
// the prompt resolves (success path or no path), or pops with the
// "Begone" dialog still up if the player can't afford.

#ifndef OB_SCREENS_ALCOVE_H
#define OB_SCREENS_ALCOVE_H

#include "../game.h"
#include "../sprites.h"

void screen_alcove_open(Game *g);
void screen_alcove_draw(const Game *g, const Sprites *s);

#endif
