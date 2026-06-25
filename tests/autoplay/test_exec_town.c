// tests/autoplay/test_exec_town.c
//
// Isolation unit tests for the executor TOWN transaction helpers (exec_town.c):
// exec_buy_siege (#12), exec_take_contract (#13). Each assumes the hero is already
// gated into a town, so we set position.in_town (the engine's own town-gate marker)
// and ample gold directly — a cheap, reliable precondition, not an end-to-end town
// walk. exec_enter_town (the fight-through router) is tested separately.

#include "greatest.h"

#include <string.h>

#include "fixtures.h"
#include "exec.h"
#include "recording.h"
#include "game.h"          // GAME_TOWNS / g->towns[] / g->stats / g->boat / g->contract

#define EXEC_SEED 1UL

static void set_in_town(Game *g, const char *town_id) {
    snprintf(g->position.in_town, sizeof g->position.in_town, "%s", town_id);
}
static const char *any_town_id(const Game *g) {
    for (int i = 0; i < GAME_TOWNS; i++)
        if (g->towns[i].id[0]) return g->towns[i].id;
    return "town";   // the gated helpers only check in_town[0]; any non-empty works
}
TEST exec_buy_siege_buys_then_no_rebuy(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    set_in_town(g, any_town_id(g));
    g->stats.gold = 10000000;
    ASSERT_FALSE(g->stats.siege_weapons);

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };
    ASSERT(exec_buy_siege(g, &rec));
    ASSERT(g->stats.siege_weapons);
    ASSERT_EQ_FMT(1, prims.count, "%d");
    ASSERT_EQ_FMT((int)RA_BUY_SIEGE, (int)prims.items[0].action, "%d");

    // Already owned: a second call is a no-op success with no new record.
    RecBuf prims2 = {0}; CombatRecList combats2 = {0};
    RecSink rec2 = { &prims2, &combats2 };
    ASSERT(exec_buy_siege(g, &rec2));
    ASSERT_EQ_FMT(0, prims2.count, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    recbuf_free(&prims2); combatreclist_free(&combats2);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

TEST exec_take_contract_takes_any(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    set_in_town(g, any_town_id(g));

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };
    ASSERT(exec_take_contract(g, res, "", &rec));       // empty id = take any next
    ASSERT(g->contract.active_id[0] != '\0');
    ASSERT_EQ_FMT(1, prims.count, "%d");
    ASSERT_EQ_FMT((int)RA_TAKE_CONTRACT, (int)prims.items[0].action, "%d");

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// exec_enter_town routes (fight-through) to a town and gates in, yielding its dock.
TEST exec_enter_town_gates_in(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };

    int dx = -1, dy = -1;
    bool entered = exec_enter_town(g, map, fog, res, /*allow_castle=*/false, &dx, &dy, &rec);
    ASSERT(entered);
    ASSERT(g->position.in_town[0] != '\0');          // gated in
    ASSERT(prims.count > 0);                          // recorded the walk + gate step

    recbuf_free(&prims); combatreclist_free(&combats);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

// With allow_castle, the home (audience) castle is a haven: standing on its gate-landing
// tile, exec_enter_town retreats THERE (0 ticks), not to a farther town — and the same
// spot without allow_castle routes to a town instead (the flag gates the castle option).
TEST exec_enter_town_castle_haven(void) {
    Resources *res = NULL; Game *g = NULL; Map *map = NULL; Fog *fog = NULL;
    ASSERT(fx_init_game_full(&res, &g, &map, &fog, "continentia", EXEC_SEED));

    const ResCastle *home = NULL;
    for (int i = 0; i < res->castle_count; i++) {
        const ResCastle *c = &res->castles[i];
        if (strcmp(c->special.flow, "audience") == 0 &&
            strcmp(c->zone, g->position.zone) == 0 && c->gate_x >= 0 && c->gate_y >= 0) {
            home = c; break;
        }
    }
    if (!home) {
        fx_free_game_full(res, g, map, fog);
        SKIPm("audience castle not in continentia (with a gate-landing) on this seed");
    }

    // Stand the hero on the castle's gate-landing tile: the castle is now the 0-tick
    // haven, beating every town.
    g->position.x = home->gate_x; g->position.y = home->gate_y;
    g->position.in_town[0] = '\0';

    RecBuf prims = {0}; CombatRecList combats = {0};
    RecSink rec = { &prims, &combats };
    ASSERT(exec_enter_town(g, map, fog, res, /*allow_castle=*/true, NULL, NULL, &rec));
    ASSERT_EQ_FMT(home->gate_x, g->position.x, "%d");   // retreated to the CASTLE...
    ASSERT_EQ_FMT(home->gate_y, g->position.y, "%d");
    ASSERT(g->position.in_town[0] == '\0');             // ...not a town

    // Same spot, allow_castle=false: the castle is not a candidate, so it routes to a town.
    g->position.x = home->gate_x; g->position.y = home->gate_y;
    g->position.in_town[0] = '\0';
    RecBuf prims2 = {0}; CombatRecList combats2 = {0};
    RecSink rec2 = { &prims2, &combats2 };
    ASSERT(exec_enter_town(g, map, fog, res, /*allow_castle=*/false, NULL, NULL, &rec2));
    ASSERT(g->position.in_town[0] != '\0');             // gated into a town

    recbuf_free(&prims); combatreclist_free(&combats);
    recbuf_free(&prims2); combatreclist_free(&combats2);
    fx_free_game_full(res, g, map, fog);
    PASS();
}

SUITE(exec_town_suite) {
    RUN_TEST(exec_enter_town_gates_in);
    RUN_TEST(exec_enter_town_castle_haven);
    RUN_TEST(exec_buy_siege_buys_then_no_rebuy);
    RUN_TEST(exec_take_contract_takes_any);
}
