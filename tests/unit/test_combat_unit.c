// Tests for combat unit init + morale + control predicates.

#include "greatest.h"
#include "combat_internal.h"
#include "fixtures.h"
#include "tables.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

TEST init_unit_sets_count_and_max(void) {
    CombatUnit u;
    memset(&u, 0xFF, sizeof u);  // dirty memory
    combat_init_unit(&u, /*troop_idx=*/0, /*count=*/25);
    ASSERT_EQ(25, u.count);
    ASSERT_EQ(25, u.max_count);
    ASSERT_EQ(25, u.turn_count);
    ASSERT_EQ(0,  u.injury);
    ASSERT_FALSE(u.dead);
    ASSERT_FALSE(u.acted);
    ASSERT_FALSE(u.retaliated);
    ASSERT_FALSE(u.frozen);
    ASSERT_FALSE(u.out_of_control);
    PASS();
}

TEST init_unit_zero_count(void) {
    CombatUnit u;
    combat_init_unit(&u, 0, 0);
    ASSERT_EQ(0, u.count);
    ASSERT_EQ(0, u.max_count);
    PASS();
}

// morale_to_rank takes a char marker: 'L'=low(1), 'H'=high(2),
// other (incl 'N')=normal(0).
TEST morale_to_rank_low_high_default(void) {
    ASSERT_EQ(1, morale_to_rank('L'));
    ASSERT_EQ(2, morale_to_rank('H'));
    ASSERT_EQ(0, morale_to_rank('N'));
    ASSERT_EQ(0, morale_to_rank('?'));
    ASSERT_EQ(0, morale_to_rank(0));
    PASS();
}

TEST under_control_when_leadership_sufficient(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g; fx_init_game(&g, res, FIXTURE_SEED);
    g.stats.leadership_current = 1000;

    const TroopDef *peasants = troop_by_id("peasants");
    ASSERT(peasants);
    // 1 hp peasants x 100 = 100 <= 1000 -> under control.
    ASSERT(unit_under_control(&g, peasants->index, 100));

    resources_free(res); free(res);
    GameFree(&g);
    PASS();
}

TEST under_control_when_leadership_insufficient(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g; fx_init_game(&g, res, FIXTURE_SEED);
    g.stats.leadership_current = 50;

    const TroopDef *peasants = troop_by_id("peasants");
    ASSERT(peasants);
    // 1 hp x 100 = 100 > 50 -> out of control.
    ASSERT_FALSE(unit_under_control(&g, peasants->index, 100));

    resources_free(res); free(res);
    GameFree(&g);
    PASS();
}

TEST under_control_invalid_troop_returns_false(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g; fx_init_game(&g, res, FIXTURE_SEED);
    ASSERT_FALSE(unit_under_control(&g, /*troop_idx=*/-1, 5));
    ASSERT_FALSE(unit_under_control(&g, 9999, 5));
    resources_free(res); free(res);
    GameFree(&g);
    PASS();
}

// Open field fields three of the five garrison slots -- the original's quirk --
// unless the target is a fixed guardian in a pack with combat.guardian_full_band,
// which fields all five (combat_prepare_foe, engine/combat.c).
static void fill_five(Unit *g) {
    for (int i = 0; i < 5; i++) {
        const TroopDef *t = troop_by_index(i);
        size_t n = strlen(t->id);
        if (n >= sizeof g[i].id) n = sizeof g[i].id - 1;
        memcpy(g[i].id, t->id, n);
        g[i].id[n] = '\0';
        g[i].count = 10 + i;
    }
}

static int foe_units(const Combat *c) {
    int n = 0;
    for (int i = 0; i < COMBAT_SLOTS; i++)
        if (c->units[COMBAT_SIDE_AI][i].count > 0) n++;
    return n;
}

TEST wandering_band_fields_three(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Unit garrison[5] = { 0 };
    fill_five(garrison);
    CombatTarget t = { 0 };
    t.name = "band"; t.garrison = garrison; t.garrison_slots = 5;
    Combat c; memset(&c, 0, sizeof c);
    combat_prepare_foe(&c, &t);
    ASSERT_EQ(3, foe_units(&c));
    resources_free(res); free(res);
    PASS();
}

TEST fixed_guardian_fields_five(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Unit garrison[5] = { 0 };
    fill_five(garrison);
    CombatTarget t = { 0 };
    t.name = "guardian"; t.garrison = garrison; t.garrison_slots = 5;
    t.full_band = true;
    Combat c; memset(&c, 0, sizeof c);
    combat_prepare_foe(&c, &t);
    ASSERT_EQ(5, foe_units(&c));
    // Each on its own row, at the field's right edge.
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(COMBAT_W - 1, c.units[COMBAT_SIDE_AI][i].x);
        ASSERT_EQ(i, c.units[COMBAT_SIDE_AI][i].y);
    }
    resources_free(res); free(res);
    PASS();
}

// The full band is the pack's rule (combat.guardian_full_band), read by the
// engine's one target builder, so the game, autoplay and --validate-pack field
// the same band.
TEST full_band_follows_the_pack_key(void) {
    Resources *res1; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res1, &g, &m, &f, NULL, FIXTURE_SEED));
    ASSERT(g->foe_count > 0);
    FoeState *foe = &g->foes[0];
    CombatTarget t;

    foe->is_static = true;
    res1->guardian_full_band = false;        // King's Bounty: three
    CombatTargetForFoe(g, foe->placement_id, &t);
    ASSERT_EQ(foe->garrison, t.garrison);
    ASSERT_STR_EQ(foe->placement_id, t.seed_key);
    ASSERT_FALSE(t.full_band);

    res1->guardian_full_band = true;         // The Glory of Rome: a guardian, five
    CombatTargetForFoe(g, foe->placement_id, &t);
    ASSERT(t.full_band);

    foe->is_static = false;                  // a wandering band stays three
    CombatTargetForFoe(g, foe->placement_id, &t);
    ASSERT_FALSE(t.full_band);

    CombatTargetForFoe(g, "no_such_foe", &t);
    ASSERT_EQ(NULL, t.garrison);
    fx_free_game_full(res1, g, m, f);
    PASS();
}

// An out-of-control stack is back under control as soon as its losses bring
// it within the hero's leadership: 100 peasants (1 hp) out of control under
// 60 leadership, 50 lost, are under control again before the next unit acts.
TEST ooc_stack_regains_control_when_it_fits(void) {
    Resources *res1; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res1, &g, &m, &f, NULL, FIXTURE_SEED));
    const TroopDef *t = troop_by_id("peasants");
    ASSERT(t && t->hit_points == 1);
    g->stats.leadership_current = 60;
    static Combat c;
    memset(&c, 0, sizeof c);
    for (int s = 0; s < COMBAT_SIDES; s++)
        for (int i = 0; i < COMBAT_SLOTS; i++) c.units[s][i].troop_idx = -1;
    c.heroes[COMBAT_SIDE_PLAYER] = g;
    combat_init_unit(&c.units[COMBAT_SIDE_PLAYER][0], t->index, 100);
    c.units[COMBAT_SIDE_PLAYER][0].out_of_control = !unit_under_control(g, t->index, 100);
    ASSERT(c.units[COMBAT_SIDE_PLAYER][0].out_of_control);
    c.side = COMBAT_SIDE_PLAYER; c.unit_id = -1;
    c.units[COMBAT_SIDE_PLAYER][0].count = 61;          // still over
    combat_next_unit(&c);
    ASSERT(c.units[COMBAT_SIDE_PLAYER][0].out_of_control);
    c.units[COMBAT_SIDE_PLAYER][0].count = 50;          // 50 died: fits under 60
    c.unit_id = -1;
    combat_next_unit(&c);
    ASSERT_FALSE(c.units[COMBAT_SIDE_PLAYER][0].out_of_control);
    fx_free_game_full(res1, g, m, f);
    PASS();
}

SUITE(unit_combat_unit_suite) {
    RUN_TEST(init_unit_sets_count_and_max);
    RUN_TEST(init_unit_zero_count);
    RUN_TEST(morale_to_rank_low_high_default);
    RUN_TEST(under_control_when_leadership_sufficient);
    RUN_TEST(under_control_when_leadership_insufficient);
    RUN_TEST(under_control_invalid_troop_returns_false);
    RUN_TEST(wandering_band_fields_three);
    RUN_TEST(fixed_guardian_fields_five);
    RUN_TEST(full_band_follows_the_pack_key);
    RUN_TEST(ooc_stack_regains_control_when_it_fits);
}
