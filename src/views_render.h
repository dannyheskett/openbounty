#ifndef OB_VIEWS_RENDER_H
#define OB_VIEWS_RENDER_H

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

// Modern: the one spells page (views_spells_input reads it), on the map and
// in a fight: `combat` greys the adventure column, else the combat one. `cur`
// 0..13 a spell, 14 the exit, labelled `exit_label`.
void modern_spells_draw(const Game *g, bool combat, int cur, const char *title, const char *right,
                        const char *exit_label);

#endif
