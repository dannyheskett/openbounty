// autoplay/exec_replay.c
//
// Replay appliers (AP-023): REC_ACTION -> autoplay_apply_rec_action,
// combat-bearing REC_ANSWER -> autoplay_apply_recorded_combat (the fight is
// re-resolved by the same pure function of (seed, identity, mode), REQ-395).
// These are reached only after plan.c has checked the prim's world
// fingerprint against the live state, so an applier never sees a diverged
// world; the few actions whose engine result is itself identifying (the
// contract take) additionally verify that result matches what was recorded.

#include "autoplay.h"

#include <stdio.h>
#include <string.h>

#include "exec.h"
#include "flow_answer.h"
#include "pending.h"
#include "player_io.h"
#include "spells_adventure.h"
#include "tables.h"

// Re-resolve the recorded fight on the live world with the same policy; the
// deterministic combat seed makes the outcome recur exactly (AP-020).
int autoplay_apply_recorded_combat(struct ExecCtx *ctx, const RecPrim *p) {
    (void)p;
    Game *g = ctx->g;
    CombatMode mode;
    CombatTarget tgt;
    memset(&tgt, 0, sizeof tgt);
    if (pending_flow == FLOW_SIEGE_MONSTER ||
        pending_flow == FLOW_SIEGE_VILLAIN) {
        mode = COMBAT_MODE_CASTLE;
        CastleRecord *cr = GameFindCastle(g, pending_castle_id);
        const ResCastle *rc = resources_castle_by_id(g->res, pending_castle_id);
        const VillainDef *v = (cr && cr->villain_id[0])
                                  ? villain_by_id(cr->villain_id) : NULL;
        tgt.name = v && v->name[0] ? v->name
                   : (rc && rc->name[0] ? rc->name : pending_castle_id);
        tgt.seed_key = pending_castle_id;
        if (cr) {
            tgt.garrison = cr->garrison;
            tgt.garrison_slots = GAME_ARMY_SLOTS;
        }
    } else if (pending_flow == FLOW_ATTACK_FOE) {
        mode = COMBAT_MODE_FOE;
        FoeState *foe = pending_foe_id[0] ? GameFindFoe(g, pending_foe_id)
                                          : NULL;
        tgt.name = "Hostile band";
        tgt.seed_key = pending_foe_id;
        if (foe) {
            tgt.garrison = foe->garrison;
            tgt.garrison_slots = GAME_ARMY_SLOTS;
        }
    } else {
        return (int)PLAYER_IO_COMBAT_NOT_RUN;
    }
    CombatResult r = combat_run_headless_ex(g, mode, &tgt, COMBAT_MAX_ROUNDS,
                                            autoplay_combat_policy, NULL);
    return (int)(r == COMBAT_RESULT_WIN ? PLAYER_IO_COMBAT_WON
                                        : PLAYER_IO_COMBAT_LOST);
}

bool autoplay_apply_rec_action(struct ExecCtx *ctx, const RecPrim *p) {
    Game *g = ctx->g;
    switch ((RecActionKind)p->action) {
    case RA_GARRISON:
        return GameGarrisonTroop(g, p->id, p->a) == 0;
    case RA_UNGARRISON:
        return GameUngarrisonTroop(g, p->id, p->a) == 0;
    case RA_DISMISS: {
        pending_flow = FLOW_DISMISS_ARMY;
        player_io_raise_decision(g, FLOW_DISMISS_ARMY, REQ_PROMPT_NUMERIC,
                                 NULL, NULL);
        FlowAnswer ans = { (PromptAnswer)(FLOW_ANS_1 + p->a), 0 };
        PlayerIoPresentation pres;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, ans,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    case RA_DISMISS_LAST: {
        // The maroon escape's confirm chain, replayed end to end.
        pending_flow = FLOW_DISMISS_ARMY;
        player_io_raise_decision(g, FLOW_DISMISS_ARMY, REQ_PROMPT_NUMERIC,
                                 NULL, NULL);
        FlowAnswer pick = { (PromptAnswer)(FLOW_ANS_1 + p->a), 0 };
        PlayerIoPresentation pres;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, pick,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres);
        if (!pres.chain_dismiss_last) return false;
        pending_flow = FLOW_DISMISS_LAST;
        player_io_raise_decision(g, FLOW_DISMISS_LAST, REQ_PROMPT_YES_NO,
                                 NULL, NULL);
        FlowAnswer yes = { FLOW_ANS_YES, 0 };
        PlayerIoPresentation pres2;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres2);
        if (!pres2.temp_death) return false;
        exec_temp_death(ctx);
        return true;
    }
    case RA_BUY_TROOP:
        return GameBuyTroop(g, p->id, p->a) == 0;
    case RA_BUY_SPELL:
        return GameBuySpell(g, p->id) == SPELL_BUY_OK;
    case RA_BUY_SIEGE:
        return GameBuySiege(g) == SIEGE_BUY_OK;
    case RA_RENT_BOAT:
        return GameRentBoat(g, p->a, p->b, p->id) == BOAT_RENT_OK;
    case RA_CANCEL_BOAT:
        return GameCancelBoat(g) == BOAT_CANCEL_OK;
    case RA_TAKE_CONTRACT: {
        const char *got = GameTakeNextContract(g);
        // An empty recorded id is a NULL take (emptied cycle slot): the
        // recorded mutation is the rotation advance itself, which the call
        // above just re-applied. A non-empty id must match what this world
        // handed out, or the state diverged.
        if (!p->id[0]) return got == NULL;
        return got != NULL && strcmp(got, p->id) == 0;
    }
    case RA_CAST_ADV_SPELL: {
        // The recorded id is the pack's spell id; dispatch on the spell's
        // engine effect, never on the id string, so a pack's own naming
        // reaches the same engine behavior the recorder invoked.
        int idx = spell_index_by_id(p->id);
        if (idx < 0) return false;
        switch (spell_adventure_effect(idx)) {
        case ADV_EFFECT_BRIDGE:
            if (g->spells.counts[idx] <= 0) return false;
            if (try_build_bridge(g, ctx->map, p->a, p->b) <= 0) return false;
            g->spells.counts[idx]--;
            return true;
        case ADV_EFFECT_RAISE_CONTROL:
            GameCastRaiseControl(g);
            return true;
        default:
            return GameApplyAdventureSpellEffect(g, idx);
        }
    }
    case RA_GATE_TOWN:
    case RA_GATE_CASTLE: {
        GateDestination dest;
        memset(&dest, 0, sizeof dest);
        {
            size_t n = 0;
            while (n + 1 < sizeof dest.zone && p->id[n]) {
                dest.zone[n] = p->id[n]; n++;
            }
            dest.zone[n] = '\0';
        }
        dest.x = p->a;
        dest.y = p->b;
        return GameGateTeleport(g, ctx->map, ctx->fog, &dest,
                                p->action == RA_GATE_TOWN ? "town_gate"
                                                          : "castle_gate");
    }
    case RA_TRAVEL_ZONE: {
        int cur = zone_index_of(ctx->res, g->position.zone);
        int pick = -1, count = 0;
        if (cur < 0) return false;
        for (int n = 0; n < ctx->res->zones[cur].neighbor_count && count < 5;
             n++) {
            snprintf(pending_nav_zones[count], sizeof pending_nav_zones[count],
                     "%s", ctx->res->zones[cur].neighbors[n]);
            if (strcmp(ctx->res->zones[cur].neighbors[n], p->id) == 0)
                pick = count;
            count++;
        }
        if (pick < 0) return false;
        pending_nav_count = count;
        pending_flow = FLOW_NAVIGATE;
        player_io_raise_decision(g, FLOW_NAVIGATE, REQ_PROMPT_NUMERIC,
                                 "", "");
        FlowAnswer ans = { (PromptAnswer)(FLOW_ANS_1 + pick), 0 };
        PlayerIoPresentation pres;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, ans,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    case RA_SEARCH: {
        pending_flow = FLOW_SEARCH;
        player_io_raise_decision(g, FLOW_SEARCH, REQ_PROMPT_YES_NO,
                                 NULL, NULL);
        FlowAnswer yes = { FLOW_ANS_YES, 0 };
        PlayerIoPresentation pres;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    case RA_SPEND_WEEK:
        GameSpendDays(g, p->a > 0 ? p->a : 1, NULL);
        return true;
    case RA_MOUNT_FLY:
        return GameMountFly(g);
    case RA_LAND:
        return GameLandHere(g, ctx->map);
    case RA_DISCARD_SPELL: {
        pending_discard_spell_idx = p->a;
        pending_flow = FLOW_DISCARD_SPELL;
        player_io_raise_decision(g, FLOW_DISCARD_SPELL, REQ_PROMPT_YES_NO,
                                 NULL, NULL);
        FlowAnswer yes = { FLOW_ANS_YES, 0 };
        PlayerIoPresentation pres;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    case RA_NONE:
    default:
        return false;
    }
}

