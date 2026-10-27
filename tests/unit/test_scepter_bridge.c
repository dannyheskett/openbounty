// A drawn bridge passes the scepter on to the next plain tile (REQ-235,
// #117). An overlay pack adds a bridge tile code to King's Bounty and a
// tiny fifth zone whose map is
//     g B B B g
//     g g g g B
// (g grass, B a bridge): three bridges between two plain tiles on the first
// row, and a bridge as the very last tile so the pass-on has to wrap. Every
// seed that buries there must land on plain ground.

#include "greatest.h"
#include "fixtures.h"
#include "cJSON.h"
#include "game.h"
#include "map.h"
#include "pack.h"
#include "resources.h"
#include "tile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TINY_DIR "build/ob_bridge_pack"

static void set_item(cJSON *obj, const char *key, cJSON *item) {
    if (cJSON_GetObjectItem(obj, key)) cJSON_ReplaceItemInObject(obj, key, item);
    else cJSON_AddItemToObject(obj, key, item);
}

static cJSON *xy(int x, int y) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "x", x);
    cJSON_AddNumberToObject(o, "y", y);
    return o;
}

static bool write_tiny_pack(void) {
    size_t n = 0;
    const unsigned char *bytes = pack_stack_read("game.json", &n);
    if (!bytes) return false;
    char *text = malloc(n + 1);
    if (!text) return false;
    memcpy(text, bytes, n);
    text[n] = '\0';
    cJSON *d = cJSON_Parse(text);
    free(text);
    if (!d) return false;
    char grass = 0;
    cJSON *codes = cJSON_GetObjectItem(d, "tile_codes");
    cJSON *code;
    cJSON_ArrayForEach(code, codes) {
        cJSON *art = cJSON_IsObject(code) ? cJSON_GetObjectItem(code, "art") : code;
        if (cJSON_IsString(art) && strcmp(art->valuestring, "grass") == 0 &&
            code->string && strlen(code->string) == 1) { grass = code->string[0]; break; }
    }
    if (!grass) { cJSON_Delete(d); return false; }
    // The bridge code: grass terrain over a river, as the Rome pack declares it.
    cJSON *bridge = cJSON_CreateObject();
    cJSON_AddStringToObject(bridge, "art", "bridge_ew");
    cJSON_AddStringToObject(bridge, "terrain", "grass");
    cJSON_AddTrueToObject(bridge, "is_bridge");
    set_item(codes, "B", bridge);
    // The fifth zone.
    cJSON *zones = cJSON_GetObjectItem(d, "zones");
    cJSON *z = cJSON_Duplicate(cJSON_GetArrayItem(zones, 1), 1);
    set_item(z, "id", cJSON_CreateString("tiny"));
    set_item(z, "name", cJSON_CreateString("Tiny"));
    set_item(z, "map", cJSON_CreateString("maps/tiny.dat"));
    set_item(z, "width", cJSON_CreateNumber(5));
    set_item(z, "height", cJSON_CreateNumber(2));
    cJSON *nb = cJSON_CreateArray();
    cJSON_AddItemToArray(nb, cJSON_CreateString("tiny"));
    set_item(z, "neighbors", nb);
    set_item(z, "is_home", cJSON_CreateFalse());
    cJSON_DeleteItemFromObject(z, "magic_alcove");
    set_item(z, "hero_spawn", xy(0, 1));
    static const char *const lists[] = { "signs", "towns", "castles", "chests",
                                         "artifacts", "dwellings", "wandering_armies" };
    for (size_t k = 0; k < sizeof lists / sizeof *lists; k++)
        set_item(z, lists[k], cJSON_CreateArray());
    cJSON_AddItemToArray(zones, z);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/maps", TINY_DIR, TINY_DIR);
    if (system(cmd) != 0) { cJSON_Delete(d); return false; }
    char *out = cJSON_Print(d);
    cJSON_Delete(d);
    FILE *f = fopen(TINY_DIR "/game.json", "wb");
    if (!f || !out) { free(out); if (f) fclose(f); return false; }
    fputs(out, f);
    fclose(f);
    free(out);
    f = fopen(TINY_DIR "/maps/tiny.dat", "wb");
    if (!f) return false;
    fprintf(f, "%cBBB%c\n%c%c%c%cB\n", grass, grass, grass, grass, grass, grass);
    fclose(f);
    return true;
}

TEST a_drawn_bridge_passes_the_scepter_on(void) {
    // The fixture pack is the base of the stack (fx_load_resources opens it
    // when nothing is there); the overlay goes on top and comes off at the
    // end, so the suites after this one find the fixture where they left it.
    Resources *base = fx_load_resources();
    ASSERT(base);
    resources_free(base); free(base);
    ASSERT(write_tiny_pack());
    Pack *overlay = pack_open(TINY_DIR);
    ASSERT(overlay);
    pack_stack_push(overlay);
    Resources *res = calloc(1, sizeof *res);
    ASSERT(res);
    ASSERT(resources_load(res, "game.json"));
    ASSERT_EQ(5, res->zone_count);
    Map *m = calloc(1, sizeof *m);
    ASSERT(m);
    ASSERT(MapLoadZone(m, res, "tiny"));
    ASSERT(MapGetTile(m, 1, 0)->is_bridge);
    ASSERT(MapGetTile(m, 4, 1)->is_bridge);
    ASSERT_EQ(TERRAIN_GRASS, MapGetTile(m, 1, 0)->terrain);
    // Every seed whose zone draw lands on the tiny zone: buried, never on a
    // bridge, and the tile past the three bridges gets its share.
    Game *g = calloc(1, sizeof *g);
    ASSERT(g);
    int tiny = 0, past_bridges = 0;
    for (unsigned long seed = 1; seed <= 400; seed++) {
        GameFree(g);
        memset(g, 0, sizeof *g);
        g->res = res;
        g->seed = seed;
        GameInit(g, "Test", 0, 1, NULL);
        if (strcmp(g->scepter.zone, "tiny") != 0) continue;
        tiny++;
        ASSERT(g->scepter.x >= 0 && g->scepter.y >= 0);
        const Tile *t = MapGetTile(m, g->scepter.x, g->scepter.y);
        ASSERT(t);
        ASSERT_FALSE(t->is_bridge);
        ASSERT_EQ(TERRAIN_GRASS, t->terrain);
        if (g->scepter.x == 4 && g->scepter.y == 0) past_bridges++;
    }
    ASSERT(tiny >= 40);          // the zone draw is uniform over five zones
    ASSERT(past_bridges >= 1);   // draws 1..4 all end on the tile past the bridges
    GameFree(g); free(g);
    MapFree(m); free(m);
    resources_free(res); free(res);
    pack_stack_pop();   // the overlay only; the fixture pack stays
    PASS();
}

SUITE(unit_scepter_bridge_suite) {
    RUN_TEST(a_drawn_bridge_passes_the_scepter_on);
}
