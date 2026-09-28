// The calm start (REQ-283, #69): hostile foes within spawn.calm_radius of
// their zone's hero_spawn roll only the weakest pool slots and few stacks;
// radius 0 (the fixture pack's default) leaves the original roll alone.

#include "greatest.h"
#include "fixtures.h"
#include "game.h"
#include "resources.h"
#include "tables.h"

#include <string.h>
#include <stdlib.h>

static int cheb(int ax, int ay, int bx, int by) {
    int dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

static const ResZone *zone_of(const Resources *res, const char *id) {
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, id) == 0) return &res->zones[i];
    return NULL;
}

// slot of a troop id in the pool of its kind, or -1
static int pool_slot(const ResSpawn *sp, const char *id) {
    for (int k = 0; k < 4; k++)
        for (int s = 0; s < sp->pool_count[k]; s++)
            if (strcmp(sp->troop_pool[k][s], id) == 0) return s;
    return -1;
}

TEST fixture_pack_has_no_calm_start(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    ASSERT_EQ(0, res->spawn.calm_radius);
    resources_free(res); free(res);
    PASS();
}

TEST calm_foes_roll_weak_slots_and_few_stacks(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    res->spawn.calm_radius = 10;
    res->spawn.calm_max_slot = 1;
    res->spawn.calm_max_stacks = 2;
    Game *g = (Game *)calloc(1, sizeof *g);
    ASSERT(g);
    fx_init_game(g, res, 42UL);
    int calm_seen = 0, wild_seen = 0;
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (f->friendly || f->is_static) continue;
        const ResZone *z = zone_of(res, f->zone);
        ASSERT(z);
        bool calm = cheb(f->origin_x, f->origin_y, z->hero_spawn_x, z->hero_spawn_y) <= 10;
        int stacks = 0;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!f->garrison[s].id[0]) continue;
            stacks++;
            if (calm) ASSERT_LTE(pool_slot(&res->spawn, f->garrison[s].id), 1);
        }
        if (calm) { ASSERT_LTE(stacks, 2); calm_seen++; } else wild_seen++;
    }
    ASSERT(calm_seen > 0);
    ASSERT(wild_seen > 0);
    resources_free(res); free(res); free(g);
    PASS();
}

TEST radius_zero_rolls_as_before(void) {
    // Same seed, same garrisons, whether the calm fields are zero or left at
    // the pack's default: radius 0 must not touch the roll.
    Unit before[GAME_ARMY_SLOTS];
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game *g = (Game *)calloc(1, sizeof *g);
    ASSERT(g);
    fx_init_game(g, res, 7UL);
    ASSERT(g->foe_count > 0);
    memcpy(before, g->foes[0].garrison, sizeof before);
    resources_free(res); free(res); free(g);
    res = fx_load_resources();
    ASSERT(res);
    res->spawn.calm_radius = 0; res->spawn.calm_max_slot = 1; res->spawn.calm_max_stacks = 2;
    g = (Game *)calloc(1, sizeof *g);
    ASSERT(g);
    fx_init_game(g, res, 7UL);
    ASSERT_EQ(0, memcmp(before, g->foes[0].garrison, sizeof before));
    resources_free(res); free(res); free(g);
    PASS();
}

SUITE(unit_spawn_calm_suite) {
    RUN_TEST(fixture_pack_has_no_calm_start);
    RUN_TEST(calm_foes_roll_weak_slots_and_few_stacks);
    RUN_TEST(radius_zero_rolls_as_before);
}
