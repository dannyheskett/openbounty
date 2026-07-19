// SaveGameWrite / SaveGameRead round-trip tests.

#include "greatest.h"
#include "savegame.h"
#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define ASSET_PATH "assets/kings-bounty/game.json"
#define SAVE_PATH  "/tmp/openbounty_unit_save.dat"

static int populate(Resources *res, Game *g, Map *m, Fog *f, unsigned long seed) {
    if (!resources_load(res, ASSET_PATH)) return 0;
    g->res = res;
    g->seed = seed;
    GameInit(g, "Test", 0, 1, NULL);
    FogInit(f);
    if (!MapLoadZoneWithPlacements(m, res, "continentia", g)) return 0;
    return 1;
}

TEST roundtrip_preserves_position(void) {
    Resources *res1 = calloc(1, sizeof *res1);
    Game *g1 = calloc(1, sizeof *g1);
    Map  *m1 = calloc(1, sizeof *m1);
    Fog  *f1 = calloc(1, sizeof *f1);
    ASSERT(populate(res1, g1, m1, f1, 42));
    g1->position.x = 25;
    g1->position.y = 50;

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g1, m1, f1));

    // Fresh structs.
    Resources *res2 = calloc(1, sizeof *res2);
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    ASSERT(resources_load(res2, ASSET_PATH));
    g2->res = res2;
    GameInit(g2, "Default", 0, 1, NULL);
    FogInit(f2);

    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));
    ASSERT_EQ(25, g2->position.x);
    ASSERT_EQ(50, g2->position.y);

    unlink(SAVE_PATH);
    resources_free(res1); free(res1); free(g1); free(m1); free(f1);
    resources_free(res2); free(res2); free(g2); free(m2); free(f2);
    PASS();
}

TEST roundtrip_preserves_seed(void) {
    Resources *res1 = calloc(1, sizeof *res1);
    Game *g1 = calloc(1, sizeof *g1);
    Map  *m1 = calloc(1, sizeof *m1);
    Fog  *f1 = calloc(1, sizeof *f1);
    ASSERT(populate(res1, g1, m1, f1, 12345));
    unsigned long expected_seed = g1->seed;

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g1, m1, f1));

    Resources *res2 = calloc(1, sizeof *res2);
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    ASSERT(resources_load(res2, ASSET_PATH));
    g2->res = res2;
    GameInit(g2, "Default", 0, 1, NULL);
    FogInit(f2);

    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));
    ASSERT_EQ(expected_seed, g2->seed);

    unlink(SAVE_PATH);
    resources_free(res1); free(res1); free(g1); free(m1); free(f1);
    resources_free(res2); free(res2); free(g2); free(m2); free(f2);
    PASS();
}

SUITE(e2e_save_suite) {
    RUN_TEST(roundtrip_preserves_position);
    RUN_TEST(roundtrip_preserves_seed);
}
