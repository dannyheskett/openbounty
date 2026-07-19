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

SUITE(unit_foe_follow_suite) {
    RUN_TEST(foes_never_stack);
    RUN_TEST(live_foe_stays_stamped);
}
