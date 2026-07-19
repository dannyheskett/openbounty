#include "tile_cache.h"
#include "assets.h"
#include "map.h"   // TILE_ART_NAME_LEN
#include "resources.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define MAX_TILE_TEXTURES 96

typedef struct {
    char      name[TILE_ART_NAME_LEN];
    Texture2D tex;
    bool      loaded;
} TileTex;

static TileTex cache[MAX_TILE_TEXTURES];

// Stamped by tile_cache_attach; nul until set. Loaders fall back to
// the legacy hardcoded layout when unset (tests that drive tile_cache
// without resources still work).
static const Resources *s_res = NULL;
void tile_cache_attach(const Resources *res) { s_res = res; }

Texture2D tile_cache_get(const char *art) {
    if (!art || !art[0]) art = "grass";
    for (int i = 0; i < MAX_TILE_TEXTURES; i++) {
        if (cache[i].loaded && strcmp(cache[i].name, art) == 0) return cache[i].tex;
    }
    for (int i = 0; i < MAX_TILE_TEXTURES; i++) {
        if (!cache[i].loaded) {
            char rel[160], path[256];
            // Fixed pack layout: tile art lives in art/tiles/*.png. The
            // extractor produces this layout; engine has no reason to
            // make it configurable.
            snprintf(rel, sizeof rel, "art/tiles/%s.png", art);
            resources_resolve_path(s_res, rel, path, sizeof path);
            cache[i].tex = LoadAssetTexture(path);
            // POINT filter keeps pixel art crisp; CLAMP prevents color
            // bleed from the adjacent edge when the destination rect
            // lands at a sub-pixel position during animated camera scroll.
            SetTextureFilter(cache[i].tex, TEXTURE_FILTER_POINT);
            SetTextureWrap(cache[i].tex, TEXTURE_WRAP_CLAMP);
            size_t n = 0;
            while (n + 1 < sizeof(cache[i].name) && art[n]) {
                cache[i].name[n] = art[n]; n++;
            }
            cache[i].name[n] = '\0';
            cache[i].loaded = true;
            return cache[i].tex;
        }
    }
    return tile_cache_get("grass");
}

void tile_cache_shutdown(void) {
    for (int i = 0; i < MAX_TILE_TEXTURES; i++) {
        if (cache[i].loaded) {
            UnloadTexture(cache[i].tex);
            cache[i].loaded = false;
        }
    }
}
