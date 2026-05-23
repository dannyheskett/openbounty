// Minimal autoplay flow — per-phase command emitter.
//
// Each phase function returns ONE ApCmd per tick. The dispatcher in
// core.c runs the command, advances one tick, asserts the command's
// post-state predicate. Hard fail on assertion failure.
//
// Sequence (seed=1, knight start at (11,58)):
//   intro → walk to (11,57) → step onto Maximus gate → recruit army
//   → exit castle → walk path collecting every reachable chest →
//   walk to nearest foe → fight → win → done
//
// The walk paths between landmarks are HARDCODED key sequences,
// pre-computed offline by the path generator in core.c (compiled out
// under #if 0; set AP_PRINT_PATHS=1 to regenerate).

#include "autoplay/internal.h"
#include "combat.h"

#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stddef.h>
#include <string.h>

// =========================================================================
// Hardcoded landmass path: 8 chests + 1 foe, in BFS order from start
// after the recruit. Each leg is a NUL-terminated key sequence (0 ends
// the leg). After the final key the hero is ON the target tile,
// which fires a chest A/B prompt or a foe Y/N prompt — handled by
// the WALK_PATH phase below.
// =========================================================================

typedef struct {
    int  x, y;        // target tile coordinates (for logging)
    char kind;        // 'C' chest, 'F' foe
    const int *keys;  // NUL-terminated key sequence
} PathLeg;

// Direction key codes (raylib): UP=265 DOWN=264 LEFT=263 RIGHT=262
// HOME=268 PAGE_UP=266 END=269 PAGE_DOWN=267
// leg0 begins from (11,57) — the post-castle-exit position — not the
// (11,58) spawn the path generator assumed. Prepend KEY_DOWN to
// re-enter (11,58) before the original sequence.
static const int leg0[] = { 264,269,263,269,0 };
static const int leg1[] = { 268,268,265,265,266,265,265,268,265,265,266,262,262,267,267,264,264,269,0 };
static const int leg2[] = { 265,0 };
static const int leg3[] = { 266,265,268,263,268,263,269,264,264,267,264,264,264,269,264,267,262,262,262,262,262,262,262,262,266,266,266,262,266,266,266,266,265,266,266,266,265,265,268,263,263,263,263,263,268,263,263,268,268,268,263,263,268,268,263,263,263,263,269,264,264,269,269,264,264,264,264,0 };
static const int leg4[] = { 265,265,265,265,266,266,265,265,266,262,262,262,262,267,264,0 };
static const int leg5[] = { 262,262,262,267,262,267,262,267,267,264,267,264,264,264,0 };
static const int leg6[] = { 265,265,265,266,262,0 };
static const int leg7[] = { 266,262,267,264,264,269,269,264,264,267,262,262,262,262,262,262,262,267,267,264,267,262,266,0 };
static const int leg8[] = { 269,263,263,263,0 };

static const PathLeg legs[] = {
    {  8, 60, 'C', leg0 },
    { 10, 54, 'C', leg1 },
    { 10, 53, 'C', leg2 },
    {  3, 48, 'C', leg3 },
    { 11, 41, 'C', leg4 },
    { 21, 50, 'C', leg5 },
    { 23, 46, 'C', leg6 },
    { 37, 56, 'C', leg7 },
    { 33, 57, 'F', leg8 },
};
#define N_LEGS ((int)(sizeof(legs)/sizeof(legs[0])))

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
static bool assert_combat_open(const Game *g) {
    (void)g; return combat_current_rendered != NULL;
}

// Combat opened OR combat already finished (in the same outer tick;
// RunCombat takes over the loop and may complete the whole fight
// before the autoplay's NEXT per_tick checks the assertion). "Won"
// is inferred from no temp_death army-wipe.
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

// "step succeeded": either position changed by (dx,dy) per the queued
// key direction, OR an interactive prompt opened (chest A/B or Foes!
// Y/N), OR a stray dialog opened that needs dismissing. We can't pass
// the (dx,dy) through assert_post without globals — instead we just
// check that *something* changed: position differs OR a prompt is now
// active OR a dialog is now active.
static bool assert_step_made_progress(const Game *g) {
    if (g->position.x != ap_pre_pos_x ||
        g->position.y != ap_pre_pos_y) return true;
    if (prompt_is_active()) return true;
    if (dialog_is_active()) return true;
    return false;
}

// Chest A/B prompt is dismissed → no prompt active.
static bool assert_prompt_gone(const Game *g) {
    (void)g; return !prompt_is_active();
}

// After Y on foe prompt: combat opens.
// (Already covered by assert_combat_open.)

// =========================================================================
// Phase dispatch
// =========================================================================

ApCmd ap_minimal_phase(const Game *g, const Map *m,
                       AutoplayState *st,
                       bool *out_phase_done,
                       AutoplayPhase *out_next_phase) {
    (void)m;
    *out_phase_done = false;
    *out_next_phase = st->phase;

    switch (st->phase) {

    // -- intro --------------------------------------------------------
    case AP_MIN_DISMISS_INTRO: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (sub == 0) {
            st->module_scratch[0] = 1;
            return (ApCmd){ "DISMISS_INTRO:wait", 0, assert_dialog_open };
        }
        st->module_scratch[0] = -1;
        *out_phase_done = true;
        *out_next_phase = AP_MIN_WALK_TO_GATE;
        return (ApCmd){ "DISMISS_INTRO:space", KEY_SPACE, assert_dialog_closed };
    }

    // -- walk to gate-approach (11,57) --------------------------------
    case AP_MIN_WALK_TO_GATE: {
        int n = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (n >= 1) {
            st->module_scratch[0] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_MIN_STEP_ONTO_GATE;
            return (ApCmd){ "WALK_TO_GATE:done", 0, assert_always_true };
        }
        st->module_scratch[0] = n + 1;
        return (ApCmd){ "WALK_TO_GATE:up", KEY_UP, assert_moved_up };
    }

    case AP_MIN_STEP_ONTO_GATE: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_OPEN_RECRUIT;
        return (ApCmd){ "STEP_ONTO_GATE:up", KEY_UP, assert_view_home_castle };
    }

    case AP_MIN_OPEN_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_RECRUIT_PIKEMEN;
        return (ApCmd){ "OPEN_RECRUIT:a", KEY_A, assert_view_recruit_soldiers };
    }

    // -- recruit pikemen: C, 1, 0, ENTER --------------------------------
    case AP_MIN_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "RECRUIT_PIKEMEN:c", KEY_C, assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "RECRUIT_PIKEMEN:1", KEY_ONE, assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "RECRUIT_PIKEMEN:0", KEY_ZERO, assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true; *out_next_phase = AP_MIN_RECRUIT_MILITIA;
            return (ApCmd){ "RECRUIT_PIKEMEN:enter", KEY_ENTER, assert_army_hp_plus_100 };
        }
    }

    case AP_MIN_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "RECRUIT_MILITIA:a", KEY_A, assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "RECRUIT_MILITIA:3", KEY_THREE, assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "RECRUIT_MILITIA:0", KEY_ZERO, assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true; *out_next_phase = AP_MIN_RECRUIT_ARCHERS;
            return (ApCmd){ "RECRUIT_MILITIA:enter", KEY_ENTER, assert_army_hp_plus_60 };
        }
    }

    case AP_MIN_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "RECRUIT_ARCHERS:b", KEY_B, assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "RECRUIT_ARCHERS:8", KEY_EIGHT, assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true; *out_next_phase = AP_MIN_EXIT_RECRUIT;
            return (ApCmd){ "RECRUIT_ARCHERS:enter", KEY_ENTER, assert_army_hp_plus_80 };
        }
    }

    case AP_MIN_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_EXIT_CASTLE;
        return (ApCmd){ "EXIT_RECRUIT:esc", KEY_ESCAPE, assert_view_home_castle };
    }

    case AP_MIN_EXIT_CASTLE: {
        *out_phase_done = true;
        *out_next_phase = AP_MIN_WALK_TO_FOE;  // reused as "walk the path"
        return (ApCmd){ "EXIT_CASTLE:esc", KEY_ESCAPE, assert_view_none };
    }

    // -- walk the hardcoded path: chests then foe --------------------
    // module_scratch layout for this phase:
    //   [0] = current leg index (0..N_LEGS-1)
    //   [1] = current key index within the leg's key array
    //   [2] = sub-state:
    //         0 = walking (emit next key)
    //         1 = arrived at chest tile, expect A/B prompt; emit A
    //         2 = arrived at foe tile, expect Foe prompt; emit Y;
    //             transition to COMBAT
    case AP_MIN_WALK_TO_FOE: {
        int leg_idx = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        int key_idx = (st->module_scratch[1] < 0) ? 0 : st->module_scratch[1];
        int sub     = (st->module_scratch[2] < 0) ? 0 : st->module_scratch[2];

        // Handle a pending prompt FIRST: if we just stepped onto a
        // chest or foe and the prompt is up, dispatch the answer.
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && strstr(hdr, "Foe")) {
                // Foe prompt: Y → combat.
                AP_LOG("[min] leg %d: foe prompt — Y", leg_idx);
                st->module_scratch[2] = 0;
                *out_phase_done = true;
                *out_next_phase = AP_MIN_COMBAT;
                return (ApCmd){ "PATH:y_foe", KEY_Y, assert_combat_open_or_won };
            }
            // Chest A/B: pick A (gold).
            AP_LOG("[min] leg %d: chest A", leg_idx);
            return (ApCmd){ "PATH:a_chest", KEY_A, assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PATH:space_dialog", KEY_SPACE, assert_dialog_closed };
        }

        // No prompt/dialog. Advance the walk.
        if (leg_idx >= N_LEGS) {
            // All legs done. Should not reach here since the foe leg
            // transitions to COMBAT. Defensive: go to DONE.
            *out_phase_done = true;
            *out_next_phase = AP_MIN_DONE;
            return (ApCmd){ "PATH:legs_exhausted", 0, assert_always_true };
        }

        const PathLeg *leg = &legs[leg_idx];
        int key = leg->keys[key_idx];
        if (key == 0) {
            // End of this leg's keys. We should be standing on the
            // target tile — the next tick the engine will fire the
            // chest A/B or Foes! prompt. Advance to next leg now and
            // emit a noop tick.
            AP_LOG("[min] leg %d arrived at (%d,%d) kind=%c",
                   leg_idx, leg->x, leg->y, leg->kind);
            st->module_scratch[0] = leg_idx + 1;
            st->module_scratch[1] = 0;
            return (ApCmd){ "PATH:leg_end_wait_prompt", 0, assert_always_true };
        }
        st->module_scratch[1] = key_idx + 1;
        return (ApCmd){ "PATH:step", key, assert_step_made_progress };
    }

    // -- attack foe / combat (unused — folded into PATH above) -------
    case AP_MIN_ATTACK_FOE: {
        // Not reached: the PATH phase emits Y when the Foe prompt is
        // up and transitions directly to COMBAT.
        *out_phase_done = true;
        *out_next_phase = AP_MIN_COMBAT;
        return (ApCmd){ "ATTACK_FOE:noop", 0, assert_always_true };
    }

    case AP_MIN_COMBAT: {
        if (combat_current_rendered == NULL) {
            *out_phase_done = true;
            *out_next_phase = AP_MIN_POST_COMBAT;
            return (ApCmd){ "COMBAT:ended", 0, assert_always_true };
        }
        return (ApCmd){ "COMBAT:wait", 0, assert_always_true };
    }

    case AP_MIN_POST_COMBAT: {
        if (!dialog_is_active() && !prompt_is_active() &&
            views_active() == VIEW_NONE) {
            *out_phase_done = true;
            *out_next_phase = AP_MIN_DONE;
            return (ApCmd){ "POST_COMBAT:noop", 0, assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "POST_COMBAT:space", KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "POST_COMBAT:wait", 0, assert_always_true };
    }

    case AP_MIN_DONE: {
        *out_phase_done = true;
        *out_next_phase = AP_ALL_DONE;
        return (ApCmd){ "DONE", 0, assert_always_true };
    }

    default:
        return (ApCmd){ "UNKNOWN_PHASE", 0, assert_dialog_open };
    }
}
