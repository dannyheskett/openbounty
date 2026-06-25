// tests/autoplay/seeds.h
//
// Shared seed list for autoplay correctness tests (multi-seed
// determinism). The autoplay suite was historically seed-1-only, which
// over-fit the tests and let real bugs hide (e.g. the intervention-drop bug,
// latent on seed 1 because no intervention commits there — see the end-to-end
// plan). Every parameterized autoplay test runs over this list via
// greatest's RUN_TESTp.
//
// Seed 1 is the firsthand-verified baseline (objectives floor pinned in
// test_objective_baseline.c). Seeds 2,3,5,8 broaden coverage; at least one of
// them is intended to exercise a committed intervention so the productivity /
// drop properties are actually tested off seed 1.

#ifndef OB_TESTS_AUTOPLAY_SEEDS_H
#define OB_TESTS_AUTOPLAY_SEEDS_H

static const unsigned long AUTOPLAY_SEEDS[] = { 1UL, 2UL, 3UL, 5UL, 8UL };
#define AUTOPLAY_SEED_COUNT \
    ((int)(sizeof(AUTOPLAY_SEEDS) / sizeof(AUTOPLAY_SEEDS[0])))

#endif // OB_TESTS_AUTOPLAY_SEEDS_H
