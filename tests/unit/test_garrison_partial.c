// Partial garrison / withdraw (GameGarrisonTroopCount, GameUngarrisonTroopCount):
// moving part of a stack leaves the rest where it was, the whole stack behaves
// exactly as the original move, and the last-army rule only refuses a whole
// last stack.

#include "greatest.h"
#include "fixtures.h"
#include "tables.h"
#include <string.h>

static CastleRecord *own_first_castle(Game *g) {
    CastleRecord *cr = &g->castles[0];
    cr->owner_kind = CASTLE_OWNER_PLAYER;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) { cr->garrison[i].id[0] = '\0'; cr->garrison[i].count = 0; }
    snprintf(g->position.own_castle, sizeof g->position.own_castle, "%s", cr->id);
    return cr;
}

static void set_army(Game *g, const char *a, int na, const char *b, int nb) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) { g->army[i].id[0] = '\0'; g->army[i].count = 0; }
    snprintf(g->army[0].id, sizeof g->army[0].id, "%s", a); g->army[0].count = na;
    if (b) { snprintf(g->army[1].id, sizeof g->army[1].id, "%s", b); g->army[1].count = nb; }
}

TEST partial_garrison_splits_the_stack(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, 0));
    CastleRecord *cr = own_first_castle(g);
    const TroopDef *t0 = troop_by_index(0), *t1 = troop_by_index(1);
    set_army(g, t0->id, 50, t1->id, 10);
    ASSERT_EQ(0, GameGarrisonTroopCount(g, cr->id, 0, 20));
    ASSERT_EQ(30, g->army[0].count);
    ASSERT_STR_EQ(t0->id, cr->garrison[0].id);
    ASSERT_EQ(20, cr->garrison[0].count);
    // A second part stacks on the same garrison troop.
    ASSERT_EQ(0, GameGarrisonTroopCount(g, cr->id, 0, 5));
    ASSERT_EQ(25, g->army[0].count);
    ASSERT_EQ(25, cr->garrison[0].count);
    // Out of range counts are refused and change nothing.
    ASSERT_EQ(1, GameGarrisonTroopCount(g, cr->id, 0, 0));
    ASSERT_EQ(1, GameGarrisonTroopCount(g, cr->id, 0, 26));
    ASSERT_EQ(25, g->army[0].count);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST last_stack_rule(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, 0));
    CastleRecord *cr = own_first_castle(g);
    const TroopDef *t0 = troop_by_index(0);
    set_army(g, t0->id, 50, NULL, 0);
    ASSERT_EQ(2, GameGarrisonTroopCount(g, cr->id, 0, 50));    // the whole last stack
    ASSERT_EQ(2, GameGarrisonTroop(g, cr->id, 0));
    ASSERT_EQ(0, GameGarrisonTroopCount(g, cr->id, 0, 49));    // part of it is fine
    ASSERT_EQ(1, g->army[0].count);
    ASSERT_EQ(49, cr->garrison[0].count);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST whole_moves_match_the_original(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, 0));
    CastleRecord *cr = own_first_castle(g);
    const TroopDef *t0 = troop_by_index(0), *t1 = troop_by_index(1);
    set_army(g, t0->id, 50, t1->id, 10);
    ASSERT_EQ(0, GameGarrisonTroop(g, cr->id, 0));            // whole: army compacts
    ASSERT_STR_EQ(t1->id, g->army[0].id);
    ASSERT_EQ(10, g->army[0].count);
    ASSERT_EQ(0, g->army[1].count);
    ASSERT_EQ(50, cr->garrison[0].count);
    ASSERT_EQ(0, GameUngarrisonTroopCount(g, cr->id, 0, 15)); // part: garrison keeps the rest
    ASSERT_EQ(35, cr->garrison[0].count);
    ASSERT_STR_EQ(t0->id, g->army[1].id);
    ASSERT_EQ(15, g->army[1].count);
    ASSERT_EQ(0, GameUngarrisonTroop(g, cr->id, 0));          // whole: garrison compacts
    ASSERT_EQ(0, cr->garrison[0].count);
    ASSERT_EQ(50, g->army[1].count);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST refused_away_from_the_castle(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, 0));
    CastleRecord *cr = own_first_castle(g);
    const TroopDef *t0 = troop_by_index(0), *t1 = troop_by_index(1);
    set_army(g, t0->id, 50, t1->id, 10);
    g->position.own_castle[0] = '\0';
    ASSERT_EQ(1, GameGarrisonTroopCount(g, cr->id, 0, 10));
    ASSERT_EQ(50, g->army[0].count);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(unit_garrison_partial_suite) {
    RUN_TEST(partial_garrison_splits_the_stack);
    RUN_TEST(last_stack_rule);
    RUN_TEST(whole_moves_match_the_original);
    RUN_TEST(refused_away_from_the_castle);
}
