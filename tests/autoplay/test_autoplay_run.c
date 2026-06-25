// tests/autoplay/test_autoplay_run.c
//
// End-to-end correctness tests for the unified autoplay stack
// (planner() -> executor -> engine), exercised headlessly on a real seed-1 world:
//
//   1. The planner reaches a definitive verdict and records a real run (oracle pin).
//   2. THE VISIBLE-MIGRATION GUARANTEE: dumb-replaying the planner's recorded
//      primitive sequence on a fresh boot reproduces the planner's terminal world
//      byte-for-byte. This is the headless-checkable core of the replay the visible
//      shell drives (plan_exec_step), so it proves headless == visible for a seed
//      without a window — the determinism the old per-step boundary_fp asserted.

#include "greatest.h"

#include <string.h>
#include <stdint.h>
#include <unistd.h>      // dup / dup2 / STDOUT_FILENO — mute the planner trace
#include <fcntl.h>       // open / O_WRONLY

#include "fixtures.h"
#include "planner.h"     // planner() / PrimRun / primrun_free
#include "plan.h"        // Plan / PlanExecutor / plan_exec_begin / plan_exec_step
#include "worldsnap.h"   // worldsnap_capture / restore / fingerprint
#include "autoplay.h"    // AUTOPLAY_VERDICT_*

#define RUN_SEED 1UL

// Run the planner with its always-on stdout decision trace muted, so the test
// output stays clean. (planner() prints "planner:"/exec DECLINE lines via printf.)
static bool planner_quiet(Game *g, Map *map, Fog *fog, const Resources *res,
                          PrimRun *run) {
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    int dn = open("/dev/null", O_WRONLY);
    if (dn >= 0) dup2(dn, STDOUT_FILENO);
    bool ok = planner(g, map, fog, res, /*zone_scope=*/-1, run);
    fflush(stdout);
    if (saved >= 0) { dup2(saved, STDOUT_FILENO); close(saved); }
    if (dn >= 0) close(dn);
    return ok;
}

// The planner runs to a definitive verdict on seed 1, plays a substantially
// complete game, and records a non-empty replayable sequence. (A pinned oracle:
// any regression that STALLS the run — the hero stranded/flailing, far short of the
// objectives a healthy playthrough reaches — or empties the recording trips here.)
// The exact final count is intentionally NOT pinned. Autoplay's job is to PLAY the
// game well, not to maximise a score, and the greedy planner's trajectory shifts
// with any movement/combat decision change, so the last one or two objectives (e.g.
// a too-strong villain castle gated by the army-build-order, a separate limitation)
// come and go between runs. We assert a HEALTHY FLOOR, not a specific score: seed 1
// completes ~105-106 of 106; anything under 100 means the hero stalled.
#define SEED1_HEALTHY_FLOOR 100
TEST planner_seed1_runs_to_a_verdict(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", RUN_SEED));

    PrimRun run; memset(&run, 0, sizeof run);
    ASSERT(planner_quiet(g, map, fog, res, &run));

    // A definitive verdict (the run terminated, did not hang).
    ASSERT(run.verdict == AUTOPLAY_VERDICT_CLEARED ||
           run.verdict == AUTOPLAY_VERDICT_PARTIAL);
    ASSERT(run.obj_done > 0);                 // it carried out real objectives
    ASSERT(run.obj_done <= run.obj_total);
    ASSERT(run.obj_done >= SEED1_HEALTHY_FLOOR);  // a healthy playthrough, not a stall
    ASSERT(run.rec.count > 0);                // a real, replayable recording

    primrun_free(&run);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// Replaying planner()'s recorded prims on a fresh boot reaches the SAME terminal
// world (Game + Map + world-RNG fingerprint) the planner left — no planner, no nav,
// no prediction at replay time, just the dumb applier. This is the migration's
// correctness guarantee (visible replays this exact recording).
TEST replay_reproduces_planner_terminal(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", RUN_SEED));

    // Snapshot the boot world, run the planner forward, capture its terminal print.
    WorldSnapshot boot; worldsnap_capture(&boot, g, map, fog);
    PrimRun run; memset(&run, 0, sizeof run);
    ASSERT(planner_quiet(g, map, fog, res, &run));
    uint64_t planner_fp = worldsnap_fingerprint(g, map);

    // Back to boot, then DUMB-replay the recording (we assert the end state
    // instead). The replay touches no planner.
    worldsnap_restore(&boot, g, map, fog);
    Plan p; memset(&p, 0, sizeof p);
    p.rec = run.rec; p.combat = run.combats;
    PlanExecutor ex; plan_exec_begin(&ex, &p, /*animate=*/NULL, /*present=*/NULL, NULL);
    int guard = 0;
    while (plan_exec_step(&ex, g, map, fog, res) == PLAN_EXEC_RUNNING) {
        if (++guard > 5000000) FAILm("replay did not terminate");
    }
    uint64_t replay_fp = worldsnap_fingerprint(g, map);

    ASSERT_EQ_FMT((unsigned long long)planner_fp, (unsigned long long)replay_fp, "%llu");

    primrun_free(&run);          // frees run.rec/combats (shared into p, no longer used)
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// Boot `seed`, run the planner, and verify its recording replays to the same terminal
// world. Returns true on a clean run-to-verdict + byte-identical replay.
static bool seed_replays_deterministically(unsigned long seed) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    if (!fx_init_game_full(&res, &g, &map, &fog, "continentia", seed)) return false;
    WorldSnapshot boot; worldsnap_capture(&boot, g, map, fog);
    PrimRun run; memset(&run, 0, sizeof run);
    bool ok = planner_quiet(g, map, fog, res, &run);
    uint64_t planner_fp = worldsnap_fingerprint(g, map);
    worldsnap_restore(&boot, g, map, fog);
    if (ok) {
        Plan p; memset(&p, 0, sizeof p);
        p.rec = run.rec; p.combat = run.combats;
        PlanExecutor ex; plan_exec_begin(&ex, &p, NULL, NULL, NULL);
        int guard = 0;
        while (plan_exec_step(&ex, g, map, fog, res) == PLAN_EXEC_RUNNING &&
               ++guard < 5000000) { }
        ok = (planner_fp == worldsnap_fingerprint(g, map));
    }
    primrun_free(&run);
    fx_free_game_full(res, g, map, fog);
    return ok;
}

// Determinism holds beyond seed 1: across several seeds the planner runs to a verdict
// and its recording replays to the identical terminal world (no seed-specific divergence).
TEST replay_determinism_holds_across_seeds(void) {
    const unsigned long seeds[] = { 2UL, 3UL, 7UL, 42UL };
    for (size_t i = 0; i < sizeof seeds / sizeof seeds[0]; i++)
        ASSERT(seed_replays_deterministically(seeds[i]));
    PASS();
}

SUITE(autoplay_run_suite) {
    RUN_TEST(planner_seed1_runs_to_a_verdict);
    RUN_TEST(replay_reproduces_planner_terminal);
    RUN_TEST(replay_determinism_holds_across_seeds);
}
