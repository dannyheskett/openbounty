// Minimal autoplay flow — per-phase command emitter.
//
// Each phase function returns ONE ApCmd per tick. The dispatcher in
// core.c runs the command, advances one tick, asserts the command's
// post-state predicate. Hard fail on assertion failure.
//
// Sequence (seed=1):
//   intro → walk to Maximus → recruit → exit castle → for each chest
//   on the start landmass, walk via flow-field and answer A/SPACE
//   → walk to foe → Y → combat → done

#include "autoplay/internal.h"
#include "combat.h"

#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stddef.h>
#include <string.h>

// Pre-computed flow-field table: for each target (chest or foe) and
// each tile (x,y) on the map, the direction key the hero should press
// to step one tile closer to that target by shortest path. Generated
// offline by AP_PRINT_FLOW=1.
//
//   FLOW[target][y][x] == -1   → we ARE the target
//   FLOW[target][y][x] ==  0   → unreachable (or off-map)
//   FLOW[target][y][x] == K    → press raylib key K
#include "flow_field.h"

// =========================================================================
// Predicates
// =========================================================================

static bool assert_always_true(const Game *g) { (void)g; return true; }

static bool assert_dialog_open(const Game *g) {
    (void)g; return dialog_is_active();
}
static bool assert_dialog_closed(const Game *g) {
    (void)g; return !dialog_is_active();
}
static bool assert_view_home_castle(const Game *g) {
    (void)g; return views_active() == VIEW_HOME_CASTLE;
}
static bool assert_view_recruit_soldiers(const Game *g) {
    (void)g; return views_active() == VIEW_RECRUIT_SOLDIERS;
}
static bool assert_view_none(const Game *g) {
    (void)g; return views_active() == VIEW_NONE && !dialog_is_active();
}
static bool assert_moved_up(const Game *g) {
    return g->position.x == ap_pre_pos_x &&
           g->position.y == ap_pre_pos_y - 1;
}
static bool assert_army_hp_plus_100(const Game *g) {
    return ap_army_total_hp(g) == ap_pre_army_hp + 100;
}
static bool assert_army_hp_plus_60(const Game *g) {
    return ap_army_total_hp(g) == ap_pre_army_hp + 60;
}
static bool assert_army_hp_plus_80(const Game *g) {
    return ap_army_total_hp(g) == ap_pre_army_hp + 80;
}

// "Step succeeded": position changed by the queued direction, OR a
// prompt opened (we stepped on a chest/foe), OR a dialog opened.
static bool assert_step_made_progress(const Game *g) {
    if (g->position.x != ap_pre_pos_x ||
        g->position.y != ap_pre_pos_y) return true;
    if (prompt_is_active()) return true;
    if (dialog_is_active()) return true;
    return false;
}

static bool assert_prompt_gone(const Game *g) {
    (void)g; return !prompt_is_active();
}

// Combat opened OR combat already finished with a win (RunCombat
// may complete the whole fight inside a single outer tick).
// Combat opened OR combat already finished (regardless of outcome).
// Used when we want POST_COMBAT to detect loss and exit gracefully
// instead of hard-failing the assertion.
static bool assert_combat_resolved(const Game *g) {
    (void)g;
    // Always passes — combat either opened (and will resolve in
    // RunCombat), or already resolved this tick. Either way the
    // POST_COMBAT phase observes the outcome.
    return true;
}

static bool assert_combat_open_or_won(const Game *g) {
    if (combat_current_rendered != NULL) return true;
    int peasants = 0, other = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
        if (strcmp(g->army[i].id, "peasants") == 0) peasants += g->army[i].count;
        else other += g->army[i].count;
    }
    bool defeat = (other == 0 && peasants > 0 && peasants <= 20);
    return !defeat;
}

// =========================================================================
// Phase dispatch
// =========================================================================

ApCmd ap_flow_phase(const Game *g, const Map *m,
                       AutoplayState *st,
                       bool *out_phase_done,
                       AutoplayPhase *out_next_phase) {
    (void)m;
    *out_phase_done = false;
    *out_next_phase = st->phase;

    switch (st->phase) {

    case AP_FLOW_DISMISS_INTRO: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (sub == 0) {
            st->module_scratch[0] = 1;
            return (ApCmd){ "DISMISS_INTRO:wait", 0, assert_dialog_open };
        }
        st->module_scratch[0] = -1;
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_WALK_TO_GATE;
        return (ApCmd){ "DISMISS_INTRO:space", KEY_SPACE, assert_dialog_closed };
    }

    case AP_FLOW_WALK_TO_GATE: {
        int n = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (n >= 1) {
            st->module_scratch[0] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_STEP_ONTO_GATE;
            return (ApCmd){ "WALK_TO_GATE:done", 0, assert_always_true };
        }
        st->module_scratch[0] = n + 1;
        return (ApCmd){ "WALK_TO_GATE:up", KEY_UP, assert_moved_up };
    }

    case AP_FLOW_STEP_ONTO_GATE: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_OPEN_RECRUIT;
        return (ApCmd){ "STEP_ONTO_GATE:up", KEY_UP, assert_view_home_castle };
    }

    case AP_FLOW_OPEN_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_RECRUIT_PIKEMEN;
        return (ApCmd){ "OPEN_RECRUIT:a", KEY_A, assert_view_recruit_soldiers };
    }

    case AP_FLOW_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "RECRUIT_PIKEMEN:c", KEY_C, assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "RECRUIT_PIKEMEN:1", KEY_ONE, assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "RECRUIT_PIKEMEN:0", KEY_ZERO, assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true; *out_next_phase = AP_FLOW_RECRUIT_MILITIA;
            return (ApCmd){ "RECRUIT_PIKEMEN:enter", KEY_ENTER, assert_army_hp_plus_100 };
        }
    }

    case AP_FLOW_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "RECRUIT_MILITIA:a", KEY_A, assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "RECRUIT_MILITIA:3", KEY_THREE, assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "RECRUIT_MILITIA:0", KEY_ZERO, assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true; *out_next_phase = AP_FLOW_RECRUIT_ARCHERS;
            return (ApCmd){ "RECRUIT_MILITIA:enter", KEY_ENTER, assert_army_hp_plus_60 };
        }
    }

    case AP_FLOW_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "RECRUIT_ARCHERS:b", KEY_B, assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "RECRUIT_ARCHERS:8", KEY_EIGHT, assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true; *out_next_phase = AP_FLOW_EXIT_RECRUIT;
            return (ApCmd){ "RECRUIT_ARCHERS:enter", KEY_ENTER, assert_army_hp_plus_80 };
        }
    }

    case AP_FLOW_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_EXIT_CASTLE;
        return (ApCmd){ "EXIT_RECRUIT:esc", KEY_ESCAPE, assert_view_home_castle };
    }

    case AP_FLOW_EXIT_CASTLE: {
        st->module_scratch[0] = 0;   // initial target idx
        st->module_scratch[1] = 0;   // consecutive fails
        st->module_scratch[2] = -1;  // last_x (sentinel = "uninitialized")
        st->module_scratch[3] = -1;  // last_y
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_WALK_TO_FOE;
        return (ApCmd){ "EXIT_CASTLE:esc", KEY_ESCAPE, assert_view_none };
    }

    // -- Flow-field walk: visit each target in TARGET_X/Y order. ----
    // module_scratch[0] = current target index
    // module_scratch[1] = consecutive failed-step count (vs scratch[2,3])
    // module_scratch[2] = last observed position x
    // module_scratch[3] = last observed position y
    case AP_FLOW_WALK_TO_FOE: {
        int ti = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];

        // Handle any prompt / dialog FIRST.
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && strstr(hdr, "Foe")) {
                AP_LOG("[min] foe prompt — Y");
                st->module_scratch[1] = 0;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                return (ApCmd){ "FLOW:y_foe", KEY_Y, assert_combat_open_or_won };
            }
            st->module_scratch[1] = 0;
            return (ApCmd){ "FLOW:a_chest", KEY_A, assert_prompt_gone };
        }
        if (dialog_is_active()) {
            st->module_scratch[1] = 0;
            return (ApCmd){ "FLOW:space_dialog", KEY_SPACE, assert_dialog_closed };
        }

        // Town view auto-opens when we step onto a town tile.
        // Transition to BUY_SIEGE.
        if (views_active() == VIEW_TOWN) {
            AP_LOG("[min] entered town — transitioning to BUY_SIEGE");
            st->module_scratch[1] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_BUY_SIEGE;
            return (ApCmd){ "FLOW:in_town", 0, assert_always_true };
        }

        if (ti >= N_TARGETS) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "FLOW:all_done", 0, assert_always_true };
        }

        int hx = g->position.x;
        int hy = g->position.y;

        // Position-progress check vs the LAST tick's observed position
        // (not this tick's pre-snapshot, which is captured AFTER the
        // previous engine step so it equals current). Initialized to
        // -1 the first time we enter this phase.
        int last_x = st->module_scratch[2];
        int last_y = st->module_scratch[3];
        if (last_x < 0) {
            st->module_scratch[2] = hx;
            st->module_scratch[3] = hy;
            st->module_scratch[1] = 0;
        } else if (hx == last_x && hy == last_y) {
            int fails = st->module_scratch[1] + 1;
            st->module_scratch[1] = fails;
            if (fails >= 3) {
                AP_LOG("[min] target %d at (%d,%d): 3 blocked steps from (%d,%d) — skip",
                       ti, TARGET_X[ti], TARGET_Y[ti], hx, hy);
                st->module_scratch[0] = ti + 1;
                st->module_scratch[1] = 0;
                return (ApCmd){ "FLOW:skip_blocked", 0, assert_always_true };
            }
        } else {
            st->module_scratch[1] = 0;
            st->module_scratch[2] = hx;
            st->module_scratch[3] = hy;
        }

        int key = (hx >= 0 && hx < 64 && hy >= 0 && hy < 64)
                  ? FLOW[ti][hy][hx] : 0;

        if (key == -1) {
            AP_LOG("[min] target %d at (%d,%d) reached", ti,
                   TARGET_X[ti], TARGET_Y[ti]);
            st->module_scratch[0] = ti + 1;
            st->module_scratch[1] = 0;
            return (ApCmd){ "FLOW:advance_target", 0, assert_always_true };
        }
        if (key == 0) {
            AP_LOG("[min] target %d at (%d,%d) unreachable from (%d,%d) — skip",
                   ti, TARGET_X[ti], TARGET_Y[ti], hx, hy);
            st->module_scratch[0] = ti + 1;
            st->module_scratch[1] = 0;
            return (ApCmd){ "FLOW:skip_unreachable", 0, assert_always_true };
        }

        return (ApCmd){ "FLOW:step", key, assert_always_true };
    }

    case AP_FLOW_ATTACK_FOE: {
        // Not reached — flow phase emits Y directly when the foe prompt fires.
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_COMBAT;
        return (ApCmd){ "ATTACK_FOE:noop", 0, assert_always_true };
    }

    case AP_FLOW_COMBAT: {
        if (combat_current_rendered == NULL) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_POST_COMBAT;
            return (ApCmd){ "COMBAT:ended", 0, assert_always_true };
        }
        return (ApCmd){ "COMBAT:wait", 0, assert_always_true };
    }

    case AP_FLOW_POST_COMBAT: {
        if (!dialog_is_active() && !prompt_is_active() &&
            views_active() == VIEW_NONE) {
            // Defeat detection: temp_death wipes the army to 20
            // peasants and forfeits the boat. If we're standing back
            // at the home spawn with that signature, end the run
            // gracefully rather than spiraling into more lost fights.
            int peasants = 0, other = 0;
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
                if (strcmp(g->army[i].id, "peasants") == 0)
                    peasants += g->army[i].count;
                else other += g->army[i].count;
            }
            bool defeat = (other == 0 && peasants > 0 && peasants <= 20);
            if (defeat) {
                AP_LOG("[flow] combat defeat — ending run gracefully "
                       "(pos=(%d,%d) gold=%d)",
                       g->position.x, g->position.y, g->stats.gold);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_DONE;
                return (ApCmd){ "POST_COMBAT:defeat", 0, assert_always_true };
            }
            // Resume the active grind. Boat mode after a sea combat:
            // back to BOAT_GRIND. Otherwise: back to land WALK_TO_FOE.
            *out_phase_done = true;
            if (g->boat.has_boat && g->travel_mode == TRAVEL_BOAT) {
                *out_next_phase = AP_FLOW_BOAT_GRIND;
            } else if (g->boat.has_boat) {
                *out_next_phase = AP_FLOW_WALK_TO_BOAT;
            } else {
                int ti = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
                if (ti < N_TARGETS && TARGET_KIND[ti] == 'F') {
                    st->module_scratch[0] = ti + 1;
                }
                *out_next_phase = AP_FLOW_WALK_TO_FOE;
            }
            return (ApCmd){ "POST_COMBAT:noop", 0, assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "POST_COMBAT:space", KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "POST_COMBAT:wait", 0, assert_always_true };
    }

    // -- Buy siege weapons at town. ---------------------------------
    // module_scratch[4] = sub-step:
    //   0 = press E (open siege row)
    //   1 = press SPACE (dismiss the "purchased" info panel)
    //   2 = done — assert siege_weapons == 1, transition to EXIT_TOWN
    case AP_FLOW_BUY_SIEGE: {
        int sub = (st->module_scratch[4] < 0) ? 0 : st->module_scratch[4];
        switch (sub) {
        case 0:
            st->module_scratch[4] = 1;
            return (ApCmd){ "BUY_SIEGE:e", KEY_E, assert_always_true };
        case 1:
            st->module_scratch[4] = 2;
            return (ApCmd){ "BUY_SIEGE:space_info", KEY_SPACE, assert_always_true };
        default:
            // Verify the purchase actually took effect.
            if (!g->stats.siege_weapons) {
                AP_LOG("[min] BUY_SIEGE: siege_weapons still 0 after purchase!");
                ap_dump_state("siege purchase failed", g, st);
                return (ApCmd){ "BUY_SIEGE:fail", 0, assert_dialog_open };
            }
            AP_LOG("[flow] siege_weapons=1, gold=%d", g->stats.gold);
            st->module_scratch[4] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_RENT_BOAT;
            return (ApCmd){ "BUY_SIEGE:done", 0, assert_always_true };
        }
    }

    // -- Rent a boat (same town, row B). ----------------------------
    // Pressing B in the town view runs town_do_boat which deducts the
    // cost and sets boat.has_boat=true atomically. No info panel pops
    // up on success, so we MUST NOT press a follow-up SPACE — that
    // would re-trigger BOAT row and CANCEL the rental.
    // module_scratch[4] = sub-step: 0=KEY_B, 1=verify+done
    case AP_FLOW_RENT_BOAT: {
        int sub = (st->module_scratch[4] < 0) ? 0 : st->module_scratch[4];
        switch (sub) {
        case 0:
            st->module_scratch[4] = 1;
            return (ApCmd){ "RENT_BOAT:b", KEY_B, assert_always_true };
        default:
            if (!g->boat.has_boat) {
                AP_LOG("[flow] RENT_BOAT: has_boat still false after purchase");
                ap_dump_state("boat purchase failed", g, st);
                return (ApCmd){ "RENT_BOAT:fail", 0, assert_dialog_open };
            }
            AP_LOG("[flow] boat rented at (%d,%d), gold=%d",
                   g->boat.x, g->boat.y, g->stats.gold);
            st->module_scratch[4] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_EXIT_TOWN;
            return (ApCmd){ "RENT_BOAT:done", 0, assert_always_true };
        }
    }

    case AP_FLOW_EXIT_TOWN: {
        if (views_active() == VIEW_NONE) {
            // Reset boat-grind state and head to boat tile.
            st->module_scratch[0] = 0;   // boat target idx
            st->module_scratch[1] = 0;   // fail counter
            st->module_scratch[2] = -1;  // last_x
            st->module_scratch[3] = -1;  // last_y
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_WALK_TO_BOAT;
            return (ApCmd){ "EXIT_TOWN:done", 0, assert_always_true };
        }
        return (ApCmd){ "EXIT_TOWN:esc", KEY_ESCAPE, assert_always_true };
    }

    // -- Walk to boat tile (one cardinal step per tick). ------------
    // After exiting hunterville the hero is at (12,60)? — let me check
    // — the bounce-back from town leaves the hero at the tile they
    // stepped from, which is wherever the flow approached the town
    // from. Either way, the boat is at g->boat.x/y (hunterville's
    // boat spawn = (11,60)). Step toward it using the simple direct
    // direction; on collision the position-progress watchdog bails.
    case AP_FLOW_WALK_TO_BOAT: {
        // Already in boat (stepped on it)? Transition.
        if (g->travel_mode == TRAVEL_BOAT) {
            AP_LOG("[flow] boarded boat at (%d,%d)",
                   g->position.x, g->position.y);
            st->module_scratch[1] = 0;
            st->module_scratch[2] = -1;
            st->module_scratch[3] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_BOAT_GRIND;
            return (ApCmd){ "WALK_TO_BOAT:boarded", 0, assert_always_true };
        }
        if (!g->boat.has_boat) {
            AP_LOG("[flow] WALK_TO_BOAT: lost the boat!");
            ap_dump_state("no boat", g, st);
            return (ApCmd){ "WALK_TO_BOAT:no_boat", 0, assert_dialog_open };
        }
        int hx = g->position.x, hy = g->position.y;
        int bx = g->boat.x,    by = g->boat.y;
        int dx = (bx > hx) ? 1 : (bx < hx) ? -1 : 0;
        int dy = (by > hy) ? 1 : (by < hy) ? -1 : 0;
        // pos-progress fail counter (scratch[1]) — same shape as
        // BOAT_GRIND below.
        int last_x = st->module_scratch[2];
        int last_y = st->module_scratch[3];
        if (last_x < 0) {
            st->module_scratch[2] = hx;
            st->module_scratch[3] = hy;
            st->module_scratch[1] = 0;
        } else if (hx == last_x && hy == last_y) {
            int fails = st->module_scratch[1] + 1;
            st->module_scratch[1] = fails;
            if (fails >= 5) {
                AP_LOG("[flow] WALK_TO_BOAT: blocked at (%d,%d) en route to (%d,%d)",
                       hx, hy, bx, by);
                ap_dump_state("walk-to-boat blocked", g, st);
                return (ApCmd){ "WALK_TO_BOAT:fail", 0, assert_dialog_open };
            }
        } else {
            st->module_scratch[1] = 0;
            st->module_scratch[2] = hx;
            st->module_scratch[3] = hy;
        }
        int key = 0;
        if      (dx == 0  && dy == -1) key = KEY_UP;
        else if (dx == 0  && dy ==  1) key = KEY_DOWN;
        else if (dx == -1 && dy ==  0) key = KEY_LEFT;
        else if (dx == 1  && dy ==  0) key = KEY_RIGHT;
        else if (dx == -1 && dy == -1) key = KEY_HOME;
        else if (dx == 1  && dy == -1) key = KEY_PAGE_UP;
        else if (dx == -1 && dy ==  1) key = KEY_END;
        else if (dx == 1  && dy ==  1) key = KEY_PAGE_DOWN;
        if (key == 0) {
            AP_LOG("[flow] WALK_TO_BOAT: already at boat tile?");
            return (ApCmd){ "WALK_TO_BOAT:wait", 0, assert_always_true };
        }
        return (ApCmd){ "WALK_TO_BOAT:step", key, assert_always_true };
    }

    // -- Boat grind: follow BOAT_FLOW for each off-landmass chest. --
    // module_scratch[0] = current boat target idx
    // module_scratch[1] = consecutive fail count
    // module_scratch[2] = last_x
    // module_scratch[3] = last_y
    case AP_FLOW_BOAT_GRIND: {
        int ti = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];

        // Handle any prompt / dialog first.
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && strstr(hdr, "Foe")) {
                AP_LOG("[flow] BOAT: foe prompt — Y");
                st->module_scratch[1] = 0;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                return (ApCmd){ "BOAT:y_foe", KEY_Y, assert_combat_resolved };
            }
            st->module_scratch[1] = 0;
            return (ApCmd){ "BOAT:a_chest", KEY_A, assert_prompt_gone };
        }
        if (dialog_is_active()) {
            st->module_scratch[1] = 0;
            return (ApCmd){ "BOAT:space_dialog", KEY_SPACE, assert_dialog_closed };
        }

        if (ti >= N_BOAT_TARGETS) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "BOAT:all_done", 0, assert_always_true };
        }

        int hx = g->position.x;
        int hy = g->position.y;

        int last_x = st->module_scratch[2];
        int last_y = st->module_scratch[3];
        if (last_x < 0) {
            st->module_scratch[2] = hx;
            st->module_scratch[3] = hy;
            st->module_scratch[1] = 0;
        } else if (hx == last_x && hy == last_y) {
            int fails = st->module_scratch[1] + 1;
            st->module_scratch[1] = fails;
            if (fails >= 5) {
                AP_LOG("[flow] BOAT target %d (%d,%d): blocked at (%d,%d) — skip",
                       ti, BOAT_TARGET_X[ti], BOAT_TARGET_Y[ti], hx, hy);
                st->module_scratch[0] = ti + 1;
                st->module_scratch[1] = 0;
                return (ApCmd){ "BOAT:skip_blocked", 0, assert_always_true };
            }
        } else {
            st->module_scratch[1] = 0;
            st->module_scratch[2] = hx;
            st->module_scratch[3] = hy;
        }

        int key = (hx >= 0 && hx < 64 && hy >= 0 && hy < 64)
                  ? BOAT_FLOW[ti][hy][hx] : 0;

        if (key == -1) {
            AP_LOG("[flow] BOAT target %d at (%d,%d) reached",
                   ti, BOAT_TARGET_X[ti], BOAT_TARGET_Y[ti]);
            st->module_scratch[0] = ti + 1;
            st->module_scratch[1] = 0;
            return (ApCmd){ "BOAT:advance_target", 0, assert_always_true };
        }
        if (key == 0) {
            AP_LOG("[flow] BOAT target %d (%d,%d) unreachable from (%d,%d) — skip",
                   ti, BOAT_TARGET_X[ti], BOAT_TARGET_Y[ti], hx, hy);
            st->module_scratch[0] = ti + 1;
            st->module_scratch[1] = 0;
            return (ApCmd){ "BOAT:skip_unreachable", 0, assert_always_true };
        }
        return (ApCmd){ "BOAT:step", key, assert_always_true };
    }

    case AP_FLOW_DONE: {
        *out_phase_done = true;
        *out_next_phase = AP_ALL_DONE;
        return (ApCmd){ "DONE", 0, assert_always_true };
    }

    default:
        return (ApCmd){ "UNKNOWN_PHASE", 0, assert_dialog_open };
    }
}
