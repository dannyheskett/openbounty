#ifndef OB_MAP_H
#define OB_MAP_H

#include <stdbool.h>
#include "tile.h"

#define MAP_MAX_W 64
#define MAP_MAX_H 64
#define TILE_ART_NAME_LEN  24
#define TILE_ID_LEN        24

#define TILE_SIGN_TITLE_LEN 48
#define TILE_SIGN_BODY_LEN  96

typedef struct {
    char     art[TILE_ART_NAME_LEN];   // sprite filename base (e.g. "water", "castle_roof")
    Terrain  terrain;                  // derived from art at load time
    Interact interactive;              // INTERACT_NONE if no overlay
    char     id[TILE_ID_LEN];          // optional named instance ("kings_castle"), empty if none
    bool     blocks_foot;              // castle walls and similar visual blockers
    bool     is_bridge;                // bridge_h / bridge_v (walkable in both modes)
    char     sign_title[TILE_SIGN_TITLE_LEN];  // empty if not a sign
    char     sign_body[TILE_SIGN_BODY_LEN];    // empty if no body or not a sign
    int      boat_spawn_x;  // for town tiles: where the rented boat appears; -1 if unset
    int      boat_spawn_y;
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
    Tile tiles[MAP_MAX_H][MAP_MAX_W];
} Map;

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

#endif
