// Map overlay / interactive lookup tests. Validates that known fixed
// positions in continentia load with the expected interactive type
// and metadata (sign title, etc.).

#include "greatest.h"
#include "adventure.h"
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

// A castle stamps as its catalog entry's footprint (REQ-228). Azram is the
// first Continentia castle, gate (30,36), and the .dat holds plain grass under
// all six tiles, so the two footprints are told apart by what the stamp adds.
static int castle_index(const Resources *res, const char *id) {
    for (int i = 0; i < res->castle_count; i++)
        if (strcmp(res->castles[i].id, id) == 0) return i;
    return -1;
}

TEST castle_default_footprint_stamps_gate_and_five_walls(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Map *m = calloc(1, sizeof *m);
    ASSERT(m);
    ASSERT(MapLoadZone(m, res, "continentia"));
    const Tile *gate = MapGetTile(m, 30, 36);
    ASSERT(gate);
    ASSERT_EQ(INTERACT_CASTLE_GATE, gate->interactive);
    ASSERT_STR_EQ("azram", gate->id);
    ASSERT_STR_EQ("castle_gate", gate->art);
    ASSERT_FALSE(gate->blocks_foot);
    struct { int x, y; const char *art; } walls[5] = {
        { 29, 35, "castle_tl" }, { 30, 35, "castle_br" }, { 31, 35, "castle_tr" },
        { 29, 36, "castle_ml" }, { 31, 36, "castle_mr" },
    };
    for (int i = 0; i < 5; i++) {
        const Tile *t = MapGetTile(m, walls[i].x, walls[i].y);
        ASSERT(t);
        ASSERT_STR_EQ(walls[i].art, t->art);
        ASSERT(t->blocks_foot);
        ASSERT_EQ(INTERACT_NONE, t->interactive);
        ASSERT_FALSE(adventure_walkable_on_foot(t));
    }
    free(m); resources_free(res); free(res);
    PASS();
}

TEST castle_1x1_footprint_stamps_only_the_gate_tile(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    int ci = castle_index(res, "azram");
    ASSERT(ci >= 0);
    res->castles[ci].footprint = RES_CASTLE_FOOTPRINT_1X1;
    Map *m = calloc(1, sizeof *m);
    ASSERT(m);
    ASSERT(MapLoadZone(m, res, "continentia"));
    const Tile *gate = MapGetTile(m, 30, 36);
    ASSERT(gate);
    ASSERT_EQ(INTERACT_CASTLE_GATE, gate->interactive);
    ASSERT_STR_EQ("azram", gate->id);
    ASSERT_STR_EQ("castle", gate->art);
    ASSERT_FALSE(gate->blocks_foot);
    // The five tiles a 3x2 castle would wall off stay plain .dat terrain.
    int around[5][2] = { {29,35}, {30,35}, {31,35}, {29,36}, {31,36} };
    for (int i = 0; i < 5; i++) {
        const Tile *t = MapGetTile(m, around[i][0], around[i][1]);
        ASSERT(t);
        ASSERT_STR_EQ("grass", t->art);
        ASSERT_FALSE(t->blocks_foot);
        ASSERT_EQ(INTERACT_NONE, t->interactive);
        ASSERT(adventure_walkable_on_foot(t));
    }
    free(m); resources_free(res); free(res);
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
    RUN_TEST(castle_default_footprint_stamps_gate_and_five_walls);
    RUN_TEST(castle_1x1_footprint_stamps_only_the_gate_tile);
}
