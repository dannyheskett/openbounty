// engine/game_storage.c -- a Game's heap tables and lists: sizing, release,
// deep copy and fingerprint (game.h, Storage).

#include "game.h"
#include "game_internal.h"
#include "map.h"
#include "adventure.h"
#include "ui_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ----- Storage --------------------------------------------------------------

// A zeroed heap table of n entries, or NULL for none. *ok goes false when an
// allocation fails.
static void *table_alloc(int n, size_t elem, bool *ok) {
    if (n <= 0) return NULL;
    void *p = calloc((size_t)n, elem);
    if (!p) *ok = false;
    return p;
}

static void game_free_tables(Game *g) {
    free(g->towns);
    free(g->castles);
    free(g->spells.counts);
    free(g->spells.learned);
    free(g->artifacts.found);
    free(g->contract.cycle);
    free(g->contract.villains_caught);
    free(g->contract.villains_prefought);
    free(g->world.zones_discovered);
    free(g->world.zone_rites);
    free(g->world.orbs_found);
    for (int i = 0; g->world.continent_fog && i < g->world.zone_count; i++)
        FogFree(&g->world.continent_fog[i]);
    free(g->world.continent_fog);
    free(g->consumed);
    free(g->bridges);
    free(g->events_done);
    free(g->dwellings);
    free(g->placements);
    free(g->foes);
    free(g->player_io.slot);
}

void GameFree(Game *g) {
    if (!g) return;
    const Resources *res = g->res;
    game_free_tables(g);
    memset(g, 0, sizeof *g);
    g->res = res;
}

bool GameAlloc(Game *g) {
    if (!g) return false;
    GameFree(g);
    const Resources *r = g->res;
    int nz = r ? r->zone_count : 0;
    int cyc = r ? r->contract.cycle_length : 5;
    if (cyc < 1) cyc = 1;
    bool ok = true;
    g->town_count   = r ? r->town_count : 0;
    g->castle_count = r ? r->castle_count : 0;
    g->towns   = table_alloc(g->town_count, sizeof *g->towns, &ok);
    g->castles = table_alloc(g->castle_count, sizeof *g->castles, &ok);
    g->spells.count   = spells_count();
    g->spells.counts  = table_alloc(g->spells.count, sizeof *g->spells.counts, &ok);
    g->spells.learned = table_alloc(g->spells.count, sizeof *g->spells.learned, &ok);
    g->artifacts.count = artifacts_count();
    g->artifacts.found = table_alloc(g->artifacts.count, sizeof *g->artifacts.found, &ok);
    g->contract.cycle_count = cyc;
    g->contract.cycle = table_alloc(cyc, sizeof *g->contract.cycle, &ok);
    g->contract.villain_count = villains_count();
    g->contract.villains_caught =
        table_alloc(g->contract.villain_count, sizeof *g->contract.villains_caught, &ok);
    g->contract.villains_prefought =
        table_alloc(g->contract.villain_count, sizeof *g->contract.villains_prefought, &ok);
    g->world.zone_count       = nz;
    g->world.zones_discovered = table_alloc(nz, sizeof *g->world.zones_discovered, &ok);
    g->world.zone_rites       = table_alloc(nz, sizeof *g->world.zone_rites, &ok);
    g->world.orbs_found       = table_alloc(nz, sizeof *g->world.orbs_found, &ok);
    g->world.continent_fog    = table_alloc(nz, sizeof *g->world.continent_fog, &ok);
    if (!ok) GameFree(g);
    return ok;
}

// Grow `*arr` (entries of `elem` bytes, `*cap` allocated) to hold `need`,
// doubling. New entries are zeroed.
static bool list_reserve(void **arr, int *cap, int need, size_t elem) {
    if (need <= *cap) return true;
    int ncap = *cap > 0 ? *cap : 16;
    while (ncap < need) ncap *= 2;
    void *p = realloc(*arr, (size_t)ncap * elem);
    if (!p) return false;
    memset((char *)p + (size_t)*cap * elem, 0, (size_t)(ncap - *cap) * elem);
    *arr = p;
    *cap = ncap;
    return true;
}

#define GAME_LIST_RESERVE(g, list, cap, need) \
    list_reserve((void **)&(g)->list, &(g)->cap, (need), sizeof *(g)->list)

bool GameReserveFoes(Game *g, int need)       { return g && GAME_LIST_RESERVE(g, foes, foe_cap, need); }
bool GameReservePlacements(Game *g, int need) { return g && GAME_LIST_RESERVE(g, placements, placement_cap, need); }
bool GameReserveConsumed(Game *g, int need)   { return g && GAME_LIST_RESERVE(g, consumed, consumed_cap, need); }
bool GameReserveBridges(Game *g, int need)    { return g && GAME_LIST_RESERVE(g, bridges, bridge_cap, need); }
bool GameReserveEventsDone(Game *g, int need) { return g && GAME_LIST_RESERVE(g, events_done, events_done_cap, need); }
bool GameReserveDwellings(Game *g, int need)  { return g && GAME_LIST_RESERVE(g, dwellings, dwelling_cap, need); }

// Copy a table of n entries into a fresh allocation (NULL for none).
static void *table_dup(const void *src, int n, size_t elem, bool *ok) {
    if (!src || n <= 0) return NULL;
    void *p = malloc((size_t)n * elem);
    if (!p) { *ok = false; return NULL; }
    memcpy(p, src, (size_t)n * elem);
    return p;
}

// Copy `n` entries into dst's table, reusing it when it already holds n.
#define COPY_TABLE(dfield, sfield, dn, sn) do {                                 \
        if ((dn) != (sn) || !(dfield)) {                                          \
            free(dfield);                                                         \
            (dfield) = table_dup((sfield), (sn), sizeof *(sfield), &ok);          \
        } else if ((sn) > 0) {                                                    \
            memcpy((dfield), (sfield), (size_t)(sn) * sizeof *(sfield));          \
        }                                                                         \
    } while (0)

// Copy a growable list's live entries, growing dst's allocation as needed.
#define COPY_LIST(list, count, cap) do {                                          \
        if (src->count > 0) {                                                     \
            if (!GAME_LIST_RESERVE(dst, list, cap, src->count)) ok = false;       \
            else memcpy(dst->list, src->list, (size_t)src->count * sizeof *src->list); \
        }                                                                         \
    } while (0)

bool GameCopy(Game *dst, const Game *src) {
    if (!dst || !src || dst == src) return dst != NULL;
    bool ok = true;
    // Keep dst's tables, then take every value field from src. The only
    // whole-Game byte copy: every table pointer is put back right below.
    Game keep;
    memcpy(&keep, dst, sizeof keep);
    memcpy(dst, src, sizeof *dst);
    dst->towns = keep.towns;               dst->castles = keep.castles;
    dst->spells.counts = keep.spells.counts;
    dst->spells.learned = keep.spells.learned;
    dst->artifacts.found = keep.artifacts.found;
    dst->contract.cycle = keep.contract.cycle;
    dst->contract.villains_caught = keep.contract.villains_caught;
    dst->contract.villains_prefought = keep.contract.villains_prefought;
    dst->world.zones_discovered = keep.world.zones_discovered;
    dst->world.zone_rites = keep.world.zone_rites;
    dst->world.orbs_found = keep.world.orbs_found;
    dst->world.continent_fog = keep.world.continent_fog;
    dst->consumed = keep.consumed;         dst->consumed_cap = keep.consumed_cap;
    dst->bridges = keep.bridges;           dst->bridge_cap = keep.bridge_cap;
    dst->events_done = keep.events_done;   dst->events_done_cap = keep.events_done_cap;
    dst->dwellings = keep.dwellings;       dst->dwelling_cap = keep.dwelling_cap;
    dst->placements = keep.placements;     dst->placement_cap = keep.placement_cap;
    dst->foes = keep.foes;                 dst->foe_cap = keep.foe_cap;
    dst->player_io = keep.player_io;

    COPY_TABLE(dst->towns, src->towns, keep.town_count, src->town_count);
    COPY_TABLE(dst->castles, src->castles, keep.castle_count, src->castle_count);
    COPY_TABLE(dst->spells.counts, src->spells.counts, keep.spells.count, src->spells.count);
    COPY_TABLE(dst->spells.learned, src->spells.learned, keep.spells.count, src->spells.count);
    COPY_TABLE(dst->artifacts.found, src->artifacts.found, keep.artifacts.count, src->artifacts.count);
    COPY_TABLE(dst->contract.cycle, src->contract.cycle, keep.contract.cycle_count, src->contract.cycle_count);
    COPY_TABLE(dst->contract.villains_caught, src->contract.villains_caught,
               keep.contract.villain_count, src->contract.villain_count);
    COPY_TABLE(dst->contract.villains_prefought, src->contract.villains_prefought,
               keep.contract.villain_count, src->contract.villain_count);
    COPY_TABLE(dst->world.zones_discovered, src->world.zones_discovered,
               keep.world.zone_count, src->world.zone_count);
    COPY_TABLE(dst->world.zone_rites, src->world.zone_rites,
               keep.world.zone_count, src->world.zone_count);
    COPY_TABLE(dst->world.orbs_found, src->world.orbs_found,
               keep.world.zone_count, src->world.zone_count);
    // The continent fogs: a table of Fog, each with its own grid.
    if (keep.world.zone_count != src->world.zone_count || !dst->world.continent_fog) {
        for (int i = 0; dst->world.continent_fog && i < keep.world.zone_count; i++)
            FogFree(&dst->world.continent_fog[i]);
        free(dst->world.continent_fog);
        dst->world.continent_fog = table_alloc(src->world.zone_count,
                                               sizeof *dst->world.continent_fog, &ok);
        if (!dst->world.continent_fog) dst->world.zone_count = 0;
    }
    for (int i = 0; ok && dst->world.continent_fog && src->world.continent_fog &&
                    i < src->world.zone_count; i++)
        if (!FogCopy(&dst->world.continent_fog[i], &src->world.continent_fog[i])) ok = false;
    COPY_LIST(consumed, consumed_count, consumed_cap);
    COPY_LIST(bridges, bridge_count, bridge_cap);
    COPY_LIST(events_done, events_done_count, events_done_cap);
    COPY_LIST(dwellings, dwelling_count, dwelling_cap);
    COPY_LIST(placements, placement_count, placement_cap);
    COPY_LIST(foes, foe_count, foe_cap);
    // The request queue, oldest first from slot 0.
    {
        PlayerIoQueue *dq = &dst->player_io;
        const PlayerIoQueue *sq = &src->player_io;
        dq->head = 0;
        dq->count = 0;
        if (sq->count > 0) {
            if (dq->cap < sq->count) {
                PlayerRequest *ns = realloc(dq->slot, (size_t)sq->cap * sizeof *ns);
                if (!ns) ok = false;
                else { dq->slot = ns; dq->cap = sq->cap; }
            }
            if (ok) {
                for (int i = 0; i < sq->count; i++)
                    dq->slot[i] = sq->slot[(sq->head + i) % sq->cap];
                dq->count = sq->count;
            }
        }
    }
    if (!ok) GameFree(dst);
    return ok;
}

static uint32_t fnv_bytes(uint32_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

#define FNV_TABLE(h, ptr, n) \
    ((ptr) && (n) > 0 ? fnv_bytes((h), (ptr), (size_t)(n) * sizeof *(ptr)) : (h))

uint32_t GameFingerprint(const Game *g, uint32_t h) {
    if (!g) return h;
    // The value fields: a zeroed image with the scalars copied in, so padding
    // and the table pointers never reach the hash.
    Game flat;
    memset(&flat, 0, sizeof flat);
    flat.version = g->version;           flat.seed = g->seed;
    flat.seed_from_catalog = g->seed_from_catalog;
    flat.seed_index = g->seed_index;     flat.oracle_mode = g->oracle_mode;
    flat.character = g->character;       flat.stats = g->stats;
    flat.position = g->position;         flat.travel_mode = g->travel_mode;
    flat.anim_frame = g->anim_frame;     flat.anim_moving = g->anim_moving;
    flat.hud_visible = g->hud_visible;
    memcpy(flat.army, g->army, sizeof flat.army);
    flat.contract.cycle_count = g->contract.cycle_count;
    memcpy(flat.contract.active_id, g->contract.active_id, sizeof flat.contract.active_id);
    flat.contract.last_contract = g->contract.last_contract;
    flat.contract.max_contract = g->contract.max_contract;
    flat.boat = g->boat;                 flat.scepter = g->scepter;
    flat.consumed_count = g->consumed_count;
    flat.bridge_count = g->bridge_count;
    flat.dwelling_count = g->dwelling_count;
    flat.placement_count = g->placement_count;
    flat.foe_count = g->foe_count;
    flat.player_io.count = g->player_io.count;
    h = fnv_bytes(h, &flat, sizeof flat);
    for (int i = 0; i < g->player_io.count; i++)
        h = fnv_bytes(h, &g->player_io.slot[(g->player_io.head + i) % g->player_io.cap],
                      sizeof *g->player_io.slot);
    h = FNV_TABLE(h, g->towns, g->town_count);
    h = FNV_TABLE(h, g->castles, g->castle_count);
    h = FNV_TABLE(h, g->spells.counts, g->spells.count);
    h = FNV_TABLE(h, g->spells.learned, g->spells.count);
    h = FNV_TABLE(h, g->artifacts.found, g->artifacts.count);
    h = FNV_TABLE(h, g->contract.cycle, g->contract.cycle_count);
    h = FNV_TABLE(h, g->contract.villains_caught, g->contract.villain_count);
    h = FNV_TABLE(h, g->contract.villains_prefought, g->contract.villain_count);
    h = FNV_TABLE(h, g->world.zones_discovered, g->world.zone_count);
    h = FNV_TABLE(h, g->world.zone_rites, g->world.zone_count);
    h = FNV_TABLE(h, g->world.orbs_found, g->world.zone_count);
    for (int i = 0; g->world.continent_fog && i < g->world.zone_count; i++) {
        const Fog *f = &g->world.continent_fog[i];
        h = fnv_bytes(h, &f->width, sizeof f->width);
        h = fnv_bytes(h, &f->height, sizeof f->height);
        h = FNV_TABLE(h, f->seen, f->width * f->height);
    }
    h = FNV_TABLE(h, g->consumed, g->consumed_count);
    h = FNV_TABLE(h, g->bridges, g->bridge_count);
    h = FNV_TABLE(h, g->dwellings, g->dwelling_count);
    h = FNV_TABLE(h, g->placements, g->placement_count);
    h = FNV_TABLE(h, g->foes, g->foe_count);
    return h;
}
