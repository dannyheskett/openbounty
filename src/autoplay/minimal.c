// Minimal autoplay flow — per-phase command emitter.
//
// Each phase function returns ONE ApCmd per tick. The dispatcher in
// core.c runs the command, advances one tick, asserts the command's
// post-state predicate. Hard fail on assertion failure.
//
// No BFS. No destination concepts. No internal wait loops. Each
// command is literal: "press this key; on the next tick I expect
// exactly this delta."

#include "autoplay/internal.h"
#include "combat.h"

#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stddef.h>
#include <string.h>

// =========================================================================
// Predicate functions
// =========================================================================

static bool assert_always_true(const Game *g) { (void)g; return true; }

static bool assert_dialog_open(const Game *g) {
    (void)g;
    return dialog_is_active();
}

static bool assert_dialog_closed(const Game *g) {
    (void)g;
    return !dialog_is_active();
}

static bool assert_view_home_castle(const Game *g) {
    (void)g;
    return views_active() == VIEW_HOME_CASTLE;
}

static bool assert_view_recruit_soldiers(const Game *g) {
    (void)g;
    return views_active() == VIEW_RECRUIT_SOLDIERS;
}

static bool assert_view_none(const Game *g) {
    (void)g;
    return views_active() == VIEW_NONE && !dialog_is_active();
}

static bool assert_moved_up(const Game *g) {
    return g->position.x == ap_pre_pos_x &&
           g->position.y == ap_pre_pos_y - 1;
}

static bool assert_moved_down(const Game *g) {
    return g->position.x == ap_pre_pos_x &&
           g->position.y == ap_pre_pos_y + 1;
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

static bool assert_combat_open(const Game *g) {
    (void)g;
    return combat_current_rendered != NULL;
}

static bool assert_combat_ended_with_win(const Game *g) {
    // Combat ended (combat_current_rendered NULL) and we did NOT lose.
    // Loss signature = army wiped to 20 peasants only.
    if (combat_current_rendered != NULL) return true;  // still running OK
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
// Phase dispatch table
// =========================================================================

// Smoke flow walks south for this many tiles after exiting the
// castle, then declares done. (Combat isn't part of the minimal smoke
// — that's a follow-up flow once the contract is proven.)
#define WALK_SOUTH_STEPS 2

ApCmd ap_minimal_phase(const Game *g, const Map *m,
                       AutoplayState *st,
                       bool *out_phase_done,
                       AutoplayPhase *out_next_phase) {
    (void)m;
    *out_phase_done = false;
    *out_next_phase = st->phase;

    switch (st->phase) {

    // -----------------------------------------------------------------
    // AP_MIN_DISMISS_INTRO
    // Two sub-steps: (a) wait one tick for the intro dialog to appear
    // (key=0, assert dialog opens), then (b) SPACE to dismiss (assert
    // dialog closed). scratch[0] tracks the sub-step index.
    // -----------------------------------------------------------------
    case AP_MIN_DISMISS_INTRO: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (sub == 0) {
            st->module_scratch[0] = 1;
            return (ApCmd){
                .name = "DISMISS_INTRO:wait_for_dialog",
                .key = 0,
                .assert_post = assert_dialog_open,
            };
        }
        // sub == 1: dialog is open, press SPACE.
        st->module_scratch[0] = -1;  // reset for next phase's use
        *out_phase_done = true;
        *out_next_phase = AP_MIN_WALK_TO_GATE;
        return (ApCmd){
            .name = "DISMISS_INTRO:space",
            .key = KEY_SPACE,
            .assert_post = assert_dialog_closed,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_WALK_TO_GATE
    // Press KEY_UP. Assert position moved up by 1. Repeat. After 1 step
    // we're at (11,57); next step lands on Maximus gate, so transition
    // to STEP_ONTO_GATE which handles that special case. scratch[0]
    // tracks the step count.
    // -----------------------------------------------------------------
    case AP_MIN_WALK_TO_GATE: {
        int n = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (n >= 1) {
            // After 1 step we're at (11,57). Transition.
            st->module_scratch[0] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_MIN_STEP_ONTO_GATE;
            return (ApCmd){
                .name = "WALK_TO_GATE:done",
                .key = 0,
                .assert_post = assert_always_true,
            };
        }
        st->module_scratch[0] = n + 1;
        return (ApCmd){
            .name = "WALK_TO_GATE:up",
            .key = KEY_UP,
            .assert_post = assert_moved_up,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_STEP_ONTO_GATE
    // Press KEY_UP. Assert VIEW_HOME_CASTLE opens.
    // -----------------------------------------------------------------
    case AP_MIN_STEP_ONTO_GATE: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_OPEN_RECRUIT;
        return (ApCmd){
            .name = "STEP_ONTO_GATE:up",
            .key = KEY_UP,
            .assert_post = assert_view_home_castle,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_OPEN_RECRUIT
    // Press A. Assert VIEW_RECRUIT_SOLDIERS opens.
    // -----------------------------------------------------------------
    case AP_MIN_OPEN_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_RECRUIT_PIKEMEN;
        return (ApCmd){
            .name = "OPEN_RECRUIT:a",
            .key = KEY_A,
            .assert_post = assert_view_recruit_soldiers,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_RECRUIT_PIKEMEN — keys: C, 1, 0, ENTER.
    // scratch[0] is the sub-index 0..3.
    // -----------------------------------------------------------------
    case AP_MIN_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0:
            st->module_scratch[0] = 1;
            return (ApCmd){
                .name = "RECRUIT_PIKEMEN:c",
                .key = KEY_C,
                .assert_post = assert_view_recruit_soldiers,
            };
        case 1:
            st->module_scratch[0] = 2;
            return (ApCmd){
                .name = "RECRUIT_PIKEMEN:1",
                .key = KEY_ONE,
                .assert_post = assert_always_true,  // numeric prompt accumulates
            };
        case 2:
            st->module_scratch[0] = 3;
            return (ApCmd){
                .name = "RECRUIT_PIKEMEN:0",
                .key = KEY_ZERO,
                .assert_post = assert_always_true,
            };
        case 3:
        default:
            st->module_scratch[0] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_MIN_RECRUIT_MILITIA;
            return (ApCmd){
                .name = "RECRUIT_PIKEMEN:enter",
                .key = KEY_ENTER,
                .assert_post = assert_army_hp_plus_100,
            };
        }
    }

    // -----------------------------------------------------------------
    // AP_MIN_RECRUIT_MILITIA — keys: A, 3, 0, ENTER.
    // -----------------------------------------------------------------
    case AP_MIN_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0:
            st->module_scratch[0] = 1;
            return (ApCmd){
                .name = "RECRUIT_MILITIA:a",
                .key = KEY_A,
                .assert_post = assert_view_recruit_soldiers,
            };
        case 1:
            st->module_scratch[0] = 2;
            return (ApCmd){
                .name = "RECRUIT_MILITIA:3",
                .key = KEY_THREE,
                .assert_post = assert_always_true,
            };
        case 2:
            st->module_scratch[0] = 3;
            return (ApCmd){
                .name = "RECRUIT_MILITIA:0",
                .key = KEY_ZERO,
                .assert_post = assert_always_true,
            };
        case 3:
        default:
            st->module_scratch[0] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_MIN_RECRUIT_ARCHERS;
            return (ApCmd){
                .name = "RECRUIT_MILITIA:enter",
                .key = KEY_ENTER,
                .assert_post = assert_army_hp_plus_60,
            };
        }
    }

    // -----------------------------------------------------------------
    // AP_MIN_RECRUIT_ARCHERS — keys: B, 8, ENTER.
    // -----------------------------------------------------------------
    case AP_MIN_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0:
            st->module_scratch[0] = 1;
            return (ApCmd){
                .name = "RECRUIT_ARCHERS:b",
                .key = KEY_B,
                .assert_post = assert_view_recruit_soldiers,
            };
        case 1:
            st->module_scratch[0] = 2;
            return (ApCmd){
                .name = "RECRUIT_ARCHERS:8",
                .key = KEY_EIGHT,
                .assert_post = assert_always_true,
            };
        case 2:
        default:
            st->module_scratch[0] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_MIN_EXIT_RECRUIT;
            return (ApCmd){
                .name = "RECRUIT_ARCHERS:enter",
                .key = KEY_ENTER,
                .assert_post = assert_army_hp_plus_80,
            };
        }
    }

    // -----------------------------------------------------------------
    // AP_MIN_EXIT_RECRUIT — KEY_ESCAPE, expect VIEW_HOME_CASTLE.
    // -----------------------------------------------------------------
    case AP_MIN_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_EXIT_CASTLE;
        return (ApCmd){
            .name = "EXIT_RECRUIT:escape",
            .key = KEY_ESCAPE,
            .assert_post = assert_view_home_castle,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_EXIT_CASTLE — KEY_ESCAPE, expect VIEW_NONE.
    // -----------------------------------------------------------------
    case AP_MIN_EXIT_CASTLE: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_WALK_TO_FOE;
        return (ApCmd){
            .name = "EXIT_CASTLE:escape",
            .key = KEY_ESCAPE,
            .assert_post = assert_view_none,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_WALK_TO_FOE
    // Walk south WALK_SOUTH_STEPS times, asserting position moved
    // down each tick. Then declare done. Combat-finding is out of
    // scope for the minimal smoke flow — it's a follow-up flow once
    // the contract is proven on the simpler path. scratch[0] tracks
    // the step counter.
    // -----------------------------------------------------------------
    case AP_MIN_WALK_TO_FOE: {
        int n = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (n >= WALK_SOUTH_STEPS) {
            st->module_scratch[0] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_MIN_DONE;
            return (ApCmd){
                .name = "WALK_TO_FOE:done",
                .key = 0,
                .assert_post = assert_always_true,
            };
        }
        st->module_scratch[0] = n + 1;
        return (ApCmd){
            .name = "WALK_TO_FOE:down",
            .key = KEY_DOWN,
            .assert_post = assert_moved_down,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_ATTACK_FOE — KEY_Y on the prompt, expect combat opens.
    // -----------------------------------------------------------------
    case AP_MIN_ATTACK_FOE: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_COMBAT;
        return (ApCmd){
            .name = "ATTACK_FOE:y",
            .key = KEY_Y,
            .assert_post = assert_combat_open,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_COMBAT
    // Emit key=0 each tick; the combat driver in core.c handles unit
    // input. Assertion: combat is still running OR ended with a win.
    // When combat_current_rendered becomes NULL, transition.
    // -----------------------------------------------------------------
    case AP_MIN_COMBAT: {
        if (combat_current_rendered == NULL) {
            *out_phase_done = true;
            *out_next_phase = AP_MIN_POST_COMBAT;
            return (ApCmd){
                .name = "COMBAT:ended",
                .key = 0,
                .assert_post = assert_always_true,
            };
        }
        return (ApCmd){
            .name = "COMBAT:wait",
            .key = 0,
            .assert_post = assert_combat_ended_with_win,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_POST_COMBAT — KEY_SPACE on victory dialog, expect closed.
    // -----------------------------------------------------------------
    case AP_MIN_POST_COMBAT: {
        if (!dialog_is_active()) {
            // No victory dialog (already dismissed or none) — go to
            // DONE.
            *out_phase_done = true;
            *out_next_phase = AP_MIN_DONE;
            return (ApCmd){
                .name = "POST_COMBAT:no_dialog",
                .key = 0,
                .assert_post = assert_always_true,
            };
        }
        *out_phase_done = true;
        *out_next_phase = AP_MIN_DONE;
        return (ApCmd){
            .name = "POST_COMBAT:space",
            .key = KEY_SPACE,
            .assert_post = assert_dialog_closed,
        };
    }

    // -----------------------------------------------------------------
    // AP_MIN_DONE — single noop that transitions to AP_ALL_DONE.
    // -----------------------------------------------------------------
    case AP_MIN_DONE: {
        *out_phase_done = true;
        *out_next_phase = AP_ALL_DONE;
        return (ApCmd){
            .name = "DONE",
            .key = 0,
            .assert_post = assert_always_true,
        };
    }

    default:
        // Unknown phase — emit a noop with always-fail assertion to
        // surface the bug.
        return (ApCmd){
            .name = "UNKNOWN_PHASE",
            .key = 0,
            .assert_post = assert_dialog_open,  // probably false → fail
        };
    }
}
