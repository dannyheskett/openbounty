// No fixed limit on content. A pack far past every old compile-time cap --
// 40 spells, 12 classes, 40 villains, 30 artifacts, 40 castles, 40 towns,
// 12 zones, a 300x300 map and 600 chests in one zone -- loads, starts a game,
// saves, loads the save back, copies and frees.
//
// The pack is King's Bounty with a small overlay pushed on top of it: a
// rewritten game.json and the big map. Everything else (art, strings, the
// original maps) reads through from King's Bounty underneath.

#include "greatest.h"
#include "fixtures.h"
#include "cJSON.h"
#include "fog.h"
#include "game.h"
#include "map.h"
#include "pack.h"
#include "resources.h"
#include "savegame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIG_DIR   "/tmp/ob_nolimits_pack"
#define BIG_SAVE  "/tmp/ob_nolimits_save.json"
#define BIG_W     300
#define BIG_H     300
#define BIG_ARMIES 120   // hostile armies in one zone

// Set obj[key] = item, adding the key when absent.
static void set_item(cJSON *obj, const char *key, cJSON *item) {
    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddItemToObject(obj, key, item);
}

// Copy entry 0 of `arr` until it holds `want`, renaming each copy and giving
// it the next index. `zone` (when given) moves the copy to that zone.
static void grow_catalog(cJSON *arr, int want, const char *prefix, const char *zone) {
    cJSON *base = cJSON_GetArrayItem(arr, 0);
    for (int i = cJSON_GetArraySize(arr); i < want; i++) {
        cJSON *c = cJSON_Duplicate(base, 1);
        char id[32], name[48];
        snprintf(id, sizeof id, "%s_%d", prefix, i);
        snprintf(name, sizeof name, "Extra %s %d", prefix, i);
        set_item(c, "index", cJSON_CreateNumber(i));
        set_item(c, "id", cJSON_CreateString(id));
        set_item(c, "name", cJSON_CreateString(name));
        cJSON_DeleteItemFromObject(c, "puzzle_cell");   // the 5x5 puzzle is a rule
        if (zone) set_item(c, "zone", cJSON_CreateString(zone));
        if (cJSON_GetObjectItem(c, "x")) {
            set_item(c, "x", cJSON_CreateNumber(20 + i * 5));
            set_item(c, "y", cJSON_CreateNumber(100));
        }
        cJSON_AddItemToArray(arr, c);
    }
}

static cJSON *xy(int x, int y) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "x", x);
    cJSON_AddNumberToObject(o, "y", y);
    return o;
}

// Write the overlay pack. False on any failure.
static bool write_big_pack(void) {
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

    // The grass byte: the map is all grass.
    char grass = 0;
    cJSON *codes = cJSON_GetObjectItem(d, "tile_codes");
    cJSON *code;
    cJSON_ArrayForEach(code, codes) {
        cJSON *art = cJSON_IsObject(code) ? cJSON_GetObjectItem(code, "art") : code;
        if (cJSON_IsString(art) && strcmp(art->valuestring, "grass") == 0 &&
            code->string && strlen(code->string) == 1) {
            grass = code->string[0];
            break;
        }
    }
    if (!grass) { cJSON_Delete(d); return false; }

    // Eight zones of 300x300, one holding 600 chests.
    cJSON *zones = cJSON_GetObjectItem(d, "zones");
    for (int zi = 0; zi < 8; zi++) {
        cJSON *z = cJSON_Duplicate(cJSON_GetArrayItem(zones, 1), 1);
        char id[16], name[16], next[16];
        snprintf(id, sizeof id, "big%d", zi);
        snprintf(name, sizeof name, "Big %d", zi);
        snprintf(next, sizeof next, "big%d", (zi + 1) % 8);
        set_item(z, "id", cJSON_CreateString(id));
        set_item(z, "name", cJSON_CreateString(name));
        set_item(z, "map", cJSON_CreateString("maps/big.dat"));
        set_item(z, "width", cJSON_CreateNumber(BIG_W));
        set_item(z, "height", cJSON_CreateNumber(BIG_H));
        cJSON *nb = cJSON_CreateArray();
        cJSON_AddItemToArray(nb, cJSON_CreateString(next));
        set_item(z, "neighbors", nb);
        set_item(z, "is_home", cJSON_CreateFalse());
        cJSON_DeleteItemFromObject(z, "magic_alcove");
        set_item(z, "hero_spawn", xy(150, 150));
        static const char *const lists[] = { "signs", "towns", "castles", "chests",
                                             "artifacts", "dwellings", "wandering_armies" };
        for (size_t k = 0; k < sizeof lists / sizeof *lists; k++)
            set_item(z, lists[k], cJSON_CreateArray());
        if (zi == 0) {
            cJSON *chests = cJSON_GetObjectItem(z, "chests");
            for (int i = 0; i < 600; i++)
                cJSON_AddItemToArray(chests, xy(10 + i % 280, 10 + (i / 280) * 3));
            // Far past the 35 armies a zone once held: every one is raised.
            cJSON *armies = cJSON_GetObjectItem(z, "wandering_armies");
            for (int i = 0; i < BIG_ARMIES; i++)
                cJSON_AddItemToArray(armies, xy(10 + i % 280, 100 + (i / 280) * 3));
        }
        cJSON_AddItemToArray(zones, z);
    }
    grow_catalog(cJSON_GetObjectItem(d, "spells"), 40, "spell", NULL);
    grow_catalog(cJSON_GetObjectItem(d, "classes"), 12, "class", NULL);
    grow_catalog(cJSON_GetObjectItem(d, "villains"), 40, "villain", "big0");
    grow_catalog(cJSON_GetObjectItem(d, "artifacts"), 30, "artifact", "big0");
    grow_catalog(cJSON_GetObjectItem(d, "castles"), 40, "castle", "big1");
    grow_catalog(cJSON_GetObjectItem(d, "towns"), 40, "town", "big2");
    cJSON *towns = cJSON_GetObjectItem(d, "towns");
    for (int i = 26; i < cJSON_GetArraySize(towns); i++) {
        cJSON *t = cJSON_GetArrayItem(towns, i);
        set_item(t, "boat", xy(21 + i * 5, 101));
        set_item(t, "gate", xy(20 + i * 5, 101));
        cJSON_DeleteItemFromObject(t, "intel_castle");
    }

    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/maps", BIG_DIR, BIG_DIR);
    if (system(cmd) != 0) { cJSON_Delete(d); return false; }
    char *out = cJSON_Print(d);
    cJSON_Delete(d);
    FILE *f = fopen(BIG_DIR "/game.json", "wb");
    if (!f || !out) { free(out); if (f) fclose(f); return false; }
    fputs(out, f);
    fclose(f);
    free(out);
    f = fopen(BIG_DIR "/maps/big.dat", "wb");
    if (!f) return false;
    char row[BIG_W + 1];
    memset(row, grass, BIG_W);
    row[BIG_W] = '\n';
    for (int y = 0; y < BIG_H; y++) fwrite(row, 1, sizeof row, f);
    fclose(f);
    return true;
}

TEST pack_past_every_old_cap_plays_saves_and_loads(void) {
    // A stack of its own: King's Bounty, then the overlay.
    pack_stack_clear();
    Pack *kb = pack_open(FIXTURE_PACK_DIR);
    ASSERT(kb);
    pack_stack_push(kb);
    ASSERT(write_big_pack());
    Pack *overlay = pack_open(BIG_DIR);
    ASSERT(overlay);
    pack_stack_push(overlay);

    Resources *res = calloc(1, sizeof *res);
    Game *g = calloc(1, sizeof *g), *back = calloc(1, sizeof *back), *copy = calloc(1, sizeof *copy);
    Map *m = calloc(1, sizeof *m), *big = calloc(1, sizeof *big);
    Fog *f = calloc(1, sizeof *f), *f2 = calloc(1, sizeof *f2);
    const char *stage = "load";
    bool ok = res && g && back && copy && m && big && f && f2 && resources_load(res, "game.json");
    if (ok) {
        stage = "counts";
        ok = res->spells_count == 40 && res->classes_count == 12 &&
             res->villains_count == 40 && res->artifacts_count == 30 &&
             res->castle_count == 40 && res->town_count == 40 && res->zone_count == 12;
    }
    if (ok) {
        stage = "new game";
        g->res = res;
        GameInitSeeded(g, "Limitless", 11, 1, NULL, 3);   // the twelfth class
        ok = g->castle_count == 40 && g->town_count == 40 && g->spells.count == 40 &&
             g->contract.villain_count == 40 && g->artifacts.count == 30 &&
             g->world.zone_count == 12 && strcmp(g->character.cls.id, "class_11") == 0;
    }
    if (ok) {
        stage = "armies";
        int hostile = 0;
        for (int i = 0; i < g->foe_count; i++)
            if (strcmp(g->foes[i].zone, "big0") == 0 && !g->foes[i].friendly) hostile++;
        ok = hostile == BIG_ARMIES;
    }
    // The 300x300 zone with its 600 chests.
    if (ok) {
        stage = "big map";
        ok = MapLoadZone(big, res, "big0") && big->width == BIG_W &&
             MapGetTile(big, BIG_W - 1, BIG_H - 1) != NULL;
        int chests = 0;
        for (int y = 0; ok && y < BIG_H; y++)
            for (int x = 0; x < BIG_W; x++)
                if (MapGetTile(big, x, y)->interactive == INTERACT_TREASURE_CHEST) chests++;
        ok = ok && chests >= 600 - 21;   // salting may take chest slots for its objects
    }
    // Claim the last of everything, save, and read it back.
    if (ok) {
        stage = "save";
        g->contract.villains_caught[39] = true;
        g->artifacts.found[29] = true;
        g->spells.counts[39] = 7;
        g->castles[39].visited = true;
        g->towns[39].visited = true;
        ok = MapLoadZoneWithPlacements(m, res, g->position.zone, g) &&
             SaveGameWrite(BIG_SAVE, g, m, f) == SAVE_OK;
    }
    if (ok) {
        stage = "load save";
        back->res = res;
        ok = SaveGameRead(BIG_SAVE, back, m, f2) == SAVE_OK &&
             back->contract.villains_caught[39] && back->artifacts.found[29] &&
             back->spells.counts[39] == 7 && back->castles[39].visited &&
             back->towns[39].visited && strcmp(back->castles[39].id, "castle_39") == 0;
    }
    if (ok) {
        stage = "copy";
        ok = GameCopy(copy, back) && copy->castles != back->castles &&
             copy->castles[39].visited && copy->spells.counts[39] == 7 &&
             GameFingerprint(copy, 1u) == GameFingerprint(back, 1u);
    }

    GameFree(g); free(g); GameFree(back); free(back); GameFree(copy); free(copy);
    MapFree(m); free(m); MapFree(big); free(big);
    FogFree(f); free(f); FogFree(f2); free(f2);
    if (res) { resources_free(res); free(res); }
    pack_stack_clear();   // closes both
    remove(BIG_SAVE);
    ASSERTm(stage, ok);
    PASS();
}

SUITE(e2e_no_limits_suite) {
    RUN_TEST(pack_past_every_old_cap_plays_saves_and_loads);
}
