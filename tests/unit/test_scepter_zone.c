// The scepter's zone is drawn from every zone the pack declares (REQ-235,
// #77), not a fixed 0..3.

#include "greatest.h"
#include "fixtures.h"
#include "game.h"
#include "resources.h"

#include <string.h>
#include <stdlib.h>

static int zone_index(const Resources *res, const char *id) {
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, id) == 0) return i;
    return -1;
}

TEST four_zone_pack_buries_in_a_declared_zone(void) {
    for (unsigned long seed = 1; seed <= 12; seed++) {
        Resources *res = fx_load_resources();
        ASSERT(res);
        ASSERT_EQ(4, res->zone_count);
        Game *g = (Game *)calloc(1, sizeof *g);
        ASSERT(g);
        fx_init_game(g, res, seed);
        ASSERT(g->scepter.zone[0]);
        ASSERT(zone_index(res, g->scepter.zone) >= 0);
        ASSERT(g->scepter.x >= 0 && g->scepter.y >= 0);
        resources_free(res); free(res); free(g);
    }
    PASS();
}

TEST fewer_zones_still_bury_the_scepter(void) {
    // With the catalog cut to two zones, every seed must still bury the
    // scepter, and only in one of the two. Before #77 half the seeds drew a
    // zone the pack no longer had and buried nothing.
    for (unsigned long seed = 1; seed <= 12; seed++) {
        Resources *res = fx_load_resources();
        ASSERT(res);
        res->zone_count = 2;
        Game *g = (Game *)calloc(1, sizeof *g);
        ASSERT(g);
        fx_init_game(g, res, seed);
        int zi = zone_index(res, g->scepter.zone);
        ASSERT(zi >= 0 && zi < 2);
        ASSERT(g->scepter.x >= 0 && g->scepter.y >= 0);
        res->zone_count = 4;   // give the loader back its zones before freeing
        resources_free(res); free(res); free(g);
    }
    PASS();
}

SUITE(unit_scepter_zone_suite) {
    RUN_TEST(four_zone_pack_buries_in_a_declared_zone);
    RUN_TEST(fewer_zones_still_bury_the_scepter);
}
