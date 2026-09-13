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

#define MAP_HEAD_BYTES offsetof(Map, tiles)

void worldsnap_release(WorldSnapshot *snap) {
    if (!snap) return;
    free(snap->map_bytes);
    snap->map_bytes = NULL;
    snap->map_cap = 0;
}

void worldsnap_capture(WorldSnapshot *snap, const Game *g, const Map *map,
                       const Fog *fog) {
    if (!snap || !g || !map || !fog) return;
    int w = map->width, h = map->height;
    if (w < 0 || w > MAP_MAX_W) w = 0;
    if (h < 0 || h > MAP_MAX_H) h = 0;
    size_t need = MAP_HEAD_BYTES + (size_t)w * (size_t)h * sizeof(Tile);
    if (snap->map_cap < need) {
        unsigned char *nb = (unsigned char *)realloc(snap->map_bytes, need);
        if (!nb) {
            fprintf(stdout, "worldsnap: out of memory (%zu bytes)\n", need);
            abort();
        }
        snap->map_bytes = nb;
        snap->map_cap = need;
    }
    memcpy(snap->map_bytes, map, MAP_HEAD_BYTES);
    unsigned char *p = snap->map_bytes + MAP_HEAD_BYTES;
    for (int y = 0; y < h; y++) {
        memcpy(p, map->tiles[y], (size_t)w * sizeof(Tile));
        p += (size_t)w * sizeof(Tile);
    }
    snap->game = *g;
    snap->fog = *fog;
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
    *g = snap->game;
    // The map: the header, then the stored rows; cells the live map used beyond
    // the restored map's area go back to zero, as a fresh load leaves them.
    int old_w = map->width, old_h = map->height;
    if (old_w < 0 || old_w > MAP_MAX_W) old_w = MAP_MAX_W;
    if (old_h < 0 || old_h > MAP_MAX_H) old_h = MAP_MAX_H;
    memcpy(map, snap->map_bytes, MAP_HEAD_BYTES);
    int w = map->width, h = map->height;
    const unsigned char *p = snap->map_bytes + MAP_HEAD_BYTES;
    for (int y = 0; y < h; y++) {
        memcpy(map->tiles[y], p, (size_t)w * sizeof(Tile));
        p += (size_t)w * sizeof(Tile);
        if (y < old_h && old_w > w)
            memset(&map->tiles[y][w], 0, (size_t)(old_w - w) * sizeof(Tile));
    }
    for (int y = h; y < old_h; y++)
        memset(map->tiles[y], 0, (size_t)old_w * sizeof(Tile));
    *fog = snap->fog;
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
    Game tmp = *g;
    tmp.res = NULL;   // exclude the shared pointer from the byte hash
    h = fnv1a(h, &tmp, sizeof tmp);
    h = fnv1a(h, map, sizeof *map);
    h = fnv1a(h, fog, sizeof *fog);
    return h;
}
