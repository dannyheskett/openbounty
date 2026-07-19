// Save-format versioning test. Loads tests/fixtures/save_v1.dat -- a
// known-good save committed alongside this test -- and asserts the
// round-tripped state matches the values that fixture was built with.
// Detects accidental save-format breaks (rename a field, drop a slot,
// change endianness) without requiring an external regression.
//
// To regenerate after an intentional format change, write a one-shot that
// builds the fixture game and calls SaveGameWrite, then commit the result.

#include "greatest.h"
#include "savegame.h"
#include "fixtures.h"
#include "game.h"

#include <stdlib.h>
#include <string.h>

#define FIXTURE_PATH "tests/fixtures/save_v1.dat"

TEST load_fixture_state_matches_minted_values(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    Game *g = calloc(1, sizeof *g);
    Map  *m = calloc(1, sizeof *m);
    Fog  *f = calloc(1, sizeof *f);
    g->res = r;
    GameInit(g, "TmpHero", 0, 1, NULL);
    FogInit(f);

    SaveResult sr = SaveGameRead(FIXTURE_PATH, g, m, f);
    ASSERT_EQ(SAVE_OK, sr);

    // Values match what the fixture was minted with (seed, stats, position).
    ASSERT_EQ(424242UL, (unsigned long)g->seed);
    ASSERT_EQ(12345, g->stats.gold);
    ASSERT_EQ(17, g->position.x);
    ASSERT_EQ(33, g->position.y);
    ASSERT_STR_EQ("continentia", g->position.zone);
    ASSERT_EQ(2, g->consumed_count);
    ASSERT(g->contract.villains_caught[0]);
    ASSERT(g->contract.villains_caught[7]);
    ASSERT(g->contract.villains_caught[16]);
    ASSERT_FALSE(g->contract.villains_caught[1]);
    ASSERT_FALSE(g->contract.villains_caught[8]);

    fx_free_game_full(r, g, m, f);
    PASS();
}

SUITE(regression_save_fixture_suite) {
    RUN_TEST(load_fixture_state_matches_minted_values);
}
