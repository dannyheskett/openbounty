// Tests for the combat RNG (combat_rand) and combat seeding.

#include "greatest.h"
#include "combat_internal.h"
#include "fixtures.h"

#include <string.h>
#include <stdlib.h>

TEST kb_rand_deterministic_for_same_seed(void) {
    Combat a = { 0 };
    Combat b = { 0 };
    a.rng_state = 12345ULL;
    b.rng_state = 12345ULL;
    for (int i = 0; i < 20; i++) {
        ASSERT_EQ(combat_rand(&a, 0, 100), combat_rand(&b, 0, 100));
    }
    PASS();
}

TEST kb_rand_respects_range(void) {
    Combat c = { 0 };
    c.rng_state = 7ULL;
    for (int i = 0; i < 200; i++) {
        int v = combat_rand(&c, 5, 10);
        ASSERT(v >= 5);
        ASSERT(v <= 10);
    }
    PASS();
}

TEST kb_rand_actually_varies(void) {
    // A broken combat_rand stuck at min would still satisfy the
    // determinism + range tests above. Assert we see ≥3 distinct
    // values across 50 calls in a 1..10 range.
    Combat c = { 0 };
    c.rng_state = 99ULL;
    int seen[11] = { 0 };
    int distinct = 0;
    for (int i = 0; i < 50; i++) {
        int v = combat_rand(&c, 1, 10);
        if (!seen[v]) { seen[v] = 1; distinct++; }
    }
    ASSERT(distinct >= 3);
    PASS();
}

TEST kb_rand_min_equals_max_returns_min(void) {
    Combat c = { 0 };
    c.rng_state = 1ULL;
    ASSERT_EQ(7, combat_rand(&c, 7, 7));
    ASSERT_EQ(0, combat_rand(&c, 0, 0));
    PASS();
}

TEST combat_seed_rng_same_inputs_same_state(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g; fx_init_game(&g, res, FIXTURE_SEED);

    Combat a = { 0 };
    Combat b = { 0 };
    CombatTarget t = { 0 };
    t.name = "test_foe";

    combat_seed_rng(&a, &g, COMBAT_MODE_FOE, &t);
    combat_seed_rng(&b, &g, COMBAT_MODE_FOE, &t);
    ASSERT(a.rng_state == b.rng_state);
    ASSERT(a.rng_state != 0);

    // Different mode → different state.
    combat_seed_rng(&b, &g, COMBAT_MODE_CASTLE, &t);
    ASSERT(a.rng_state != b.rng_state);

    resources_free(res); free(res);
    PASS();
}

SUITE(combat_rng_suite) {
    RUN_TEST(kb_rand_deterministic_for_same_seed);
    RUN_TEST(kb_rand_respects_range);
    RUN_TEST(kb_rand_actually_varies);
    RUN_TEST(kb_rand_min_equals_max_returns_min);
    RUN_TEST(combat_seed_rng_same_inputs_same_state);
}
