// resources_art_manifest() is the single answer to "what art does this pack
// use". Art used to be reachable four different ways -- explicit game.json
// paths, bare tile_codes names, a list hardcoded in the shell, and villain
// frames derived from a portrait filename -- plus a fifth, the placed-object
// names map.c stamps by interact kind. Nothing could enumerate a pack.
//
// The invariant worth guarding is that every path it reports actually exists
// in the pack. A dangling entry means art the game will try to load and fail
// to find; a shrinking count means a category quietly stopped being reachable.

#include "greatest.h"
#include "resources.h"
#include "tables.h"
#include "map.h"
#include "pack.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

static char s_paths[RES_ART_MANIFEST_MAX][RES_PATH_LEN];

TEST every_manifest_path_exists_in_the_pack(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    int n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
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
    int n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
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
        if (strstr(p, "art/tiles/castle_gate.png")) object = true;
    }
    ASSERT(hero); ASSERT(combat); ASSERT(font); ASSERT(troop);
    ASSERT(villain); ASSERT(tile); ASSERT(object);
    resources_free(r); free(r);
    PASS();
}

TEST placed_object_names_are_asked_for_not_copied(void) {
    // map.c owns these names; the manifest must source them from there so the
    // two cannot drift. Castle art is served per footprint (REQ-228).
    int n = 0;
    const char *const *names = map_object_art_names(&n);
    ASSERT(names);
    ASSERT(n >= 10);
    for (int i = 0; i < n; i++) ASSERT(strcmp(names[i], "wandering_army") != 0);
    for (int i = 0; i < n; i++) ASSERT(strncmp(names[i], "castle", 6) != 0);
    for (int i = 0; i < n; i++) ASSERT(strcmp(names[i], "town") != 0);
    int n3 = 0;
    const char *const *c3 = map_castle_art_names(RES_CASTLE_FOOTPRINT_3X2, &n3);
    ASSERT_EQ(6, n3);
    bool gate = false;
    for (int i = 0; i < n3; i++)
        if (strcmp(c3[i], "castle_gate") == 0) gate = true;
    ASSERT(gate);
    int n1 = 0;
    const char *const *c1 = map_castle_art_names(RES_CASTLE_FOOTPRINT_1X1, &n1);
    ASSERT_EQ(1, n1);
    ASSERT_STR_EQ("castle", c1[0]);
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
    int n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/castle_gate.png"));
    ASSERT_FALSE(manifest_has(n, "art/tiles/castle.png"));

    for (int i = 0; i < r->castle_count; i++)
        r->castles[i].footprint = RES_CASTLE_FOOTPRINT_1X1;
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/castle.png"));
    ASSERT_FALSE(manifest_has(n, "art/tiles/castle_gate.png"));
    ASSERT_FALSE(manifest_has(n, "art/tiles/castle_tl.png"));

    r->castles[0].footprint = RES_CASTLE_FOOTPRINT_3X2;
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/castle.png"));
    ASSERT(manifest_has(n, "art/tiles/castle_gate.png"));
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
    int n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/grass.png"));
    ASSERT_FALSE(manifest_has(n, "art/tiles/alpha/grass.png"));

    strcpy(r->zones[0].tile_set, "alpha");
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/grass.png"));
    ASSERT(manifest_has(n, "art/tiles/alpha/grass.png"));
    ASSERT(manifest_has(n, "art/tiles/alpha/water_edge_01.png"));

    for (int i = 0; i < r->zone_count; i++) strcpy(r->zones[i].tile_set, "alpha");
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT_FALSE(manifest_has(n, "art/tiles/grass.png"));
    int alpha = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(s_paths[i], "art/tiles/alpha/grass.png") == 0) alpha++;
    ASSERT_EQ(1, alpha);
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
    int n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/town.png"));
    ASSERT_FALSE(manifest_has(n, "art/tiles/town_x.png"));
    strcpy(r->towns[0].art, "town_x");
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/town.png"));
    ASSERT(manifest_has(n, "art/tiles/town_x.png"));
    for (int i = 0; i < r->town_count; i++) strcpy(r->towns[i].art, "town_x");
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT_FALSE(manifest_has(n, "art/tiles/town.png"));
    int c = 0;
    for (int i = 0; i < n; i++) if (strcmp(s_paths[i], "art/tiles/town_x.png") == 0) c++;
    ASSERT_EQ(1, c);
    resources_free(r); free(r);
    PASS();
}

TEST army_art_is_listed_per_zone(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->zone_count >= 2);
    int n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/wandering_army.png"));
    strcpy(r->zones[0].army_art, "army_x");
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/tiles/wandering_army.png"));
    ASSERT(manifest_has(n, "art/tiles/army_x.png"));
    for (int i = 0; i < r->zone_count; i++) strcpy(r->zones[i].army_art, "army_x");
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT_FALSE(manifest_has(n, "art/tiles/wandering_army.png"));
    resources_free(r); free(r);
    PASS();
}

TEST class_hero_art_is_listed_when_declared(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT(r->classes_count >= 1);
    ASSERT(resources_class_hero(r, r->classes[0].id) == NULL);
    int n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT_FALSE(manifest_has(n, "art/classes/x_hero.png"));
    strcpy(r->class_hero[0].tile, "art/classes/x_hero.png");
    r->class_hero[0].walk.count[0] = 1;
    strcpy(r->class_hero[0].walk.frames[0][0], "art/classes/x_walk_00.png");
    ASSERT(resources_class_hero(r, r->classes[0].id) != NULL);
    n = resources_art_manifest(r, s_paths, RES_ART_MANIFEST_MAX);
    ASSERT(manifest_has(n, "art/classes/x_hero.png"));
    ASSERT(manifest_has(n, "art/classes/x_walk_00.png"));
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

SUITE(unit_art_manifest_suite) {
    RUN_TEST(every_manifest_path_exists_in_the_pack);
    RUN_TEST(manifest_covers_every_category);
    RUN_TEST(placed_object_names_are_asked_for_not_copied);
    RUN_TEST(castle_art_follows_the_footprint);
    RUN_TEST(terrain_art_is_listed_per_tile_set);
    RUN_TEST(town_art_is_listed_per_catalog_entry);
    RUN_TEST(army_art_is_listed_per_zone);
    RUN_TEST(class_hero_art_is_listed_when_declared);
    RUN_TEST(a_pack_may_name_its_own_font);
}
