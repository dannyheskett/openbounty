// Home castle Blessing and Tribute (GameSeekBlessing, GamePayTribute): the
// blessing needs every artifact and is given once; a tribute costs its gold,
// grows leadership and magic by a share of what the hero has, and a short
// purse pays nothing.

#include "greatest.h"
#include "fixtures.h"
#include "tables.h"
#include <string.h>

static void set_rules(Resources *res) {
    res->economy.audiences = true;
    res->economy.blessing_leadership_pct = 50;
    res->economy.tribute_cost = 50000;
    res->economy.tribute_leadership_pct = 25;
    res->economy.tribute_magic_pct = 25;
}

TEST blessing_needs_every_artifact_and_is_once(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, 0));
    set_rules(res);
    int total = artifacts_count() < 8 ? artifacts_count() : 8;
    ASSERT(total > 1);
    memset(g->artifacts.found, 0, sizeof g->artifacts.found);
    for (int i = 0; i < total - 1; i++) g->artifacts.found[i] = true;
    g->stats.leadership_base = g->stats.leadership_current = 1000;
    int needed = 0;
    GameAudienceGain gain;
    ASSERT_EQ(GAME_BLESSING_NEED_ARTIFACTS, GameSeekBlessing(g, &needed, &gain));
    ASSERT_EQ(1, needed);
    ASSERT_EQ(1000, g->stats.leadership_base);
    g->artifacts.found[total - 1] = true;
    ASSERT_EQ(GAME_BLESSING_GRANTED, GameSeekBlessing(g, &needed, &gain));
    ASSERT_EQ(500, gain.leadership);
    ASSERT_EQ(1500, g->stats.leadership_base);
    ASSERT_EQ(1500, g->stats.leadership_current);
    ASSERT_EQ(GAME_BLESSING_ALREADY, GameSeekBlessing(g, &needed, &gain));
    ASSERT_EQ(1500, g->stats.leadership_base);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST tribute_pays_and_repeats(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, 0));
    set_rules(res);
    g->stats.gold = 120000;
    g->stats.leadership_base = g->stats.leadership_current = 400;
    g->stats.spell_power = 8;
    g->stats.max_spells = 2;
    int needed = 0;
    GameAudienceGain gain;
    ASSERT(GamePayTribute(g, &needed, &gain));
    ASSERT_EQ(70000, g->stats.gold);
    ASSERT_EQ(100, gain.leadership);
    ASSERT_EQ(500, g->stats.leadership_base);
    ASSERT_EQ(10, g->stats.spell_power);
    ASSERT_EQ(3, g->stats.max_spells);       // 25% of 2 rounds to at least 1
    ASSERT(GamePayTribute(g, &needed, &gain));   // of what the hero has now
    ASSERT_EQ(625, g->stats.leadership_base);
    ASSERT_EQ(12, g->stats.spell_power);
    ASSERT_EQ(2, g->stats.tributes);
    ASSERT_FALSE(GamePayTribute(g, &needed, &gain));
    ASSERT_EQ(30000, needed);
    ASSERT_EQ(20000, g->stats.gold);
    ASSERT_EQ(625, g->stats.leadership_base);
    // A hero with no magic gains none.
    g->stats.gold = 50000;
    g->stats.spell_power = 0;
    g->stats.max_spells = 0;
    ASSERT(GamePayTribute(g, &needed, &gain));
    ASSERT_EQ(0, g->stats.spell_power);
    ASSERT_EQ(0, g->stats.max_spells);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(unit_audiences_suite) {
    RUN_TEST(blessing_needs_every_artifact_and_is_once);
    RUN_TEST(tribute_pays_and_repeats);
}
