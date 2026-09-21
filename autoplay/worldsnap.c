// autoplay/worldsnap.c -- atomic attempt snapshot/rollback (AP-030..AP-033).

#include "worldsnap.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pending.h"
#include "spells_adventure.h"

// Calendar-death latch: set when a restore discards a world that died on the
// calendar; never cleared by a restore (it is outside Game). See worldsnap.h.
static bool s_calendar_dead = false;

void worldsnap_reset_calendar_dead(void) { s_calendar_dead = false; }
bool worldsnap_calendar_dead(void) { return s_calendar_dead; }

void worldsnap_release(WorldSnapshot *snap) {
    if (!snap) return;
    free(snap->map_bytes);
    snap->map_bytes = NULL;
    snap->map_cap = 0;
    GameFree(&snap->game);
    FogFree(&snap->fog);
}

static size_t map_tile_count(const Map *map) {
    if (map->width <= 0 || map->height <= 0 || !map->tiles) return 0;
    return (size_t)map->width * (size_t)map->height;
}

void worldsnap_capture(WorldSnapshot *snap, const Game *g, const Map *map,
                       const Fog *fog) {
    if (!snap || !g || !map || !fog) return;
    size_t cells = map_tile_count(map);
    size_t nstr  = map->str_off ? (size_t)map->str_count : 0;
    size_t npool = map->pool ? (size_t)map->pool_used : 0;
    size_t need = MAP_HEAD_BYTES + nstr * sizeof(uint32_t) + npool + cells * sizeof(Tile);
    if (snap->map_cap < need) {
        unsigned char *nb = (unsigned char *)realloc(snap->map_bytes, need);
        if (!nb) {
            fprintf(stdout, "worldsnap: out of memory (%zu bytes)\n", need);
            abort();
        }
        snap->map_bytes = nb;
        snap->map_cap = need;
    }
    // Header, then the string offsets, the pool text and the tiles, each at the
    // length the header records.
    unsigned char *p = snap->map_bytes;
    Map head;
    memcpy(&head, map, MAP_HEAD_BYTES);
    head.str_count = (int)nstr;
    head.pool_used = (int)npool;
    if (!cells) head.width = head.height = 0;
    memcpy(p, &head, MAP_HEAD_BYTES);                 p += MAP_HEAD_BYTES;
    if (nstr)  { memcpy(p, map->str_off, nstr * sizeof(uint32_t)); p += nstr * sizeof(uint32_t); }
    if (npool) { memcpy(p, map->pool, npool);         p += npool; }
    if (cells) memcpy(p, map->tiles, cells * sizeof(Tile));
    if (!GameCopy(&snap->game, g)) {
        fprintf(stdout, "worldsnap: out of memory copying the game\n");
        abort();
    }
    if (!FogCopy(&snap->fog, fog)) {
        fprintf(stdout, "worldsnap: out of memory copying the fog\n");
        abort();
    }
    snap->rng = GameRngSnapshot();
    ledger_snap(&snap->ledger);
}

void worldsnap_restore(const WorldSnapshot *snap, Game *g, Map *map, Fog *fog) {
    if (!snap || !g || !map || !fog) return;
    // Latch BEFORE the overwrite: the world being discarded is the live one.
    // If it died on the calendar, the restore is about to resurrect the run
    // (positive days_left, game_over cleared) -- record the death so the planner
    // stops instead of continuing on a calendar a larger budget still has.
    // Narrow: latch only when the world being RESTORED TO (the committed
    // baseline) is itself out of days -- not when a rolled-back speculative
    // attempt merely drove its scratch copy to day 0.
    if (snap->game.stats.game_over && snap->game.stats.days_left == 0 &&
        !snap->game.stats.won)
        s_calendar_dead = true;
    if (!GameCopy(g, &snap->game)) {
        fprintf(stdout, "worldsnap: out of memory restoring the game\n");
        abort();
    }
    // The map: its header, then its string pool and tiles at the stored sizes.
    const unsigned char *p = snap->map_bytes;
    Map head;
    memcpy(&head, p, MAP_HEAD_BYTES);                 p += MAP_HEAD_BYTES;
    if (map->width != head.width || map->height != head.height || !map->tiles) {
        if (!MapAlloc(map, head.width, head.height)) {
            fprintf(stdout, "worldsnap: out of memory restoring the map\n");
            abort();
        }
    }
    int nstr = head.str_count, npool = head.pool_used;
    if (nstr > map->str_cap) {
        uint32_t *so = realloc(map->str_off, (size_t)nstr * sizeof *so);
        if (!so) { fprintf(stdout, "worldsnap: out of memory\n"); abort(); }
        map->str_off = so; map->str_cap = nstr;
    }
    if (npool > map->pool_cap) {
        char *pl = realloc(map->pool, (size_t)npool);
        if (!pl) { fprintf(stdout, "worldsnap: out of memory\n"); abort(); }
        map->pool = pl; map->pool_cap = npool;
    }
    memcpy(map, &head, MAP_HEAD_BYTES);
    if (nstr)  { memcpy(map->str_off, p, (size_t)nstr * sizeof(uint32_t)); p += (size_t)nstr * sizeof(uint32_t); }
    if (npool) { memcpy(map->pool, p, (size_t)npool); p += (size_t)npool; }
    size_t cells = map_tile_count(map);
    if (cells) memcpy(map->tiles, p, cells * sizeof(Tile));
    if (!FogCopy(fog, &snap->fog)) {
        fprintf(stdout, "worldsnap: out of memory restoring the fog\n");
        abort();
    }
    GameRngRestore(snap->rng);
    ledger_unsnap(&snap->ledger);
    // A capture is always taken at FLOW_NONE; the pending flow scratch and the
    // adventure-spell UI continuations are process globals outside Game, so a
    // restore must reset them explicitly (AP-031, AP-033).
    pending_reset();
    spells_adventure_reset_ui();
}

// FNV-1a over the whole value state, minus Game's res pointer (shared,
// read-only, position-dependent). Test-facing (AP-032).
static uint32_t fnv1a(uint32_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

uint32_t worldsnap_fingerprint(const Game *g, const Map *map, const Fog *fog) {
    uint32_t h = 2166136261u;
    if (!g || !map || !fog) return h;
    h = GameFingerprint(g, h);
    h = fnv1a(h, map, MAP_HEAD_BYTES);
    if (map->str_off) h = fnv1a(h, map->str_off, (size_t)map->str_count * sizeof(uint32_t));
    if (map->pool)    h = fnv1a(h, map->pool, (size_t)map->pool_used);
    h = fnv1a(h, map->tiles ? map->tiles : (const Tile *)"", map_tile_count(map) * sizeof(Tile));
    h = fnv1a(h, &fog->width, sizeof fog->width);
    h = fnv1a(h, &fog->height, sizeof fog->height);
    if (fog->seen) h = fnv1a(h, fog->seen, (size_t)fog->width * (size_t)fog->height);
    return h;
}
