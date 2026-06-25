#ifndef OB_MAP_RENDER_H
#define OB_MAP_RENDER_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"

// Render the 5x5 tile overworld viewport at native (1x) tile scale inside
// the CL_MAP_* rect. Hero is centered; camera clamps at map edges.
void map_render_draw(const Game *g, const Map *m, const Fog *f,
                      const Sprites *s);

#endif
