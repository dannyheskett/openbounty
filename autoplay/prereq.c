// autoplay/prereq.c -- the engine-enforced hard gates (AP-060).

#include "prereq.h"

#include <stdio.h>
#include <string.h>

#include "diag.h"

// The BUY-SIEGE candidate (prereq_make_buy_siege): resolved to the nearest
// town at execution (the executor walks the whole town set).
static void prereq_make_buy_siege(PlanStep *out) {
    memset(out, 0, sizeof *out);
    out->kind = STEP_SIEGE_WEAPONS;
    out->zone_index = 0;
    out->x = out->y = -1;
    snprintf(out->handle, sizeof out->handle, "siege_weapons");
    snprintf(out->label, sizeof out->label, "buy:siege-weapons");
}

int prereq_unmet(const ExecCtx *ctx, const PlanStep *step,
                 PlanStep *out, int cap) {
    int n = 0;
    if ((step->kind == STEP_MONSTER_CASTLE || step->kind == STEP_VILLAIN) &&
        !ctx->g->stats.siege_weapons && n < cap) {
        prereq_make_buy_siege(&out[n++]);
    }
    return n;
}

// ----- Static prerequisite gates (AP-060/AP-061) -----------------------------
// One O(1) read of the four pack/engine structures that gate an objective:
// the navmap zone chain, the contract window, siege weapons, and the alcove's
// magic price. Deliberately NO resource reasoning beyond that single magic
// gold gate -- army/leadership affordability stays the executor's dynamic
// business (the GOLD/STOCK/NO_WINNING_ARMY causes), not a static prereq.
unsigned prereq_gated(const ExecCtx *ctx, const PlanStep *step,
                      int open_others) {
    const Game *g = ctx->g;
    unsigned m = 0;

    // Zone reach: the objective's continent must be discovered. Home starts
    // discovered; every other continent chains off the prior one's navmap
    // (engine/step.c) and no money buys the trip until the map is found.
    if (step->zone_index >= 0 && step->zone_index < GAME_CONTINENTS &&
        !g->world.zones_discovered[step->zone_index])
        m |= PREREQ_ZONE;

    switch (step->kind) {
    case STEP_VILLAIN:
        // Contract window: the villain's contract must be obtainable now -- the
        // engine caps it to the cycle_length lowest-index un-captured villains,
        // so villain i waits on earlier captures, not on gold.
        if (!GameVillainContractObtainable(g, step->handle))
            m |= PREREQ_CONTRACT;
        /* fall through -- a villain lives in a castle, so it sieges too. */
        /* FALLTHRU */
    case STEP_MONSTER_CASTLE:
        if (!g->stats.siege_weapons)
            m |= PREREQ_SIEGE;
        break;
    case STEP_ALCOVE:
        // The one permitted resource gate: magic is bought here for the alcove
        // price, so the step is dead until the wallet clears it.
        if (!g->stats.knows_magic &&
            g->stats.gold < ctx->res->economy.alcove_cost)
            m |= PREREQ_MAGIC;
        break;
    case STEP_SCEPTER:
        // Finale (AP-052, re-homed from the planner's select loop): the dig
        // ends the game, so it waits until every other objective is done -- the
        // all-clear guarantee, now expressed as a prerequisite.
        if (open_others > 0)
            m |= PREREQ_FINALE;
        break;
    default:
        break;
    }
    return m;
}

bool prereq_actionable(const ExecCtx *ctx, const PlanStep *step,
                       int open_others) {
    return (prereq_gated(ctx, step, open_others) & PREREQ_HARD) == 0;
}

bool prereq_magic_enabled(const Game *g) {
    return g->stats.knows_magic;
}

void prereq_dump(const ExecCtx *ctx, const PlanStepSet *set) {
    if (!ob_diag_verbose()) return;
    int open_others = 0;
    for (int i = 0; i < set->count; i++)
        if (set->steps[i].kind != STEP_SCEPTER &&
            !planstep_is_done(ctx->g, &set->steps[i]))
            open_others++;
    int act = 0, gz = 0, gc = 0, gs = 0, gm = 0;
    for (int i = 0; i < set->count; i++) {
        unsigned mm = prereq_gated(ctx, &set->steps[i], open_others);
        if (mm & PREREQ_ZONE) gz++;
        if (mm & PREREQ_CONTRACT) gc++;
        if (mm & PREREQ_SIEGE) gs++;
        if (mm & PREREQ_MAGIC) gm++;
        if ((mm & PREREQ_HARD) == 0) act++;
    }
    printf("[PREREQ] %d objectives (initial): hard-actionable=%d | gated "
           "zone=%d contract=%d | soft siege=%d magic=%d | magic_enabled=%d\n",
           set->count, act, gz, gc, gs, gm, (int)prereq_magic_enabled(ctx->g));
    printf("[PREREQ] villain window:");
    for (int i = 0; i < set->count; i++) {
        const PlanStep *s = &set->steps[i];
        if (s->kind != STEP_VILLAIN) continue;
        unsigned mm = prereq_gated(ctx, s, open_others);
        printf(" %s%s", s->handle,
               (mm & PREREQ_CONTRACT) ? "-x" :
               (mm & PREREQ_ZONE) ? "-z" : "-ok");
    }
    printf("\n");
}
