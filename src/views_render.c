// src/views_render.c -- the detail-view dispatcher.
//
// WHICH detail view is drawn is decided here and is the same in both render
// modes; HOW each one looks is not, so the drawing goes to one of the two
// implementations behind views_render_impl.h:
//
//   src/legacy/views_render.c  -- the DOS original, frozen.
//   src/modern/views_render.c  -- modern UI work.

#include "views_render.h"
#include "views_render_impl.h"
#include "layout.h"

// The world map's reveal toggle: without the orb the map is fog-limited and
// SPACE does nothing; with it, SPACE swaps between "your map" and the
// continent-wide reveal. Player state, so it lives here rather than in either
// draw path.
static bool s_worldmap_whole_map = false;

void views_render_worldmap_toggle_hero_only(void) {
    s_worldmap_whole_map = !s_worldmap_whole_map;
}

bool views_render_worldmap_whole(void) {
    return s_worldmap_whole_map;
}

void views_render_draw(const Game *g, const Map *m, const Fog *f,
                       const Sprites *s) {
    if (CL_IS_MODERN) modern_views_render_draw(g, m, f, s);
    else              legacy_views_render_draw(g, m, f, s);
}
