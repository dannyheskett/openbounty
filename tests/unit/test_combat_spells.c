// Tests for spell helpers exposed from combat.c.

#include "greatest.h"
#include "combat_internal.h"
#include "fixtures.h"
#include "tables.h"
#include "resources.h"

#include <string.h>
#include <stdlib.h>

// Minimal Combat scaffold: no resources, just a zero'd struct with empty slots.
static void spell_combat_reset(Combat *c) {
    memset(c, 0, sizeof *c);
    c->rng_state = 1ULL;
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            c->units[s][i].troop_idx = -1;
        }
    }
}

static void spell_place(Combat *c, int side, int slot, const char *id,
                        int count, int x, int y) {
    const TroopDef *t = troop_by_id(id);
    if (!t) return;
    combat_init_unit(&c->units[side][slot], t->index, count);
    c->units[side][slot].x = x;
    c->units[side][slot].y = y;
    c->umap[y][x] = (unsigned char)(side * COMBAT_SLOTS + slot + 1);
}

TEST spell_damage_value_scales_with_sp(void) {
    ASSERT_EQ(10, spell_damage_value(10, 1));
    ASSERT_EQ(20, spell_damage_value(10, 2));
    ASSERT_EQ(50, spell_damage_value(10, 5));
    PASS();
}

TEST spell_damage_value_clamps_low_sp(void) {
    // sp < 1 is treated as 1 (sp=0 disabled, sp=-3 nonsense).
    ASSERT_EQ(10, spell_damage_value(10, 0));
    ASSERT_EQ(10, spell_damage_value(10, -3));
    PASS();
}

TEST spell_damage_value_zero_base(void) {
    ASSERT_EQ(0, spell_damage_value(0, 1));
    ASSERT_EQ(0, spell_damage_value(0, 5));
    PASS();
}

TEST spell_damage_value_large_inputs(void) {
    // 100 base * 10 sp = 1000. Within int range.
    ASSERT_EQ(1000, spell_damage_value(100, 10));
    PASS();
}

// ---- effect tests --------------------------------------------------------

TEST spell_damage_reduces_count(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 1, 0, "peasants", 50, 3, 2);
    int before = c.units[1][0].count;
    int kills = spell_damage(&c, 1, 0, 30);
    ASSERT(kills >= 0);
    ASSERT(c.units[1][0].count <= before);
    // 30 damage on 1-HP peasants = ~30 kills (modulo injury).
    ASSERT(before - c.units[1][0].count > 0);
    resources_free(res); free(res); PASS();
}

TEST spell_damage_kills_full_stack_when_overdamaged(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 1, 0, "peasants", 10, 3, 2);
    c.units[1][0].turn_count = 10;   // stack snapshot for kill bookkeeping
    spell_damage(&c, 1, 0, 999);
    ASSERT_EQ(0, c.units[1][0].count);
    ASSERT(c.units[1][0].dead);
    resources_free(res); free(res); PASS();
}

TEST spell_clone_increases_count(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 0, 0, "peasants", 10, 2, 2);
    int before = c.units[0][0].count;
    int max_before = c.units[0][0].max_count;
    spell_clone(&c, 0, 0, /*sp=*/3);   // 3*10 = 30 damage / HP=1 = 30 clones
    ASSERT(c.units[0][0].count > before);
    // max_count grows alongside count so the new clones don't get instantly
    // capped by absorb/leech logic.
    ASSERT(c.units[0][0].max_count > max_before);
    resources_free(res); free(res); PASS();
}

TEST spell_teleport_moves_unit_and_updates_umap(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 0, 0, "knights", 5, 1, 1);
    int old_x = 1, old_y = 1, new_x = 4, new_y = 3;
    spell_teleport(&c, 0, 0, new_x, new_y);
    ASSERT_EQ(new_x, c.units[0][0].x);
    ASSERT_EQ(new_y, c.units[0][0].y);
    ASSERT_EQ(0, c.umap[old_y][old_x]);
    ASSERT(c.umap[new_y][new_x] != 0);
    resources_free(res); free(res); PASS();
}

TEST spell_freeze_sets_flag(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 1, 0, "orcs", 10, 3, 2);
    ASSERT_FALSE(c.units[1][0].frozen);
    int rc = spell_freeze(&c, 1, 0);
    ASSERT_EQ(1, rc);
    ASSERT(c.units[1][0].frozen);
    resources_free(res); free(res); PASS();
}

TEST spell_freeze_immune_target(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 1, 0, "dragons", 3, 3, 2);
    int rc = spell_freeze(&c, 1, 0);
    ASSERT_EQ(-1, rc);
    ASSERT_FALSE(c.units[1][0].frozen);
    resources_free(res); free(res); PASS();
}

TEST spell_resurrect_revives_up_to_max(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 0, 0, "peasants", 50, 2, 2);
    c.units[0][0].max_count = 50;
    c.units[0][0].count     = 30;     // some have died
    c.units[0][0].injury    = 5;
    spell_resurrect(&c, 0, 0, /*sp=*/4);
    // Revives sp=4 creatures, capped at max_count.
    ASSERT_EQ(34, c.units[0][0].count);
    ASSERT_EQ(0,  c.units[0][0].injury);
    resources_free(res); free(res); PASS();
}

TEST spell_resurrect_no_op_on_full_stack(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 0, 0, "peasants", 50, 2, 2);
    c.units[0][0].max_count = 50;
    c.units[0][0].count     = 50;     // already at cap
    spell_resurrect(&c, 0, 0, /*sp=*/4);
    ASSERT_EQ(50, c.units[0][0].count);
    resources_free(res); free(res); PASS();
}

TEST spell_resurrect_skips_dead_stack(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    Combat c; spell_combat_reset(&c);
    spell_place(&c, 0, 0, "peasants", 0, 2, 2);   // count=0 → fully dead
    c.units[0][0].max_count = 50;
    spell_resurrect(&c, 0, 0, /*sp=*/4);
    ASSERT_EQ(0, c.units[0][0].count);     // Resurrect can't bring back wholly dead stacks.
    resources_free(res); free(res); PASS();
}

SUITE(unit_combat_spells_suite) {
    RUN_TEST(spell_damage_value_scales_with_sp);
    RUN_TEST(spell_damage_value_clamps_low_sp);
    RUN_TEST(spell_damage_value_zero_base);
    RUN_TEST(spell_damage_value_large_inputs);
    RUN_TEST(spell_damage_reduces_count);
    RUN_TEST(spell_damage_kills_full_stack_when_overdamaged);
    RUN_TEST(spell_clone_increases_count);
    RUN_TEST(spell_teleport_moves_unit_and_updates_umap);
    RUN_TEST(spell_freeze_sets_flag);
    RUN_TEST(spell_freeze_immune_target);
    RUN_TEST(spell_resurrect_revives_up_to_max);
    RUN_TEST(spell_resurrect_no_op_on_full_stack);
    RUN_TEST(spell_resurrect_skips_dead_stack);
}
