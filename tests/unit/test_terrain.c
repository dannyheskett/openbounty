// Terrain / walkability predicate tests.

#include "greatest.h"
#include "adventure.h"
#include "tile.h"
#include "map.h"
#include "game.h"
#include "spells_adventure.h"

#include <stdlib.h>

#include <string.h>

static Tile make_tile(Terrain t, bool blocks) {
    Tile out;
    memset(&out, 0, sizeof out);
    out.terrain = t;
    out.blocks_foot = blocks;
    out.boat_spawn_x = -1;
    out.boat_spawn_y = -1;
    return out;
}

TEST walkable_grass_passes(void) {
    Tile t = make_tile(TERRAIN_GRASS, false);
    ASSERT(adventure_walkable_on_foot(&t));
    PASS();
}

TEST walkable_water_blocks_on_foot(void) {
    Tile t = make_tile(TERRAIN_WATER, false);
    ASSERT_FALSE(adventure_walkable_on_foot(&t));
    PASS();
}

TEST walkable_water_passes_in_boat(void) {
    Tile t = make_tile(TERRAIN_WATER, false);
    ASSERT(adventure_walkable_in_boat(&t));
    PASS();
}

TEST walkable_in_flight_ignores_water(void) {
    Tile t = make_tile(TERRAIN_WATER, false);
    ASSERT(adventure_walkable_in_flight(&t));
    PASS();
}

// ---- rivers ---------------------------------------------------------------------

TEST river_art_and_name(void) {
    ASSERT_EQ(TERRAIN_RIVER, TerrainFromArt("river_ns"));
    ASSERT_EQ(TERRAIN_RIVER, TerrainFromArt("river_c_se"));
    ASSERT_EQ(TERRAIN_GRASS, TerrainFromArt("road_ns"));      // a road stays grass
    ASSERT_STR_EQ("river", TerrainName(TERRAIN_RIVER));
    PASS();
}

TEST river_blocks_foot_and_boat_but_not_flight(void) {
    Tile t = make_tile(TERRAIN_RIVER, false);
    ASSERT_FALSE(adventure_walkable_on_foot(&t));
    ASSERT_FALSE(adventure_walkable_in_boat(&t));             // boats stay on the sea
    ASSERT(adventure_walkable_in_flight(&t));
    PASS();
}

TEST bridge_spell_crosses_a_river(void) {
    // A strip of land with a one-tile river at x=2 and land beyond it.
    Map *m = calloc(1, sizeof *m);
    Game *g = calloc(1, sizeof *g);
    ASSERT(m && g);
    ASSERT(MapAlloc(m, 5, 1));
    for (int x = 0; x < 5; x++) MAP_TILE(m, x, 0) = make_tile(TERRAIN_GRASS, false);
    MAP_TILE(m, 2, 0) = make_tile(TERRAIN_RIVER, false);
    g->position.x = 1; g->position.y = 0;
    ASSERT_EQ(1, try_build_bridge(g, m, 1, 0));               // one river tile, then land stops it
    ASSERT(MAP_TILE(m, 2, 0).is_bridge);
    ASSERT(adventure_walkable_on_foot(&MAP_TILE(m, 2, 0)));
    ASSERT_STR_EQ("bridge_river_ew", TileArt(m, &MAP_TILE(m, 2, 0)));   // the river bridge, not the sea's
    ASSERT_FALSE(MAP_TILE(m, 3, 0).is_bridge);
    GameFree(g); free(g); MapFree(m); free(m);
    PASS();
}

SUITE(unit_terrain_suite) {
    RUN_TEST(walkable_grass_passes);
    RUN_TEST(walkable_water_blocks_on_foot);
    RUN_TEST(walkable_water_passes_in_boat);
    RUN_TEST(walkable_in_flight_ignores_water);
    RUN_TEST(river_art_and_name);
    RUN_TEST(river_blocks_foot_and_boat_but_not_flight);
    RUN_TEST(bridge_spell_crosses_a_river);
}
