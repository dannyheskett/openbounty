// src/shell_weekend.c

#include "shell_weekend.h"

#include <stdio.h>
#include <string.h>

#include "pending.h"
#include "prompt.h"
#include "tables.h"
#include "ui.h"
#include "views.h"
#include "layout.h"
#include "player_io.h"

// The stacks the week could not pay, named in a list: "A", "A and B",
// "A, B and C" (the pack's own and-word is not needed: the template carries
// the sentence, the list only the names).
static bool troops_left(const Game *g, char *out, size_t cap) {
    const char *names[GAME_ARMY_SLOTS];
    int n = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        const ArmyStack *u = &g->stats.last_week_left[i];
        if (!u->id[0] || u->count <= 0) continue;
        const TroopDef *t = troop_by_id(u->id);
        names[n++] = t ? t->name : u->id;
    }
    out[0] = '\0';
    size_t o = 0;
    for (int i = 0; i < n && o + 1 < cap; i++) {
        const char *sep = i == 0 ? "" : (i == n - 1 ? " and " : ", ");
        o += (size_t)snprintf(out + o, cap - o, "%s%s", sep, names[i]);
    }
    return n > 0;
}

static void week_end_done(void) {
    pending_week_phase = WK_PHASE_NONE;
    pending_week_paid  = 0;
}

bool pump_week_end_dialog(const Game *g) {
    if (pending_week_phase == WK_PHASE_NONE)        return false;
    if (dialog_is_active() || prompt_is_active())   return false;
    if (views_active() != VIEW_NONE)                return false;

    if (pending_week_phase == WK_PHASE_ASTROLOGY) {
        const TroopDef *t = troop_by_index(pending_astrology_troop_idx);
        const char *creature = t->name;
        const ResBanners *bn = &g->res->banners;
        char header[64], body[320], wbuf[16];
        snprintf(wbuf, sizeof wbuf, "%d", pending_week_id);
        ResTemplateVar hvars[] = { { "WEEK", wbuf } };
        resources_format_template(header, sizeof header,
                                  bn->astrology_header, hvars, 1);
        ResTemplateVar bvars[] = { { "TROOP", creature } };
        resources_format_template(body, sizeof body,
                                  bn->astrology_body, bvars, 1);
        if (CL_IS_MODERN) player_io_note_face((Game *)g, header, body, REQ_FACE_TROOP, t->index);
        else              player_io_note((Game *)g, header, body);
        pending_week_phase = WK_PHASE_BUDGET;
        return true;
    }

    if (pending_week_phase == WK_PHASE_BUDGET) {
        // The figures end_day recorded as it charged the week (OPENKB-SPEC
        // section 16.7), so On Hand + Payment - Boat - Army = Balance even
        // when the gold floor cut the upkeep short or the boat was repossessed.
        // openkb's screen showed full recruit cost here; the charge is a tenth.
        int on_hand = g->stats.last_week_on_hand;

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
        int         left_values[5] = { on_hand, g->stats.last_commission,
                                       g->stats.last_week_boat,
                                       g->stats.last_week_army, g->stats.gold };

        // Right column: up to 5 non-empty army stacks (break on
        // the first empty slot), each at its weekly upkeep.
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
            rcost[rn] = GameStackWeeklyUpkeep(t->id, g->army[i].count);
            rn++;
        }

        // Build 5 body rows, left + gap + right.
        char body[320];
        int bo = 0;
        if (CL_IS_MODERN) {
            // Modern: columns as wide as their widest entry, so any number
            // lines up: labels left, amounts right-aligned, troop names left,
            // costs right-aligned. The font is fixed-pitch.
            int lw = 0, vw = 0, tw = 0, cw = 0;
            char nb[16];
            for (int i = 0; i < 5; i++) {
                int n = (int)strlen(left_labels[i]); if (n > lw) lw = n;
                n = snprintf(nb, sizeof nb, "%d", left_values[i]); if (n > vw) vw = n;
                if (i < rn) {
                    n = (int)strlen(rtroop[i]); if (n > tw) tw = n;
                    n = snprintf(nb, sizeof nb, "%d", rcost[i]); if (n > cw) cw = n;
                }
            }
            for (int i = 0; i < 5; i++) {
                if (i < rn)
                    bo += snprintf(body + bo, sizeof(body) - (size_t)bo, "%-*s %*d  %-*s %*d\n",
                                   lw, left_labels[i], vw, left_values[i], tw, rtroop[i], cw, rcost[i]);
                else
                    bo += snprintf(body + bo, sizeof(body) - (size_t)bo, "%-*s %*d\n",
                                   lw, left_labels[i], vw, left_values[i]);
                if (bo >= (int)sizeof(body)) { bo = (int)sizeof(body) - 1; break; }
            }
            player_io_note((Game *)g, header, body);
            pending_week_phase = WK_PHASE_LEFT;
            return true;
        }
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
        player_io_note((Game *)g, header, body);
        pending_week_phase = WK_PHASE_LEFT;
        return true;
    }

    if (pending_week_phase == WK_PHASE_LEFT) {
        // The troops the week could not pay and that left (#141), named
        // after the budget that shows what was paid.
        char names[160], body[320];
        if (!g->res->banners.week_troops_left[0] || !troops_left(g, names, sizeof names)) {
            week_end_done();
            return false;
        }
        ResTemplateVar vars[] = { { "TROOPS", names } };
        resources_format_template(body, sizeof body, g->res->banners.week_troops_left, vars, 1);
        player_io_note((Game *)g, "", body);
        week_end_done();
        return true;
    }

    return false;
}
