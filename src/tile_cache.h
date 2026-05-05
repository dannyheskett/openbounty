#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include "raylib.h"

// Lazy, fixed-size texture cache keyed by tile-art name. Each unique
// art string (e.g. "grass", "water", "castle_wall") is loaded from
// `<tiles_dir>/<art>.<tiles_extension>` (pack-relative) on first
// request, via the global pack stack. When the cache is full, later
// misses fall back to the "grass" slot.

#include "resources.h"

void      tile_cache_attach(const Resources *res);
void      tile_cache_shutdown(void);
Texture2D tile_cache_get(const char *art);

#endif
