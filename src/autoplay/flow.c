// Autoplay flow — per-phase command emitter.
//
// Each phase function returns ONE ApCmd per tick. The dispatcher in
// core.c runs the command, advances one tick, asserts the command's
// post-state predicate. Hard fail on assertion failure.
//
// Sequence (seed=1):
//   intro → walk to Maximus → recruit → exit castle → GRIND (consumes
//   hand-authored step lists, one per leg) → done.
//
// The town visit (buy siege weapons + rent boat) is handled inline by
// the GRIND phase when it detects VIEW_TOWN opening as a side effect
// of a step. Modal phases (BUY_SIEGE/RENT_BOAT/EXIT_TOWN) return
// control to GRIND when they finish.

#include "autoplay/internal.h"
#include "combat.h"

#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stddef.h>
#include <string.h>

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

static bool assert_prompt_gone(const Game *g) {
    (void)g; return !prompt_is_active();
}

// Combat opened OR combat already finished (regardless of outcome).
// POST_COMBAT detects loss and exits gracefully.
static bool assert_combat_resolved(const Game *g) {
    (void)g; return true;
}

// =========================================================================
// Phase dispatch
// =========================================================================

// =========================================================================
// Per-chest leg stubs (seed=1 continentia, 54 chests).
//
// Each function returns the next direction key to press to walk the
// hero one tile toward the named chest, given the current step index
// inside this leg. Step index 0 = the first step of this leg (i.e.,
// the hero is at wherever the previous chest's leg ended). When the
// list is exhausted the function returns 0 (sentinel).
//
// All step lists are EMPTY for now (stub). Author each list inline
// by hand-tracing the seed=1 map. Returning 0 from step 0 means
// "leg has no steps" — GRIND treats that as "leg done, advance".
// =========================================================================

// treasure_chest_073 @ (36, 3)
//
// Hero starts this leg at (11, 57) — the tile they're on when GRIND
// first runs (post-EXIT_CASTLE from the home castle gate at (11, 56)).
// chest_073 is on the northern landmass, unreachable on foot, so the
// leg walks south to Hunterville town at (12, 60), the town-modal
// auto-rents a boat (boat spawns at (11, 60)), then the hero boards
// and sails the long way around — south through row 61, west along
// the south coast, up the west edge to row 0, then east across the
// top to (36, 2), and finally one step south onto the chest at
// (36, 3). The chest prompt + flavor dialog are handled by GRIND's
// modal interceptors (KEY_A + KEY_SPACE), so this list contains only
// the movement keys.
static int leg_chest_073_step(int step_idx) {
    // BFS-derived path from start (11, 57) to chest at (36, 3):
    //   walk D D R D    — into Hunterville at (12, 60); town flow auto-rents boat
    //   board L D       — back to (11, 59) then south onto boat tile (11, 60)
    //   sail 103 steps  — strict water-only BFS from (11, 60) to (36, 2)
    //   step D          — disembark south onto chest tile (36, 3)
    static const int keys[] = {
        KEY_DOWN, KEY_DOWN, KEY_RIGHT, KEY_DOWN, KEY_LEFT, KEY_DOWN, KEY_DOWN, KEY_LEFT,
        KEY_LEFT, KEY_LEFT, KEY_LEFT, KEY_UP, KEY_LEFT, KEY_LEFT, KEY_UP, KEY_UP,
        KEY_LEFT, KEY_UP, KEY_LEFT, KEY_UP, KEY_UP, KEY_LEFT, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
        KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
        KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
        KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
        KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_DOWN,
    };
    const int n = (int)(sizeof(keys) / sizeof(keys[0]));
    if (step_idx < 0 || step_idx >= n) return 0;
    return keys[step_idx];
}

// treasure_chest_074 @ (59, 3)
//
// Hero starts at (36, 3) — on the chest_073 tile, on foot, boat parked
// just north at (36, 2). chest_074 is on a separate island further
// east, also unreachable by foot. Re-board, sail east along row 2,
// disembark south onto the chest at (59, 3).
static int leg_chest_074_step(int step_idx) {
    // BFS-derived path from (36, 3) to chest at (59, 3):
    //   board U     — step north onto boat at (36, 2); travel_mode=BOAT
    //   sail R x23  — east along row 2 (all water) to (59, 2)
    //   step D      — disembark south onto chest tile (59, 3)
    static const int keys[] = {
        KEY_UP,
        KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
        KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
        KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
        KEY_DOWN,
    };
    const int n = (int)(sizeof(keys) / sizeof(keys[0]));
    if (step_idx < 0 || step_idx >= n) return 0;
    return keys[step_idx];
}

// treasure_chest_072 @ (45, 4)
static int leg_chest_072_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_071 @ (15, 5)
static int leg_chest_071_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_066 @ (23, 6)
static int leg_chest_066_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_067 @ (24, 6)
static int leg_chest_067_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_065 @ (5, 7)
static int leg_chest_065_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_064 @ (27, 8)
static int leg_chest_064_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_063 @ (10, 13)
static int leg_chest_063_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_060 @ (20, 14)
static int leg_chest_060_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_062 @ (56, 14)
static int leg_chest_062_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_059 @ (47, 16)
static int leg_chest_059_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_057 @ (42, 18)
static int leg_chest_057_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_056 @ (46, 19)
static int leg_chest_056_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_055 @ (13, 20)
static int leg_chest_055_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_052 @ (17, 22)
static int leg_chest_052_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_053 @ (33, 22)
static int leg_chest_053_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_054 @ (44, 22)
static int leg_chest_054_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_050 @ (54, 23)
static int leg_chest_050_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_046 @ (6, 24)
static int leg_chest_046_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_047 @ (7, 24)
static int leg_chest_047_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_048 @ (8, 24)
static int leg_chest_048_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_049 @ (36, 24)
static int leg_chest_049_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_045 @ (60, 27)
static int leg_chest_045_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_042 @ (23, 28)
static int leg_chest_042_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_043 @ (49, 28)
static int leg_chest_043_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_041 @ (17, 29)
static int leg_chest_041_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_039 @ (59, 30)
static int leg_chest_039_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_036 @ (9, 32)
static int leg_chest_036_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_034 @ (39, 34)
static int leg_chest_034_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_033 @ (31, 37)
static int leg_chest_033_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_031 @ (41, 39)
static int leg_chest_031_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_029 @ (11, 41)
static int leg_chest_029_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_027 @ (8, 42)
static int leg_chest_027_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_028 @ (25, 42)
static int leg_chest_028_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_025 @ (29, 44)
static int leg_chest_025_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_023 @ (41, 45)
static int leg_chest_023_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_019 @ (6, 46)
static int leg_chest_019_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_020 @ (23, 46)
static int leg_chest_020_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_021 @ (52, 46)
static int leg_chest_021_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_017 @ (3, 48)
static int leg_chest_017_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_018 @ (40, 48)
static int leg_chest_018_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_014 @ (21, 50)
static int leg_chest_014_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_015 @ (43, 50)
static int leg_chest_015_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_012 @ (10, 53)
static int leg_chest_012_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_011 @ (10, 54)
static int leg_chest_011_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_007 @ (29, 56)
static int leg_chest_007_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_008 @ (37, 56)
static int leg_chest_008_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_006 @ (25, 57)
static int leg_chest_006_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_003 @ (3, 59)
static int leg_chest_003_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_005 @ (50, 59)
static int leg_chest_005_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_000 @ (8, 60)
static int leg_chest_000_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_001 @ (22, 60)
static int leg_chest_001_step(int step_idx) {
    (void)step_idx; return 0;
}

// treasure_chest_002 @ (33, 60)
static int leg_chest_002_step(int step_idx) {
    (void)step_idx; return 0;
}

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
        // Reset GRIND scratch on entry.
        st->module_scratch[0] = 0;  // leg index
        st->module_scratch[1] = 0;  // step index within leg
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_GRIND;
        return (ApCmd){ "EXIT_CASTLE:esc", KEY_ESCAPE, assert_view_none };
    }

    // -- GRIND: consume LEGS[ti].steps[step_idx] one per tick. --------
    // Stub for now: just transition to DONE. The actual LEGS[] table
    // and step-consumption logic are wired up in a follow-up commit.
    case AP_FLOW_GRIND: {
        // Handle modal prompts first so an unexpected popup doesn't
        // wedge the run before we have steps to play.
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && strstr(hdr, "Foe")) {
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                return (ApCmd){ "GRIND:y_foe", KEY_Y, assert_combat_resolved };
            }
            return (ApCmd){ "GRIND:a_chest", KEY_A, assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "GRIND:space_dialog", KEY_SPACE, assert_dialog_closed };
        }
        // Town view: divert into RENT_BOAT → EXIT_TOWN. (BUY_SIEGE is
        // skipped here because the chest_073 leg lands in Hunterville
        // with only 1000g and siege costs 3000g; the chest_073 path
        // doesn't need siege weapons anyway.)
        if (views_active() == VIEW_TOWN) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_RENT_BOAT;
            return (ApCmd){ "GRIND:in_town", 0, assert_always_true };
        }
        // Multi-leg drive. module_scratch[0] = leg index, [1] = step
        // index within the current leg. Reset on EXIT_CASTLE (above).
        {
            int leg = (st->module_scratch[0] < 0)
                      ? 0 : st->module_scratch[0];
            int step_idx = (st->module_scratch[1] < 0)
                           ? 0 : st->module_scratch[1];
            int key = 0;
            const char *name = "GRIND:leg_step";
            const char *done_name = "GRIND:leg_done";
            switch (leg) {
            case 0:
                key = leg_chest_073_step(step_idx);
                name = "GRIND:leg_073_step";
                done_name = "GRIND:leg_073_done";
                break;
            case 1:
                key = leg_chest_074_step(step_idx);
                name = "GRIND:leg_074_step";
                done_name = "GRIND:leg_074_done";
                break;
            default:
                AP_LOG("[flow] all legs done: pos=(%d,%d) gold=%d",
                       g->position.x, g->position.y, g->stats.gold);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_DONE;
                return (ApCmd){ "GRIND:all_done", 0, assert_always_true };
            }
            if (key == 0) {
                AP_LOG("[flow] leg %d done: pos=(%d,%d) gold=%d",
                       leg, g->position.x, g->position.y, g->stats.gold);
                st->module_scratch[0] = leg + 1;
                st->module_scratch[1] = 0;
                return (ApCmd){ done_name, 0, assert_always_true };
            }
            st->module_scratch[1] = step_idx + 1;
            return (ApCmd){ name, key, assert_always_true };
        }
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
            // peasants and forfeits the boat. End the run cleanly
            // rather than spiraling.
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
            // Resume GRIND.
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_GRIND;
            return (ApCmd){ "POST_COMBAT:noop", 0, assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "POST_COMBAT:space", KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "POST_COMBAT:wait", 0, assert_always_true };
    }

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
            if (!g->stats.siege_weapons) {
                AP_LOG("[flow] BUY_SIEGE: siege_weapons still 0 after purchase!");
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
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_GRIND;
            return (ApCmd){ "EXIT_TOWN:done", 0, assert_always_true };
        }
        return (ApCmd){ "EXIT_TOWN:esc", KEY_ESCAPE, assert_always_true };
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
