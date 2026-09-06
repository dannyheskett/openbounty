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
    // navmap, or orb depending on salt -- but it MUST be interactive.
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

TEST terrain_art_is_bare_without_a_tile_set(void) {
    // A zone with no tile_set draws the shared art/tiles/ names, so a pack
    // that predates the key loads byte for byte as before.
    Resources *res = calloc(1, sizeof *res);
    Map       *m   = calloc(1, sizeof *m);
    ASSERT(res && m);
    ASSERT(resources_load(res, ASSET_PATH));
    ASSERT(MapLoadZone(m, res, "continentia"));
    ASSERT_STR_EQ("", m->tile_set);
    bool saw_plain = false;
    for (int y = 0; y < m->height; y++)
        for (int x = 0; x < m->width; x++) {
            const Tile *t = MapGetTile(m, x, y);
            ASSERT(strchr(t->art, '/') == NULL);
            if (strcmp(t->art, "grass") == 0) saw_plain = true;
        }
    ASSERT(saw_plain);
    char buf[TILE_ART_NAME_LEN];
    ASSERT_STR_EQ("water", MapTerrainArt(m, "water", buf, sizeof buf));
    resources_free(res); free(res); free(m);
    PASS();
}

TEST terrain_art_lives_under_the_zone_tile_set(void) {
    // With "tile_set": "x" every terrain code resolves as x/<art>; object
    // tiles stamped from the zone lists keep their bare names; and the
    // tiles the engine writes later (cleared objects, bridges) follow suit.
    Resources *res = calloc(1, sizeof *res);
    Map       *m   = calloc(1, sizeof *m);
    ASSERT(res && m);
    ASSERT(resources_load(res, ASSET_PATH));
    ResZone *z = (ResZone *)resources_zone_by_id(res, "continentia");
    ASSERT(z);
    strcpy(z->tile_set, "continentia");
    ASSERT(MapLoadZone(m, res, "continentia"));
    ASSERT_STR_EQ("continentia", m->tile_set);
    int prefixed = 0, objects = 0;
    for (int y = 0; y < m->height; y++)
        for (int x = 0; x < m->width; x++) {
            const Tile *t = MapGetTile(m, x, y);
            if (t->interactive == INTERACT_NONE && !t->blocks_foot) {
                ASSERT(strncmp(t->art, "continentia/", 12) == 0);
                prefixed++;
            } else if (t->interactive != INTERACT_NONE) {
                ASSERT(strchr(t->art, '/') == NULL);
                objects++;
            }
        }
    ASSERT(prefixed > 1000);
    ASSERT(objects > 10);
    char buf[TILE_ART_NAME_LEN];
    ASSERT_STR_EQ("continentia/water", MapTerrainArt(m, "water", buf, sizeof buf));
    // Clearing an object reverts to the set's grass, not the shared one.
    int ox = -1, oy = -1;
    for (int y = 0; y < m->height && ox < 0; y++)
        for (int x = 0; x < m->width; x++)
            if (MapGetTile(m, x, y)->interactive != INTERACT_NONE &&
                MapGetTile(m, x, y)->terrain != TERRAIN_WATER) { ox = x; oy = y; break; }
    ASSERT(ox >= 0);
    MapClearInteractive(m, ox, oy);
    ASSERT_STR_EQ("continentia/grass", MapGetTile(m, ox, oy)->art);
    resources_free(res); free(res); free(m);
    PASS();
}

TEST town_stamps_its_own_art_or_the_shared_tile(void) {
    Resources *res = calloc(1, sizeof *res);
    Map       *m   = calloc(1, sizeof *m);
    ASSERT(res && m);
    ASSERT(resources_load(res, ASSET_PATH));
    ASSERT(MapLoadZone(m, res, "continentia"));
    int tx = -1, ty = -1;
    for (int y = 0; y < m->height && tx < 0; y++)
        for (int x = 0; x < m->width; x++)
            if (MapGetTile(m, x, y)->interactive == INTERACT_TOWN) { tx = x; ty = y; break; }
    ASSERT(tx >= 0);
    ASSERT_STR_EQ("town", MapGetTile(m, tx, ty)->art);
    const char *id = MapGetTile(m, tx, ty)->id;
    for (int i = 0; i < res->town_count; i++)
        if (strcmp(res->towns[i].id, id) == 0) strcpy(res->towns[i].art, "town_x");
    ASSERT(MapLoadZone(m, res, "continentia"));
    ASSERT_STR_EQ("town_x", MapGetTile(m, tx, ty)->art);
    resources_free(res); free(res); free(m);
    PASS();
}

SUITE(unit_map_suite) {
    RUN_TEST(terrain_art_is_bare_without_a_tile_set);
    RUN_TEST(terrain_art_lives_under_the_zone_tile_set);
    RUN_TEST(town_stamps_its_own_art_or_the_shared_tile);
    RUN_TEST(in_bounds_corners_and_outside);
    RUN_TEST(load_continentia_succeeds);
    RUN_TEST(get_tile_at_known_chest_position);
}
