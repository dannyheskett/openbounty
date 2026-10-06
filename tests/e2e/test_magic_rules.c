// The pack's magic rules (#157): a limit for each spell (magic.max_per_spell),
// one spell learned at a temple renewed each week (magic.weekly_renewal), and
// no discard offer where each spell has its own limit. King's Bounty sets
// neither, so each test runs the rule off and on over the same game.

#include "greatest.h"
#include "fixtures.h"
#include "game.h"
#include "tables.h"
#include "resources.h"
#include "savegame.h"
#include "state_serialize.h"
#include "spells_adventure.h"
#include "pending.h"
#include "player_io.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAGIC_SAVE "build/openbounty_magic_rules.dat"

// The hero stands in the first town, which sells spell `idx`.
static void at_temple(Game *g, int idx) {
    const SpellDef *sp = spell_by_index(idx);
    snprintf(g->towns[0].spell_for_sale, sizeof g->towns[0].spell_for_sale, "%.23s", sp->id);
    snprintf(g->position.in_town, sizeof g->position.in_town, "%s", g->towns[0].id);
    g->stats.gold = 1000000;
}

static void clear_book(Game *g) {
    for (int i = 0; i < g->spells.count; i++) {
        g->spells.counts[i] = 0;
        g->spells.learned[i] = false;
    }
}

TEST the_limit_is_per_spell_where_the_pack_says(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    ASSERT(g->town_count > 0 && g->spells.count >= 2);
    res->economy.rites_per_zone = false;
    for (int pass = 0; pass < 2; pass++) {
        bool per_spell = pass == 1;
        res->economy.spell_limit_per_spell = per_spell;
        clear_book(g);
        g->stats.max_spells = 3;
        g->spells.counts[0] = 3;                       // spell A full
        at_temple(g, 1);                               // the town sells B
        ASSERT_EQ(per_spell ? SPELL_BUY_OK : SPELL_BUY_AT_CAP, GameBuySpell(g, g->towns[0].id));
        ASSERT_EQ(per_spell ? 1 : 0, g->spells.counts[1]);
        at_temple(g, 0);                               // a fourth A: refused either way
        ASSERT_EQ(SPELL_BUY_AT_CAP, GameBuySpell(g, g->towns[0].id));
        ASSERT_EQ(3, g->spells.counts[0]);
    }
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST spell_room_counts_one_spell_or_all(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    clear_book(g);
    g->stats.max_spells = 5;
    g->spells.counts[0] = 2;
    g->spells.counts[1] = 4;
    res->economy.spell_limit_per_spell = false;
    ASSERT_EQ(0, GameSpellRoom(g, 0));                 // 6 held of 5: never below 0
    ASSERT_EQ(0, GameSpellRoom(g, 2));
    res->economy.spell_limit_per_spell = true;
    ASSERT_EQ(3, GameSpellRoom(g, 0));
    ASSERT_EQ(1, GameSpellRoom(g, 1));
    ASSERT_EQ(5, GameSpellRoom(g, 2));
    g->spells.counts[2] = 9;
    ASSERT_EQ(0, GameSpellRoom(g, 2));
    ASSERT_EQ(0, GameSpellRoom(g, -1));
    ASSERT_EQ(0, GameSpellRoom(g, g->spells.count));
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST only_a_temple_teaches_a_spell(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    res->economy.rites_per_zone = false;
    res->economy.spell_limit_per_spell = true;
    clear_book(g);
    g->stats.max_spells = 4;
    at_temple(g, 2);
    ASSERT_EQ(SPELL_BUY_OK, GameBuySpell(g, g->towns[0].id));
    ASSERT(g->spells.learned[2]);

    // A chest that can only hold a new spell: charges, never learning, never
    // past the limit and never none; a spell already at its limit makes it a
    // gold chest.
    ResChest *ch = &res->economy.chest;
    ch->chance_gold[0] = ch->chance_commission[0] = 0;
    ch->chance_spell_power[0] = ch->chance_max_spells[0] = 0;
    ch->chance_new_spell[0] = 101;
    char body[256];
    for (int x = 0; x < 40; x++) {
        bool before[64] = { 0 };
        for (int i = 0; i < g->spells.count && i < 64; i++) before[i] = g->spells.learned[i];
        int held = GameKnownSpells(g);
        ChestPending pend;
        ChestOutcome o = GameRollChest(g, 0, x, -1, body, sizeof body, &pend);
        if (o == CHEST_OUTCOME_GOLD) {
            ASSERT(pend.pending_gold > 0);
            ASSERT_EQ(held, GameKnownSpells(g));
        } else {
            ASSERT_EQ(CHEST_OUTCOME_NEW_SPELL, o);
            ASSERT(GameKnownSpells(g) > held);
        }
        for (int i = 0; i < g->spells.count && i < 64; i++) {
            ASSERT_EQ(before[i], g->spells.learned[i]);
            ASSERT(g->spells.counts[i] <= g->stats.max_spells);
        }
    }
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST a_full_spell_makes_a_gold_chest(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    res->economy.spell_limit_per_spell = true;
    clear_book(g);
    g->stats.max_spells = 3;
    for (int i = 0; i < g->spells.count; i++) g->spells.counts[i] = 3;
    ResChest *ch = &res->economy.chest;
    ch->chance_gold[0] = ch->chance_commission[0] = 0;
    ch->chance_spell_power[0] = ch->chance_max_spells[0] = 0;
    ch->chance_new_spell[0] = 101;
    char body[256];
    for (int x = 0; x < 20; x++) {
        ChestPending pend;
        ASSERT_EQ(CHEST_OUTCOME_GOLD,
                  GameRollChest(g, 0, x, -1, body, sizeof body, &pend));
        ASSERT(pend.pending_gold > 0);
        for (int i = 0; i < g->spells.count; i++) ASSERT_EQ(3, g->spells.counts[i]);
    }
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST the_weekly_pick_is_repeatable_and_learned_only(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    clear_book(g);
    res->economy.spell_weekly_renewal = true;
    ASSERT_EQ(-1, GamePickRenewedSpell(g, 3));         // nothing learned
    g->spells.learned[1] = g->spells.learned[4] = g->spells.learned[9] = true;
    res->economy.spell_weekly_renewal = false;
    ASSERT_EQ(-1, GamePickRenewedSpell(g, 3));         // the rule off
    res->economy.spell_weekly_renewal = true;
    int seen[64] = { 0 };
    for (int w = 0; w < 200; w++) {
        int k = GamePickRenewedSpell(g, w);
        ASSERT(k >= 0 && k < g->spells.count);
        ASSERT(g->spells.learned[k]);
        ASSERT_EQ(k, GamePickRenewedSpell(g, w));      // the same week, the same spell
        seen[k]++;
    }
    ASSERT(seen[1] > 0 && seen[4] > 0 && seen[9] > 0);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST the_week_end_fills_the_picked_spell_to_the_limit(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->boat.has_boat = false;
    g->stats.gold = 100000;
    for (int pass = 0; pass < 2; pass++) {
        bool renew = pass == 1;
        res->economy.spell_weekly_renewal = renew;
        clear_book(g);
        g->stats.max_spells = 6;
        g->spells.learned[5] = true;                   // one learned spell, spent
        g->spells.counts[3] = 2;                       // a chest's, not learned
        int paid = 0;
        GameSpendWeek(g, &paid);
        ASSERT_EQ(renew ? 6 : 0, g->spells.counts[5]);
        ASSERT_EQ(renew ? 5 : -1, g->stats.last_renewed_spell);
        ASSERT_EQ(2, g->spells.counts[3]);
    }
    // Charges already past the limit are kept.
    clear_book(g);
    g->stats.max_spells = 6;
    g->spells.learned[5] = true;
    g->spells.counts[5] = 9;
    int paid = 0;
    GameSpendWeek(g, &paid);
    ASSERT_EQ(9, g->spells.counts[5]);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST learned_spells_survive_a_save(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    clear_book(g);
    g->spells.learned[2] = g->spells.learned[7] = true;

    // With the weekly renewal off (King's Bounty) as with it on: the learned
    // spells are part of the Game, so the save carries them either way.
    for (int renewal = 0; renewal < 2; renewal++) {
        res->economy.spell_weekly_renewal = renewal != 0;
        g->spells.learned[2] = g->spells.learned[7] = true;
        ASSERT_EQ(SAVE_OK, SaveGameWrite(MAGIC_SAVE, g, m, f));
        clear_book(g);
        ASSERT_EQ(SAVE_OK, SaveGameRead(MAGIC_SAVE, g, m, f));
        for (int i = 0; i < g->spells.count; i++)
            ASSERT_EQ(i == 2 || i == 7, g->spells.learned[i]);
    }
    unlink(MAGIC_SAVE);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST no_discard_offer_with_the_per_spell_limit(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    int combat = -1;
    for (int i = 0; i < spells_count() && combat < 0; i++)
        if (spell_by_index(i)->kind == SPELL_KIND_COMBAT) combat = i;
    ASSERT(combat >= 0);
    for (int pass = 0; pass < 2; pass++) {
        bool per_spell = pass == 1;
        res->economy.spell_limit_per_spell = per_spell;
        player_io_reset(g);
        pending_flow = FLOW_NONE;
        g->spells.counts[combat] = 2;
        dispatch_adventure_spell(g, combat);
        ASSERT_FALSE(player_io_idle(g));               // something is said either way
        ASSERT_EQ(per_spell ? FLOW_NONE : FLOW_DISCARD_SPELL, pending_flow);
        ASSERT_EQ(2, g->spells.counts[combat]);
    }
    player_io_reset(g);
    pending_flow = FLOW_NONE;
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(e2e_magic_rules_suite) {
    RUN_TEST(the_limit_is_per_spell_where_the_pack_says);
    RUN_TEST(spell_room_counts_one_spell_or_all);
    RUN_TEST(only_a_temple_teaches_a_spell);
    RUN_TEST(a_full_spell_makes_a_gold_chest);
    RUN_TEST(the_weekly_pick_is_repeatable_and_learned_only);
    RUN_TEST(the_week_end_fills_the_picked_spell_to_the_limit);
    RUN_TEST(learned_spells_survive_a_save);
    RUN_TEST(no_discard_offer_with_the_per_spell_limit);
}
