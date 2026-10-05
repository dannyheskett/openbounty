// A wandering foe never stands on a zone event's tile. Galliae's Temple of
// Ocean is a grass-ground landmark with no object on it: a foe that walked
// onto it hid it, and its leaving repainted the cell as plain grass, so the
// temple vanished from the map while its vista still fired.

#include "greatest.h"
#include "game.h"
#include "map.h"
#include "tile.h"
#include "pack.h"
#include "resources.h"

#include <stdlib.h>
#include <string.h>

TEST a_foe_walks_round_the_temple_of_ocean(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *res = calloc(1, sizeof *res);
    Game *g = calloc(1, sizeof *g);
    Map *m = calloc(1, sizeof *m);
    // Every check is read into these flags before any assert, so a failure
    // never leaves the Rome pack on the stack for later suites.
    bool loaded = false, temple = false, kept_off = true, art_kept = true;
    if (res && g && m && resources_load(res, "game.json")) {
        g->res = res;
        g->seed = 42;
        GameInit(g, "T", 0, 1, NULL);
        snprintf(g->position.zone, sizeof g->position.zone, "galliae");
        const ResZone *z = resources_zone_by_id(res, "galliae");
        loaded = z && z->event_count > 0 && MapLoadZoneWithPlacements(m, res, "galliae", g);
        if (loaded) {
            int tx = z->events[0].x, ty = z->events[0].y;            // the temple
            char art[TILE_ART_NAME_LEN];
            snprintf(art, sizeof art, "%s", TileArt(m, MapGetTile(m, tx, ty)));
            temple = strstr(art, "temple_ocean") != NULL;
            // One hostile foe beside the temple, homing on the sea beyond it:
            // the temple is the nearest step, and must not be taken.
            for (int i = 0; i < g->foe_count; i++) g->foes[i].alive = false;
            if (GameReserveFoes(g, 1)) {
                FoeState *f = &g->foes[0];
                memset(f, 0, sizeof *f);
                snprintf(f->zone, sizeof f->zone, "galliae");
                snprintf(f->placement_id, sizeof f->placement_id, "foe_test");
                f->x = tx + 1; f->y = ty;
                f->alive = true;
                g->foe_count = 1;
                MapStampFoe(m, f->x, f->y, f->placement_id);
                g->position.x = g->position.last_x = tx - 1;
                g->position.y = g->position.last_y = ty;
                for (int step = 0; step < 6; step++) {
                    GameFoesFollow(g, m);
                    if (f->x == tx && f->y == ty) kept_off = false;
                    if (strcmp(art, TileArt(m, MapGetTile(m, tx, ty))) != 0) art_kept = false;
                }
            }
        }
    }
    if (g) { GameFree(g); free(g); }
    if (m) { MapFree(m); free(m); }
    if (res) { resources_free(res); free(res); }
    pack_stack_pop();

    ASSERT(loaded);
    ASSERT(temple);
    ASSERT(kept_off);
    ASSERT(art_kept);
    PASS();
}

SUITE(e2e_foe_landmarks_suite) {
    RUN_TEST(a_foe_walks_round_the_temple_of_ocean);
}
