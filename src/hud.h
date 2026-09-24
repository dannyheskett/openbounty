#ifndef OB_HUD_H
#define OB_HUD_H

#include "game.h"
#include "sprites.h"
#include "input.h"

// Right-side sidebar, always visible, stacked in a column to the right of the
// map viewport. Legacy: Contract / Siege / Magic / Puzzle / Gold panels, each
// framed. Modern: Contract / Siege / Magic / Gold / Days, one column.
void hud_draw(const Game *g, const Sprites *s);

// The panel a finger landed on this frame, as the action it stands for, or
// INPUT_ACTION_NONE. Modern only: legacy's sidebar is a display.
InputAction hud_tapped(void);

// The puzzle tile: the grid, with a cover over every piece still to be won.
// `frame`: the panel's own frame (legacy's sidebar); a modern column has none.
void hud_draw_puzzle_tile(const Game *g, const Sprites *s, int x, int y, bool frame);

// Modern: a column of `tiles` one-tile panels from (x, y), w wide and h tall,
// made one piece -- a band across each join between tiles, and below the last
// tile the column's dark fill. Drawn after the tiles.
void hud_column_finish(int x, int y, int w, int h, int tiles);

// The key a tile's screen answers to, in the tile's top-left corner, while a
// keyboard is in use. Nothing otherwise.
void hud_key_hint(int x, int y, const char *key);

#endif
