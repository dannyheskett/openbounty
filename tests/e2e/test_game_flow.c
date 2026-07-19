// Multi-step game flow tests. These exercise multiple subsystems
// in sequence to catch integration bugs that single-function unit
// tests miss.

#include "greatest.h"
#include "game.h"
#include "savegame.h"
#include "fixtures.h"
#include "tables.h"
#include "tile.h"
#include "map.h"
#include "step.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define SAVE_PATH "/tmp/openbounty_flow.dat"

// Helper: simulate a single step in (dx,dy) by checking walkability
// and updating Game.position. Mirrors the engine's step path but ignores
// fog reveal etc. -- flow tests only care about state mutations.
static bool flow_walk(Game *g, Map *m, int dx, int dy) {
    int nx = g->position.x + dx;
    int ny = g->position.y + dy;
    const Tile *t = MapGetTile(m, nx, ny);
    if (!t) return false;
    extern bool adventure_walkable_on_foot(const Tile *t);
    if (!adventure_walkable_on_foot(t)) return false;
    g->position.last_x = g->position.x;
    g->position.last_y = g->position.y;
    g->position.x = nx;
    g->position.y = ny;
    return true;
}

TEST walk_save_load_walk_continues(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    int start_x = g->position.x;
    int start_y = g->position.y;

    // Walk a few steps in any walkable direction to advance position.
    int walked = 0;
    int dirs[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
    for (int i = 0; i < 4 && walked < 1; i++) {
        if (flow_walk(g, m, dirs[i][0], dirs[i][1])) walked++;
    }
    ASSERT(walked > 0);
    int saved_x = g->position.x;
    int saved_y = g->position.y;

    // Save -> fresh struct -> load -> position restored.
    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));

    Resources *r2 = fx_load_resources();
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    g2->res = r2;
    GameInit(g2, "Other", 0, 1, NULL);
    FogInit(f2);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));

    ASSERT_EQ(saved_x, g2->position.x);
    ASSERT_EQ(saved_y, g2->position.y);
    // Movement isn't a snapshot of start_x/y -- we walked at least 1 tile.
    ASSERT(saved_x != start_x || saved_y != start_y);

    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    fx_free_game_full(r2,  g2, m2, f2);
    PASS();
}

TEST chest_consumed_persists_across_save(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    int gold_before = g->stats.gold;
    GameAcceptChestGold(g, 250);
    GameAddConsumed(g, "continentia", 3, 59);
    ASSERT_EQ(gold_before + 250, g->stats.gold);
    ASSERT_EQ(1, g->consumed_count);

    ASSERT_EQ(SAVE_OK, SaveGameWrite(SAVE_PATH, g, m, f));

    Resources *r2 = fx_load_resources();
    Game *g2 = calloc(1, sizeof *g2);
    Map  *m2 = calloc(1, sizeof *m2);
    Fog  *f2 = calloc(1, sizeof *f2);
    g2->res = r2;
    GameInit(g2, "Other", 0, 1, NULL);
    FogInit(f2);
    ASSERT_EQ(SAVE_OK, SaveGameRead(SAVE_PATH, g2, m2, f2));

    ASSERT_EQ(gold_before + 250, g2->stats.gold);
    ASSERT_EQ(1, g2->consumed_count);
    ASSERT_STR_EQ("continentia", g2->consumed[0].zone);
    ASSERT_EQ(3, g2->consumed[0].x);
    ASSERT_EQ(59, g2->consumed[0].y);

    unlink(SAVE_PATH);
    fx_free_game_full(res, g, m, f);
    fx_free_game_full(r2,  g2, m2, f2);
    PASS();
}

TEST switch_zone_updates_position_zone(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    ASSERT_STR_EQ("continentia", g->position.zone);

    bool ok = GameSwitchZone(g, m, f, "forestria");
    ASSERT(ok);
    ASSERT_STR_EQ("forestria", g->position.zone);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST switch_zone_preserves_fog_on_return(void) {
    // Verify that fog of a discovered continent is restored when the
    // hero returns. Fog is per-continent, kept across boat trips
    // between continents.
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    // Reveal a distinctive tile in continentia far from the spawn.
    int marker_x = 30, marker_y = 30;
    FogReveal(f, m, marker_x, marker_y, 2);
    ASSERT(FogSeen(f, marker_x, marker_y));

    // Sail to forestria.
    ASSERT(GameSwitchZone(g, m, f, "forestria"));
    ASSERT_STR_EQ("forestria", g->position.zone);
    // Forestria's fog at (marker_x, marker_y) should be unrevealed
    // (never visited there, and the marker was on continentia).
    ASSERT_FALSE(FogSeen(f, marker_x, marker_y));

    // Sail back to continentia.
    ASSERT(GameSwitchZone(g, m, f, "continentia"));
    // The marker should still be revealed -- fog persisted.
    ASSERT(FogSeen(f, marker_x, marker_y));

    fx_free_game_full(res, g, m, f);
    PASS();
}

// Find a foot-walkable land tile orthogonally adjacent to the hero, returning
// its delta in *dx,*dy. Used to set up a boarding step onto a known tile.
static bool find_adjacent_walkable(Game *g, Map *m, int *dx, int *dy) {
    extern bool adventure_walkable_on_foot(const Tile *t);
    int dirs[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
    for (int i = 0; i < 4; i++) {
        const Tile *t = MapGetTile(m, g->position.x + dirs[i][0],
                                      g->position.y + dirs[i][1]);
        if (t && adventure_walkable_on_foot(t) &&
            t->interactive == INTERACT_NONE && !t->is_bridge) {
            *dx = dirs[i][0]; *dy = dirs[i][1];
            return true;
        }
    }
    return false;
}

TEST boat_in_other_zone_is_not_boarded(void) {
    // Regression: a boat left behind in another zone (e.g. by a gate spell,
    // boat.zone != position.zone) must NOT be boarded when the hero walks onto
    // a tile whose coords coincidentally match the abandoned boat's coords.
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));
    ASSERT_STR_EQ("continentia", g->position.zone);

    int dx, dy;
    ASSERT(find_adjacent_walkable(g, m, &dx, &dy));
    int target_x = g->position.x + dx, target_y = g->position.y + dy;

    // Boat sits at the target tile but in a DIFFERENT zone.
    g->travel_mode = TRAVEL_WALK;
    g->boat.has_boat = true;
    g->boat.x = target_x;
    g->boat.y = target_y;
    strcpy(g->boat.zone, "forestria");

    ASSERT(GameStep(g, m, f, res, dx, dy));
    // Stepped onto the tile but did NOT board the cross-zone ghost boat.
    ASSERT_EQ(target_x, g->position.x);
    ASSERT_EQ(target_y, g->position.y);
    ASSERT_EQ(TRAVEL_WALK, g->travel_mode);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST boat_in_current_zone_is_boarded(void) {
    // Positive control: a boat in the hero's CURRENT zone is still boarded
    // normally when the hero steps onto its tile.
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    int dx, dy;
    ASSERT(find_adjacent_walkable(g, m, &dx, &dy));
    int target_x = g->position.x + dx, target_y = g->position.y + dy;

    g->travel_mode = TRAVEL_WALK;
    g->boat.has_boat = true;
    g->boat.x = target_x;
    g->boat.y = target_y;
    strcpy(g->boat.zone, "continentia");   // same zone as hero

    ASSERT(GameStep(g, m, f, res, dx, dy));
    ASSERT_EQ(target_x, g->position.x);
    ASSERT_EQ(target_y, g->position.y);
    ASSERT_EQ(TRAVEL_BOAT, g->travel_mode);   // boarded

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST gate_teleport_leaves_boat_behind(void) {
    // Casting a gate spell while in a boat must leave the boat behind in the
    // origin zone and land the hero on foot at the destination.
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", FIXTURE_SEED));

    // Put the hero in a boat in continentia.
    g->travel_mode = TRAVEL_BOAT;
    g->boat.has_boat = true;
    g->boat.x = g->position.x;
    g->boat.y = g->position.y;
    strcpy(g->boat.zone, "continentia");
    int cast_x = g->position.x, cast_y = g->position.y;

    // Give a town_gate charge so the teleport can consume one.
    int sidx = spell_index_by_id("town_gate");
    ASSERT(sidx >= 0);
    g->spells.counts[sidx] = 2;

    GateDestination dest;
    strcpy(dest.name, "Forestria");
    strcpy(dest.zone, "forestria");
    dest.x = -1; dest.y = -1;   // use the zone spawn as the landing tile

    ASSERT(GameGateTeleport(g, m, f, &dest, "town_gate"));

    // Arrived in forestria, on foot.
    ASSERT_STR_EQ("forestria", g->position.zone);
    ASSERT_EQ(TRAVEL_WALK, g->travel_mode);
    // One charge consumed.
    ASSERT_EQ(1, g->spells.counts[sidx]);
    // Boat left behind in the origin zone at the cast tile.
    ASSERT(g->boat.has_boat);
    ASSERT_STR_EQ("continentia", g->boat.zone);
    ASSERT_EQ(cast_x, g->boat.x);
    ASSERT_EQ(cast_y, g->boat.y);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST add_troop_changes_army(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    // Find a free slot or already-existing peasants.
    int existing = 0;
    for (int i = 0; i < 5; i++) {
        if (strcmp(g->army[i].id, "peasants") == 0) {
            existing = g->army[i].count;
            break;
        }
    }
    int rc = GameAddTroop(g, "peasants", 5);
    ASSERT(rc >= 0);
    int after = 0;
    for (int i = 0; i < 5; i++) {
        if (strcmp(g->army[i].id, "peasants") == 0) {
            after = g->army[i].count;
            break;
        }
    }
    ASSERT(after >= existing + 5);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST villain_capture_increments_count(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    ASSERT_EQ(0, GameVillainsCaught(g));

    g->contract.villains_caught[2] = true;
    ASSERT_EQ(1, GameVillainsCaught(g));
    g->contract.villains_caught[7] = true;
    ASSERT_EQ(2, GameVillainsCaught(g));

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_after_state_change(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    int gold_before = g->stats.gold;
    GameAcceptChestGold(g, 100);
    ASSERT_EQ(gold_before + 100, g->stats.gold);

    // Fog is updated via FogReveal in production; for a flow test we
    // just sanity-check that consumed-count is intact post-mutation.
    GameAddConsumed(g, "continentia", 10, 20);
    ASSERT_EQ(1, g->consumed_count);

    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(e2e_game_flow_suite) {
    RUN_TEST(walk_save_load_walk_continues);
    RUN_TEST(chest_consumed_persists_across_save);
    RUN_TEST(switch_zone_updates_position_zone);
    RUN_TEST(switch_zone_preserves_fog_on_return);
    RUN_TEST(boat_in_other_zone_is_not_boarded);
    RUN_TEST(boat_in_current_zone_is_boarded);
    RUN_TEST(gate_teleport_leaves_boat_behind);
    RUN_TEST(add_troop_changes_army);
    RUN_TEST(villain_capture_increments_count);
    RUN_TEST(snapshot_after_state_change);
}
