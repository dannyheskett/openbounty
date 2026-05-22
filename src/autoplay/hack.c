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
//   [0],[1] = contract town gate (x,y)
//   [2],[3] = boat-rental town (same coords today, separate slots so
//             a future villain can split the two)
//   [4],[5] = Hack's castle gate (x,y), set by AP_HACK_LOCATE_CASTLE
//   [6],[7] = chosen sail-target land tile (x,y), set by sail planner

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
            // Skip the home-castle re-stock on seed=1: the contract
            // town is on a different landmass than king_maximus, no
            // overland path back. The post-Murray army handles Hack.
            st->phase = AP_HACK_LOCATE_CASTLE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] exited town, locating Hack's castle "
                   "(skipping re-stock; cross-landmass)");
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

    // ---- Re-stock phases (unused on seed=1) ---------------------------
    case AP_HACK_WALK_TO_KING:
    case AP_HACK_OPEN_RECRUIT:
    case AP_HACK_DO_RECRUITS:
    case AP_HACK_LEAVE_RECRUIT:
    case AP_HACK_LEAVE_KING:
        AP_LOG("[hack] re-stock phases not exercised on seed=1; phase=%d",
               (int)st->phase);
        return SHELL_RUN_EXIT_FAIL;

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
        st->module_scratch[4] = gate_x;
        st->module_scratch[5] = gate_y;
        st->path_len = 0;
        AP_LOG("[hack] castle located at (%d,%d) in zone %s",
               gate_x, gate_y, zone);
        // Decide: walk overland if possible, otherwise sail.
        ApPoint diag[AP_PATH_MAX];
        int n_walk = ap_bfs(m, g->position.x, g->position.y,
                            gate_x, gate_y, AP_TRAVEL_WALK,
                            diag, AP_PATH_MAX);
        if (n_walk > 0) {
            st->phase = AP_HACK_WALK_TO_GATE;
            AP_LOG("[hack] overland route exists (%d steps)", n_walk);
        } else {
            st->phase = AP_HACK_WALK_TO_TOWN_FOR_BOAT;
            AP_LOG("[hack] no overland route — going back to town for a boat");
        }
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_WALK_TO_TOWN_FOR_BOAT:
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_HACK_RENT_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("[hack] entered town for boat");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        // Walk back to the contract town (cached in scratch[2..3]).
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            st->module_scratch[2], st->module_scratch[3],
                            AP_TRAVEL_WALK)) {
            AP_LOG("[hack] BFS failed walking back to boat town (%d,%d)",
                   st->module_scratch[2], st->module_scratch[3]);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 3600,
                        "[hack] stuck walking back to boat town");
        return SHELL_RUN_CONTINUE;

    case AP_HACK_RENT_BOAT: {
        // We might be carrying a stale boat from a previous voyage
        // (Murray's leftover boat at the old disembark point).
        // town_do_boat with has_boat=true CANCELS the rental — so if
        // the existing boat isn't near this town, press B to cancel
        // first, then press B again on a later frame to actually rent
        // a fresh one at this town's boat slot. "Near" = within ~5
        // tiles of the town's nominal location.
        int town_x = st->module_scratch[2];
        int town_y = st->module_scratch[3];
        bool boat_is_local = false;
        if (g->boat.has_boat) {
            int dx = g->boat.x - town_x; if (dx < 0) dx = -dx;
            int dy = g->boat.y - town_y; if (dy < 0) dy = -dy;
            boat_is_local = (dx <= 5 && dy <= 5);
        }
        if (g->boat.has_boat && boat_is_local) {
            input_host_queue_key(KEY_ESCAPE);
            st->phase = AP_HACK_EXIT_TOWN_AFTER_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("[hack] using existing boat at (%d,%d), gold=%d",
                   g->boat.x, g->boat.y, g->stats.gold);
            return SHELL_RUN_CONTINUE;
        }
        // Either no boat yet, or stale boat far from this town. Press
        // B once per phase entry; phase_action_queued throttles it.
        // If we press once and boat is stale, has_boat flips to false
        // (cancel). We re-enter this case, throttle resets via the
        // EXIT_TOWN_AFTER_CONTRACT phase or — for the stale-cancel
        // path — by manually resetting on each frame.
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_B);
            st->phase_action_queued = true;
            return SHELL_RUN_CONTINUE;
        }
        // After the B was processed: if has_boat is now false and we
        // started this phase WITH a boat, we just cancelled the
        // stale one — reset and queue another B to rent fresh.
        if (!g->boat.has_boat && st->phase_started_at + 30 < frame_no) {
            st->phase_action_queued = false;
            // Loop on next frame to re-press B.
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "[hack] town didn't accept B for boat rental");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_EXIT_TOWN_AFTER_BOAT:
        if (views_active() == VIEW_NONE) {
            st->phase = AP_HACK_BOARD_BOAT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] exited town, walking to boat at (%d,%d)",
                   g->boat.x, g->boat.y);
            return SHELL_RUN_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "[hack] couldn't exit town after boat rental");
        return SHELL_RUN_CONTINUE;

    case AP_HACK_BOARD_BOAT: {
        int bx = g->boat.x, by = g->boat.y;
        if (g->travel_mode == TRAVEL_BOAT) {
            st->phase = AP_HACK_SAIL_TO_GATE_COAST;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            // Reset sail-target slots; the sail planner picks them.
            st->module_scratch[6] = -1;
            st->module_scratch[7] = -1;
            AP_LOG("[hack] boarded boat at (%d,%d), now sailing", bx, by);
            return SHELL_RUN_CONTINUE;
        }
        int dx = 0, dy = 0;
        if (bx > g->position.x) dx = 1; else if (bx < g->position.x) dx = -1;
        if (by > g->position.y) dy = 1; else if (by < g->position.y) dy = -1;
        int k = ap_dir_key(g->position.x, g->position.y,
                           g->position.x + dx, g->position.y + dy);
        if (k == 0) {
            AP_LOG("[hack] already adjacent to boat? pos=(%d,%d) boat=(%d,%d)",
                   g->position.x, g->position.y, bx, by);
            return SHELL_RUN_EXIT_FAIL;
        }
        input_host_queue_key(k);
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "[hack] couldn't board boat");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_SAIL_TO_GATE_COAST: {
        // Same shape as Murray's sail planner, parameterized by the
        // module's cached gate coords (scratch[4],[5]) instead of
        // hard-coded azram.
        int gx = st->module_scratch[4];
        int gy = st->module_scratch[5];

        // Diagnostic trace: log progress periodically so we can see
        // whether we're moving, stuck on a tile, or in combat.
        if (frame_no % 300 == 0) {
            ApPoint nxt = (st->path_idx < st->path_len) ?
                st->path[st->path_idx] : (ApPoint){-1,-1};
            fprintf(stderr,
                    "[autoplay] [hack] sail f=%d hero=(%d,%d) "
                    "boat=(%d,%d) travel=%d path[%d/%d] next=(%d,%d) "
                    "tgt=(%d,%d) dlg=%d prompt=%d in_combat=%d "
                    "view=%d qdepth=%d\n",
                    frame_no,
                    g->position.x, g->position.y,
                    g->boat.x, g->boat.y, g->travel_mode,
                    st->path_idx, st->path_len, nxt.x, nxt.y,
                    st->path_target_x, st->path_target_y,
                    dialog_is_active(), prompt_is_active(),
                    combat_current_rendered ? 1 : 0,
                    (int)views_active(), input_host_queue_depth());
        }

        // Defense in depth: if any view (town, castle, dwelling, ...)
        // opens mid-sail, ESC out of it and re-plan. ap_bfs is supposed
        // to route around interactive tiles, but this catches future
        // map changes / unseen edge cases before they wedge the run.
        if (views_active() != VIEW_NONE) {
            if (input_host_queue_depth() == 0) {
                input_host_queue_key(KEY_ESCAPE);
            }
            st->path_len = 0;
            AP_TIMEOUT_FAIL(st->phase_started_at, 14400,
                            "[hack] stuck in unexpected view during sail");
            return SHELL_RUN_CONTINUE;
        }

        if (g->travel_mode == TRAVEL_WALK) {
            int adx = g->position.x - gx; if (adx < 0) adx = -adx;
            int ady = g->position.y - gy; if (ady < 0) ady = -ady;
            ApPoint walk_diag[AP_PATH_MAX];
            int n_walk_to_gate = (adx + ady <= 25)
                ? ap_bfs(m, g->position.x, g->position.y, gx, gy,
                         AP_TRAVEL_WALK, walk_diag, AP_PATH_MAX)
                : 0;
            if (n_walk_to_gate > 0) {
                st->phase = AP_HACK_WALK_TO_GATE;
                st->phase_started_at = frame_no;
                st->path_len = 0;
                AP_LOG("[hack] disembarked at (%d,%d), gate reachable on "
                       "foot (%d steps)",
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
                                "[hack] stuck reboarding boat");
                return SHELL_RUN_CONTINUE;
            }
            st->phase = AP_HACK_WALK_TO_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] disembarked at (%d,%d) and no boat — fall through",
                   g->position.x, g->position.y);
            return SHELL_RUN_CONTINUE;
        }

        // Plan once: find the best land tile near (gx,gy) that's
        // sailable from current pos and has a walkable path to the
        // gate.
        if (st->path_len == 0) {
            int best_total = 1 << 30;
            int best_x = -1, best_y = -1;
            ApPoint sail_path[AP_PATH_MAX];
            ApPoint walk_path[AP_PATH_MAX];
            for (int dy = -12; dy <= 12; dy++) {
                for (int dx = -12; dx <= 12; dx++) {
                    int tx = gx + dx;
                    int ty = gy + dy;
                    if (!MapInBounds(m, tx, ty)) continue;
                    const Tile *t = MapGetTile(m, tx, ty);
                    if (!t) continue;
                    if (!TerrainWalkable(t->terrain) || t->blocks_foot) continue;
                    if (t->interactive != INTERACT_NONE) continue;
                    int n_walk = ap_bfs(m, tx, ty, gx, gy,
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
                AP_LOG("[hack] no sailable approach to gate (%d,%d) "
                       "from (%d,%d)",
                       gx, gy, g->position.x, g->position.y);
                return SHELL_RUN_EXIT_FAIL;
            }
            st->module_scratch[6] = best_x;
            st->module_scratch[7] = best_y;
            AP_LOG("[hack] sailing to land (%d,%d) "
                   "(sail+walk=%d steps)", best_x, best_y, best_total);
        }
        if (g->position.x == st->path_target_x &&
            g->position.y == st->path_target_y) {
            st->phase = AP_HACK_DISEMBARK_NEAR_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            st->path_target_x, st->path_target_y,
                            AP_TRAVEL_BOAT)) {
            AP_LOG("[hack] sail: BFS recompute failed from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            AP_LOG("[hack] sailing step failed at (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 72000,
                        "[hack] stuck sailing to gate coast");
        return SHELL_RUN_CONTINUE;
    }

    case AP_HACK_DISEMBARK_NEAR_GATE: {
        int gx = st->module_scratch[4];
        int gy = st->module_scratch[5];
        if (g->travel_mode != TRAVEL_BOAT) {
            st->phase = AP_HACK_WALK_TO_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("[hack] disembarked at (%d,%d)",
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
            int adx = nx - gx; if (adx < 0) adx = -adx;
            int ady = ny - gy; if (ady < 0) ady = -ady;
            int d = adx + ady;
            if (d < best_d) {
                best_d = d;
                chosen_k = ap_dir_key(g->position.x, g->position.y,
                                      nx, ny);
            }
        }
        if (chosen_k == 0) {
            AP_LOG("[hack] no adjacent land tile to disembark from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        input_host_queue_key(chosen_k);
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "[hack] couldn't disembark");
        return SHELL_RUN_CONTINUE;
    }

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
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        int gx = st->module_scratch[4];
        int gy = st->module_scratch[5];
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            gx, gy, AP_TRAVEL_WALK)) {
            AP_LOG("[hack] BFS failed walking to gate (%d,%d) from (%d,%d)",
                   gx, gy, g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 14400,
                        "[hack] stuck walking to Hack's castle");
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
        if (!ap_save_checkpoint(g, m, f)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        printf("autoplay: captured Hack — gold=%d, "
               "army_hp=%d, pos=(%d,%d)\n",
               g->stats.gold, ap_army_total_hp(g),
               g->position.x, g->position.y);
        st->phase = AP_ALL_DONE;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;
    }

    default:
        break;
    }
    return SHELL_RUN_EXIT_FAIL;
}
