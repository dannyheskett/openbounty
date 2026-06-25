// tests/autoplay/test_exec_fight.c
//
// Isolation unit tests for the executor COMBAT helpers (exec_fight.c).
// exec_garrison (#6) here; exec_fight gets its tests when combat_policy.c folds in.

#include "greatest.h"

#include "fixtures.h"
#include "exec.h"
#include "recording.h"
#include "combat.h"        // CombatTarget / COMBAT_MODE_FOE
#include "pending.h"       // pending_flow / FLOW_NONE

#define EXEC_SEED 1UL

// exec_fight only acts on a combat the caller provoked: with no pending combat flow
// it is a no-op (false, no record, no fight). The predicted-win and predicted-loss
// branches run against real provoked fights in the recreated SLAY/SIEGE tests.
TEST exec_fight_noop_without_combat_flow(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    pending_flow = FLOW_NONE;
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    ASSERT_FALSE(exec_fight(g, map, &rec));
    ASSERT_EQ_FMT(0, prims.count, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// With no castle the hero owns, the engine refuses to garrison: exec_garrison
// returns false and records nothing (no spurious RA_GARRISON_WEAKEST). The
// weakest-stack scan must run over the boot army without tripping. (The success
// path — garrison into a captured castle — is covered by the recreated SIEGE
// integration test.)
TEST exec_garrison_refused_without_owned_castle(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    ASSERT_FALSE(exec_garrison(g, "no_such_castle", &rec));
    ASSERT_EQ_FMT(0, prims.count, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

SUITE(exec_fight_suite) {
    RUN_TEST(exec_fight_noop_without_combat_flow);
    RUN_TEST(exec_garrison_refused_without_owned_castle);
}
