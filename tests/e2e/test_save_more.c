// Additional save round-trip tests beyond test_save.c basics.

#include "greatest.h"
#include "savegame.h"
#include "fixtures.h"
#include "tables.h"
#include "map.h"
#include "tile.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define SAVE_PATH "/tmp/openbounty_save_more.dat"

TEST save_preserves_army_contents(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    // Snapshot initial army so we know what to compare against.
    char army0_id[32]; int army0_count;
    snprintf(army0_id, sizeof army0_id, "%s", g->army[0].id);
    army0_count = g->army[0].count;

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));

    Resources *r2 = fx_load_resources();
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    g2->res = r2;
    GameInit(g2, "Other", 0, 1, NULL);
    FogInit(f2);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));

    ASSERT_STR_EQ(army0_id, g2->army[0].id);
    ASSERT_EQ(army0_count, g2->army[0].count);

    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    fx_free_game_full(r2,  g2, m2, f2);
    PASS();
}

TEST save_preserves_consumed_list(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    GameAddConsumed(g, "continentia", 5, 10);
    GameAddConsumed(g, "continentia", 3, 7);
    GameAddConsumed(g, "forestria",   1, 1);
    int before = g->consumed_count;
    ASSERT_EQ(3, before);

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));

    Resources *r2 = fx_load_resources();
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    g2->res = r2;
    GameInit(g2, "Other", 0, 1, NULL);
    FogInit(f2);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));

    ASSERT_EQ(before, g2->consumed_count);

    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    fx_free_game_full(r2,  g2, m2, f2);
    PASS();
}

TEST save_preserves_villains_caught(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    g->contract.villains_caught[0] = true;
    g->contract.villains_caught[5] = true;
    g->contract.villains_caught[12] = true;

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));

    Resources *r2 = fx_load_resources();
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    g2->res = r2;
    GameInit(g2, "Other", 0, 1, NULL);
    FogInit(f2);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));

    ASSERT(g2->contract.villains_caught[0]);
    ASSERT(g2->contract.villains_caught[5]);
    ASSERT(g2->contract.villains_caught[12]);
    ASSERT_FALSE(g2->contract.villains_caught[3]);

    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    fx_free_game_full(r2,  g2, m2, f2);
    PASS();
}

TEST save_load_save_state_identical(void) {
    // The byte-level representation of a save isn't guaranteed
    // identical across save->load->save (e.g. options[] backwards-compat
    // may add fields). What we DO guarantee: the loaded state's
    // key state fields survive the round trip, which is what this asserts
    // field by field below.
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));

    Resources *r2 = fx_load_resources();
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    g2->res = r2;
    GameInit(g2, "Other", 0, 1, NULL);
    FogInit(f2);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));

    // Compare key state fields directly.
    ASSERT_EQ(g->seed,           g2->seed);
    ASSERT_EQ(g->stats.gold,     g2->stats.gold);
    ASSERT_EQ(g->position.x,     g2->position.x);
    ASSERT_EQ(g->position.y,     g2->position.y);
    ASSERT_STR_EQ(g->position.zone, g2->position.zone);
    ASSERT_EQ(g->consumed_count, g2->consumed_count);

    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    fx_free_game_full(r2,  g2, m2, f2);
    PASS();
}


// A save read into a running game brings its zone back onto the map: an
// artifact picked up after the save was written stands again, and one picked
// up before it stays gone (issue #58, the in-game Load).
TEST reload_zone_after_reading_a_save(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    // The first artifact salted onto the fixture's zone.
    int ax = -1, ay = -1;
    for (int i = 0; i < g->placement_count; i++) {
        const SaltedPlacement *sp = &g->placements[i];
        if (sp->kind == INTERACT_ARTIFACT && strcmp(sp->zone, g->position.zone) == 0) { ax = sp->x; ay = sp->y; break; }
    }
    ASSERT(ax >= 0);
    ASSERT_EQ(INTERACT_ARTIFACT, MapGetTile(m, ax, ay)->interactive);
    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));          // before the pickup
    // Take it: clear the tile and record it, as the step does.
    const ArtifactDef *a = artifact_by_id(TileId(m, MapGetTile(m, ax, ay)));
    ASSERT(a);
    ASSERT(GameClaimArtifact(g, a->index));
    MapClearInteractive(m, ax, ay);
    GameAddConsumed(g, g->position.zone, ax, ay);
    ASSERT_EQ(INTERACT_NONE, MapGetTile(m, ax, ay)->interactive);
    // Read the older save into the running game and bring its zone back.
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g, m, f));
    ASSERT(GameReloadZoneMap(g, m, g->position.zone));
    ASSERT_FALSE(g->artifacts.found[a->index]);
    ASSERT_EQ(INTERACT_ARTIFACT, MapGetTile(m, ax, ay)->interactive);   // back where it was
    // The other way round: pick up, save, read that save over a fresh map.
    ASSERT(GameClaimArtifact(g, a->index));
    MapClearInteractive(m, ax, ay);
    GameAddConsumed(g, g->position.zone, ax, ay);
    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));
    ASSERT(MapLoadZoneWithPlacements(m, res, g->position.zone, g));    // a stale map with the tile
    ASSERT_EQ(INTERACT_ARTIFACT, MapGetTile(m, ax, ay)->interactive);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g, m, f));
    ASSERT(GameReloadZoneMap(g, m, g->position.zone));
    ASSERT(g->artifacts.found[a->index]);
    ASSERT_EQ(INTERACT_NONE, MapGetTile(m, ax, ay)->interactive);       // gone, as found
    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(e2e_save_more_suite) {
    RUN_TEST(save_preserves_army_contents);
    RUN_TEST(save_preserves_consumed_list);
    RUN_TEST(save_preserves_villains_caught);
    RUN_TEST(save_load_save_state_identical);
    RUN_TEST(reload_zone_after_reading_a_save);
}
