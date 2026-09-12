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

TEST siege_grid_defaults_to_empty(void) {
    // The fixture pack declares no sprites.ui.siege_grid, so the per-code
    // siege walls apply (legacy mode).
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT_EQ(0, (int)r->sprites.siege_grid[0]);
    resources_free(r); free(r);
    PASS();
}

// A tile code is one raw byte of a map file. Printable ASCII names itself; a
// byte with no printable spelling -- and every byte over 127, which cannot be
// a JSON key at all because cJSON encodes a \u escape as UTF-8 -- is named by
// a two-digit hex escape instead. Rome's four road ends are \x80..\x83.
TEST tile_code_key_names_a_byte(void) {
    ASSERT_EQ('.',  resources_tile_code_from_key("."));
    ASSERT_EQ('y',  resources_tile_code_from_key("y"));
    ASSERT_EQ(0x80, resources_tile_code_from_key("\\x80"));
    ASSERT_EQ(0x83, resources_tile_code_from_key("\\x83"));
    ASSERT_EQ(0xff, resources_tile_code_from_key("\\xFF"));
    ASSERT_EQ(0x0a, resources_tile_code_from_key("\\x0a"));   // either case
    ASSERT_EQ(0x0a, resources_tile_code_from_key("\\X0A"));
    // A backslash is still a code in its own right when it stands alone.
    ASSERT_EQ('\\', resources_tile_code_from_key("\\"));
    // Nothing else names a code.
    ASSERT_EQ(-1, resources_tile_code_from_key(""));
    ASSERT_EQ(-1, resources_tile_code_from_key(NULL));
    ASSERT_EQ(-1, resources_tile_code_from_key("ab"));
    ASSERT_EQ(-1, resources_tile_code_from_key("\\x8"));
    ASSERT_EQ(-1, resources_tile_code_from_key("\\x800"));
    ASSERT_EQ(-1, resources_tile_code_from_key("\\xgg"));
    PASS();
}

SUITE(unit_resources_suite) {
    RUN_TEST(tile_code_key_names_a_byte);
    RUN_TEST(troops_catalog_nonempty);
    RUN_TEST(spells_catalog_complete);
    RUN_TEST(classes_catalog_four);
    RUN_TEST(villains_count_seventeen);
    RUN_TEST(castle_footprint_string_parses);
    RUN_TEST(castles_default_to_the_3x2_footprint);
    RUN_TEST(siege_grid_defaults_to_empty);
}
