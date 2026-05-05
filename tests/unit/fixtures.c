// Shared test fixtures.

#include "fixtures.h"
#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The test suite uses a single global pack opened lazily. Tests don't
// run concurrently, so a singleton is fine. The pack is closed on
// process exit (atexit).
static Pack *s_test_pack = NULL;

static void close_test_pack(void) {
    pack_stack_clear();
    s_test_pack = NULL;
}

static bool ensure_test_pack(void) {
    if (s_test_pack) return true;
    s_test_pack = pack_open(FIXTURE_PACK_DIR);
    if (!s_test_pack) return false;
    pack_stack_push(s_test_pack);
    atexit(close_test_pack);
    return true;
}

Resources *fx_load_resources(void) {
    if (!ensure_test_pack()) return NULL;
    Resources *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    if (!resources_load(r, FIXTURE_MANIFEST)) {
        free(r);
        return NULL;
    }
    return r;
}

void fx_init_game(Game *g, Resources *res, unsigned long seed) {
    if (!g || !res) return;
    memset(g, 0, sizeof *g);
    g->res  = res;
    g->seed = seed ? seed : FIXTURE_SEED;
    GameInit(g, "Test", /*pclass=*/0, /*difficulty=*/1, NULL);
}

bool fx_init_game_full(Resources **out_res, Game **out_game,
                       Map **out_map, Fog **out_fog,
                       const char *zone, unsigned long seed) {
    Resources *res = fx_load_resources();
    if (!res) return false;
    Game *game = calloc(1, sizeof *game);
    Map  *map  = calloc(1, sizeof *map);
    Fog  *fog  = calloc(1, sizeof *fog);
    if (!game || !map || !fog) {
        free(game); free(map); free(fog);
        resources_free(res); free(res);
        return false;
    }
    fx_init_game(game, res, seed);
    FogInit(fog);
    const char *zid = (zone && zone[0]) ? zone : res->world.starting_zone;
    if (!MapLoadZoneWithPlacements(map, res, zid, game)) {
        free(game); free(map); free(fog);
        resources_free(res); free(res);
        return false;
    }
    if (out_res)  *out_res  = res;
    if (out_game) *out_game = game;
    if (out_map)  *out_map  = map;
    if (out_fog)  *out_fog  = fog;
    return true;
}

void fx_free_game_full(Resources *res, Game *game, Map *map, Fog *fog) {
    free(fog);
    free(map);
    free(game);
    if (res) {
        resources_free(res);
        free(res);
    }
}
