// state_build_snapshot tests: determinism + structure.
// Note: these rely on GameInit honoring a pre-set seed, which is what makes
// the snapshots reproducible.

#include "greatest.h"
#include "state_serialize.h"
#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>

#define ASSET_PATH "assets/kings-bounty/game.json"

static int build(Resources *res, Game *g, Map *m, Fog *f, unsigned long seed) {
    if (!resources_load(res, ASSET_PATH)) return 0;
    g->res = res;
    g->seed = seed;
    GameInit(g, "Test", 0, 1, NULL);
    FogInit(f);
    if (!MapLoadZoneWithPlacements(m, res, "continentia", g)) return 0;
    return 1;
}

static char *snapshot_str(Game *g, Map *m, Fog *f) {
    cJSON *snap = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    if (!snap) return NULL;
    char *out = cJSON_PrintUnformatted(snap);
    cJSON_Delete(snap);
    return out;
}

TEST snapshot_is_deterministic_for_same_seed(void) {
    Resources *r1 = calloc(1, sizeof *r1);
    Game *g1 = calloc(1, sizeof *g1);
    Map  *m1 = calloc(1, sizeof *m1);
    Fog  *f1 = calloc(1, sizeof *f1);
    ASSERT(build(r1, g1, m1, f1, 42));
    char *s1 = snapshot_str(g1, m1, f1);
    ASSERT(s1);

    Resources *r2 = calloc(1, sizeof *r2);
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    ASSERT(build(r2, g2, m2, f2, 42));
    char *s2 = snapshot_str(g2, m2, f2);
    ASSERT(s2);

    ASSERT_STR_EQ(s1, s2);

    free(s1); free(s2);
    resources_free(r1); free(r1); free(g1); free(m1); free(f1);
    resources_free(r2); free(r2); free(g2); free(m2); free(f2);
    PASS();
}

TEST snapshot_differs_for_different_seed(void) {
    Resources *r1 = calloc(1, sizeof *r1);
    Game *g1 = calloc(1, sizeof *g1);
    Map  *m1 = calloc(1, sizeof *m1);
    Fog  *f1 = calloc(1, sizeof *f1);
    ASSERT(build(r1, g1, m1, f1, 42));
    char *s1 = snapshot_str(g1, m1, f1);

    Resources *r2 = calloc(1, sizeof *r2);
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    ASSERT(build(r2, g2, m2, f2, 99));
    char *s2 = snapshot_str(g2, m2, f2);

    // Different seeds should produce different worlds (scepter location,
    // dwellings, etc.).
    ASSERT(strcmp(s1, s2) != 0);

    free(s1); free(s2);
    resources_free(r1); free(r1); free(g1); free(m1); free(f1);
    resources_free(r2); free(r2); free(g2); free(m2); free(f2);
    PASS();
}

TEST snapshot_includes_character_block(void) {
    Resources *r = calloc(1, sizeof *r);
    Game *g = calloc(1, sizeof *g);
    Map  *m = calloc(1, sizeof *m);
    Fog  *f = calloc(1, sizeof *f);
    ASSERT(build(r, g, m, f, 42));

    cJSON *snap = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    ASSERT(snap);
    cJSON *ch = cJSON_GetObjectItem(snap, "character");
    ASSERT(ch != NULL);
    cJSON *nm = cJSON_GetObjectItem(ch, "name");
    ASSERT(nm && cJSON_IsString(nm));
    ASSERT_STR_EQ("Test", nm->valuestring);
    cJSON_Delete(snap);

    resources_free(r); free(r); free(g); free(m); free(f);
    PASS();
}

SUITE(unit_state_suite) {
    RUN_TEST(snapshot_is_deterministic_for_same_seed);
    RUN_TEST(snapshot_differs_for_different_seed);
    RUN_TEST(snapshot_includes_character_block);
}
