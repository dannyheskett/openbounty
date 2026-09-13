// Autoplay worldsnap: a capture must restore bit-identically (AP-030..032) --
// Game, Map, Fog, and the world RNG all revert, and two identical worlds
// fingerprint identically. Zero-asset: synthetic Game/Map/Fog built inline.

#include "greatest.h"

#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "worldsnap.h"

typedef struct {
    Game *g;
    Map *m;
    Fog *f;
    WorldSnapshot *snap;
} WsFixture;

static WsFixture ws_make(void) {
    WsFixture fx;
    fx.g = calloc(1, sizeof *fx.g);
    fx.m = calloc(1, sizeof *fx.m);
    fx.f = calloc(1, sizeof *fx.f);
    fx.snap = calloc(1, sizeof *fx.snap);
    fx.m->width = MAP_MAX_W;
    fx.m->height = MAP_MAX_H;
    snprintf(fx.g->position.zone, sizeof fx.g->position.zone, "testzone");
    fx.g->position.x = 7;
    fx.g->position.y = 9;
    fx.g->stats.gold = 1234;
    fx.g->stats.days_left = 500;
    fx.g->stats.leadership_current = 100;
    snprintf(fx.g->army[0].id, sizeof fx.g->army[0].id, "militia");
    fx.g->army[0].count = 20;
    return fx;
}

static void ws_free(WsFixture *fx) {
    worldsnap_release(fx->snap);
    free(fx->snap);
    free(fx->f);
    free(fx->m);
    free(fx->g);
}

TEST worldsnap_restores_bit_identically(void) {
    WsFixture fx = ws_make();
    uint32_t before = worldsnap_fingerprint(fx.g, fx.m, fx.f);
    worldsnap_capture(fx.snap, fx.g, fx.m, fx.f);

    // Mutate everything a rollback must revert.
    fx.g->stats.gold = 99999;
    fx.g->position.x = 1;
    fx.g->army[0].count = 3;
    fx.m->tiles[5][5].terrain = TERRAIN_WATER;
    fx.f->seen[3][3] = true;
    uint32_t mutated = worldsnap_fingerprint(fx.g, fx.m, fx.f);
    ASSERT(mutated != before);

    worldsnap_restore(fx.snap, fx.g, fx.m, fx.f);
    uint32_t after = worldsnap_fingerprint(fx.g, fx.m, fx.f);
    ASSERT_EQ_FMT(before, after, "%u");
    ASSERT_EQ_FMT(1234, fx.g->stats.gold, "%d");
    ASSERT_EQ_FMT(20, fx.g->army[0].count, "%d");
    ws_free(&fx);
    PASS();
}

TEST worldsnap_restores_world_rng(void) {
    WsFixture fx = ws_make();
    uint64_t rng_before = GameRngSnapshot();
    worldsnap_capture(fx.snap, fx.g, fx.m, fx.f);
    GameRngRestore(rng_before ^ 0xDEADBEEFULL);   // perturb
    worldsnap_restore(fx.snap, fx.g, fx.m, fx.f);
    ASSERT_EQ_FMT((unsigned long long)rng_before,
                  (unsigned long long)GameRngSnapshot(), "%llu");
    ws_free(&fx);
    PASS();
}

// A snapshot of a small map restored over a larger live map (a zone change
// between capture and restore) comes back bit-identical to a fresh load of the
// small map: the cells beyond its width x height are zero again.
TEST worldsnap_small_map_over_large_live_map(void) {
    WsFixture fx = ws_make();
    fx.m->width = 20;
    fx.m->height = 10;
    fx.m->tiles[2][3].art = 7;
    uint32_t before = worldsnap_fingerprint(fx.g, fx.m, fx.f);
    worldsnap_capture(fx.snap, fx.g, fx.m, fx.f);
    // The live map becomes a larger one with cells set everywhere.
    fx.m->width = MAP_MAX_W;
    fx.m->height = MAP_MAX_H;
    for (int y = 0; y < MAP_MAX_H; y++)
        for (int x = 0; x < MAP_MAX_W; x++) fx.m->tiles[y][x].terrain = TERRAIN_FOREST;
    worldsnap_restore(fx.snap, fx.g, fx.m, fx.f);
    ASSERT_EQ(20, fx.m->width);
    ASSERT_EQ(10, fx.m->height);
    ASSERT_EQ(0, fx.m->tiles[50][50].terrain);
    ASSERT_EQ(0, fx.m->tiles[5][30].terrain);
    ASSERT_EQ(7, fx.m->tiles[2][3].art);
    ASSERT_EQ_FMT(before, worldsnap_fingerprint(fx.g, fx.m, fx.f), "%u");
    ws_free(&fx);
    PASS();
}

SUITE(autoplay_worldsnap_suite) {
    RUN_TEST(worldsnap_restores_bit_identically);
    RUN_TEST(worldsnap_small_map_over_large_live_map);
    RUN_TEST(worldsnap_restores_world_rng);
}
