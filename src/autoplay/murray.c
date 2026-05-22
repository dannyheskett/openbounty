// Murray module: capture Murray the Miser from a fresh seed-1 start.
//
// Path on seed=1, knight starting at (11,58) in continentia:
//   1. dismiss the godlike-actions intro dialog
//   2. walk 2 north to king_maximus (11,56) and recruit the
//      Murray-beating recipe (pikemen×10 + militia×30 + archers×8 →
//      300 HP, 1000 gold left)
//   3. walk south to hunterville town (12,60) — closest town to
//      spawn — and press A to take the next contract; on seed=1
//      this is "murray"
//   4. press B in hunterville to rent the boat (cost 500g, boat
//      spawns at (11,60))
//   5. board the boat, BFS-sail across continentia's south sea to
//      a land tile near azram (~58 steps), disembark
//   6. walk to azram gate (30,36), step onto it → siege prompt
//   7. press Y → rendered combat. Combat input scripted by
//      core.c's autoplay_before_frame (per-unit shoot/melee heuristic)
//   8. dismiss the victory + capture dialogs, verify
//      villains_caught[murray]=true, save the checkpoint, and chain
//      directly into the Hack module's first phase
//
// The state struct, BFS helpers, combat scripting, and common-prompt
// handler all live in core.c; this file owns only the phase machine
// and the transitions.

#include "autoplay/internal.h"

#include "input_host.h"
#include "tables.h"
#include "combat.h"
#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stdio.h>
#include <string.h>

ShellRunVerdict ap_murray_per_frame(Game *g, Map *m, Fog *f,
                                    Resources *res, int frame_no,
                                    AutoplayState *st) {
    (void)f; (void)res;

    switch (st->phase) {
    case AP_MURRAY_DISMISS_INTRO:
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            st->phase = AP_MURRAY_WALK_TO_KING;
            st->phase_started_at = frame_no;
            AP_LOG("intro dismissed, walking to king_maximus to recruit");
        }
        AP_TIMEOUT_FAIL(0, 1200, "intro dialog never appeared");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_WALK_TO_KING:
        if (views_active() == VIEW_HOME_CASTLE) {
            st->phase = AP_MURRAY_OPEN_RECRUIT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("entered king_maximus");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            11, 56, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed: pos->king_maximus (11,56)");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            AP_LOG("step failed walking to king_maximus");
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 1800,
                        "stuck walking to king_maximus");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_OPEN_RECRUIT:
        input_host_queue_key(KEY_A);
        st->phase = AP_MURRAY_DO_RECRUITS;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_DO_RECRUITS:
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            input_host_queue_key(KEY_C);   // pikemen
            ap_queue_recruit_count(10);
            input_host_queue_key(KEY_A);   // militia
            ap_queue_recruit_count(30);
            input_host_queue_key(KEY_B);   // archers
            ap_queue_recruit_count(8);
            st->phase = AP_MURRAY_LEAVE_RECRUIT;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 300,
                        "VIEW_RECRUIT_SOLDIERS never opened");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_LEAVE_RECRUIT:
        if (ap_army_total_hp(g) >= 300) {
            input_host_queue_key(KEY_ESCAPE);
            st->phase = AP_MURRAY_LEAVE_KING;
            st->phase_started_at = frame_no;
            AP_LOG("recruits done, army HP=%d gold=%d",
                   ap_army_total_hp(g), g->stats.gold);
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 600,
                        "army HP never reached 300");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_LEAVE_KING:
        if (views_active() == VIEW_NONE) {
            st->phase = AP_MURRAY_WALK_TO_TOWN_FOR_CONTRACT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("exited king_maximus, walking to a town for contract");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            input_host_queue_key(KEY_ESCAPE);
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't exit king_maximus");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_WALK_TO_TOWN_FOR_CONTRACT:
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_MURRAY_TAKE_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            st->path_len = 0;
            AP_LOG("entered town (contract)");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        // Hunterville at (12,60) is the closest town to spawn.
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            12, 60, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed: pos->hunterville (12,60) from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            AP_LOG("step failed walking to hunterville (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 3600,
                        "stuck walking to hunterville");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_TAKE_CONTRACT: {
        const char *active = g->contract.active_id;
        if (active[0] && strcmp(active, "murray") == 0) {
            st->phase = AP_MURRAY_CLOSE_TOWN_INFO_AFTER_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("contract taken: %s", active);
            return SHELL_RUN_CONTINUE;
        }
        if (active[0]) {
            AP_LOG("unexpected active contract '%s' (wanted murray)",
                   active);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_A);
            st->phase_action_queued = true;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "town didn't accept A for contract");
        return SHELL_RUN_CONTINUE;
    }

    case AP_MURRAY_CLOSE_TOWN_INFO_AFTER_CONTRACT:
        input_host_queue_key(KEY_SPACE);
        st->phase = AP_MURRAY_EXIT_TOWN_AFTER_CONTRACT;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_EXIT_TOWN_AFTER_CONTRACT:
        if (views_active() == VIEW_NONE) {
            st->phase = AP_MURRAY_WALK_TO_TOWN_FOR_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            st->path_len = 0;
            AP_LOG("exited town after contract, walking back for boat");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_MURRAY_RENT_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("in town after contract, renting boat");
            return SHELL_RUN_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't transition after town contract");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_WALK_TO_TOWN_FOR_BOAT:
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_MURRAY_RENT_BOAT;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            12, 60, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed back to hunterville from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 3600,
                        "stuck walking back to hunterville (boat)");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_RENT_BOAT:
        if (g->boat.has_boat) {
            input_host_queue_key(KEY_ESCAPE);
            st->phase = AP_MURRAY_EXIT_TOWN_AFTER_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("boat rented, gold=%d boat=(%d,%d)",
                   g->stats.gold, g->boat.x, g->boat.y);
            return SHELL_RUN_CONTINUE;
        }
        // Queue exactly ONE B per phase entry. has_boat toggles on
        // every B press, so a second pending B would CANCEL the rental.
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_B);
            st->phase_action_queued = true;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "town didn't accept B for boat rental");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_EXIT_TOWN_AFTER_BOAT:
        if (views_active() == VIEW_NONE) {
            st->phase = AP_MURRAY_BOARD_BOAT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("exited town, walking to boat at (%d,%d)",
                   g->boat.x, g->boat.y);
            return SHELL_RUN_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't exit town after boat rental");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_BOARD_BOAT: {
        int bx = g->boat.x, by = g->boat.y;
        if (g->travel_mode == TRAVEL_BOAT) {
            st->phase = AP_MURRAY_SAIL_TO_AZRAM_COAST;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("boarded boat at (%d,%d), now sailing", bx, by);
            return SHELL_RUN_CONTINUE;
        }
        int dx = 0, dy = 0;
        if (bx > g->position.x) dx = 1; else if (bx < g->position.x) dx = -1;
        if (by > g->position.y) dy = 1; else if (by < g->position.y) dy = -1;
        int k = ap_dir_key(g->position.x, g->position.y,
                           g->position.x + dx, g->position.y + dy);
        if (k == 0) {
            AP_LOG("already adjacent to boat? pos=(%d,%d) boat=(%d,%d)",
                   g->position.x, g->position.y, bx, by);
            return SHELL_RUN_EXIT_FAIL;
        }
        input_host_queue_key(k);
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "couldn't board boat");
        return SHELL_RUN_CONTINUE;
    }

    case AP_MURRAY_SAIL_TO_AZRAM_COAST: {
        // If we disembarked mid-sail (combat bounce, BFS picked a
        // land tile en route): close enough to azram → walk; too
        // far → re-board.
        if (g->travel_mode == TRAVEL_WALK) {
            int adx = g->position.x - 30; if (adx < 0) adx = -adx;
            int ady = g->position.y - 37; if (ady < 0) ady = -ady;
            ApPoint walk_diag[AP_PATH_MAX];
            int n_walk_to_gate = (adx + ady <= 25)
                ? ap_bfs(m, g->position.x, g->position.y, 30, 37,
                         AP_TRAVEL_WALK, walk_diag, AP_PATH_MAX)
                : 0;
            if (n_walk_to_gate > 0) {
                st->phase = AP_MURRAY_WALK_TO_AZRAM_GATE;
                st->phase_started_at = frame_no;
                st->path_len = 0;
                AP_LOG("disembarked at (%d,%d), "
                       "azram reachable on foot (%d steps), walking",
                       g->position.x, g->position.y, n_walk_to_gate);
                return SHELL_RUN_CONTINUE;
            }
            if (g->boat.has_boat && g->boat.x >= 0) {
                int dx = 0, dy = 0;
                if (g->boat.x > g->position.x) dx = 1;
                else if (g->boat.x < g->position.x) dx = -1;
                if (g->boat.y > g->position.y) dy = 1;
                else if (g->boat.y < g->position.y) dy = -1;
                int k = ap_dir_key(g->position.x, g->position.y,
                                   g->position.x + dx,
                                   g->position.y + dy);
                if (k != 0 && input_host_queue_depth() == 0) {
                    input_host_queue_key(k);
                }
                AP_TIMEOUT_FAIL(st->phase_started_at, 14400,
                                "stuck reboarding boat after disembark");
                return SHELL_RUN_CONTINUE;
            }
            st->phase = AP_MURRAY_WALK_TO_AZRAM_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("disembarked at (%d,%d) and no boat — fall through "
                   "to overland walk attempt",
                   g->position.x, g->position.y);
            return SHELL_RUN_CONTINUE;
        }
        // Plan: sail directly to a LAND tile near azram. Stepping
        // from water onto land in boat mode auto-disembarks at the
        // destination tile. Then a short walk reaches the gate.
        int best_x = -1, best_y = -1;
        if (st->path_len == 0) {
            int azram_gate_x = 30, azram_gate_y = 36;
            int best_total = 1 << 30;
            ApPoint sail_path[AP_PATH_MAX];
            ApPoint walk_path[AP_PATH_MAX];
            for (int dy = -12; dy <= 12; dy++) {
                for (int dx = -12; dx <= 12; dx++) {
                    int tx = azram_gate_x + dx;
                    int ty = azram_gate_y + dy;
                    if (!MapInBounds(m, tx, ty)) continue;
                    const Tile *t = MapGetTile(m, tx, ty);
                    if (!t) continue;
                    if (!TerrainWalkable(t->terrain) || t->blocks_foot) continue;
                    if (t->interactive != INTERACT_NONE) continue;
                    int n_walk = ap_bfs(m, tx, ty,
                                        azram_gate_x, azram_gate_y,
                                        AP_TRAVEL_WALK,
                                        walk_path, AP_PATH_MAX);
                    if (n_walk <= 0) continue;
                    int n_sail = ap_bfs(m, g->position.x, g->position.y,
                                        tx, ty, AP_TRAVEL_BOAT,
                                        sail_path, AP_PATH_MAX);
                    if (n_sail <= 0) continue;
                    int total = n_sail + n_walk;
                    if (total < best_total) {
                        best_total = total;
                        best_x = tx;
                        best_y = ty;
                        memcpy(st->path, sail_path,
                               sizeof(ApPoint) * (size_t)n_sail);
                        st->path_len = n_sail;
                        st->path_idx = 0;
                        st->path_target_x = tx;
                        st->path_target_y = ty;
                    }
                }
            }
            if (best_x < 0) {
                AP_LOG("no sailable approach to azram gate from (%d,%d)",
                       g->position.x, g->position.y);
                return SHELL_RUN_EXIT_FAIL;
            }
            AP_LOG("sailing to land (%d,%d) (sail+walk=%d steps)",
                   best_x, best_y, best_total);
        }
        if (g->position.x == st->path_target_x &&
            g->position.y == st->path_target_y) {
            st->phase = AP_MURRAY_DISEMBARK_NEAR_AZRAM;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            st->path_target_x, st->path_target_y,
                            AP_TRAVEL_BOAT)) {
            AP_LOG("sail: BFS recompute failed from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            AP_LOG("sailing step failed at (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "stuck sailing to azram coast");
        return SHELL_RUN_CONTINUE;
    }

    case AP_MURRAY_DISEMBARK_NEAR_AZRAM: {
        if (g->travel_mode != TRAVEL_BOAT) {
            st->phase = AP_MURRAY_WALK_TO_AZRAM_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("disembarked at (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_CONTINUE;
        }
        static const int dxs[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
        static const int dys[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
        int chosen_k = 0;
        int best_d = 1 << 30;
        for (int i = 0; i < 8; i++) {
            int nx = g->position.x + dxs[i];
            int ny = g->position.y + dys[i];
            if (!MapInBounds(m, nx, ny)) continue;
            const Tile *t = MapGetTile(m, nx, ny);
            if (!t || !TerrainWalkable(t->terrain) || t->blocks_foot) {
                continue;
            }
            int adx = nx - 30; if (adx < 0) adx = -adx;
            int ady = ny - 37; if (ady < 0) ady = -ady;
            int d = adx + ady;
            if (d < best_d) {
                best_d = d;
                chosen_k = ap_dir_key(g->position.x, g->position.y,
                                      nx, ny);
            }
        }
        if (chosen_k == 0) {
            AP_LOG("no adjacent land tile to disembark from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        input_host_queue_key(chosen_k);
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "couldn't disembark");
        return SHELL_RUN_CONTINUE;
    }

    case AP_MURRAY_WALK_TO_AZRAM_GATE: {
        const VillainDef *vm = villain_by_id("murray");
        if (vm && g->contract.villains_caught[vm->index]) {
            st->phase = AP_MURRAY_VERIFY;
            st->phase_started_at = frame_no;
            AP_LOG("villains_caught[murray] set — verifying");
            return SHELL_RUN_CONTINUE;
        }
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && strstr(hdr, "Siege")) {
                st->phase = AP_MURRAY_SIEGE_PROMPT;
                st->phase_started_at = frame_no;
                AP_LOG("siege prompt up");
                return SHELL_RUN_CONTINUE;
            }
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            30, 36, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed walking to azram gate from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "stuck walking to azram gate");
        return SHELL_RUN_CONTINUE;
    }

    case AP_MURRAY_SIEGE_PROMPT:
        input_host_queue_key(KEY_Y);
        st->phase = AP_MURRAY_COMBAT;
        st->phase_started_at = frame_no;
        st->combat_unit_id_last_acted = -1;
        st->combat_frame_last_action = frame_no;
        AP_LOG("siege confirmed, entering combat");
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_COMBAT: {
        Combat *c = combat_current_rendered;
        if (!c) {
            if (st->phase_started_at + 60 < frame_no) {
                if (dialog_is_active()) {
                    st->phase = AP_MURRAY_POST_COMBAT_DIALOG;
                    st->phase_started_at = frame_no;
                    return SHELL_RUN_CONTINUE;
                }
            }
            return SHELL_RUN_CONTINUE;
        }
        // Combat input is driven by core.c's autoplay_before_frame.
        // Here we just wait for combat to end.
        AP_TIMEOUT_FAIL(st->phase_started_at, 36000,
                        "combat ran too long (no progress)");
        return SHELL_RUN_CONTINUE;
    }

    case AP_MURRAY_POST_COMBAT_DIALOG:
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            return SHELL_RUN_CONTINUE;
        }
        st->phase = AP_MURRAY_VERIFY;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_MURRAY_VERIFY: {
        const VillainDef *vm = villain_by_id("murray");
        if (!vm) {
            AP_LOG("villain catalog missing murray");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!g->contract.villains_caught[vm->index]) {
            AP_LOG("villains_caught[murray] is false; capture failed");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_save_checkpoint(g, m, f, AP_SLOT_MURRAY)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        printf("autoplay: captured Murray — gold=%d, "
               "army_hp=%d, pos=(%d,%d)\n",
               g->stats.gold, ap_army_total_hp(g),
               g->position.x, g->position.y);
        // One-shot diagnostic: scan continentia for dwellings + chests
        // so we can pick prep waypoints. Logs once at Murray verify.
        AP_LOG("=== continentia dwelling scan ===");
        for (int y = 0; y < m->height; y++) {
            for (int x = 0; x < m->width; x++) {
                const Tile *t = MapGetTile(m, x, y);
                if (!t) continue;
                const char *kind = NULL;
                switch (t->interactive) {
                    case INTERACT_DWELLING_PLAINS:  kind = "plains";  break;
                    case INTERACT_DWELLING_FOREST:  kind = "forest";  break;
                    case INTERACT_DWELLING_HILLS:   kind = "hills";   break;
                    case INTERACT_DWELLING_DUNGEON: kind = "dungeon"; break;
                    default: continue;
                }
                fprintf(stderr,
                        "[autoplay] dwelling: (%d,%d) kind=%s art='%s' id='%s'\n",
                        x, y, kind, t->art, t->id);
            }
        }
        AP_LOG("=== continentia chest scan ===");
        for (int y = 0; y < m->height; y++) {
            for (int x = 0; x < m->width; x++) {
                const Tile *t = MapGetTile(m, x, y);
                if (!t) continue;
                if (t->interactive == INTERACT_TREASURE_CHEST) {
                    fprintf(stderr,
                            "[autoplay] chest: (%d,%d) id='%s'\n",
                            x, y, t->id);
                }
            }
        }
        AP_LOG("=== scan done ===");
        // Chain into the next module: Hack.
        st->phase = AP_HACK_FIRST;
        st->phase_started_at = frame_no;
        st->phase_action_queued = false;
        st->path_len = 0;
        // Reset module scratch — Hack uses these slots for its own
        // ad-hoc state. -1 is the "not set yet" sentinel; valid map
        // coords are always >= 0.
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
