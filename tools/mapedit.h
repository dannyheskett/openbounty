// openbounty-mapedit -- GUI map editor for pack zones.
//
// Edits the two files that define a zone's map:
//   maps/<zone>.dat      terrain, one byte per tile (this file's MapGrid)
//   game.json            the zone's object arrays (towns, castles, chests,
//                        signs, dwellings, armies)
//
// The .dat holds the FULLY RENDERED map. Edge variants are baked in on save
// (REQ-229); the engine computes nothing about appearance at load. The author
// paints base terrain only and never places a variant by hand.

#ifndef OB_MAPEDIT_H
#define OB_MAPEDIT_H

#include "map.h"
#include "resources.h"
#include "tile.h"

#include <stdbool.h>

#define MAPEDIT_MAX_W MAP_MAX_W
#define MAPEDIT_MAX_H MAP_MAX_H

// One authored tile. `terrain` is what the author paints; `variant` is
// derived by the furnish pass and is never edited directly.
//   variant < 0  -> the plain terrain tile
//   variant >= 0 -> <terrain>_edge_<variant> art
typedef struct {
    Terrain terrain;
    int     variant;
    // A pack may ship decorative alternates that are neither the plain tile
    // nor an edge variant -- kings-bounty's "grass_variant" is one. They carry
    // no neighbour semantics, so furnish leaves them alone; we keep the
    // original byte here and write it straight back, and clear it the moment
    // the author paints over the tile.
    char    decor;
} MapCell;

typedef struct {
    int     w, h;
    MapCell cell[MAPEDIT_MAX_H][MAPEDIT_MAX_W];
    char    zone_id[32];
    char    tile_set[32];   // the zone's terrain art folder, "" = shared
    char    dat_path[192];
    bool    dirty;
} MapGrid;

// --- furnishing (tools/mapedit_furnish.c) -----------------------------------

// Absorb tiles whose neighbour pattern has no legal edge variant. Returns the
// number of tiles changed. Run before mapedit_furnish().
int mapedit_despeckle(MapGrid *m);

// Assign every tile's edge variant from its neighbours. Returns the number of
// tiles given a variant; writes the count that had no legal variant (which
// should be zero after despeckle) to *unresolved_out when non-NULL.
int mapedit_furnish(MapGrid *m, int *unresolved_out);

// --- io (tools/mapedit_io.c) ------------------------------------------------

// Read a zone's .dat through the pack, resolving each byte to a terrain and
// variant via the pack's tile_codes table. Returns false and leaves *m
// untouched on failure.
bool mapedit_load(MapGrid *m, const Resources *res, const char *zone_id);

// Despeckle, furnish, then write the .dat back. The written file is the
// fully rendered map.
bool mapedit_save(MapGrid *m, const Resources *res);

// The tile-code byte for a (terrain, variant) pair, or 0 when the pack
// declares no such tile.
char mapedit_code_for(const Resources *res, Terrain t, int variant);

// The art stem for a (terrain, variant) pair, e.g. "water_edge_10". Returns
// a pointer to a static buffer.
const char *mapedit_art_for(Terrain t, int variant);

#endif
