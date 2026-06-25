// Contract / villain capture flow tests. Exercises GameFulfillContract
// (gold payout, villain bookkeeping, active-contract clearing) and
// GameMaybeRankUp (rank advancement at threshold).

#include "greatest.h"
#include "game.h"
#include "tables.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

TEST fulfill_contract_pays_reward_and_marks_caught(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    const VillainDef *v = villain_by_id("murray");
    ASSERT(v);
    int gold_before = g->stats.gold;
    int idx = v->index;
    ASSERT_FALSE(g->contract.villains_caught[idx]);

    bool ok = GameFulfillContract(g, "murray");
    ASSERT(ok);
    ASSERT_EQ(gold_before + v->reward, g->stats.gold);
    ASSERT(g->contract.villains_caught[idx]);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST fulfill_contract_clears_active_when_match(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    snprintf(g->contract.active_id, sizeof g->contract.active_id, "%s", "murray");
    GameFulfillContract(g, "murray");
    ASSERT_EQ('\0', g->contract.active_id[0]);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST fulfill_contract_unknown_id_returns_false(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    int gold_before = g->stats.gold;
    bool ok = GameFulfillContract(g, "no_such_villain");
    ASSERT_FALSE(ok);
    ASSERT_EQ(gold_before, g->stats.gold);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST rank_up_no_op_at_zero_villains(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    int rank_before = g->character.cls.rank_index;
    bool changed = GameMaybeRankUp(g);
    ASSERT_FALSE(changed);
    ASSERT_EQ(rank_before, g->character.cls.rank_index);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST rank_up_advances_when_threshold_met(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    const ClassDef *cls = class_by_id(g->character.cls.id);
    ASSERT(cls);

    // Mark enough villains caught to satisfy the next rank's threshold.
    int needed = cls->ranks[1].villains_needed;
    ASSERT(needed > 0);
    for (int i = 0; i < needed && i < 17; i++) {
        g->contract.villains_caught[i] = true;
    }
    int rank_before = g->character.cls.rank_index;

    bool changed = GameMaybeRankUp(g);
    ASSERT(changed);
    ASSERT(g->character.cls.rank_index > rank_before);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST rank_up_recomputes_leadership(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    const ClassDef *cls = class_by_id(g->character.cls.id);
    ASSERT(cls);
    int leadership_before = g->stats.leadership_base;
    int needed = cls->ranks[1].villains_needed;
    for (int i = 0; i < needed && i < 17; i++) {
        g->contract.villains_caught[i] = true;
    }
    GameMaybeRankUp(g);
    // Higher rank → higher leadership cap.
    ASSERT(g->stats.leadership_base > leadership_before);

    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(e2e_contract_suite) {
    RUN_TEST(fulfill_contract_pays_reward_and_marks_caught);
    RUN_TEST(fulfill_contract_clears_active_when_match);
    RUN_TEST(fulfill_contract_unknown_id_returns_false);
    RUN_TEST(rank_up_no_op_at_zero_villains);
    RUN_TEST(rank_up_advances_when_threshold_met);
    RUN_TEST(rank_up_recomputes_leadership);
}
