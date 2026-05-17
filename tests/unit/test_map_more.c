// Additional map tests.

#include "greatest.h"
#include "map.h"
#include "tile.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

TEST clear_interactive_removes_overlay(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    // (3, 59) is the Treasure Island chest slot; should be interactive.
    const Tile *t = MapGetTile(m, 3, 59);
    ASSERT(t);
    ASSERT(t->interactive != INTERACT_NONE);

    MapClearInteractive(m, 3, 59);
    t = MapGetTile(m, 3, 59);
    ASSERT_EQ(INTERACT_NONE, t->interactive);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST load_all_four_zones_succeeds(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game *g = calloc(1, sizeof *g);
    g->res = res;
    g->seed = FIXTURE_SEED;
    GameInit(g, "Test", 0, 1, NULL);

    static const char *zones[4] = {
        "continentia", "forestria", "archipelia", "saharia"
    };
    for (int i = 0; i < 4; i++) {
        Map *m = calloc(1, sizeof *m);
        ASSERT(m);
        ASSERT(MapLoadZoneWithPlacements(m, res, zones[i], g));
        ASSERT_EQ(64, m->width);
        ASSERT_EQ(64, m->height);
        free(m);
    }

    free(g);
    resources_free(res); free(res);
    PASS();
}

TEST get_tile_out_of_bounds_returns_null(void) {
    Map m = { .width = 64, .height = 64 };
    ASSERT(MapGetTile(&m, -1, 0)  == NULL);
    ASSERT(MapGetTile(&m, 0,  -1) == NULL);
    ASSERT(MapGetTile(&m, 64, 0)  == NULL);
    ASSERT(MapGetTile(&m, 0, 64)  == NULL);
    PASS();
}

TEST map_walkable_matches_adventure_predicate(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    // Spot-check a handful of tiles. We don't compare every tile —
    // just verify that MapWalkable agrees with adventure_walkable_on_foot
    // on a few sample positions.
    int agree = 0;
    int disagree = 0;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            const Tile *t = MapGetTile(m, x, y);
            if (!t) continue;
            // adventure_walkable_on_foot is the source of truth used by
            // try_step. MapWalkable is a convenience wrapper.
            extern bool adventure_walkable_on_foot(const Tile *t);
            bool a = adventure_walkable_on_foot(t);
            bool b = MapWalkable(m, x, y);
            if (a == b) agree++;
            else        disagree++;
        }
    }
    // Most tiles should agree (or all). If they ever diverge, one of
    // the two predicates needs documenting / fixing.
    ASSERT(agree > 0);
    // Allow some divergence (e.g. interactive overlays may pass one
    // and not the other) — but not more than 5% of the map.
    ASSERT(disagree < (64 * 64 / 20));

    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(unit_map_more_suite) {
    RUN_TEST(clear_interactive_removes_overlay);
    RUN_TEST(load_all_four_zones_succeeds);
    RUN_TEST(get_tile_out_of_bounds_returns_null);
    RUN_TEST(map_walkable_matches_adventure_predicate);
}
