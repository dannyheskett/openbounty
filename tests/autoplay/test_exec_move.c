// tests/autoplay/test_exec_move.c
//
// Isolation unit tests for the executor MOVEMENT helpers (autoplay/exec.h).
// Each test exercises ONE helper's own contract against a real seed-1 world —
// not an end-to-end plan run. Helpers added in P1 (exec_path / exec_travel /
// exec_reach) get their tests appended to this suite as they are built.

#include "greatest.h"

#include <string.h>

#include <string.h>

#include "fixtures.h"
#include "exec.h"        // the helpers under test
#include "recording.h"   // RecBuf / RecSink / recbuf_free
#include "nav.h"         // NavPoint / NavTravel / NavStatus / nav_reachable
#include "pending.h"     // pending_flow / FLOW_ATTACK_FOE
#include "adventure.h"   // adventure_walkable_on_foot
#include "tile.h"        // INTERACT_NONE
#include "map.h"

#define EXEC_SEED 1UL

// 8-neighbour scan order (fixed; orthogonals first so the chosen step never
// depends on diagonal-corner rules).
static const int NDX[8] = { 1, -1, 0,  0, -1, 1, -1, 1 };
static const int NDY[8] = { 0,  0, 1, -1, -1,-1,  1, 1 };

// A neighbour the on-foot hero can freely walk ONTO (walkable terrain, no
// interactive bouncer): exec_step there must move and record.
static bool find_free_dir(const Map *map, const Game *g, int *out_dx, int *out_dy) {
    for (int k = 0; k < 8; k++) {
        const Tile *t = MapGetTile(map, g->position.x + NDX[k], g->position.y + NDY[k]);
        if (!t) continue;
        if (t->interactive != INTERACT_NONE) continue;   // bouncer: would not move
        if (!adventure_walkable_on_foot(t)) continue;
        *out_dx = NDX[k]; *out_dy = NDY[k];
        return true;
    }
    return false;
}

// A neighbour the on-foot hero CANNOT enter (off-map, or non-foot terrain such as
// open water): exec_step there must NOT move but must still record the attempt.
// Skips interactive tiles (those are walkable terrain that merely bounces, and
// could raise a flow we must not leave dangling in an isolation test).
static bool find_blocked_dir(const Map *map, const Game *g, int *out_dx, int *out_dy) {
    for (int k = 0; k < 8; k++) {
        int nx = g->position.x + NDX[k], ny = g->position.y + NDY[k];
        const Tile *t = MapGetTile(map, nx, ny);
        if (!t) { *out_dx = NDX[k]; *out_dy = NDY[k]; return true; }   // off-map
        if (t->interactive != INTERACT_NONE) continue;                // could bounce
        if (!adventure_walkable_on_foot(t)) { *out_dx = NDX[k]; *out_dy = NDY[k]; return true; }
    }
    return false;
}

// exec_step moves the hero one tile AND records exactly one matching REC_MOVE.
TEST exec_step_moves_and_records(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    int dx = 0, dy = 0;
    ASSERT(find_free_dir(map, g, &dx, &dy));

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    int px = g->position.x, py = g->position.y;
    bool moved = exec_step(g, map, fog, res, dx, dy, &rec);

    ASSERT(moved);
    ASSERT_EQ_FMT(px + dx, g->position.x, "%d");
    ASSERT_EQ_FMT(py + dy, g->position.y, "%d");

    // Exactly one REC_MOVE carrying the same (dx,dy) — the helper's whole job.
    ASSERT_EQ_FMT(1, prims.count, "%d");
    ASSERT_EQ_FMT((int)REC_MOVE, (int)prims.items[0].kind, "%d");
    ASSERT_EQ_FMT(dx, (int)prims.items[0].dx, "%d");
    ASSERT_EQ_FMT(dy, (int)prims.items[0].dy, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// A blocked step records its REC_MOVE all the same: the executor logs intent, the
// engine decides the (null) effect, and replay reproduces it identically.
TEST exec_step_records_even_when_blocked(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    int dx = 0, dy = 0;
    if (!find_blocked_dir(map, g, &dx, &dy)) {
        fx_free_game_full(res, g, map, fog);
        SKIPm("seed-1 spawn has no blocked neighbour to exercise this path");
    }

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    int px = g->position.x, py = g->position.y;
    bool moved = exec_step(g, map, fog, res, dx, dy, &rec);

    ASSERT_FALSE(moved);                          // blocked: no tile change
    ASSERT_EQ_FMT(px, g->position.x, "%d");
    ASSERT_EQ_FMT(py, g->position.y, "%d");
    ASSERT_EQ_FMT(1, prims.count, "%d");          // recorded anyway (fidelity)
    ASSERT_EQ_FMT((int)REC_MOVE, (int)prims.items[0].kind, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// A NULL sink is a supported "not recording" mode: exec_step must still move and
// must not crash (rec_push_move guards NULL).
TEST exec_step_null_sink_is_safe(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    int dx = 0, dy = 0;
    ASSERT(find_free_dir(map, g, &dx, &dy));

    int px = g->position.x, py = g->position.y;
    bool moved = exec_step(g, map, fog, res, dx, dy, NULL);

    ASSERT(moved);
    ASSERT_EQ_FMT(px + dx, g->position.x, "%d");
    ASSERT_EQ_FMT(py + dy, g->position.y, "%d");

    fx_free_game_full(res, g, map, fog);
    PASS();
}

// A foot-reachable tile at least `min_steps` away from `from` (so the path is
// non-trivial), found by growing Chebyshev rings — deterministic, first hit wins.
static bool foot_reachable_target(const Map *map, NavPoint from, int min_steps,
                                  NavPoint *out) {
    NavOptions opts; nav_default_options(&opts);
    int maxr = map->width > map->height ? map->width : map->height;
    for (int r = min_steps; r <= maxr; r++)
        for (int y = from.y - r; y <= from.y + r; y++)
            for (int x = from.x - r; x <= from.x + r; x++) {
                int cx = (x - from.x >= 0) ? x - from.x : from.x - x;
                int cy = (y - from.y >= 0) ? y - from.y : from.y - y;
                if ((cx > cy ? cx : cy) != r) continue;
                if (x < 0 || x >= map->width || y < 0 || y >= map->height) continue;
                NavPoint cand = { x, y };
                int steps = 0;
                if (nav_reachable(map, from, cand, &opts, &steps) && steps >= min_steps) {
                    *out = cand; return true;
                }
            }
    return false;
}

// exec_path yields a legal single step toward a reachable foot target.
TEST exec_path_returns_a_valid_first_step(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    NavPoint from = { g->position.x, g->position.y };
    NavPoint target;
    ASSERT(foot_reachable_target(map, from, /*min_steps=*/6, &target));

    NavTravel foot = { NAV_MODE_FOOT, false, -1, -1 };
    int dx = 9, dy = 9;
    NavStatus ns = exec_path(map, from, &foot, target, /*goal_is_bouncer=*/false, &dx, &dy);

    ASSERT_EQ_FMT((int)NAV_OK, (int)ns, "%d");
    ASSERT(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
    ASSERT_FALSE(dx == 0 && dy == 0);

    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_path reports NAV_ARRIVED (and a zero step) when already on the goal.
TEST exec_path_arrived_when_from_equals_to(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    NavPoint here = { g->position.x, g->position.y };
    NavTravel foot = { NAV_MODE_FOOT, false, -1, -1 };
    int dx = 9, dy = 9;
    NavStatus ns = exec_path(map, here, &foot, here, /*goal_is_bouncer=*/false, &dx, &dy);

    ASSERT_EQ_FMT((int)NAV_ARRIVED, (int)ns, "%d");
    ASSERT_EQ_FMT(0, dx, "%d");
    ASSERT_EQ_FMT(0, dy, "%d");

    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_travel (plain) walks the hero to a reachable in-zone tile.
TEST exec_travel_reaches_a_tile(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    NavPoint from = { g->position.x, g->position.y };
    NavPoint target;
    ASSERT(foot_reachable_target(map, from, /*min_steps=*/6, &target));

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    bool arrived = exec_travel(g, map, fog, res, g->position.zone,
                               target.x, target.y, /*fight_through=*/false, &rec);
    ASSERT(arrived);
    ASSERT_EQ_FMT(target.x, g->position.x, "%d");
    ASSERT_EQ_FMT(target.y, g->position.y, "%d");
    ASSERT(prims.count > 0);                         // recorded its steps

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_reach defers (false) on an undiscovered destination zone rather than asserting.
TEST exec_reach_defers_undiscovered_zone(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    const char *undiscovered = NULL;
    for (int i = 0; i < res->zone_count && i < GAME_CONTINENTS; i++)
        if (!g->world.zones_discovered[i]) { undiscovered = res->zones[i].id; break; }
    if (!undiscovered) {
        fx_free_game_full(res, g, map, fog);
        SKIPm("all zones discovered at boot on this seed");
    }

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };
    ASSERT_FALSE(exec_reach(g, map, fog, res, undiscovered, 5, 5,
                            /*fight_through=*/true, &rec));

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// A foot-reachable hostile foe in the hero's zone, if any (else returns false).
static bool find_reachable_foe(Game *g, const Map *map, int *fx, int *fy) {
    NavOptions opts; nav_default_options(&opts);
    NavPoint from = { g->position.x, g->position.y };
    int best = -1;
    for (int i = 0; i < g->foe_count; i++) {
        FoeState *f = &g->foes[i];
        if (!f->alive || f->friendly) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        for (int k = 0; k < 8; k++) {
            NavPoint nb = { f->x + NDX[k], f->y + NDY[k] };
            int steps = 0;
            if (nav_reachable(map, from, nb, &opts, &steps)) {
                if (best < 0 || steps < best) { best = steps; *fx = f->x; *fy = f->y; }
                break;
            }
        }
    }
    return best >= 0;
}

// exec_reach travels up to a bouncing foe and steps ONTO it, raising FLOW_ATTACK_FOE.
TEST exec_reach_steps_onto_a_foe(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    int fx = 0, fy = 0;
    if (!find_reachable_foe(g, map, &fx, &fy)) {
        fx_free_game_full(res, g, map, fog);
        SKIPm("no foot-reachable hostile foe at spawn on this seed");
    }

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    bool reached = exec_reach(g, map, fog, res, g->position.zone, fx, fy,
                              /*fight_through=*/true, &rec);
    ASSERT(reached);
    ASSERT_EQ_FMT((int)FLOW_ATTACK_FOE, (int)pending_flow, "%d");  // bounced onto -> flow up
    exec_fight(g, map, &rec);                        // resolve so no flow dangles

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

SUITE(exec_move_suite) {
    RUN_TEST(exec_step_moves_and_records);
    RUN_TEST(exec_step_records_even_when_blocked);
    RUN_TEST(exec_step_null_sink_is_safe);
    RUN_TEST(exec_path_returns_a_valid_first_step);
    RUN_TEST(exec_path_arrived_when_from_equals_to);
    RUN_TEST(exec_travel_reaches_a_tile);
    RUN_TEST(exec_reach_defers_undiscovered_zone);
    RUN_TEST(exec_reach_steps_onto_a_foe);
}
