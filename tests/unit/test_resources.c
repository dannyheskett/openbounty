// Resource catalog sanity tests. The catalog is loaded once from
// game.json; these tests ensure the load produced something sane.

#include "greatest.h"
#include "tables.h"
#include "fixtures.h"
#include "resources.h"
#include "pack.h"

#include <string.h>

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

// The spawn pool roll. A five-troop pool must walk exactly as the old fixed walk
// did for every chance and tier; a six-troop pool with its own curve reaches its
// sixth slot and its new third slot.
static int old_walk(const int *curve, int chance) {
    int slot = 0;
    while (slot < 4 && chance > curve[slot]) slot++;
    return slot;
}

TEST spawn_five_troop_pool_walks_as_before(void) {
    ResSpawn sp;
    memset(&sp, 0, sizeof sp);
    static int curve[4][4] = { { 60, 90, 98, 101 }, { 20, 70, 95, 99 }, { 10, 20, 50, 90 }, { 3, 6, 10, 40 } };
    for (int t = 0; t < 4; t++) { sp.chance_curve[t] = curve[t]; sp.chance_curve_len[t] = 4; }
    for (int k = 0; k < 4; k++) sp.pool_count[k] = 5;
    for (int k = 0; k < 4; k++)
        for (int t = 0; t < 4; t++)
            for (int chance = 1; chance <= 100; chance++)
                ASSERT_EQ(old_walk(curve[t], chance), resources_spawn_slot(&sp, k, t, chance));
    PASS();
}

TEST spawn_six_troop_pool_uses_its_own_curve(void) {
    ResSpawn sp;
    memset(&sp, 0, sizeof sp);
    static int shared[4] = { 60, 90, 98, 101 };
    static int own[5] = { 60, 90, 94, 98, 101 };
    static char pool[6][RES_ID_LEN] = { "a", "b", "c", "d", "e", "f" };
    sp.chance_curve[0] = shared; sp.chance_curve_len[0] = 4;
    sp.pool_count[0] = 6; sp.troop_pool[0] = pool;
    sp.pool_count[1] = 5;
    sp.kind_curve_set[0] = true;
    sp.kind_curve[0][0] = own; sp.kind_curve_len[0][0] = 5;
    ASSERT_EQ(0, resources_spawn_slot(&sp, 0, 0, 60));
    ASSERT_EQ(1, resources_spawn_slot(&sp, 0, 0, 90));
    ASSERT_EQ(2, resources_spawn_slot(&sp, 0, 0, 94));    // the new third troop
    ASSERT_EQ(3, resources_spawn_slot(&sp, 0, 0, 98));
    ASSERT_EQ(4, resources_spawn_slot(&sp, 0, 0, 100));
    ASSERT_STR_EQ("c", resources_spawn_troop(&sp, 0, 0, 94));
    ASSERT_EQ(3, resources_spawn_slot(&sp, 1, 0, 99));    // a five-troop pool keeps the shared curve
    ASSERT_STR_EQ("", resources_spawn_troop(&sp, 1, 0, 99));   // that pool names no troops: none
    PASS();
}

// The catalog holds as many troops as the pack declares: a copy of the fixture
// pack with 40 troops appended loads every one of them.
static void copy_file(const char *from, const char *to) {
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    if (a && b) { char buf[8192]; size_t n; while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b); }
    if (a) fclose(a);
    if (b) fclose(b);
}

TEST catalog_has_no_troop_limit(void) {
    const char *dir = "/tmp/ob_manytroops_pack";
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/strings", dir, dir);
    ASSERT_EQ(0, system(cmd));
    copy_file("tests/fixtures/animpack/strings/en.json", "/tmp/ob_manytroops_pack/strings/en.json");
    // Rewrite troops[] with 40 extra entries after the fixture's own.
    FILE *f = fopen("tests/fixtures/animpack/game.json", "rb");
    ASSERT(f);
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *txt = malloc((size_t)len + 1);
    ASSERT(txt);
    ASSERT_EQ((size_t)len, fread(txt, 1, (size_t)len, f));
    txt[len] = '\0';
    fclose(f);
    char *at = strstr(txt, "\"troops\"");
    ASSERT(at);
    char *open = strchr(at, '[');
    ASSERT(open);
    FILE *o = fopen("/tmp/ob_manytroops_pack/game.json", "wb");
    ASSERT(o);
    fwrite(txt, 1, (size_t)(open - txt) + 1, o);
    for (int i = 0; i < 40; i++)
        fprintf(o, "{\"index\": %d, \"id\": \"extra_%d\", \"name\": \"Extra %d\", \"dwelling\": \"plains\"},", 1000 + i, i, i);
    fputs(open + 1, o);
    fclose(o);
    free(txt);

    Pack *p = pack_open(dir);
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    ASSERT(r);
    bool ok = resources_load(r, "game.json");
    int n = r->troops_count;
    bool last_ok = n > 40 && strcmp(r->troops[39].id, "extra_39") == 0;
    resources_free(r);
    free(r);
    pack_stack_pop();
    ASSERT(ok);
    ASSERT(n > 40);
    ASSERT(last_ok);
    PASS();
}

SUITE(unit_resources_suite) {
    RUN_TEST(catalog_has_no_troop_limit);
    RUN_TEST(spawn_five_troop_pool_walks_as_before);
    RUN_TEST(spawn_six_troop_pool_uses_its_own_curve);
    RUN_TEST(tile_code_key_names_a_byte);
    RUN_TEST(troops_catalog_nonempty);
    RUN_TEST(spells_catalog_complete);
    RUN_TEST(classes_catalog_four);
    RUN_TEST(villains_count_seventeen);
    RUN_TEST(castle_footprint_string_parses);
    RUN_TEST(castles_default_to_the_3x2_footprint);
    RUN_TEST(siege_grid_defaults_to_empty);
}
