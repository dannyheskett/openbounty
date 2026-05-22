// Grind phase 1: sweep continentia for treasure chests and artifacts
// to build up gold (and any incidental leadership / spells / troop
// joins) before the next villain. Runs after Hack capture.
//
// Strategy: scan the map each iteration for any remaining
// INTERACT_TREASURE_CHEST or INTERACT_ARTIFACT tile, pick the nearest
// one by Chebyshev distance, and ferry to it via ap_ferry_tick. The
// pickup interaction (chest A/B prompt, artifact dialog, etc.) is
// auto-handled by ap_handle_common_prompts in core.c — it presses A
// for chest gold-or-leadership and SPACEs through artifact dialogs.
//
// Termination: the scan returns no targets (map cleared, or the
// remaining ones are completely unreachable). To avoid spinning on
// unreachable pickups, we also cap consecutive ferry failures —
// after AP_GRIND_P1_FAIL_CAP failed targets in a row, we stop.
//
// State slot layout (module_scratch[]):
//   [0],[1]   = current target (x,y)
//   [2]       = total pickups completed this run
//   [3]       = consecutive ferry-failure count (cap)
//   [4]       = starting gold (for the "delta" log on exit)
//   [5]       = walk-step queued flag for the final step onto target
//   [6]       = failed-coord ring write index (0..GRIND_FAIL_LIST_MAX)
//   [7]       = failed-coord count (capped at GRIND_FAIL_LIST_MAX)
//   [8..23]   = failed-coord ring: 8 (x,y) pairs of pickups whose
//               ferry returned FAILED. Future scans skip tiles equal
//               to any entry, so we don't keep retargeting the same
//               unreachable chest.
//   [25]      = frame_no of last position change (defeat/wedge watchdog)
//   [26],[27] = last known position
//   [28..31]  = reserved
//
// Per-pickup expected loop: PICK_TARGET → WALK_TO_TARGET → (pickup
// handled by common_prompts) → PICK_TARGET … → VERIFY when scan empty
// or when too many ferries fail consecutively.

#include "autoplay/internal.h"

#include "input_host.h"
#include "tables.h"
#include "tile.h"
#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stdio.h>
#include <string.h>

#define AP_GRIND_P1_FAIL_CAP    32   // consecutive failures before giving up
#define GRIND_BLACKLIST_CAP     64

static bool grind_p1_is_failed(const AutoplayState *st, int x, int y) {
    int n = st->grind_blacklist_count;
    if (n < 0) n = 0;
    if (n > GRIND_BLACKLIST_CAP) n = GRIND_BLACKLIST_CAP;
    for (int i = 0; i < n; i++) {
        if (st->grind_blacklist_x[i] == x &&
            st->grind_blacklist_y[i] == y) return true;
    }
    return false;
}

static void grind_p1_remember_failed(AutoplayState *st, int x, int y) {
    // Already in the list? No-op.
    if (grind_p1_is_failed(st, x, y)) return;
    int n = st->grind_blacklist_count;
    if (n < 0) n = 0;
    if (n >= GRIND_BLACKLIST_CAP) {
        // Full — overwrite oldest (slot 0); shift down.
        for (int i = 1; i < GRIND_BLACKLIST_CAP; i++) {
            st->grind_blacklist_x[i - 1] = st->grind_blacklist_x[i];
            st->grind_blacklist_y[i - 1] = st->grind_blacklist_y[i];
        }
        st->grind_blacklist_x[GRIND_BLACKLIST_CAP - 1] = x;
        st->grind_blacklist_y[GRIND_BLACKLIST_CAP - 1] = y;
        return;
    }
    st->grind_blacklist_x[n] = x;
    st->grind_blacklist_y[n] = y;
    st->grind_blacklist_count = n + 1;
}

// Scan the current zone's map for the nearest unclaimed pickup tile
// (chest or artifact) from (cx,cy). Prefers overland-reachable
// targets (Chebyshev distance among those that have an overland
// walk path); if none are overland-reachable, falls back to nearest
// remaining anywhere (caller will ferry). Tiles in the failed-list
// are skipped so we don't retarget the same unreachable pickup.
static bool grind_p1_find_nearest_pickup(const Map *m,
                                         const AutoplayState *st,
                                         int cx, int cy,
                                         int *out_x, int *out_y) {
    int best_overland_x = -1, best_overland_y = -1;
    int best_overland_d = 1 << 30;
    int best_any_x = -1, best_any_y = -1;
    int best_any_d = 1 << 30;
    ApPoint tmp[AP_PATH_MAX];
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = MapGetTile(m, x, y);
            if (!t) continue;
            if (t->interactive != INTERACT_TREASURE_CHEST &&
                t->interactive != INTERACT_ARTIFACT) {
                continue;
            }
            if (grind_p1_is_failed(st, x, y)) continue;
            int adx = x - cx; if (adx < 0) adx = -adx;
            int ady = y - cy; if (ady < 0) ady = -ady;
            int d = (adx > ady) ? adx : ady;
            if (d < best_any_d) {
                best_any_d = d;
                best_any_x = x;
                best_any_y = y;
            }
            // Overland reachability is cheap to check cumulatively for
            // candidates within the "best so far" budget — skip the
            // BFS if this can't possibly beat the current overland
            // best by Chebyshev alone.
            if (d >= best_overland_d) continue;
            int n = ap_bfs(m, cx, cy, x, y, AP_TRAVEL_WALK,
                           tmp, AP_PATH_MAX);
            if (n > 0 && n < best_overland_d) {
                best_overland_d = n;
                best_overland_x = x;
                best_overland_y = y;
            }
        }
    }
    if (best_overland_x >= 0) {
        if (out_x) *out_x = best_overland_x;
        if (out_y) *out_y = best_overland_y;
        return true;
    }
    if (best_any_x >= 0) {
        if (out_x) *out_x = best_any_x;
        if (out_y) *out_y = best_any_y;
        return true;
    }
    return false;
}

ShellRunVerdict ap_grind_p1_per_frame(Game *g, Map *m, Fog *f,
                                      Resources *res, int frame_no,
                                      AutoplayState *st) {
    (void)f; (void)res;

    switch (st->phase) {

    case AP_GRIND_P1_PICK_TARGET: {
        if (st->module_scratch[4] < 0) {
            // First entry: initialize the state slots we use.
            st->module_scratch[2] = 0;       // pickups completed
            st->module_scratch[3] = 0;       // consecutive fails
            st->module_scratch[4] = g->stats.gold;
            // Reset blacklist for this sweep.
            st->grind_blacklist_count = 0;
            for (int i = 0; i < GRIND_BLACKLIST_CAP; i++) {
                st->grind_blacklist_x[i] = -1;
                st->grind_blacklist_y[i] = -1;
            }
            st->module_scratch[6] = 0;       // failed-coord write idx
            st->module_scratch[7] = 0;       // failed-coord count
            AP_LOG("[grind-p1] sweep starting from (%d,%d), gold=%d",
                   g->position.x, g->position.y, g->stats.gold);
        }
        if (st->module_scratch[3] >= AP_GRIND_P1_FAIL_CAP) {
            AP_LOG("[grind-p1] %d failures in a row — stopping sweep",
                   st->module_scratch[3]);
            st->phase = AP_GRIND_P1_VERIFY;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        int tx, ty;
        if (!grind_p1_find_nearest_pickup(m, st,
                                          g->position.x, g->position.y,
                                          &tx, &ty)) {
            AP_LOG("[grind-p1] no reachable pickups remain");
            st->phase = AP_GRIND_P1_VERIFY;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        const Tile *tt = MapGetTile(m, tx, ty);
        const char *kind = (tt && tt->interactive == INTERACT_ARTIFACT)
            ? "artifact" : "chest";
        AP_LOG("[grind-p1] target #%d: %s at (%d,%d), gold=%d",
               st->module_scratch[2] + 1, kind, tx, ty, g->stats.gold);
        st->module_scratch[0] = tx;
        st->module_scratch[1] = ty;
        st->module_scratch[5] = 0;
        st->module_scratch[25] = frame_no;
        st->module_scratch[26] = g->position.x;
        st->module_scratch[27] = g->position.y;
        st->ferry_state = FERRY_IDLE;
        st->phase = AP_GRIND_P1_WALK_TO_TARGET;
        st->phase_started_at = frame_no;
        st->path_len = 0;
        return SHELL_RUN_CONTINUE;
    }

    case AP_GRIND_P1_WALK_TO_TARGET: {
        int tx = st->module_scratch[0];
        int ty = st->module_scratch[1];
        // The pickup tile may have been claimed by walking onto it
        // (the engine clears the interactive flag after the dialog
        // dismisses). When that happens, advance to the next pickup.
        const Tile *tt = MapGetTile(m, tx, ty);
        bool still_pickup = tt &&
            (tt->interactive == INTERACT_TREASURE_CHEST ||
             tt->interactive == INTERACT_ARTIFACT);
        if (!still_pickup) {
            st->module_scratch[2] += 1;
            st->module_scratch[3] = 0;
            AP_LOG("[grind-p1] pickup #%d claimed at (%d,%d), gold=%d",
                   st->module_scratch[2], tx, ty, g->stats.gold);
            st->phase = AP_GRIND_P1_PICK_TARGET;
            st->phase_started_at = frame_no;
            st->ferry_state = FERRY_IDLE;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        // Position-change watchdog: if the hero hasn't moved for a
        // while, the current target is wedged (foe loop, combat-loss
        // teleport, etc). Blacklist it and pick a new target.
        // scratch[26],[27] = last known position;
        // scratch[25]       = frame_no of last position change.
        if (st->module_scratch[26] != g->position.x ||
            st->module_scratch[27] != g->position.y) {
            // Defeat teleport detection: if the hero "moved" but ended
            // up at the home spawn (11,58 on continentia) far from the
            // target, that's a temp_death — blacklist the target and
            // restart with a different one. Also check for the
            // 20-peasants-only army signature.
            int dx = g->position.x - st->module_scratch[26];
            int dy = g->position.y - st->module_scratch[27];
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            bool big_jump = (dx > 2 || dy > 2);
            bool at_home  = (g->position.x == 11 && g->position.y == 58);
            int peasants = 0, other = 0;
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
                if (strcmp(g->army[i].id, "peasants") == 0) {
                    peasants = g->army[i].count;
                } else {
                    other += g->army[i].count;
                }
            }
            bool peasants_only = (other == 0 && peasants > 0);
            if (big_jump && at_home && peasants_only) {
                AP_LOG("[grind-p1] DEFEAT detected (teleport to home, "
                       "20 peasants only) — blacklisting (%d,%d)",
                       tx, ty);
                grind_p1_remember_failed(st, tx, ty);
                st->module_scratch[3] += 1;
                st->phase = AP_GRIND_P1_PICK_TARGET;
                st->phase_started_at = frame_no;
                st->module_scratch[25] = frame_no;
                st->module_scratch[26] = g->position.x;
                st->module_scratch[27] = g->position.y;
                st->ferry_state = FERRY_IDLE;
                st->path_len = 0;
                return SHELL_RUN_CONTINUE;
            }
            st->module_scratch[26] = g->position.x;
            st->module_scratch[27] = g->position.y;
            st->module_scratch[25] = frame_no;
        }
        if (frame_no - st->module_scratch[25] > 1800) {
            AP_LOG("[grind-p1] target (%d,%d) wedged at (%d,%d) — "
                   "blacklisting and skipping",
                   tx, ty, g->position.x, g->position.y);
            grind_p1_remember_failed(st, tx, ty);
            st->module_scratch[3] += 1;
            st->phase = AP_GRIND_P1_PICK_TARGET;
            st->phase_started_at = frame_no;
            st->module_scratch[25] = frame_no;
            st->ferry_state = FERRY_IDLE;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        // Wait for any open prompt/dialog to settle — that's the chest
        // or artifact pickup interaction firing, handled by
        // ap_handle_common_prompts.
        if (prompt_is_active() || dialog_is_active()) {
            return SHELL_RUN_CONTINUE;
        }
        // Ferry to the pickup tile. Pickups are interactive, so ferry
        // stops adjacent; we step onto the tile to fire the pickup.
        FerryState fs = ap_ferry_tick(st, g, m, frame_no, tx, ty);
        if (fs == FERRY_FAILED) {
            AP_LOG("[grind-p1] ferry to (%d,%d) failed — skipping",
                   tx, ty);
            grind_p1_remember_failed(st, tx, ty);
            st->module_scratch[3] += 1;
            st->phase = AP_GRIND_P1_PICK_TARGET;
            st->phase_started_at = frame_no;
            st->ferry_state = FERRY_IDLE;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        if (fs == FERRY_DONE) {
            // Adjacent to target. Step onto it; engine handles the
            // chest/artifact dialog automatically next frame.
            if (input_host_queue_depth() == 0 && !st->module_scratch[5]) {
                int k = ap_dir_key(g->position.x, g->position.y, tx, ty);
                if (k != 0) {
                    input_host_queue_key(k);
                    st->module_scratch[5] = 1;
                }
            }
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "[grind-p1] stuck heading to pickup");
        return SHELL_RUN_CONTINUE;
    }

    case AP_GRIND_P1_VERIFY: {
        int delta = g->stats.gold - st->module_scratch[4];
        printf("autoplay: grind P1 swept %d pickups — "
               "gold %d → %d (Δ%+d), army_hp=%d, pos=(%d,%d)\n",
               st->module_scratch[2],
               st->module_scratch[4], g->stats.gold, delta,
               ap_army_total_hp(g),
               g->position.x, g->position.y);
        if (!ap_save_checkpoint(g, m, f, AP_SLOT_GRIND_P1)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        st->phase = AP_ALL_DONE;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;
    }

    default:
        break;
    }
    return SHELL_RUN_EXIT_FAIL;
}
