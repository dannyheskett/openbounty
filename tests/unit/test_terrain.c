// Terrain / walkability predicate tests.

#include "greatest.h"
#include "adventure.h"
#include "tile.h"
#include "map.h"

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

SUITE(unit_terrain_suite) {
    RUN_TEST(walkable_grass_passes);
    RUN_TEST(walkable_water_blocks_on_foot);
    RUN_TEST(walkable_water_passes_in_boat);
    RUN_TEST(walkable_in_flight_ignores_water);
}
