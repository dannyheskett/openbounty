#ifndef OB_MAP_H
#define OB_MAP_H

#include <stdbool.h>
#include <stdint.h>
#include "tile.h"
#include <stddef.h>

#define TILE_ART_NAME_LEN  48   // "<tile_set>/<terrain art>" must fit
#define TILE_ID_LEN        24

#define TILE_SIGN_TITLE_LEN 48
#define TILE_SIGN_BODY_LEN  96

// A tile's text (art names, ids, signpost text) lives once in its Map's string
// pool; the tile holds small indices into it (0 = the empty string). A tile is
// then a few bytes, so the grid, and every autoplay snapshot of it, costs what
// the loaded map needs rather than the text copied into every cell. The pool
// grows as strings are added. Busiest shipped map (kings-bounty continentia):
// 252 strings, 3.8 KB of text.
typedef uint16_t MapStr;   // a tile field is 16 bits: up to 65535 distinct strings per map

typedef struct {
    MapStr   art;                      // sprite filename base (e.g. "water", "castle_roof")
    MapStr   ground;                   // the cell's OWN terrain art from the map (a road, an
                                       // edge piece); what an object stands on and what comes
                                       // back when the object is cleared (REQ-229f)
    MapStr   id;                       // optional named instance ("kings_castle"), 0 if none
    MapStr   sign_title;               // 0 if not a sign
    MapStr   sign_body;                // 0 if no body or not a sign
    uint8_t  terrain;                  // Terrain, derived from art at load time
    uint8_t  interactive;              // Interact, INTERACT_NONE if no overlay
    bool     blocks_foot;              // castle walls and similar visual blockers
    bool     is_bridge;                // bridge_h / bridge_v (walkable in both modes)
    int16_t  boat_spawn_x;  // for town tiles: where the rented boat appears; -1 if unset
    int16_t  boat_spawn_y;
} Tile;

typedef struct {
    int  width;
    int  height;
    char name[32];
    int  hero_spawn_x;
    int  hero_spawn_y;
    // Special coords set by salting. -1 if unset / not salted for this game.
    // Navmap and orb are hidden in chest tiles (0x8B) that don't change byte
    // but have semantic meaning: navmap unlocks the next continent,
    // orb is a crystal ball that reveals the whole continent.
    int  navmap_x;
    int  navmap_y;
    int  orb_x;
    int  orb_y;
    // The zone's terrain art folder ("" = the shared art/tiles/). Every
    // terrain art name stamped into a Tile goes through MapTerrainArt so
    // the prefix is applied in exactly one place.
    char tile_set[32];
    // The zone's wandering-army art stem ("wandering_army" unless the zone
    // declares `army_art`); every foe stamp reads it from here.
    char army_art[TILE_ART_NAME_LEN];
    // The string pool the tiles index (see Tile). Rebuilt by every load.
    int      str_count;                // strings in use, index 0 = ""
    int      pool_used;                // bytes of `pool` in use
    // Heap from here on; MAP_HEAD_BYTES is everything above. A Map starts
    // zeroed (calloc or `= { 0 }`) and is released with MapFree.
    int       str_cap;                 // entries allocated at str_off
    int       pool_cap;                // bytes allocated at pool
    uint32_t *str_off;
    char     *pool;
    Tile     *tiles;                   // width x height, row by row: tiles[y * width + x]
} Map;

#define MAP_HEAD_BYTES offsetof(Map, str_cap)

// The cell at (x, y), unchecked: callers have bounds-checked already.
#define MAP_TILE(m, x, y) ((m)->tiles[(y) * (m)->width + (x)])

// Size the map to width x height, every cell zero, with an empty string
// pool; anything it held before is released. False when out of memory.
bool MapAlloc(Map *map, int width, int height);
// Release the map's heap and zero it. Safe on a zeroed map.
void MapFree(Map *map);

// The string a tile field holds ("" for 0 or out of range).
const char *MapStrGet(const Map *map, MapStr s);
// The pool index of `s` in this map, adding it if new ("" and NULL are 0). A
// pool that runs out stops the program with a message: a silently dropped name
// would draw the wrong tile.
MapStr MapStrIntern(Map *map, const char *s);

// A tile's text, read and written through its map's pool.
static inline const char *TileArt(const Map *m, const Tile *t)       { return MapStrGet(m, t->art); }
static inline const char *TileGround(const Map *m, const Tile *t)    { return MapStrGet(m, t->ground); }
static inline const char *TileId(const Map *m, const Tile *t)        { return MapStrGet(m, t->id); }
static inline const char *TileSignTitle(const Map *m, const Tile *t) { return MapStrGet(m, t->sign_title); }
static inline const char *TileSignBody(const Map *m, const Tile *t)  { return MapStrGet(m, t->sign_body); }
static inline void TileSetArt(Map *m, Tile *t, const char *s)    { t->art = MapStrIntern(m, s); }
static inline void TileSetGround(Map *m, Tile *t, const char *s) { t->ground = MapStrIntern(m, s); }
static inline void TileSetId(Map *m, Tile *t, const char *s)     { t->id = MapStrIntern(m, s); }

// Write the art name for a terrain art stem in this map's tile set into
// `out`: "<tile_set>/<stem>" when the map declares a set, else the stem.
// Returns `out`.
const char *MapTerrainArt(const Map *map, const char *stem, char *out, size_t cap);

// Pull in Resources so the loader sees it. (It's a small header.)
#include "resources.h"

// Load a zone by id. Reads the .dat referenced by the zone's `map` field,
// resolves each byte through the `tile_codes` table, and stamps interactive
// overlays (signs, towns, castles, chests, artifacts, dwellings, armies)
// from the zone's per-instance object lists. Returns false on failure
// (missing zone id, unreadable .dat, unknown tile code).
bool MapLoadZone(Map *map, const Resources *res, const char *zone_id);

// Same as MapLoadZone, but after stamping the JSON-declared objects also
// replays every SaltedPlacement in the provided game struct whose zone
// matches zone_id. Use this whenever a Game is available; MapLoadZone is
// kept for callers that load maps without game state (map viewer, tests).
// `game` may be NULL, in which case this is equivalent to MapLoadZone.
struct Game;
bool MapLoadZoneWithPlacements(Map *map, const Resources *res,
                               const char *zone_id, const struct Game *game);

const Tile *MapGetTile(const Map *map, int x, int y);
bool MapInBounds(const Map *map, int x, int y);
bool MapWalkable(const Map *map, int x, int y);

// Remove the interactive overlay on a tile (artifact pickup, consumed chest,
// etc.). Also clears `art` and `id` so the tile renders as plain terrain.
// No-op if the coord is out of bounds.
void MapClearInteractive(Map *map, int x, int y);

// Stamp a live foe's tile with the INTERACT_FOE overlay (id = placement_id, art
// "wandering_army"). The single definition of "a foe occupies this tile" used by
// zone-load stamping and by foes-follow re-sync. No-op if out of bounds or if the
// tile already holds a DIFFERENT interactive (a chest/gate is never clobbered).
void MapStampFoe(Map *map, int x, int y, const char *placement_id);

// Remove a foe stamp -- and ONLY a foe stamp (the clear-side half of
// MapStampFoe's overlay rule). Returns true when an INTERACT_FOE overlay was
// cleared; false (no-op) when the tile holds anything else, so a foe passing
// over the hero's tile can never destroy an unconsumed pickup beneath.
bool MapClearFoeStamp(Map *map, int x, int y);


// The tile art names this module stamps for placed objects. These come from
// the engine's interact-kind mapping rather than from game.json, so callers
// enumerating a pack's art must include them. Returns a static array.
const char *const *map_object_art_names(int *out_count);

// The castle art for one footprint (REQ-228): the six pieces of the 3x2 stamp,
// or the single `castle` tile of a 1x1 castle. Read off the same tables
// stamp_objects paints from, so the manifest cannot drift from the map.
const char *const *map_castle_art_names(ResCastleFootprint fp, int *out_count);

#endif
