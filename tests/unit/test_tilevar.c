// The cosmetic tile-variant pick: pure, stable, spread, and off when a
// stem declares nothing.
#include "greatest.h"
#include "tilevar.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

TEST pick_is_stable_and_in_range(void) {
    for (int y = 0; y < 20; y++)
        for (int x = 0; x < 20; x++) {
            int a = tilevar_pick(7u, x, y, 4);
            ASSERT(a >= 0 && a < 4);
            ASSERT_EQ(a, tilevar_pick(7u, x, y, 4));
        }
    ASSERT_EQ(0, tilevar_pick(7u, 3, 3, 1));
    ASSERT_EQ(0, tilevar_pick(7u, 3, 3, 0));
    PASS();
}

TEST pick_spreads_over_cells_and_changes_with_seed(void) {
    int count[4] = { 0, 0, 0, 0 }, differ = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            count[tilevar_pick(1u, x, y, 4)]++;
            if (tilevar_pick(1u, x, y, 4) != tilevar_pick(2u, x, y, 4)) differ++;
        }
    for (int i = 0; i < 4; i++) ASSERT(count[i] > 1024 / 8);   // no choice starved
    ASSERT(differ > 1024 / 2);                                   // a new seed reshuffles
    PASS();
}

TEST art_substitutes_only_declared_stems_and_keeps_the_set(void) {
    Resources r;
    memset(&r, 0, sizeof r);
    ResTileCode *g = &r.tile_codes['.'];
    g->present = true; strcpy(g->art, "grass");
    g->variant_count = 2; strcpy(g->variants[0], "grass_01"); strcpy(g->variants[1], "grass_02");
    tilevar_init(&r, 5u);
    char buf[64];
    int base = 0, v1 = 0, v2 = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            char a[64], buf2[64];
            snprintf(a, sizeof a, "%s", tilevar_art("grass", x, y, buf, sizeof buf));
            if (strcmp(a, "grass") == 0) base++;
            else if (strcmp(a, "grass_01") == 0) v1++;
            else if (strcmp(a, "grass_02") == 0) v2++;
            else FAIL();
            const char *b = tilevar_art("rome/grass", x, y, buf2, sizeof buf2);
            ASSERT(strncmp(b, "rome/", 5) == 0);
            ASSERT_STR_EQ(a, b + 5);                 // same pick, set prefix kept
        }
    ASSERT(base > 0 && v1 > 0 && v2 > 0);
    ASSERT_STR_EQ("forest", tilevar_art("forest", 1, 1, buf, sizeof buf));
    tilevar_init(NULL, 0u);
    ASSERT_STR_EQ("grass", tilevar_art("grass", 1, 1, buf, sizeof buf));
    PASS();
}

SUITE(unit_tilevar_suite) {
    RUN_TEST(pick_is_stable_and_in_range);
    RUN_TEST(pick_spreads_over_cells_and_changes_with_seed);
    RUN_TEST(art_substitutes_only_declared_stems_and_keeps_the_set);
}
