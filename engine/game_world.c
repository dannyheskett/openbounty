// engine/game_world.c -- a new game: GameInit, and the seeded salting of
// every zone (spells, foes, objects, villains, the scepter, castle garrisons).

#include "game.h"
#include "game_internal.h"
#include "map.h"
#include "adventure.h"
#include "ui_host.h"
#include "fatal.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    // The scepter's zone is drawn from every zone the pack declares (#77):
    // a four-zone pack draws 0..3, and a pack with fewer or more zones buries
    // in one of its own.
    int zones = g->res->zone_count > 0 ? g->res->zone_count : 1;
    int scepter_continent = game_rng_next(0, zones - 1);
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
    game_copy_id(g->character.cls.id, sizeof(g->character.cls.id), cls->id);
    g->character.cls.rank_index = 0;
    game_copy_id(g->character.cls.rank_id, sizeof(g->character.cls.rank_id), cls->ranks[0].id);
    game_copy_id(g->character.cls.rank_title, sizeof(g->character.cls.rank_title), cls->ranks[0].name);

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
    g->stats.last_renewed_spell = -1;
    g->stats.last_week_on_hand = g->stats.last_week_army = g->stats.last_week_boat = 0;

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
            game_copy_id(g->position.zone, sizeof(g->position.zone), z->id);
            g->position.x = z->home_spawn_x;
            g->position.y = z->home_spawn_y;
            home_zone_index = zi;
            break;
        }
        if (!g->position.zone[0]) {
            game_copy_id(g->position.zone, sizeof(g->position.zone),
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
        // continent_found[HOME_CONTINENT] = 1
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
        if (v) game_copy_id(g->contract.cycle[i],
                       sizeof(g->contract.cycle[i]), v->id);
        else   g->contract.cycle[i][0] = '\0';
    }

    // Step 8 (play.c:426-433): Starting army (2 slots + empty rest).
    for (i = 0; i < cls->starting_troop_count && i < GAME_ARMY_SLOTS; i++) {
        const char *troop = cls->starting_troops[i];
        int count = cls->starting_counts[i];
        if (!troop[0] || count <= 0) continue;
        game_copy_id(g->army[i].id, sizeof(g->army[i].id), troop);
        g->army[i].count = count;
    }

    // Step 9 (play.c:435-441): Default player options.
    for (int oi = 0; oi < 7; oi++) {
        g->stats.options[oi] = g->res ? g->res->world.default_options[oi] : 1;
    }

    // Step 10 (play.c:444): Randomize spells sold in towns.
    salt_spells(g);

    // Remove magic alcove(s) if the starting class already knows magic.
    // The alcove is an overlay at the tile declared
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
            game_copy_id(cr->id, sizeof(cr->id), rc->id);
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

    // Assign villains to castles.
    salt_villains(g);

    // Repopulate every remaining monster-owned castle with a troop stack
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
                if (rd->troop[0])   // a pack-pinned breed (the elephant park)
                    game_enforce_dwelling_pinned(g, z->id, rd->x, rd->y, rd->troop);
                else
                    game_enforce_dwelling(g, z->id, rd->x, rd->y, rd->kind);
            }
        }
    }
    for (int pi = 0; pi < g->placement_count; pi++) {
        const SaltedPlacement *p = &g->placements[pi];
        const char *kind = DwellingCatalogKind((Interact)p->kind);
        if (!kind) continue;
        game_enforce_dwelling(g, p->zone, p->x, p->y, kind);
    }

    // Clear fog around starting location
    clear_fog(g);
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
        game_copy_id(tr->id, sizeof(tr->id), rt->id);
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
        game_copy_id(g->towns[i].spell_for_sale,
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
            if (sp) game_copy_id(g->towns[t].spell_for_sale,
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
        if (sp) game_copy_id(g->towns[i].spell_for_sale,
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

// A troop's dwelling kind to its tile; any kind the tiles don't know (the
// catalog's "castle", say) is a plains dwelling.
static Interact dwelling_kind_to_interact(const char *kind) {
    Interact i = DwellingInteractFromKind(kind);
    return i != INTERACT_NONE ? i : INTERACT_DWELLING_PLAINS;
}

// pick the troop first, derive
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
// difficulty (= continent) governs the
// chance distribution; the dwelling kind is rolled fresh each call and
// indexes the troop pool independently. So Saharia (cont 3) skews to
// the rarest slot regardless of kind, but kind itself is uniform.
// The calm start (REQ-283, #69): true when a hostile foe at (x, y) lies within
// spawn.calm_radius of its zone's hero_spawn. Radius 0 (the King's Bounty
// pack) never calms, so the original roll runs unchanged.
static bool foe_is_calm(const Game *g, int continent, int x, int y) {
    const ResSpawn *sp = &g->res->spawn;
    if (sp->calm_radius <= 0) return false;
    if (continent < 0 || continent >= g->res->zone_count) return false;
    const ResZone *z = &g->res->zones[continent];
    int dx = x - z->hero_spawn_x, dy = y - z->hero_spawn_y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx > dy ? dx : dy) <= sp->calm_radius;
}

static void roll_hostile_garrison(const Game *g, int continent, int x, int y, Unit *out) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        out[i].id[0] = '\0';
        out[i].count = 0;
    }
    if (!g || !g->res) return;
    int continent_tier = continent & 3;
    const bool calm = foe_is_calm(g, continent, x, y);
    const ResSpawn *sp = &g->res->spawn;
    int max_stacks = calm ? (sp->calm_max_stacks > 0 ? sp->calm_max_stacks : 1) : 3;
    int stacks = 1 + game_rng_next(0, max_stacks - 1);   // 1..3 stacks; calm: 1..calm_max_stacks
    if (stacks > GAME_ARMY_SLOTS) stacks = GAME_ARMY_SLOTS;
    for (int s = 0; s < stacks; s++) {
        int kind = game_rng_next(0, 3);     // dwelling = rand(0,3)
        int chance = game_rng_next(1, 100);
        const char *tid;
        if (calm) {
            int slot = resources_spawn_slot(sp, kind, continent_tier, chance);
            if (slot > sp->calm_max_slot) slot = sp->calm_max_slot;
            tid = resources_spawn_troop_at(sp, kind, slot);
        } else {
            tid = resources_spawn_troop(sp, kind, continent_tier, chance);
        }
        if (!tid || !tid[0]) continue;
        const TroopDef *td = troop_by_id(tid);
        if (!td) continue;
        int base = td->tier_counts[continent_tier];
        if (base < 2) base = 2;
        int jitter = game_rng_next(0, base / 2);
        game_copy_id(out[s].id, sizeof(out[s].id), tid);
        out[s].count = base + jitter;
    }
}

static void add_foe(Game *g, int continent, const char *zone, int x, int y,
                    const char *placement_id, bool friendly,
                    bool is_static, const ResZoneArmy *explicit_army) {
    if (!g || !GameReserveFoes(g, g->foe_count + 1)) return;
    FoeState *f = &g->foes[g->foe_count++];
    game_copy_id(f->zone, sizeof(f->zone), zone);
    f->x = x;
    f->y = y;
    f->origin_x = x;   // pinned to the spawn tile; wander updates x/y, not this
    f->origin_y = y;
    game_copy_id(f->placement_id, sizeof(f->placement_id), placement_id);
    f->alive = true;
    f->friendly = friendly;
    f->is_static = is_static;
    f->scene_index = -1;
    if (explicit_army) {
        game_copy_id(f->requires_troop, sizeof(f->requires_troop),
                explicit_army->requires_troop);
        f->scene_index = explicit_army->scene_index;
        game_copy_id(f->scene_title, sizeof(f->scene_title), explicit_army->title);
    }
    // Explicit garrison (a hand-tuned guardian) if one was declared; otherwise
    // roll by zone tier. For friendlies the garrison is unused (recruit dialog
    // rolls a fresh creature), but populating it keeps save/load + tests uniform.
    if (explicit_army && explicit_army->army_stacks > 0) {
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            f->garrison[s].id[0] = '\0';
            f->garrison[s].count = 0;
        }
        for (int s = 0; s < explicit_army->army_stacks && s < GAME_ARMY_SLOTS; s++) {
            game_copy_id(f->garrison[s].id, sizeof(f->garrison[s].id),
                    explicit_army->army_id[s]);
            f->garrison[s].count = explicit_army->army_count[s];
        }
    } else {
        roll_hostile_garrison(g, continent, x, y, f->garrison);
    }
}

const ResZoneArmy *GameFoeArmy(const Game *g, const FoeState *f) {
    if (!g || !g->res || !f || f->friendly) return NULL;
    for (int zi = 0; zi < g->res->zone_count; zi++) {
        const ResZone *z = &g->res->zones[zi];
        if (strcmp(z->id, f->zone) != 0) continue;
        for (int i = 0; i < z->army_count; i++) {
            const ResZoneArmy *a = &z->armies[i];
            char fallback[32];
            const char *aid = a->id;
            if (!aid[0]) {
                snprintf(fallback, sizeof(fallback), "static_foe_%d", i);
                aid = fallback;
            }
            if (strcmp(aid, f->placement_id) == 0) return a;
        }
        return NULL;
    }
    return NULL;
}

// True when a chest of the zone pins this artifact ("artifact": id).
static bool chest_pins_artifact(const ResZone *z, const char *artifact_id) {
    if (!z || !artifact_id || !artifact_id[0]) return false;
    for (int i = 0; i < z->chest_count; i++)
        if (strcmp(z->chests[i].artifact, artifact_id) == 0) return true;
    return false;
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

    // A chest that names an artifact ("artifact": id) is that artifact: it is
    // placed here, before the salt draws, and counts against the zone's
    // artifact quota, so the salt scatters only the rest.
    int pinned = 0;
    for (int i = 0; i < z->chest_count; i++) {
        const ResZoneChest *c = &z->chests[i];
        if (!c->artifact[0]) continue;
        GameAddPlacement(g, z->id, c->x, c->y, INTERACT_ARTIFACT, c->artifact);
        pinned++;
    }
    min_artifacts = (min_artifacts > pinned) ? min_artifacts - pinned : 0;

    // The barrel is every chest the salt may use: a "fixed" chest stays a
    // chest (a prize at the end of a path), and a pinned artifact is already
    // placed, so both are left out.
    int *slots = (int *)calloc((size_t)(z->chest_count > 0 ? z->chest_count : 1), sizeof(int));
    if (!slots) return;
    int barrel_len = 0;
    for (int i = 0; i < z->chest_count; i++)
        if (!z->chests[i].fixed && !z->chests[i].artifact[0]) slots[barrel_len++] = i;
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
                // `local_idx == artifact_counter`, skipping any a chest has
                // pinned (those are placed already, above).
                int ac = g->res->artifacts_count;
                int aidx = -1;
                while (aidx < 0 && artifact_counter < ac) {
                    for (int j = 0; j < ac; j++) {
                        const ArtifactDef *cand = artifact_by_index(j);
                        if (cand &&
                            strcmp(cand->zone, z->id) == 0 &&
                            cand->local_idx == artifact_counter) {
                            aidx = j;
                            break;
                        }
                    }
                    if (aidx >= 0 && chest_pins_artifact(z, artifact_by_index(aidx)->id)) {
                        aidx = -1;
                        artifact_counter++;
                        continue;
                    }
                    break;
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
                // Derive kind from troop's dwells field.
                Interact ik = dwelling_kind_to_interact(td->dwelling);
                snprintf(id, sizeof(id), "sd_%.20s_%d", tid, dwelling_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y, ik, id);
                // Pin the troop in the dwelling state row so first-visit
                // returns this exact troop, not a random one of the kind.
                game_enforce_dwelling_pinned(g, z->id, slot->x, slot->y, tid);
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
    game_copy_id(g->scepter.zone, sizeof(g->scepter.zone), z->id);
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
    bool passing = false;   // the drawn tile was a bridge; take the next plain one
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &MAP_TILE(m, x, y);
            if (t->terrain != TERRAIN_GRASS) continue;
            if (t->interactive != INTERACT_NONE) continue;
            if (t->blocks_foot) continue;
            if (count == target || passing) {
                // A bridge declares grass terrain over a river (#117): the
                // scepter passes on to the next plain tile in the same walk,
                // with no further draw, so the count and the draw above are
                // untouched and no world that never drew a bridge moves.
                if (t->is_bridge) { passing = true; continue; }
                g->scepter.x = x;
                g->scepter.y = y;
                MapFree(m); free(m);
                return;
            }
            count++;
        }
    }
    if (passing) {
        // The drawn bridge was the last tile of the walk: wrap to the first
        // plain tile so a bridge never leaves the scepter unburied.
        for (int y = 0; y < m->height; y++) {
            for (int x = 0; x < m->width; x++) {
                const Tile *t = &MAP_TILE(m, x, y);
                if (t->terrain != TERRAIN_GRASS || t->interactive != INTERACT_NONE ||
                    t->blocks_foot || t->is_bridge) continue;
                g->scepter.x = x;
                g->scepter.y = y;
                MapFree(m); free(m);
                return;
            }
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
            game_copy_id(cr->villain_id, sizeof(cr->villain_id), v->id);
            for (int s = 0; s < GAME_ARMY_SLOTS && s < 5; s++) {
                game_copy_id(cr->garrison[s].id,
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
// difficulty (= continent tier) governs
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
    game_copy_id(out_id, out_id_sz, troop_id);

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
        game_copy_id(cr->garrison[s].id, sizeof(cr->garrison[s].id), tid);
        cr->garrison[s].count = tcount;
    }
}
