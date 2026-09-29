// The scepter lies on plain walkable ground in every Rome world: grass
// terrain, no overlay, not blocking, and not a bridge (REQ-235, #117). The
// Rome pack is the one with bridge and river tile codes.

#include "greatest.h"
#include "pack.h"
#include "resources.h"
#include "game.h"
#include "map.h"
#include "tile.h"

#include <stdlib.h>
#include <string.h>

TEST every_rome_world_buries_on_plain_ground(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *res = calloc(1, sizeof *res);
    ASSERT(res);
    ASSERT(resources_load(res, "game.json"));
    Game *g = calloc(1, sizeof *g);
    Map *m = calloc(1, sizeof *m);
    ASSERT(g && m);
    for (int idx = 0; idx < 256; idx++) {
        GameFree(g);
        memset(g, 0, sizeof *g);
        g->res = res;
        g->seed = GameSeedFromIndex((unsigned char)idx);
        GameInit(g, "Test", 0, 1, NULL);
        ASSERT(g->scepter.zone[0]);
        ASSERT(g->scepter.x >= 0 && g->scepter.y >= 0);
        ASSERT(MapLoadZone(m, res, g->scepter.zone));
        const Tile *t = MapGetTile(m, g->scepter.x, g->scepter.y);
        ASSERT(t);
        ASSERT_EQ(TERRAIN_GRASS, t->terrain);
        ASSERT_EQ(INTERACT_NONE, t->interactive);
        ASSERT_FALSE(t->blocks_foot);
        ASSERT_FALSE(t->is_bridge);
        ASSERT(TerrainWalkable(t->terrain));
    }
    GameFree(g); free(g);
    MapFree(m); free(m);
    resources_free(res); free(res);
    pack_stack_pop();
    PASS();
}

SUITE(unit_scepter_ground_suite) {
    RUN_TEST(every_rome_world_buries_on_plain_ground);
}
