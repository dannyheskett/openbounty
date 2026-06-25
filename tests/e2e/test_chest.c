// GameAcceptChest{Gold,Leadership} + GameAddConsumed tests.

#include "greatest.h"
#include "game.h"

#include <string.h>

TEST accept_gold_increments_stat(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.stats.gold = 100;
    GameAcceptChestGold(&g, 50);
    ASSERT_EQ(150, g.stats.gold);
    PASS();
}

TEST accept_gold_zero_is_noop(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.stats.gold = 200;
    GameAcceptChestGold(&g, 0);
    ASSERT_EQ(200, g.stats.gold);
    PASS();
}

TEST accept_leadership_increments_both_stats(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.stats.leadership_base = 100;
    g.stats.leadership_current = 100;
    GameAcceptChestLeadership(&g, 25);
    ASSERT_EQ(125, g.stats.leadership_base);
    ASSERT_EQ(125, g.stats.leadership_current);
    PASS();
}

TEST add_consumed_dedupes(void) {
    Game g;
    memset(&g, 0, sizeof g);
    GameAddConsumed(&g, "continentia", 5, 10);
    ASSERT_EQ(1, g.consumed_count);
    GameAddConsumed(&g, "continentia", 5, 10);  // dup, should not grow
    ASSERT_EQ(1, g.consumed_count);
    GameAddConsumed(&g, "continentia", 6, 10);  // distinct, should grow
    ASSERT_EQ(2, g.consumed_count);
    PASS();
}

SUITE(e2e_chest_suite) {
    RUN_TEST(accept_gold_increments_stat);
    RUN_TEST(accept_gold_zero_is_noop);
    RUN_TEST(accept_leadership_increments_both_stats);
    RUN_TEST(add_consumed_dedupes);
}
