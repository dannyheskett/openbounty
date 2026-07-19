#ifndef OB_TILE_H
#define OB_TILE_H

#include <stdbool.h>

typedef enum {
    TERRAIN_GRASS = 0,
    TERRAIN_FOREST,
    TERRAIN_MOUNTAIN,
    TERRAIN_WATER,
    TERRAIN_DESERT,
    TERRAIN_COUNT
} Terrain;

typedef enum {
    INTERACT_NONE = 0,
    INTERACT_CASTLE_GATE,
    INTERACT_TOWN,
    INTERACT_TREASURE_CHEST,
    INTERACT_SIGN,
    INTERACT_ARTIFACT,
    INTERACT_DWELLING_PLAINS,
    INTERACT_DWELLING_FOREST,
    INTERACT_DWELLING_HILLS,
    INTERACT_DWELLING_DUNGEON,
    INTERACT_ALCOVE,                  // Archmage Aurange's spell alcove
    INTERACT_ORB,
    INTERACT_TELECAVE,
    INTERACT_NAVMAP,
    // has one foe tile (0x91); friendly vs hostile is decided per-foe
    // at attack time by that foe's FoeState.friendly flag (game.h).
    INTERACT_FOE,
    INTERACT_COUNT
} Interact;

// Look up a terrain category from an art name (e.g. "water_edge_03" -> TERRAIN_WATER).
// Returns TERRAIN_GRASS for unknown names.
Terrain TerrainFromArt(const char *art);

// Whether the art visually blocks pedestrian movement (true for castle walls,
// in addition to natural blockers like trees/rocks which are handled via terrain).
bool ArtBlocksFoot(const char *art);

// Whether the art is a bridge (walkable in both walk and boat mode).
bool ArtIsBridge(const char *art);

// Parse an interactive string into the enum.
Interact InteractFromString(const char *s);
const char *InteractToString(Interact i);

bool TerrainWalkable(Terrain t);
int  TerrainMoveCost(Terrain t);

const char *TerrainName(Terrain t);

#endif
