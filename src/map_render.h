#ifndef OB_MAP_RENDER_H
#define OB_MAP_RENDER_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"

// Render the overworld viewport inside the CL_MAP_* rect. Legacy: the 5x5
// whole tiles, the hero on the centre one except where the camera clamps at
// the map's edges. Modern: the hero's tile centred across the pane on the row
// that holds its middle, the rows flush with the columns' tiles, every cell
// the pane shows drawn to its last pixel, and no clamp -- past the world's
// edge is dark and the hero stays on the centre tile.
void map_render_draw(const Game *g, const Map *m, const Fog *f,
                      const Sprites *s);

// One map cell at `dst`: the ground under an object (or a landmark that names
// its own), then the cell's art, in their cosmetic variants. The one way a
// map cell is drawn -- the map, the gate's preview and the puzzle alike.
void map_render_cell(const Map *m, int mx, int my, Rectangle dst);

// The top-left corner of the hero's cell, in design pixels: where a tap's
// direction is measured from.
void map_render_hero_cell(const Game *g, const Map *m, int *x, int *y);
// The same, as the last frame drew it.
void map_render_last_hero_cell(int *x, int *y);

#endif
