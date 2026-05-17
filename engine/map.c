#include "map.h"
#include "assets.h"
#include "resources.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < dst_size && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// Translate one character from the .dat into a Tile, using the tile_codes
// lookup in Resources. Returns false if the byte has no mapping.
static bool fill_tile_from_code(Tile *t, const Resources *res, unsigned char c) {
    if (c >= RES_TILE_CODE_COUNT) return false;
    const ResTileCode *tc = &res->tile_codes[c];
    if (!tc->present) return false;
    copy_string(t->art, sizeof(t->art), tc->art);
    t->terrain     = (Terrain)tc->terrain;
    t->blocks_foot = tc->blocks_foot;
    t->is_bridge   = tc->is_bridge;
    t->interactive = INTERACT_NONE;
    t->id[0]       = '\0';
    t->sign_title[0] = '\0';
    t->sign_body[0]  = '\0';
    t->boat_spawn_x  = -1;
    t->boat_spawn_y  = -1;
    return true;
}

static void default_tile(Tile *t) {
    copy_string(t->art, sizeof(t->art), "grass");
    t->terrain     = TERRAIN_GRASS;
    t->blocks_foot = false;
    t->is_bridge   = false;
    t->interactive = INTERACT_NONE;
    t->id[0]       = '\0';
    t->sign_title[0] = '\0';
    t->sign_body[0]  = '\0';
    t->boat_spawn_x  = -1;
    t->boat_spawn_y  = -1;
}

// Skip over a comment line (starts with '#' — blank ignored silently).
static const char *skip_blank_and_comments(const char *p, const char *end) {
    while (p < end) {
        if (*p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '#') {
            while (p < end && *p != '\n') p++;
            if (p < end) p++;
            continue;
        }
        break;
    }
    return p;
}

static bool load_dat(Map *map, const Resources *res, const ResZone *zone) {
    size_t sz = 0;
    const unsigned char *bytes = LoadAssetBytes(zone->map_path, &sz);
    if (!bytes) {
        fprintf(stderr, "MapLoadZone: cannot read %s\n", zone->map_path);
        return false;
    }

    map->width  = zone->width;
    map->height = zone->height;
    if (map->width > MAP_MAX_W || map->height > MAP_MAX_H) {
        fprintf(stderr, "MapLoadZone: %s too large: %dx%d\n",
                zone->id, map->width, map->height);
        UnloadAssetBytes(bytes);
        return false;
    }
    copy_string(map->name, sizeof(map->name), zone->id);
    map->hero_spawn_x = zone->hero_spawn_x;
    map->hero_spawn_y = zone->hero_spawn_y;
    map->navmap_x = map->navmap_y = -1;
    map->orb_x    = map->orb_y    = -1;

    for (int y = 0; y < map->height; y++)
        for (int x = 0; x < map->width; x++)
            default_tile(&map->tiles[y][x]);

    const char *p   = (const char *)bytes;
    const char *end = (const char *)bytes + sz;
    p = skip_blank_and_comments(p, end);

    for (int y = 0; y < map->height && p < end; y++) {
        int x = 0;
        while (p < end && *p != '\n' && *p != '\r' && x < map->width) {
            unsigned char c = (unsigned char)*p++;
            if (!fill_tile_from_code(&map->tiles[y][x], res, c)) {
                fprintf(stderr,
                        "MapLoadZone: %s:%d:%d unknown tile code 0x%02x '%c'\n",
                        zone->map_path, y + 1, x + 1, c,
                        (c >= 0x20 && c < 0x7F) ? c : '?');
                UnloadAssetBytes(bytes);
                return false;
            }
            x++;
        }
        // Pad short rows with default grass (already set above).
        // Consume the rest of the line (handles trailing chars gracefully).
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') p++;
    }

    UnloadAssetBytes(bytes);
    return true;
}

// Find a tile and stamp an interactive overlay onto it. Silent no-op if out
// of bounds (per-zone object lists may outlive edits to the .dat).
static Tile *tile_at(Map *map, int x, int y) {
    if (!MapInBounds(map, x, y)) return NULL;
    return &map->tiles[y][x];
}

// stamp_objects: paint all JSON-authored objects onto the map. This is
// the single source of truth for object placement — the .dat tilemap is
// pure terrain and never carries object glyphs. Each branch sets BOTH
// `t->interactive` (gameplay) and `t->art` (rendering); the renderer in
// map_render.c looks up `t->art` to pick the sprite, so every
// branch must set it or the object renders as the underlying terrain.
static void stamp_objects(Map *map, const ResZone *z) {
    for (int i = 0; i < z->sign_count; i++) {
        Tile *t = tile_at(map, z->signs[i].x, z->signs[i].y);
        if (!t) continue;
        t->interactive = INTERACT_SIGN;
        copy_string(t->id,         sizeof(t->id),         z->signs[i].id);
        copy_string(t->sign_title, sizeof(t->sign_title), z->signs[i].title);
        copy_string(t->sign_body,  sizeof(t->sign_body),  z->signs[i].body);
        copy_string(t->art,        sizeof(t->art),        "sign");
    }
    for (int i = 0; i < z->town_count; i++) {
        Tile *t = tile_at(map, z->towns[i].x, z->towns[i].y);
        if (!t) continue;
        t->interactive = INTERACT_TOWN;
        copy_string(t->id, sizeof(t->id), z->towns[i].id);
        t->boat_spawn_x = z->towns[i].boat_x;
        t->boat_spawn_y = z->towns[i].boat_y;
        copy_string(t->art, sizeof(t->art), "town");
    }
    for (int i = 0; i < z->castle_count; i++) {
        // Castle is a 3-wide × 2-tall block centered on the gate. The
        // JSON-authored (x, y) is the gate at bottom-center:
        //
        //   (x-1, y-1)=tl   (x, y-1)=br_top   (x+1, y-1)=tr
        //   (x-1, y  )=ml   (x, y  )=GATE     (x+1, y  )=mr
        //
        // (The 'br' suffix is misleading -- it sits at the top-middle in
        // the source art. We preserve the asset names.)
        // The 5 wall tiles are decorative-only (no interactive flag) and
        // block player movement; the gate carries the interactive flag.
        int cx = z->castles[i].x;
        int cy = z->castles[i].y;
        struct { int dx, dy; const char *art; } parts[6] = {
            { -1, -1, "castle_tl" },
            {  0, -1, "castle_br" },
            { +1, -1, "castle_tr" },
            { -1,  0, "castle_ml" },
            {  0,  0, "castle_gate" },
            { +1,  0, "castle_mr" },
        };
        for (int p = 0; p < 6; p++) {
            Tile *t = tile_at(map, cx + parts[p].dx, cy + parts[p].dy);
            if (!t) continue;
            copy_string(t->art, sizeof(t->art), parts[p].art);
            if (p == 4) {
                // Gate: interactive entry point.
                t->interactive = INTERACT_CASTLE_GATE;
                copy_string(t->id, sizeof(t->id), z->castles[i].id);
                t->blocks_foot = false;
            } else {
                // Wall: decorative scenery, blocks the player.
                t->blocks_foot = true;
            }
        }
        // Optional decorations: extra wall pieces declared per-castle in
        // JSON (used by the home castle for its surrounding mini-tower
        // complex). Same blocks_foot semantics as the standard walls;
        // never interactive.
        for (int p = 0; p < z->castles[i].decor_count; p++) {
            const ResCastleDecor *d = &z->castles[i].decorations[p];
            Tile *t = tile_at(map, cx + d->dx, cy + d->dy);
            if (!t || !d->art[0]) continue;
            copy_string(t->art, sizeof(t->art), d->art);
            t->blocks_foot = true;
        }
    }
    for (int i = 0; i < z->chest_count; i++) {
        Tile *t = tile_at(map, z->chests[i].x, z->chests[i].y);
        if (!t) continue;
        t->interactive = INTERACT_TREASURE_CHEST;
        copy_string(t->id, sizeof(t->id), z->chests[i].id);
        copy_string(t->art, sizeof(t->art), "chest");
    }
    for (int i = 0; i < z->artifact_count; i++) {
        Tile *t = tile_at(map, z->artifacts[i].x, z->artifacts[i].y);
        if (!t) continue;
        t->interactive = INTERACT_ARTIFACT;
        copy_string(t->id, sizeof(t->id), z->artifacts[i].id);
        // Both artifact tile bytes (0x92/0x93) display the same
        // chest-style art; the artifact identity is reveal-on-pickup,
        // not from the world tile.
        copy_string(t->art, sizeof(t->art), "artifact_chest");
    }
    for (int i = 0; i < z->dwelling_count; i++) {
        Tile *t = tile_at(map, z->dwellings[i].x, z->dwellings[i].y);
        if (!t) continue;
        const char *k = z->dwellings[i].kind;
        if      (strcmp(k, "plains")  == 0) {
            t->interactive = INTERACT_DWELLING_PLAINS;
            copy_string(t->art, sizeof(t->art), "dwelling_plains");
        } else if (strcmp(k, "forest")  == 0) {
            t->interactive = INTERACT_DWELLING_FOREST;
            copy_string(t->art, sizeof(t->art), "dwelling_forest");
        } else if (strcmp(k, "hills")   == 0) {
            t->interactive = INTERACT_DWELLING_HILLS;
            copy_string(t->art, sizeof(t->art), "dwelling_hills");
        } else if (strcmp(k, "dungeon") == 0) {
            t->interactive = INTERACT_DWELLING_DUNGEON;
            copy_string(t->art, sizeof(t->art), "dwelling_dungeon");
        }
        copy_string(t->id, sizeof(t->id), z->dwellings[i].id);
    }
    // Archmage Aurange's alcove. Rendered with the hills-dwelling sprite
    // (the alcove reuses the hill-cave art). Walking here triggers the
    // spell-teaching flow in step.c. The interactive flag also lets
    // render code distinguish alcove tiles from regular hills dwellings
    // if it ever wants to differentiate.
    if (z->magic_alcove_x >= 0 && z->magic_alcove_y >= 0) {
        Tile *t = tile_at(map, z->magic_alcove_x, z->magic_alcove_y);
        if (t) {
            t->interactive = INTERACT_ALCOVE;
            copy_string(t->art, sizeof(t->art), "dwelling_hills");
            copy_string(t->id,  sizeof(t->id),  "alcove");
            // The alcove sits on a mountain-edge tile; force it walkable
            // so the player can step on it (the sprite implies a passable
            // cave entrance regardless of the underlying terrain).
            // Mirrors how castle gates override their decorative wall
            // surroundings.
            t->blocks_foot = false;
        }
    }
    // Static z->armies[] are NOT stamped here — they're registered as
    // hostile foes in g->foes[] at GameInit time, and stamped via the
    // unified foe path in stamp_placements().
}

// Map a salted-placement kind to the tile art it should display.
// tile-byte parity:
//   - Navmap and orb keep the chest appearance (byte 0x8B stays); the
//     player discovers what they are by stepping on them.
//   - Telecave has a distinct byte (0x8E); we use dwelling_dungeon as a
//     visually cave-like stand-in until a dedicated telecave sprite exists.
//   - Salt-placed dwellings paint their kind-specific art; the .dat is
//     pure terrain so we can't inherit a glyph here.
//   - Salt-placed artifacts use the same chest art as JSON artifacts
//     (per , both tile bytes look alike).
static const char *placement_art(int kind) {
    switch (kind) {
        case INTERACT_TELECAVE:         return "dwelling_dungeon";
        case INTERACT_NAVMAP:           return "chest";
        case INTERACT_ORB:              return "chest";
        case INTERACT_ARTIFACT:         return "artifact_chest";
        case INTERACT_DWELLING_PLAINS:  return "dwelling_plains";
        case INTERACT_DWELLING_FOREST:  return "dwelling_forest";
        case INTERACT_DWELLING_HILLS:   return "dwelling_hills";
        case INTERACT_DWELLING_DUNGEON: return "dwelling_dungeon";
        default: return NULL;
    }
}

static void stamp_placements(Map *map, const Game *game, const char *zone_id) {
    if (!game || !zone_id) return;
    // Non-foe salted placements (artifacts, navmaps, orbs, telecaves,
    // dwellings).
    for (int i = 0; i < game->placement_count; i++) {
        const SaltedPlacement *p = &game->placements[i];
        if (strcmp(p->zone, zone_id) != 0) continue;
        Tile *t = tile_at(map, p->x, p->y);
        if (!t) continue;
        t->interactive = (Interact)p->kind;
        copy_string(t->id, sizeof(t->id), p->id);
        const char *art = placement_art(p->kind);
        if (art) copy_string(t->art, sizeof(t->art), art);
    }
    // All foes — friendly and hostile — stamped from the live FoeState
    // table. This is one-tile-type model (0x91); friendly vs
    // hostile is a per-foe attribute decided at attack time, not at
    // stamp time. Coordinates are read live so foes_follow movement
    // survives zone reload.
    for (int i = 0; i < game->foe_count; i++) {
        const FoeState *f = &game->foes[i];
        if (!f->alive) continue;
        if (strcmp(f->zone, zone_id) != 0) continue;
        Tile *t = tile_at(map, f->x, f->y);
        if (!t) continue;
        t->interactive = INTERACT_FOE;
        copy_string(t->id, sizeof(t->id), f->placement_id);
        copy_string(t->art, sizeof(t->art), "wandering_army");
    }
}

bool MapLoadZone(Map *map, const Resources *res, const char *zone_id) {
    return MapLoadZoneWithPlacements(map, res, zone_id, NULL);
}

bool MapLoadZoneWithPlacements(Map *map, const Resources *res,
                               const char *zone_id, const Game *game) {
    if (!map || !res || !zone_id) return false;
    const ResZone *zone = resources_zone_by_id(res, zone_id);
    if (!zone) {
        fprintf(stderr, "MapLoadZone: unknown zone id '%s'\n", zone_id);
        return false;
    }
    memset(map, 0, sizeof(*map));
    if (!load_dat(map, res, zone)) return false;
    stamp_objects(map, zone);
    stamp_placements(map, game, zone_id);
    return true;
}

const Tile *MapGetTile(const Map *map, int x, int y) {
    if (!MapInBounds(map, x, y)) return NULL;
    return &map->tiles[y][x];
}

bool MapInBounds(const Map *map, int x, int y) {
    return x >= 0 && y >= 0 && x < map->width && y < map->height;
}

bool MapWalkable(const Map *map, int x, int y) {
    const Tile *t = MapGetTile(map, x, y);
    if (!t) return false;
    return TerrainWalkable(t->terrain);
}

void MapClearInteractive(Map *map, int x, int y) {
    if (!MapInBounds(map, x, y)) return;
    Tile *t = &map->tiles[y][x];
    t->interactive = INTERACT_NONE;
    t->id[0] = '\0';
    // Revert to plain walkable terrain. sets consumed tiles to
    // byte 0x00 (grass), so the tile becomes passable regardless of
    // what it used to be underneath (dwellings often sit on mountain
    // edges, alcoves on mountain-variant tiles, etc.). Water stays
    // water so picked-up floating interactives don't become walkable.
    if (t->terrain == TERRAIN_WATER) {
        copy_string(t->art, sizeof(t->art), "water");
    } else {
        copy_string(t->art, sizeof(t->art), "grass");
        t->terrain     = TERRAIN_GRASS;
        t->blocks_foot = false;
        t->is_bridge   = false;
    }
}
