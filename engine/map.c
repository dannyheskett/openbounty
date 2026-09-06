#include "map.h"
#include "assets_bytes.h"   // LoadAssetBytes / UnloadAssetBytes
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
const char *MapTerrainArt(const Map *map, const char *stem, char *out, size_t cap) {
    if (!out || cap == 0) return "";
    if (map && map->tile_set[0])
        snprintf(out, cap, "%s/%s", map->tile_set, stem ? stem : "");
    else
        snprintf(out, cap, "%s", stem ? stem : "");
    return out;
}

static bool fill_tile_from_code(const Map *map, Tile *t, const Resources *res,
                                unsigned char c) {
    if (c >= RES_TILE_CODE_COUNT) return false;
    const ResTileCode *tc = &res->tile_codes[c];
    if (!tc->present) return false;
    MapTerrainArt(map, tc->art, t->art, sizeof(t->art));
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

static void default_tile(const Map *map, Tile *t) {
    MapTerrainArt(map, "grass", t->art, sizeof(t->art));
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

// Skip over a comment line (starts with '#' -- blank ignored silently).
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
        fprintf(stdout, "MapLoadZone: cannot read %s\n", zone->map_path);
        return false;
    }

    map->width  = zone->width;
    map->height = zone->height;
    if (map->width > MAP_MAX_W || map->height > MAP_MAX_H) {
        fprintf(stdout, "MapLoadZone: %s too large: %dx%d\n",
                zone->id, map->width, map->height);
        UnloadAssetBytes(bytes);
        return false;
    }
    copy_string(map->name, sizeof(map->name), zone->id);
    copy_string(map->tile_set, sizeof(map->tile_set), zone->tile_set);
    map->hero_spawn_x = zone->hero_spawn_x;
    map->hero_spawn_y = zone->hero_spawn_y;
    map->navmap_x = map->navmap_y = -1;
    map->orb_x    = map->orb_y    = -1;

    for (int y = 0; y < map->height; y++)
        for (int x = 0; x < map->width; x++)
            default_tile(map, &map->tiles[y][x]);

    const char *p   = (const char *)bytes;
    const char *end = (const char *)bytes + sz;
    p = skip_blank_and_comments(p, end);

    for (int y = 0; y < map->height && p < end; y++) {
        int x = 0;
        while (p < end && *p != '\n' && *p != '\r' && x < map->width) {
            unsigned char c = (unsigned char)*p++;
            if (!fill_tile_from_code(map, &map->tiles[y][x], res, c)) {
                fprintf(stdout,
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

// The castle stamps (REQ-228), one table per footprint. stamp_objects paints
// from these and map_castle_art_names reads the art names off them, so the
// art manifest cannot drift from what the map draws.
//
// 3x2: a 3-wide x 2-tall block centred on the gate. The JSON-authored (x, y)
// is the gate at bottom-centre:
//
//   (x-1, y-1)=tl   (x, y-1)=br_top   (x+1, y-1)=tr
//   (x-1, y  )=ml   (x, y  )=GATE     (x+1, y  )=mr
//
// (The 'br' suffix is misleading -- it sits at the top-middle in the source
// art. We preserve the asset names.) The five wall tiles are decorative-only
// (no interactive flag) and block player movement; the gate carries the
// interactive flag.
//
// 1x1: the gate tile alone, drawn with the single `castle` art, so the castle
// sits on the map the way a town does.
typedef struct { int dx, dy; const char *art; bool gate; } CastlePart;

static const CastlePart CASTLE_3X2_PARTS[] = {
    { -1, -1, "castle_tl",   false },
    {  0, -1, "castle_br",   false },
    { +1, -1, "castle_tr",   false },
    { -1,  0, "castle_ml",   false },
    {  0,  0, "castle_gate", true  },
    { +1,  0, "castle_mr",   false },
};
static const CastlePart CASTLE_1X1_PARTS[] = {
    {  0,  0, "castle", true },
};

static const CastlePart *castle_parts(ResCastleFootprint fp, int *out_count) {
    if (fp == RES_CASTLE_FOOTPRINT_1X1) {
        *out_count = (int)(sizeof CASTLE_1X1_PARTS / sizeof CASTLE_1X1_PARTS[0]);
        return CASTLE_1X1_PARTS;
    }
    *out_count = (int)(sizeof CASTLE_3X2_PARTS / sizeof CASTLE_3X2_PARTS[0]);
    return CASTLE_3X2_PARTS;
}

// stamp_objects: paint all JSON-authored objects onto the map. This is
// the single source of truth for object placement -- the .dat tilemap is
// pure terrain and never carries object glyphs. Each branch sets BOTH
// `t->interactive` (gameplay) and `t->art` (rendering); the renderer in
// map_render.c looks up `t->art` to pick the sprite, so every
// branch must set it or the object renders as the underlying terrain.
static void stamp_objects(Map *map, const Resources *res, const ResZone *z,
                          const Game *game) {
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
        const ResTown *zt = resources_zone_town(res, z, i);
        if (!zt) continue;
        Tile *t = tile_at(map, zt->x, zt->y);
        if (!t) continue;
        t->interactive = INTERACT_TOWN;
        copy_string(t->id, sizeof(t->id), zt->id);
        t->boat_spawn_x = zt->boat_x;
        t->boat_spawn_y = zt->boat_y;
        // A town draws its own tile when the catalog entry names one
        // (`art`, a stem under art/tiles/), else the shared "town" tile.
        copy_string(t->art, sizeof(t->art), zt->art[0] ? zt->art : "town");
    }
    for (int i = 0; i < z->castle_count; i++) {
        // The footprint is the catalog entry's choice (REQ-228); a zone castle
        // with no catalog entry stamps the classic 3x2.
        int cx = z->castles[i].x;
        int cy = z->castles[i].y;
        const ResCastle *rc = resources_castle_by_id(res, z->castles[i].id);
        ResCastleFootprint fp = rc ? rc->footprint : RES_CASTLE_FOOTPRINT_3X2;
        int nparts = 0;
        const CastlePart *parts = castle_parts(fp, &nparts);
        for (int p = 0; p < nparts; p++) {
            Tile *t = tile_at(map, cx + parts[p].dx, cy + parts[p].dy);
            if (!t) continue;
            copy_string(t->art, sizeof(t->art), parts[p].art);
            if (parts[p].gate) {
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
        // A chest slot salted into a friendly foe is no longer a chest -- the
        // foe was placed here (or wandered off, or was recruited). Suppress the
        // chest so the foe isn't shadowed at spawn and no phantom chest is left
        // behind afterwards. stamp_placements then stamps the live foe here.
        if (GameFriendlyFoeOriginAt(game, z->id, z->chests[i].x, z->chests[i].y))
            continue;
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
    // Static z->armies[] are NOT stamped here -- they're registered as
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
    // All foes -- friendly and hostile -- stamped from the live FoeState
    // table. This is one-tile-type model (0x91); friendly vs
    // hostile is a per-foe attribute decided at attack time, not at
    // stamp time. Coordinates are read live so foes_follow movement
    // survives zone reload.
    for (int i = 0; i < game->foe_count; i++) {
        const FoeState *f = &game->foes[i];
        if (!f->alive) continue;
        if (strcmp(f->zone, zone_id) != 0) continue;
        MapStampFoe(map, f->x, f->y, f->placement_id);
    }
}

void MapStampFoe(Map *map, int x, int y, const char *placement_id) {
    Tile *t = tile_at(map, x, y);
    if (!t) return;
    // Never clobber a non-foe overlay (gate / dwelling / town / artifact); a
    // foe should never share a tile with one. The ONE exception is a treasure
    // chest: a chest coinciding with a live foe is always the foe's own origin
    // slot (a chest slot salted into a friendly foe), because foe_can_stand
    // forbids a foe from ever standing on a real chest. stamp_objects normally
    // suppresses that origin chest; this override is the belt-and-suspenders
    // guard for any path that stamped one anyway.
    bool spurious_chest = (t->interactive == INTERACT_TREASURE_CHEST);
    if (t->interactive != INTERACT_NONE && t->interactive != INTERACT_FOE &&
        !spurious_chest)
        return;
    t->interactive = INTERACT_FOE;
    copy_string(t->id, sizeof(t->id), placement_id);
    copy_string(t->art, sizeof(t->art), "wandering_army");
}

bool MapClearFoeStamp(Map *map, int x, int y) {
    // The clear-side half of MapStampFoe's overlay rule: a foe LEAVING a tile
    // (wander step, death) must only remove ITS OWN stamp. A foe can stand on
    // a non-foe interactive in exactly one way -- the hero-tile combat trigger
    // while the hero occupies an unconsumed pickup -- and an unguarded clear
    // there destroys the pickup with no consumed-ledger entry (an objective
    // that can never complete).
    if (!MapInBounds(map, x, y)) return false;
    Tile *t = &map->tiles[y][x];
    if (t->interactive != INTERACT_FOE) return false;
    MapClearInteractive(map, x, y);
    return true;
}

bool MapLoadZone(Map *map, const Resources *res, const char *zone_id) {
    return MapLoadZoneWithPlacements(map, res, zone_id, NULL);
}

bool MapLoadZoneWithPlacements(Map *map, const Resources *res,
                               const char *zone_id, const Game *game) {
    if (!map || !res || !zone_id) return false;
    const ResZone *zone = resources_zone_by_id(res, zone_id);
    if (!zone) {
        fprintf(stdout, "MapLoadZone: unknown zone id '%s'\n", zone_id);
        return false;
    }
    memset(map, 0, sizeof(*map));
    if (!load_dat(map, res, zone)) return false;
    stamp_objects(map, res, zone, game);
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
        MapTerrainArt(map, "water", t->art, sizeof(t->art));
    } else {
        MapTerrainArt(map, "grass", t->art, sizeof(t->art));
        t->terrain     = TERRAIN_GRASS;
        t->blocks_foot = false;
        t->is_bridge   = false;
    }
}

// Art names this module stamps onto tiles for placed objects (towns,
// dwellings, chests, signs, bridges, foes). They are NOT in game.json -- the
// engine chooses them by interact kind -- so resources_art_manifest() has to
// ask for them rather than duplicate the list and drift from it. Castle art
// depends on the footprint and is served by map_castle_art_names.
const char *const *map_object_art_names(int *out_count) {
    // "town" is not here: town art is per catalog entry (ResTown.art, default
    // "town") and the manifest lists it from the catalog.
    static const char *const NAMES[] = {
        "chest", "artifact_chest", "artifact_ring",
        "sign", "bridge_h", "bridge_v", "wandering_army",
        "dwelling_plains", "dwelling_forest", "dwelling_hills",
        "dwelling_dungeon",
    };
    if (out_count) *out_count = (int)(sizeof NAMES / sizeof NAMES[0]);
    return NAMES;
}

const char *const *map_castle_art_names(ResCastleFootprint fp, int *out_count) {
    // Filled from the stamp tables, so the names live in one place.
    static const char *names[2][8];
    int which = (fp == RES_CASTLE_FOOTPRINT_1X1) ? 1 : 0;
    int n = 0;
    const CastlePart *parts = castle_parts(fp, &n);
    for (int i = 0; i < n && i < 8; i++) names[which][i] = parts[i].art;
    if (out_count) *out_count = n;
    return names[which];
}
