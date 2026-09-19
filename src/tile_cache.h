#ifndef OB_TILE_CACHE_H
#define OB_TILE_CACHE_H

#include "raylib.h"

// Lazy texture cache keyed by tile-art name. Each unique art string (e.g.
// "grass", "water", "castle_wall") is loaded from `art/tiles/<art>.png`
// (pack-relative) on first request, via the global pack stack. The cache is
// heap, sized at attach from every tile image the pack's game.json names, and
// grows past that if asked: every image the game requests is kept.

#include "resources.h"

void      tile_cache_attach(const Resources *res);
void      tile_cache_shutdown(void);
Texture2D tile_cache_get(const char *art);

#endif
