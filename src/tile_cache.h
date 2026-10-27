#ifndef OB_TILE_CACHE_H
#define OB_TILE_CACHE_H

#include "ob_types.h"

// Lazy texture cache keyed by tile-art name. Each unique art string (e.g.
// "grass", "galliae/water", "chest") is loaded on first request, via the global
// pack stack: a map object from `art/objects/<art>.png`, terrain from
// `art/tiles/[<set>/]<art>.png` (pack-relative). NULL or "" asks for the pack's
// default tile (map_art "default"). The cache is
// heap, sized at attach from every tile image the pack's game.json names, and
// grows past that if asked: every image the game requests is kept.

#include "resources.h"

void      tile_cache_attach(const Resources *res);
void      tile_cache_shutdown(void);
Texture2D tile_cache_get(const char *art);

#endif
