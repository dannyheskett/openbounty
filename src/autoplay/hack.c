// Hack module: capture Hack the Rogue immediately after Murray.
//
// Entry state (from murray.c's terminal transition): hero is near
// azram (around (30,37) on seed=1), still on continentia, with
// ~230 HP remaining army (down from 300 after the Murray fight) and
// ~6750 gold (Murray's reward credited).
//
// Path on seed=1:
//   1. find the nearest overland-reachable town and walk there. On
//      seed=1 from (30,37) the nearest is xoctan (51,35), 22 steps.
//   2. press A in town to take the next contract — on seed=1 this
//      is "hack".
//   3. locate Hack's castle. salt_villains pinned it to one of the
//      continentia castles at GameInit; we scan g->castles[] for
//      villain_id == "hack". On seed=1 it's at (22,14).
//   4. if the gate is overland-reachable from here, walk. Otherwise
//      re-rent a boat at the contract town and sail across to a
//      land tile near the gate, then walk.
//   5. step onto the gate → siege prompt → Y → rendered combat.
//      Combat input is driven by core.c's autoplay_before_frame.
//   6. dismiss the victory + capture dialogs, verify
//      villains_caught[hack]=true, save the checkpoint, transition
//      to AP_ALL_DONE.
//
// State slot layout (module_scratch[]):
//   [0],[1] = contract town (xoctan on seed=1)
//   [8],[9] = Hack's castle gate (x,y), set once by LOCATE_CASTLE
//      [13] = LEAVE_RECRUIT: last seen army HP (for settle detection)
//      [14] = LEAVE_RECRUIT: last seen gold
//      [15] = LEAVE_RECRUIT: frame_no of last change
//      [16] = dwelling-tour index (0,1,... over the visit list)
//  [17],[18] = current dwelling target (x,y)
//      [19] = RECRUIT_AT_DWELLING: count-entry queued flag
// Boat/sail state used to live here too; now owned by ap_ferry_tick
// in AutoplayState.ferry_*.

#include "autoplay/internal.h"

#include "input_host.h"
#include "tables.h"
#include "combat.h"
#include "tile.h"
#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stdio.h>
#include <string.h>

// Scan g->castles[] for the one currently held by `villain_id`.
// Returns -1 if not found. The (x,y) stored on each CastleRecord
// is the catalog (gate) coordinate.
static int find_villain_castle(const Game *g, const char *villain_id,
                               int *out_x, int *out_y,
                               const char **out_zone) {
    if (!g || !villain_id || !villain_id[0]) return -1;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (strcmp(cr->villain_id, villain_id) != 0) continue;
        const ResCastle *rc = resources_castle_by_id(g->res, cr->id);
        if (!rc) continue;
        if (out_x)    *out_x    = rc->x;
        if (out_y)    *out_y    = rc->y;
        if (out_zone) *out_zone = rc->zone;
        return i;
    }
    return -1;
}

// Walk g->res->towns[] for towns in the current zone, BFS-distance
// each from (cx,cy) on foot, return the closest. -1 if no town is
// overland-reachable.
static int find_nearest_town(const Game *g, const Map *m,
                             int cx, int cy,
                             int *out_x, int *out_y) {
    int best_dist = 1 << 30;
    int best_x = -1, best_y = -1;
    ApPoint tmp[AP_PATH_MAX];
    for (int i = 0; i < g->res->town_count; i++) {
        const ResTown *t = &g->res->towns[i];
        if (strcmp(t->zone, g->position.zone) != 0) continue;
        int n = ap_bfs(m, cx, cy, t->x, t->y, AP_TRAVEL_WALK,
                       tmp, AP_PATH_MAX);
        if (n > 0 && n < best_dist) {
            best_dist = n;
            best_x = t->x;
            best_y = t->y;
        }
    }
    if (best_x < 0) return -1;
    if (out_x) *out_x = best_x;
    if (out_y) *out_y = best_y;
    return best_dist;
}

ShellRunVerdict ap_hack_per_frame(Game *g, Map *m, Fog *f,
                                  Resources *res, int frame_no,
                                  AutoplayState *st) {
    (void)f; (void)res;

    switch (st->phase) {

    case AP_HACK_WALK_TO_TOWN_FOR_CONTRACT:
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_HACK_TAKE_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            st->path_len = 0;
            AP_LOG("[hack] entered town for contract");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        if (st->module_scratch[0] < 0) {
            int tx = -1, ty = -1;
            int n = find_nearest_town(g, m, g->position.x, g->position.y,
                                      &tx, &ty);
            if (n <= 0) {
                AP_LOG("[hack] no overland-reachable town from (%d,%d)",
                       g->position.x, g->position.y);
                return SHELL_RUN_EXIT_FAIL;
            }
            st->module_scratch[0] = tx;
            st->module_scratch[1] = ty;
            st->module_scratch[2] = tx;   // boat-rental town defaults
            st->module_scratch[3] = ty;   // to the contract town.
            AP_LOG("[hack] nearest town: (%d,%d) (%d steps)", tx, ty, n);
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            st->module_scratch[0], st->module_scratch[1],
                            AP_TRAVEL_WALK)) {
            AP_LOG("[hack] BFS failed walking to town (%d,%d) from (%d,%d)",
                   st->module_scratch[0], st->module_scratch[1],
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "[hack] stuck walking to nearest town");
        return SHELL_RUN_CONTINUE;

    case AP_HACK_TAKE_CONTRACT: {
        const char *active = g->contract.active_id;
        if (active[0] && strcmp(active, "hack") == 0) {
            st->phase = AP_HACK_CLOSE_TOWN_INFO_AFTER_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("[hack] contract taken: %s", active);
            return SHELL_RUN_CONTINUE;
        }
        if (active[0]) {
            AP_LOG("[hack] unexpected active contract '%s' (wanted hack)",
                   active);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_A);
            st->phase_action_queued = true;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "[hack] town didn't accept A for contract");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_CLOSE_TOWN_INFO_AFTER_CONTRACT:
        input_host_queue_key(KEY_SPACE);
        st->phase = AP_HACK_EXIT_TOWN_AFTER_CONTRACT;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_HACK_EXIT_TOWN_AFTER_CONTRACT:
        if (views_active() == VIEW_NONE) {
            st->phase = AP_HACK_LOCATE_CASTLE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] exited town, locating Hack's castle");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() == VIEW_TOWN) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "[hack] couldn't transition after town contract");
        return SHELL_RUN_CONTINUE;

    case AP_HACK_LOCATE_CASTLE: {
        int gate_x = -1, gate_y = -1;
        const char *zone = NULL;
        int idx = find_villain_castle(g, "hack", &gate_x, &gate_y, &zone);
        if (idx < 0) {
            AP_LOG("[hack] no castle found holding 'hack'");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!zone || strcmp(zone, g->position.zone) != 0) {
            AP_LOG("[hack] castle in zone '%s' but we're in '%s' — "
                   "cross-zone autoplay not yet supported",
                   zone ? zone : "(null)", g->position.zone);
            return SHELL_RUN_EXIT_FAIL;
        }
        // Stash Hack's gate permanently in scratch[8],[9].
        st->module_scratch[8] = gate_x;
        st->module_scratch[9] = gate_y;
        st->path_len = 0;
        AP_LOG("[hack] castle located at (%d,%d) in zone %s",
               gate_x, gate_y, zone);

        // Always restock before sieging Hack. The post-Murray army at
        // ~230 HP loses to Hack's tier-0 garrison; we need a fresh
        // recruit cycle at Maximus. WALK_TO_KING calls ap_ferry_tick
        // to (11,56), which handles the cross-landmass boat trip
        // automatically.
        st->ferry_state = FERRY_IDLE;
        st->phase = AP_HACK_WALK_TO_KING;
        st->phase_started_at = frame_no;
        AP_LOG("[hack] heading home to Maximus (11,56) for restock");
        return SHELL_RUN_CONTINUE;
    }


    case AP_HACK_WALK_TO_KING: {
        // Entered the home castle yet? Move on to recruit.
        if (views_active() == VIEW_HOME_CASTLE) {
            st->phase = AP_HACK_OPEN_RECRUIT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] entered king_maximus for restock");
            return SHELL_RUN_CONTINUE;
        }
        // Ferry to Maximus's gate (11,56). Ferry stops adjacent because
        // the gate is interactive; then we step N to trigger the castle.
        FerryState fs = ap_ferry_tick(st, g, m, frame_no, 11, 56);
        if (fs == FERRY_FAILED) {
            AP_LOG("[hack] ferry to Maximus failed");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (fs == FERRY_DONE) {
            // Adjacent to gate. Step onto it; engine opens the home castle.
            if (input_host_queue_depth() == 0) {
                int k = ap_dir_key(g->position.x, g->position.y, 11, 56);
                if (k != 0) input_host_queue_key(k);
            }
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 14400,
                        "[hack] stuck heading to Maximus");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_OPEN_RECRUIT:
        input_host_queue_key(KEY_A);
        st->phase = AP_HACK_DO_RECRUITS;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_HACK_DO_RECRUITS:
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            AP_LOG("[hack] recruit screen open, HP=%d gold=%d "
                   "leadership=%d", ap_army_total_hp(g), g->stats.gold,
                   g->stats.leadership_current);
            // Same shape as Murray's recruit (C/A/B + counts).
            // Pool order is militia/archers/pikemen by recruit_cost.
            // Knight leadership=100 caps per-troop, so request more than
            // we'd ever get — buy_troop silently clamps.
            input_host_queue_key(KEY_C);   // pikemen
            ap_queue_recruit_count(99);
            input_host_queue_key(KEY_A);   // militia
            ap_queue_recruit_count(99);
            input_host_queue_key(KEY_B);   // archers
            ap_queue_recruit_count(99);
            st->phase = AP_HACK_LEAVE_RECRUIT;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 300,
                        "[hack] VIEW_RECRUIT_SOLDIERS never opened");
        return SHELL_RUN_CONTINUE;

    case AP_HACK_LEAVE_RECRUIT: {
        // The recruit queue from DO_RECRUITS is processed asynchronously
        // by the recruit screen. Knight leadership=100 means we can't
        // hit a fixed HP target — total HP across troop types can exceed
        // 100 since each troop type is capped independently, but how
        // much we get depends on what's already in the army. Wait until
        // the recruit queue has been fully drained AND a few frames
        // have passed without gold/HP changing, then ESC out.
        int hp_now = ap_army_total_hp(g);
        int gold_now = g->stats.gold;
        // scratch[13] = last_hp, scratch[14] = last_gold,
        // scratch[15] = stable-frame-count
        if (st->module_scratch[13] != hp_now ||
            st->module_scratch[14] != gold_now) {
            st->module_scratch[13] = hp_now;
            st->module_scratch[14] = gold_now;
            st->module_scratch[15] = frame_no;
        }
        if (input_host_queue_depth() == 0 &&
            frame_no - st->module_scratch[15] >= 30) {
            input_host_queue_key(KEY_ESCAPE);
            st->phase = AP_HACK_LEAVE_KING;
            st->phase_started_at = frame_no;
            AP_LOG("[hack] restock done, army HP=%d gold=%d",
                   hp_now, gold_now);
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 1200,
                        "[hack] recruit queue never settled");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_LEAVE_KING:
        if (views_active() == VIEW_NONE) {
            // Restock at Maximus done; now tour dwellings north of the
            // castle to pick up elite troops (gnomes, ghosts) before
            // heading out to Hack. Kick off the tour at index 0.
            st->module_scratch[16] = 0;
            st->module_scratch[17] = -1;
            st->module_scratch[18] = -1;
            st->module_scratch[19] = 0;
            st->ferry_state = FERRY_IDLE;
            st->phase = AP_HACK_WALK_TO_DWELLING;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] exited king_maximus, starting dwelling tour");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            input_host_queue_key(KEY_ESCAPE);
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "[hack] couldn't exit king_maximus after restock");
        return SHELL_RUN_CONTINUE;

    // ---- Dwelling tour ---------------------------------------------------
    // Walk to each waypoint in turn, recruit max-affordable at the
    // dwelling's tile, then advance to the next. When done, fall through
    // to the boat-to-Hack phases. The waypoint list is seed=1-specific
    // and hand-picked for high-HP-per-leadership troops.

    case AP_HACK_WALK_TO_DWELLING: {
        // Hard-coded waypoint list (x,y) tuned for seed=1 dwellings on
        // continentia, north of king_maximus, ordered by walk distance:
        //   index 0 → gnomes  (21,37)   5 HP, 60g
        //   index 1 → skeletons (15,44) 4 HP — undead, immune to morale
        //   index 2 → ghosts  (5,29)    10 HP, skill 4, 400g (best!)
        // Stops when index runs off the end, then routes to the boat.
        static const struct { int x, y; const char *name; } tour[] = {
            { 21, 37, "gnomes" },
            { 15, 44, "skeletons" },
            { 5,  29, "ghosts" },
        };
        const int tour_n = (int)(sizeof(tour) / sizeof(tour[0]));
        int idx = st->module_scratch[16];
        if (idx >= tour_n) {
            // Tour complete — head out to Hack via the to-Hack ferry.
            st->ferry_state = FERRY_IDLE;
            st->phase = AP_HACK_WALK_TO_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] dwelling tour complete, army HP=%d gold=%d "
                   "→ heading to Hack at (%d,%d)",
                   ap_army_total_hp(g), g->stats.gold,
                   st->module_scratch[8], st->module_scratch[9]);
            return SHELL_RUN_CONTINUE;
        }
        int tx = tour[idx].x, ty = tour[idx].y;
        st->module_scratch[17] = tx;
        st->module_scratch[18] = ty;

        // Dwelling tile is interactive (INTERACT_DWELLING_*). Stepping
        // on it opens a text-input "how many to recruit?" prompt. Once
        // we're adjacent, hand off to RECRUIT_AT_DWELLING which queues
        // the count and ENTER.
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                st->phase = AP_HACK_RECRUIT_AT_DWELLING;
                st->phase_started_at = frame_no;
                st->module_scratch[19] = 0;
                AP_LOG("[hack] reached dwelling '%s' at (%d,%d) — "
                       "recruit prompt up", tour[idx].name, tx, ty);
                return SHELL_RUN_CONTINUE;
            }
            // Some other prompt (foe?). Common_prompts will handle.
            return SHELL_RUN_CONTINUE;
        }
        // Ferry to the dwelling tile. Ferry returns DONE when we're
        // adjacent (since the tile is interactive). We then step onto
        // it to trigger the prompt — handled on next per_frame above.
        FerryState fs = ap_ferry_tick(st, g, m, frame_no, tx, ty);
        if (fs == FERRY_FAILED) {
            AP_LOG("[hack] dwelling %s (%d,%d) unreachable — skipping",
                   tour[idx].name, tx, ty);
            st->module_scratch[16] = idx + 1;
            st->ferry_state = FERRY_IDLE;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        if (fs == FERRY_DONE) {
            // Adjacent to dwelling. Step onto it.
            if (input_host_queue_depth() == 0) {
                int k = ap_dir_key(g->position.x, g->position.y, tx, ty);
                if (k != 0) input_host_queue_key(k);
            }
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 86400,
                        "[hack] stuck heading to dwelling");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_RECRUIT_AT_DWELLING: {
        // Type "99" + ENTER to recruit as many as we can afford / fit.
        // The dwelling screen clamps silently if 99 > cap. If gold or
        // leadership is too low we may get 0; that's fine — we proceed.
        if (!st->module_scratch[19]) {
            ap_queue_recruit_count(99);
            st->module_scratch[19] = 1;
            return SHELL_RUN_CONTINUE;
        }
        // Wait for the prompt to close, then advance.
        if (!prompt_is_active() && input_host_queue_depth() == 0) {
            AP_LOG("[hack] dwelling recruit settled, army HP=%d gold=%d",
                   ap_army_total_hp(g), g->stats.gold);
            st->phase = AP_HACK_EXIT_DWELLING;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 600,
                        "[hack] dwelling recruit didn't settle");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_EXIT_DWELLING:
        // After recruit settles a dialog ("you bought N") may pop up.
        // ap_handle_common_prompts will SPACE it. Wait for VIEW_NONE.
        if (views_active() == VIEW_NONE && !prompt_is_active() &&
            !dialog_is_active()) {
            // Step off the dwelling tile and on to the next waypoint.
            st->module_scratch[16] += 1;
            st->ferry_state = FERRY_IDLE;
            st->path_len = 0;
            st->phase = AP_HACK_WALK_TO_DWELLING;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 600,
                        "[hack] couldn't exit dwelling view");
        return SHELL_RUN_CONTINUE;

    case AP_HACK_WALK_TO_GATE: {
        const VillainDef *vh = villain_by_id("hack");
        if (vh && g->contract.villains_caught[vh->index]) {
            st->phase = AP_HACK_VERIFY;
            st->phase_started_at = frame_no;
            AP_LOG("[hack] villains_caught[hack] set — verifying");
            return SHELL_RUN_CONTINUE;
        }
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && (strstr(hdr, "Siege") || strstr(hdr, "Castle"))) {
                st->phase = AP_HACK_SIEGE_PROMPT;
                st->phase_started_at = frame_no;
                AP_LOG("[hack] siege prompt up");
                return SHELL_RUN_CONTINUE;
            }
            return SHELL_RUN_CONTINUE;
        }
        // Ferry to Hack's gate. The gate is interactive so the ferry
        // stops adjacent; we step onto it to fire the siege prompt.
        int gx = st->module_scratch[8];
        int gy = st->module_scratch[9];
        FerryState fs = ap_ferry_tick(st, g, m, frame_no, gx, gy);
        if (fs == FERRY_FAILED) {
            AP_LOG("[hack] ferry to Hack's gate failed");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (fs == FERRY_DONE) {
            if (input_host_queue_depth() == 0) {
                int k = ap_dir_key(g->position.x, g->position.y, gx, gy);
                if (k != 0) input_host_queue_key(k);
            }
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 86400,
                        "[hack] stuck heading to Hack's gate");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_SIEGE_PROMPT:
        input_host_queue_key(KEY_Y);
        st->phase = AP_HACK_COMBAT;
        st->phase_started_at = frame_no;
        st->combat_unit_id_last_acted = -1;
        st->combat_frame_last_action = frame_no;
        AP_LOG("[hack] siege confirmed, entering combat");
        return SHELL_RUN_CONTINUE;

    case AP_HACK_COMBAT: {
        Combat *c = combat_current_rendered;
        if (!c) {
            if (st->phase_started_at + 60 < frame_no) {
                if (dialog_is_active()) {
                    st->phase = AP_HACK_POST_COMBAT_DIALOG;
                    st->phase_started_at = frame_no;
                    return SHELL_RUN_CONTINUE;
                }
            }
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 36000,
                        "[hack] combat ran too long (no progress)");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_POST_COMBAT_DIALOG:
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            return SHELL_RUN_CONTINUE;
        }
        st->phase = AP_HACK_VERIFY;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_HACK_VERIFY: {
        const VillainDef *vh = villain_by_id("hack");
        if (!vh) {
            AP_LOG("[hack] villain catalog missing 'hack'");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!g->contract.villains_caught[vh->index]) {
            AP_LOG("[hack] villains_caught[hack] is false; capture failed");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_save_checkpoint(g, m, f, AP_SLOT_HACK)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        printf("autoplay: captured Hack — gold=%d, "
               "army_hp=%d, pos=(%d,%d)\n",
               g->stats.gold, ap_army_total_hp(g),
               g->position.x, g->position.y);
        // Chain into the grind sweep before the next villain.
        st->phase = AP_GRIND_P1_FIRST;
        st->phase_started_at = frame_no;
        st->phase_action_queued = false;
        st->ferry_state = FERRY_IDLE;
        st->path_len = 0;
        for (size_t i = 0;
             i < sizeof(st->module_scratch) / sizeof(st->module_scratch[0]);
             i++) {
            st->module_scratch[i] = -1;
        }
        return SHELL_RUN_CONTINUE;
    }

    default:
        break;
    }
    return SHELL_RUN_EXIT_FAIL;
}
