// GameFoeCanEvade: with economy.evade_needs_free_square a hostile foe can be
// evaded only while one of the 8 squares around the hero is free to move onto;
// without the setting evading is always allowed. Zero-asset: a calloc'd Map is
// all walkable grass with nothing on it.

#include "greatest.h"
#include "game.h"
#include "map.h"
#include "resources.h"
#include <stdlib.h>
#include <string.h>

typedef struct { Resources *res; Game *g; Map *m; } EvFx;

static EvFx ev_make(bool rule) {
    EvFx fx;
    fx.res = calloc(1, sizeof *fx.res);
    fx.g = calloc(1, sizeof *fx.g);
    fx.m = calloc(1, sizeof *fx.m);
    fx.res->economy.evade_needs_free_square = rule;
    fx.g->res = fx.res;
    MapAlloc(fx.m, 16, 16);
    strcpy(fx.g->position.zone, "z");
    fx.g->position.x = 8; fx.g->position.y = 8;
    return fx;
}

static void ev_free(EvFx *fx) { MapFree(fx->m); free(fx->m); GameFree(fx->g); free(fx->g); free(fx->res); }

static void block_all_but(EvFx *fx, int keep_dx, int keep_dy) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if ((!dx && !dy) || (dx == keep_dx && dy == keep_dy)) continue;
            MAP_TILE(fx->m, 8 + dx, 8 + dy).blocks_foot = true;
        }
}

TEST open_ground_allows_evade(void) {
    EvFx fx = ev_make(true);
    ASSERT(GameFoeCanEvade(fx.g, fx.m));
    ev_free(&fx);
    PASS();
}

TEST boxed_in_blocks_evade(void) {
    EvFx fx = ev_make(true);
    block_all_but(&fx, 9, 9);                     // keep nothing free
    ASSERT_FALSE(GameFoeCanEvade(fx.g, fx.m));
    ev_free(&fx);
    PASS();
}

TEST one_free_diagonal_is_enough(void) {
    EvFx fx = ev_make(true);
    block_all_but(&fx, 1, -1);
    ASSERT(GameFoeCanEvade(fx.g, fx.m));
    ev_free(&fx);
    PASS();
}

TEST an_object_or_a_foe_is_not_free(void) {
    EvFx fx = ev_make(true);
    block_all_but(&fx, 1, 0);
    MAP_TILE(fx.m, 9, 8).interactive = INTERACT_TREASURE_CHEST;
    ASSERT_FALSE(GameFoeCanEvade(fx.g, fx.m));
    MAP_TILE(fx.m, 9, 8).interactive = INTERACT_NONE;
    GameReserveFoes(fx.g, 1);
    FoeState *f = &fx.g->foes[0];
    strcpy(f->zone, "z"); f->x = 9; f->y = 8; f->alive = true;
    fx.g->foe_count = 1;
    ASSERT_FALSE(GameFoeCanEvade(fx.g, fx.m));
    ev_free(&fx);
    PASS();
}

TEST without_the_rule_evade_is_always_allowed(void) {
    EvFx fx = ev_make(false);
    block_all_but(&fx, 9, 9);
    ASSERT(GameFoeCanEvade(fx.g, fx.m));
    ev_free(&fx);
    PASS();
}

SUITE(unit_foe_evade_suite) {
    RUN_TEST(open_ground_allows_evade);
    RUN_TEST(boxed_in_blocks_evade);
    RUN_TEST(one_free_diagonal_is_enough);
    RUN_TEST(an_object_or_a_foe_is_not_free);
    RUN_TEST(without_the_rule_evade_is_always_allowed);
}
