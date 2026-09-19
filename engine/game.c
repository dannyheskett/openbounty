#include "game.h"
#include "map.h"
#include "adventure.h"
#include "savegame.h"
#include "fatal.h"
#include "ui_host.h"   // recorder_capture, audio_play_tune + AudioTuneId
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

// Forward declarations for helpers defined later in this file but
// used by GameInit's eager-populate sweep.
static DwellingState *enforce_dwelling(Game *g, const char *zone, int x, int y,
                                       const char *dwelling_kind);
static DwellingState *enforce_dwelling_pinned(Game *g, const char *zone,
                                              int x, int y,
                                              const char *troop_id);

// Seeded random helper for deterministic scepter placement.
// Uses uint64_t so the LCG behaves identically on 32-bit and 64-bit
// platforms (unsigned long is 32-bit on Windows and breaks the >>32
// shift).
static uint64_t game_rng_state = 0;

static void game_rng_seed(uint64_t seed) {
    game_rng_state = seed ^ 0x5DEECE66DULL;  // Linear congruential generator seed
}

// 8-bit world catalog: expand a 0..255 index into a full-width world seed.
// The expansion is not decoration. Consumers read the seed at three widths --
// flows.c spawns use (seed >> 8), chest_rand / the dwelling pick / the weekly
// salt truncate to (unsigned), and the LCG above takes all 64 bits -- so a raw
// 0..255 seed would hand (seed >> 8) the value 0 for every world and leave the
// rest with 8 bits of entropy. Avalanching first keeps all 256 worlds distinct
// through every one of those paths.
//
// This is the catalog's identity: changing these constants, or the order of the
// game_rng_next() calls below, re-maps every world and invalidates any recorded
// per-world result.
uint64_t GameSeedFromIndex(unsigned char index) {
    uint64_t z = (uint64_t)index + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static int game_rng_next(int min, int max) {
    if (min > max) return min;
    if (min == max) return min;
    game_rng_state = game_rng_state * 25214903917ULL + 11ULL;
    unsigned int result = (unsigned int)(game_rng_state >> 32);
    return min + (result % (max - min + 1));
}

// Snapshot/restore the process-global world RNG. The autoplay planner
// drives the real engine on game COPIES to validate objectives, but some
// engine ops (e.g. GameRollChest) advance this global state; the planner
// snapshots before and restores after so the live game's RNG sequence is
// unperturbed by planning.
uint64_t GameRngSnapshot(void) { return game_rng_state; }
void     GameRngRestore(uint64_t s) { game_rng_state = s; }

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
    dst->artifacts.found = keep.artifacts.found;
    dst->contract.cycle = keep.contract.cycle;
    dst->contract.villains_caught = keep.contract.villains_caught;
    dst->contract.villains_prefought = keep.contract.villains_prefought;
    dst->world.zones_discovered = keep.world.zones_discovered;
    dst->world.zone_rites = keep.world.zone_rites;
    dst->world.orbs_found = keep.world.orbs_found;
    dst->world.continent_fog = keep.world.continent_fog;
    dst->consumed = keep.consumed;         dst->consumed_cap = keep.consumed_cap;
    dst->events_done = keep.events_done;   dst->events_done_cap = keep.events_done_cap;
    dst->dwellings = keep.dwellings;       dst->dwelling_cap = keep.dwelling_cap;
    dst->placements = keep.placements;     dst->placement_cap = keep.placement_cap;
    dst->foes = keep.foes;                 dst->foe_cap = keep.foe_cap;
    dst->player_io = keep.player_io;

    COPY_TABLE(dst->towns, src->towns, keep.town_count, src->town_count);
    COPY_TABLE(dst->castles, src->castles, keep.castle_count, src->castle_count);
    COPY_TABLE(dst->spells.counts, src->spells.counts, keep.spells.count, src->spells.count);
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
    h = FNV_TABLE(h, g->dwellings, g->dwelling_count);
    h = FNV_TABLE(h, g->placements, g->placement_count);
    h = FNV_TABLE(h, g->foes, g->foe_count);
    return h;
}

static void copy_id(char *dst, size_t dst_sz, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < dst_sz && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void GameAddPlacement(Game *g, const char *zone, int x, int y, int kind, const char *id) {
    if (!g || !zone) return;
    if (!GameReservePlacements(g, g->placement_count + 1)) return;
    SaltedPlacement *p = &g->placements[g->placement_count++];
    copy_id(p->zone, sizeof(p->zone), zone);
    p->x = x;
    p->y = y;
    p->kind = kind;
    copy_id(p->id, sizeof(p->id), id);
}

// spawn_game implementation.
void GameInit(Game *g, const char *name, int pclass, int difficulty, const unsigned char *land) {
    GameInitSeeded(g, name, pclass, difficulty, land, -1);
}

void GameInitSeeded(Game *g, const char *name, int pclass, int difficulty,
                    const unsigned char *land, int seed_index) {
    int i;
    // Preserve the Resources pointer across the memset -- it must be set by
    // the caller (main.c / menu_new) before GameInit and is used by init
    // steps like salt_spells/salt_continent/salt_villains.
    const Resources *res_saved = g->res;
    // Preserve any caller-supplied raw seed so tests can force determinism.
    // Only consulted when seed_index < 0; see the seed resolution below.
    uint64_t seed_saved = g->seed;
    // Every table sized from the pack, zeroed (the Game must be zeroed or
    // previously sized).
    g->res = res_saved;
    if (!GameAlloc(g)) {
        fatal_user_error("OpenBounty", "Out of memory starting a game.");
        exit(2);
    }
    player_io_reset(g);   // empty the uniform player-IO request queue (the
                          // memset already zeroed it; this centralizes the
                          // invariant -- see engine/include/player_io.h)
    pending_reset();      // clear the pending-flow GLOBALS too: they leak across
                          // in-process games (a stale pending_flow would make the
                          // new game's first tick answer a phantom prompt).
    (void)land;   // World byte-map is unused; we load zone maps lazily
                  // via MapLoadZoneWithPlacements instead.

    // Seed resolution, in priority order. seed_index is the catalog identity
    // and `seed` is always derived from it; the raw-seed path is the escape
    // hatch for callers that pin g->seed directly.
    if (seed_index >= 0) {
        g->seed_index = seed_index & 0xFF;
        g->seed_from_catalog = true;
        g->seed = GameSeedFromIndex((unsigned char)g->seed_index);
    } else if (seed_saved != 0) {
        g->seed = seed_saved;
        g->seed_from_catalog = false;
    } else {
        // No world asked for: pick one from wall-clock time. Without this, the
        // memset above leaves g->seed = 0 and every new game produces the
        // exact same world (same dwellings, same artifacts, same scepter
        // location). Mix in name + class so two games started in the same
        // second still differ.
        uint64_t t = (uint64_t)time(NULL);
        uint64_t h = t;
        h ^= (uint64_t)pclass * 2654435761ULL;
        for (const char *p = name; p && *p; p++) {
            h = h * 31ULL + (uint64_t)(unsigned char)*p;
        }
        g->seed_index = (int)(h & 0xFFu);
        g->seed_from_catalog = true;
        g->seed = GameSeedFromIndex((unsigned char)g->seed_index);
    }

    // Step 2 (play.c:385-388): Hide scepter.
    // Seed the deterministic RNG from g->seed so every subsequent
    // game_rng_next() call produces a reproducible world from this
    // seed (saves restore g->seed and re-derive identical state).
    game_rng_seed(g->seed);
    g->scepter.key = game_rng_next(0, 255);
    int scepter_continent = game_rng_next(0, 3);
    bury_scepter(g, scepter_continent);

    // Step 3 (play.c:390-400): Character name, class, difficulty, days, gold.
    if (name && name[0]) {
        strncpy(g->character.name, name, sizeof(g->character.name) - 1);
        g->character.name[sizeof(g->character.name) - 1] = '\0';
        // Capitalize first letter ).
        if (g->character.name[0] >= 'a' && g->character.name[0] <= 'z') {
            g->character.name[0] = (char)(g->character.name[0] - 'a' + 'A');
        }
    } else {
        const char *dn = g->res->world.default_name;
        strncpy(g->character.name, dn, sizeof(g->character.name) - 1);
        g->character.name[sizeof(g->character.name) - 1] = '\0';
    }

    g->character.difficulty = difficulty;

    const ClassDef *cls = (pclass >= 0 && pclass < classes_count()) ? class_by_index(pclass) : class_by_index(0);
    if (!cls) cls = class_by_index(0);
    copy_id(g->character.cls.id, sizeof(g->character.cls.id), cls->id);
    g->character.cls.rank_index = 0;
    copy_id(g->character.cls.rank_id, sizeof(g->character.cls.rank_id), cls->ranks[0].id);
    copy_id(g->character.cls.rank_title, sizeof(g->character.cls.rank_title), cls->ranks[0].name);

    int lead = 0, maxsp = 0, spp = 0, comm = 0;
    class_stats_at_rank(cls, 0, &lead, &maxsp, &spp, &comm);
    g->stats.gold = cls->starting_gold;
    g->stats.commission_weekly = comm;
    g->stats.leadership_base = lead;
    g->stats.leadership_current = lead;
    g->stats.spell_power = spp;
    g->stats.max_spells = maxsp;
    g->stats.knows_magic = cls->ranks[0].knows_magic;
    g->stats.siege_weapons = 0;

    int di = (difficulty >= 0 && difficulty < 4) ? difficulty : 0;
    g->stats.days_left = g->res ? g->res->time.days_per_difficulty[di] : 900;
    g->stats.steps_left_today = g->res ? g->res->time.day_steps : 40;
    g->stats.last_commission = 0;

    // Step 4: Starting position (home continent, home_spawn). Look for
    // the zone flagged is_home; fall back to world.starting_zone +
    // hero_spawn.
    g->position.zone[0] = '\0';
    g->position.x = g->position.y = 0;
    int home_zone_index = -1;
    if (g->res) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            if (!z->is_home) continue;
            copy_id(g->position.zone, sizeof(g->position.zone), z->id);
            g->position.x = z->home_spawn_x;
            g->position.y = z->home_spawn_y;
            home_zone_index = zi;
            break;
        }
        if (!g->position.zone[0]) {
            copy_id(g->position.zone, sizeof(g->position.zone),
                    g->res->world.starting_zone);
            for (int zi = 0; zi < g->res->zone_count; zi++) {
                const ResZone *z = &g->res->zones[zi];
                if (strcmp(z->id, g->res->world.starting_zone) == 0) {
                    g->position.x = z->hero_spawn_x;
                    g->position.y = z->hero_spawn_y;
                    home_zone_index = zi;
                    break;
                }
            }
        }
        // : continent_found[HOME_CONTINENT] = 1
        if (home_zone_index >= 0) {
            g->world.zones_discovered[home_zone_index] = true;
        }
    }

    // Step 5 (play.c:407-410): Mount, boat, last position.
    g->character.mount = MOUNT_RIDE;
    g->boat.has_boat = false;
    g->boat.x = -1;
    g->boat.y = -1;
    g->position.last_x = g->position.x;
    g->position.last_y = g->position.y;

    // Step 6 (play.c:412-415): Rank init (leadership from base_leadership,
    // time_stop = 0). Our player_accept_rank is a no-op today because rank
    // stats already come from class_stats_at_rank above; leaving the call
    // for sequence parity.
    g->character.cls.rank_index = 0;
    player_accept_rank(g);
    g->stats.time_stop = 0;

    // Step 7: Contract cycle. Seed with the first cycle_length villain
    // ids from the catalog.
    g->contract.active_id[0] = '\0';
    int cycle_len = g->contract.cycle_count;
    g->contract.last_contract = g->res ? g->res->contract.initial_last_contract
                                       : cycle_len - 1;
    g->contract.max_contract  = cycle_len;
    for (i = 0; i < cycle_len; i++) {
        const VillainDef *v = villain_by_index(i);
        if (v) copy_id(g->contract.cycle[i],
                       sizeof(g->contract.cycle[i]), v->id);
        else   g->contract.cycle[i][0] = '\0';
    }

    // Step 8 (play.c:426-433): Starting army (2 slots + empty rest).
    for (i = 0; i < cls->starting_troop_count && i < GAME_ARMY_SLOTS; i++) {
        const char *troop = cls->starting_troops[i];
        int count = cls->starting_counts[i];
        if (!troop[0] || count <= 0) continue;
        copy_id(g->army[i].id, sizeof(g->army[i].id), troop);
        g->army[i].count = count;
    }

    // Step 9 (play.c:435-441): Default player options.
    for (int oi = 0; oi < 7; oi++) {
        g->stats.options[oi] = g->res ? g->res->world.default_options[oi] : 1;
    }

    // Step 10 (play.c:444): Randomize spells sold in towns.
    salt_spells(g);

    // Remove magic alcove(s) if the starting class already knows magic
    // (). The alcove is an overlay at the tile declared
    // in zones[].magic_alcove; marking it consumed stops MapLoadZone /
    // stamp_objects from rendering an interactive on that tile.
    // With rites per zone, such a class knows only the home zone's rites, and
    // only that alcove is spent.
    if (g->stats.knows_magic && g->res) {
        bool per_zone = g->res->economy.rites_per_zone;
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            if (per_zone && strcmp(z->id, g->res->world.starting_zone) != 0) continue;
            if (per_zone && zi < g->world.zone_count) g->world.zone_rites[zi] = true;
            if (z->magic_alcove_x < 0 || z->magic_alcove_y < 0) continue;
            GameAddConsumed(g, z->id, z->magic_alcove_x, z->magic_alcove_y);
        }
    }

    // Salt each zone from its own salt budget (zones[].salt in game.json).
    //  loops continents calling
    //   salt_continent(game, i, 2, 1, 1, 2, 10, 5);
    // with the same budget per continent. We read the budget from the
    // ResZone so mods can tune it.
    if (g->res) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            salt_continent(g, zi,
                           z->salt.artifacts,
                           z->salt.navmaps,
                           z->salt.orbs,
                           z->salt.telecaves,
                           z->salt.dwellings,
                           z->salt.friendly_foes);
        }
    }

    // Initialize castles: eagerly copy ids from the resource catalog so
    // GameFindCastle works, and mark them all monster-owned 
    // spawn_game:461-464 (castle_owner[i] = 0x7F). Villain assignment
    // (salt_villains) runs later and overwrites owner_kind where applicable.
    {
        int ncastles = g->castle_count;
        for (i = 0; i < ncastles; i++) {
            const ResCastle *rc = &g->res->castles[i];
            CastleRecord *cr = &g->castles[i];
            copy_id(cr->id, sizeof(cr->id), rc->id);
            cr->visited = false;
            cr->known = false;
            cr->owner_kind = CASTLE_OWNER_MONSTERS;
            cr->villain_id[0] = '\0';
            for (int sl = 0; sl < GAME_ARMY_SLOTS; sl++) {
                cr->garrison[sl].id[0] = '\0';
                cr->garrison[sl].count = 0;
            }
        }
        // Any leftover slots stay zeroed from memset earlier in GameInit.
    }

    // Assign villains to castles ().
    salt_villains(g);

    // Repopulate every remaining monster-owned castle with a troop stack
    // ().
    for (i = 0; i < g->castle_count; i++) {
        if (!g->castles[i].id[0]) continue;
        // Castles flagged special.excluded_from_contract never hold a
        // monster garrison; mark them CASTLE_OWNER_SPECIAL so downstream
        // UI/flow can distinguish them from ordinary monster castles.
        if (g->res && i < g->res->castle_count &&
            g->res->castles[i].special.excluded_from_contract) {
            g->castles[i].owner_kind = CASTLE_OWNER_SPECIAL;
            continue;
        }
        if (g->castles[i].owner_kind == CASTLE_OWNER_MONSTERS) {
            repopulate_castle(g, i);
        }
    }

    // enforce_dwelling: eagerly create a DwellingState row for every
    // dwelling tile so save state matches the "all dwellings populated
    // at game creation" model. Two sources:
    //   1. JSON-declared dwellings (ResZone.dwellings[]).
    //   2. Salt-placed dwellings (g->placements[] kind == DWELLING_*).
    // Visit-time GameTouchDwelling becomes a pure lookup.
    if (g->res) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            for (int di = 0; di < z->dwelling_count; di++) {
                const ResZoneDwelling *rd = &z->dwellings[di];
                enforce_dwelling(g, z->id, rd->x, rd->y, rd->kind);
            }
        }
    }
    for (int pi = 0; pi < g->placement_count; pi++) {
        const SaltedPlacement *p = &g->placements[pi];
        const char *kind = NULL;
        switch ((Interact)p->kind) {
            case INTERACT_DWELLING_PLAINS:  kind = "plains";  break;
            case INTERACT_DWELLING_FOREST:  kind = "forest";  break;
            case INTERACT_DWELLING_HILLS:   kind = "hills";   break;
            case INTERACT_DWELLING_DUNGEON: kind = "dungeon"; break;
            default: continue;
        }
        enforce_dwelling(g, p->zone, p->x, p->y, kind);
    }

    // Clear fog around starting location
    clear_fog(g);
}

// The cast formulas, exposed as read-only queries so planning layers never
// duplicate them (game.h). The casts below apply exactly these values.
// Both are the bare original formulas: NO minimum. A class whose current rank
// has spell_power 0 (Barbarian, first rank) gets nothing from either spell --
// that is the intended original behavior, not a degenerate case to clamp away.
int GameTimeStopStepsPerCast(const Game *g) {
    return (g ? g->stats.spell_power : 0) * 10;
}

int GameRaiseControlAmount(const Game *g) {
    return (g ? g->stats.spell_power : 0) * 100;
}

bool GameTerrainCostsFullDay(int terrain) {
    return terrain == TERRAIN_DESERT;
}

// time_stop: game->time_stop += spell_power * 10.
void GameCastTimeStop(Game *g) {
    if (!g) return;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_TIME_STOP);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    g->spells.counts[idx]--;
    g->stats.time_stop += GameTimeStopStepsPerCast(g);
}

// raise_control: leadership_current += spell_power*100 (the field's only lever
// besides rank-ups), capped at GAME_LEADERSHIP_MAX. TEMPORARY: leadership_current
// resets to leadership_base at the next week boundary (GameOnStep), so callers
// that need the boost for a fight must use it the same tick. Consumes one
// charge; no-op (nothing spent) if none owned. Note the charge IS spent even at
// spell_power 0, where the amount is 0 -- the cast happens, it just does nothing.
void GameCastRaiseControl(Game *g) {
    if (!g) return;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    g->spells.counts[idx]--;
    g->stats.leadership_current += GameRaiseControlAmount(g);
    if (g->stats.leadership_current > GAME_LEADERSHIP_MAX)
        g->stats.leadership_current = GAME_LEADERSHIP_MAX;
}

// find_villain: scan castles for the one held by the active contract's
// villain, mark it known so its location shows on the world map and
// the intel dialog reflects it.
void GameCastFindVillain(Game *g) {
    if (!g) return;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_FIND_VILLAIN);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    if (!g->contract.active_id[0]) return;
    g->spells.counts[idx]--;
    for (int i = 0; i < g->castle_count; i++) {
        if (!g->castles[i].id[0]) continue;
        if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
        if (strcmp(g->castles[i].villain_id, g->contract.active_id) != 0)
            continue;
        g->castles[i].known = true;
        return;
    }
}

// Eagerly populates every TownRecord from res->towns[] and assigns a
// spell to each:
//   1. Towns with a non-empty `pinned_spell` field get that spell pre-placed.
//      (Any town may pin any spell via its game.json record.)
//   2. For every other spell (not already pinned): pick a random unclaimed
//      town, assign the spell. Retry until placed.
//   3. Any town left without a spell gets a random spell.
// RNG is deterministic from g->seed (game_rng_seed already called).
void salt_spells(Game *g) {
    if (!g || !g->res) return;

    const Resources *res = g->res;
    int nspells = spells_count();
    int ntowns  = g->town_count;
    if (nspells <= 0 || ntowns <= 0) return;

    // Eagerly create a TownRecord for every town in resources, so salt
    // assignments survive independent of visit order. Reset spell_for_sale
    // to ""  before assignment.
    for (int i = 0; i < ntowns; i++) {
        const ResTown *rt = &res->towns[i];
        TownRecord *tr = &g->towns[i];
        copy_id(tr->id, sizeof(tr->id), rt->id);
        tr->visited = false;
        tr->spell_for_sale[0] = '\0';
    }

    // Step 1: apply every town's pinned_spell, if any. Track which spell
    // ids are already claimed so step 2 can skip them.
    // One flag per spell of the pack (a spell's index beyond the count is never
    // walked by step 2, so it needs no flag).
    bool *spell_claimed = nspells > 0 ? calloc((size_t)nspells, sizeof *spell_claimed) : NULL;
    for (int i = 0; i < ntowns; i++) {
        const char *pin = res->towns[i].pinned_spell;
        if (!pin[0]) continue;
        const SpellDef *sp = spell_by_id(pin);
        if (!sp) continue;
        copy_id(g->towns[i].spell_for_sale,
                sizeof(g->towns[i].spell_for_sale), sp->id);
        if (spell_claimed && sp->index >= 0 && sp->index < nspells)
            spell_claimed[sp->index] = true;
    }

    // Step 2: for every not-yet-claimed spell, place it at a random
    // currently-empty town.  loop.
    // A pack may declare more spells than towns (e.g. 14 spells, 11 towns).
    // Once no empty town remains the surplus spells simply go unsold --
    // without this bound the random-empty-town search spins forever. Packs
    // with towns >= spells never hit empty_towns == 0 before s == nspells,
    // so their draw sequence (and every derived digest) is unchanged.
    int empty_towns = 0;
    for (int i = 0; i < ntowns; i++)
        if (g->towns[i].spell_for_sale[0] == '\0') empty_towns++;
    for (int s = 0; s < nspells && empty_towns > 0; ) {
        if (spell_claimed && spell_claimed[s]) { s++; continue; }
        int t = game_rng_next(0, ntowns - 1);
        if (g->towns[t].spell_for_sale[0] == '\0') {
            const SpellDef *sp = spell_by_index(s);
            if (sp) copy_id(g->towns[t].spell_for_sale,
                            sizeof(g->towns[t].spell_for_sale), sp->id);
            s++;
            empty_towns--;
        }
    }
    free(spell_claimed);

    // Step 3: any still-empty town gets a random spell.
    for (int i = 0; i < ntowns; i++) {
        if (g->towns[i].spell_for_sale[0]) continue;
        int s = game_rng_next(0, nspells - 1);
        const SpellDef *sp = spell_by_index(s);
        if (sp) copy_id(g->towns[i].spell_for_sale,
                        sizeof(g->towns[i].spell_for_sale), sp->id);
    }
}

// Salt kinds -- internal enum used while building the barrel.
typedef enum {
    SALT_NONE = 0,
    SALT_ARTIFACT,
    SALT_NAVMAP,
    SALT_ORB,
    SALT_TELECAVE,
    SALT_DWELLING,
    SALT_FRIENDLY,
} SaltKind;

static Interact dwelling_kind_to_interact(const char *kind) {
    if (!kind) return INTERACT_DWELLING_PLAINS;
    // troop->dwelling uses singular "hill"; zone JSON uses plural "hills".
    if (strcmp(kind, "forest") == 0)  return INTERACT_DWELLING_FOREST;
    if (strcmp(kind, "hills") == 0)   return INTERACT_DWELLING_HILLS;
    if (strcmp(kind, "hill") == 0)    return INTERACT_DWELLING_HILLS;
    if (strcmp(kind, "dungeon") == 0) return INTERACT_DWELLING_DUNGEON;
    return INTERACT_DWELLING_PLAINS;
}

// : pick the troop first, derive
// kind from troops[id].dwells. Per-zone preferred troop list comes
// first; remaining slots roll uniformly in dwelling_range_min..max.
static const char *salt_pick_dwelling_troop(const Game *g, int continent,
                                            int slot_index) {
    const ResZone *z = &g->res->zones[continent];
    if (slot_index < z->salt.preferred_troop_count) {
        return z->salt.preferred_troops[slot_index];
    }
    int lo = z->salt.dwelling_range_min;
    int hi = z->salt.dwelling_range_max;
    if (lo < 0 || hi < 0 || lo > hi) return NULL;
    int total = troops_count();
    if (lo >= total) return NULL;
    if (hi >= total) hi = total - 1;
    // Castle-kind troops (militia, archers, pikemen, knights, cavalry) are
    // recruited only at the home castle (REQ-310); they must never host a
    // dwelling. The numeric range straddles their catalog indices, so re-roll
    // whenever one comes up. Bounded guard avoids a spin if the range holds
    // nothing else; returning NULL then leaves the slot unplaced.
    for (int guard = 0; guard <= (hi - lo) * 20 + 20; guard++) {
        int idx = game_rng_next(lo, hi);
        const TroopDef *t = troop_by_index(idx);
        if (!t || !t->id[0]) continue;
        if (strcmp(t->dwelling, "castle") == 0) continue;
        return t->id;
    }
    return NULL;
}

// Converts a subset of the zone's saltable slots (JSON-declared
// chests[] positions) into randomly-typed objects per the supplied
// budget. Placements land in Game.placements[] via GameAddPlacement,
// so they survive across save/load.
//
// continent is the zone index in res->zones[]. RNG state has already been
// seeded from g->seed in GameInit, so repeated runs with the same seed
// produce the same layout (required for reproducible new games).
// Roll a defending stack (up to GAME_ARMY_SLOTS units) for a hostile foe
// using the zone's tier spawn pool. Deterministic given the current
// game_rng state so save/load reproduces the same garrison.
// : difficulty (= continent) governs the
// chance distribution; the dwelling kind is rolled fresh each call and
// indexes the troop pool independently. So Saharia (cont 3) skews to
// the rarest slot regardless of kind, but kind itself is uniform.
static void roll_hostile_garrison(const Game *g, int continent, Unit *out) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        out[i].id[0] = '\0';
        out[i].count = 0;
    }
    if (!g || !g->res) return;
    int continent_tier = continent & 3;
    int stacks = 1 + game_rng_next(0, 2);   // 1..3 stacks
    if (stacks > GAME_ARMY_SLOTS) stacks = GAME_ARMY_SLOTS;
    for (int s = 0; s < stacks; s++) {
        int kind = game_rng_next(0, 3);     // dwelling = rand(0,3)
        int chance = game_rng_next(1, 100);
        const char *tid = resources_spawn_troop(&g->res->spawn, kind, continent_tier, chance);
        if (!tid || !tid[0]) continue;
        const TroopDef *td = troop_by_id(tid);
        if (!td) continue;
        int base = td->tier_counts[continent_tier];
        if (base < 2) base = 2;
        int jitter = game_rng_next(0, base / 2);
        copy_id(out[s].id, sizeof(out[s].id), tid);
        out[s].count = base + jitter;
    }
}

static void add_foe(Game *g, int continent, const char *zone, int x, int y,
                    const char *placement_id, bool friendly,
                    bool is_static, const ResZoneArmy *explicit_army) {
    if (!g || !GameReserveFoes(g, g->foe_count + 1)) return;
    FoeState *f = &g->foes[g->foe_count++];
    copy_id(f->zone, sizeof(f->zone), zone);
    f->x = x;
    f->y = y;
    f->origin_x = x;   // pinned to the spawn tile; wander updates x/y, not this
    f->origin_y = y;
    copy_id(f->placement_id, sizeof(f->placement_id), placement_id);
    f->alive = true;
    f->friendly = friendly;
    f->is_static = is_static;
    // Explicit garrison (a hand-tuned guardian) if one was declared; otherwise
    // roll by zone tier. For friendlies the garrison is unused (recruit dialog
    // rolls a fresh creature), but populating it keeps save/load + tests uniform.
    if (explicit_army && explicit_army->army_stacks > 0) {
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            f->garrison[s].id[0] = '\0';
            f->garrison[s].count = 0;
        }
        for (int s = 0; s < explicit_army->army_stacks && s < GAME_ARMY_SLOTS; s++) {
            copy_id(f->garrison[s].id, sizeof(f->garrison[s].id),
                    explicit_army->army_id[s]);
            f->garrison[s].count = explicit_army->army_count[s];
        }
    } else {
        roll_hostile_garrison(g, continent, f->garrison);
    }
}

void salt_continent(Game *g, int continent, int min_artifacts, int min_navmaps,
                    int min_orbs, int min_telecaves, int min_dwellings,
                    int min_friendly) {
    if (!g || !g->res) return;
    if (continent < 0 || continent >= g->res->zone_count) return;

    const ResZone *z = &g->res->zones[continent];

    // Hostile foes: every army the zone declares (zones[].wandering_armies)
    // becomes a hostile foe, its garrison rolled or, for a guardian, its own.
    // Registered first so friendly foes appear after them in g->foes[] -- but
    // classification is by the per-foe `friendly` flag, not by index ordering.
    // The list is sized by the pack: no count is dropped.
    for (int i = 0; i < z->army_count; i++) {
        const ResZoneArmy *a = &z->armies[i];
        const char *aid = (a->id[0]) ? a->id : NULL;
        char fallback[32];
        if (!aid) {
            snprintf(fallback, sizeof(fallback), "static_foe_%d", i);
            aid = fallback;
        }
        add_foe(g, continent, z->id, a->x, a->y, aid, /*friendly=*/false,
                a->is_static, a);
    }

    // The barrel is every chest the salt may use: a "fixed" chest stays a
    // chest (a prize at the end of a path), so it is left out.
    int *slots = (int *)calloc((size_t)(z->chest_count > 0 ? z->chest_count : 1), sizeof(int));
    if (!slots) return;
    int barrel_len = 0;
    for (int i = 0; i < z->chest_count; i++)
        if (!z->chests[i].fixed) slots[barrel_len++] = i;
    int min_len = min_artifacts + min_navmaps + min_orbs +
                  min_telecaves + min_dwellings + min_friendly;

    if (min_len == 0) { free(slots); return; }   // nothing to place beyond static foes
    if (barrel_len < min_len) {
        fprintf(stdout,
                "salt_continent: zone '%s' has %d chests, need %d. "
                "Skipping.\n",
                z->id, barrel_len, min_len);
        free(slots);
        return;
    }

    // Allocate tag barrel.
    SaltKind *barrel = (SaltKind *)calloc((size_t)barrel_len, sizeof(SaltKind));
    if (!barrel) { free(slots); return; }

    // Tag the required number of each kind at random unclaimed positions.
    // This retries until unclaimed (OpenKB's play.c:222-234).
    #define TAG_N(count, kind) do {                                     \
        int _placed = 0;                                                \
        int _guard = 0;                                                 \
        while (_placed < (count) && _guard < barrel_len * 20) {         \
            int _bi = game_rng_next(0, barrel_len - 1);                 \
            if (barrel[_bi] == SALT_NONE) {                             \
                barrel[_bi] = (kind);                                   \
                _placed++;                                              \
            }                                                           \
            _guard++;                                                   \
        }                                                               \
    } while (0)

    TAG_N(min_artifacts, SALT_ARTIFACT);
    TAG_N(min_navmaps,   SALT_NAVMAP);
    TAG_N(min_orbs,      SALT_ORB);
    TAG_N(min_telecaves, SALT_TELECAVE);
    TAG_N(min_dwellings, SALT_DWELLING);
    TAG_N(min_friendly,  SALT_FRIENDLY);

    #undef TAG_N

    // Emit placements. For each tagged chest slot, add a SaltedPlacement
    // which MapLoadZoneWithPlacements will stamp when the zone is loaded.
    int artifact_counter = 0;
    int telecave_counter = 0;
    int dwelling_counter = 0;
    int foe_counter      = 0;
    int orb_counter      = 0;
    int navmap_counter   = 0;

    for (int i = 0; i < barrel_len; i++) {
        const ResZoneChest *slot = &z->chests[slots[i]];
        char id[32];
        switch (barrel[i]) {
            case SALT_ARTIFACT: {
                // Each (continent, slot) is fixed by
                // artifact_inversion[continent*2+slot]. We honour this by
                // looking up the artifact whose `zone == z->id` and
                // `local_idx == artifact_counter`.
                int ac = g->res->artifacts_count;
                int aidx = -1;
                for (int j = 0; j < ac; j++) {
                    const ArtifactDef *cand = artifact_by_index(j);
                    if (cand &&
                        strcmp(cand->zone, z->id) == 0 &&
                        cand->local_idx == artifact_counter) {
                        aidx = j;
                        break;
                    }
                }
                if (aidx < 0) { artifact_counter++; break; }
                const ArtifactDef *a = artifact_by_index(aidx);
                const char *aid = (a && a->id[0]) ? a->id : "";
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_ARTIFACT, aid);
                artifact_counter++;
                break;
            }
            case SALT_NAVMAP: {
                snprintf(id, sizeof(id), "navmap_%d", navmap_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_NAVMAP, id);
                navmap_counter++;
                break;
            }
            case SALT_ORB: {
                snprintf(id, sizeof(id), "orb_%d", orb_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_ORB, id);
                orb_counter++;
                break;
            }
            case SALT_TELECAVE: {
                snprintf(id, sizeof(id), "telecave_%d", telecave_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_TELECAVE, id);
                telecave_counter++;
                break;
            }
            case SALT_DWELLING: {
                const char *tid = salt_pick_dwelling_troop(g, continent,
                                                           dwelling_counter);
                if (!tid) { dwelling_counter++; break; }
                const TroopDef *td = troop_by_id(tid);
                if (!td) { dwelling_counter++; break; }
                // Derive kind from troop's dwells field. Catalog
                // uses singular ("hill"); dwelling_kind_to_interact handles
                // both singular and plural.
                Interact ik = dwelling_kind_to_interact(td->dwelling);
                snprintf(id, sizeof(id), "sd_%.20s_%d", tid, dwelling_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y, ik, id);
                // Pin the troop in the dwelling state row so first-visit
                // returns this exact troop, not a random one of the kind.
                enforce_dwelling_pinned(g, z->id, slot->x, slot->y, tid);
                dwelling_counter++;
                break;
            }
            case SALT_FRIENDLY: {
                // Friendly foes go into g->foes[] (not placements[]).
                // salt_continent -- friendlies are
                // registered in foe_coords[], not via static map data.
                snprintf(id, sizeof(id), "salt_foe_friendly_%d", foe_counter);
                add_foe(g, continent, z->id, slot->x, slot->y, id, /*friendly=*/true,
                        /*is_static=*/false, /*explicit_army=*/NULL);
                foe_counter++;
                break;
            }
            case SALT_NONE:
            default:
                break;
        }
    }

    free(barrel);
    free(slots);
}

void furnish_map(Game *g) {
    // furnish_map rewrites base terrain bytes into
    // edge-variant bytes at runtime. OpenBounty bakes edge variants
    // directly into the per-zone .dat files (see assets/kings-bounty/
    // data/*.dat), so this step is a no-op here. Retained only to mirror
    //  spawn_game call sequence.
    (void)g;
}

void clear_fog(Game *g) {
    //  reveals a 5x5 square around the hero in
    // game->fog[continent][y][x]. In openbounty, fog is not owned by
    // Game -- it lives in a standalone Fog struct in main.c, initialized
    // *after* GameInit. The equivalent reveal happens there via
    //   FogReveal(&fog, &map, game.position.x, game.position.y, ...);
    // using the same 5x5 shape (see fog.c). This call is retained as a
    // marker to preserve spawn_game sequence ordering.
    (void)g;
}

void bury_scepter(Game *g, int continent) {
    // Walk the continent's tile grid row-major, count plain-grass tiles,
    // and bury the scepter on the Nth one (treating grass tiles as
    // TERRAIN_GRASS with no interactive overlay).
    if (!g || !g->res) return;
    if (continent < 0 || continent >= g->res->zone_count) return;
    const ResZone *z = &g->res->zones[continent];
    copy_id(g->scepter.zone, sizeof(g->scepter.zone), z->id);
    g->scepter.x = -1;
    g->scepter.y = -1;

    Map *m = (Map *)calloc(1, sizeof(Map));
    if (!m) return;
    if (!MapLoadZone(m, g->res, z->id)) {
        MapFree(m); free(m);
        return;
    }
    // First pass: count grass tiles.
    int total = 0;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &MAP_TILE(m, x, y);
            if (t->terrain == TERRAIN_GRASS &&
                t->interactive == INTERACT_NONE &&
                !t->blocks_foot) {
                total++;
            }
        }
    }
    if (total <= 0) { MapFree(m); free(m); return; }
    int target = game_rng_next(0, total - 1);
    int count = 0;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &MAP_TILE(m, x, y);
            if (t->terrain != TERRAIN_GRASS) continue;
            if (t->interactive != INTERACT_NONE) continue;
            if (t->blocks_foot) continue;
            if (count == target) {
                g->scepter.x = x;
                g->scepter.y = y;
                MapFree(m); free(m);
                return;
            }
            count++;
        }
    }
    MapFree(m); free(m);
}

// player_accept_rank bumps leadership / spells / commission
// when the player ranks up. OpenBounty does the equivalent inline in
// GameMaybeRankUp (via class_stats_at_rank), so this entry point is a
// pure marker matching  spawn_game call order.
void player_accept_rank(Game *g) { (void)g; }

// Iterate villains
// by global index within each continent, picking random unowned castles
// until the continent's quota is filled. OpenBounty each VillainDef
// already declares its home zone, so we iterate the villain roster once
// and place each in a random unowned castle in its own zone.
//
// Castles flagged special.excluded_from_contract are skipped -- they are
// never held by villains even if in the same zone.
void salt_villains(Game *g) {
    if (!g || !g->res) return;

    int nvillains = g->res->villains_count;
    int ncastles  = g->res->castle_count;
    if (ncastles > g->castle_count) ncastles = g->castle_count;
    // The draw covers the castles up to the last one a contract may use:
    // no-contract castles at the end of the list (King's Bounty's King
    // Maximus) stay out of it, so every world keeps its villain placements.
    while (ncastles > 0 &&
           g->res->castles[ncastles - 1].special.excluded_from_contract)
        ncastles--;
    if (nvillains <= 0 || ncastles <= 0) return;

    for (int vi = 0; vi < nvillains; vi++) {
        const VillainDef *v = &g->res->villains[vi];
        if (!v->zone[0]) continue;   // villain has no home zone

        // Retry loop.
        // Guard against infinite spin when no castles match.
        int guard = 0;
        while (guard < ncastles * 20) {
            int ci = game_rng_next(0, ncastles - 1);
            const ResCastle *rc = &g->res->castles[ci];
            if (strcmp(rc->zone, v->zone) != 0) { guard++; continue; }
            if (rc->special.excluded_from_contract) { guard++; continue; }
            CastleRecord *cr = &g->castles[ci];
            if (cr->owner_kind != CASTLE_OWNER_MONSTERS) {
                guard++; continue;   // already owned by another villain
            }

            // Claim it.
            cr->owner_kind = CASTLE_OWNER_VILLAIN;
            copy_id(cr->villain_id, sizeof(cr->villain_id), v->id);
            for (int s = 0; s < GAME_ARMY_SLOTS && s < 5; s++) {
                copy_id(cr->garrison[s].id,
                        sizeof(cr->garrison[s].id),
                        v->army_troops[s]);
                cr->garrison[s].count = v->army_counts[s];
            }
            break;
        }
    }
}

// Port of . Picks a random troop id
// from a tier's pool via a chance-curve walk, then pulls the monster
// stack size from the chosen troop's tier_counts[tier].
//
// *out_id is written to the picked troop's resource id (empty string if
// the pool is unconfigured). *out_count is the stack size.
// : difficulty (= continent tier) governs
// the chance distribution; the dwelling kind is rolled fresh each call
// and indexes the troop pool independently.
static void roll_creature(Game *g, int tier,
                          char *out_id, size_t out_id_sz, int *out_count) {
    out_id[0] = '\0';
    if (out_count) *out_count = 0;
    if (!g || !g->res) return;
    if (tier < 0 || tier >= RES_SPAWN_TIERS) tier = 0;

    const ResSpawn *sp = &g->res->spawn;

    int kind = game_rng_next(0, 3);
    int chance = game_rng_next(1, 100);
    const char *troop_id = resources_spawn_troop(sp, kind, tier, chance);
    if (!troop_id[0]) return;

    const TroopDef *t = troop_by_id(troop_id);
    copy_id(out_id, out_id_sz, troop_id);

    int count = (t ? t->tier_counts[tier] : 0);
    // Force minimum stack of 2: if (troop_count <= 1) troop_count = 2;
    if (count <= 1) count = 2;
    if (out_count) *out_count = count;
}

// Port of . Fills the castle's
// garrison with 5 rolled troop stacks using the castle's difficulty_tier.
void repopulate_castle(Game *g, int castle_id) {
    if (!g || !g->res) return;
    if (castle_id < 0 || castle_id >= g->castle_count) return;
    if (castle_id >= g->res->castle_count) return;

    int tier = g->res->castles[castle_id].difficulty_tier;
    if (tier < 0 || tier >= RES_SPAWN_TIERS) tier = 0;

    CastleRecord *cr = &g->castles[castle_id];
    for (int s = 0; s < GAME_ARMY_SLOTS && s < 5; s++) {
        char tid[CAT_ID_LEN];
        int tcount = 0;
        roll_creature(g, tier, tid, sizeof(tid), &tcount);
        copy_id(cr->garrison[s].id, sizeof(cr->garrison[s].id), tid);
        cr->garrison[s].count = tcount;
    }
}

// Weekly-economy prediction queries -- the SAME quantities the week boundary below
// applies (end_day's week block). Planning layers read these to decide whether
// waiting a week accrues or bleeds gold; the formulas exist only here.
int GameStackWeeklyUpkeep(const char *troop_id, int count) {
    // Weekly upkeep is count * (recruit_cost/10), not full recruit_cost.
    if (!troop_id || !troop_id[0] || count <= 0) return 0;
    const TroopDef *t = troop_by_id(troop_id);
    return t ? count * (t->recruit_cost / 10) : 0;
}

int GameArmyWeeklyUpkeep(const Game *g) {
    if (!g) return 0;
    int upkeep = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        upkeep += GameStackWeeklyUpkeep(g->army[i].id, g->army[i].count);
    return upkeep;
}

int GameWeeklyNetGold(const Game *g) {
    if (!g) return 0;
    int net = g->stats.commission_weekly - GameArmyWeeklyUpkeep(g);
    if (g->boat.has_boat) {
        // Mirror end_day's order exactly: commission and upkeep land BEFORE
        // the fare check, so affordability is judged on the post-credit
        // wallet. (Judging on the pre-credit wallet predicted "repossessed,
        // not charged" for a poor hero whose commission covers the fare --
        // overstating weekly income by the whole fare.)
        int boat_cost = GameBoatCost(g);
        if (g->stats.gold + net >= boat_cost) net -= boat_cost;   // else repossessed, not charged
    }
    return net;
}

int GameWeeklyOutgoings(const Game *g) {
    // end_week's gross weekly debit (upkeep + boat fare). Unlike
    // GameWeeklyNetGold, the fare is counted unconditionally: this is what
    // the week CHARGES, not what the post-credit wallet can afford.
    if (!g) return 0;
    return GameArmyWeeklyUpkeep(g) + (g->boat.has_boat ? GameBoatCost(g) : 0);
}

int GameWeekId(const Game *g) {
    // Weeks elapsed since start -- the key GamePickAstrologyCreature is fed
    // at each week boundary (end_day below uses this same function).
    if (!g) return 0;
    int start_days = g->res->time.days_per_difficulty[0];
    if ((int)g->character.difficulty >= 0 &&
        (int)g->character.difficulty < 4) {
        start_days = g->res->time.days_per_difficulty[
            (int)g->character.difficulty];
    }
    int passed = start_days - g->stats.days_left;
    int wk = g->res->time.week_days;
    return (wk > 0) ? (passed / wk) : 0;
}

static void end_day(Game *g, bool *week_ended, int *commission_paid) {
    if (week_ended) *week_ended = false;
    if (commission_paid) *commission_paid = 0;

    if (g->stats.days_left > 0) g->stats.days_left--;
    g->stats.steps_left_today = g->res->time.day_steps;
    g->stats.time_stop = 0;

    if (g->stats.days_left == 0) {
        g->stats.game_over = true;
        return;
    }
    int wk = g->res->time.week_days;
    if (wk > 0 && g->stats.days_left % wk == 0) {
        // Week boundary. end_week order:
        //   reset time_stop, leadership
        //   roll astrology creature
        //   credit commission, debit upkeep + boat
        //   ghosts -> peasants on astrology=peasants week
        //   repopulate matching dwellings
        g->stats.time_stop = 0;
        g->stats.leadership_current = g->stats.leadership_base;

        // Week id: weeks elapsed since start.
        int week_id = GameWeekId(g);
        int astrology = GamePickAstrologyCreature(g, week_id);
        g->stats.last_astrology_troop = astrology;

        g->stats.gold += g->stats.commission_weekly;
        g->stats.last_commission = g->stats.commission_weekly;

        g->stats.gold -= GameArmyWeeklyUpkeep(g);

        if (g->boat.has_boat) {
            int boat_cost = GameBoatCost(g);
            if (g->stats.gold >= boat_cost) {
                g->stats.gold -= boat_cost;
            } else {
                // Can't afford boat -- it's repossessed.
                g->boat.has_boat = false;
                g->boat.x = -1;
                g->boat.y = -1;
            }
        }
        if (g->stats.gold < 0) g->stats.gold = 0;

        // Astrology: full repopulate of matching dwellings; grow others;
        // ghosts -> peasants when creature == peasants.
        GameApplyAstrology(g, astrology);

        // Player castles do NOT auto-repopulate. The hero garrisons
        // them by transferring troops in via the own_castle screen;
        // empty player castles stay empty until the player visits and
        // deposits a stack. The astrology growth block below correctly
        // skips player- and special-owned castles for the same reason.
        //
        // (Earlier this loop called repopulate_castle() -- the monster
        // spawner -- on empty player castles, which silently stuffed
        // captured castles with random monster garrisons every week.)

        // : weekly astrology growth.
        // For every non-player-owned castle, stacks whose troop matches
        // the astrology creature grow by troop.growth_per_week. Hostile
        // foes on the overworld grow by the same rule (play.c:1028-1031).
        const TroopDef *astro = troop_by_index(astrology);
        if (astro && astro->id[0] && astro->growth_per_week > 0) {
            for (int i = 0; i < g->castle_count; i++) {
                if (!g->castles[i].id[0]) continue;
                if (g->castles[i].owner_kind == CASTLE_OWNER_PLAYER ||
                    g->castles[i].owner_kind == CASTLE_OWNER_SPECIAL) continue;
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    if (g->castles[i].garrison[s].count == 0) continue;
                    if (strcmp(g->castles[i].garrison[s].id, astro->id) != 0)
                        continue;
                    g->castles[i].garrison[s].count += astro->growth_per_week;
                }
            }
            for (int i = 0; i < g->foe_count; i++) {
                FoeState *f = &g->foes[i];
                if (!f->alive) continue;
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    if (f->garrison[s].count == 0) continue;
                    if (strcmp(f->garrison[s].id, astro->id) != 0) continue;
                    f->garrison[s].count += astro->growth_per_week;
                }
            }
        }

        if (week_ended) *week_ended = true;
        if (commission_paid) *commission_paid = g->stats.commission_weekly;
    }
}

void GameOnStep(Game *g, bool terrain_is_desert,
                bool *day_ended, bool *week_ended, int *commission_paid) {
    if (day_ended)        *day_ended = false;
    if (week_ended)       *week_ended = false;
    if (commission_paid)  *commission_paid = 0;

    if (g->stats.game_over) return;

    // time_stop absorbs a step without advancing the day.
    if (g->stats.time_stop > 0) {
        g->stats.time_stop--;
        return;
    }

    if (terrain_is_desert) {
        g->stats.steps_left_today = 0;
    } else if (g->stats.steps_left_today > 0) {
        g->stats.steps_left_today--;
    }

    // Handle any day rollovers. Desert only zeroes once, but the loop
    // tolerates effects that push steps further negative.
    while (g->stats.steps_left_today <= 0 && !g->stats.game_over) {
        bool we = false;
        int  paid = 0;
        end_day(g, &we, &paid);
        if (day_ended) *day_ended = true;
        if (we)        { if (week_ended) *week_ended = true; }
        if (paid > 0)  { if (commission_paid) *commission_paid = paid; }
    }

    // State trace hook. Fires once per step regardless of day/week
    // rollover; the snapshot reflects the post-step state including
    // rolled-over day count.
    recorder_capture("step");
    audio_play_tune(AUDIO_TUNE_WALK);
}

bool GameIsOver(const Game *g) {
    return g->stats.game_over || g->stats.days_left == 0;
}

int GameSpendDays(Game *g, int days, int *total_commission) {
    if (total_commission) *total_commission = 0;
    if (!g) return 0;
    if (days > g->stats.days_left) days = g->stats.days_left;
    int weeks = 0;
    for (int i = 0; i < days && !g->stats.game_over; i++) {
        bool we = false; int paid = 0;
        end_day(g, &we, &paid);
        if (we) weeks++;
        if (paid > 0 && total_commission) *total_commission += paid;
    }
    return weeks;
}

int GameSpendWeek(Game *g, int *total_commission) {
    if (!g) return 0;
    int week_days = g->res ? g->res->time.week_days : 5;
    if (week_days < 1) week_days = 5;
    // Days remaining in the current week: how many more end_day() calls
    // land on a multiple of week_days. end_week_days =
    // WEEK_DAYS - (passed_days % WEEK_DAYS).
    // passed_days isn't tracked directly; derive from days_left and the
    // starting count.
    int start_days = g->res ? g->res->time.days_per_difficulty[0] : 900;
    if ((int)g->character.difficulty >= 0 &&
        (int)g->character.difficulty < 4 && g->res) {
        start_days = g->res->time.days_per_difficulty[(int)g->character.difficulty];
    }
    int passed = start_days - g->stats.days_left;
    int into_week = passed % week_days;
    int to_spend = week_days - into_week;
    if (to_spend <= 0) to_spend = week_days;
    return GameSpendDays(g, to_spend, total_commission);
}

bool GameClaimArtifact(Game *g, int idx) {
    if (idx < 0 || idx >= g->artifacts.count) return false;
    if (g->artifacts.found[idx]) return false;
    g->artifacts.found[idx] = true;

    const ArtifactDef *a = artifact_by_index(idx);
    if (!a) return true;
    switch (a->power) {
        case ARTIFACT_POWER_DOUBLE_LEADERSHIP:
            g->stats.leadership_base    *= 2;
            g->stats.leadership_current  = g->stats.leadership_base;
            break;
        case ARTIFACT_POWER_INCREASE_COMMISSION:
            g->stats.commission_weekly += 2000;
            break;
        case ARTIFACT_POWER_DOUBLE_SPELL_POWER:
            g->stats.spell_power *= 2;
            break;
        case ARTIFACT_POWER_DOUBLE_MAX_SPELLS:
            g->stats.max_spells *= 2;
            break;
        // Damage/protection apply in combat; cheaper boats is a passive query;
        // unknown is a no-op.
        default: break;
    }
    {
        char tag[64];
        snprintf(tag, sizeof tag, "artifact:%s", a->id);
        recorder_capture(tag);
    }
    return true;
}

bool GameHasPower(const Game *g, ArtifactPower power) {
    for (int i = 0; i < g->artifacts.count; i++) {
        const ArtifactDef *a = artifact_by_index(i);
        if (g->artifacts.found[i] && a && a->power == power) return true;
    }
    return false;
}

void GameAddConsumed(Game *g, const char *zone, int x, int y) {
    if (!g || !zone) return;
    for (int i = 0; i < g->consumed_count; i++) {
        if (g->consumed[i].x == x && g->consumed[i].y == y &&
            strcmp(g->consumed[i].zone, zone) == 0) return;
    }
    if (!GameReserveConsumed(g, g->consumed_count + 1)) return;
    TileMutation *m = &g->consumed[g->consumed_count++];
    copy_id(m->zone, sizeof(m->zone), zone);
    m->x = x;
    m->y = y;
}

void GameApplyTileMutations(const Game *g, Map *map, const char *zone) {
    if (!g || !map || !zone) return;
    for (int i = 0; i < g->consumed_count; i++) {
        const TileMutation *m = &g->consumed[i];
        if (strcmp(m->zone, zone) != 0) continue;
        MapClearInteractive(map, m->x, m->y);
    }
    // A vista that has played changed the map for good (its bridge, its cleared
    // pass): re-apply those tiles every time the zone loads.
    const ResZone *z = g->res ? resources_zone_by_id(g->res, zone) : NULL;
    for (int i = 0; z && i < g->events_done_count; i++) {
        if (strcmp(g->events_done[i].zone, zone) != 0) continue;
        for (int k = 0; k < z->event_count; k++) {
            const ResZoneEvent *ev = &z->events[k];
            if (strcmp(ev->id, g->events_done[i].id) != 0) continue;
            for (int e = 0; e < ev->effect_count; e++)
                MapSetTileFromCode(map, g->res, ev->effects[e].x,
                                   ev->effects[e].y, ev->effects[e].code);
        }
    }
}

bool GameEventFired(const Game *g, const char *zone, const char *id) {
    if (!g || !zone || !id) return false;
    for (int i = 0; i < g->events_done_count; i++)
        if (strcmp(g->events_done[i].zone, zone) == 0 &&
            strcmp(g->events_done[i].id, id) == 0) return true;
    return false;
}

// How much of one precondition the hero holds right now.
static int event_req_held(const Game *g, const ResEventReq *rq) {
    switch (rq->kind) {
    case RES_EVENT_REQ_SPELL: {
        int idx = spell_index_by_id(rq->id);
        return (idx >= 0 && idx < g->spells.count) ? g->spells.counts[idx] : 0;
    }
    case RES_EVENT_REQ_TROOP: {
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (strcmp(g->army[i].id, rq->id) == 0) n += g->army[i].count;
        return n;
    }
    case RES_EVENT_REQ_GOLD:
        return g->stats.gold;
    case RES_EVENT_REQ_ARTIFACT: {
        const ArtifactDef *a = artifact_by_id(rq->id);
        return (a && a->index >= 0 && a->index < g->artifacts.count &&
                g->artifacts.found[a->index]) ? 1 : 0;
    }
    }
    return 0;
}

// Spend what a precondition says it consumes (troops and artifacts are held,
// never spent: a vista asks the hero to bring them, not to give them up).
static void event_req_spend(Game *g, const ResEventReq *rq) {
    if (!rq->consume) return;
    switch (rq->kind) {
    case RES_EVENT_REQ_SPELL: {
        int idx = spell_index_by_id(rq->id);
        if (idx >= 0 && idx < g->spells.count) {
            g->spells.counts[idx] -= rq->count;
            if (g->spells.counts[idx] < 0) g->spells.counts[idx] = 0;
        }
        break;
    }
    case RES_EVENT_REQ_GOLD:
        g->stats.gold -= rq->count;
        if (g->stats.gold < 0) g->stats.gold = 0;
        break;
    case RES_EVENT_REQ_TROOP:
    case RES_EVENT_REQ_ARTIFACT:
        break;
    }
}

bool GameTryFireEvent(Game *g, Map *map, int x, int y) {
    if (!g || !map || !g->res || !g->position.zone[0]) return false;
    const ResZone *z = resources_zone_by_id(g->res, g->position.zone);
    if (!z) return false;
    for (int k = 0; k < z->event_count; k++) {
        const ResZoneEvent *ev = &z->events[k];
        if (ev->x != x || ev->y != y) continue;
        if (GameEventFired(g, g->position.zone, ev->id)) return false;
        for (int q = 0; q < ev->req_count; q++)
            if (event_req_held(g, &ev->reqs[q]) < ev->reqs[q].count) return false;
        if (!GameReserveEventsDone(g, g->events_done_count + 1)) return false;
        for (int q = 0; q < ev->req_count; q++) event_req_spend(g, &ev->reqs[q]);
        for (int e = 0; e < ev->effect_count; e++)
            MapSetTileFromCode(map, g->res, ev->effects[e].x, ev->effects[e].y,
                               ev->effects[e].code);
        EventFired *done = &g->events_done[g->events_done_count++];
        copy_id(done->zone, sizeof done->zone, g->position.zone);
        copy_id(done->id, sizeof done->id, ev->id);
        player_io_note_scene_event(g, ev->title, ev->body, ev->scene_index);
        return true;
    }
    return false;
}

int GameKnownSpells(const Game *g) {
    int total = 0;
    for (int i = 0; i < g->spells.count; i++) total += g->spells.counts[i];
    return total;
}

int GameBoatCost(const Game *g) {
    if (GameHasPower(g, ARTIFACT_POWER_CHEAPER_BOATS))
        return g->res->economy.boat_cost_cheap;
    return g->res->economy.boat_cost_normal;
}

BoatActionResult GameRentBoat(Game *g, int boat_x, int boat_y,
                              const char *zone_id) {
    if (!g) return BOAT_RENT_NO_GOLD;
    // Boats are rented ONLY at a town (the town menu's Boat row). Reject an
    // off-map rental so no caller can conjure a boat without visiting a town.
    if (!g->position.in_town[0]) return BOAT_RENT_NO_GOLD;
    int cost = GameBoatCost(g);
    // KB: `if (gold <= boat_cost)` -- exact-match also fails (no free boat
    // when gold equals the cost).
    if (g->stats.gold <= cost) return BOAT_RENT_NO_GOLD;
    g->stats.gold -= cost;
    // Zero the WHOLE BoatState first (padding + the zone[] tail past the terminator): writing only
    // the live fields leaves stale bytes from a previous boat, which are meaningless but part of the
    // serialized/fingerprinted byte image -- two runs that reach the same logical boat via different
    // histories would then differ byte-for-byte (a determinism regression).
    memset(&g->boat, 0, sizeof g->boat);
    g->boat.has_boat = true;
    g->boat.x = boat_x;
    g->boat.y = boat_y;
    size_t m = 0;
    if (zone_id) {
        while (m + 1 < sizeof(g->boat.zone) && zone_id[m]) {
            g->boat.zone[m] = zone_id[m]; m++;
        }
    }
    g->boat.zone[m] = '\0';
    return BOAT_RENT_OK;
}

BoatActionResult GameCancelBoat(Game *g) {
    if (!g) return BOAT_CANCEL_OK;
    // Cancelling a rental is a town-menu action too (the Boat row toggles).
    if (!g->position.in_town[0]) return BOAT_CANCEL_OK;
    // KB: refuses cancellation while sailing; must disembark first.
    if (g->travel_mode == TRAVEL_BOAT) return BOAT_CANCEL_AT_SEA;
    memset(&g->boat, 0, sizeof g->boat);   // whole BoatState (padding + zone tail) -> canonical no-boat image
    g->boat.has_boat = false;
    g->boat.x = -1;
    g->boat.y = -1;
    return BOAT_CANCEL_OK;
}

SiegeBuyResult GameBuySiege(Game *g) {
    if (!g) return SIEGE_BUY_NO_GOLD;
    // Siege weapons are bought ONLY at a town (the town menu's Siege row). Reject
    // an off-map call so no caller can acquire them without visiting a town.
    if (!g->position.in_town[0]) return SIEGE_BUY_NO_GOLD;
    if (g->stats.siege_weapons) return SIEGE_BUY_ALREADY;
    int cost = g->res->economy.siege_cost;
    if (g->stats.gold <= cost) return SIEGE_BUY_NO_GOLD;  // KB: <= fails
    g->stats.gold -= cost;
    g->stats.siege_weapons = 1;
    return SIEGE_BUY_OK;
}

bool GameFoeCanEvade(const Game *g, const Map *map) {
    if (!g || !map) return false;
    if (!g->res || !g->res->economy.evade_needs_free_square) return true;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int x = g->position.x + dx, y = g->position.y + dy;
            const Tile *t = MapGetTile(map, x, y);
            if (!t || t->interactive != INTERACT_NONE) continue;
            bool ok = (g->character.mount == MOUNT_FLY) ? adventure_walkable_in_flight(t)
                    : (g->travel_mode == TRAVEL_BOAT)   ? (t->terrain == TERRAIN_WATER || t->is_bridge)
                    :                                     adventure_walkable_on_foot(t);
            if (!ok) continue;
            bool foe_here = false;
            for (int i = 0; i < g->foe_count && !foe_here; i++) {
                const FoeState *f = &g->foes[i];
                foe_here = f->alive && f->x == x && f->y == y &&
                           strcmp(f->zone, g->position.zone) == 0;
            }
            if (!foe_here) return true;
        }
    }
    return false;
}

int GameAlcoveCost(const Game *g, const char *zone_id) {
    if (!g || !g->res) return 0;
    const ResZone *z = zone_id ? resources_zone_by_id(g->res, zone_id) : NULL;
    return (z && z->alcove_cost >= 0) ? z->alcove_cost : g->res->economy.alcove_cost;
}

bool GameHasRites(const Game *g, const char *zone_id) {
    if (!g) return false;
    if (!g->res || !g->res->economy.rites_per_zone) return g->stats.knows_magic;
    int zi = zone_id ? resources_zone_index(g->res, zone_id) : -1;
    return zi >= 0 && zi < g->world.zone_count && g->world.zone_rites[zi];
}

bool GameTownHasRites(const Game *g, const char *town_id) {
    if (!g || !g->res) return false;
    if (!g->res->economy.rites_per_zone) return true;   // one magic: towns sell to anyone
    const ResTown *t = town_id ? resources_town_by_id(g->res, town_id) : NULL;
    return t && GameHasRites(g, t->zone);
}

SpellBuyResult GameBuySpell(Game *g, const char *town_id) {
    if (!g || !town_id || !town_id[0]) return SPELL_BUY_NO_SPELL;
    // A spell is bought ONLY at the town you are visiting (the town menu's Spell
    // row). Require the hero to be AT this town -- reject an off-map call or one
    // naming a town the hero is not in.
    if (strcmp(g->position.in_town, town_id) != 0) return SPELL_BUY_NO_SPELL;
    const TownRecord *t = NULL;
    for (int i = 0; i < g->town_count; i++)
        if (strcmp(g->towns[i].id, town_id) == 0) { t = &g->towns[i]; break; }
    const SpellDef *sp =
        (t && t->spell_for_sale[0]) ? spell_by_id(t->spell_for_sale) : NULL;
    if (!sp) return SPELL_BUY_NO_SPELL;
    if (g->res && g->res->economy.rites_per_zone && !GameTownHasRites(g, town_id))
        return SPELL_BUY_NO_RITES;
    if (GameKnownSpells(g) >= g->stats.max_spells) return SPELL_BUY_AT_CAP;
    if (g->stats.gold <= sp->cost) return SPELL_BUY_NO_GOLD;  // KB: <= fails
    g->spells.counts[sp->index]++;
    g->stats.gold -= sp->cost;
    return SPELL_BUY_OK;
}

TownRecord *GameTouchTown(Game *g, const char *town_id) {
    if (!town_id || !town_id[0]) return NULL;
    // Existing slot? Touching a known town marks it visited (the Town
    // Gate spell filters on this flag); first-visit also gets it set
    // below via the fresh-slot path.
    for (int i = 0; i < g->town_count; i++) {
        if (strcmp(g->towns[i].id, town_id) == 0) {
            g->towns[i].visited = true;
            return &g->towns[i];
        }
    }
    // Allocate a fresh slot.
    for (int i = 0; i < g->town_count; i++) {
        if (g->towns[i].id[0]) continue;
        TownRecord *t = &g->towns[i];
        copy_id(t->id, sizeof(t->id), town_id);
        t->visited = true;
        // Deterministic spell: seed xor town slot index, modulo spells_count().
        // We don't
        // carry that hardcoded pairing; any spell is fair game.
        // uint64_t, not unsigned long: the latter is 32-bit on Windows and
        // 64-bit elsewhere, which gave the same world different town spells
        // per platform once the seed carried entropy above bit 31.
        int idx = (int)((g->seed ^ (uint64_t)(i + 1)) % (uint64_t)spells_count());
        const SpellDef *sp = spell_by_index(idx);
        if (sp) copy_id(t->spell_for_sale, sizeof(t->spell_for_sale), sp->id);
        else    t->spell_for_sale[0] = '\0';
        return t;
    }
    return NULL;
}

bool GameVillainContractObtainable(const Game *g, const char *villain_id) {
    // A villain's contract can be taken right now iff its id is currently in one
    // of the active contract-cycle slots (or is already the active contract). A
    // villain that has not yet rotated into the cycle is PREDICTABLY unobtainable
    // until an earlier villain is captured (GameFulfillContract refills its slot)
    // -- a deterministic, O(cycle_length) check with no simulation. The planner
    // uses this to skip such villains INSTANTLY instead of probing them.
    if (!g || !villain_id || !villain_id[0]) return false;
    if (g->contract.active_id[0] &&
        strcmp(g->contract.active_id, villain_id) == 0) return true;
    int n = g->res ? g->res->contract.cycle_length : 0;
    if (n > g->contract.cycle_count) n = g->contract.cycle_count;
    for (int i = 0; i < n; i++)
        if (strcmp(g->contract.cycle[i], villain_id) == 0) return true;
    return false;
}

const char *GameTakeNextContract(Game *g) {
    // Contracts are taken ONLY at a town (the player's only path is the town
    // menu's "Get New Contract" row). The engine enforces it so no caller --
    // autoplay included -- can take a contract from the open map.
    if (!g || !g->position.in_town[0]) return NULL;
    int n = g->contract.cycle_count;
    if (n < 1) return NULL;
    g->contract.last_contract++;
    if (g->contract.last_contract > n - 1) g->contract.last_contract = 0;
    const char *vid = g->contract.cycle[g->contract.last_contract];
    if (!vid[0]) return NULL;
    copy_id(g->contract.active_id, sizeof(g->contract.active_id), vid);
    {
        char tag[64];
        snprintf(tag, sizeof tag, "contract:new:%s", vid);
        recorder_capture(tag);
    }
    return g->contract.active_id;
}

const char *GameTakeContractAt(Game *g, int slot) {
    if (!g || !g->position.in_town[0]) return NULL;
    int n = g->contract.cycle_count;
    if (slot < 0 || slot >= n) return NULL;
    const char *vid = g->contract.cycle[slot];
    if (!vid[0]) return NULL;
    g->contract.last_contract = slot;
    copy_id(g->contract.active_id, sizeof(g->contract.active_id), vid);
    {
        char tag[64];
        snprintf(tag, sizeof tag, "contract:new:%s", vid);
        recorder_capture(tag);
    }
    return g->contract.active_id;
}

bool GameFulfillContract(Game *g, const char *villain_id) {
    if (!g || !villain_id || !villain_id[0]) return false;
    const VillainDef *v = villain_by_id(villain_id);
    if (!v) return false;

    g->stats.gold += v->reward;
    if (v->index >= 0 && v->index < g->contract.villain_count) {
        g->contract.villains_caught[v->index] = true;
    }

    int cycle_len = g->contract.cycle_count;

    int slot = -1;
    for (int i = 0; i < cycle_len; i++) {
        if (strcmp(g->contract.cycle[i], villain_id) == 0) { slot = i; break; }
    }

    if (g->contract.active_id[0] &&
        strcmp(g->contract.active_id, villain_id) == 0) {
        g->contract.active_id[0] = '\0';
    }

    if (slot < 0) return false;

    g->contract.cycle[slot][0] = '\0';

    int total = villains_count();
    for (int i = g->contract.max_contract; i < total; i++) {
        const VillainDef *cand = villain_by_index(i);
        if (!cand) continue;
        if (cand->index >= 0 && cand->index < g->contract.villain_count &&
            g->contract.villains_caught[cand->index]) continue;
        copy_id(g->contract.cycle[slot],
                sizeof(g->contract.cycle[slot]), cand->id);
        break;
    }
    g->contract.max_contract++;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "contract:done:%s", villain_id);
        recorder_capture(tag);
    }
    return true;
}

bool GameMaybeRankUp(Game *g) {
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls) return false;
    int caught = GameVillainsCaught(g);
    int r = g->character.cls.rank_index;
    // Promote while caught count meets the next rank's threshold.
    bool changed = false;
    while (r < 3 && caught >= cls->ranks[r + 1].villains_needed) {
        r++;
        changed = true;
    }
    if (!changed) return false;
    int old_r = g->character.cls.rank_index;
    g->character.cls.rank_index = r;
    copy_id(g->character.cls.rank_id, sizeof(g->character.cls.rank_id),
            cls->ranks[r].id);
    copy_id(g->character.cls.rank_title, sizeof(g->character.cls.rank_title),
            cls->ranks[r].name);
    // Promotion applies the new ranks' bonuses ADDITIVELY (openkb: each stat
    // is an accumulator -- "base_leadership never decreases", spell_power /
    // max_spells / commission "increment from class ranks"). Assigning the
    // class-table cumulative here instead WIPED every chest-accumulated
    // bonus (leadership, spell power, max spells, commission) at each
    // promotion -- a fidelity bug that starved late-game heroes.
    int lead0 = 0, maxsp0 = 0, spp0 = 0, comm0 = 0;
    int lead1 = 0, maxsp1 = 0, spp1 = 0, comm1 = 0;
    class_stats_at_rank(cls, old_r, &lead0, &maxsp0, &spp0, &comm0);
    class_stats_at_rank(cls, r,     &lead1, &maxsp1, &spp1, &comm1);
    g->stats.leadership_base   += lead1 - lead0;
    g->stats.commission_weekly += comm1 - comm0;
    g->stats.max_spells        += maxsp1 - maxsp0;
    g->stats.spell_power       += spp1 - spp0;
    // The current cap gains the same delta (it re-syncs to base at end_week).
    g->stats.leadership_current += lead1 - lead0;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "rank:%s", g->character.cls.rank_id);
        recorder_capture(tag);
    }
    return true;
}

GameAudienceOutcome GameAudienceWithKing(Game *g, int *out_needed) {
    if (out_needed) *out_needed = 0;
    const ClassDef *cls = class_by_id(g->character.cls.id);
    int rank = g->character.cls.rank_index;
    if (!cls || rank + 1 >= cls->rank_count) return GAME_AUDIENCE_FINAL_RANK;
    int needed = cls->ranks[rank + 1].villains_needed - GameVillainsCaught(g);
    if (needed > 0) {
        if (out_needed) *out_needed = needed;
        return GAME_AUDIENCE_MORE_NEEDED;
    }
    GameMaybeRankUp(g);
    return GAME_AUDIENCE_PROMOTED;
}

// pct of v, at least 1 when v is above 0.
static int pct_gain(int v, int pct) {
    if (v <= 0 || pct <= 0) return 0;
    int d = (int)((long long)v * pct / 100);
    return d < 1 ? 1 : d;
}

GameBlessingOutcome GameSeekBlessing(Game *g, int *out_needed, GameAudienceGain *gain) {
    if (out_needed) *out_needed = 0;
    if (gain) memset(gain, 0, sizeof *gain);
    if (g->stats.blessed) return GAME_BLESSING_ALREADY;
    int total = g->artifacts.count;
    int missing = total - GameArtifactsFound(g);
    if (missing > 0) {
        if (out_needed) *out_needed = missing;
        return GAME_BLESSING_NEED_ARTIFACTS;
    }
    int d = pct_gain(g->stats.leadership_base, g->res->economy.blessing_leadership_pct);
    g->stats.leadership_base += d;
    g->stats.leadership_current += d;   // re-syncs to base at end_week
    g->stats.blessed = true;
    if (gain) gain->leadership = d;
    return GAME_BLESSING_GRANTED;
}

bool GamePayTribute(Game *g, int *out_needed, GameAudienceGain *gain) {
    const ResEconomy *ec = &g->res->economy;
    if (out_needed) *out_needed = 0;
    if (gain) memset(gain, 0, sizeof *gain);
    if (g->stats.gold < ec->tribute_cost) {
        if (out_needed) *out_needed = ec->tribute_cost - g->stats.gold;
        return false;
    }
    g->stats.gold -= ec->tribute_cost;
    int dl = pct_gain(g->stats.leadership_base, ec->tribute_leadership_pct);
    int dp = pct_gain(g->stats.spell_power, ec->tribute_magic_pct);
    int dm = pct_gain(g->stats.max_spells, ec->tribute_magic_pct);
    g->stats.leadership_base += dl;
    g->stats.leadership_current += dl;
    g->stats.spell_power += dp;
    g->stats.max_spells += dm;
    g->stats.tributes++;
    if (gain) { gain->leadership = dl; gain->spell_power = dp; gain->max_spells = dm; }
    return true;
}

int GameArmyTotalLeadership(const Game *g) {
    int total = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0]) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        if (!t) continue;
        total += t->hit_points * g->army[i].count;
    }
    return total;
}

int GameArmyStackCount(const Game *g) {
    int n = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        if (g->army[i].id[0]) n++;
    return n;
}

int GameComputeScore(const Game *g) {
    if (!g) return 0;
    const ResScoring *sc = &g->res->economy.scoring;
    int score = sc->per_villain  * GameVillainsCaught(g)
              + sc->per_artifact * GameArtifactsFound(g)
              + sc->per_castle   * GameCastlesOwned(g)
              - sc->kill_penalty * g->stats.followers_killed;
    int d = (int)g->character.difficulty;
    if (sc->easy_halves && d == DIFFICULTY_EASY) {
        score /= 2;
    } else if (d >= 1 && d < 5) {
        score *= sc->difficulty_multiplier[d];
    }
    if (score < 0) score = 0;
    return score;
}

int GameVillainsCaught(const Game *g) {
    // villains_caught[] is keyed by VillainDef.index, which load validation
    // constrains to [0, villains_count) and uniqueness.
    int n = 0;
    for (int i = 0; i < g->contract.villain_count; i++)
        if (g->contract.villains_caught[i]) n++;
    return n;
}

int GameArtifactsFound(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->artifacts.count; i++)
        if (g->artifacts.found[i]) n++;
    return n;
}

// A town whose informant reports on the sacred artifacts rather than on a
// castle: which one he names, and where it lies. Candidates are the artifacts
// not yet found -- salt-placed ones from g->placements[], JSON-authored ones
// from the zone catalog -- and the choice is a pure hash of the seed and the
// town, never a draw from the game RNG, so a given seed always answers alike.
// As artifacts are claimed the list shrinks and the town names another.
bool GameTownArtifactIntel(const Game *g, const char *town_id,
                           char *out_zone, int zone_cap, int *out_x, int *out_y) {
    if (!g || !g->res || !town_id) return false;
    const Resources *r = g->res;

    // Gather the unfound ones, in a fixed order (placements, then catalog).
    int cand_cap = g->placement_count;
    for (int z = 0; z < r->zone_count; z++) cand_cap += r->zones[z].artifact_count;
    if (cand_cap <= 0) return false;
    struct IntelCand { const char *zone; int x, y; } *cand = malloc((size_t)cand_cap * sizeof *cand);
    if (!cand) return false;
    int n = 0;
    for (int i = 0; i < g->placement_count && n < cand_cap; i++) {
        const SaltedPlacement *p = &g->placements[i];
        if (p->kind != INTERACT_ARTIFACT) continue;
        const ArtifactDef *a = artifact_by_id(p->id);
        if (!a || g->artifacts.found[a->index]) continue;
        cand[n].zone = p->zone; cand[n].x = p->x; cand[n].y = p->y; n++;
    }
    for (int z = 0; z < r->zone_count && n < cand_cap; z++) {
        const ResZone *rz = &r->zones[z];
        for (int i = 0; i < rz->artifact_count && n < cand_cap; i++) {
            const ArtifactDef *a = artifact_by_id(rz->artifacts[i].id);
            if (!a || g->artifacts.found[a->index]) continue;
            cand[n].zone = rz->id; cand[n].x = rz->artifacts[i].x; cand[n].y = rz->artifacts[i].y; n++;
        }
    }
    if (n == 0) { free(cand); return false; }

    // A pure hash of the seed and the town's id: stable, and different towns
    // name different chests.
    uint64_t h = g->seed * 1099511628211ull;
    for (const char *p = town_id; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    h ^= h >> 29;
    int pick = (int)(h % (uint64_t)n);

    if (out_zone && zone_cap > 0) snprintf(out_zone, (size_t)zone_cap, "%s", cand[pick].zone);
    if (out_x) *out_x = cand[pick].x;
    if (out_y) *out_y = cand[pick].y;
    free(cand);
    return true;
}

int GameCastlesOwned(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->castle_count; i++) {
        if (!g->castles[i].id[0]) continue;
        if (g->castles[i].owner_kind == CASTLE_OWNER_PLAYER) n++;
    }
    return n;
}

// ---- Treasure chest rolls  ---------------
// Curves and value ranges live in res->economy.chest (game.json economy.chest).
// Defaults match ..503 when game.json omits the block.

// Deterministic pseudo-random per (seed, x, y) so the same chest gives the
// same outcome on replay.
static unsigned chest_rand(const Game *g, int x, int y, unsigned salt) {
    unsigned h = (unsigned)g->seed ^ 0xC0FFEEu;
    h ^= (unsigned)(x * 131u);
    h ^= (unsigned)(y * 97u);
    h ^= salt;
    h = h * 1664525u + 1013904223u;
    return h;
}

ChestOutcome GamePeekChest(const Game *g, int zone_index, int x, int y,
                           ChestPending *out_pending) {
    if (out_pending) {
        out_pending->pending_gold = 0;
        out_pending->pending_leadership = 0;
    }
    int zi = (zone_index >= 0 && zone_index < 4) ? zone_index : 0;
    const ResChest *ch = &g->res->economy.chest;
    int chance = (int)(chest_rand(g, x, y, 1) % 100u) + 1;
    if (chance < ch->chance_gold[zi]) {
        int points = (int)(chest_rand(g, x, y, 2) %
                           (unsigned)(ch->gold_max[zi] > 0 ? ch->gold_max[zi] : 1)) + 1;
        points += ch->gold_min[zi];
        int gold = points * 100;
        int leadership = gold / 50;
        if (GameHasPower(g, ARTIFACT_POWER_DOUBLE_LEADERSHIP)) leadership *= 2;
        if (out_pending) {
            out_pending->pending_gold = gold;
            out_pending->pending_leadership = leadership;
        }
        return CHEST_OUTCOME_GOLD;
    }
    if (chance < ch->chance_commission[zi]) return CHEST_OUTCOME_COMMISSION;
    if (chance < ch->chance_spell_power[zi]) return CHEST_OUTCOME_SPELL_POWER;
    if (chance < ch->chance_max_spells[zi]) return CHEST_OUTCOME_MAX_SPELLS;
    if (chance < ch->chance_new_spell[zi]) {
        if (spells_count() <= 0) return CHEST_OUTCOME_EMPTY;
        return CHEST_OUTCOME_NEW_SPELL;
    }
    return CHEST_OUTCOME_EMPTY;
}

ChestOutcome GameRollChest(Game *g, int zone_index, int x, int y,
                           char *out_body, int out_sz,
                           ChestPending *out_pending) {
    if (out_pending) {
        out_pending->pending_gold = 0;
        out_pending->pending_leadership = 0;
    }
    int zi = (zone_index >= 0 && zone_index < 4) ? zone_index : 0;
    const ResChest *ch = &g->res->economy.chest;
    const ResBanners *bn = &g->res->banners;
    int chance = (int)(chest_rand(g, x, y, 1) % 100u) + 1;   // 1..100

    if (chance < ch->chance_gold[zi]) {
        int points = (int)(chest_rand(g, x, y, 2) %
                           (unsigned)(ch->gold_max[zi] > 0 ? ch->gold_max[zi] : 1)) + 1;
        points += ch->gold_min[zi];
        int gold = points * 100;
        int leadership = gold / 50;
        if (GameHasPower(g, ARTIFACT_POWER_DOUBLE_LEADERSHIP)) leadership *= 2;
        // : caller runs the
        // prompt and calls GameAcceptChestGold / GameAcceptChestLeadership
        // based on the player's choice.
        if (out_pending) {
            out_pending->pending_gold = gold;
            out_pending->pending_leadership = leadership;
        }
        char gbuf[16], lbuf[16];
        snprintf(gbuf, sizeof gbuf, "%d", gold);
        snprintf(lbuf, sizeof lbuf, "%d", leadership);
        ResTemplateVar vars[] = {
            { "GOLD", gbuf }, { "LEADERSHIP", lbuf },
        };
        resources_format_template(out_body, out_sz, bn->chest_gold,
                                  vars, (int)(sizeof vars / sizeof vars[0]));
        return CHEST_OUTCOME_GOLD;
    }
    if (chance < ch->chance_commission[zi]) {
        int points = (int)(chest_rand(g, x, y, 3) %
                           (unsigned)(ch->commission_max[zi] > 0 ? ch->commission_max[zi] : 1)) + 1;
        points += ch->commission_min[zi];
        g->stats.commission_weekly += points;
        char pbuf[16];
        snprintf(pbuf, sizeof pbuf, "%d", points);
        ResTemplateVar vars[] = { { "POINTS", pbuf } };
        resources_format_template(out_body, out_sz, bn->chest_commission,
                                  vars, 1);
        return CHEST_OUTCOME_COMMISSION;
    }
    if (chance < ch->chance_spell_power[zi]) {
        int points = 1;
        g->stats.spell_power += points;
        char pbuf[16];
        snprintf(pbuf, sizeof pbuf, "%d", points);
        ResTemplateVar vars[] = { { "POINTS", pbuf } };
        resources_format_template(out_body, out_sz, bn->chest_spell_power,
                                  vars, 1);
        return CHEST_OUTCOME_SPELL_POWER;
    }
    if (chance < ch->chance_max_spells[zi]) {
        int points = ch->max_spells_base[zi];
        if (GameHasPower(g, ARTIFACT_POWER_DOUBLE_MAX_SPELLS)) points *= 2;
        g->stats.max_spells += points;
        char pbuf[16];
        snprintf(pbuf, sizeof pbuf, "%d", points);
        ResTemplateVar vars[] = { { "POINTS", pbuf } };
        resources_format_template(out_body, out_sz, bn->chest_max_spells,
                                  vars, 1);
        return CHEST_OUTCOME_MAX_SPELLS;
    }
    if (chance < ch->chance_new_spell[zi]) {
        int sc = spells_count();
        if (sc <= 0) {
            resources_format_template(out_body, out_sz, bn->chest_empty, NULL, 0);
            return CHEST_OUTCOME_EMPTY;
        }
        int spell_type = (int)(chest_rand(g, x, y, 4) % (unsigned)sc);
        int spell_num  = (int)(chest_rand(g, x, y, 5) % (unsigned)(zi + 1)) + 1;
        g->spells.counts[spell_type] += spell_num;
        const SpellDef *sp = spell_by_index(spell_type);
        char cbuf[16];
        snprintf(cbuf, sizeof cbuf, "%d", spell_num);
        ResTemplateVar vars[] = {
            { "COUNT", cbuf },
            { "SPELL", sp ? sp->name : "(unknown)" },
        };
        resources_format_template(out_body, out_sz, bn->chest_new_spell,
                                  vars, 2);
        return CHEST_OUTCOME_NEW_SPELL;
    }

    resources_format_template(out_body, out_sz, bn->chest_empty, NULL, 0);
    return CHEST_OUTCOME_EMPTY;
}

void GameAcceptChestGold(Game *g, int gold) {
    if (!g || gold <= 0) return;
    g->stats.gold += gold;
    {
        char tag[48];
        snprintf(tag, sizeof tag, "chest:gold:%d", gold);
        recorder_capture(tag);
    }
    // Note: no tune here -- the chest tune already played on chest
    // entry, matching take_chest's
    // "play happy music first, regardless of outcome" model.
}

void GameAcceptChestLeadership(Game *g, int leadership) {
    if (!g || leadership <= 0) return;
    g->stats.leadership_base += leadership;
    g->stats.leadership_current += leadership;
    {
        char tag[48];
        snprintf(tag, sizeof tag, "chest:lead:%d", leadership);
        recorder_capture(tag);
    }
    // See note above re: tune already played on chest entry.
}

// ---- Dwelling helpers ----------------------------------------------------

const TroopDef *GameDwellingTroopAt(const Game *g, const char *dwelling_kind,
                                    int x, int y) {
    if (!g || !dwelling_kind || !dwelling_kind[0]) return NULL;
    // Collect all troops whose `dwelling` field matches.
    int total = troops_count();
    if (total <= 0) return NULL;
    int *cand = malloc((size_t)total * sizeof *cand);
    if (!cand) return NULL;
    int n = 0;
    for (int i = 0; i < total; i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t) continue;
        if (strcmp(t->dwelling, dwelling_kind) == 0) cand[n++] = i;
    }
    if (n == 0) { free(cand); return NULL; }
    // Deterministic pick: (seed, x, y) -> index into cand[].
    unsigned h = (unsigned)g->seed;
    h ^= (unsigned)(x * 131u);
    h ^= (unsigned)(y * 97u);
    h = h * 1664525u + 1013904223u;
    int pick = (int)(h % (unsigned)n);
    const TroopDef *out = troop_by_index(cand[pick]);
    free(cand);
    return out;
}

//  verbatim:
//
//   free_leadership = game->leadership
//   for i in 0..4:
//     if player_troops[i] == troop_id:
//       free_leadership -= troops[troop_id].hit_points * player_numbers[i]
//       break
//   return free_leadership
//
// Only the slot already holding `troop_id` is subtracted; other troops
// are ignored. The recruit ceiling is `free_leadership / hp`.
int GameMaxRecruitable(const Game *g, const char *troop_id) {
    if (!g || !troop_id) return 0;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t || t->hit_points <= 0) return 0;
    // Recruit cap: (leadership_current - same_troop_leadership) / hp
    // Crucially, *other* troop slots do NOT reduce the cap. This is
    // why a fresh Knight on Easy can recruit 30 Militia + 8 Archers
    // + 10 Pikemen all at once even though the totals exceed leadership
    // 100 -- each troop type is checked independently.
    int same_troop_consumed = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0) {
            same_troop_consumed += g->army[i].count * t->hit_points;
        }
    }
    int free_leadership = g->stats.leadership_current - same_troop_consumed;
    if (free_leadership < 0) return 0;
    return free_leadership / t->hit_points;
}

// JSON-declared dwellings use plural kinds ("hills"), but troop catalog
// entries use singular ("hill"). Normalize so GameDwellingTroopAt's
// strcmp against troop->dwelling matches.
static const char *dwelling_kind_normalize(const char *kind) {
    if (!kind) return "";
    if (strcmp(kind, "hills") == 0) return "hill";
    return kind;
}

// enforce_dwelling: eagerly materialize a
// DwellingState row for one tile: pick troop by kind+seed and set
// count = max_population. Idempotent -- returns the existing row on
// re-touch without overwriting it.
static DwellingState *enforce_dwelling(Game *g, const char *zone, int x, int y,
                                       const char *dwelling_kind) {
    if (!g || !zone || !zone[0]) return NULL;
    // Find existing.
    for (int i = 0; i < g->dwelling_count; i++) {
        if (g->dwellings[i].x == x && g->dwellings[i].y == y &&
            strcmp(g->dwellings[i].zone, zone) == 0) {
            return &g->dwellings[i];
        }
    }
    // Create new.
    if (!GameReserveDwellings(g, g->dwelling_count + 1)) return NULL;
    DwellingState *d = &g->dwellings[g->dwelling_count++];
    memset(d, 0, sizeof(*d));
    copy_id(d->zone, sizeof(d->zone), zone);
    d->x = x; d->y = y;
    const TroopDef *t = GameDwellingTroopAt(g,
                                            dwelling_kind_normalize(dwelling_kind),
                                            x, y);
    if (t) {
        copy_id(d->troop_id, sizeof(d->troop_id), t->id);
        d->max_population = t->max_population;
        // Dwellings start at full population.
        d->count = t->max_population;
    }
    return d;
}

// Variant that pins the troop directly (populate_dwelling: troop
// is decided up-front, kind is derived from troops[id].dwells). Used by
// salt_continent so per-zone preferred-troop lists / dwelling_range work.
static DwellingState *enforce_dwelling_pinned(Game *g, const char *zone,
                                              int x, int y,
                                              const char *troop_id) {
    if (!g || !zone || !zone[0] || !troop_id || !troop_id[0]) return NULL;
    for (int i = 0; i < g->dwelling_count; i++) {
        if (g->dwellings[i].x == x && g->dwellings[i].y == y &&
            strcmp(g->dwellings[i].zone, zone) == 0) {
            return &g->dwellings[i];
        }
    }
    const TroopDef *t = troop_by_id(troop_id);
    if (t && !GameReserveDwellings(g, g->dwelling_count + 1)) return NULL;
    if (!t) return NULL;
    DwellingState *d = &g->dwellings[g->dwelling_count++];
    memset(d, 0, sizeof(*d));
    copy_id(d->zone, sizeof(d->zone), zone);
    d->x = x; d->y = y;
    copy_id(d->troop_id, sizeof(d->troop_id), t->id);
    d->max_population = t->max_population;
    d->count = t->max_population;
    return d;
}

// Public accessor -- kept for the visit-handler path (main.c). Eager
// init in GameInit means the row almost always exists already; this
// just looks it up.
DwellingState *GameTouchDwelling(Game *g, const char *zone, int x, int y,
                                 const char *dwelling_kind) {
    return enforce_dwelling(g, zone, x, y, dwelling_kind);
}

void GameTempDeath(Game *g, Map *map, Fog *fog, const Resources *res) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    g->stats.siege_weapons = 0;

    // The consolation stack (pack knob; fizzles to an empty army when the
    // configured troop id is absent from the catalog).
    const TroopDef *t = troop_by_id(res->tuning.temp_death_troop);
    if (t) {
        size_t k = 0;
        while (k + 1 < sizeof(g->army[0].id) && t->id[k]) {
            g->army[0].id[k] = t->id[k]; k++;
        }
        g->army[0].id[k] = '\0';
        g->army[0].count = res->tuning.temp_death_count;
    }

    // Forfeit the boat rental on defeat. Without this, GameSwitchZone
    // below would move the boat to the home spawn, magically materializing
    // it on land at the King's castle. Boat is rental property; losing
    // it matches the rest of temp_death's "forfeit everything" semantics
    // (army wiped, siege weapons revoked).
    g->boat.has_boat = false;
    g->boat.x = -1;
    g->boat.y = -1;
    g->boat.zone[0] = '\0';

    for (int zi = 0; zi < res->zone_count; zi++) {
        if (!res->zones[zi].is_home) continue;
        GameSwitchZone(g, map, fog, res->zones[zi].id);
        g->position.x = res->zones[zi].home_spawn_x;
        g->position.y = res->zones[zi].home_spawn_y;
        g->position.last_x = g->position.x;
        g->position.last_y = g->position.y;
        break;
    }
    g->character.mount = MOUNT_RIDE;
    g->travel_mode = TRAVEL_WALK;
}

bool GameSwitchZone(Game *g, Map *map, Fog *fog, const char *zone_id) {
    if (!g || !map || !fog || !zone_id) return false;
    const ResZone *z = resources_zone_by_id(g->res, zone_id);
    if (!z) return false;

    // Snapshot the outgoing zone's fog into per-continent storage so
    // we can restore it later ([continent][y][x]).
    {
        const ResZone *old_z = (g->position.zone[0])
            ? resources_zone_by_id(g->res, g->position.zone) : NULL;
        if (old_z) {
            int old_zi = (int)(old_z - g->res->zones);
            if (old_zi >= 0 && old_zi < g->world.zone_count) {
                FogCopy(&g->world.continent_fog[old_zi], fog);
            }
        }
    }

    // The zone sailed from picks the landing ("arrivals"), else the spawn.
    char from[sizeof g->position.zone];
    copy_id(from, sizeof from, g->position.zone);

    if (!MapLoadZoneWithPlacements(map, g->res, zone_id, g)) return false;
    // Re-apply consumed tiles so picked-up artifacts / chests stay gone.
    GameApplyTileMutations(g, map, zone_id);
    // Move hero to the arrival point.
    copy_id(g->position.zone, sizeof(g->position.zone), zone_id);
    g->position.x = map->hero_spawn_x;
    g->position.y = map->hero_spawn_y;
    if (strcmp(from, zone_id) != 0)
        resources_zone_arrival(z, from, &g->position.x, &g->position.y);
    g->position.last_x = g->position.x;
    g->position.last_y = g->position.y;

    // Match the hero's travel mode to the spawn terrain. Cross-continent
    // arrivals can land on water (open ocean entry) or land (the home
    // continent's hero_spawn is on grass). Without this, sailing back
    // to Continentia would leave the hero in a boat on land.
    {
        const Tile *spawn = MapGetTile(map, g->position.x, g->position.y);
        bool on_water = spawn && spawn->terrain == TERRAIN_WATER &&
                        !spawn->is_bridge;
        if (on_water) {
            g->travel_mode = TRAVEL_BOAT;
            g->boat.has_boat = true;
            g->boat.x = g->position.x;
            g->boat.y = g->position.y;
            memset(g->boat.zone, 0, sizeof g->boat.zone);   // zero the tail before copy (canonical image)
            copy_id(g->boat.zone, sizeof(g->boat.zone), zone_id);
        } else {
            g->travel_mode = TRAVEL_WALK;
            // Keep the boat; place it at the spawn so the player can
            // re-board to sail away. The renderer hides the boat tile
            // when the hero is standing on it.
            if (g->boat.has_boat) {
                g->boat.x = g->position.x;
                g->boat.y = g->position.y;
                memset(g->boat.zone, 0, sizeof g->boat.zone);   // zero the tail before copy (canonical image)
                copy_id(g->boat.zone, sizeof(g->boat.zone), zone_id);
            }
        }
    }

    // Restore fog for the incoming zone. Continents revisited by boat or
    // teleport keep whatever the player had previously revealed; first
    // visits start blank and reveal around spawn.
    int zi = (int)(z - g->res->zones);
    if (zi >= 0 && zi < g->world.zone_count && g->world.zones_discovered[zi]) {
        FogCopy(fog, &g->world.continent_fog[zi]);
    } else {
        FogInit(fog);
    }
    FogRevealFor(g->res, fog, map, g->position.x, g->position.y);
    // Mark discovered.
    if (zi >= 0 && zi < g->world.zone_count) g->world.zones_discovered[zi] = true;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "zone:%s", zone_id);
        recorder_capture(tag);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Gate spells (Town Gate / Castle Gate): destination lists + teleport.
//
// The engine owns visited-eligibility and the boat semantics so the shell UI
// and the autoplay responder share one source of truth.
// ---------------------------------------------------------------------------

// Case-insensitive name compare (alphabetical), then zone for a stable tie.
static int gate_name_icmp(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
static int gate_dest_cmp(const void *a, const void *b) {
    const GateDestination *da = a, *db = b;
    int c = gate_name_icmp(da->name, db->name);
    return c ? c : strcmp(da->zone, db->zone);
}

int GameGateDestinations(const Game *g, GateDestKind kind,
                         GateDestination *out, int cap) {
    if (!g || !out || cap <= 0) return 0;
    int n = 0;
    if (kind == GATE_DEST_CASTLE) {
        int cn = g->castle_count;
        for (int i = 0; i < cn && n < cap; i++) {
            if (!g->castles[i].visited) continue;
            const ResCastle *rc = &g->res->castles[i];
            if (resources_castle_is_home(rc)) continue;
            if (!rc->zone[0]) continue;
            snprintf(out[n].name, sizeof out[n].name, "%s",
                     rc->name[0] ? rc->name : rc->id);
            snprintf(out[n].zone, sizeof out[n].zone, "%s", rc->zone);
            out[n].x = rc->gate_x >= 0 ? rc->gate_x : rc->x;
            out[n].y = rc->gate_y >= 0 ? rc->gate_y : rc->y;
            n++;
        }
    } else {
        int tn = g->town_count;
        for (int i = 0; i < tn && n < cap; i++) {
            if (!g->towns[i].visited) continue;
            const ResTown *rt = &g->res->towns[i];
            if (!rt->zone[0]) continue;
            snprintf(out[n].name, sizeof out[n].name, "%s",
                     rt->name[0] ? rt->name : rt->id);
            snprintf(out[n].zone, sizeof out[n].zone, "%s", rt->zone);
            out[n].x = rt->gate_x >= 0 ? rt->gate_x : rt->x;
            out[n].y = rt->gate_y >= 0 ? rt->gate_y : rt->y;
            n++;
        }
    }
    if (n > 1) qsort(out, (size_t)n, sizeof out[0], gate_dest_cmp);
    return n;
}

bool GameGateTeleport(Game *g, Map *map, Fog *fog,
                      const GateDestination *dest, const char *spell_id) {
    if (!g || !map || !fog || !dest || !dest->zone[0]) return false;

    // Leave any current boat behind at the cast tile (origin zone). This must
    // happen BEFORE the zone switch, which would otherwise carry the boat to
    // the destination spawn. We snapshot the "left behind" boat and restore it
    // after the switch so the boat's zone stays the origin zone.
    BoatState left_behind;
    memset(&left_behind, 0, sizeof left_behind);   // zero padding + zone tail: an uninitialized local
                                                   // copies stack garbage into g->boat below, which the
                                                   // serialized/fingerprinted byte image would then carry
                                                   // (a determinism regression across otherwise-equal runs).
    bool have_left = false;
    if (g->travel_mode == TRAVEL_BOAT) {
        left_behind.has_boat = true;
        left_behind.x = g->position.x;
        left_behind.y = g->position.y;
        copy_id(left_behind.zone, sizeof left_behind.zone, g->position.zone);
        have_left = true;
    } else if (g->boat.has_boat) {
        // A docked boat (hero on foot) also stays where it is.
        left_behind = g->boat;
        have_left = true;
    }

    if (!GameSwitchZone(g, map, fog, dest->zone)) return false;

    // Always arrive on foot at the gate landing tile.
    if (dest->x >= 0 && dest->y >= 0) {
        g->position.x = dest->x;
        g->position.y = dest->y;
        g->position.last_x = dest->x;
        g->position.last_y = dest->y;
    }
    g->travel_mode = TRAVEL_WALK;

    // Restore the boat to where it was left (origin zone), overriding the
    // re-attach GameSwitchZone performed at the destination spawn.
    if (have_left) {
        g->boat = left_behind;
    }

    int sidx = spell_index_by_id(spell_id);
    if (sidx >= 0 && g->spells.counts[sidx] > 0) g->spells.counts[sidx]--;
    return true;
}

void GameGrowDwellings(Game *g) {
    if (!g) return;
    for (int i = 0; i < g->dwelling_count; i++) {
        DwellingState *d = &g->dwellings[i];
        if (!d->troop_id[0]) continue;
        const TroopDef *t = troop_by_id(d->troop_id);
        if (!t) continue;
        d->count += t->growth_per_week;
        if (d->count > d->max_population) d->count = d->max_population;
    }
}

int GamePickAstrologyCreature(const Game *g, int week_id) {
    if (!g) return 0;
    int tc = troops_count();
    if (tc < 1) tc = 1;
    // Every 4th week is peasants `).
    if ((week_id & 3) == 0) return 0;
    unsigned h = (unsigned)g->seed ^ (unsigned)week_id;
    h = h * 1664525u + 1013904223u;
    int idx = 1 + (int)(h % (unsigned)(tc > 1 ? tc - 1 : 1));
    if (idx >= tc) idx = 0;
    return idx;
}

const char *GameApplyAstrology(Game *g, int troop_idx) {
    if (!g) return "";
    const TroopDef *at = troop_by_index(troop_idx);
    if (!at) return "";

    // Repopulate dwellings whose troop == astrology creature (
    // play.c:1024-1026). Non-matching dwellings carry their current
    // population over unchanged -- population only refills on the troop's
    // astrology week.
    for (int i = 0; i < g->dwelling_count; i++) {
        DwellingState *d = &g->dwellings[i];
        if (!d->troop_id[0]) continue;
        if (strcmp(d->troop_id, at->id) == 0) {
            d->count = d->max_population;
        }
    }

    // Week of the Peasants (index 0) converts absorb-ability troops in
    // the player's army to peasants (ghosts -> peasants trick;
    // play.c:1007-1011).
    if (troop_idx == 0) {
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!g->army[s].id[0] || g->army[s].count == 0) continue;
            const TroopDef *t = troop_by_id(g->army[s].id);
            if (!t) continue;
            if (t->abilities & TROOP_ABIL_ABSORB) {
                copy_id(g->army[s].id, sizeof(g->army[s].id), at->id);
            }
        }
    }

    return at->id;
}

// Recruit-location guard (mirrors the in_town town-core guards): a troop may only
// be bought where a player can buy it. Home-pool troops (catalog dwelling=="castle")
// require the hero on the home-castle gate (position.home_castle set). Any other
// troop is a dwelling troop: it requires the hero standing on a dwelling tile whose
// offered troop matches AND that still has population. Returns the legal cap (>=0)
// for the troop, or -1 when the hero is at no legal source for it.
static int recruit_location_cap(const Game *g, const char *troop_id,
                                const TroopDef *t) {
    // A dwelling the hero is STANDING ON that offers this troop sells it,
    // whatever the troop's catalog class: the salt roll can seed castle-class
    // troops at dwellings (salt_pick_dwelling_troop draws from the full
    // range), and the old castle-first early-return refused the very shop
    // the world spawned -- the prompt opened, the buy silently failed.
    if (g->position.dwelling_troop[0] &&
        strcmp(g->position.dwelling_troop, troop_id) == 0) {
        for (int i = 0; i < g->dwelling_count; i++) {
            const DwellingState *d = &g->dwellings[i];
            if (d->x == g->position.dwelling_x && d->y == g->position.dwelling_y &&
                strcmp(d->zone, g->position.zone) == 0 &&
                strcmp(d->troop_id, troop_id) == 0) {
                return d->count;
            }
        }
        return -1;
    }
    if (strcmp(t->dwelling, "castle") == 0) {
        // Home pool: unlimited (the castle never runs dry); gated only by being
        // at the home castle.
        return g->position.home_castle[0] ? (1 << 28) : -1;
    }
    // Dwelling troop: the hero must be on the dwelling that offers this troop.
    if (!g->position.dwelling_troop[0] ||
        strcmp(g->position.dwelling_troop, troop_id) != 0) {
        return -1;
    }
    for (int i = 0; i < g->dwelling_count; i++) {
        const DwellingState *d = &g->dwellings[i];
        if (d->x == g->position.dwelling_x && d->y == g->position.dwelling_y &&
            strcmp(d->zone, g->position.zone) == 0 &&
            strcmp(d->troop_id, troop_id) == 0) {
            return d->count;
        }
    }
    return -1;
}

int GameRecruitLocationCap(const Game *g, const char *troop_id) {
    if (!g || !troop_id) return -1;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t) return -1;
    return recruit_location_cap(g, troop_id, t);
}

int GameBuyTroop(Game *g, const char *troop_id, int count) {
    if (!g || !troop_id || count <= 0) return 2;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t) return 2;
    // Location guard: refuse a recruit the hero cannot legally make from here.
    // Same contract as the town transaction cores (GameBuySpell etc.) -- the engine,
    // not the caller, is the legality boundary, so autoplay is held to it too.
    int loc_cap = recruit_location_cap(g, troop_id, t);
    if (loc_cap < 0 || count > loc_cap) {
        return 4;   // illegal location / over the dwelling population
    }
    int total_cost = t->recruit_cost * count;
    if (g->stats.gold < total_cost) return 1;
    if (count > GameMaxRecruitable(g, troop_id)) return 3;

    // Find matching stack or an empty slot.
    int slot = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0]) { slot = i; break; }
        }
    }
    if (slot < 0) return 2;
    copy_id(g->army[slot].id, sizeof(g->army[slot].id), troop_id);
    g->army[slot].count += count;
    g->stats.gold -= total_cost;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "buy:%s:%d", troop_id, count);
        recorder_capture(tag);
    }
    return 0;
}

int GameAddTroop(Game *g, const char *troop_id, int count) {
    if (!g || !troop_id || count <= 0) return 1;
    int slot = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0 && g->army[i].count > 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0] || g->army[i].count == 0) { slot = i; break; }
        }
    }
    if (slot < 0) return 1;
    copy_id(g->army[slot].id, sizeof(g->army[slot].id), troop_id);
    g->army[slot].count += count;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "add:%s:%d", troop_id, count);
        recorder_capture(tag);
    }
    return 0;
}

void GameCompactArmy(Game *g) {
    if (!g) return;
    int dst = 0;
    for (int src = 0; src < GAME_ARMY_SLOTS; src++) {
        if (!g->army[src].id[0] || g->army[src].count == 0) continue;
        if (dst != src) {
            g->army[dst] = g->army[src];
        }
        dst++;
    }
    for (int i = dst; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
}

// Garrison is only legal at an owned castle the hero is standing at -- the
// own-castle screen (or the moment a siege win takes the castle). The engine, not
// the caller, enforces it: position.own_castle must name THIS castle, and the
// castle must be player-owned. Same contract as the in_town town cores.
static bool garrison_location_ok(const Game *g, const char *castle_id) {
    if (!castle_id || !castle_id[0]) return false;
    if (strcmp(g->position.own_castle, castle_id) != 0) return false;
    const CastleRecord *cr = GameFindCastleConst(g, castle_id);
    return cr && cr->owner_kind == CASTLE_OWNER_PLAYER;
}

//  garrison_troop, generalised to part of a stack. Moving the whole stack is
// exactly the original: refused when it is the hero's last, and the army is
// compacted. Moving part of it always leaves the stack in the army, so it is
// never the last-army refusal.
int GameGarrisonTroopCount(Game *g, const char *castle_id, int slot, int count) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    if (!garrison_location_ok(g, castle_id)) return 1;
    const ArmyStack *src = &g->army[slot];
    if (!src->id[0] || src->count == 0) return 1;
    if (count <= 0 || count > src->count) return 1;
    bool whole = (count == src->count);

    // Refuse if this would leave the player with no army (
    // game->player_troops[1]; we count non-empty slots other than `slot`).
    if (whole) {
        int remaining = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (i == slot) continue;
            if (g->army[i].id[0] && g->army[i].count > 0) { remaining++; break; }
        }
        if (remaining == 0) return 2;
    }

    CastleRecord *cr = GameFindCastle(g, castle_id);
    if (!cr) return 1;

    // Find matching troop in garrison, or first empty slot.
    int dst = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(cr->garrison[i].id, src->id) == 0 && cr->garrison[i].count > 0) {
            dst = i;
            break;
        }
        if (!cr->garrison[i].id[0] || cr->garrison[i].count == 0) {
            if (dst < 0) dst = i;   // remember first empty
        }
    }
    if (dst < 0) return 1;

    copy_id(cr->garrison[dst].id, sizeof(cr->garrison[dst].id), src->id);
    cr->garrison[dst].count += count;

    if (!whole) {
        g->army[slot].count -= count;
        return 0;
    }
    // Remove from player. dismiss_troop zeroes the stack and
    // leaves a gap; openbounty compacts so the filled slots stay
    // contiguous (matches the visible UI expectation that A/B/C/...
    // are dense).
    g->army[slot].id[0] = '\0';
    g->army[slot].count = 0;
    GameCompactArmy(g);
    return 0;
}

int GameGarrisonTroop(Game *g, const char *castle_id, int slot) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    return GameGarrisonTroopCount(g, castle_id, slot, g->army[slot].count);
}

//  ungarrison_troop, generalised to part of a stack. The whole stack is the
// original move (the garrison is compacted); part of it leaves the rest there.
int GameUngarrisonTroopCount(Game *g, const char *castle_id, int slot, int count) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    if (!garrison_location_ok(g, castle_id)) return 1;
    CastleRecord *cr = GameFindCastle(g, castle_id);
    if (!cr) return 1;
    const Unit *src = &cr->garrison[slot];
    if (!src->id[0] || src->count == 0) return 1;
    if (count <= 0 || count > src->count) return 1;
    bool whole = (count == src->count);

    // Find matching army stack or empty slot.
    int dst = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, src->id) == 0 && g->army[i].count > 0) {
            dst = i;
            break;
        }
        if (!g->army[i].id[0] || g->army[i].count == 0) {
            if (dst < 0) dst = i;
        }
    }
    if (dst < 0) return 1;

    copy_id(g->army[dst].id, sizeof(g->army[dst].id), src->id);
    g->army[dst].count += count;

    if (!whole) {
        cr->garrison[slot].count -= count;
        return 0;
    }
    // Compact the garrison .
    for (int i = slot; i < GAME_ARMY_SLOTS - 1; i++) {
        copy_id(cr->garrison[i].id, sizeof(cr->garrison[i].id),
                cr->garrison[i + 1].id);
        cr->garrison[i].count = cr->garrison[i + 1].count;
    }
    cr->garrison[GAME_ARMY_SLOTS - 1].id[0] = '\0';
    cr->garrison[GAME_ARMY_SLOTS - 1].count = 0;
    return 0;
}

int GameUngarrisonTroop(Game *g, const char *castle_id, int slot) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    const CastleRecord *cr = GameFindCastleConst(g, castle_id);
    if (!cr) return 1;
    return GameUngarrisonTroopCount(g, castle_id, slot, cr->garrison[slot].count);
}

bool GameTroopFlies(const TroopDef *t) {
    return t && (t->abilities & TROOP_ABIL_FLY) && t->skill_level >= 2;
}

bool GamePlayerCanFly(const Game *g) {
    if (!g) return false;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count == 0) continue;
        if (!GameTroopFlies(troop_by_id(g->army[i].id))) return false;
    }
    return true;
}

// Mount transitions -- the ONE copy of the legality the shell's FLY/LAND keys
// apply (shell_actions.c) and autoplay's fly-leg rides on.
bool GameMountFly(Game *g) {
    if (!g || g->character.mount != MOUNT_RIDE || !GamePlayerCanFly(g)) return false;
    g->character.mount = MOUNT_FLY;
    return true;
}

bool GameCanLandAt(const Game *g, const Map *map, int x, int y) {
    (void)g;
    const Tile *t = map ? MapGetTile(map, x, y) : NULL;
    return t && t->terrain == TERRAIN_GRASS &&
           t->interactive == INTERACT_NONE && !t->blocks_foot;
}

bool GameLandHere(Game *g, const Map *map) {
    if (!g || g->character.mount != MOUNT_FLY) return false;
    if (!GameCanLandAt(g, map, g->position.x, g->position.y)) return false;
    g->character.mount = MOUNT_RIDE;
    return true;
}

const char *GameNumberName(const Game *g, int count) {
    if (!g || count < 1) return "";
    const Resources *r = g->res;
    if (!r || r->number_name_count <= 0) return "";
    // Thresholds listed high-to-low; pick first label whose min <= count.
    for (int i = 0; i < r->number_name_count; i++) {
        if (count >= r->number_name_thresholds[i])
            return r->number_name_labels[i];
    }
    return r->number_name_labels[r->number_name_count - 1];
}

CastleRecord *GameFindCastle(Game *g, const char *castle_id) {
    if (!g || !castle_id || !castle_id[0]) return NULL;
    for (int i = 0; i < g->castle_count; i++) {
        if (strcmp(g->castles[i].id, castle_id) == 0) return &g->castles[i];
    }
    return NULL;
}

const CastleRecord *GameFindCastleConst(const Game *g, const char *castle_id) {
    return GameFindCastle((Game *)g, castle_id);
}

FoeState *GameFindFoe(Game *g, const char *placement_id) {
    if (!g || !placement_id || !placement_id[0]) return NULL;
    // Placement IDs (wandering_army_NNN) are NOT unique across zones --
    // the same numbering restarts in every zone's `wandering_armies[]`.
    // Scope the match to the hero's current zone so defeating a foe on
    // one continent doesn't silently mark a same-named foe on another
    // continent as dead, leaving the one you actually fought alive and
    // pursuing forever.
    const char *zone = g->position.zone;
    for (int i = 0; i < g->foe_count; i++) {
        if (strcmp(g->foes[i].placement_id, placement_id) != 0) continue;
        if (zone[0] && strcmp(g->foes[i].zone, zone) != 0) continue;
        return &g->foes[i];
    }
    return NULL;
}

const FoeState *GameFindFoeConst(const Game *g, const char *placement_id) {
    return GameFindFoe((Game *)g, placement_id);
}

bool GameFriendlyFoeOriginAt(const Game *g, const char *zone, int x, int y) {
    if (!g || !zone) return false;
    // Match regardless of `alive`: a friendly foe's origin chest slot is spent
    // the moment the game is salted -- it stays suppressed after the foe is
    // recruited or killed so the slot never reverts to an openable chest.
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->friendly) continue;
        if (f->origin_x != x || f->origin_y != y) continue;
        if (strcmp(f->zone, zone) != 0) continue;
        return true;
    }
    return false;
}

// Port of foe_closest_offset (OpenKB's play.c:1738).
// Walk each on-screen foe within 2 tiles of the hero's last
// position one step toward it. The step is picked by evaluating all 9
// cells of the foe's 3x3 neighborhood (including stay-put) and choosing
// the cell with the minimum Euclidean distance to the target. Non-center
// cells that aren't walkable get bumped to a max-distance sentinel so
// they're never picked. The center cell always keeps its real distance,
// which lets foes stay put when every move would be worse than waiting.
//
// Target is hero's `last_*` (the tile the hero just vacated), not current
// position. Combat on
// collision is handled by the step-on-foe interact path, not here; this
// function never steps onto the hero's current tile.

// Integer Euclidean distance: isqrt32(dx^2 + dy^2).
// Scaled x1000 internally so single-step differences (e.g. cardinal vs
// diagonal) don't round-collapse to equal integers and starve the picker
// of tie-breaking resolution. The absolute value doesn't matter -- only
// ordering does.
static unsigned foe_dist_sq(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return (unsigned)(dx * dx + dy * dy);
}

// Keep foes off the eight tiles surrounding a castle gate. This is a house
// rule, NOT a restoration: the original's 0x00 test does not prevent it,
// because the approach tile below a gate is grass in both. Without it a foe
// parks on the doorstep of the hero's own castle. See issue #22.
static bool adjacent_to_castle_gate(const Map *map, int x, int y) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            const Tile *n = MapGetTile(map, x + dx, y + dy);
            if (n && n->interactive == INTERACT_CASTLE_GATE) return true;
        }
    }
    return false;
}

static bool foe_can_stand(const Map *map, int x, int y) {
    if (!MapInBounds(map, x, y)) return false;
    const Tile *t = MapGetTile(map, x, y);
    if (!t) return false;
    if (t->blocks_foot) return false;
    // The original accepts a candidate tile only when its map byte is 0x00,
    // i.e. grass (OPENKB-SPEC.md:6234, foe_closest_offset at play.c:1738).
    // Deliberately NOT TerrainWalkable(), which also admits desert: desert is
    // a non-zero byte and impassable to foes, which is what makes it a refuge
    // for the hero. Hero movement is unaffected; adventure_walkable_on_foot
    // still crosses desert at 40 movement points per tile.
    if (t->terrain != TERRAIN_GRASS) return false;
    // Bridges have no terrain of their own: TerrainFromArt falls through to
    // GRASS for art it does not recognise, so the test above lets them past.
    // A bridge is a non-zero byte, so the original rejects it; without this a
    // foe walks over water and follows the hero across a barrier.
    if (t->is_bridge) return false;
    // Treat any stamped tile byte as an obstacle -- the foe can't
    // sit on a chest, castle gate, another foe, etc. Our INTERACT_NONE
    // check captures the same rule.
    if (t->interactive != INTERACT_NONE) return false;
    if (adjacent_to_castle_gate(map, x, y)) return false;
    return true;
}

// Authoritative occupancy: does any LIVE foe other than `except_idx`, in `zone`,
// sit at (x,y)? Stamp-independent -- the map's INTERACT_FOE overlay can momentarily
// disagree with real foe positions, so the anti-stacking gate consults g->foes[]
// directly rather than trusting the tile byte.
static bool foe_occupies(const Game *g, const char *zone, int x, int y,
                         int except_idx) {
    for (int j = 0; j < g->foe_count; j++) {
        if (j == except_idx) continue;
        const FoeState *o = &g->foes[j];
        if (!o->alive) continue;
        if (o->x != x || o->y != y) continue;
        if (strcmp(o->zone, zone) != 0) continue;
        return true;
    }
    return false;
}

int GameFoesFollow(Game *g, Map *map) {
    if (!g || !map) return -1;
    int tx = g->position.last_x;
    int ty = g->position.last_y;
    int collided = -1;
    // STAMP RE-SYNC: every live in-zone foe NOT on the hero's current tile must
    // carry its INTERACT_FOE stamp before we read the map for standability below.
    // A foe that stepped onto the hero (collision path) is intentionally left
    // unstamped while the hero shares its tile; once the hero moves away nothing
    // else re-stamps it, leaving an unstamped "phantom" the occupancy proxy can't
    // see (another foe stacks on it) and the stamp-based combat trigger can't
    // engage. Re-establish the invariant here so foe_can_stand and the step-onto
    // combat trigger are both correct. (Skips the hero's tile: the collided foe
    // stays unstamped, preserving the `collided` contract below.)
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->alive) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        if (f->x == g->position.x && f->y == g->position.y) continue;
        MapStampFoe(map, f->x, f->y, f->placement_id);
    }
    // Iterate ALL foes -- friendly and hostile -- through foes_follow.
    // The friendly/hostile distinction only matters at attack time.
    for (int i = 0; i < g->foe_count; i++) {
        FoeState *f = &g->foes[i];
        if (!f->alive) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        if (f->is_static) continue;   // fixed guardian: never moves
        // Range gate (OpenKB's play.c:823-829): foe must be within
        // GAME_FOE_FOLLOW_RANGE tiles on each axis of the hero's previous
        // position.
        int diff_x = f->x - tx; if (diff_x < 0) diff_x = -diff_x;
        int diff_y = f->y - ty; if (diff_y < 0) diff_y = -diff_y;
        if (diff_x > GAME_FOE_FOLLOW_RANGE || diff_y > GAME_FOE_FOLLOW_RANGE)
            continue;

        // Evaluate all 9 neighborhood cells (foe_closest_offset,
        // play.c:1738). The center is always eligible (foe can stand still).
        // Non-center obstacles get a sentinel distance, so any real cell
        // beats them. The hero's current tile is NOT excluded --
        // allows a foe to step onto the hero, which becomes the combat
        // trigger via the "stepped on a foe" check at game.c:6552.
        const unsigned SENTINEL = 0xFFFFFFFFu;
        unsigned best_dist = SENTINEL;
        int      best_x = f->x;
        int      best_y = f->y;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = f->x + dx;
                int ny = f->y + dy;
                if (!MapInBounds(map, nx, ny)) continue;
                bool is_center = (dx == 0 && dy == 0);
                bool is_hero_tile = (nx == g->position.x &&
                                     ny == g->position.y);
                // A flying hero is untouchable (issue #12): the foe must not
                // step onto the hero's tile (no combat), nor chase the hero
                // across the water / trees / castle no-go tiles it is flying
                // over. Skip that tile entirely so only real, foe-standable
                // ground is ever considered. Gated to real play: the autoplay
                // oracle (oracle_mode) keeps the legacy behavior its search is
                // tuned against -- wandering-foe contact gates no objective, so
                // the winnability verdict is unchanged. The visible replay runs
                // with oracle_mode set, so it stays consistent with the resolve.
                if (is_hero_tile && g->character.mount == MOUNT_FLY &&
                    !g->oracle_mode)
                    continue;
                // Used ONLY for the anti-stacking exemption below, not for
                // walkability: a foe standing on the hero (the collision that
                // triggers combat) is not a stack, so a second foe is still
                // allowed to target that tile. Drops away in the boat, where
                // the hero sits on water no land foe can occupy anyway.
                bool hero_reachable = (is_hero_tile &&
                                       g->travel_mode != TRAVEL_BOAT);
                // `if (i || j)` gate: only non-center cells get the obstacle
                // penalty, matching the original's `if (i != 0 or j != 0)`.
                //
                // The hero's tile gets NO exemption here. foe_closest_offset
                // (OPENKB-SPEC.md:6234) tests the map byte of all eight
                // non-center cells without caring where the player is, so a
                // hero standing on any non-zero tile simply cannot be reached.
                // That is what makes desert a refuge rather than merely
                // impassable: foes neither cross it nor attack into it. The
                // same now holds for bridges, towns and the other interactive
                // tiles, and for the castle-gate approach.
                //
                // This previously exempted the hero's tile so the foe could
                // step on and trigger combat, which let a foe on adjacent
                // grass reach a hero standing anywhere at all.
                if (!is_center && !foe_can_stand(map, nx, ny))
                    continue;
                // Anti-stacking, stamp-independent: never target a tile another
                // live foe already holds (two foes may never share a spot). The
                // hero's tile is exempt -- a foe stepping onto the hero is the
                // combat trigger, not a stack.
                if (!is_center && !hero_reachable &&
                    foe_occupies(g, f->zone, nx, ny, i))
                    continue;
                unsigned d = foe_dist_sq(nx, ny, tx, ty);
                if (d < best_dist) {
                    best_dist = d;
                    best_x = nx;
                    best_y = ny;
                }
            }
        }
        if (best_x == f->x && best_y == f->y) continue;

        // Move: clear the old tile -- foe stamp only (never a pickup the
        // hero-tile exception let the foe stand on).
        MapClearFoeStamp(map, f->x, f->y);
        f->x = best_x;
        f->y = best_y;

        // If the foe stepped onto the hero, surface that to the caller and
        // do NOT stamp the foe tile (the hero is on it). Caller will fire
        // the attack/recruit flow against this foe.
        if (best_x == g->position.x && best_y == g->position.y) {
            collided = i;
            continue;
        }

        // Otherwise stamp the new tile with the foe.
        Tile *dst = &MAP_TILE(map, best_x, best_y);
        dst->interactive = INTERACT_FOE;
        TileSetId(map, dst, f->placement_id);
        TileSetArt(map, dst, map->army_art[0] ? map->army_art : "wandering_army");
    }
    return collided;
}
