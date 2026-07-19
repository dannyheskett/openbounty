// Map overlay / interactive lookup tests. Validates that known fixed
// positions in continentia load with the expected interactive type
// and metadata (sign title, etc.).

#include "greatest.h"
#include "map.h"
#include "tile.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

TEST sign_at_known_coord_has_expected_title(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    // From assets/kings-bounty/game.json: sign at (3, 60) with title
    // "Treasure Island".
    const Tile *t = MapGetTile(m, 3, 60);
    ASSERT(t);
    ASSERT_EQ(INTERACT_SIGN, t->interactive);
    ASSERT_STR_EQ("Treasure Island", t->sign_title);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST get_tile_in_bounds_returns_non_null(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    ASSERT(MapGetTile(m, 0, 0));
    ASSERT(MapGetTile(m, m->width - 1, m->height - 1));
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST get_tile_out_of_bounds_returns_null(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    ASSERT_FALSE(MapGetTile(m, -1, 0));
    ASSERT_FALSE(MapGetTile(m, 0, -1));
    ASSERT_FALSE(MapGetTile(m, m->width, 0));
    ASSERT_FALSE(MapGetTile(m, 0, m->height));
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST in_bounds_matches_dimensions(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    ASSERT(MapInBounds(m, 0, 0));
    ASSERT(MapInBounds(m, m->width - 1, m->height - 1));
    ASSERT_FALSE(MapInBounds(m, m->width, 0));
    ASSERT_FALSE(MapInBounds(m, 0, m->height));
    ASSERT_FALSE(MapInBounds(m, -1, 0));
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST clear_interactive_removes_overlay_metadata(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    // Sign at (3,60) -- clear it and verify the overlay is gone.
    const Tile *before = MapGetTile(m, 3, 60);
    ASSERT(before);
    ASSERT(before->interactive != INTERACT_NONE);

    MapClearInteractive(m, 3, 60);

    const Tile *after = MapGetTile(m, 3, 60);
    ASSERT(after);
    ASSERT_EQ(INTERACT_NONE, after->interactive);
    ASSERT_EQ('\0', after->id[0]);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST clear_interactive_out_of_bounds_no_op(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    // Should not crash.
    MapClearInteractive(m, -1, -1);
    MapClearInteractive(m, m->width + 5, m->height + 5);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST hero_spawn_coords_present(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    // Continentia hero_spawn = (12, 62) -- water tile just off the
    // home castle coast. Cross-zone arrivals need a water spawn so the
    // boat parks correctly; the new-game
    // start uses home_spawn (11, 58) instead.
    ASSERT_EQ(12, m->hero_spawn_x);
    ASSERT_EQ(62, m->hero_spawn_y);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(unit_map_overlay_suite) {
    RUN_TEST(sign_at_known_coord_has_expected_title);
    RUN_TEST(get_tile_in_bounds_returns_non_null);
    RUN_TEST(get_tile_out_of_bounds_returns_null);
    RUN_TEST(in_bounds_matches_dimensions);
    RUN_TEST(clear_interactive_removes_overlay_metadata);
    RUN_TEST(clear_interactive_out_of_bounds_no_op);
    RUN_TEST(hero_spawn_coords_present);
}
