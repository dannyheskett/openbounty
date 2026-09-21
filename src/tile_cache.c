#include "tile_cache.h"
#include "gfx.h"
#include "assets.h"
#include "map.h"   // TILE_ART_NAME_LEN
#include "resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    char      name[TILE_ART_NAME_LEN];
    Texture2D tex;
} TileTex;

// Heap, sized by the pack: every tile image game.json can name (each
// tile_codes art and its variants, once for the shared set and once for each
// zone's own set), and grown if anything asks for more. Nothing is dropped.
static TileTex *cache;
static int      cache_n, cache_cap;

// Stamped by tile_cache_attach; nul until set. Loaders fall back to
// the legacy hardcoded layout when unset (tests that drive tile_cache
// without resources still work).
static const Resources *s_res = NULL;

static bool cache_reserve(int want) {
    if (want <= cache_cap) return true;
    int cap = cache_cap > 0 ? cache_cap : 64;
    while (cap < want) cap *= 2;
    TileTex *grown = (TileTex *)realloc(cache, (size_t)cap * sizeof *grown);
    if (!grown) return false;
    cache = grown;
    cache_cap = cap;
    return true;
}

void tile_cache_attach(const Resources *res) {
    s_res = res;
    if (!res) return;
    int arts = 0;
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++)
        if (res->tile_codes[i].present && res->tile_codes[i].art[0])
            arts += 1 + res->tile_codes[i].variant_count;
    int sets = 1;                                   // the shared art/tiles/
    for (int zi = 0; zi < res->zone_count; zi++) {
        if (!res->zones[zi].tile_set[0]) continue;
        bool dup = false;
        for (int k = 0; k < zi; k++)
            if (strcmp(res->zones[k].tile_set, res->zones[zi].tile_set) == 0) dup = true;
        if (!dup) sets++;
    }
    cache_reserve(arts * sets);
}

Texture2D tile_cache_get(const char *art) {
    if (!art || !art[0]) art = "grass";
    for (int i = 0; i < cache_n; i++)
        if (strcmp(cache[i].name, art) == 0) return cache[i].tex;
    if (!cache_reserve(cache_n + 1)) return (Texture2D){ 0 };
    TileTex *t = &cache[cache_n++];
    snprintf(t->name, sizeof t->name, "%s", art);   // the key is the name asked for
    char rel[160], path[256];
    // Tile art lives in art/tiles/*.png. A zone's own set ("<set>/<art>") draws its own file only for the arts it
    // overrides; every other name comes from the master art/tiles/ set.
    const char *slash = strchr(art, '/');
    if (slash && s_res) {
        char set[TILE_ART_NAME_LEN];
        snprintf(set, sizeof set, "%.*s", (int)(slash - art), art);
        if (!resources_tile_from_set(s_res, set, slash + 1)) art = slash + 1;
    }
    snprintf(rel, sizeof rel, "art/tiles/%s.png", art);
    resources_resolve_path(s_res, rel, path, sizeof path);
    t->tex = LoadAssetTexture(path);
    // POINT filter keeps pixel art crisp; CLAMP prevents color
    // bleed from the adjacent edge when the destination rect
    // lands at a sub-pixel position during animated camera scroll.
    gfx_texture_point_clamp(t->tex);
    return t->tex;
}

void tile_cache_shutdown(void) {
    for (int i = 0; i < cache_n; i++) gfx_texture_free(cache[i].tex);
    free(cache);
    cache = NULL;
    cache_n = cache_cap = 0;
}
