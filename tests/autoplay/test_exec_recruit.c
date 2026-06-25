// tests/autoplay/test_exec_recruit.c
//
// Isolation unit tests for the executor RECRUIT leaf helpers (exec_recruit.c):
// exec_recruit_sources (#9) and exec_dismiss (#10).

#include "greatest.h"

#include <string.h>

#include "fixtures.h"
#include "exec.h"
#include "recording.h"
#include "recruit.h"        // RecruitSource / army_stack_count

#define EXEC_SEED 1UL

// exec_recruit_sources enumerates well-formed sources deterministically and honours
// the cap.
TEST exec_recruit_sources_enumerates(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    RecruitSource buf[64];
    int n = exec_recruit_sources(g, buf, 64);
    ASSERT(n >= 0 && n <= 64);
    for (int i = 0; i < n; i++)
        ASSERT(buf[i].troop_id[0] != '\0');             // every source names a troop

    // Deterministic: a second identical call yields the same count.
    RecruitSource buf2[64];
    ASSERT_EQ_FMT(n, exec_recruit_sources(g, buf2, 64), "%d");

    // Cap honoured.
    ASSERT_EQ_FMT(0, exec_recruit_sources(g, buf, 0), "%d");

    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_dismiss removes one held stack and records RA_DISMISS_TYPE keyed by its id.
TEST exec_dismiss_removes_a_stack(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    int n0 = army_stack_count(g);
    if (n0 < 2) {   // need a spare stack: the engine refuses to empty the army
        fx_free_game_full(res, g, map, fog);
        SKIPm("boot army has fewer than 2 stacks on this seed");
    }

    char id0[24] = {0};   // RA_DISMISS_TYPE.act_id is [24]; boot army ids fit
    {
        const char *aid = g->army[0].id;
        size_t m = strlen(aid);
        if (m >= sizeof id0) m = sizeof id0 - 1;
        memcpy(id0, aid, m);
    }

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    ASSERT(exec_dismiss(g, 0, &rec));
    ASSERT_EQ_FMT(n0 - 1, army_stack_count(g), "%d");
    ASSERT_EQ_FMT(1, prims.count, "%d");
    ASSERT_EQ_FMT((int)REC_ACTION, (int)prims.items[0].kind, "%d");
    ASSERT_EQ_FMT((int)RA_DISMISS_TYPE, (int)prims.items[0].action, "%d");
    ASSERT_STR_EQ(id0, prims.items[0].act_id);

    // Guard: an out-of-range slot dismisses nothing and records nothing.
    RecBuf prims2 = {0}; CombatRecList combats2 = {0};
    RecSink rec2 = { &prims2, &combats2 };
    ASSERT_FALSE(exec_dismiss(g, 99, &rec2));
    ASSERT_EQ_FMT(0, prims2.count, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    recbuf_free(&prims2); combatreclist_free(&combats2);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_recruit with an empty target is trivially satisfied (no dismiss, no buy).
TEST exec_recruit_empty_target_is_trivial(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    ArmyTarget target; memset(&target, 0, sizeof target);   // n == 0
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    RecruitRequest req = { .mode = RECRUIT_APPLY_TARGET, .target = &target };
    ASSERT(exec_recruit(g, map, fog, res, &req, &rec));
    ASSERT_EQ_FMT(0, prims.count, "%d");                     // nothing to do

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_recruit with a target == the held army needs no dismiss and no buy.
TEST exec_recruit_already_satisfied(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    ArmyTarget target; memset(&target, 0, sizeof target);
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        char *dst = target.slot[target.n].id;
        size_t cap = sizeof target.slot[target.n].id;
        size_t m = strlen(g->army[s].id);
        if (m >= cap) m = cap - 1;
        memcpy(dst, g->army[s].id, m); dst[m] = '\0';
        target.slot[target.n].count = g->army[s].count;
        target.n++;
    }
    ASSERT(target.n > 0);                                    // boot army is non-empty

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };
    RecruitRequest req = { .mode = RECRUIT_APPLY_TARGET, .target = &target };
    ASSERT(exec_recruit(g, map, fog, res, &req, &rec));
    ASSERT_EQ_FMT(0, prims.count, "%d");                     // already held: no dismiss/buy

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_recruit_one rejects a null source and a non-positive count up front.
TEST exec_recruit_one_guards(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };
    RecruitSource src; memset(&src, 0, sizeof src);

    ASSERT_FALSE(exec_recruit_one(g, map, fog, res, NULL, 5, &rec));   // null source
    ASSERT_FALSE(exec_recruit_one(g, map, fog, res, &src, 0, &rec));   // n <= 0
    ASSERT_EQ_FMT(0, prims.count, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

SUITE(exec_recruit_suite) {
    RUN_TEST(exec_recruit_sources_enumerates);
    RUN_TEST(exec_dismiss_removes_a_stack);
    RUN_TEST(exec_recruit_empty_target_is_trivial);
    RUN_TEST(exec_recruit_already_satisfied);
    RUN_TEST(exec_recruit_one_guards);
}
