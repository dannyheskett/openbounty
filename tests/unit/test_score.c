// Score / progress accessor tests.

#include "greatest.h"
#include "game.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

TEST compute_score_starts_at_zero_villains(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g; fx_init_game(&g, res, FIXTURE_SEED);

    // No villains caught yet → 0 of 17.
    ASSERT_EQ(0, GameVillainsCaught(&g));
    // Score may include other terms (gold, leadership). Just sanity-check
    // it's >= 0 for a fresh game.
    ASSERT(GameComputeScore(&g) >= 0);

    resources_free(res); free(res);
    PASS();
}

TEST villains_caught_reflects_state(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g; fx_init_game(&g, res, FIXTURE_SEED);

    g.contract.villains_caught[0] = true;
    g.contract.villains_caught[3] = true;
    g.contract.villains_caught[8] = true;
    ASSERT_EQ(3, GameVillainsCaught(&g));

    g.contract.villains_caught[3] = false;
    ASSERT_EQ(2, GameVillainsCaught(&g));

    resources_free(res); free(res);
    PASS();
}

TEST artifacts_found_reflects_state(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g; fx_init_game(&g, res, FIXTURE_SEED);

    int initial = GameArtifactsFound(&g);
    // Mark the first two artifacts as found.
    g.artifacts.found[0] = true;
    g.artifacts.found[1] = true;
    ASSERT_EQ(initial + 2, GameArtifactsFound(&g));

    resources_free(res); free(res);
    PASS();
}

SUITE(score_suite) {
    RUN_TEST(compute_score_starts_at_zero_villains);
    RUN_TEST(villains_caught_reflects_state);
    RUN_TEST(artifacts_found_reflects_state);
}
