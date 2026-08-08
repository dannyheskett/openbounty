// GameFoesFollow invariants: two live foes may never share a tile, and a live
// foe off the hero's tile always carries its INTERACT_FOE stamp (so the
// stamp-based step-onto combat trigger can engage it). Zero-asset: builds a
// Game + Map inline (calloc zeroes tiles to TERRAIN_GRASS / INTERACT_NONE and
// travel_mode to TRAVEL_WALK), so it runs in well under a millisecond.
//
// Regression: two wandering foes could stack on one tile because an unstamped
// "phantom" foe made the occupancy check read the tile as free; the stacked-on
// foe could then never be engaged (a pursuit livelock).

#include "greatest.h"
#include "map.h"
#include "tile.h"
#include "game.h"

#include <string.h>
#include <stdlib.h>

// Place a live hostile foe at (x,y) in zone "z". Does NOT stamp the map tile --
// the caller decides whether this foe starts stamped or as a phantom.
static void put_foe(Game *g, int idx, const char *id, int x, int y) {
    FoeState *f = &g->foes[idx];
    strcpy(f->zone, "z");
    f->x = x; f->y = y;
    f->alive = true;
    f->friendly = false;
    strcpy(f->placement_id, id);
}

// Two foes, one of them an unstamped phantom on the tile the other foe wants to
// step onto. Post-fix, the mover must NOT stack on top of the phantom.
TEST foes_never_stack(void) {
    Game *g = calloc(1, sizeof *g);
    Map  *m = calloc(1, sizeof *m);
    ASSERT(g && m);
    m->width = 64; m->height = 64;

    strcpy(g->position.zone, "z");
    g->position.x = 8;  g->position.y = 42;   // hero (not adjacent to the foes)
    g->position.last_x = 8; g->position.last_y = 44;   // foes home toward (8,44)

    // Phantom: sits at (8,44) but its tile is left INTERACT_NONE.
    put_foe(g, 0, "foe0", 8, 44);
    // Mover: at (9,44); its cheapest home-step toward (8,44) is the phantom tile.
    put_foe(g, 1, "foe1", 9, 44);
    g->foe_count = 2;

    GameFoesFollow(g, m);

    ASSERT_FALSE(g->foes[0].x == g->foes[1].x &&
                 g->foes[0].y == g->foes[1].y);
    free(g); free(m);
    PASS();
}

// A live foe off the hero's tile that starts unstamped must be re-stamped, so the
// stamp-based combat trigger can engage it (no unreachable phantom).
TEST live_foe_stays_stamped(void) {
    Game *g = calloc(1, sizeof *g);
    Map  *m = calloc(1, sizeof *m);
    ASSERT(g && m);
    m->width = 64; m->height = 64;

    strcpy(g->position.zone, "z");
    g->position.x = 5;  g->position.y = 7;
    g->position.last_x = 5; g->position.last_y = 5;   // foe already on its home tile

    // Phantom at (5,5), unstamped; homing target is its own tile, so it won't move.
    put_foe(g, 0, "foe0", 5, 5);
    g->foe_count = 1;

    GameFoesFollow(g, m);

    const Tile *t = MapGetTile(m, 5, 5);
    ASSERT(t != NULL);
    ASSERT_EQ(INTERACT_FOE, t->interactive);
    ASSERT_EQ(0, strcmp(t->id, "foe0"));
    free(g); free(m);
    PASS();
}

// ---------------------------------------------------------------------------
// Terrain gating (issue #22). The original accepts a candidate tile only when
// its map byte is 0x00 grass (OPENKB-SPEC.md:6234). Each test below sets up one
// foe at (9,44) homing toward (8,44) and makes every neighbour of the foe
// unstandable, so the only legal outcome is staying put -- the centre cell is
// exempt from the obstacle test in both the original and GameFoesFollow.
//
// homing_control() is the load-bearing counterpart: it proves the same fixture
// DOES move the foe over plain grass, so a "did not move" assertion below can
// never pass vacuously.

// Build the shared fixture: 64x64 all-grass map, hero clear of the foe, one live
// foe at (9,44) whose homing target is (8,44).
static void homing_fixture(Game **pg, Map **pm) {
    Game *g = calloc(1, sizeof *g);
    Map  *m = calloc(1, sizeof *m);
    m->width = 64; m->height = 64;
    strcpy(g->position.zone, "z");
    g->position.x = 8;  g->position.y = 40;
    g->position.last_x = 8; g->position.last_y = 44;
    put_foe(g, 0, "foe0", 9, 44);
    g->foe_count = 1;
    *pg = g; *pm = m;
}

// Control: plain grass, so the foe must reach its target tile. If this ever
// fails, every "stays put" assertion in this section is meaningless.
TEST homing_control(void) {
    Game *g; Map *m; homing_fixture(&g, &m);
    GameFoesFollow(g, m);
    ASSERT_EQ(8, g->foes[0].x);
    ASSERT_EQ(44, g->foes[0].y);
    free(g); free(m);
    PASS();
}

// Desert is a non-zero byte. Foes must not enter it, which is what makes desert
// a refuge for the hero.
TEST foe_refuses_desert(void) {
    Game *g; Map *m; homing_fixture(&g, &m);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) m->tiles[44 + dy][9 + dx].terrain = TERRAIN_DESERT;
    GameFoesFollow(g, m);
    ASSERT_EQ(9, g->foes[0].x);
    ASSERT_EQ(44, g->foes[0].y);
    free(g); free(m);
    PASS();
}

// A bridge keeps TERRAIN_GRASS (TerrainFromArt falls through for unrecognised
// art), so only the is_bridge flag distinguishes it. Without that check a foe
// crosses water and follows the hero over a barrier.
TEST foe_refuses_bridge(void) {
    Game *g; Map *m; homing_fixture(&g, &m);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) m->tiles[44 + dy][9 + dx].is_bridge = true;
    GameFoesFollow(g, m);
    ASSERT_EQ(9, g->foes[0].x);
    ASSERT_EQ(44, g->foes[0].y);
    free(g); free(m);
    PASS();
}

// House rule, not a restoration: the gate approach is grass in the original too.
// A gate at (7,44) makes (8,43), (8,44) and (8,45) all gate-adjacent, so no
// step toward the target is legal.
TEST foe_refuses_castle_gate_approach(void) {
    Game *g; Map *m; homing_fixture(&g, &m);
    m->tiles[44][7].interactive = INTERACT_CASTLE_GATE;
    GameFoesFollow(g, m);
    ASSERT_FALSE(g->foes[0].x == 8 && g->foes[0].y == 44);
    ASSERT_EQ(9, g->foes[0].x);
    ASSERT_EQ(44, g->foes[0].y);
    free(g); free(m);
    PASS();
}

// The gate tile itself was already rejected by the INTERACT_NONE test; keep that
// nailed down so the new adjacency rule can't be mistaken for the only guard.
TEST foe_refuses_castle_gate_tile(void) {
    Game *g; Map *m; homing_fixture(&g, &m);
    m->tiles[44][8].interactive = INTERACT_CASTLE_GATE;
    GameFoesFollow(g, m);
    ASSERT_FALSE(g->foes[0].x == 8 && g->foes[0].y == 44);
    free(g); free(m);
    PASS();
}

// Terrain the previous rule already rejected via TerrainWalkable(). No behavior
// change, pinned so a future edit to foe_can_stand cannot quietly readmit them.
static void assert_terrain_blocks(Terrain terr, int *ox, int *oy) {
    Game *g; Map *m; homing_fixture(&g, &m);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) m->tiles[44 + dy][9 + dx].terrain = terr;
    GameFoesFollow(g, m);
    *ox = g->foes[0].x; *oy = g->foes[0].y;
    free(g); free(m);
}

TEST foe_refuses_water_forest_mountain(void) {
    int x, y;
    assert_terrain_blocks(TERRAIN_WATER, &x, &y);
    ASSERT_EQ(9, x); ASSERT_EQ(44, y);
    assert_terrain_blocks(TERRAIN_FOREST, &x, &y);
    ASSERT_EQ(9, x); ASSERT_EQ(44, y);
    assert_terrain_blocks(TERRAIN_MOUNTAIN, &x, &y);
    ASSERT_EQ(9, x); ASSERT_EQ(44, y);
    PASS();
}

// Pack data may seat a foe on desert or a bridge; the grass-only rule must not
// strand it. The center cell is exempt from the obstacle test in both the
// original and GameFoesFollow, so the foe can always step back onto grass.
TEST foe_on_desert_can_leave(void) {
    Game *g; Map *m; homing_fixture(&g, &m);
    m->tiles[44][9].terrain = TERRAIN_DESERT;   // the foe's OWN tile
    GameFoesFollow(g, m);
    ASSERT_EQ(8, g->foes[0].x);
    ASSERT_EQ(44, g->foes[0].y);
    free(g); free(m);
    PASS();
}

// ---------------------------------------------------------------------------
// Reaching the hero. foe_closest_offset tests the map byte of all eight
// non-center cells without regard to where the player stands, so a hero on any
// non-zero tile cannot be reached. GameFoesFollow returns the foe index when a
// foe lands on the hero (the combat trigger) and -1 otherwise.

// Place the hero on the foe's target tile with the given terrain, and report
// whether the foe reached them.
static int hero_contact(Terrain terr, bool bridge, bool boat) {
    Game *g; Map *m; homing_fixture(&g, &m);
    g->position.x = 8; g->position.y = 44;
    g->position.last_x = 8; g->position.last_y = 44;
    m->tiles[44][8].terrain = terr;
    m->tiles[44][8].is_bridge = bridge;
    if (boat) g->travel_mode = TRAVEL_BOAT;
    int r = GameFoesFollow(g, m);
    free(g); free(m);
    return r;
}

// Control. Without this, every "unreachable" assertion below could pass simply
// because contact is broken everywhere, which would silently disable combat.
TEST hero_on_grass_is_reachable(void) {
    ASSERT_EQ(0, hero_contact(TERRAIN_GRASS, false, false));
    PASS();
}

TEST hero_on_desert_is_unreachable(void) {
    ASSERT_EQ(-1, hero_contact(TERRAIN_DESERT, false, false));
    PASS();
}

TEST hero_on_bridge_is_unreachable(void) {
    ASSERT_EQ(-1, hero_contact(TERRAIN_GRASS, true, false));
    PASS();
}

TEST hero_in_boat_is_unreachable(void) {
    ASSERT_EQ(-1, hero_contact(TERRAIN_WATER, false, true));
    PASS();
}

// The castle-gate house rule covers the hero too: standing on the approach is
// as safe as any other tile a foe may not enter.
TEST hero_on_gate_approach_is_unreachable(void) {
    Game *g; Map *m; homing_fixture(&g, &m);
    g->position.x = 8; g->position.y = 44;
    g->position.last_x = 8; g->position.last_y = 44;
    m->tiles[44][7].interactive = INTERACT_CASTLE_GATE;
    ASSERT_EQ(-1, GameFoesFollow(g, m));
    free(g); free(m);
    PASS();
}

SUITE(unit_foe_follow_suite) {
    RUN_TEST(foes_never_stack);
    RUN_TEST(live_foe_stays_stamped);
    RUN_TEST(homing_control);
    RUN_TEST(foe_refuses_desert);
    RUN_TEST(foe_refuses_bridge);
    RUN_TEST(foe_refuses_castle_gate_approach);
    RUN_TEST(foe_refuses_castle_gate_tile);
    RUN_TEST(foe_refuses_water_forest_mountain);
    RUN_TEST(foe_on_desert_can_leave);
    RUN_TEST(hero_on_grass_is_reachable);
    RUN_TEST(hero_on_desert_is_unreachable);
    RUN_TEST(hero_on_bridge_is_unreachable);
    RUN_TEST(hero_in_boat_is_unreachable);
    RUN_TEST(hero_on_gate_approach_is_unreachable);
}
