#ifndef HUD_H
#define HUD_H

#include "game.h"
#include "sprites.h"

// Right-side sidebar: Contract / Siege / Magic / Puzzle / Gold panels,
// always visible, stacked vertically in a column to the right of the
// map viewport.
void hud_draw(const Game *g, const Sprites *s);

#endif
