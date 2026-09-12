// src/views_render_impl.h
//
// The detail views' two draw paths. src/views_render.c is a dispatcher: it
// owns the public entry points in views_render.h and the one piece of state
// the views keep, and sends the drawing to one of these two implementations.
//
//   src/legacy/views_render.c  -- FROZEN. The DOS original's view panels.
//   src/modern/views_render.c  -- where modern UI work happens.
//
// Nothing outside this trio includes this header.

#ifndef OB_VIEWS_RENDER_IMPL_H
#define OB_VIEWS_RENDER_IMPL_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"
#include <stdbool.h>

void legacy_views_render_draw(const Game *g, const Map *m, const Fog *f,
                              const Sprites *s);
void modern_views_render_draw(const Game *g, const Map *m, const Fog *f,
                              const Sprites *s);

// The world map's reveal toggle. One flag, owned by the dispatcher: it is
// player state, not a drawing decision, so it does not fork.
bool views_render_worldmap_whole(void);

#endif
