// Tests for the attacker's combat morale (REQ-385 item 3, #75): the rule
// ported from King's Bounty when combat.morale_as_army_view is off, the
// army view's rule (REQ-271) when it is on. Uses the fixture pack's chart
// (chart[my][their]): A beside D is N, D beside A is L, C alone is H, A
// alone is N.

#include "greatest.h"
#include "combat_internal.h"
#include "fixtures.h"
#include "tables.h"

#include <string.h>
#include <stdlib.h>

static void zero_combat(Combat *c) {
    memset(c, 0, sizeof *c);
    for (int s = 0; s < COMBAT_SIDES; s++)
        for (int i = 0; i < COMBAT_SLOTS; i++)
            c->units[s][i].troop_idx = -1;
    c->rng_state = 1ULL;
}

static int troop_idx(const char *id) {
    const TroopDef *t = troop_by_id(id);
    return t ? t->index : -1;
}

// side 0: slot 0 peasants (group A), slot 1 orcs (group D)
static void pair_army(Combat *c) {
    zero_combat(c);
    combat_init_unit(&c->units[0][0], troop_idx("peasants"), 10);
    combat_init_unit(&c->units[0][1], troop_idx("orcs"), 10);
}

TEST ported_rule_degrades_a_mixed_army_to_normal(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    res->morale_as_army_view = false;
    Combat c; pair_army(&c);
    ASSERT_EQ(0, combat_unit_morale_rank(&c, 0, 0));   // peasants: min(chart[A][A]=N, chart[D][A]=L) by rank -> Normal
    ASSERT_EQ(0, combat_unit_morale_rank(&c, 0, 1));   // orcs: min(chart[A][D]=N, chart[D][D]=H) -> Normal
    resources_free(res); free(res);
    PASS();
}

TEST ported_rule_reads_the_diagonal_for_a_lone_troop(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    res->morale_as_army_view = false;
    Combat c; zero_combat(&c);
    combat_init_unit(&c.units[0][0], troop_idx("peasants"), 10);   // chart[A][A] = N
    ASSERT_EQ(0, combat_unit_morale_rank(&c, 0, 0));
    combat_init_unit(&c.units[0][0], troop_idx("sprites"), 10);    // chart[C][C] = H
    ASSERT_EQ(2, combat_unit_morale_rank(&c, 0, 0));
    resources_free(res); free(res);
    PASS();
}

TEST army_view_rule_matches_the_label(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    res->morale_as_army_view = true;
    Combat c; pair_army(&c);
    ASSERT_EQ(0, combat_unit_morale_rank(&c, 0, 0));   // peasants beside orcs: chart[A][D] = N -> Normal
    ASSERT_EQ(1, combat_unit_morale_rank(&c, 0, 1));   // orcs beside peasants: chart[D][A] = L -> Low
    resources_free(res); free(res);
    PASS();
}

TEST army_view_rule_lone_troop_is_high(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    res->morale_as_army_view = true;
    Combat c; zero_combat(&c);
    combat_init_unit(&c.units[0][0], troop_idx("peasants"), 10);
    ASSERT_EQ(2, combat_unit_morale_rank(&c, 0, 0));
    resources_free(res); free(res);
    PASS();
}

TEST army_view_rule_ignores_dead_slots(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    res->morale_as_army_view = true;
    Combat c; pair_army(&c);
    c.units[0][0].count = 0;                            // the peasants are gone
    ASSERT_EQ(2, combat_unit_morale_rank(&c, 0, 1));   // orcs alone -> High
    resources_free(res); free(res);
    PASS();
}

SUITE(unit_combat_morale_suite) {
    RUN_TEST(ported_rule_degrades_a_mixed_army_to_normal);
    RUN_TEST(ported_rule_reads_the_diagonal_for_a_lone_troop);
    RUN_TEST(army_view_rule_matches_the_label);
    RUN_TEST(army_view_rule_lone_troop_is_high);
    RUN_TEST(army_view_rule_ignores_dead_slots);
}
