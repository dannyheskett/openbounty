#ifndef OB_HUD_H
#define OB_HUD_H

#include "game.h"
#include "sprites.h"

// Right-side sidebar: Contract / Siege / Magic / Puzzle / Gold panels,
// always visible, stacked vertically in a column to the right of the
// map viewport.
void hud_draw(const Game *g, const Sprites *s);

// Single HUD tiles (CL_SIDEBAR_W x CL_TILE_H, framed) at (x, y).
void hud_draw_siege_tile(const Game *g, const Sprites *s, int x, int y);
void hud_draw_gold_tile(const Game *g, const Sprites *s, int x, int y);

#endif
