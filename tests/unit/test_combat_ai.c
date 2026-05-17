// Combat AI helper tests. Exercises ai_pick_target (target selection)
// and unit_move_offset (single-step pathfinding) by setting up small
// hand-crafted Combat boards. Mirrors the combat_test_digest scaffold.

#include "greatest.h"
#include "combat.h"
#include "combat_internal.h"
#include "fixtures.h"
#include "tables.h"
#include "resources.h"

#include <string.h>
#include <stdlib.h>

// Helper: zero out a Combat and mark all slots empty.
static void ai_reset(Combat *c) {
    memset(c, 0, sizeof *c);
    c->rng_state = 1;
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            c->units[s][i].troop_idx = -1;
        }
    }
}

// Helper: place a unit by id at (x,y), set shots from troop catalog.
static void ai_place(Combat *c, int side, int slot, const char *id,
                     int count, int x, int y) {
    const TroopDef *t = troop_by_id(id);
    if (!t) return;
    combat_init_unit(&c->units[side][slot], t->index, count);
    c->units[side][slot].x = x;
    c->units[side][slot].y = y;
    c->units[side][slot].shots = t->ranged_ammo;
    // Mirror umap so unit_move_offset's friendly-skip logic sees occupants.
    c->umap[y][x] = (unsigned char)(side * COMBAT_SLOTS + slot + 1);
}

// All tests load Resources first — ai_place() and friends look up
// troop catalog entries by id which require the global catalog set
// up in resources_load.
#define AI_SETUP() \
    Resources *res = fx_load_resources(); \
    ASSERT(res)
#define AI_TEARDOWN() \
    do { resources_free(res); free(res); } while (0)

TEST ai_pick_target_returns_zero_when_no_enemies(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "peasants", 10, 0, 0);
    unsigned char uid = ai_pick_target(&c, COMBAT_SIDE_PLAYER, 0, false);
    ASSERT_EQ(0, uid);
    AI_TEARDOWN();
    PASS();
}

TEST ai_pick_target_nearby_only_picks_adjacent(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 2, 2);
    ai_place(&c, COMBAT_SIDE_AI, 0, "orcs", 10, 3, 2);   // adjacent
    ai_place(&c, COMBAT_SIDE_AI, 1, "wolves", 10, 5, 4); // far

    unsigned char nearby = ai_pick_target(&c, COMBAT_SIDE_PLAYER, 0, true);
    ASSERT(nearby != 0);
    int s = (nearby - 1) / COMBAT_SLOTS;
    int i = (nearby - 1) % COMBAT_SLOTS;
    ASSERT_EQ(COMBAT_SIDE_AI, s);
    ASSERT_EQ(0, i);
    AI_TEARDOWN();
    PASS();
}

TEST ai_pick_target_far_prefers_shooters(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 0, 0);
    ai_place(&c, COMBAT_SIDE_AI, 0, "wolves",  10, 5, 0); // melee (no ranged_ammo)
    ai_place(&c, COMBAT_SIDE_AI, 1, "archers", 10, 5, 4); // ranged

    unsigned char uid = ai_pick_target(&c, COMBAT_SIDE_PLAYER, 0, false);
    ASSERT(uid != 0);
    int i = (uid - 1) % COMBAT_SLOTS;
    ASSERT_EQ(1, i);  // archers — shooter priority
    AI_TEARDOWN();
    PASS();
}

TEST ai_pick_target_far_prefers_low_hp_when_no_shooters(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 0, 0);
    ai_place(&c, COMBAT_SIDE_AI, 0, "ogres",  10, 5, 0);
    ai_place(&c, COMBAT_SIDE_AI, 1, "peasants", 50, 5, 4);

    unsigned char uid = ai_pick_target(&c, COMBAT_SIDE_PLAYER, 0, false);
    ASSERT(uid != 0);
    int i = (uid - 1) % COMBAT_SLOTS;
    ASSERT_EQ(1, i);
    AI_TEARDOWN();
    PASS();
}

TEST ai_pick_target_skips_dead_slots(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 0, 0);
    ai_place(&c, COMBAT_SIDE_AI, 0, "orcs", 10, 3, 2);
    const TroopDef *t = troop_by_id("wolves");
    ASSERT(t);
    c.units[COMBAT_SIDE_AI][1].troop_idx = t->index;
    c.units[COMBAT_SIDE_AI][1].count     = 0;

    unsigned char uid = ai_pick_target(&c, COMBAT_SIDE_PLAYER, 0, false);
    ASSERT(uid != 0);
    int i = (uid - 1) % COMBAT_SLOTS;
    ASSERT_EQ(0, i);
    AI_TEARDOWN();
    PASS();
}

TEST ai_pick_target_ooc_attacks_own_side(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 0, 0);
    c.units[COMBAT_SIDE_PLAYER][0].out_of_control = true;
    ai_place(&c, COMBAT_SIDE_PLAYER, 1, "peasants", 50, 5, 4);
    unsigned char uid = ai_pick_target(&c, COMBAT_SIDE_PLAYER, 0, false);
    ASSERT(uid != 0);
    int s = (uid - 1) / COMBAT_SLOTS;
    int i = (uid - 1) % COMBAT_SLOTS;
    ASSERT_EQ(COMBAT_SIDE_PLAYER, s);
    ASSERT_EQ(1, i);
    AI_TEARDOWN();
    PASS();
}

TEST move_offset_steps_toward_target(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 1, 2);
    ai_place(&c, COMBAT_SIDE_AI, 0, "orcs", 10, 5, 2);

    int ox = 99, oy = 99;
    unit_move_offset(&c, &c.units[COMBAT_SIDE_PLAYER][0],
                     5, 2, &ox, &oy);
    // Target is east; we should advance with +1 in x. Tie-break may
    // prefer a diagonal — accept any step that increases x toward target.
    ASSERT_EQ(1, ox);
    AI_TEARDOWN();
    PASS();
}

TEST move_offset_skips_friendly_blocker(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 1, 2);
    ai_place(&c, COMBAT_SIDE_PLAYER, 1, "knights", 5, 2, 2);  // east blocker
    ai_place(&c, COMBAT_SIDE_AI, 0, "orcs", 10, 5, 2);

    int ox = 99, oy = 99;
    unit_move_offset(&c, &c.units[COMBAT_SIDE_PLAYER][0],
                     5, 2, &ox, &oy);
    ASSERT(!(ox == 1 && oy == 0));        // didn't step into friendly cell
    ASSERT(ox != 0 || oy != 0);           // still moved somewhere
    AI_TEARDOWN();
    PASS();
}

TEST move_offset_no_target_returns_zero_when_blocked(void) {
    AI_SETUP();
    Combat c; ai_reset(&c);
    ai_place(&c, COMBAT_SIDE_PLAYER, 0, "knights", 5, 2, 2);
    // Pile friendlies + obstacles on all 8 neighbours so nothing is walkable.
    int n = 1;
    for (int dy = -1; dy <= 1 && n < COMBAT_SLOTS; dy++) {
        for (int dx = -1; dx <= 1 && n < COMBAT_SLOTS; dx++) {
            if (dx == 0 && dy == 0) continue;
            ai_place(&c, COMBAT_SIDE_PLAYER, n, "peasants", 10, 2 + dx, 2 + dy);
            n++;
        }
    }
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = 2 + dx, ny = 2 + dy;
            if (c.umap[ny][nx] == 0) c.omap[ny][nx] = 1;
        }
    }
    int ox = 99, oy = 99;
    unit_move_offset(&c, &c.units[COMBAT_SIDE_PLAYER][0],
                     5, 4, &ox, &oy);
    ASSERT_EQ(0, ox);
    ASSERT_EQ(0, oy);
    AI_TEARDOWN();
    PASS();
}

SUITE(unit_combat_ai_suite) {
    RUN_TEST(ai_pick_target_returns_zero_when_no_enemies);
    RUN_TEST(ai_pick_target_nearby_only_picks_adjacent);
    RUN_TEST(ai_pick_target_far_prefers_shooters);
    RUN_TEST(ai_pick_target_far_prefers_low_hp_when_no_shooters);
    RUN_TEST(ai_pick_target_skips_dead_slots);
    RUN_TEST(ai_pick_target_ooc_attacks_own_side);
    RUN_TEST(move_offset_steps_toward_target);
    RUN_TEST(move_offset_skips_friendly_blocker);
    RUN_TEST(move_offset_no_target_returns_zero_when_blocked);
}
