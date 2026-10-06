// Open-field obstacles (engine/combat.c combat_reset_match): each cell of the
// middle columns holds one at the pack's combat.field_obstacle_chance. At King's
// Bounty's 10 the draw is the original one in ten, random stream and all; Glory
// of Rome sets 12 (#64).

#include "greatest.h"
#include "combat.h"
#include "fixtures.h"
#include "resources.h"

#include <string.h>
#include <stdlib.h>

static void open_field(Combat *c, uint32_t seed) {
    memset(c, 0, sizeof *c);
    c->rng_state = seed;
    for (int s = 0; s < COMBAT_SIDES; s++)
        for (int i = 0; i < COMBAT_SLOTS; i++) c->units[s][i].troop_idx = -1;
    combat_reset_match(c);
}

static int count_obstacles(const Combat *c, bool *outside_middle) {
    int n = 0;
    for (int y = 0; y < COMBAT_H; y++)
        for (int x = 0; x < COMBAT_W; x++) {
            if (!c->omap[y][x]) continue;
            n++;
            if (x < 1 || x > COMBAT_W - 3) *outside_middle = true;
        }
    return n;
}

TEST one_in_ten_is_the_original_draw(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    ASSERT_EQ(10, res->field_obstacle_chance);        // a pack that sets nothing
    for (uint32_t seed = 1; seed < 200; seed++) {
        Combat c, ref;
        open_field(&c, seed);
        // The original loop, replayed on the same stream.
        memset(&ref, 0, sizeof ref);
        ref.rng_state = seed;
        for (int j = 0; j < COMBAT_H; j++)
            for (int i = 1; i <= COMBAT_W - 3; i++)
                if (combat_rand(&ref, 0, 9) == 0)
                    ref.omap[j][i] = (unsigned char)combat_rand(&ref, 1, 3);
        ASSERT_MEM_EQ(ref.omap, c.omap, sizeof c.omap);
        ASSERT_EQ(ref.rng_state, c.rng_state);
    }
    resources_free(res); free(res);
    PASS();
}

TEST the_pack_chance_sets_the_density(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    static const int chances[] = { 0, 10, 20 };
    int total[3] = { 0 };
    const int seeds = 2000;
    for (int k = 0; k < 3; k++) {
        res->field_obstacle_chance = chances[k];
        bool outside = false;
        for (uint32_t seed = 1; seed <= (uint32_t)seeds; seed++) {
            Combat c;
            open_field(&c, seed);
            total[k] += count_obstacles(&c, &outside);
        }
        ASSERT_FALSE(outside);                           // the middle columns only
    }
    ASSERT_EQ(0, total[0]);
    // 15 cells: 1.5 a battle at 10, 3 at 20.
    ASSERT(total[1] > seeds * 13 / 10 && total[1] < seeds * 17 / 10);
    ASSERT(total[2] > seeds * 27 / 10 && total[2] < seeds * 33 / 10);
    res->field_obstacle_chance = 10;
    resources_free(res); free(res);
    PASS();
}

SUITE(unit_field_obstacles_suite) {
    RUN_TEST(one_in_ten_is_the_original_draw);
    RUN_TEST(the_pack_chance_sets_the_density);
}
