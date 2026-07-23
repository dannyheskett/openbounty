// engine/flow_resolve.c
//
// Engine-side apply-cores for pending-flow resolution. Each
// function is the state-mutation half lifted out of src/shell_promptdispatch.c
// verbatim, with the shell-only pieces removed:
//   - combat is run by the host adapter, which passes in the CombatResult;
//   - perform_temp_death / RunCombat / views_dismiss / run_end_cartoon stay
//     in the adapter (this layer signals when they're needed via return value);
//   - prompt reads become the FlowAnswer parameter.
//
// open_dialog is a host callback (ui_host.h) -- the engine is allowed to call
// it -- so result/capture dialog text is composed here, keeping shell and
// autoplay verbatim-identical.

#include "flow_resolve.h"

#include <stdio.h>
#include <string.h>

#include "flows.h"      // schedule_week_end, show_win_game, show_lose_game
#include "tables.h"     // villain_by_id, VillainDef
#include "ui_host.h"    // open_dialog

// open_dialog_flags with MSG_FLAG_PADDED (3 leading newlines) is a shell-only
// helper; the one place that used it (FLOW_RECRUIT no-gold) is reproduced here
// by pre-padding and calling the host open_dialog. Keep the pad in sync with
// src/ui.c's MSG_FLAG_PADDED behavior.
static void open_dialog_padded(Game *g, const char *header, const char *body) {
    char padded[700];
    snprintf(padded, sizeof padded, "\n\n\n%s", body ? body : "");
    player_io_message(g, header, padded);
}

bool flow_apply_search(Game *g, const Resources *res, FlowAnswer ans,
                       bool *out_won, bool *out_game_over,
                       int *out_week_commission) {
    if (out_won)             *out_won = false;
    if (out_game_over)       *out_game_over = false;
    if (out_week_commission) *out_week_commission = 0;
    if (ans.kind != FLOW_ANS_YES) return false;

    bool on_scepter =
        (strcmp(g->scepter.zone, g->position.zone) == 0 &&
         g->scepter.x == g->position.x &&
         g->scepter.y == g->position.y);
    if (on_scepter) {
        // The win cartoon is render-side (adapter runs it before the win
        // screen). show_win_game itself is engine-side; let the adapter own
        // ordering by reporting the win and NOT opening the win screen here.
        // Persist the win on the Game so non-adapter consumers (the autoplay
        // planner's done-predicate) can observe it -- out_won alone is
        // transient and visible only to the immediate caller.
        g->stats.won = true;
        if (out_won) *out_won = true;
        return true;
    }

    // Spend search_cost_days. If the period crosses a week boundary, the
    // adventure loop's end_of_week must run -- report the commission so the
    // adapter can schedule_week_end for parity.
    int paid = 0;
    int weeks = GameSpendDays(g, res->tuning.search_cost_days, &paid);
    if (g->stats.game_over) {
        if (out_game_over) *out_game_over = true;
    } else {
        if (weeks > 0 && out_week_commission) *out_week_commission = paid;
        player_io_message(g, NULL,
            "Your search of this area has\n"
            "revealed nothing.");
    }
    return false;
}

bool flow_apply_dismiss_army(Game *g, FlowAnswer ans, int *out_slot) {
    if (out_slot) *out_slot = -1;
    if (ans.kind < FLOW_ANS_1 || ans.kind > FLOW_ANS_5) return false;
    int slot = (int)ans.kind - (int)FLOW_ANS_1;
    if (slot < 0 || slot >= GAME_ARMY_SLOTS) return false;
    if (out_slot) *out_slot = slot;

    // If this is the LAST non-empty stack, the adapter chains into the
    // "sent back to King in disgrace" confirm -- don't zero the slot here.
    int occupied = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (g->army[i].id[0] && g->army[i].count > 0) occupied++;
    }
    if (occupied <= 1) return true;   // adapter must chain to dismiss-last

    g->army[slot].id[0] = '\0';
    g->army[slot].count = 0;
    GameCompactArmy(g);
    return false;
}

bool flow_apply_dismiss_last(FlowAnswer ans) {
    // Confirmed dismissal of the last stack -> temp-death (shell orchestration).
    return ans.kind == FLOW_ANS_YES;
}

bool flow_apply_siege_monster(Game *g, const char *castle_id,
                              CombatResult outcome) {
    if (outcome == COMBAT_RESULT_WIN) {
        CastleRecord *cr = GameFindCastle(g, castle_id);
        if (cr) {
            cr->owner_kind = CASTLE_OWNER_PLAYER;
            cr->visited = true;
            for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                cr->garrison[s].id[0] = '\0';
                cr->garrison[s].count = 0;
            }
            // The hero just took this castle and is at it: allow a garrison now (the
            // own-castle marker the garrison core requires). Cleared on the next move.
            snprintf(g->position.own_castle, sizeof g->position.own_castle,
                     "%s", castle_id);
        }
        return false;
    }
    return outcome == COMBAT_RESULT_LOSS;   // adapter runs perform_temp_death
}

bool flow_apply_siege_villain(Game *g, const Resources *res,
                              const char *castle_id, CombatResult outcome) {
    (void)res;
    if (outcome != COMBAT_RESULT_WIN) {
        return outcome == COMBAT_RESULT_LOSS;
    }

    CastleRecord *cr = GameFindCastle(g, castle_id);
    if (!cr) return false;

    // Snapshot villain id+name BEFORE clearing the castle.
    char caught_vid[24];
    size_t k = 0;
    while (k + 1 < sizeof(caught_vid) && cr->villain_id[k]) {
        caught_vid[k] = cr->villain_id[k]; k++;
    }
    caught_vid[k] = '\0';
    const VillainDef *captured = caught_vid[0]
        ? villain_by_id(caught_vid) : NULL;
    const char *vname = (captured && captured->name[0])
        ? captured->name : caught_vid;

    bool contract_match = false;
    if (caught_vid[0] && g->contract.active_id[0] &&
        strcmp(caught_vid, g->contract.active_id) == 0) {
        contract_match = true;
    }

    if (contract_match) {
        cr->owner_kind = CASTLE_OWNER_PLAYER;
        cr->visited = true;
        cr->villain_id[0] = '\0';
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            cr->garrison[s].id[0] = '\0';
            cr->garrison[s].count = 0;
        }
        // The hero just took this castle and is at it: allow a garrison now (the
        // own-castle marker the garrison core requires). Cleared on the next move.
        snprintf(g->position.own_castle, sizeof g->position.own_castle,
                 "%s", castle_id);
    } else {
        // NO CONTRACT: the Lord is set free -- he keeps his castle and rebuilds his
        // army from its starting roster (the pack's VillainDef army; weekly
        // astrology growth resumes from there). The battle's spoils were already
        // credited by the combat core; the bounty, rank credit, and scepter-map
        // piece stay contract-gated. The defeat is remembered (villains_prefought):
        // pre-weakening a late lord before his contract window is a legitimate play.
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            cr->garrison[s].id[0] = '\0';
            cr->garrison[s].count = 0;
        }
        if (captured) {
            for (int s = 0; s < GAME_ARMY_SLOTS && s < 5; s++) {
                if (!captured->army_troops[s][0] || captured->army_counts[s] <= 0)
                    continue;
                snprintf(cr->garrison[s].id, sizeof cr->garrison[s].id,
                         "%s", captured->army_troops[s]);
                cr->garrison[s].count = captured->army_counts[s];
            }
            if (captured->index >= 0 && captured->index < 17)
                g->contract.villains_prefought[captured->index] = true;
        }
    }

    int prev_rank = g->character.cls.rank_index;
    int reward_gold = 0;
    bool ranked_up = false;
    if (contract_match) {
        if (captured) reward_gold = captured->reward;
        GameFulfillContract(g, caught_vid);
        // Real play (issue #13): the hero advances in rank ONLY via an audience
        // with King Maximus (shell_audience.c / OPENKB-SPEC 22), never on
        // capture. The autoplay oracle (g->oracle_mode) keeps the legacy
        // on-capture promotion so its search stays efficient and byte-identical
        // to the baseline -- a real player never sets oracle_mode.
        if (g->oracle_mode) {
            GameMaybeRankUp(g);
            ranked_up = (g->character.cls.rank_index != prev_rank);
        }
    }

    if (caught_vid[0]) {
        char body[640];
        if (contract_match) {
            int n = snprintf(body, sizeof body,
                "...and the capture of %s.\n\n"
                "For fulfilling your contract\n"
                "you receive an additional\n"
                "%d gold as bounty...\n"
                "and a piece of the map to\n"
                "the stolen scepter.",
                vname, reward_gold);
            if (ranked_up && n > 0 && n < (int)sizeof body) {
                snprintf(body + n, sizeof body - (size_t)n,
                         "\n\nYou are promoted to %s!",
                         g->character.cls.rank_title);
            }
        } else {
            snprintf(body, sizeof body,
                "...and the capture of %s.\n\n"
                "Since you did not have the\n"
                "proper contract, the Lord\n"
                "has been set free.",
                vname);
        }
        player_io_message(g, "Capture", body);
    }
    return false;
}

bool flow_apply_attack_foe(Game *g, Map *map, const char *foe_id,
                           int foe_x, int foe_y, CombatResult outcome) {
    if (outcome == COMBAT_RESULT_WIN) {
        FoeState *foe = (foe_id && foe_id[0]) ? GameFindFoe(g, foe_id) : NULL;
        // Foe-stamp-only clears: the dead foe may sit on the HERO's tile
        // (the step-onto combat trigger), and the hero may be standing on an
        // unconsumed pickup an unguarded clear would destroy.
        MapClearFoeStamp(map, foe_x, foe_y);
        if (foe) {
            foe->alive = false;
            // The foe may have follow-moved (GameFoesFollow) between this attack flow
            // being queued and resolved, relocating its INTERACT_FOE overlay away from
            // the flow's stale (foe_x,foe_y). Clear at the foe's CURRENT tile too, or
            // that stamp outlives the now-dead foe as a phantom wall nav can never pass.
            // (Mirrors flow_apply_accept_friendly's same fix for the friendly path.)
            MapClearFoeStamp(map, foe->x, foe->y);
        }
        return false;
    }
    return outcome == COMBAT_RESULT_LOSS;
}

void flow_apply_chest_choice(Game *g, int chest_gold, int chest_leadership,
                             FlowAnswer ans) {
    // A=gold (FLOW_ANS_1), B=distribute leadership (FLOW_ANS_2).
    if (ans.kind == FLOW_ANS_1) {
        GameAcceptChestGold(g, chest_gold);
    } else if (ans.kind == FLOW_ANS_2) {
        GameAcceptChestLeadership(g, chest_leadership);
    }
}

void flow_apply_discard_spell(Game *g, int spell_idx, FlowAnswer ans) {
    // YES: discard ONE charge of the chosen spell, freeing a spellbook slot
    // against the max_spells cap (GameKnownSpells). NO/cancel: no change. Guarded
    // so a stale/invalid index or an already-empty spell is a safe no-op.
    if (!g || ans.kind != FLOW_ANS_YES) return;
    if (spell_idx < 0 || spell_idx >= 14) return;
    if (g->spells.counts[spell_idx] <= 0) return;
    g->spells.counts[spell_idx]--;
}

void flow_apply_alcove(Game *g, Map *map, const Resources *res,
                       FlowAnswer ans) {
    if (ans.kind != FLOW_ANS_YES) return;
    const ResBanners *bn = &res->banners;
    char msg[RES_BANNER_LEN];
    if (g->stats.gold < res->economy.alcove_cost) {
        char cbuf[16];
        snprintf(cbuf, sizeof cbuf, "%d", res->economy.alcove_cost);
        ResTemplateVar vars[] = { { "COST", cbuf } };
        resources_format_template(msg, sizeof msg, bn->alcove_no_gold, vars, 1);
        player_io_message(g, res->ui.dt_alcove_result, msg);
    } else {
        g->stats.gold -= res->economy.alcove_cost;
        g->stats.knows_magic = true;
        MapClearInteractive(map, g->position.x, g->position.y);
        GameAddConsumed(g, g->position.zone, g->position.x, g->position.y);
        resources_format_template(msg, sizeof msg, bn->alcove_taught, NULL, 0);
        player_io_message(g, res->ui.dt_alcove_result, msg);
    }
}

void flow_apply_recruit(Game *g, const RecruitParams *params, FlowAnswer ans) {
    if (ans.kind != FLOW_ANS_YES || !params || !params->troop_id ||
        !params->troop_id[0]) {
        return;
    }
    int n = ans.number;
    int max = GameMaxRecruitable(g, params->troop_id);
    if (max < 0) max = 0;
    if (n > max) n = max;
    if (n <= 0) return;

    int rc = GameBuyTroop(g, params->troop_id, n);
    if (rc == 1) {
        open_dialog_padded(g, NULL, g->res->banners.town_no_gold);
    } else if (rc == 2) {
        player_io_message(g, NULL, g->res->banners.no_troop_slots);
    } else if (rc == 0) {
        // Success ONLY: reduce dwelling population. rc=3 (over leadership) and
        // rc=4 (location refusal) must NOT decrement -- the player received no
        // troops in those cases.
        for (int i = 0; i < g->dwelling_count; i++) {
            DwellingState *d = &g->dwellings[i];
            if (d->x == params->x && d->y == params->y &&
                params->zone && strcmp(d->zone, params->zone) == 0) {
                d->count -= n;
                if (d->count < 0) d->count = 0;
                break;
            }
        }
    }
}

void flow_apply_accept_friendly(Game *g, Map *map, const FriendlyParams *params,
                                FlowAnswer ans) {
    if (!params) return;
    if (ans.kind == FLOW_ANS_YES && params->troop_id && params->troop_id[0] &&
        params->count > 0) {
        GameAddTroop(g, params->troop_id, params->count);
    }
    // Always consume the foe (yes or no). Foe-stamp-only clears, and the
    // consumed entry follows the clear: recording a non-foe tile here would
    // wipe (or falsely complete) whatever the hero stood on at reload.
    if (params->zone && params->zone[0]) {
        if (MapClearFoeStamp(map, params->x, params->y))
            GameAddConsumed(g, params->zone, params->x, params->y);
    }
    if (params->foe_id && params->foe_id[0]) {
        FoeState *foe = GameFindFoe(g, params->foe_id);
        if (foe) {
            foe->alive = false;
            // The foe may have follow-moved (GameFoesFollow) between this flow being
            // queued and answered, relocating its INTERACT_FOE overlay away from the
            // flow's stale (params->x,y). Clear at the foe's CURRENT tile too, or that
            // stamp outlives the now-dead foe as a phantom wall nav can never pass.
            if (MapClearFoeStamp(map, foe->x, foe->y))
                GameAddConsumed(g, foe->zone, foe->x, foe->y);
        }
    }
}

bool flow_apply_navigate(Game *g, Map *map, Fog *fog,
                         const char zones[][32], int zone_count,
                         FlowAnswer ans, int *out_week_commission) {
    if (out_week_commission) *out_week_commission = 0;
    if (ans.kind < FLOW_ANS_1 || ans.kind > FLOW_ANS_5) return false;
    int idx = (int)ans.kind - (int)FLOW_ANS_1;
    if (idx < 0 || idx >= zone_count) return false;

    const char *target = zones[idx];
    if (!GameSwitchZone(g, map, fog, target)) {
        player_io_message(g, NULL, "Cannot reach that continent.");
        return false;
    }
    int paid = 0;
    GameSpendWeek(g, &paid);
    if (paid > 0 && out_week_commission) *out_week_commission = paid;
    return true;
}
