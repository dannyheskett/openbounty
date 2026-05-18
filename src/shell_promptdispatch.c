// src/shell_promptdispatch.c

#include "shell_promptdispatch.h"

#include <stdio.h>
#include <string.h>

#include "combat_loop.h"
#include "end_cartoon.h"
#include "flows.h"
#include "pending.h"
#include "prompt.h"
#include "shell_tempdeath.h"
#include "tables.h"
#include "ui.h"
#include "views.h"

bool prompt_dispatch_tick(ShellCtx *ctx) {
    if (!prompt_is_active()) return false;

    Game            *g  = ctx->game;
    Map             *m  = ctx->map;
    Fog             *f  = ctx->fog;
    const Resources *r_ = ctx->res;

    // Bottom-frame prompt (yes/no or numeric). When it returns a
    // result, dispatch based on pending_flow.
    PromptResult r = prompt_update();
    if (r == PROMPT_RESULT_NONE) return true;

    switch (pending_flow) {
    case FLOW_SEARCH:
        if (r == PROMPT_RESULT_YES) {
            // Scepter tile → win_game; otherwise spend search_cost_days
            // and show the "revealed nothing" banner.
            bool on_scepter =
                (strcmp(g->scepter.zone, g->position.zone) == 0 &&
                 g->scepter.x == g->position.x &&
                 g->scepter.y == g->position.y);
            if (on_scepter) {
                run_end_cartoon(ctx->render_target, r_, ctx->sprites);
                show_win_game(g, r_);
            } else {
                // Spend search_cost_days. If the period crosses a week
                // boundary, the adventure loop's end_of_week runs — so
                // we must queue schedule_week_end for parity.
                int paid = 0;
                int weeks = GameSpendDays(g, r_->tuning.search_cost_days, &paid);
                if (g->stats.game_over) {
                    show_lose_game(g, r_);
                } else {
                    if (weeks > 0) schedule_week_end(g, paid);
                    open_dialog(NULL,
                        "Your search of this area has\n"
                        "revealed nothing.");
                }
            }
        }
        break;

    case FLOW_DISMISS_ARMY:
        if (r >= PROMPT_RESULT_1 && r <= PROMPT_RESULT_5) {
            int slot = r - PROMPT_RESULT_1;
            if (slot < 0 || slot >= GAME_ARMY_SLOTS) break;
            // If this is the LAST non-empty stack, chain into the
            // "sent back to King in disgrace" confirm.
            int occupied = 0;
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (g->army[i].id[0] && g->army[i].count > 0) occupied++;
            }
            if (occupied <= 1) {
                pending_castle_id[0] = (char)('0' + slot);
                pending_castle_id[1] = '\0';
                pending_flow = FLOW_DISMISS_LAST;
                {
                    char body[RES_BANNER_LEN];
                    resources_format_template(body, sizeof body,
                                              g->res->banners.body_dismiss_last,
                                              NULL, 0);
                    prompt_yes_no_open(g->res->ui.dt_dismiss_last, body);
                }
                goto prompt_chained;
            }
            // Non-last dismissal: zero the slot, then compact.
            g->army[slot].id[0] = '\0';
            g->army[slot].count = 0;
            GameCompactArmy(g);
        }
        break;

    case FLOW_DISMISS_LAST:
        if (r == PROMPT_RESULT_YES) perform_temp_death(g, m, f, r_);
        pending_castle_id[0] = '\0';
        break;

    case FLOW_SIEGE_MONSTER: {
        if (r == PROMPT_RESULT_YES) {
            CastleRecord *cr = GameFindCastle(g, pending_castle_id);
            const ResCastle *rc = resources_castle_by_id(r_, pending_castle_id);
            CombatTarget tgt = { 0 };
            tgt.name = rc && rc->name[0] ? rc->name : pending_castle_id;
            if (cr) {
                tgt.garrison = cr->garrison;
                tgt.garrison_slots = GAME_ARMY_SLOTS;
            }
            CombatResult outcome = RunCombat(g, ctx->sprites,
                                             ctx->render_target,
                                             COMBAT_MODE_CASTLE, &tgt);
            if (outcome == COMBAT_RESULT_WIN && cr) {
                cr->owner_kind = CASTLE_OWNER_PLAYER;
                cr->visited = true;
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    cr->garrison[s].id[0] = '\0';
                    cr->garrison[s].count = 0;
                }
            } else if (outcome == COMBAT_RESULT_LOSS) {
                perform_temp_death(g, m, f, r_);
            }
        }
        pending_castle_id[0] = '\0';
        break;
    }

    case FLOW_SIEGE_VILLAIN: {
        if (r == PROMPT_RESULT_YES) {
            CastleRecord *cr = GameFindCastle(g, pending_castle_id);
            const ResCastle *rc = resources_castle_by_id(r_, pending_castle_id);
            const VillainDef *v = (cr && cr->villain_id[0])
                                ? villain_by_id(cr->villain_id) : NULL;
            CombatTarget tgt = { 0 };
            tgt.name = v && v->name[0] ? v->name
                     : (rc && rc->name[0] ? rc->name : pending_castle_id);
            if (cr) {
                tgt.garrison = cr->garrison;
                tgt.garrison_slots = GAME_ARMY_SLOTS;
            }
            CombatResult outcome = RunCombat(g, ctx->sprites,
                                             ctx->render_target,
                                             COMBAT_MODE_CASTLE, &tgt);
            if (outcome == COMBAT_RESULT_WIN && cr) {
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

                cr->owner_kind = CASTLE_OWNER_PLAYER;
                cr->visited = true;
                cr->villain_id[0] = '\0';
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    cr->garrison[s].id[0] = '\0';
                    cr->garrison[s].count = 0;
                }

                bool contract_match = false;
                if (caught_vid[0] && g->contract.active_id[0] &&
                    strcmp(caught_vid, g->contract.active_id) == 0) {
                    contract_match = true;
                }

                int prev_rank = g->character.cls.rank_index;
                int reward_gold = 0;
                bool ranked_up = false;
                if (contract_match) {
                    if (captured) reward_gold = captured->reward;
                    GameFulfillContract(g, caught_vid);
                    GameMaybeRankUp(g);
                    ranked_up = (g->character.cls.rank_index != prev_rank);
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
                    open_dialog("Capture", body);
                }
            } else if (outcome == COMBAT_RESULT_LOSS) {
                perform_temp_death(g, m, f, r_);
            }
        }
        pending_castle_id[0] = '\0';
        break;
    }

    case FLOW_ATTACK_FOE: {
        if (r == PROMPT_RESULT_YES && pending_foe_id[0]) {
            FoeState *foe = GameFindFoe(g, pending_foe_id);
            CombatTarget tgt = { 0 };
            tgt.name = "Hostile band";
            if (foe) {
                tgt.garrison = foe->garrison;
                tgt.garrison_slots = GAME_ARMY_SLOTS;
            }
            CombatResult outcome = RunCombat(g, ctx->sprites,
                                             ctx->render_target,
                                             COMBAT_MODE_FOE, &tgt);
            if (outcome == COMBAT_RESULT_WIN) {
                MapClearInteractive(m, pending_foe_x, pending_foe_y);
                if (foe) foe->alive = false;
            } else if (outcome == COMBAT_RESULT_LOSS) {
                perform_temp_death(g, m, f, r_);
            }
        }
        pending_foe_id[0] = '\0';
        pending_foe_x = pending_foe_y = -1;
        break;
    }

    case FLOW_CHEST_CHOICE:
        // A=gold, B=distribute. prompt_ab_open returns PROMPT_RESULT_1
        // for A and PROMPT_RESULT_2 for B.
        if (r == PROMPT_RESULT_1) {
            GameAcceptChestGold(g, pending_chest_gold);
        } else if (r == PROMPT_RESULT_2) {
            GameAcceptChestLeadership(g, pending_chest_leadership);
        }
        pending_chest_gold = 0;
        pending_chest_leadership = 0;
        break;

    case FLOW_ALCOVE:
        if (r == PROMPT_RESULT_YES) {
            const ResBanners *bn = &r_->banners;
            char msg[RES_BANNER_LEN];
            if (g->stats.gold < r_->economy.alcove_cost) {
                char cbuf[16];
                snprintf(cbuf, sizeof cbuf, "%d", r_->economy.alcove_cost);
                ResTemplateVar vars[] = { { "COST", cbuf } };
                resources_format_template(msg, sizeof msg,
                                          bn->alcove_no_gold, vars, 1);
                open_dialog(r_->ui.dt_alcove_result, msg);
            } else {
                g->stats.gold -= r_->economy.alcove_cost;
                g->stats.knows_magic = true;
                MapClearInteractive(m, g->position.x, g->position.y);
                GameAddConsumed(g, g->position.zone,
                                g->position.x, g->position.y);
                resources_format_template(msg, sizeof msg,
                                          bn->alcove_taught, NULL, 0);
                open_dialog(r_->ui.dt_alcove_result, msg);
            }
        }
        if (views_active() == VIEW_ALCOVE) views_dismiss();
        break;

    case FLOW_RECRUIT:
        if (r == PROMPT_RESULT_YES && pending_dwelling_troop[0]) {
            int n = prompt_text_input_value();
            int max = GameMaxRecruitable(g, pending_dwelling_troop);
            if (max < 0) max = 0;
            if (n > max) n = max;
            if (n > 0) {
                int rc = GameBuyTroop(g, pending_dwelling_troop, n);
                if (rc == 1) {
                    open_dialog_flags(NULL,
                        g->res->banners.town_no_gold, MSG_FLAG_PADDED);
                } else if (rc == 2) {
                    open_dialog(NULL, g->res->banners.no_troop_slots);
                } else {
                    // Success: reduce dwelling population.
                    for (int i = 0; i < g->dwelling_count; i++) {
                        DwellingState *d = &g->dwellings[i];
                        if (d->x == pending_dwelling_x &&
                            d->y == pending_dwelling_y &&
                            strcmp(d->zone, pending_dwelling_zone) == 0) {
                            d->count -= n;
                            if (d->count < 0) d->count = 0;
                            break;
                        }
                    }
                }
            }
        }
        pending_dwelling_troop[0] = '\0';
        pending_dwelling_zone[0]  = '\0';
        pending_dwelling_x = pending_dwelling_y = -1;
        if (views_active() == VIEW_DWELLING) views_dismiss();
        break;

    case FLOW_ACCEPT_FRIENDLY:
        if (r == PROMPT_RESULT_YES &&
            pending_dwelling_troop[0] &&
            pending_friendly_count > 0) {
            GameAddTroop(g, pending_dwelling_troop, pending_friendly_count);
        }
        // Always consume the foe (yes or no).
        if (pending_dwelling_zone[0]) {
            MapClearInteractive(m, pending_dwelling_x, pending_dwelling_y);
            GameAddConsumed(g, pending_dwelling_zone,
                            pending_dwelling_x, pending_dwelling_y);
        }
        if (pending_friendly_foe_id[0]) {
            FoeState *foe = GameFindFoe(g, pending_friendly_foe_id);
            if (foe) foe->alive = false;
        }
        pending_dwelling_troop[0] = '\0';
        pending_dwelling_zone[0]  = '\0';
        pending_friendly_foe_id[0] = '\0';
        pending_friendly_count = 0;
        pending_dwelling_x = pending_dwelling_y = -1;
        break;

    case FLOW_NAVIGATE:
        if (r >= PROMPT_RESULT_1 && r <= PROMPT_RESULT_5) {
            int idx = r - PROMPT_RESULT_1;
            if (idx >= 0 && idx < pending_nav_count) {
                const char *target = pending_nav_zones[idx];
                if (!GameSwitchZone(g, m, f, target)) {
                    open_dialog(NULL, "Cannot reach that continent.");
                } else {
                    int paid = 0;
                    GameSpendWeek(g, &paid);
                    if (paid > 0) schedule_week_end(g, paid);
                }
            }
        }
        pending_nav_count = 0;
        break;

    case FLOW_NONE: default: break;
    }
    pending_flow = FLOW_NONE;
prompt_chained: ;   // chained-prompt target: skip reset
    return true;
}
