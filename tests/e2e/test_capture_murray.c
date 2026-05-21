// Long-test: capture Murray the Miser from a freshly-spawned game on a
// fixed seed. Exercises the full early-game capture path end-to-end
// using engine APIs only (no UI). On seed=1 the contract cycle starts
// with Murray, his castle ("azram") sits at gate (30,37) in continentia,
// and his garrison totals 215 HP. The knight's 60 HP starting army is
// not enough; the test recruits at the home castle's roster
// (king_maximus) to bring army HP to ~300 before sieging.
//
// The siege itself runs through combat_run_headless, which drives both
// sides through combat_ai_action — the same code path RunCombat uses on
// auto-combat. Headless avoids the rendered loop and the shell's
// prompt machinery, so the test stays deterministic and fast.

#include "greatest.h"
#include "game.h"
#include "combat.h"
#include "tables.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

// Recipe that beats Murray's garrison on seed=1 within knight leadership
// caps. Verified via standalone sim. Total cost: 6500g (starting gold
// 7500g leaves 1000g). Total HP added: +240 on top of the 60 HP starter
// = 300 HP fighting a 215 HP garrison.
static const struct {
    const char *troop;
    int         count;
} RECRUIT_RECIPE[] = {
    { "pikemen", 10 },   // 3000g, +100 HP (best per-leader value)
    { "militia", 30 },   // 1500g, +60 HP (rounds out empty slot)
    { "archers",  8 },   // 2000g, +80 HP (ranged kit vs melee garrison)
};

static int army_total_hp(const Game *g) {
    int hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0]) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        if (t) hp += t->hit_points * g->army[i].count;
    }
    return hp;
}

// Locate Murray's castle on this seed. Murray's villain_id is constant
// ("murray"); which CASTLE he holds is randomized by salt_villains at
// GameInit. On seed=1 it happens to be "azram".
static CastleRecord *find_murray_castle(Game *g) {
    for (int i = 0; i < GAME_CASTLES; i++) {
        if (g->castles[i].id[0] &&
            strcmp(g->castles[i].villain_id, "murray") == 0) {
            return &g->castles[i];
        }
    }
    return NULL;
}

TEST capture_murray_seed1_full_flow(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, "continentia", 1));

    // ---- 1. Verify starting state matches our assumptions ----
    ASSERT_STR_EQ("knight",      g->character.cls.id);
    ASSERT_STR_EQ("continentia", g->position.zone);
    ASSERT_EQ(11, g->position.x);
    ASSERT_EQ(58, g->position.y);
    ASSERT_EQ(7500, g->stats.gold);
    ASSERT_EQ(100,  g->stats.leadership_current);
    ASSERT_EQ(60,   army_total_hp(g));   // militia x20 + archers x2

    // ---- 2. Take the next contract — on seed=1 cycle this is Murray ----
    const char *contract = GameTakeNextContract(g);
    ASSERT(contract != NULL);
    ASSERT_STR_EQ("murray", contract);
    ASSERT_STR_EQ("murray", g->contract.active_id);

    const VillainDef *murray = villain_by_id("murray");
    ASSERT(murray);
    ASSERT_FALSE(g->contract.villains_caught[murray->index]);

    // ---- 3. Recruit at king_maximus (home castle) ----
    // king_maximus is the audience castle but its "A) Recruit Soldiers"
    // menu invokes the same recruit_soldiers screen as any castle —
    // selling all dwelling="castle" troops at their stock cost. We bypass
    // the screen and call GameBuyTroop directly, the same engine API the
    // screen ultimately invokes.
    int gold_before_recruit = g->stats.gold;
    for (size_t i = 0; i < sizeof RECRUIT_RECIPE / sizeof RECRUIT_RECIPE[0]; i++) {
        int rc = GameBuyTroop(g, RECRUIT_RECIPE[i].troop, RECRUIT_RECIPE[i].count);
        ASSERT_EQm("recruit step failed (leadership cap or gold)", 0, rc);
    }
    ASSERT_EQ(6500, gold_before_recruit - g->stats.gold);
    ASSERT_EQ(300,  army_total_hp(g));

    // ---- 4. Locate Murray's castle and mark it known ----
    // GameCastFindVillain marks the active contract's castle as
    // cr->known = true (the spell's only effect). Required prerequisite:
    // we own at least one charge of find_villain. Knights start with no
    // magic and zero charges, so we inject one for the test — a human
    // player gets it by visiting the alcove (5000g) then buying the
    // spell at a town. Same end state, fewer ticks.
    int fv_idx = spell_index_by_id("find_villain");
    ASSERT(fv_idx >= 0);
    g->stats.knows_magic    = true;
    g->spells.counts[fv_idx] = 1;
    GameCastFindVillain(g);
    ASSERT_EQ(0, g->spells.counts[fv_idx]); // charge consumed

    CastleRecord *murray_cr = find_murray_castle(g);
    ASSERT(murray_cr);
    ASSERT(murray_cr->known);
    ASSERT_EQ(CASTLE_OWNER_VILLAIN, murray_cr->owner_kind);

    // ---- 5. Siege headless ----
    // RunCombat (the rendered loop) hands the garrison to combat as a
    // CombatTarget; combat_run_headless does the same, just without
    // the per-frame renderer. On WIN it credits spoils to g->stats.gold
    // and mutates g->army to reflect surviving counts.
    CombatTarget tgt = {
        .name           = "Murray",
        .garrison       = murray_cr->garrison,
        .garrison_slots = GAME_ARMY_SLOTS,
    };
    int gold_pre_siege = g->stats.gold;
    CombatResult outcome = combat_run_headless(g, COMBAT_MODE_CASTLE, &tgt, 64);
    ASSERT_EQ(COMBAT_RESULT_WIN, outcome);

    // Spoils went into our purse (combat_prepare_castle credits
    // spoils_factor * 5 * count per garrison stack on WIN).
    ASSERT(g->stats.gold > gold_pre_siege);

    // At least one stack survived.
    ASSERT(army_total_hp(g) > 0);

    // ---- 6. Transition castle to PLAYER ownership ----
    // The rendered loop's FLOW_SIEGE_VILLAIN handler does this after
    // RunCombat returns WIN. Replicating it in the test:
    murray_cr->owner_kind = CASTLE_OWNER_PLAYER;
    murray_cr->visited    = true;
    murray_cr->villain_id[0] = '\0';
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        murray_cr->garrison[s].id[0] = '\0';
        murray_cr->garrison[s].count = 0;
    }

    // ---- 7. Fulfill the contract — gold reward + caught flag ----
    int gold_pre_contract = g->stats.gold;
    bool ok = GameFulfillContract(g, "murray");
    ASSERT(ok);
    ASSERT_EQ(gold_pre_contract + murray->reward, g->stats.gold);
    ASSERT(g->contract.villains_caught[murray->index]);
    ASSERT_EQ('\0', g->contract.active_id[0]);  // active contract cleared

    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(e2e_capture_murray_suite) {
    RUN_TEST(capture_murray_seed1_full_flow);
}
