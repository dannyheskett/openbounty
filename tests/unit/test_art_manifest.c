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
    // two cannot drift.
    int n = 0;
    const char *const *names = map_object_art_names(&n);
    ASSERT(names);
    ASSERT(n >= 14);
    bool gate = false;
    for (int i = 0; i < n; i++)
        if (strcmp(names[i], "castle_gate") == 0) gate = true;
    ASSERT(gate);
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
    RUN_TEST(a_pack_may_name_its_own_font);
}
