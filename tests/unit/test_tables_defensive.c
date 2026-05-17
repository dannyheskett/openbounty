// Defensive-path tests for the catalog lookups in tables.h.
// Verifies that bogus inputs (unknown id, out-of-range index, NULL)
// return NULL rather than crashing or returning garbage.

#include "greatest.h"
#include "tables.h"
#include "fixtures.h"

#include <stdlib.h>

TEST troop_by_id_unknown_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(troop_by_id("not_a_troop"));
    ASSERT_FALSE(troop_by_id(""));
    ASSERT_FALSE(troop_by_id(NULL));
    resources_free(res); free(res); PASS();
}

TEST troop_by_index_out_of_range_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(troop_by_index(-1));
    ASSERT_FALSE(troop_by_index(99999));
    ASSERT_FALSE(troop_by_index(troops_count()));
    resources_free(res); free(res); PASS();
}

TEST troop_by_index_in_range_returns_def(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT(troop_by_index(0));
    ASSERT(troop_by_index(troops_count() - 1));
    resources_free(res); free(res); PASS();
}

TEST spell_by_id_unknown_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(spell_by_id("not_a_spell"));
    ASSERT_FALSE(spell_by_id(""));
    ASSERT_FALSE(spell_by_id(NULL));
    resources_free(res); free(res); PASS();
}

TEST spell_by_index_out_of_range_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(spell_by_index(-1));
    ASSERT_FALSE(spell_by_index(99999));
    ASSERT_FALSE(spell_by_index(spells_count()));
    resources_free(res); free(res); PASS();
}

TEST spell_index_by_id_unknown_returns_neg1(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_EQ(-1, spell_index_by_id("not_a_spell"));
    ASSERT_EQ(-1, spell_index_by_id(""));
    ASSERT_EQ(-1, spell_index_by_id(NULL));
    resources_free(res); free(res); PASS();
}

TEST class_by_id_unknown_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(class_by_id("not_a_class"));
    ASSERT_FALSE(class_by_id(""));
    ASSERT_FALSE(class_by_id(NULL));
    resources_free(res); free(res); PASS();
}

TEST class_by_index_out_of_range_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(class_by_index(-1));
    ASSERT_FALSE(class_by_index(99999));
    ASSERT_FALSE(class_by_index(classes_count()));
    resources_free(res); free(res); PASS();
}

TEST villain_by_id_unknown_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(villain_by_id("not_a_villain"));
    ASSERT_FALSE(villain_by_id(""));
    ASSERT_FALSE(villain_by_id(NULL));
    resources_free(res); free(res); PASS();
}

TEST villain_by_index_out_of_range_returns_null(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    ASSERT_FALSE(villain_by_index(-1));
    ASSERT_FALSE(villain_by_index(99999));
    ASSERT_FALSE(villain_by_index(villains_count()));
    resources_free(res); free(res); PASS();
}

TEST class_stats_at_rank_zero_yields_base_values(void) {
    Resources *res = fx_load_resources(); ASSERT(res);
    const ClassDef *cls = class_by_index(0);
    ASSERT(cls);
    int lead = -1, maxsp = -1, spp = -1, comm = -1;
    class_stats_at_rank(cls, 0, &lead, &maxsp, &spp, &comm);
    // All four must be set to non-negative values.
    ASSERT(lead  >= 0);
    ASSERT(maxsp >= 0);
    ASSERT(spp   >= 0);
    ASSERT(comm  >= 0);
    resources_free(res); free(res); PASS();
}

SUITE(unit_tables_defensive_suite) {
    RUN_TEST(troop_by_id_unknown_returns_null);
    RUN_TEST(troop_by_index_out_of_range_returns_null);
    RUN_TEST(troop_by_index_in_range_returns_def);
    RUN_TEST(spell_by_id_unknown_returns_null);
    RUN_TEST(spell_by_index_out_of_range_returns_null);
    RUN_TEST(spell_index_by_id_unknown_returns_neg1);
    RUN_TEST(class_by_id_unknown_returns_null);
    RUN_TEST(class_by_index_out_of_range_returns_null);
    RUN_TEST(villain_by_id_unknown_returns_null);
    RUN_TEST(villain_by_index_out_of_range_returns_null);
    RUN_TEST(class_stats_at_rank_zero_yields_base_values);
}
