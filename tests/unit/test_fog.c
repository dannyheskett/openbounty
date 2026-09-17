// Fog of war tests. FogReveal stamps a 5x5 square around (cx, cy),
// clamping at map edges. The radius arg is intentionally ignored.

#include "greatest.h"
#include "fog.h"
#include "map.h"
#include "savegame.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SAVE_PATH "/tmp/openbounty_fog.dat"

TEST init_starts_with_no_tiles_seen(void) {
    Fog f = { 0 };
    ASSERT_FALSE(FogSeen(&f, 0, 0));
    ASSERT_FALSE(FogSeen(&f, 50, 50));
    PASS();
}

TEST reveal_marks_5x5_square(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    int cx = 30, cy = 30;
    FogReveal(f, m, cx, cy, /*radius=*/2);

    // Center revealed.
    ASSERT(FogSeen(f, cx, cy));
    // All cells in -2..+2 around center.
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            ASSERT(FogSeen(f, cx + dx, cy + dy));
        }
    }
    // 3 tiles away -- not revealed.
    ASSERT_FALSE(FogSeen(f, cx + 3, cy));
    ASSERT_FALSE(FogSeen(f, cx, cy + 3));
    ASSERT_FALSE(FogSeen(f, cx - 3, cy - 3));

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST reveal_clamps_at_map_edges(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    // Top-left corner -- should not crash and only stamp in-bounds tiles.
    FogReveal(f, m, 0, 0, 2);
    ASSERT(FogSeen(f, 0, 0));
    ASSERT(FogSeen(f, 1, 1));
    ASSERT(FogSeen(f, 2, 2));
    // Negative coords still report unseen.
    ASSERT_FALSE(FogSeen(f, -1, 0));
    ASSERT_FALSE(FogSeen(f, 0, -1));

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST reveal_radius_arg_is_ignored(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    int cx = 20, cy = 20;
    // Even with radius=99, only the canonical 5x5 box should be stamped.
    FogReveal(f, m, cx, cy, 99);
    ASSERT(FogSeen(f, cx + 2, cy + 2));     // edge of canonical box
    ASSERT_FALSE(FogSeen(f, cx + 3, cy));   // outside canonical box

    fx_free_game_full(res, g, m, f);
    PASS();
}

// FogRevealRadius honours its radius: radius 3 is a 7x7 square.
TEST reveal_radius_honours_the_radius(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    int cx = 20, cy = 20;
    FogRevealRadius(f, m, cx, cy, 3);
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++)
            ASSERT(FogSeen(f, cx + dx, cy + dy));
    ASSERT_FALSE(FogSeen(f, cx + 4, cy));
    ASSERT_FALSE(FogSeen(f, cx, cy - 4));
    fx_free_game_full(res, g, m, f);
    PASS();
}

// The pack's reveal: legacy is the original's 5x5 whatever fog_sight says;
// modern honours fog_sight, so a 7-wide viewport has no unexplored columns.
TEST reveal_for_is_authentic_in_legacy_and_honours_fog_sight_in_modern(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    ASSERT_EQ(3, res->world.fog_sight);          // what both shipped packs declare

    res->render.mode = RENDER_MODE_LEGACY;
    FogRevealFor(res, f, m, 20, 20);
    ASSERT(FogSeen(f, 22, 22));
    ASSERT_FALSE(FogSeen(f, 23, 20));            // still the authentic 5x5

    FogInit(f);
    res->render.mode = RENDER_MODE_MODERN;
    FogRevealFor(res, f, m, 20, 20);
    ASSERT(FogSeen(f, 23, 23));                  // 7x7
    ASSERT_FALSE(FogSeen(f, 24, 20));

    res->render.mode = RENDER_MODE_LEGACY;       // leave the shared fixture as found
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST seen_out_of_bounds_returns_false(void) {
    Fog f = { 0 };
    ASSERT(FogSize(&f, 20, 10));
    FogSet(&f, 19, 9, true);
    ASSERT(FogSeen(&f, 19, 9));
    ASSERT_FALSE(FogSeen(&f, -1, 0));
    ASSERT_FALSE(FogSeen(&f, 0, -1));
    ASSERT_FALSE(FogSeen(&f, 20, 0));
    ASSERT_FALSE(FogSeen(&f, 0, 10));
    FogFree(&f);
    PASS();
}

// No fixed fog size: a 300x300 map's fog holds its far corner, and a copy
// carries it.
TEST fog_has_no_size_limit(void) {
    Map m = { 0 };
    ASSERT(MapAlloc(&m, 300, 300));
    Fog f = { 0 }, c = { 0 };
    FogReveal(&f, &m, 299, 299, 2);
    ASSERT(FogSeen(&f, 299, 299));
    ASSERT(FogSeen(&f, 297, 297));
    ASSERT_FALSE(FogSeen(&f, 296, 296));
    ASSERT(FogCopy(&c, &f));
    ASSERT(FogSeen(&c, 299, 299));
    FogFree(&f); FogFree(&c); MapFree(&m);
    PASS();
}

TEST fog_survives_save_load_round_trip(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    FogReveal(f, m, 10, 10, 2);
    FogReveal(f, m, 40, 25, 2);

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));

    Resources *r2 = fx_load_resources();
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    g2->res = r2;
    GameInit(g2, "Other", 0, 1, NULL);
    FogInit(f2);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));

    // Both reveal centers should still be seen.
    ASSERT(FogSeen(f2, 10, 10));
    ASSERT(FogSeen(f2, 40, 25));
    // And cells 4 tiles away from either should still be unseen.
    ASSERT_FALSE(FogSeen(f2, 16, 10));

    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    fx_free_game_full(r2,  g2, m2, f2);
    PASS();
}

SUITE(unit_fog_suite) {
    RUN_TEST(init_starts_with_no_tiles_seen);
    RUN_TEST(reveal_marks_5x5_square);
    RUN_TEST(reveal_clamps_at_map_edges);
    RUN_TEST(reveal_radius_arg_is_ignored);
    RUN_TEST(reveal_radius_honours_the_radius);
    RUN_TEST(reveal_for_is_authentic_in_legacy_and_honours_fog_sight_in_modern);
    RUN_TEST(seen_out_of_bounds_returns_false);
    RUN_TEST(fog_has_no_size_limit);
    RUN_TEST(fog_survives_save_load_round_trip);
}
