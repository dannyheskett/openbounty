// Economy tests: recruit drains gold, refuses when broke, refuses
// when leadership cap exceeded, and stacks onto matching slots.

#include "greatest.h"
#include "game.h"
#include "tables.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

static int find_slot(const Game *g, const char *id) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, id) == 0) return i;
    }
    return -1;
}

// Put the hero on a dwelling that offers `troop_id` with `pop` recruits, so a
// GameBuyTroop(g, troop_id, n<=pop) is at a LEGAL recruit location (the engine
// location guard requires position.dwelling_troop to match the troop the hero
// stands on). Mirrors stepping onto a dwelling tile in step.c.
static void recruit_here(Game *g, const char *troop_id, int pop) {
    int i = g->dwelling_count++;
    DwellingState *d = &g->dwellings[i];
    snprintf(d->zone, sizeof d->zone, "%s", g->position.zone);
    d->x = g->position.x; d->y = g->position.y;
    snprintf(d->troop_id, sizeof d->troop_id, "%s", troop_id);
    d->count = pop; d->max_population = pop;
    snprintf(g->position.dwelling_troop, sizeof g->position.dwelling_troop,
             "%s", troop_id);
    g->position.dwelling_x = g->position.x;
    g->position.dwelling_y = g->position.y;
}

TEST buy_troop_drains_gold(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 10000;
    g->stats.leadership_current = 1000;

    const TroopDef *t = troop_by_id("peasants");
    ASSERT(t);
    recruit_here(g, "peasants", 1000);
    int gold_before = g->stats.gold;
    int rc = GameBuyTroop(g, "peasants", 5);
    ASSERT_EQ(0, rc);
    ASSERT_EQ(gold_before - t->recruit_cost * 5, g->stats.gold);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_refuses_when_broke(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 1;             // far less than any troop cost
    g->stats.leadership_current = 1000;
    recruit_here(g, "peasants", 1000);
    int rc = GameBuyTroop(g, "peasants", 5);
    ASSERT_EQ(1, rc);              // 1 = insufficient gold
    ASSERT_EQ(1, g->stats.gold);   // gold untouched
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_refuses_over_leadership(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 1000000;
    g->stats.leadership_current = 5;   // can only handle a tiny stack
    recruit_here(g, "dragons", 100);
    int rc = GameBuyTroop(g, "dragons", 100);   // dragons = 200 HP each
    ASSERT_EQ(3, rc);                  // 3 = leadership exceeded
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_zero_or_negative_count_refused(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 10000;
    g->stats.leadership_current = 1000;
    ASSERT(GameBuyTroop(g, "peasants", 0)  != 0);
    ASSERT(GameBuyTroop(g, "peasants", -1) != 0);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_unknown_id_refused(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 10000;
    int gold_before = g->stats.gold;
    int rc = GameBuyTroop(g, "nonexistent", 5);
    ASSERT(rc != 0);
    ASSERT_EQ(gold_before, g->stats.gold);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST buy_troop_stacks_on_matching_slot(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 100000;
    g->stats.leadership_current = 10000;
    recruit_here(g, "peasants", 1000);

    GameBuyTroop(g, "peasants", 3);
    int slot = find_slot(g, "peasants");
    ASSERT(slot >= 0);
    int count_before = g->army[slot].count;
    GameBuyTroop(g, "peasants", 7);
    // Same slot -- count is the sum, no new slot used.
    ASSERT_EQ(slot, find_slot(g, "peasants"));
    ASSERT_EQ(count_before + 7, g->army[slot].count);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST max_recruitable_ignores_other_troops(void) {
    // Per-troop cap only subtracts the SAME troop's leadership
    // consumption, not other stacks. This is what lets a fresh Knight
    // recruit 30 Militia + 8 Archers + 10 Pikemen even though the
    // totals exceed leadership 100.
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    g->stats.leadership_current = 100;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    int rc = GameAddTroop(g, "peasants", 60);
    ASSERT_EQ(0, rc);

    // Cavalry hp=20. Other-troop leadership ignored; free = 100.
    // Max = 100/20 = 5.
    int max_cavalry = GameMaxRecruitable(g, "cavalry");
    ASSERT_EQ(5, max_cavalry);

    // Knights hp=35. Max = 100/35 = 2.
    int max_knights = GameMaxRecruitable(g, "knights");
    ASSERT_EQ(2, max_knights);

    // Adding more peasants reduces the peasant cap (same-troop slot
    // consumption IS subtracted). Free for peasants = 100 - 60 = 40.
    int max_peasants = GameMaxRecruitable(g, "peasants");
    ASSERT_EQ(40, max_peasants);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST max_recruitable_scales_with_leadership(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    // Empty the army first so existing stacks don't eat the leadership budget.
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }

    g->stats.leadership_current = 100;
    int n100 = GameMaxRecruitable(g, "peasants");
    g->stats.leadership_current = 200;
    int n200 = GameMaxRecruitable(g, "peasants");
    ASSERT(n200 > n100);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST compact_army_collapses_gaps(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    // Lay out a deliberate gap: slots 0 and 2 filled, 1/3/4 empty.
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    snprintf(g->army[0].id, sizeof g->army[0].id, "peasants");
    g->army[0].count = 50;
    snprintf(g->army[2].id, sizeof g->army[2].id, "archers");
    g->army[2].count = 5;

    GameCompactArmy(g);

    ASSERT_STR_EQ("peasants", g->army[0].id);
    ASSERT_EQ(50, g->army[0].count);
    ASSERT_STR_EQ("archers", g->army[1].id);
    ASSERT_EQ(5, g->army[1].count);
    ASSERT_EQ('\0', g->army[2].id[0]);
    ASSERT_EQ(0, g->army[2].count);
    ASSERT_EQ('\0', g->army[3].id[0]);
    ASSERT_EQ('\0', g->army[4].id[0]);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST compact_army_zero_count_treated_as_empty(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    // Slot 0: id set but count=0 -> treated as empty (combat wipe leaves
    // this state before compact).
    snprintf(g->army[0].id, sizeof g->army[0].id, "ghosts");
    g->army[0].count = 0;
    snprintf(g->army[1].id, sizeof g->army[1].id, "knights");
    g->army[1].count = 3;

    GameCompactArmy(g);
    ASSERT_STR_EQ("knights", g->army[0].id);
    ASSERT_EQ(3, g->army[0].count);
    ASSERT_EQ('\0', g->army[1].id[0]);

    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST compact_army_already_dense_unchanged(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    snprintf(g->army[0].id, sizeof g->army[0].id, "peasants");
    g->army[0].count = 10;
    snprintf(g->army[1].id, sizeof g->army[1].id, "militia");
    g->army[1].count = 5;

    GameCompactArmy(g);
    ASSERT_STR_EQ("peasants", g->army[0].id);
    ASSERT_EQ(10, g->army[0].count);
    ASSERT_STR_EQ("militia", g->army[1].id);
    ASSERT_EQ(5, g->army[1].count);
    fx_free_game_full(res, g, m, f);
    PASS();
}

// ---- Boat rental engine cores (GameRentBoat / GameCancelBoat) ----

TEST rent_boat_deducts_gold_and_places(void) {
    Resources *res = NULL; Game *g = NULL; Map *m = NULL; Fog *f = NULL;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    int cost = GameBoatCost(g);
    g->stats.gold = cost + 1;            // just enough (must be strictly >)
    // Boat rental is a TOWN transaction (engine in_town guard): the hero must be
    // at a town. Mirror the step-onto-gate state the player reaches before renting.
    snprintf(g->position.in_town, sizeof g->position.in_town, "%s", "test_town");
    BoatActionResult r = GameRentBoat(g, 7, 9, "continentia");
    ASSERT_EQ_FMT((int)BOAT_RENT_OK, (int)r, "%d");
    ASSERT(g->boat.has_boat);
    ASSERT_EQ_FMT(7, g->boat.x, "%d");
    ASSERT_EQ_FMT(9, g->boat.y, "%d");
    ASSERT_EQ_FMT(1, g->stats.gold, "%d");
    ASSERT_EQ(0, strcmp(g->boat.zone, "continentia"));
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST rent_boat_refuses_when_gold_equals_cost(void) {
    Resources *res = NULL; Game *g = NULL; Map *m = NULL; Fog *f = NULL;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    int cost = GameBoatCost(g);
    g->stats.gold = cost;                // exact match must fail (KB rule)
    BoatActionResult r = GameRentBoat(g, 1, 2, "continentia");
    ASSERT_EQ_FMT((int)BOAT_RENT_NO_GOLD, (int)r, "%d");
    ASSERT_FALSE(g->boat.has_boat);
    ASSERT_EQ_FMT(cost, g->stats.gold, "%d");   // gold untouched
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST cancel_boat_clears_when_ashore(void) {
    Resources *res = NULL; Game *g = NULL; Map *m = NULL; Fog *f = NULL;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = GameBoatCost(g) + 1;
    snprintf(g->position.in_town, sizeof g->position.in_town, "%s", "test_town");
    ASSERT_EQ_FMT((int)BOAT_RENT_OK, (int)GameRentBoat(g, 3, 4, "continentia"), "%d");
    g->travel_mode = TRAVEL_WALK;        // ashore
    ASSERT_EQ_FMT((int)BOAT_CANCEL_OK, (int)GameCancelBoat(g), "%d");
    ASSERT_FALSE(g->boat.has_boat);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST cancel_boat_refused_at_sea(void) {
    Resources *res = NULL; Game *g = NULL; Map *m = NULL; Fog *f = NULL;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = GameBoatCost(g) + 1;
    snprintf(g->position.in_town, sizeof g->position.in_town, "%s", "test_town");
    ASSERT_EQ_FMT((int)BOAT_RENT_OK, (int)GameRentBoat(g, 3, 4, "continentia"), "%d");
    g->travel_mode = TRAVEL_BOAT;        // sailing
    ASSERT_EQ_FMT((int)BOAT_CANCEL_AT_SEA, (int)GameCancelBoat(g), "%d");
    ASSERT(g->boat.has_boat);            // still held -- must disembark first
    fx_free_game_full(res, g, m, f);
    PASS();
}

// At the week's end a castle the hero holds with no garrison falls back to the
// monsters with a fresh garrison; one with a stack left in it stays the hero's
// untouched (issue #112, OPENKB-SPEC section 16.11).
TEST empty_player_castle_is_retaken_at_week_end(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    int empty = -1, held = -1;
    for (int i = 0; i < g->castle_count; i++) {
        if (g->castles[i].owner_kind != CASTLE_OWNER_MONSTERS) continue;
        if (empty < 0) empty = i; else { held = i; break; }
    }
    ASSERT(empty >= 0 && held >= 0);
    // Both taken, as a siege win leaves them; one gets a stack.
    CastleRecord *ce = &g->castles[empty], *ch = &g->castles[held];
    ce->owner_kind = ch->owner_kind = CASTLE_OWNER_PLAYER;
    ce->taken = ch->taken = true;
    memset(ce->garrison, 0, sizeof ce->garrison);
    memset(ch->garrison, 0, sizeof ch->garrison);
    snprintf(ch->garrison[2].id, sizeof ch->garrison[2].id, "%.*s",
             (int)sizeof ch->garrison[2].id - 1, g->army[0].id);
    ch->garrison[2].count = 7;
    snprintf(g->position.own_castle, sizeof g->position.own_castle, "%s", ce->id);
    int owned = GameCastlesOwned(g);
    int paid = 0;
    GameSpendWeek(g, &paid);
    ASSERT_EQ(CASTLE_OWNER_MONSTERS, ce->owner_kind);
    ASSERT(ce->taken);                              // still counts as won once
    int stacks = 0;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
        if (ce->garrison[s].count > 0 && ce->garrison[s].id[0]) stacks++;
    ASSERT_EQ(5, stacks);
    ASSERT_EQ('\0', g->position.own_castle[0]);   // no garrisoning a lost castle
    ASSERT_EQ(CASTLE_OWNER_PLAYER, ch->owner_kind);
    ASSERT_STR_EQ(g->army[0].id, ch->garrison[2].id);
    ASSERT_EQ(7, ch->garrison[2].count);
    ASSERT_EQ(owned - 1, GameCastlesOwned(g));
    fx_free_game_full(res, g, m, f);
    PASS();
}

// The week's end records what it charged, so the budget screen adds up:
// On Hand + Payment - Boat - Army = Balance, with each stack at a tenth of
// its recruit cost rounded down per unit (issue #130, OPENKB-SPEC 16.7).
TEST week_end_records_what_it_charged(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    // A troop whose cost is not a multiple of 10, where dividing the army's
    // total by ten would disagree with the per-stack charge.
    const TroopDef *odd = NULL;
    for (int i = 0; i < troops_count() && !odd; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && t->recruit_cost > 10 && t->recruit_cost % 10) odd = t;
    }
    ASSERT(odd);
    memset(g->army, 0, sizeof g->army);
    snprintf(g->army[0].id, sizeof g->army[0].id, "%s", odd->id);
    g->army[0].count = 30;
    g->boat.has_boat = false;
    int upkeep = 30 * (odd->recruit_cost / 10);
    ASSERT_EQ(upkeep, GameArmyWeeklyUpkeep(g));

    g->stats.gold = 2000;
    int paid = 0;
    GameSpendWeek(g, &paid);
    ASSERT_EQ(2000, g->stats.last_week_on_hand);
    ASSERT_EQ(upkeep, g->stats.last_week_army);
    ASSERT_EQ(0, g->stats.last_week_boat);
    ASSERT_EQ(g->stats.last_week_on_hand + g->stats.last_commission
              - g->stats.last_week_boat - g->stats.last_week_army,
              g->stats.gold);

    // A wallet short of the upkeep pays what it holds, and still adds up.
    g->army[0].count = 30000;
    g->stats.gold = 0;
    GameSpendWeek(g, &paid);
    ASSERT_EQ(0, g->stats.gold);
    ASSERT_EQ(g->stats.last_commission, g->stats.last_week_army);
    ASSERT_EQ(g->stats.last_week_on_hand + g->stats.last_commission
              - g->stats.last_week_boat - g->stats.last_week_army,
              g->stats.gold);
    fx_free_game_full(res, g, m, f);
    PASS();
}

// Where unpaid troops leave (#141), the week pays stacks in slot order, each
// in full or not at all; a stack it cannot pay leaves, the rest close up, and
// the week's prediction (GameWeeklyNetGold) matches what it did. Without the
// pack setting (King's Bounty) the army stays and the gold floor takes the
// shortfall, as openkb.
TEST unpaid_troops_leave_where_the_pack_says(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    const TroopDef *dear = NULL, *cheap = NULL;
    for (int i = 0; i < troops_count(); i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t || t->recruit_cost < 10) continue;
        if (!dear || t->recruit_cost > dear->recruit_cost) dear = t;
        if (!cheap || t->recruit_cost < cheap->recruit_cost) cheap = t;
    }
    ASSERT(dear && cheap && dear != cheap);
    g->boat.has_boat = false;
    g->stats.commission_weekly = 100;
    for (int pass = 0; pass < 2; pass++) {
        bool leave = pass == 1;
        res->economy.unpaid_troops_leave = leave;
        memset(g->army, 0, sizeof g->army);
        snprintf(g->army[0].id, sizeof g->army[0].id, "%s", dear->id);
        g->army[0].count = 1000;                       // far past the wallet
        snprintf(g->army[1].id, sizeof g->army[1].id, "%s", cheap->id);
        g->army[1].count = 10;
        int cheap_cost = GameStackWeeklyUpkeep(cheap->id, 10);
        g->stats.gold = 500;
        ASSERT(cheap_cost <= 600);
        int predicted = GameWeeklyNetGold(g);
        int before = g->stats.gold, paid = 0;
        GameSpendWeek(g, &paid);
        if (leave) {
            ASSERT_STR_EQ(cheap->id, g->army[0].id);  // the dear stack went; the cheap closed up
            ASSERT_EQ(10, g->army[0].count);
            ASSERT_EQ('\0', g->army[1].id[0]);
            ASSERT_STR_EQ(dear->id, g->stats.last_week_left[0].id);
            ASSERT_EQ(1000, g->stats.last_week_left[0].count);
            ASSERT_EQ(cheap_cost, g->stats.last_week_army);
            ASSERT_EQ(before + predicted, g->stats.gold);
        } else {
            ASSERT_STR_EQ(dear->id, g->army[0].id);   // everyone stays, the floor takes it
            ASSERT_EQ(0, g->stats.gold);
            ASSERT_EQ('\0', g->stats.last_week_left[0].id[0]);
        }
        ASSERT_EQ(g->stats.last_week_on_hand + g->stats.last_commission
                  - g->stats.last_week_boat - g->stats.last_week_army, g->stats.gold);
    }
    fx_free_game_full(res, g, m, f);
    PASS();
}

// The home castle offers a troop only at six times its hit points of
// leadership, and the engine holds every caller to it (autoplay included).
TEST the_castle_offers_a_troop_at_six_times_its_hit_points(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    const TroopDef *t = NULL;
    for (int i = 0; i < res->troops_count && !t; i++)
        if (strcmp(res->troops[i].dwelling, "castle") == 0 &&
            res->troops[i].hit_points > 1) t = &res->troops[i];
    ASSERT(t);
    strcpy(g->position.home_castle, "home");
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) { g->army[i].id[0] = '\0'; g->army[i].count = 0; }
    g->stats.gold = 1000000;
    g->stats.leadership_current = t->hit_points * 6 - 1;
    ASSERT_FALSE(GameCastleOffersTroop(g, t));
    ASSERT_EQ(-1, GameRecruitLocationCap(g, t->id));
    ASSERT_EQ(4, GameBuyTroop(g, t->id, 1));
    g->stats.leadership_current = t->hit_points * 6;
    ASSERT(GameCastleOffersTroop(g, t));
    ASSERT(GameRecruitLocationCap(g, t->id) > 0);
    ASSERT_EQ(0, GameBuyTroop(g, t->id, 1));
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST army_view_morale_follows_the_chart(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) { g->army[i].id[0] = '\0'; g->army[i].count = 0; }
    ASSERT_EQ('N', GameArmySlotMorale(g, 0));             // empty
    const TroopDef *a = &res->troops[0];
    strcpy(g->army[0].id, a->id); g->army[0].count = 5;
    ASSERT_EQ('H', GameArmySlotMorale(g, 0));             // alone
    for (int j = 1; j < res->troops_count; j++) {
        const TroopDef *b = &res->troops[j];
        strcpy(g->army[1].id, b->id); g->army[1].count = 5;
        char r = morale_result(a->morale_group, b->morale_group);
        ASSERT_EQ(r == 'L' ? 'L' : (r == 'H' ? 'H' : 'N'), GameArmySlotMorale(g, 0));
    }
    ASSERT_EQ('N', GameArmySlotMorale(g, -1));
    ASSERT_EQ('N', GameArmySlotMorale(g, GAME_ARMY_SLOTS));
    fx_free_game_full(res, g, m, f);
    PASS();
}

// The pack's boat artifact ("cheaper_boat_rental") is the power the rent
// reads: found, it brings the boat down to the cheap rent.
TEST the_boat_artifact_cheapens_the_boat(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    int k = -1;
    for (int i = 0; i < g->artifacts.count; i++) {
        const ArtifactDef *a = artifact_by_index(i);
        if (a && a->power == ARTIFACT_POWER_CHEAPER_BOATS) k = i;
        g->artifacts.found[i] = false;
    }
    ASSERT(k >= 0);
    ASSERT_EQ(res->economy.boat_cost_normal, GameBoatCost(g));
    g->artifacts.found[k] = true;
    ASSERT_EQ(res->economy.boat_cost_cheap, GameBoatCost(g));
    fx_free_game_full(res, g, m, f);
    PASS();
}

// A short purse at the week end pays what it holds for the boat, and the boat
// stays: a hero sailing it is never left at sea without one (#199).
TEST a_broke_hero_keeps_the_boat(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) { g->army[i].id[0] = '\0'; g->army[i].count = 0; }
    g->boat.has_boat = true;
    g->boat.x = 5; g->boat.y = 6;
    strcpy(g->boat.zone, g->position.zone);
    g->travel_mode = TRAVEL_BOAT;
    g->stats.gold = 0;
    g->stats.commission_weekly = 10;              // less than the fare
    int paid = 0;
    GameSpendWeek(g, &paid);
    ASSERT(g->boat.has_boat);
    ASSERT_EQ(TRAVEL_BOAT, g->travel_mode);
    ASSERT_EQ(5, g->boat.x);
    ASSERT_EQ(6, g->boat.y);
    ASSERT_EQ(10, g->stats.last_week_boat);       // what the purse held
    ASSERT_EQ(0, g->stats.gold);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(e2e_economy_suite) {
    RUN_TEST(rent_boat_deducts_gold_and_places);
    RUN_TEST(rent_boat_refuses_when_gold_equals_cost);
    RUN_TEST(cancel_boat_clears_when_ashore);
    RUN_TEST(cancel_boat_refused_at_sea);
    RUN_TEST(buy_troop_drains_gold);
    RUN_TEST(buy_troop_refuses_when_broke);
    RUN_TEST(buy_troop_refuses_over_leadership);
    RUN_TEST(buy_troop_zero_or_negative_count_refused);
    RUN_TEST(buy_troop_unknown_id_refused);
    RUN_TEST(buy_troop_stacks_on_matching_slot);
    RUN_TEST(max_recruitable_ignores_other_troops);
    RUN_TEST(max_recruitable_scales_with_leadership);
    RUN_TEST(compact_army_collapses_gaps);
    RUN_TEST(compact_army_zero_count_treated_as_empty);
    RUN_TEST(compact_army_already_dense_unchanged);
    RUN_TEST(empty_player_castle_is_retaken_at_week_end);
    RUN_TEST(week_end_records_what_it_charged);
    RUN_TEST(unpaid_troops_leave_where_the_pack_says);
    RUN_TEST(the_castle_offers_a_troop_at_six_times_its_hit_points);
    RUN_TEST(army_view_morale_follows_the_chart);
    RUN_TEST(the_boat_artifact_cheapens_the_boat);
    RUN_TEST(a_broke_hero_keeps_the_boat);
}
