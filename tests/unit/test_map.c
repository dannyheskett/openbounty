// Map predicate + zone-load tests. Loads continentia from the real
// game.json so tests cover the full asset pipeline. The Resources
// + Map + Game live on the stack inside each test (no globals
// shared between tests).

#include "greatest.h"
#include "map.h"
#include "tile.h"
#include "game.h"
#include "resources.h"

#include <string.h>
#include <stdlib.h>

#define ASSET_PATH "assets/kings-bounty/game.json"

TEST in_bounds_corners_and_outside(void) {
    Map m = { .width = 64, .height = 64 };
    ASSERT(MapInBounds(&m, 0, 0));
    ASSERT(MapInBounds(&m, 63, 63));
    ASSERT_FALSE(MapInBounds(&m, -1, 0));
    ASSERT_FALSE(MapInBounds(&m, 64, 0));
    ASSERT_FALSE(MapInBounds(&m, 0, -1));
    ASSERT_FALSE(MapInBounds(&m, 0, 64));
    PASS();
}

TEST load_continentia_succeeds(void) {
    Resources *res = calloc(1, sizeof *res);
    Game      *g   = calloc(1, sizeof *g);
    Map       *m   = calloc(1, sizeof *m);
    ASSERT(res && g && m);

    ASSERT(resources_load(res, ASSET_PATH));
    g->res = res;
    g->seed = 42;
    GameInit(g, "Test", 0, 1, NULL);

    ASSERT(MapLoadZoneWithPlacements(m, res, "continentia", g));
    ASSERT_EQ(64, m->width);
    ASSERT_EQ(64, m->height);

    resources_free(res);
    free(res);
    free(g);
    free(m);
    PASS();
}

TEST get_tile_at_known_chest_position(void) {
    // (3, 59) is the "Treasure Island" chest slot in continentia (after
    // the LAND.ORG-driven port). Verify the loader stamps an interactive
    // overlay there. The tile may end up as a chest, dwelling, artifact,
    // navmap, or orb depending on salt — but it MUST be interactive.
    Resources *res = calloc(1, sizeof *res);
    Game      *g   = calloc(1, sizeof *g);
    Map       *m   = calloc(1, sizeof *m);
    ASSERT(res && g && m);

    ASSERT(resources_load(res, ASSET_PATH));
    g->res = res;
    g->seed = 42;
    GameInit(g, "Test", 0, 1, NULL);
    ASSERT(MapLoadZoneWithPlacements(m, res, "continentia", g));

    const Tile *t = MapGetTile(m, 3, 59);
    ASSERT(t != NULL);
    // Whatever salt rolled the slot into, it shouldn't be plain grass.
    ASSERT(t->interactive != INTERACT_NONE);

    resources_free(res);
    free(res);
    free(g);
    free(m);
    PASS();
}

SUITE(unit_map_suite) {
    RUN_TEST(in_bounds_corners_and_outside);
    RUN_TEST(load_continentia_succeeds);
    RUN_TEST(get_tile_at_known_chest_position);
}
