// src/shell_weekend.c

#include "shell_weekend.h"

#include <stdio.h>

#include "pending.h"
#include "prompt.h"
#include "tables.h"
#include "ui.h"
#include "views.h"

// End-of-week budget screen displays per-troop cost
// as count * full recruit_cost and sums those for the "Army" total —
// even though the actual gold deducted is /10. We match that display
// semantic for parity. The real deduction happens in game.c end_day's
// week boundary and uses /10.
static int army_upkeep(const Game *g) {
    int total = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count == 0) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        if (t) total += g->army[i].count * t->recruit_cost;
    }
    return total;
}

bool pump_week_end_dialog(const Game *g) {
    if (pending_week_phase == WK_PHASE_NONE)        return false;
    if (dialog_is_active() || prompt_is_active())   return false;
    if (views_active() != VIEW_NONE)                return false;

    if (pending_week_phase == WK_PHASE_ASTROLOGY) {
        const TroopDef *t = troop_by_index(pending_astrology_troop_idx);
        const char *creature = (t && t->name[0]) ? t->name : "Peasants";
        const ResBanners *bn = &g->res->banners;
        char header[64], body[320], wbuf[16];
        snprintf(wbuf, sizeof wbuf, "%d", pending_week_id);
        ResTemplateVar hvars[] = { { "WEEK", wbuf } };
        resources_format_template(header, sizeof header,
                                  bn->astrology_header, hvars, 1);
        ResTemplateVar bvars[] = { { "TROOP", creature } };
        resources_format_template(body, sizeof body,
                                  bn->astrology_body, bvars, 1);
        player_io_message((Game *)g, header, body);
        pending_week_phase = WK_PHASE_BUDGET;
        return true;
    }

    if (pending_week_phase == WK_PHASE_BUDGET) {
        int upkeep_display = army_upkeep(g);   // full cost; shown as "Army"
        int boat = g->boat.has_boat ? GameBoatCost(g) : 0;
        // Reconstruct gold-on-hand *before* this week's deductions using
        // the REAL deduction (recruit_cost/10), since that's what game.c
        // end_day actually spent. The dialog is internally inconsistent
        // (shows full-cost Army total but deducted /10); we preserve the
        // "On Hand" math that reflects reality.
        int upkeep_real = upkeep_display / 10;
        int on_hand = g->stats.gold - pending_week_paid + upkeep_real + boat;
        if (on_hand < 0) on_hand = 0;

        // End-of-week budget screen. 28-column
        // bottom frame, two columns:
        //   Col 0-12:  "<label>% 6d"  (5 rows)
        //   Col 14-27: "<troop-8char>% 6d" for up to 5 army stacks.
        // Header + labels come from strings.banners.budget_*.
        const ResBanners *bn = &g->res->banners;
        char header[64], wbuf[16];
        snprintf(wbuf, sizeof wbuf, "%d", pending_week_id);
        ResTemplateVar hvars[] = { { "WEEK", wbuf } };
        resources_format_template(header, sizeof header,
                                  bn->budget_header, hvars, 1);

        // Left column values.
        const char *left_labels[5] = {
            bn->budget_on_hand,
            bn->budget_payment,
            bn->budget_boat,
            bn->budget_army,
            bn->budget_balance,
        };
        int         left_values[5] = { on_hand, pending_week_paid, boat,
                                       upkeep_display, g->stats.gold };

        // Right column: up to 5 non-empty army stacks (break on
        // the first empty slot). Cost = count * recruit_cost (full price,
        // not the upkeep-adjusted /10 — full recruit price).
        char rtroop[5][16] = { {0} };
        int  rcost[5] = { 0 };
        int  rn = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS && i < 5; i++) {
            if (!g->army[i].id[0] || g->army[i].count == 0) break;
            const TroopDef *t = troop_by_id(g->army[i].id);
            if (!t) break;
            size_t k = 0;
            while (k + 1 < sizeof(rtroop[rn]) && t->name[k]) {
                rtroop[rn][k] = t->name[k]; k++;
            }
            rtroop[rn][k] = '\0';
            rcost[rn] = g->army[i].count * t->recruit_cost;
            rn++;
        }

        // Build 5 body rows, left + gap + right.
        char body[320];
        int bo = 0;
        for (int i = 0; i < 5; i++) {
            // Left: "<label7>% 6d" = 13 chars.
            char left[16];
            snprintf(left, sizeof(left), "%s% 6d",
                     left_labels[i], left_values[i]);
            // Right: "<troop-8>% 6d" = 14 chars, or blank if no troop.
            char right[32];
            if (i < rn) {
                snprintf(right, sizeof(right), "%-8s% 6d",
                         rtroop[i], rcost[i]);
            } else {
                right[0] = '\0';
            }
            // Pad `left` to 13, add a space gap (col 13), then right.
            bo += snprintf(body + bo, sizeof(body) - bo,
                           "%-13s %s\n", left, right);
        }
        player_io_message((Game *)g, header, body);
        pending_week_phase = WK_PHASE_NONE;
        pending_week_paid  = 0;
        return true;
    }

    return false;
}
