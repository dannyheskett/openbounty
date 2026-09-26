// A zone chest that names an artifact ("artifact": id) is that artifact: the
// salt places it at the chest before it draws, counts it against the zone's
// artifact quota and scatters only the rest (OPENBOUNTY-SPEC REQ-231). The
// legacy pack pins nothing, so one pin is added by hand.

#include "greatest.h"
#include "fixtures.h"
#include "tile.h"
#include <stdlib.h>
#include <string.h>

// Artifact placements in `zone` (all of them, or only `id`); the last one's cell.
static int artifacts_in(const Game *g, const char *zone, const char *id, int *x, int *y) {
    int n = 0;
    for (int i = 0; i < g->placement_count; i++) {
        const SaltedPlacement *sp = &g->placements[i];
        if (sp->kind != INTERACT_ARTIFACT || strcmp(sp->zone, zone) != 0) continue;
        if (id && strcmp(sp->id, id) != 0) continue;
        if (x) *x = sp->x;
        if (y) *y = sp->y;
        n++;
    }
    return n;
}

static int placements_at(const Game *g, const char *zone, int x, int y) {
    int n = 0;
    for (int i = 0; i < g->placement_count; i++) {
        const SaltedPlacement *sp = &g->placements[i];
        if (strcmp(sp->zone, zone) == 0 && sp->x == x && sp->y == y) n++;
    }
    return n;
}

TEST a_chest_may_pin_an_artifact(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    ResZone *z = (ResZone *)resources_zone_by_id(res, "continentia");
    ASSERT(z && z->chest_count > 0);
    const int cx = z->chests[0].x, cy = z->chests[0].y;
    Game *g = calloc(1, sizeof *g);
    ASSERT(g);

    // Nothing pinned: the zone's two artifacts are salted.
    fx_init_game(g, res, 7);
    ASSERT_EQ(2, artifacts_in(g, "continentia", NULL, NULL, NULL));

    // The first chest pins the Articles: they stand on it, the Ring is still
    // salted, the quota is still two, and nothing else lands on that cell.
    strcpy(z->chests[0].artifact, "articles");
    for (unsigned long seed = 1; seed <= 6; seed++) {
        GameFree(g);
        fx_init_game(g, res, seed);
        int x = -1, y = -1;
        ASSERT_EQ(1, artifacts_in(g, "continentia", "articles", &x, &y));
        ASSERT_EQ(cx, x);
        ASSERT_EQ(cy, y);
        ASSERT_EQ(1, artifacts_in(g, "continentia", "ring", NULL, NULL));
        ASSERT_EQ(2, artifacts_in(g, "continentia", NULL, NULL, NULL));
        ASSERT_EQ(1, placements_at(g, "continentia", cx, cy));
    }
    z->chests[0].artifact[0] = '\0';

    GameFree(g);
    free(g);
    resources_free(res);
    free(res);
    PASS();
}

SUITE(unit_chest_pin_suite) {
    RUN_TEST(a_chest_may_pin_an_artifact);
}
