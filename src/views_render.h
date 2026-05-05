#ifndef VIEWS_RENDER_H
#define VIEWS_RENDER_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"

// Render the currently-active view in style. No-op when
// views_active() is VIEW_NONE, VIEW_MENU, VIEW_TOWN, or VIEW_OPTIONS
// (those have their own renderers elsewhere).
void views_render_draw(const Game *g, const Map *m, const Fog *f,
                        const Sprites *s);

// Toggle worldmap "hero only" mode .
void views_render_worldmap_toggle_hero_only(void);

#endif
