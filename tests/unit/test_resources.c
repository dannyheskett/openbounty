// Resource catalog sanity tests. The catalog is loaded once from
// game.json; these tests ensure the load produced something sane.

#include "greatest.h"
#include "tables.h"
#include "fixtures.h"

#include <stdlib.h>

TEST troops_catalog_nonempty(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    int n = troops_count();
    ASSERT(n > 0);
    ASSERT(n >= 20);  // OpenKB ships ~25 troops
    resources_free(r); free(r);
    PASS();
}

TEST spells_catalog_complete(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    // OpenKB has 14 spells (7 combat + 7 adventure).
    ASSERT_EQ(14, spells_count());
    resources_free(r); free(r);
    PASS();
}

TEST classes_catalog_four(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    // Knight, Paladin, Barbarian, Sorceress.
    ASSERT_EQ(4, classes_count());
    ASSERT(class_by_id("knight")    != NULL);
    ASSERT(class_by_id("paladin")   != NULL);
    ASSERT(class_by_id("barbarian") != NULL);
    ASSERT(class_by_id("sorceress") != NULL);
    resources_free(r); free(r);
    PASS();
}

TEST villains_count_seventeen(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    // OpenKB has 17 villains.
    ASSERT_EQ(17, villains_count());
    resources_free(r); free(r);
    PASS();
}

SUITE(resources_suite) {
    RUN_TEST(troops_catalog_nonempty);
    RUN_TEST(spells_catalog_complete);
    RUN_TEST(classes_catalog_four);
    RUN_TEST(villains_count_seventeen);
}
