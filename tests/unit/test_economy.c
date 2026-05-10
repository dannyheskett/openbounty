// Economy tests: recruit drains gold, refuses when broke, refuses
// when leadership cap exceeded, and stacks onto matching slots.

#include "greatest.h"
#include "game.h"
#include "tables.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

static int find_slot(const Game *g, const char *id) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, id) == 0) return i;
    }
    return -1;
}

TEST buy_troop_drains_gold(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 10000;
    g->stats.leadership_current = 1000;

    const TroopDef *t = troop_by_id("peasants");
    ASSERT(t);
    int gold_before = g->stats.gold;
    int rc = GameBuyTroop(g, "peasants", 5);
    ASSERT_EQ(0, rc);
    ASSERT_EQ(gold_before - t->recruit_cost * 5, g->stats.gold);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_refuses_when_broke(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 1;             // far less than any troop cost
    g->stats.leadership_current = 1000;
    int rc = GameBuyTroop(g, "peasants", 5);
    ASSERT_EQ(1, rc);              // 1 = insufficient gold
    ASSERT_EQ(1, g->stats.gold);   // gold untouched
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_refuses_over_leadership(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 1000000;
    g->stats.leadership_current = 5;   // can only handle a tiny stack
    int rc = GameBuyTroop(g, "dragons", 100);   // dragons = 200 HP each
    ASSERT_EQ(3, rc);                  // 3 = leadership exceeded
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_zero_or_negative_count_refused(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 10000;
    g->stats.leadership_current = 1000;
    ASSERT(GameBuyTroop(g, "peasants", 0)  != 0);
    ASSERT(GameBuyTroop(g, "peasants", -1) != 0);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_unknown_id_refused(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 10000;
    int gold_before = g->stats.gold;
    int rc = GameBuyTroop(g, "nonexistent", 5);
    ASSERT(rc != 0);
    ASSERT_EQ(gold_before, g->stats.gold);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_stacks_on_matching_slot(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 100000;
    g->stats.leadership_current = 10000;

    GameBuyTroop(g, "peasants", 3);
    int slot = find_slot(g, "peasants");
    ASSERT(slot >= 0);
    int count_before = g->army[slot].count;
    GameBuyTroop(g, "peasants", 7);
    // Same slot — count is the sum, no new slot used.
    ASSERT_EQ(slot, find_slot(g, "peasants"));
    ASSERT_EQ(count_before + 7, g->army[slot].count);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST max_recruitable_ignores_other_troops(void) {
    // Per-troop cap only subtracts the SAME troop's leadership
    // consumption, not other stacks. This is what lets a fresh Knight
    // recruit 30 Militia + 8 Archers + 10 Pikemen even though the
    // totals exceed leadership 100.
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    g->stats.leadership_current = 100;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    int rc = GameAddTroop(g, "peasants", 60);
    ASSERT_EQ(0, rc);

    // Cavalry hp=20. Other-troop leadership ignored; free = 100.
    // Max = 100/20 = 5.
    int max_cavalry = GameMaxRecruitable(g, "cavalry");
    ASSERT_EQ(5, max_cavalry);

    // Knights hp=35. Max = 100/35 = 2.
    int max_knights = GameMaxRecruitable(g, "knights");
    ASSERT_EQ(2, max_knights);

    // Adding more peasants reduces the peasant cap (same-troop slot
    // consumption IS subtracted). Free for peasants = 100 - 60 = 40.
    int max_peasants = GameMaxRecruitable(g, "peasants");
    ASSERT_EQ(40, max_peasants);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST max_recruitable_scales_with_leadership(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    // Empty the army first so existing stacks don't eat the leadership budget.
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }

    g->stats.leadership_current = 100;
    int n100 = GameMaxRecruitable(g, "peasants");
    g->stats.leadership_current = 200;
    int n200 = GameMaxRecruitable(g, "peasants");
    ASSERT(n200 > n100);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST compact_army_collapses_gaps(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    // Lay out a deliberate gap: slots 0 and 2 filled, 1/3/4 empty.
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    snprintf(g->army[0].id, sizeof g->army[0].id, "peasants");
    g->army[0].count = 50;
    snprintf(g->army[2].id, sizeof g->army[2].id, "archers");
    g->army[2].count = 5;

    GameCompactArmy(g);

    ASSERT_STR_EQ("peasants", g->army[0].id);
    ASSERT_EQ(50, g->army[0].count);
    ASSERT_STR_EQ("archers", g->army[1].id);
    ASSERT_EQ(5, g->army[1].count);
    ASSERT_EQ('\0', g->army[2].id[0]);
    ASSERT_EQ(0, g->army[2].count);
    ASSERT_EQ('\0', g->army[3].id[0]);
    ASSERT_EQ('\0', g->army[4].id[0]);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST compact_army_zero_count_treated_as_empty(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    // Slot 0: id set but count=0 → treated as empty (combat wipe leaves
    // this state before compact).
    snprintf(g->army[0].id, sizeof g->army[0].id, "ghosts");
    g->army[0].count = 0;
    snprintf(g->army[1].id, sizeof g->army[1].id, "knights");
    g->army[1].count = 3;

    GameCompactArmy(g);
    ASSERT_STR_EQ("knights", g->army[0].id);
    ASSERT_EQ(3, g->army[0].count);
    ASSERT_EQ('\0', g->army[1].id[0]);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST compact_army_already_dense_unchanged(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    snprintf(g->army[0].id, sizeof g->army[0].id, "peasants");
    g->army[0].count = 10;
    snprintf(g->army[1].id, sizeof g->army[1].id, "militia");
    g->army[1].count = 5;

    GameCompactArmy(g);
    ASSERT_STR_EQ("peasants", g->army[0].id);
    ASSERT_EQ(10, g->army[0].count);
    ASSERT_STR_EQ("militia", g->army[1].id);
    ASSERT_EQ(5, g->army[1].count);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(economy_suite) {
    RUN_TEST(buy_troop_drains_gold);
    RUN_TEST(buy_troop_refuses_when_broke);
    RUN_TEST(buy_troop_refuses_over_leadership);
    RUN_TEST(buy_troop_zero_or_negative_count_refused);
    RUN_TEST(buy_troop_unknown_id_refused);
    RUN_TEST(buy_troop_stacks_on_matching_slot);
    RUN_TEST(max_recruitable_ignores_other_troops);
    RUN_TEST(max_recruitable_scales_with_leadership);
    RUN_TEST(compact_army_collapses_gaps);
    RUN_TEST(compact_army_zero_count_treated_as_empty);
    RUN_TEST(compact_army_already_dense_unchanged);
}
