// A cleared object on desert: plain grass by default (the original, the
// King's Bounty pack), the cell's own sand when world.clear_keeps_ground is
// set (REQ-229f, #107).

#include "greatest.h"
#include "fixtures.h"
#include "map.h"
#include "tile.h"

#include <string.h>

// The n-th plain desert cell of the map: desert terrain, no overlay, walkable.
static bool nth_desert(const Map *m, int n, int *ox, int *oy) {
    int seen = 0;
    for (int y = 0; y < m->height; y++)
        for (int x = 0; x < m->width; x++) {
            const Tile *t = MapGetTile(m, x, y);
            if (t->terrain != TERRAIN_DESERT || t->interactive != INTERACT_NONE) continue;
            if (t->blocks_foot || !t->ground) continue;
            if (seen++ == n) { *ox = x; *oy = y; return true; }
        }
    return false;
}

static void stamp_foe(Map *m, int x, int y) {
    Tile *t = &MAP_TILE(m, x, y);
    t->interactive = INTERACT_FOE;
    TileSetArt(m, t, "wandering_army");
}

TEST default_clear_on_desert_leaves_grass(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "saharia", FIXTURE_SEED));
    ASSERT_FALSE(m->clear_keeps_ground);   // the fixture pack declares no rule
    int x, y;
    ASSERT(nth_desert(m, 0, &x, &y));
    stamp_foe(m, x, y);
    MapClearInteractive(m, x, y);
    const Tile *t = MapGetTile(m, x, y);
    ASSERT_EQ(INTERACT_NONE, t->interactive);
    ASSERT_EQ(TERRAIN_GRASS, t->terrain);
    ASSERT(strstr(TileArt(m, t), "grass") != NULL);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST keeps_ground_clear_on_desert_leaves_sand(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "saharia", FIXTURE_SEED));
    m->clear_keeps_ground = true;
    int x, y;
    ASSERT(nth_desert(m, 1, &x, &y));
    const char *ground_before = TileGround(m, &MAP_TILE(m, x, y));
    ASSERT(strstr(ground_before, "desert") != NULL);
    stamp_foe(m, x, y);
    MapClearInteractive(m, x, y);
    const Tile *t = MapGetTile(m, x, y);
    ASSERT_EQ(INTERACT_NONE, t->interactive);
    ASSERT_EQ(TERRAIN_DESERT, t->terrain);
    ASSERT(strcmp(TileGround(m, t), TileArt(m, t)) == 0);
    ASSERT(strstr(TileArt(m, t), "desert") != NULL);
    ASSERT_FALSE(t->blocks_foot);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST keeps_ground_clear_on_water_stays_water(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "saharia", FIXTURE_SEED));
    m->clear_keeps_ground = true;
    int wx = -1, wy = -1;
    for (int y = 0; y < m->height && wx < 0; y++)
        for (int x = 0; x < m->width; x++)
            if (MapGetTile(m, x, y)->terrain == TERRAIN_WATER) { wx = x; wy = y; break; }
    ASSERT(wx >= 0);
    stamp_foe(m, wx, wy);
    MapClearInteractive(m, wx, wy);
    ASSERT_EQ(TERRAIN_WATER, MapGetTile(m, wx, wy)->terrain);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(unit_map_clear_ground_suite) {
    RUN_TEST(default_clear_on_desert_leaves_grass);
    RUN_TEST(keeps_ground_clear_on_desert_leaves_sand);
    RUN_TEST(keeps_ground_clear_on_water_stays_water);
}
