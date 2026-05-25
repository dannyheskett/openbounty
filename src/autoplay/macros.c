// Reusable phase helpers for autoplay flow.c. See macros.h.

#include "autoplay/macros.h"
#include "autoplay/nav.h"

#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"
#include "pending.h"
#include "tables.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// -- shared assert predicates ----------------------------------------------
static bool ap_macro_assert_always(const Game *g) { (void)g; return true; }
static bool ap_macro_assert_dialog_closed(const Game *g) {
    (void)g; return !dialog_is_active();
}
static bool ap_macro_assert_prompt_gone(const Game *g) {
    (void)g; return !prompt_is_active();
}

// -- tile lookup -----------------------------------------------------------
// Linear scan of the current map for the first tile whose
// `interactive == want_interact` and (if want_id != NULL && want_id[0])
// whose `id == want_id`.
static bool find_tile_in_zone(const Map *m,
                              int want_interact,
                              const char *want_id,
                              int *out_x, int *out_y) {
    if (!m) return false;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if ((int)t->interactive != want_interact) continue;
            if (want_id && want_id[0] &&
                strcmp(t->id, want_id) != 0) continue;
            *out_x = x;
            *out_y = y;
            return true;
        }
    }
    return false;
}

// -- generic nav-to-coords body --------------------------------------------
// Used by every nav helper. Handles prompt/dialog/town views, picks the
// step (foe-avoiding first, then plain fallback), transitions to the
// caller's next_phase on arrival. On no-path, transitions to AP_FLOW_DONE
// so the run fails fast rather than spinning.
static ApCmd ap_nav_body(const Game *g, const Map *m,
                         AutoplayState *st,
                         const char *label,
                         int goal_x, int goal_y,
                         AutoplayPhase resume_phase,
                         AutoplayPhase next_phase,
                         bool *out_phase_done,
                         AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];

    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "yes_no") == 0) {
            // Foe encounter — route to combat with resume back here.
            st->module_scratch[3] = resume_phase;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_COMBAT;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:y_foe", label);
            return (ApCmd){ cmd_buf, KEY_Y, ap_macro_assert_always };
        }
        if (kind && strcmp(kind, "text") == 0) {
            snprintf(cmd_buf, sizeof cmd_buf, "%s:enter", label);
            return (ApCmd){ cmd_buf, KEY_ENTER,
                            ap_macro_assert_prompt_gone };
        }
        // A/B chest — default to gold (B = leadership in classic UI;
        // varies). Phases that need a different policy shouldn't use
        // this helper for the chest-grabbing leg.
        snprintf(cmd_buf, sizeof cmd_buf, "%s:b_chest", label);
        return (ApCmd){ cmd_buf, KEY_B, ap_macro_assert_prompt_gone };
    }
    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }
    if (views_active() == VIEW_TOWN) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc_town", label);
        return (ApCmd){ cmd_buf, KEY_ESCAPE, ap_macro_assert_always };
    }

    if (g->position.x == goal_x && g->position.y == goal_y) {
        *out_phase_done = true;
        *out_next_phase = next_phase;
        AP_LOG("[%s] arrived: pos=(%d,%d) gold=%d hp=%d",
               label, g->position.x, g->position.y, g->stats.gold,
               ap_army_total_hp(g));
        snprintf(cmd_buf, sizeof cmd_buf, "%s:arrived", label);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    int key = ap_nav_step_avoiding_foes_and_desert(g, m, goal_x, goal_y);
    if (key == 0) key = ap_nav_step(g, m, goal_x, goal_y);
    if (key == 0) {
        AP_LOG("[%s] no path from (%d,%d) to (%d,%d) — halting",
               label, g->position.x, g->position.y, goal_x, goal_y);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_path", label);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    snprintf(cmd_buf, sizeof cmd_buf, "%s:nav", label);
    return (ApCmd){ cmd_buf, key, ap_macro_assert_always };
}

// -- #2 ap_nav_to_xy --------------------------------------------------------
ApCmd ap_nav_to_xy(const Game *g, const Map *m,
                   AutoplayState *st,
                   const char *label,
                   int goal_x, int goal_y,
                   AutoplayPhase resume_phase,
                   AutoplayPhase next_phase,
                   bool *out_phase_done,
                   AutoplayPhase *out_next_phase) {
    return ap_nav_body(g, m, st, label, goal_x, goal_y,
                       resume_phase, next_phase,
                       out_phase_done, out_next_phase);
}

// -- #3 ap_nav_to_tile ------------------------------------------------------
ApCmd ap_nav_to_tile(const Game *g, const Map *m,
                     AutoplayState *st,
                     const char *label,
                     int want_interact,
                     const char *want_id,
                     AutoplayPhase resume_phase,
                     AutoplayPhase next_phase,
                     bool *out_phase_done,
                     AutoplayPhase *out_next_phase) {
    int gx, gy;
    if (!find_tile_in_zone(m, want_interact, want_id, &gx, &gy)) {
        // Target already consumed (artifact picked up, chest opened,
        // etc.) — treat as success and advance.
        AP_LOG("[%s] target already consumed (interactive=%d id='%s') "
               "— skipping ahead", label, want_interact,
               want_id ? want_id : "");
        *out_phase_done = true;
        *out_next_phase = next_phase;
        static char cmd_buf[64];
        snprintf(cmd_buf, sizeof cmd_buf, "%s:done_already", label);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    return ap_nav_body(g, m, st, label, gx, gy,
                       resume_phase, next_phase,
                       out_phase_done, out_next_phase);
}

ApCmd ap_nav_to_artifact(const Game *g, const Map *m,
                         AutoplayState *st,
                         const char *artifact_id,
                         AutoplayPhase resume_phase,
                         AutoplayPhase next_phase,
                         bool *out_phase_done,
                         AutoplayPhase *out_next_phase) {
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "NAV_ARTIFACT[%s]",
             artifact_id ? artifact_id : "?");
    return ap_nav_to_tile(g, m, st, label_buf,
                          (int)INTERACT_ARTIFACT, artifact_id,
                          resume_phase, next_phase,
                          out_phase_done, out_next_phase);
}

ApCmd ap_nav_to_town(const Game *g, const Map *m,
                     AutoplayState *st,
                     const char *town_id,
                     AutoplayPhase resume_phase,
                     AutoplayPhase next_phase,
                     bool *out_phase_done,
                     AutoplayPhase *out_next_phase) {
    (void)st;
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "NAV_TOWN[%s]",
             town_id ? town_id : "?");

    // Town entered: hand off to caller.
    if (views_active() == VIEW_TOWN) {
        AP_LOG("[%s] entered: pos=(%d,%d)", label_buf,
               g->position.x, g->position.y);
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:entered", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    int gx = -1, gy = -1;
    if (!find_tile_in_zone(m, (int)INTERACT_TOWN, town_id, &gx, &gy)) {
        AP_LOG("[%s] town not on map — halt", label_buf);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_town", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    return ap_nav_body(g, m, st, label_buf, gx, gy,
                       resume_phase, next_phase,
                       out_phase_done, out_next_phase);
}

ApCmd ap_nav_to_castle(const Game *g, const Map *m,
                       AutoplayState *st,
                       const char *castle_id,
                       AutoplayPhase resume_phase,
                       AutoplayPhase next_phase,
                       bool *out_phase_done,
                       AutoplayPhase *out_next_phase) {
    (void)st;
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "NAV_CASTLE[%s]",
             castle_id ? castle_id : "?");

    // Castle view opened: hand off.
    ViewKind v = views_active();
    if (v == VIEW_HOME_CASTLE || v == VIEW_OWN_CASTLE) {
        AP_LOG("[%s] entered castle view: pos=(%d,%d)", label_buf,
               g->position.x, g->position.y);
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:entered", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    int gx = -1, gy = -1;
    if (!find_tile_in_zone(m, (int)INTERACT_CASTLE_GATE,
                           castle_id, &gx, &gy)) {
        AP_LOG("[%s] castle gate not on map — halt", label_buf);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_gate", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    // Walk to the tile just south of the gate; engine treats UP from
    // there as a gate-step.
    int target_x = gx, target_y = gy + 1;
    if (g->position.x == target_x && g->position.y == target_y &&
        v == VIEW_NONE && !dialog_is_active() && !prompt_is_active()) {
        // Adjacent — press UP to step onto the gate (which opens
        // the castle view OR triggers a combat / audience).
        snprintf(cmd_buf, sizeof cmd_buf, "%s:up_into_gate",
                 label_buf);
        return (ApCmd){ cmd_buf, KEY_UP, ap_macro_assert_always };
    }
    return ap_nav_body(g, m, st, label_buf, target_x, target_y,
                       resume_phase, next_phase,
                       out_phase_done, out_next_phase);
}

// =========================================================================
// #5 ap_sail_to_zone
// =========================================================================
// Sub-step encoding in module_scratch[14]:
//   0: nav to open-sea tile (board boat first if needed)
//   1: press N to open navmap prompt
//   2: press digit matching dest_zone_id in pending_nav_zones[]
//   3: wait for zone switch; advance to next_phase
//
// Open-sea tile lookup: scan the boat's parked location's water
// neighbors, or just use the boat's parked tile itself once boarded.

static bool find_open_sea_near_boat(const Game *g, const Map *m,
                                    int *out_x, int *out_y) {
    if (!g || !m) return false;
    int bx = g->boat.x, by = g->boat.y;
    if (bx < 0 || by < 0) return false;
    const Tile *bt = MapGetTile(m, bx, by);
    if (bt && bt->terrain == TERRAIN_WATER) {
        *out_x = bx; *out_y = by;
        return true;
    }
    // Boat parked next to a town — look at neighbors.
    static const int dx[8] = {0,1,1,1,0,-1,-1,-1};
    static const int dy[8] = {-1,-1,0,1,1,1,0,-1};
    for (int i = 0; i < 8; i++) {
        int x = bx + dx[i], y = by + dy[i];
        const Tile *t = MapGetTile(m, x, y);
        if (t && t->terrain == TERRAIN_WATER) {
            *out_x = x; *out_y = y;
            return true;
        }
    }
    return false;
}

ApCmd ap_sail_to_zone(const Game *g, const Map *m,
                      AutoplayState *st,
                      const char *dest_zone_id,
                      AutoplayPhase resume_phase,
                      AutoplayPhase next_phase,
                      bool *out_phase_done,
                      AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "SAIL[%s]",
             dest_zone_id ? dest_zone_id : "?");

    // Step 3: zone-switch achieved.
    if (strcmp(g->position.zone, dest_zone_id) == 0) {
        AP_LOG("[%s] arrived: pos=(%d,%d)", label_buf,
               g->position.x, g->position.y);
        st->module_scratch[14] = 0;
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:arrived", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    // Handle dialogs that pop up during sailing.
    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }

    int step = st->module_scratch[14];

    // Step 2: navmap prompt is open — press the digit for dest_zone_id.
    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "numeric") == 0) {
            for (int i = 0; i < pending_nav_count; i++) {
                if (strcmp(pending_nav_zones[i], dest_zone_id) == 0) {
                    int key = KEY_ONE + i;
                    st->module_scratch[14] = 3;
                    snprintf(cmd_buf, sizeof cmd_buf, "%s:pick_%d",
                             label_buf, i + 1);
                    return (ApCmd){ cmd_buf, key,
                                    ap_macro_assert_always };
                }
            }
            // Destination not in the prompt — navmap not picked up.
            AP_LOG("[%s] dest not in nav options (count=%d) — halt",
                   label_buf, pending_nav_count);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:no_option",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        // Unexpected prompt — dismiss with ENTER.
        snprintf(cmd_buf, sizeof cmd_buf, "%s:enter", label_buf);
        return (ApCmd){ cmd_buf, KEY_ENTER,
                        ap_macro_assert_prompt_gone };
    }

    // Step 1: in a boat on water — press N.
    if (step == 1 || (g->travel_mode == TRAVEL_BOAT && step <= 1)) {
        const Tile *here = MapGetTile(m, g->position.x, g->position.y);
        if (g->travel_mode == TRAVEL_BOAT &&
            here && here->terrain == TERRAIN_WATER) {
            st->module_scratch[14] = 2;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:n", label_buf);
            return (ApCmd){ cmd_buf, KEY_N,
                            ap_macro_assert_always };
        }
    }

    // Step 0: not in boat OR not on water — nav to the boat / sea tile.
    int sx, sy;
    if (!find_open_sea_near_boat(g, m, &sx, &sy)) {
        AP_LOG("[%s] no boat parked / no open sea found — halt",
               label_buf);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_sea", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    if (g->position.x == sx && g->position.y == sy &&
        g->travel_mode == TRAVEL_BOAT) {
        st->module_scratch[14] = 1;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:on_sea", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    // Use the generic nav body to step toward sea tile.
    return ap_nav_body(g, m, st, label_buf, sx, sy,
                       resume_phase, resume_phase,
                       out_phase_done, out_next_phase);
}

// =========================================================================
// #6 ap_collect_artifacts_in_zone
// =========================================================================

ApCmd ap_collect_artifacts_in_zone(const Game *g, const Map *m,
                                   AutoplayState *st,
                                   const char *zone_id,
                                   AutoplayPhase resume_phase,
                                   AutoplayPhase next_phase,
                                   bool *out_phase_done,
                                   AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "COLLECT_ART[%s]",
             zone_id ? zone_id : "?");

    // Wrong zone? Halt — caller should have sailed first.
    if (strcmp(g->position.zone, zone_id) != 0) {
        AP_LOG("[%s] wrong zone (in '%s') — halt",
               label_buf, g->position.zone);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:wrong_zone", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    // Find first remaining artifact tile.
    int gx = -1, gy = -1;
    if (!find_tile_in_zone(m, (int)INTERACT_ARTIFACT, NULL, &gx, &gy)) {
        AP_LOG("[%s] all artifacts collected — advancing",
               label_buf);
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:done", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    return ap_nav_body(g, m, st, label_buf, gx, gy,
                       resume_phase, resume_phase,
                       out_phase_done, out_next_phase);
}

// =========================================================================
// #1 ap_rehome_and_recruit
// =========================================================================
// Sub-step encoding in module_scratch[10]:
//   0: ensure we're on Continentia (sail back if not)
//   1: nav to maximus_castle gate
//   2: step UP onto gate to open VIEW_HOME_CASTLE
//   3: press A to open VIEW_RECRUIT_SOLDIERS
//   4: row buy loop (module_scratch[11] sub-counter: 0..5 rows,
//      each row = letter+9+9+9+ENTER → next row)
//   5: ESC out of recruit (back to VIEW_HOME_CASTLE)
//   6: ESC out of castle (back to VIEW_NONE)
//   7: done
//
// module_scratch[11] for the row loop encodes:
//   high nibble: which row (0..4 for A..E)
//   low nibble: which keystroke within the row (0=letter, 1..3=9, 4=ENTER)

ApCmd ap_rehome_and_recruit(const Game *g, const Map *m,
                            AutoplayState *st,
                            int min_gold_reserve,
                            AutoplayPhase resume_phase,
                            AutoplayPhase next_phase,
                            bool *out_phase_done,
                            AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    const char *label = "REHOME";

    // If the caller has changed since the last invocation, reset.
    // We tag the active call by the resume_phase value in slot [5].
    if (st->module_scratch[5] != (int)resume_phase) {
        st->module_scratch[5]  = (int)resume_phase;
        st->module_scratch[10] = 0;
        st->module_scratch[11] = 0;
    }

    int step = st->module_scratch[10];

    // Handle any incoming dialog or prompt first (e.g. arriving in
    // Maximus's audience would have a prompt, but maximus_castle is
    // owned-by-player so no audience).
    if (prompt_is_active() && step < 4) {
        // Foe encountered en route — pass through nav body's combat
        // resume.
        return ap_nav_body(g, m, st, label, 0, 0,
                           resume_phase, resume_phase,
                           out_phase_done, out_next_phase);
    }
    if (dialog_is_active() && step != 4) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }

    // Step 0: ensure we're on Continentia.
    if (step == 0) {
        if (strcmp(g->position.zone, "continentia") != 0) {
            return ap_sail_to_zone(g, m, st, "continentia",
                                   resume_phase, resume_phase,
                                   out_phase_done, out_next_phase);
        }
        st->module_scratch[10] = 1;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:on_home_zone", label);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    // Step 1: nav to maximus_castle gate. The gate is just south of
    // the castle home (look at INTERACT_CASTLE_GATE tile with
    // id="king_maximus").
    if (step == 1) {
        int gx = -1, gy = -1;
        if (!find_tile_in_zone(m, (int)INTERACT_CASTLE_GATE,
                               "king_maximus", &gx, &gy)) {
            AP_LOG("[%s] maximus_castle gate not found — halt", label);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:no_gate", label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        // Walk to the tile JUST SOUTH of the gate (so UP triggers it).
        int target_x = gx, target_y = gy + 1;
        if (g->position.x == target_x && g->position.y == target_y) {
            st->module_scratch[10] = 2;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:at_gate_south", label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        return ap_nav_body(g, m, st, label, target_x, target_y,
                           resume_phase, resume_phase,
                           out_phase_done, out_next_phase);
    }

    // Step 2: step UP onto the gate (opens VIEW_HOME_CASTLE).
    if (step == 2) {
        if (views_active() == VIEW_HOME_CASTLE) {
            st->module_scratch[10] = 3;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:in_castle", label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:up_into_gate", label);
        return (ApCmd){ cmd_buf, KEY_UP, ap_macro_assert_always };
    }

    // Step 3: press A to open VIEW_RECRUIT_SOLDIERS.
    if (step == 3) {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[10] = 4;
            st->module_scratch[11] = 0;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:in_recruit", label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:a", label);
        return (ApCmd){ cmd_buf, KEY_A, ap_macro_assert_always };
    }

    // Step 4: row loop. Encode sub = row*8 + ks (ks=0..4 per row).
    if (step == 4) {
        int sub = st->module_scratch[11];
        int row = sub >> 3;
        int ks  = sub & 7;
        const int NROWS = 5;
        // Gold-reserve check: if the caller asked to leave gold on
        // the table, stop row recruitment before the next row when
        // the current gold is already at/below the reserve floor.
        if (ks == 0 && min_gold_reserve > 0 &&
            g->stats.gold <= min_gold_reserve) {
            st->module_scratch[10] = 5;
            st->module_scratch[11] = 0;
            AP_LOG("[%s] gold-reserve %d hit (gold=%d) — exit",
                   label, min_gold_reserve, g->stats.gold);
            snprintf(cmd_buf, sizeof cmd_buf, "%s:reserve_hit",
                     label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        if (row >= NROWS) {
            // All rows done — exit recruit.
            st->module_scratch[10] = 5;
            st->module_scratch[11] = 0;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:rows_done", label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        switch (ks) {
        case 0: {
            // Press letter A+row to pick this row.
            st->module_scratch[11] = (row << 3) | 1;
            int key = KEY_A + row;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:row%d_letter",
                     label, row);
            return (ApCmd){ cmd_buf, key, ap_macro_assert_always };
        }
        case 1: case 2: case 3: {
            st->module_scratch[11] = (row << 3) | (ks + 1);
            snprintf(cmd_buf, sizeof cmd_buf, "%s:row%d_9_%d",
                     label, row, ks);
            return (ApCmd){ cmd_buf, KEY_NINE,
                            ap_macro_assert_always };
        }
        default: {
            // ENTER — commit; advance to next row.
            st->module_scratch[11] = ((row + 1) << 3) | 0;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:row%d_enter",
                     label, row);
            return (ApCmd){ cmd_buf, KEY_ENTER,
                            ap_macro_assert_always };
        }
        }
    }

    // Step 5: ESC out of recruit.
    if (step == 5) {
        if (views_active() == VIEW_HOME_CASTLE) {
            st->module_scratch[10] = 6;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:in_castle_post",
                     label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc_recruit", label);
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }

    // Step 6: ESC out of castle.
    if (step == 6) {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[%s] complete: pos=(%d,%d) gold=%d hp=%d",
                   label, g->position.x, g->position.y,
                   g->stats.gold, ap_army_total_hp(g));
            st->module_scratch[10] = 0;
            st->module_scratch[11] = 0;
            *out_phase_done = true;
            *out_next_phase = next_phase;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:done", label);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc_castle", label);
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }

    // Shouldn't reach here — reset.
    st->module_scratch[10] = 0;
    snprintf(cmd_buf, sizeof cmd_buf, "%s:reset", label);
    return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
}

// =========================================================================
// #7 ap_monster_castle_grind
// =========================================================================
// Sub-step encoding in module_scratch[8]:
//   0: find next monster castle in zone; if none, advance to next_phase
//   1: nav to gate (uses #3); after capture: continue
//   2: post-combat: enter own_castle, garrison cheapest troop, exit
//   3: invoke ap_rehome_and_recruit; on return advance to step 0
//
// module_scratch[9] tracks the cached "current target castle index" so
// we don't pick a different castle mid-fight after partial reordering.

static int total_garrison_hp(const CastleRecord *cr) {
    if (!cr) return 0;
    int hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!cr->garrison[i].id[0] || cr->garrison[i].count == 0) continue;
        const TroopDef *t = troop_by_id(cr->garrison[i].id);
        if (t) hp += t->hit_points * cr->garrison[i].count;
    }
    return hp;
}

static const CastleRecord *
find_weakest_monster_castle(const Game *g, const char *zone_id) {
    const CastleRecord *best = NULL;
    int best_hp = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (cr->owner_kind != CASTLE_OWNER_MONSTERS) continue;
        const ResCastle *rc = g->res
            ? resources_castle_by_id(g->res, cr->id) : NULL;
        if (!rc || strcmp(rc->zone, zone_id) != 0) continue;
        int hp = total_garrison_hp(cr);
        if (!best || hp < best_hp) {
            best = cr;
            best_hp = hp;
        }
    }
    return best;
}

ApCmd ap_monster_castle_grind(const Game *g, const Map *m,
                              AutoplayState *st,
                              const char *zone_id,
                              AutoplayPhase resume_phase,
                              AutoplayPhase next_phase,
                              bool *out_phase_done,
                              AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "GRIND[%s]",
             zone_id ? zone_id : "?");

    int step = st->module_scratch[8];

    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }

    // Step 3: re-recruit. Delegated to #1; advances back to step 0
    // when REHOME completes (it transitions to resume_phase = same
    // phase, so the macro re-runs and falls back into step 0).
    if (step == 3) {
        st->module_scratch[8] = 0;
        return ap_rehome_and_recruit(g, m, st, 0,
                                     resume_phase, resume_phase,
                                     out_phase_done, out_next_phase);
    }

    // Step 2: just captured; in own_castle view. Garrison cheapest
    // single troop (1x of whatever's in slot 0) by pressing the
    // garrison hotkey, ESC out.
    if (step == 2) {
        if (views_active() == VIEW_OWN_CASTLE) {
            // Simplest: ESC out. Garrisoning requires a deeper sub-
            // flow (own_castle → transfer screen → pick row → 1 →
            // ENTER → ESC) that varies per game build. Leave the
            // garrison empty for now — Phase 8 / 10 handle garrison
            // explicitly when needed; the grind macro's job is the
            // capture loop.
            st->module_scratch[8] = 3;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:exit_own",
                     label_buf);
            return (ApCmd){ cmd_buf, KEY_ESCAPE,
                            ap_macro_assert_always };
        }
        if (views_active() == VIEW_NONE) {
            // Exited already; head to re-recruit.
            st->module_scratch[8] = 3;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:to_recruit",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        // Other view active (recruit/transfer) — ESC.
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc_other", label_buf);
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }

    // Step 0: pick next monster castle.
    if (step == 0) {
        if (strcmp(g->position.zone, zone_id) != 0) {
            AP_LOG("[%s] wrong zone (in '%s') — halt",
                   label_buf, g->position.zone);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:wrong_zone",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        const CastleRecord *cr = find_weakest_monster_castle(g, zone_id);
        if (!cr) {
            AP_LOG("[%s] no monster castles left — advancing",
                   label_buf);
            st->module_scratch[8] = 0;
            *out_phase_done = true;
            *out_next_phase = next_phase;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:done", label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        st->module_scratch[8] = 1;
        AP_LOG("[%s] next target: castle='%s' hp=%d",
               label_buf, cr->id, total_garrison_hp(cr));
        snprintf(cmd_buf, sizeof cmd_buf, "%s:picked", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    // Step 1: nav to gate. After capture combat resolves, hero ends
    // up in VIEW_OWN_CASTLE (per engine flow) — detect that and bump
    // to step 2.
    if (step == 1) {
        if (views_active() == VIEW_OWN_CASTLE) {
            st->module_scratch[8] = 2;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:captured",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        const CastleRecord *cr = find_weakest_monster_castle(g, zone_id);
        if (!cr) {
            // Captured during last tick — go to step 0 to detect end.
            st->module_scratch[8] = 0;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:already",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        return ap_nav_to_castle(g, m, st, cr->id,
                                resume_phase, resume_phase,
                                out_phase_done, out_next_phase);
    }

    // Reset on unknown state.
    st->module_scratch[8] = 0;
    snprintf(cmd_buf, sizeof cmd_buf, "%s:reset", label_buf);
    return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
}

// =========================================================================
// ap_town_action — single-row town menu action.
// =========================================================================
// Town row letters: A=Contract, B=Boat, C=Info, D=Spell, E=Siege.
// The helper drives the appropriate row, dismisses any resulting info
// panel with SPACE, then ESCs out of the town view to next_phase.

ApCmd ap_town_action(const Game *g, const Map *m,
                     AutoplayState *st,
                     const char *action_kind,
                     AutoplayPhase resume_phase,
                     AutoplayPhase next_phase,
                     bool *out_phase_done,
                     AutoplayPhase *out_next_phase) {
    (void)m; (void)resume_phase;
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "TOWN[%s]",
             action_kind ? action_kind : "?");

    // Dismiss any info panel first.
    if (views_town_info_text() != NULL) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space_info", label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_always };
    }
    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space_dialog",
                 label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }

    // "exit" handles VIEW_NONE itself. For all other actions, if the
    // town view is gone we can't drive a row, so we just advance.
    if (views_active() == VIEW_NONE &&
        strcmp(action_kind, "exit") != 0) {
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_view", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    if (views_active() != VIEW_TOWN &&
        views_active() != VIEW_NONE) {
        // Some other view (recruit, castle, dwelling, etc.) — ESC.
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc_other", label_buf);
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }

    if (strcmp(action_kind, "siege") == 0) {
        if (g->stats.siege_weapons) {
            // Already done — advance.
            *out_phase_done = true;
            *out_next_phase = next_phase;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:already",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:e", label_buf);
        return (ApCmd){ cmd_buf, KEY_E, ap_macro_assert_always };
    }
    if (strcmp(action_kind, "boat") == 0) {
        if (g->boat.has_boat) {
            *out_phase_done = true;
            *out_next_phase = next_phase;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:already",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:b", label_buf);
        return (ApCmd){ cmd_buf, KEY_B, ap_macro_assert_always };
    }
    if (strcmp(action_kind, "contract") == 0) {
        if (g->contract.active_id[0]) {
            *out_phase_done = true;
            *out_next_phase = next_phase;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:already",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:a", label_buf);
        return (ApCmd){ cmd_buf, KEY_A, ap_macro_assert_always };
    }
    if (strncmp(action_kind, "contract_zone:", 14) == 0) {
        const char *want_zone = action_kind + 14;
        if (g->contract.active_id[0]) {
            const VillainDef *v =
                villain_by_id(g->contract.active_id);
            if (v && strcmp(v->zone, want_zone) == 0) {
                *out_phase_done = true;
                *out_next_phase = next_phase;
                snprintf(cmd_buf, sizeof cmd_buf, "%s:got",
                         label_buf);
                return (ApCmd){ cmd_buf, 0,
                                ap_macro_assert_always };
            }
            // Wrong zone — press A again to reroll.
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:a", label_buf);
        return (ApCmd){ cmd_buf, KEY_A, ap_macro_assert_always };
    }
    if (strcmp(action_kind, "exit") == 0) {
        // Just ESC out (helper for chaining at the end).
        if (views_active() == VIEW_NONE) {
            *out_phase_done = true;
            *out_next_phase = next_phase;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:exited",
                     label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc", label_buf);
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }

    // Unknown action — just exit.
    AP_LOG("[%s] unknown action — halting", label_buf);
    *out_phase_done = true;
    *out_next_phase = AP_FLOW_DONE;
    snprintf(cmd_buf, sizeof cmd_buf, "%s:unknown", label_buf);
    return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
}

// =========================================================================
// ap_buy_spells_at_town — buy spell_id N times.
// =========================================================================
// Mechanic: press D to open spell menu, accept (the engine's spell-buy
// prompt is yes/no by spell). We repeatedly press D until either
// max_spells is reached or we run out of gold (the buy bounces back
// without changing gold or spell count).
//
// Implementation: this is a single-row spammer. Each tick checks the
// current spell count; if below target, press D to attempt a buy.
// The engine handles bounce-back on no-gold or capped.

ApCmd ap_buy_spells_at_town(const Game *g, const Map *m,
                            AutoplayState *st,
                            const char *spell_id,
                            int target_count,
                            AutoplayPhase resume_phase,
                            AutoplayPhase next_phase,
                            bool *out_phase_done,
                            AutoplayPhase *out_next_phase) {
    (void)m; (void)resume_phase;
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "BUY_SPELLS[%s]",
             spell_id ? spell_id : "?");

    if (views_town_info_text() != NULL) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space_info", label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_always };
    }
    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space_dialog",
                 label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }
    if (prompt_is_active()) {
        // Spell-buy prompt — accept (Y).
        snprintf(cmd_buf, sizeof cmd_buf, "%s:y", label_buf);
        return (ApCmd){ cmd_buf, KEY_Y, ap_macro_assert_always };
    }
    if (views_active() == VIEW_NONE) {
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:done", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    if (views_active() != VIEW_TOWN) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc_other", label_buf);
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }

    // Look up spell count.
    int idx = -1;
    int n_spells = (int)(sizeof(g->spells.counts)/sizeof(g->spells.counts[0]));
    for (int i = 0; i < n_spells; i++) {
        const SpellDef *s = spell_by_index(i);
        if (s && strcmp(s->id, spell_id) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        AP_LOG("[%s] unknown spell — halting", label_buf);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_spell", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }

    int have = g->spells.counts[idx];
    // Track gold to detect bounce.
    int prev_gold = st->module_scratch[12];
    if (have >= target_count) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:esc_done", label_buf);
        st->module_scratch[12] = 0;
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }
    if (prev_gold > 0 && g->stats.gold == prev_gold) {
        // Last D bounced (no gold or capped). Exit.
        snprintf(cmd_buf, sizeof cmd_buf, "%s:bounce_esc", label_buf);
        st->module_scratch[12] = 0;
        return (ApCmd){ cmd_buf, KEY_ESCAPE,
                        ap_macro_assert_always };
    }
    st->module_scratch[12] = g->stats.gold;
    snprintf(cmd_buf, sizeof cmd_buf, "%s:d", label_buf);
    return (ApCmd){ cmd_buf, KEY_D, ap_macro_assert_always };
}

// =========================================================================
// ap_visit_alcove — magic-buying alcove.
// =========================================================================

ApCmd ap_visit_alcove(const Game *g, const Map *m,
                      AutoplayState *st,
                      int x, int y,
                      AutoplayPhase resume_phase,
                      AutoplayPhase next_phase,
                      bool *out_phase_done,
                      AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    const char *label = "ALCOVE";

    if (g->stats.knows_magic) {
        AP_LOG("[%s] already claimed — advancing", label);
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:already", label);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "yes_no") == 0) {
            // Could be alcove offer OR foe encounter.
            if (views_active() == VIEW_ALCOVE) {
                snprintf(cmd_buf, sizeof cmd_buf, "%s:y_alcove",
                         label);
                return (ApCmd){ cmd_buf, KEY_Y,
                                ap_macro_assert_always };
            }
            // Foe — combat.
            st->module_scratch[3] = resume_phase;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_COMBAT;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:y_foe", label);
            return (ApCmd){ cmd_buf, KEY_Y,
                            ap_macro_assert_always };
        }
        if (kind && strcmp(kind, "text") == 0) {
            snprintf(cmd_buf, sizeof cmd_buf, "%s:enter", label);
            return (ApCmd){ cmd_buf, KEY_ENTER,
                            ap_macro_assert_prompt_gone };
        }
        snprintf(cmd_buf, sizeof cmd_buf, "%s:b", label);
        return (ApCmd){ cmd_buf, KEY_B, ap_macro_assert_prompt_gone };
    }
    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }
    return ap_nav_body(g, m, st, label, x, y,
                       resume_phase, next_phase,
                       out_phase_done, out_next_phase);
}

// =========================================================================
// ap_recruit_at_dwelling — fixed-count dwelling buy.
// =========================================================================

ApCmd ap_recruit_at_dwelling(const Game *g, const Map *m,
                             AutoplayState *st,
                             const char *dwelling_id,
                             const char *troop_id,
                             int target_count,
                             AutoplayPhase resume_phase,
                             AutoplayPhase next_phase,
                             bool *out_phase_done,
                             AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "DWELL[%s]",
             troop_id ? troop_id : "?");

    // Already in army with enough? Skip.
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (g->army[i].id[0] &&
            strcmp(g->army[i].id, troop_id) == 0 &&
            g->army[i].count >= target_count) {
            AP_LOG("[%s] already have %d %s — advancing",
                   label_buf, g->army[i].count, troop_id);
            *out_phase_done = true;
            *out_next_phase = next_phase;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:already", label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
    }

    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "yes_no") == 0) {
            // Foe encounter during nav.
            st->module_scratch[3] = resume_phase;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_COMBAT;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:y_foe", label_buf);
            return (ApCmd){ cmd_buf, KEY_Y,
                            ap_macro_assert_always };
        }
        if (kind && strcmp(kind, "text") == 0) {
            // Dwelling-open prompt — start typing count.
            int sub = st->module_scratch[6];
            if (sub == 0) {
                st->module_scratch[6] = 1;
                snprintf(cmd_buf, sizeof cmd_buf, "%s:9_1",
                         label_buf);
                return (ApCmd){ cmd_buf, KEY_NINE,
                                ap_macro_assert_always };
            }
            if (sub == 1) {
                st->module_scratch[6] = 2;
                snprintf(cmd_buf, sizeof cmd_buf, "%s:9_2",
                         label_buf);
                return (ApCmd){ cmd_buf, KEY_NINE,
                                ap_macro_assert_always };
            }
            if (sub == 2) {
                st->module_scratch[6] = 3;
                snprintf(cmd_buf, sizeof cmd_buf, "%s:9_3",
                         label_buf);
                return (ApCmd){ cmd_buf, KEY_NINE,
                                ap_macro_assert_always };
            }
            if (sub == 3) {
                st->module_scratch[6] = 4;
                snprintf(cmd_buf, sizeof cmd_buf, "%s:9_4",
                         label_buf);
                return (ApCmd){ cmd_buf, KEY_NINE,
                                ap_macro_assert_always };
            }
            st->module_scratch[6] = 0;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:enter",
                     label_buf);
            return (ApCmd){ cmd_buf, KEY_ENTER,
                            ap_macro_assert_prompt_gone };
        }
    }
    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }

    // Find the dwelling tile. dwelling_id can be either the exact
    // salted id ("sd_dwarves_3") or just the troop name ("dwarves") —
    // we accept either by prefix match against "sd_<troop>_".
    char prefix[32];
    snprintf(prefix, sizeof prefix, "sd_%s_", troop_id);
    int dx = -1, dy = -1;
    for (int y = 0; y < m->height && dx < 0; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (t->interactive == INTERACT_DWELLING_PLAINS ||
                t->interactive == INTERACT_DWELLING_FOREST ||
                t->interactive == INTERACT_DWELLING_HILLS ||
                t->interactive == INTERACT_DWELLING_DUNGEON) {
                if (dwelling_id && dwelling_id[0] &&
                    strcmp(t->id, dwelling_id) == 0) {
                    dx = x; dy = y; break;
                }
                if (strncmp(t->id, prefix, strlen(prefix)) == 0) {
                    dx = x; dy = y; break;
                }
            }
        }
    }
    if (dx < 0) {
        AP_LOG("[%s] no dwelling for '%s' — advancing",
               label_buf, troop_id);
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_dwell", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    return ap_nav_body(g, m, st, label_buf, dx, dy,
                       resume_phase, next_phase,
                       out_phase_done, out_next_phase);
}

// =========================================================================
// ap_hunt_foe — walk onto a foe to trigger combat.
// =========================================================================

ApCmd ap_hunt_foe(const Game *g, const Map *m,
                  AutoplayState *st,
                  const char *foe_id,
                  AutoplayPhase resume_phase,
                  AutoplayPhase next_phase,
                  bool *out_phase_done,
                  AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "HUNT[%s]",
             foe_id ? foe_id : "?");

    const FoeState *foe = GameFindFoeConst(g, foe_id);
    if (!foe || !foe->alive) {
        AP_LOG("[%s] foe dead/missing — advancing", label_buf);
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:done", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    return ap_nav_body(g, m, st, label_buf, foe->x, foe->y,
                       resume_phase, next_phase,
                       out_phase_done, out_next_phase);
}

// =========================================================================
// ap_garrison_at_castle — drop a troop stack into an own castle.
// =========================================================================
// Sub-state in module_scratch[8]:
//   0: walk to gate tile, step onto it (engine opens VIEW_OWN_CASTLE)
//   1: SPACE toggles GARRISON mode (the screen starts in REMOVE)
//   2: press letter for the troop's slot
//   3: ESC out

ApCmd ap_garrison_at_castle(const Game *g, const Map *m,
                            AutoplayState *st,
                            const char *castle_id,
                            const char *troop_id,
                            AutoplayPhase resume_phase,
                            AutoplayPhase next_phase,
                            bool *out_phase_done,
                            AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    static char label_buf[64];
    snprintf(label_buf, sizeof label_buf, "GARR[%s/%s]",
             castle_id ? castle_id : "?",
             troop_id ? troop_id : "?");
    int sub = st->module_scratch[8];

    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }

    if (sub == 0) {
        if (views_active() == VIEW_OWN_CASTLE) {
            st->module_scratch[8] = 1;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:opened", label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        // Find castle's gate.
        int gx = -1, gy = -1;
        for (int yy = 0; yy < m->height && gx < 0; yy++) {
            for (int xx = 0; xx < m->width; xx++) {
                const Tile *t = &m->tiles[yy][xx];
                if (t->interactive == INTERACT_CASTLE_GATE &&
                    strcmp(t->id, castle_id) == 0) {
                    gx = xx; gy = yy; break;
                }
            }
        }
        if (gx < 0) {
            AP_LOG("[%s] gate not found — halt", label_buf);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:no_gate", label_buf);
            return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
        }
        return ap_nav_body(g, m, st, label_buf, gx, gy,
                           resume_phase, resume_phase,
                           out_phase_done, out_next_phase);
    }

    if (sub == 1) {
        st->module_scratch[8] = 2;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space_toggle",
                 label_buf);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_always };
    }

    if (sub == 2) {
        int slot = -1;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, troop_id) == 0 &&
                g->army[i].count > 0) { slot = i; break; }
        }
        if (slot < 0) {
            AP_LOG("[%s] troop not in army — skip", label_buf);
            st->module_scratch[8] = 3;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:no_troop",
                     label_buf);
            return (ApCmd){ cmd_buf, KEY_ESCAPE,
                            ap_macro_assert_always };
        }
        st->module_scratch[8] = 3;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:row_%d", label_buf,
                 slot);
        return (ApCmd){ cmd_buf, KEY_A + slot,
                        ap_macro_assert_always };
    }

    // sub == 3: ESC out.
    if (views_active() == VIEW_NONE) {
        AP_LOG("[%s] done: gold=%d hp=%d", label_buf,
               g->stats.gold, ap_army_total_hp(g));
        st->module_scratch[8] = 0;
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:done", label_buf);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    snprintf(cmd_buf, sizeof cmd_buf, "%s:esc", label_buf);
    return (ApCmd){ cmd_buf, KEY_ESCAPE,
                    ap_macro_assert_always };
}

// =========================================================================
// ap_chest_tour — walk a fixed list of coords.
// =========================================================================
// module_scratch[0] = current leg index.

ApCmd ap_chest_tour(const Game *g, const Map *m,
                    AutoplayState *st,
                    const char *label,
                    const ApChestLeg *legs, int n_legs,
                    int chest_policy,
                    AutoplayPhase resume_phase,
                    AutoplayPhase next_phase,
                    bool *out_phase_done,
                    AutoplayPhase *out_next_phase) {
    static char cmd_buf[64];
    int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];

    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "yes_no") == 0) {
            st->module_scratch[3] = resume_phase;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_COMBAT;
            snprintf(cmd_buf, sizeof cmd_buf, "%s:y_foe", label);
            return (ApCmd){ cmd_buf, KEY_Y,
                            ap_macro_assert_always };
        }
        if (kind && strcmp(kind, "text") == 0) {
            snprintf(cmd_buf, sizeof cmd_buf, "%s:enter", label);
            return (ApCmd){ cmd_buf, KEY_ENTER,
                            ap_macro_assert_prompt_gone };
        }
        // Chest A/B prompt.
        int key = (chest_policy == 1) ? KEY_B : KEY_A;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:%c",
                 label, (chest_policy == 1) ? 'b' : 'a');
        return (ApCmd){ cmd_buf, key, ap_macro_assert_prompt_gone };
    }
    if (dialog_is_active()) {
        snprintf(cmd_buf, sizeof cmd_buf, "%s:space", label);
        return (ApCmd){ cmd_buf, KEY_SPACE,
                        ap_macro_assert_dialog_closed };
    }

    if (leg >= n_legs) {
        AP_LOG("[%s] tour complete: pos=(%d,%d) gold=%d hp=%d",
               label, g->position.x, g->position.y, g->stats.gold,
               ap_army_total_hp(g));
        st->module_scratch[0] = 0;
        *out_phase_done = true;
        *out_next_phase = next_phase;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:done", label);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    int gx = legs[leg].x, gy = legs[leg].y;
    if (g->position.x == gx && g->position.y == gy) {
        AP_LOG("[%s] leg %d (%s) done: gold=%d hp=%d",
               label, leg, legs[leg].name,
               g->stats.gold, ap_army_total_hp(g));
        st->module_scratch[0] = leg + 1;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:leg_%d", label, leg);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    int key = ap_nav_step(g, m, gx, gy);
    if (key == 0) {
        AP_LOG("[%s] leg %d no path — skipping", label, leg);
        st->module_scratch[0] = leg + 1;
        snprintf(cmd_buf, sizeof cmd_buf, "%s:no_path_%d", label, leg);
        return (ApCmd){ cmd_buf, 0, ap_macro_assert_always };
    }
    snprintf(cmd_buf, sizeof cmd_buf, "%s:nav", label);
    return (ApCmd){ cmd_buf, key, ap_macro_assert_always };
}
