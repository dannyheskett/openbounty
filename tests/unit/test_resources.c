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
    ASSERT(n >= 20);  // The catalog ships ~25 troops
    resources_free(r); free(r);
    PASS();
}

TEST spells_catalog_complete(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    // 14 spells: 7 combat + 7 adventure.
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
    // 17 villains.
    ASSERT_EQ(17, villains_count());
    resources_free(r); free(r);
    PASS();
}

// castles[].footprint (REQ-228): absent and "3x2" are the classic stamp, "1x1"
// the single-tile castle, and anything else falls back to 3x2 and is reported
// through the parser's return value.
TEST castle_footprint_string_parses(void) {
    ResCastleFootprint fp = RES_CASTLE_FOOTPRINT_1X1;
    ASSERT(resources_parse_castle_footprint("1x1", &fp));
    ASSERT_EQ(RES_CASTLE_FOOTPRINT_1X1, fp);
    ASSERT(resources_parse_castle_footprint("3x2", &fp));
    ASSERT_EQ(RES_CASTLE_FOOTPRINT_3X2, fp);
    fp = RES_CASTLE_FOOTPRINT_1X1;
    ASSERT(resources_parse_castle_footprint("", &fp));
    ASSERT_EQ(RES_CASTLE_FOOTPRINT_3X2, fp);
    fp = RES_CASTLE_FOOTPRINT_1X1;
    ASSERT(resources_parse_castle_footprint(NULL, &fp));
    ASSERT_EQ(RES_CASTLE_FOOTPRINT_3X2, fp);
    fp = RES_CASTLE_FOOTPRINT_1X1;
    ASSERT_FALSE(resources_parse_castle_footprint("2x2", &fp));
    ASSERT_EQ(RES_CASTLE_FOOTPRINT_3X2, fp);
    PASS();
}

TEST castles_default_to_the_3x2_footprint(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->castle_count > 0);
    for (int i = 0; i < r->castle_count; i++)
        ASSERT_EQ(RES_CASTLE_FOOTPRINT_3X2, r->castles[i].footprint);
    resources_free(r); free(r);
    PASS();
}

SUITE(unit_resources_suite) {
    RUN_TEST(troops_catalog_nonempty);
    RUN_TEST(spells_catalog_complete);
    RUN_TEST(classes_catalog_four);
    RUN_TEST(villains_count_seventeen);
    RUN_TEST(castle_footprint_string_parses);
    RUN_TEST(castles_default_to_the_3x2_footprint);
}
