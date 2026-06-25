#include "tile.h"
#include <string.h>

static bool starts_with(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return false; }
    return true;
}

Terrain TerrainFromArt(const char *art) {
    if (!art) return TERRAIN_GRASS;
    if (strcmp(art, "water") == 0 || starts_with(art, "water_"))       return TERRAIN_WATER;
    if (strcmp(art, "forest") == 0 || starts_with(art, "forest_"))    return TERRAIN_FOREST;
    if (strcmp(art, "mountain") == 0 || starts_with(art, "mountain_"))return TERRAIN_MOUNTAIN;
    if (strcmp(art, "desert") == 0 || starts_with(art, "desert_"))    return TERRAIN_DESERT;
    // Castle parts, towns, dwellings, signs, chests, boat, artifacts, bridges: all on grass.
    return TERRAIN_GRASS;
}

bool ArtBlocksFoot(const char *art) {
    if (!art) return false;
    // Castle art tiles ( IS_CASTLE: 0x02-0x07) block pedestrian movement.
    // Our naming: castle_base_*, castle_wall_*, castle_variant_a.
    if (starts_with(art, "castle_")) return true;
    return false;
}

bool ArtIsBridge(const char *art) {
    if (!art) return false;
    return strcmp(art, "bridge_h") == 0 || strcmp(art, "bridge_v") == 0;
}

Interact InteractFromString(const char *s) {
    if (!s) return INTERACT_NONE;
    if (strcmp(s, "castle_gate")        == 0) return INTERACT_CASTLE_GATE;
    if (strcmp(s, "town")               == 0) return INTERACT_TOWN;
    if (strcmp(s, "treasure_chest")     == 0) return INTERACT_TREASURE_CHEST;
    if (strcmp(s, "chest")              == 0) return INTERACT_TREASURE_CHEST;
    if (strcmp(s, "sign")               == 0) return INTERACT_SIGN;
    if (strcmp(s, "artifact")           == 0) return INTERACT_ARTIFACT;
    if (strcmp(s, "dwelling_plains")    == 0) return INTERACT_DWELLING_PLAINS;
    if (strcmp(s, "dwelling_forest")    == 0) return INTERACT_DWELLING_FOREST;
    if (strcmp(s, "dwelling_hills")     == 0) return INTERACT_DWELLING_HILLS;
    if (strcmp(s, "dwelling_dungeon")   == 0) return INTERACT_DWELLING_DUNGEON;
    if (strcmp(s, "orb")                == 0) return INTERACT_ORB;
    if (strcmp(s, "telecave")           == 0) return INTERACT_TELECAVE;
    if (strcmp(s, "navmap")             == 0) return INTERACT_NAVMAP;
    if (strcmp(s, "foe")                == 0) return INTERACT_FOE;
    return INTERACT_NONE;
}

const char *InteractToString(Interact i) {
    switch (i) {
        case INTERACT_CASTLE_GATE:      return "castle_gate";
        case INTERACT_TOWN:             return "town";
        case INTERACT_TREASURE_CHEST:   return "treasure_chest";
        case INTERACT_SIGN:             return "sign";
        case INTERACT_ARTIFACT:         return "artifact";
        case INTERACT_DWELLING_PLAINS:  return "dwelling_plains";
        case INTERACT_DWELLING_FOREST:  return "dwelling_forest";
        case INTERACT_DWELLING_HILLS:   return "dwelling_hills";
        case INTERACT_DWELLING_DUNGEON: return "dwelling_dungeon";
        case INTERACT_ORB:              return "orb";
        case INTERACT_TELECAVE:         return "telecave";
        case INTERACT_NAVMAP:           return "navmap";
        case INTERACT_FOE:              return "foe";
        default:                        return "none";
    }
}

bool TerrainWalkable(Terrain t) {
    return t == TERRAIN_GRASS || t == TERRAIN_DESERT;
}

int TerrainMoveCost(Terrain t) {
    switch (t) {
        case TERRAIN_GRASS:  return 1;
        case TERRAIN_DESERT: return 40;   // authentic KB: ~1 tile per day
        default:             return 0;    // not walkable
    }
}

const char *TerrainName(Terrain t) {
    switch (t) {
        case TERRAIN_GRASS:    return "grass";
        case TERRAIN_FOREST:   return "forest";
        case TERRAIN_MOUNTAIN: return "mountain";
        case TERRAIN_WATER:    return "water";
        case TERRAIN_DESERT:   return "desert";
        default:               return "?";
    }
}
