// resources_art_manifest() is the single answer to "what art does this pack
// use". Art used to be reachable four different ways -- explicit game.json
// paths, bare tile_codes names, a list hardcoded in the shell, and villain
// frames derived from a portrait filename -- plus a fifth, the placed-object
// names map.c stamped by interact kind. Nothing could enumerate a pack. Those
// names are game.json's "map_art" now, so the pack declares every one.
//
// The invariant worth guarding is that every path it reports actually exists
// in the pack. A dangling entry means art the game will try to load and fail
// to find; a shrinking count means a category quietly stopped being reachable.

#include "greatest.h"
#include "resources.h"
#include "tables.h"
#include "map.h"
#include "combat.h"
#include "pack.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

// The manifest list, grown by resources_art_manifest and reused by every test.
static ResArtList s_list;
#define s_paths (s_list.path)

TEST every_manifest_path_exists_in_the_pack(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    int n = resources_art_manifest(r, &s_list);
    ASSERT(n > 0);
    for (int i = 0; i < n; i++) {
        size_t sz = 0;
        const unsigned char *b = pack_stack_read(s_paths[i], &sz);
        if (!b) FAILm(s_paths[i]);      // names the missing file
        ASSERT(sz > 0);
    }
    resources_free(r); free(r);
    PASS();
}

TEST manifest_covers_every_category(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    int n = resources_art_manifest(r, &s_list);
    // One probe per discovery mechanism, so a category going dark fails here
    // rather than silently shrinking the count.
    bool hero = false, combat = false, font = false, tile = false,
         troop = false, villain = false, object = false;
    for (int i = 0; i < n; i++) {
        const char *p = s_paths[i];
        if (strstr(p, "art/sprites/"))  hero    = true;
        if (strstr(p, "art/combat/"))   combat  = true;
        if (strstr(p, "art/font/"))     font    = true;
        if (strstr(p, "art/troops/"))   troop   = true;
        if (strstr(p, "art/villains/")) villain = true;
        if (strstr(p, "art/tiles/grass.png"))       tile   = true;
        if (strstr(p, "art/objects/castle_gate.png")) object = true;
    }
    ASSERT(hero); ASSERT(combat); ASSERT(font); ASSERT(troop);
    ASSERT(villain); ASSERT(tile); ASSERT(object);
    resources_free(r); free(r);
    PASS();
}

TEST placed_object_names_come_from_map_art(void) {
    // map.c stamps the names game.json's "map_art" declares, each a stem under
    // art/objects/; a pack that leaves one out gets the standard name.
    Resources *r = fx_load_resources();
    ASSERT(r);
    const ResMapArt *m = resources_map_art(r);
    ASSERT_STR_EQ("chest", m->chest);
    ASSERT_STR_EQ("castle_gate", m->castle_3x2[RES_CASTLE_GATE]);
    ASSERT_STR_EQ("castle_tm", m->castle_3x2[RES_CASTLE_TM]);
    ASSERT(resources_art_is_object(r, "chest"));
    ASSERT(resources_art_is_object(r, "castle_gate"));
    ASSERT_FALSE(resources_art_is_object(r, "grass"));
    ASSERT_STR_EQ("bridge_ew", resources_map_art(NULL)->bridge[0][0]);
    ASSERT_STR_EQ("castle", resources_map_art(NULL)->castle_1x1);
    int n = resources_art_manifest(r, &s_list);
    for (int i = 0; i < n; i++)
        ASSERT_FALSE(strncmp(s_paths[i], "art/tiles/castle", 16) == 0 || strcmp(s_paths[i], "art/tiles/chest.png") == 0);
    resources_free(r); free(r);
    PASS();
}

TEST every_rome_manifest_path_exists(void) {
    // Rome draws from four tile sets, map art overlays and art/objects/: the
    // manifest names each file from the folder the game loads it from, so a
    // sand edge only a province's set has is never asked of the master set.
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    int n = ok ? resources_art_manifest(r, &s_list) : 0;
    static char missing[RES_PATH_LEN];       // static: greatest prints it after we return
    missing[0] = 0;
    int objects = 0, sets = 0, overlays = 0;
    for (int i = 0; i < n; i++) {
        size_t sz = 0;
        if (!pack_stack_read(s_paths[i], &sz) && !missing[0]) snprintf(missing, sizeof missing, "%s", s_paths[i]);
        objects  += strncmp(s_paths[i], "art/objects/", 12) == 0;
        sets     += strncmp(s_paths[i], "art/tiles/africa/", 17) == 0;
        overlays += strstr(s_paths[i], "_apron_") != NULL;
    }
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();
    ASSERT(ok);
    if (missing[0]) FAILm(missing);
    ASSERT(objects > 20);
    ASSERT(sets > 100);
    ASSERT(overlays >= 24);
    PASS();
}

TEST rome_picker_columns_split_the_painting(void) {
    // The class painting's figures are not evenly spaced: the pack names
    // where each one after the first starts, one boundary per neighbour pair,
    // rising left to right inside the 256 px painting.
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    int cn = ok ? r->sprites.class_picker_column_count : -1;
    int cols[RES_PICKER_COLUMNS] = { 0 };
    if (ok) memcpy(cols, r->sprites.class_picker_columns, sizeof cols);
    int classes = ok ? r->classes_count : 0;
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();
    ASSERT(ok);
    ASSERT_EQ(classes - 1, cn);
    for (int i = 0; i < cn; i++) {
        ASSERT(cols[i] > (i ? cols[i - 1] : 0));
        ASSERT(cols[i] < 256);
    }
    PASS();
}

static bool manifest_has(int n, const char *path) {
    for (int i = 0; i < n; i++)
        if (strcmp(s_paths[i], path) == 0) return true;
    return false;
}

TEST castle_art_follows_the_footprint(void) {
    // A pack lists the six 3x2 pieces only while some castle stamps 3x2, and
    // the single `castle` tile only while some castle stamps 1x1 (REQ-228).
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->castle_count > 1);
    int n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/objects/castle_gate.png"));
    ASSERT_FALSE(manifest_has(n, "art/objects/castle.png"));

    for (int i = 0; i < r->castle_count; i++)
        r->castles[i].footprint = RES_CASTLE_FOOTPRINT_1X1;
    n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/objects/castle.png"));
    ASSERT_FALSE(manifest_has(n, "art/objects/castle_gate.png"));
    ASSERT_FALSE(manifest_has(n, "art/objects/castle_tm.png"));   // the decorations keep tl, tr, ml, mr

    r->castles[0].footprint = RES_CASTLE_FOOTPRINT_3X2;
    n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/objects/castle.png"));
    ASSERT(manifest_has(n, "art/objects/castle_gate.png"));
    resources_free(r); free(r);
    PASS();
}

TEST terrain_art_is_listed_per_tile_set(void) {
    // The shared art/tiles/ set is listed while some zone draws it; a zone
    // that declares "tile_set" adds art/tiles/<set>/<art>.png for every tile
    // code, once per distinct set; and when every zone declares a set the
    // shared names drop out, so a pack ships exactly the terrain it draws.
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->zone_count >= 2);
    int n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/tiles/grass.png"));
    ASSERT_FALSE(manifest_has(n, "art/tiles/alpha/grass.png"));

    strcpy(r->zones[0].tile_set, "alpha");
    n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/tiles/grass.png"));
    ASSERT(manifest_has(n, "art/tiles/alpha/grass.png"));
    ASSERT(manifest_has(n, "art/tiles/alpha/water_edge_nw.png"));

    for (int i = 0; i < r->zone_count; i++) strcpy(r->zones[i].tile_set, "alpha");
    n = resources_art_manifest(r, &s_list);
    ASSERT_FALSE(manifest_has(n, "art/tiles/grass.png"));
    int alpha = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(s_paths[i], "art/tiles/alpha/grass.png") == 0) alpha++;
    ASSERT_EQ(1, alpha);
    resources_free(r); free(r);
    PASS();
}

TEST a_tile_set_may_override_single_arts(void) {
    // A zone's "tile_set_arts" forks only the names it lists: those come from
    // art/tiles/<set>/, every other name from the master art/tiles/ set, and
    // the manifest asks the set folder for exactly the forked files.
    Resources *r = fx_load_resources();
    ASSERT(r);
    strcpy(r->zones[0].tile_set, "alpha");
    r->zones[0].tile_set_arts = calloc(2, sizeof *r->zones[0].tile_set_arts);
    ASSERT(r->zones[0].tile_set_arts);
    strcpy(r->zones[0].tile_set_arts[0], "grass");
    strcpy(r->zones[0].tile_set_arts[1], "forest");
    r->zones[0].tile_set_art_count = 2;
    ASSERT(resources_tile_from_set(r, "alpha", "grass"));
    ASSERT(resources_tile_from_set(r, "alpha", "forest"));
    ASSERT_FALSE(resources_tile_from_set(r, "alpha", "water"));
    ASSERT_FALSE(resources_tile_from_set(r, "beta", "grass"));
    int n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/tiles/alpha/grass.png"));
    ASSERT(manifest_has(n, "art/tiles/alpha/forest.png"));
    ASSERT_FALSE(manifest_has(n, "art/tiles/alpha/water.png"));
    ASSERT(manifest_has(n, "art/tiles/water.png"));        // the master set is still asked for
    ASSERT(manifest_has(n, "art/tiles/grass.png"));
    resources_free(r); free(r);
    PASS();
}

TEST town_art_is_listed_per_catalog_entry(void) {
    // The shared town tile is listed while some town lacks `art`; a town
    // that declares one adds its stem; when every town declares, the shared
    // tile drops out (REQ-228a).
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->town_count >= 2);
    int n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/objects/town.png"));
    ASSERT_FALSE(manifest_has(n, "art/objects/town_x.png"));
    strcpy(r->towns[0].art, "town_x");
    n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/objects/town.png"));
    ASSERT(manifest_has(n, "art/objects/town_x.png"));
    for (int i = 0; i < r->town_count; i++) strcpy(r->towns[i].art, "town_x");
    n = resources_art_manifest(r, &s_list);
    ASSERT_FALSE(manifest_has(n, "art/objects/town.png"));
    int c = 0;
    for (int i = 0; i < n; i++) if (strcmp(s_paths[i], "art/objects/town_x.png") == 0) c++;
    ASSERT_EQ(1, c);
    resources_free(r); free(r);
    PASS();
}

TEST army_art_is_listed_per_zone(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->zone_count >= 2);
    int n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/objects/wandering_army.png"));
    strcpy(r->zones[0].army_art, "army_x");
    n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/objects/wandering_army.png"));
    ASSERT(manifest_has(n, "art/objects/army_x.png"));
    for (int i = 0; i < r->zone_count; i++) strcpy(r->zones[i].army_art, "army_x");
    n = resources_art_manifest(r, &s_list);
    ASSERT_FALSE(manifest_has(n, "art/objects/wandering_army.png"));
    resources_free(r); free(r);
    PASS();
}

TEST class_hero_art_is_listed_when_declared(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->classes_count >= 1);
    ASSERT(resources_class_hero(r, r->classes[0].id) == NULL);
    int n = resources_art_manifest(r, &s_list);
    ASSERT_FALSE(manifest_has(n, "art/classes/x_hero.png"));
    strcpy(r->class_hero[0].tile, "art/classes/x_hero.png");
    free(r->class_hero[0].walk.frames[0]);   // resources_free frees this strip
    r->class_hero[0].walk.frames[0] = calloc(1, RES_PATH_LEN);
    ASSERT(r->class_hero[0].walk.frames[0]);
    r->class_hero[0].walk.count[0] = 1;
    strcpy(r->class_hero[0].walk.frames[0][0], "art/classes/x_walk_00.png");
    ASSERT(resources_class_hero(r, r->classes[0].id) != NULL);
    n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, "art/classes/x_hero.png"));
    ASSERT(manifest_has(n, "art/classes/x_walk_00.png"));
    resources_free(r); free(r);
    PASS();
}

TEST a_ttf_font_and_its_licence_are_in_the_manifest(void) {
    Resources *r = fx_load_resources();
    ASSERT(r != NULL);
    ASSERT_EQ(0, r->font.file[0]);                  // kings-bounty declares no TTF
    int before = resources_art_manifest(r, &s_list);
    snprintf(r->font.file, sizeof r->font.file, "art/font/Face.ttf");
    snprintf(r->font.license, sizeof r->font.license, "art/font/OFL.txt");
    int after = resources_art_manifest(r, &s_list);
    ASSERT_EQ(before + 2, after);
    bool ttf = false, lic = false;
    for (int i = 0; i < after; i++) {
        if (strcmp(s_paths[i], "art/font/Face.ttf") == 0) ttf = true;
        if (strcmp(s_paths[i], "art/font/OFL.txt") == 0) lic = true;
    }
    ASSERT(ttf); ASSERT(lic);
    resources_free(r); free(r);
    PASS();
}

TEST a_pack_may_name_its_own_font(void) {
    // The path was compiled into main.c. Defaulted, not hardcoded, now.
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->sprites.font[0] != '\0');
    ASSERT(r->sprites.palette[0] != '\0');
    ASSERT_EQ(RES_COMBAT_TILES, r->sprites.combat_count);
    resources_free(r); free(r);
    PASS();
}

TEST siege_grid_is_listed_only_when_declared(void) {
    // sprites.ui.siege_grid names a prefix; the manifest expands it to one
    // path per cell of the band plus board (REQ-165c). Absent, nothing.
    Resources *r = fx_load_resources();
    ASSERT(r);
    int n = resources_art_manifest(r, &s_list);
    for (int i = 0; i < n; i++)
        ASSERT_FALSE(strstr(s_paths[i], "art/combat/siege/"));
    char p[RES_PATH_LEN];
    ASSERT_FALSE(resources_siege_grid_path(r, 0, 0, p, sizeof p));
    ASSERT_EQ(0, (int)p[0]);

    strcpy(r->sprites.siege_grid, "art/combat/siege/cell");
    int m = resources_art_manifest(r, &s_list);
    ASSERT_EQ(n + COMBAT_W * (COMBAT_H + 1) - 6, m);
    ASSERT(manifest_has(m, "art/combat/siege/cell_0_0.png"));
    ASSERT(manifest_has(m, "art/combat/siege/cell_5_5.png"));
    // the per-code wall pieces (combat[5..10]) leave the manifest with the grid
    for (int i = 5; i <= 10; i++) ASSERT_FALSE(manifest_has(m, r->sprites.combat[i]));
    ASSERT(manifest_has(m, r->sprites.combat[11]));
    ASSERT(resources_siege_grid_path(r, 5, COMBAT_H, p, sizeof p));
    ASSERT_STR_EQ("art/combat/siege/cell_5_5.png", p);
    ASSERT_FALSE(resources_siege_grid_path(r, COMBAT_W, 0, p, sizeof p));
    ASSERT_FALSE(resources_siege_grid_path(r, 0, COMBAT_H + 1, p, sizeof p));
    resources_free(r); free(r);
    PASS();
}

TEST combat_field_tile_is_dropped_when_ground_is_terrain(void) {
    // sprites.ui.combat_ground "terrain": the hero's map tile is the combat
    // ground, so sprites.combat[0] is not part of the pack (REQ-165d).
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT_FALSE(resources_combat_ground_is_terrain(r));
    int n = resources_art_manifest(r, &s_list);
    ASSERT(manifest_has(n, r->sprites.combat[0]));
    strcpy(r->sprites.combat_ground, "terrain");
    ASSERT(resources_combat_ground_is_terrain(r));
    int m = resources_art_manifest(r, &s_list);
    ASSERT_EQ(n - 1, m);
    ASSERT_FALSE(manifest_has(m, r->sprites.combat[0]));
    ASSERT(manifest_has(m, r->sprites.combat[1]));
    resources_free(r); free(r);
    PASS();
}

SUITE(unit_art_manifest_suite) {
    RUN_TEST(combat_field_tile_is_dropped_when_ground_is_terrain);
    RUN_TEST(siege_grid_is_listed_only_when_declared);
    RUN_TEST(every_manifest_path_exists_in_the_pack);
    RUN_TEST(manifest_covers_every_category);
    RUN_TEST(placed_object_names_come_from_map_art);
    RUN_TEST(every_rome_manifest_path_exists);
    RUN_TEST(rome_picker_columns_split_the_painting);
    RUN_TEST(castle_art_follows_the_footprint);
    RUN_TEST(terrain_art_is_listed_per_tile_set);
    RUN_TEST(a_tile_set_may_override_single_arts);
    RUN_TEST(town_art_is_listed_per_catalog_entry);
    RUN_TEST(army_art_is_listed_per_zone);
    RUN_TEST(class_hero_art_is_listed_when_declared);
    RUN_TEST(a_pack_may_name_its_own_font);
    RUN_TEST(a_ttf_font_and_its_licence_are_in_the_manifest);
}
