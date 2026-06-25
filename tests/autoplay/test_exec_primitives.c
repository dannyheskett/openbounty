// tests/autoplay/test_exec_primitives.c
//
// Primitive-level tests: one smoke test per PrimKind, all driven through execute().
// PRIM_TOWN is split into its 4 sub-services (SIEGE, BOAT, CONTRACT, SPELL) for a
// total of 14 primitive paths + the null guard = 15 tests.
//
// Tests that require a world object (alcove, foe, castle, dwelling) enumerate at
// runtime via plansteps_enumerate_*; SKIP() if seed-1 continentia has zero candidates.
// Rule 6 (town-adjacency contract) is verified on every successful execute() call.
//
// PRIM_CAST: tested via STEP_CAST_SPELL (spell_index = -1 → exec_cast guard → false).
// PRIM_HOME: vestigial; STEP_RECRUIT_HOME dispatches to exec_home → always false.

#include "greatest.h"

#include <string.h>

#include "fixtures.h"
#include "goals.h"         // PlanStep / PlanKind / STEP_*; plansteps_enumerate_*
#include "primitives.h"    // execute
#include "recording.h"
#include "nav.h"           // nav_reachable / nav_default_options (reachability pre-check)

#define EXEC_SEED 1UL

// 8-neighbour offsets (orthogonals first) — used to find an approach tile for
// reachability pre-checks before a foe/castle smoke test commits to execute().
static const int SNDX[8] = { 1,-1, 0, 0,-1, 1,-1, 1 };
static const int SNDY[8] = { 0, 0, 1,-1,-1,-1, 1, 1 };

// Resolve zone name to its res->zones[] index, or -1 if absent.
static int find_zi(const Resources *res, const char *zone) {
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, zone) == 0) return i;
    return -1;
}

// Check if any 8-neighbour of (tx,ty) is reachable via foot-nav from `from`.
// Used to pre-qualify foe/castle targets before committing to execute().
static bool any_neighbor_reachable(const Map *map, NavPoint from, int tx, int ty) {
    NavOptions opts; nav_default_options(&opts);
    for (int k = 0; k < 8; k++) {
        NavPoint nb = { tx + SNDX[k], ty + SNDY[k] };
        int steps = 0;
        if (nav_reachable(map, from, nb, &opts, &steps)) return true;
    }
    return false;
}

// ─── NULL GUARD ──────────────────────────────────────────────────────────────

TEST prim_execute_guards_null(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    ASSERT_FALSE(execute(NULL, res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_FETCH ──────────────────────────────────────────────────────────────

// Target = hero's tile: trivially arrived; Rule 6 routes him to nearest town.
TEST prim_fetch_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    PlanStep s; memset(&s, 0, sizeof s);
    s.kind       = STEP_CHEST;
    s.zone_index = -1;                           // PRIM_DZ falls back to hero's zone
    s.target.x   = g->position.x;
    s.target.y   = g->position.y;
    ASSERT(execute(&s, res, g, map, fog, &rec));
    ASSERT(g->position.in_town[0] != '\0');      // Rule 6

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_WAIT ───────────────────────────────────────────────────────────────

TEST prim_wait_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    PlanStep s; memset(&s, 0, sizeof s);
    s.kind       = STEP_WAIT_WEEKS;
    s.target.x   = 1;                            // week count via target.x
    ASSERT(execute(&s, res, g, map, fog, &rec));
    ASSERT_EQ_FMT((int)RA_WAIT_WEEK, (int)prims.items[0].action, "%d");
    ASSERT(g->position.in_town[0] != '\0');      // Rule 6

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_HOME ───────────────────────────────────────────────────────────────

// exec_home is vestigial and always returns false.
TEST prim_home_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    PlanStep s; memset(&s, 0, sizeof s);
    s.kind = STEP_RECRUIT_HOME;
    ASSERT_FALSE(execute(&s, res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_TOWN / SIEGE ───────────────────────────────────────────────────────

// Pre-own siege weapons → exec_town short-circuits without navigating.
TEST prim_town_siege_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    g->stats.siege_weapons = 1;
    PlanStep s; memset(&s, 0, sizeof s);
    s.kind = STEP_SIEGE_WEAPONS;
    ASSERT(execute(&s, res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_TOWN / BOAT ────────────────────────────────────────────────────────

// Pre-hold a boat → exec_town short-circuits.
TEST prim_town_boat_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    g->boat.has_boat = 1;
    PlanStep s; memset(&s, 0, sizeof s);
    s.kind = STEP_RENT_BOAT;
    ASSERT(execute(&s, res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_TOWN / CONTRACT ────────────────────────────────────────────────────

// Empty handle → take any available contract (first in cycle).
TEST prim_town_contract_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    g->stats.gold = 200000;
    PlanStep s; memset(&s, 0, sizeof s);
    s.kind = STEP_TAKE_CONTRACT;
    // handle = "" → exec_take_contract cycles once, takes the first available contract
    ASSERT(execute(&s, res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_TOWN / SPELL ───────────────────────────────────────────────────────

// exec_enter_town routes to the nearest town; SKIP if that town has no spell.
TEST prim_town_spell_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    g->stats.gold = 200000;
    PlanStep s; memset(&s, 0, sizeof s);
    s.kind = STEP_BUY_SPELLS;
    bool ok = execute(&s, res, g, map, fog, &rec);
    if (!ok) SKIPm("nearest town on this seed does not offer a spell");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_LEARN ──────────────────────────────────────────────────────────────

TEST prim_learn_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    int zi = find_zi(res, "continentia");
    PlanStepSet set; memset(&set, 0, sizeof set);
    plansteps_enumerate_noncombat(&set, g, map, zi);
    int idx = -1;
    for (int i = 0; i < set.count; i++)
        if (set.steps[i].kind == STEP_ALCOVE) { idx = i; break; }
    if (idx < 0) SKIPm("no alcove in continentia on this seed");

    ASSERT(execute(&set.steps[idx], res, g, map, fog, &rec));
    ASSERT(g->stats.knows_magic);

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_SLAY ───────────────────────────────────────────────────────────────

// Enumerate foes; pick the first one with a nav-reachable neighbor. Teleport the
// hero to that neighbor so exec_reach has zero navigation work: it steps directly
// onto the foe tile, raising FLOW_ATTACK_FOE, then exec_fight runs.
TEST prim_slay_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    int zi = find_zi(res, "continentia");
    PlanStepSet set; memset(&set, 0, sizeof set);
    plansteps_enumerate_foes(&set, g, zi);

    // Find a foe + approach cell: the first neighbor of the foe that is
    // nav-reachable from spawn (block-bouncers policy, foot only).
    NavPoint from = { g->position.x, g->position.y };
    int best = -1;
    int approach_x = -1, approach_y = -1;
    for (int i = 0; i < set.count && best < 0; i++) {
        FoeState *foe_i = GameFindFoe(g, set.steps[i].handle);
        if (!foe_i || !foe_i->alive) continue;
        NavOptions opts; nav_default_options(&opts);
        for (int k = 0; k < 8; k++) {
            NavPoint nb = { foe_i->x + SNDX[k], foe_i->y + SNDY[k] };
            if (nav_reachable(map, from, nb, &opts, NULL)) {
                best = i; approach_x = nb.x; approach_y = nb.y; break;
            }
        }
    }
    if (best < 0) SKIPm("no foot-reachable hostile foe in continentia on this seed");

    // Foe side: 1 peasant — minimal defender.
    FoeState *foe = GameFindFoe(g, set.steps[best].handle);
    if (!foe) SKIPm("foe handle not found in game state");
    memset(foe->garrison, 0, sizeof foe->garrison);
    snprintf(foe->garrison[0].id, sizeof foe->garrison[0].id, "peasants");
    foe->garrison[0].count = 1;

    // Hero side: 100 in-control militia + massive leadership guarantees WIN.
    g->stats.leadership_base = g->stats.leadership_current = 999999;
    memset(g->army, 0, sizeof g->army);
    snprintf(g->army[0].id, sizeof g->army[0].id, "militia");
    g->army[0].count = 100;

    // Teleport hero to the approach tile. exec_reach finds the hero already at
    // the chosen neighbor (Chebyshev = 0), so exec_travel has no work to do and
    // exec_step fires directly onto the foe. This eliminates all fight-through
    // navigation that could modify g->army before the prediction runs.
    g->position.x = approach_x;
    g->position.y = approach_y;

    ASSERT(execute(&set.steps[best], res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_SIEGE ──────────────────────────────────────────────────────────────

// Find a reachable monster castle gate; pre-own siege weapons + dragons guarantee win.
TEST prim_siege_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    int zi = find_zi(res, "continentia");
    PlanStepSet set; memset(&set, 0, sizeof set);
    plansteps_enumerate_combat(&set, g, zi);

    // Find the first monster-castle gate reachable from spawn.
    NavPoint from = { g->position.x, g->position.y };
    int best = -1;
    for (int i = 0; i < set.count; i++) {
        if (set.steps[i].kind != STEP_MONSTER_CASTLE) continue;
        if (any_neighbor_reachable(map, from, set.steps[i].target.x,
                                   set.steps[i].target.y)) { best = i; break; }
    }
    if (best < 0) SKIPm("no reachable monster castle in continentia on this seed");

    // Replace the castle garrison with 1 peasant: trivially beatable, leaves enough
    // survivors for exec_garrison (hero loses nothing meaningful to 1 peasant).
    // castle_id is in the PlanStep handle (plansteps_enumerate_combat sets it).
    g->stats.siege_weapons = 1;
    CastleRecord *castle = GameFindCastle(g, set.steps[best].handle);
    if (!castle) SKIPm("castle handle not found in game state");
    memset(castle->garrison, 0, sizeof castle->garrison);
    snprintf(castle->garrison[0].id, sizeof castle->garrison[0].id, "peasants");
    castle->garrison[0].count = 1;
    ASSERT(execute(&set.steps[best], res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_DIG ────────────────────────────────────────────────────────────────

// Plant the scepter at the hero's current tile: travel is trivial, search succeeds.
TEST prim_dig_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    snprintf(g->scepter.zone, sizeof g->scepter.zone, "%s", g->position.zone);
    g->scepter.x   = g->position.x;
    g->scepter.y   = g->position.y;
    g->scepter.key = 1;

    PlanStep s; memset(&s, 0, sizeof s);
    s.kind       = STEP_SCEPTER;
    s.zone_index = -1;                           // PRIM_DZ falls back to hero's zone
    s.target.x   = g->position.x;
    s.target.y   = g->position.y;
    ASSERT(execute(&s, res, g, map, fog, &rec));
    ASSERT(g->stats.won);

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_ARMY ───────────────────────────────────────────────────────────────

// STEP_RECOMPOSE_ARMY carries no army data; planstep_to_prim leaves ArmyTarget zeroed.
// The test exercises dispatch to exec_recruit; the outcome is not asserted.
TEST prim_army_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    PlanStep s; memset(&s, 0, sizeof s);
    s.kind = STEP_RECOMPOSE_ARMY;
    execute(&s, res, g, map, fog, &rec);

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_DWELL ──────────────────────────────────────────────────────────────

// Scan salted placements for any dwelling in continentia; SKIP if none found.
TEST prim_dwell_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    int zi = find_zi(res, "continentia");
    const SaltedPlacement *dwell = NULL;
    for (int i = 0; i < g->placement_count; i++) {
        const SaltedPlacement *p = &g->placements[i];
        if (strcmp(p->zone, "continentia") != 0) continue;
        if (p->kind == INTERACT_DWELLING_PLAINS ||
            p->kind == INTERACT_DWELLING_FOREST ||
            p->kind == INTERACT_DWELLING_HILLS  ||
            p->kind == INTERACT_DWELLING_DUNGEON) { dwell = p; break; }
    }
    if (!dwell) SKIPm("no dwelling placement in continentia on this seed");

    g->stats.gold = 200000;
    PlanStep s; memset(&s, 0, sizeof s);
    s.kind       = STEP_RECRUIT_DWELLING;
    s.zone_index = zi;
    s.tile_bound = true;
    s.target.x   = dwell->x;
    s.target.y   = dwell->y;
    ASSERT(execute(&s, res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// ─── PRIM_CAST ───────────────────────────────────────────────────────────────

// spell_index = -1 hits the exec_cast guard and returns false. This is the minimum
// proof that STEP_CAST_SPELL routes through execute() → exec_cast.
TEST prim_cast_smoke(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));
    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    PlanStep s; memset(&s, 0, sizeof s);
    s.kind       = STEP_CAST_SPELL;
    s.target.x   = -1;                          // spell_index < 0 → exec_cast guard
    ASSERT_FALSE(execute(&s, res, g, map, fog, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

SUITE(exec_prim_suite) {
    RUN_TEST(prim_execute_guards_null);
    RUN_TEST(prim_fetch_smoke);
    RUN_TEST(prim_wait_smoke);
    RUN_TEST(prim_home_smoke);
    RUN_TEST(prim_town_siege_smoke);
    RUN_TEST(prim_town_boat_smoke);
    RUN_TEST(prim_town_contract_smoke);
    RUN_TEST(prim_town_spell_smoke);
    RUN_TEST(prim_learn_smoke);
    RUN_TEST(prim_slay_smoke);
    RUN_TEST(prim_siege_smoke);
    RUN_TEST(prim_dig_smoke);
    RUN_TEST(prim_army_smoke);
    RUN_TEST(prim_dwell_smoke);
    RUN_TEST(prim_cast_smoke);
}
