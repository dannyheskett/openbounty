// Grind phase 0: pre-Hack chest sweep on continentia south band.
//
// Runs after Murray VERIFY, before Hack contract pickup. Walks to a
// hand-picked list of gold chests near Maximus and answers B
// (distribute gold to peasants → +leadership) at each. The leadership
// boost raises the per-troop recruit cap so the post-restock army has
// enough HP to win the Hack siege.
//
// Targets are seed=1-specific gold chests in continentia (zone 0):
//   (10,53) +8     (3,59)  +10    (20,59) +10
//   (22,60) +10    (25,57) +10
// Total: +48 leadership (computed offline from GameRollChest).
//
// State slot layout (module_scratch[]):
//   [0],[1]   = current target (x,y)
//   [2]       = pickups completed this sweep
//   [3]       = consecutive ferry-failure count
//   [4]       = starting leadership (for the delta log on exit)
//   [5]       = walk-step queued flag for the final step onto target
//   [16]      = sweep index into the hand-picked target list

#include "autoplay/internal.h"

#include "input_host.h"
#include "tile.h"
#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stdio.h>
#include <string.h>

#define GRIND_P0_FAIL_CAP 8

// Seed=1 gold chests in the southern band, ordered by walking distance
// from Maximus (11,56). Each (x,y) is a GameRollChest GOLD-outcome on
// continentia; B-answer yields the listed leadership.
static const struct { int x, y; int leadership; } grind_p0_targets[] = {
    { 10, 53, 8  },
    {  3, 59, 10 },
    { 20, 59, 10 },
    { 22, 60, 10 },
    { 25, 57, 10 },
};
#define GRIND_P0_N \
    ((int)(sizeof(grind_p0_targets) / sizeof(grind_p0_targets[0])))

ShellRunVerdict ap_grind_p0_per_frame(Game *g, Map *m, Fog *f,
                                      Resources *res, int frame_no,
                                      AutoplayState *st) {
    (void)f; (void)res;

    switch (st->phase) {

    case AP_GRIND_P0_PICK_TARGET: {
        if (st->module_scratch[4] < 0) {
            // First entry: initialize counters and pin starting state.
            st->module_scratch[2] = 0;   // pickups completed
            st->module_scratch[3] = 0;   // consecutive fails
            st->module_scratch[4] = g->stats.leadership_current;
            st->module_scratch[16] = 0; // sweep index
            AP_LOG("[grind-p0] starting leadership sweep from (%d,%d), "
                   "leadership=%d", g->position.x, g->position.y,
                   g->stats.leadership_current);
        }
        if (st->module_scratch[3] >= GRIND_P0_FAIL_CAP) {
            AP_LOG("[grind-p0] %d failures in a row — stopping sweep",
                   st->module_scratch[3]);
            st->phase = AP_GRIND_P0_VERIFY;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        int idx = st->module_scratch[16];
        // Skip any targets already claimed (e.g. by a foe combat that
        // walked us onto the tile, or a previous run via savegame).
        while (idx < GRIND_P0_N) {
            int tx = grind_p0_targets[idx].x;
            int ty = grind_p0_targets[idx].y;
            const Tile *tt = MapGetTile(m, tx, ty);
            if (tt && tt->interactive == INTERACT_TREASURE_CHEST) break;
            idx++;
        }
        st->module_scratch[16] = idx;
        if (idx >= GRIND_P0_N) {
            st->phase = AP_GRIND_P0_VERIFY;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        int tx = grind_p0_targets[idx].x;
        int ty = grind_p0_targets[idx].y;
        AP_LOG("[grind-p0] target #%d: chest at (%d,%d), "
               "expecting +%d leadership",
               idx + 1, tx, ty, grind_p0_targets[idx].leadership);
        st->module_scratch[0] = tx;
        st->module_scratch[1] = ty;
        st->module_scratch[5] = 0;
        st->ferry_state = FERRY_IDLE;
        st->phase = AP_GRIND_P0_WALK_TO_TARGET;
        st->phase_started_at = frame_no;
        st->path_len = 0;
        return SHELL_RUN_CONTINUE;
    }

    case AP_GRIND_P0_WALK_TO_TARGET: {
        int tx = st->module_scratch[0];
        int ty = st->module_scratch[1];
        // The pickup tile may have been claimed by walking onto it
        // (the engine clears the interactive flag after the dialog
        // dismisses). When that happens, advance to the next pickup.
        const Tile *tt = MapGetTile(m, tx, ty);
        bool still_pickup = tt &&
            tt->interactive == INTERACT_TREASURE_CHEST;
        if (!still_pickup) {
            st->module_scratch[2] += 1;
            st->module_scratch[3] = 0;
            st->module_scratch[16] += 1;
            AP_LOG("[grind-p0] pickup #%d claimed at (%d,%d), "
                   "leadership=%d",
                   st->module_scratch[2], tx, ty,
                   g->stats.leadership_current);
            st->phase = AP_GRIND_P0_PICK_TARGET;
            st->phase_started_at = frame_no;
            st->ferry_state = FERRY_IDLE;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        // Wait for any open prompt/dialog to settle — that's the chest
        // A/B prompt firing, handled by ap_handle_common_prompts (which
        // sees AP_GRIND_P0_WALK_TO_TARGET and answers B for leadership).
        if (prompt_is_active() || dialog_is_active()) {
            return SHELL_RUN_CONTINUE;
        }
        // Ferry to the chest tile (which is interactive — ferry stops
        // adjacent and we step onto it ourselves).
        FerryState fs = ap_ferry_tick(st, g, m, frame_no, tx, ty);
        if (fs == FERRY_FAILED) {
            AP_LOG("[grind-p0] ferry to (%d,%d) failed — skipping",
                   tx, ty);
            st->module_scratch[3] += 1;
            st->module_scratch[16] += 1;
            st->phase = AP_GRIND_P0_PICK_TARGET;
            st->phase_started_at = frame_no;
            st->ferry_state = FERRY_IDLE;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        if (fs == FERRY_DONE) {
            if (input_host_queue_depth() == 0 && !st->module_scratch[5]) {
                int k = ap_dir_key(g->position.x, g->position.y, tx, ty);
                if (k != 0) {
                    input_host_queue_key(k);
                    st->module_scratch[5] = 1;
                }
            }
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 86400,
                        "[grind-p0] stuck heading to chest");
        return SHELL_RUN_CONTINUE;
    }

    case AP_GRIND_P0_VERIFY: {
        int delta = g->stats.leadership_current - st->module_scratch[4];
        printf("autoplay: grind P0 swept %d chests — "
               "leadership %d → %d (Δ%+d), gold=%d, pos=(%d,%d)\n",
               st->module_scratch[2],
               st->module_scratch[4], g->stats.leadership_current, delta,
               g->stats.gold, g->position.x, g->position.y);
        if (!ap_save_checkpoint(g, m, f, AP_SLOT_GRIND_P0)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        // Resume Murray's flow at WALK_TO_TOWN_FOR_CONTRACT — we ran
        // after the Maximus recruit, so the army is set and we just
        // continue on to xoctan for the contract.
        st->phase = AP_MURRAY_WALK_TO_TOWN_FOR_CONTRACT;
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
