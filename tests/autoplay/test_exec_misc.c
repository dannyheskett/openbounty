// tests/autoplay/test_exec_misc.c
//
// Isolation unit tests for the cross-cutting executor helpers defined in
// primitives.c: exec_spend_week (#18), exec_answer (#16).

#include "greatest.h"

#include "fixtures.h"
#include "exec.h"
#include "recording.h"
#include "pending.h"        // pending_flow / FLOW_NONE
#include "flow_answer.h"    // FlowAnswer / FLOW_ANS_YES

#define EXEC_SEED 1UL

// exec_spend_week banks one week and reports the game still live, recording one
// RA_WAIT_WEEK.
TEST exec_spend_week_banks_a_week(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    bool live = exec_spend_week(g, &rec);

    ASSERT(live);                                       // one week never ends the game
    ASSERT_EQ_FMT(1, prims.count, "%d");
    ASSERT_EQ_FMT((int)REC_ACTION, (int)prims.items[0].kind, "%d");
    ASSERT_EQ_FMT((int)RA_WAIT_WEEK, (int)prims.items[0].action, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_answer is a no-op (false, no record) when there is no pending flow to answer.
TEST exec_answer_requires_a_pending_flow(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    pending_flow = FLOW_NONE;                           // nothing provoked
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    FlowAnswer yes; yes.kind = FLOW_ANS_YES; yes.number = 0;
    ASSERT_FALSE(exec_answer(g, map, yes, &rec));
    ASSERT_EQ_FMT(0, prims.count, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

SUITE(exec_misc_suite) {
    RUN_TEST(exec_spend_week_banks_a_week);
    RUN_TEST(exec_answer_requires_a_pending_flow);
}
