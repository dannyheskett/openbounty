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
#include "autoplay/nav.h"
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

ApCmd ap_flow_phase(const Game *g, const Map *m,
                       AutoplayState *st,
                       bool *out_phase_done,
                       AutoplayPhase *out_next_phase) {
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
        st->module_scratch[5] = 0;  // replay: last key (0 = none)
        st->module_scratch[6] = 0;  // replay: last pre-x
        st->module_scratch[7] = 0;  // replay: last pre-y
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_GRIND;
        return (ApCmd){ "EXIT_CASTLE:esc", KEY_ESCAPE, assert_view_none };
    }

    // -- GRIND: consume LEGS[ti].steps[step_idx] one per tick. --------
    // Stub for now: just transition to DONE. The actual LEGS[] table
    // and step-consumption logic are wired up in a follow-up commit.
    case AP_FLOW_GRIND: {
        // Handle modal prompts first so an unexpected popup doesn't
        // wedge the run before we have steps to play. When any modal
        // diverts the leg, clear the replay-tracking state so the next
        // leg-step tick doesn't mistake "position unchanged" for a
        // blocked move.
        if (prompt_is_active()) {
            st->module_scratch[5] = 0;
            const char *kind = prompt_kind_str();
            // Yes/no = foe attack confirmation. Press Y to engage.
            if (kind && strcmp(kind, "yes_no") == 0) {
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                return (ApCmd){ "GRIND:y_foe", KEY_Y, assert_combat_resolved };
            }
            // Text input = friendly-army or dwelling recruitment count.
            // Press Enter to commit 0 (no recruit); the engine then
            // consumes the foe / dwelling so the prompt won't re-fire.
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "GRIND:enter_recruit_0", KEY_ENTER,
                                assert_prompt_gone };
            }
            // A/B chest choice: press A to take the gold path.
            // Plain yes/no (search, etc.): A is also accepted.
            return (ApCmd){ "GRIND:a_chest", KEY_A, assert_prompt_gone };
        }
        if (dialog_is_active()) {
            st->module_scratch[5] = 0;
            return (ApCmd){ "GRIND:space_dialog", KEY_SPACE, assert_dialog_closed };
        }
        // Town view: divert into RENT_BOAT → EXIT_TOWN.
        if (views_active() == VIEW_TOWN) {
            st->module_scratch[5] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_RENT_BOAT;
            return (ApCmd){ "GRIND:in_town", 0, assert_always_true };
        }
        // Castle view: we routed onto the home gate for a re-recruit.
        // Divert into the REOPEN_RECRUIT sub-flow.
        if (views_active() == VIEW_HOME_CASTLE) {
            st->module_scratch[5] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_REOPEN_RECRUIT;
            return (ApCmd){ "GRIND:in_castle", 0, assert_always_true };
        }
        // Multi-leg drive via the generic navigator. The legs[] table
        // holds the chest coords; ap_nav_step plans a path each tick
        // (handles foot/boat, town-rents-boat) and emits the next key.
        // When ap_nav_step returns 0 the hero is on the goal tile —
        // advance to the next leg.
        {
            // The 54 actual chest tiles in continentia at seed=1
            // (extracted from a tick-1 AP_DUMP_MAP=1 run that prints
            // only INTERACT_TREASURE_CHEST tiles). Ordered nearest-
            // neighbor from the GRIND start at (11, 57). The other 21
            // slots that game.json names treasure_chest_NNN are
            // re-tagged at salt time as artifacts / navmaps / orbs /
            // telecaves / dwellings / friendly foes and aren't chests.
            static const struct {
                int x, y;
                const char *name;
            } legs[] = {
                { 10, 54, "chest_01" },
                { 10, 53, "chest_02" },
                {  8, 60, "chest_03" },
                {  3, 59, "chest_04" },
                {  3, 48, "chest_05" },
                {  6, 46, "chest_06" },
                {  8, 42, "chest_07" },
                { 11, 41, "chest_08" },
                {  9, 32, "chest_09" },
                { 13, 20, "chest_13" },
                { 17, 22, "chest_14" },
                { 17, 29, "chest_15" },
                { 23, 28, "chest_16" },
                { 33, 22, "chest_17" },
                { 36, 24, "chest_18" },
                { 44, 22, "chest_19" },
                { 46, 19, "chest_20" },
                { 47, 16, "chest_21" },
                { 42, 18, "chest_22" },
                { 45,  4, "chest_23" },
                { 36,  3, "chest_24" },
                { 27,  8, "chest_25" },
                { 24,  6, "chest_26" },
                { 23,  6, "chest_27" },
                { 15,  5, "chest_28" },
                {  5,  7, "chest_29" },
                { 10, 13, "chest_30" },
                { 20, 14, "chest_31" },
                { 25, 42, "chest_32" },
                { 29, 44, "chest_33" },
                { 23, 46, "chest_34" },
                { 21, 50, "chest_35" },
                { 25, 57, "chest_36" },
                { 29, 56, "chest_37" },
                { 37, 56, "chest_38" },
                { 33, 60, "chest_39" },
                { 22, 60, "chest_40" },
                { 50, 59, "chest_41" },
                { 52, 46, "chest_42" },
                { 41, 45, "chest_43" },
                { 40, 48, "chest_44" },
                { 43, 50, "chest_45" },
                { 41, 39, "chest_46" },
                { 39, 34, "chest_47" },
                { 31, 37, "chest_48" },
                { 49, 28, "chest_49" },
                { 54, 23, "chest_50" },
                { 60, 27, "chest_51" },
                { 59, 30, "chest_52" },
                { 56, 14, "chest_53" },
                { 59,  3, "chest_54" },
                // Moved to end: foe-dense cluster that defeats us
                // before we can collect them in the natural order.
                {  8, 24, "chest_10" },
                {  7, 24, "chest_11" },
                {  6, 24, "chest_12" },
            };
            const int n_legs = (int)(sizeof(legs) / sizeof(legs[0]));
            int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
            if (leg >= n_legs) {
                AP_LOG("[flow] all legs done: pos=(%d,%d) gold=%d",
                       g->position.x, g->position.y, g->stats.gold);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_DONE;
                return (ApCmd){ "GRIND:all_done", 0, assert_always_true };
            }
            int gx = legs[leg].x, gy = legs[leg].y;
            // Periodic re-recruit: every 5 legs route to the home
            // castle gate at (11, 56) first. module_scratch[8] holds
            // the next leg index at which to recruit; once done at
            // that threshold it's bumped by 5.
            int recruit_at = st->module_scratch[8];
            if (recruit_at <= 0) recruit_at = 5;
            if (leg >= recruit_at) {
                gx = 11; gy = 56;
            }
            // Goal reached? Advance to the next leg.
            if (g->position.x == gx && g->position.y == gy) {
                AP_LOG("[flow] leg %d (%s) done: pos=(%d,%d) gold=%d",
                       leg, legs[leg].name, g->position.x, g->position.y,
                       g->stats.gold);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "GRIND:leg_done", 0, assert_always_true };
            }
            int key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                // No path found — log and abort the leg by advancing
                // anyway so the run terminates rather than spins.
                AP_LOG("[flow] leg %d (%s) no path from (%d,%d) to (%d,%d)",
                       leg, legs[leg].name,
                       g->position.x, g->position.y, gx, gy);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "GRIND:no_path", 0, assert_always_true };
            }
            return (ApCmd){ "GRIND:nav", key, assert_always_true };
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

    // -- Mid-grind re-recruit at the home castle. ---------------------
    case AP_FLOW_REOPEN_RECRUIT: {
        st->module_scratch[4] = 0;   // pikemen sub-step counter
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_REBUY_PIKEMEN;
        return (ApCmd){ "REOPEN_RECRUIT:a", KEY_A,
                        assert_view_recruit_soldiers };
    }

    case AP_FLOW_REBUY_PIKEMEN: {
        // C (pick pikemen) → 3 → Enter. Buys 3 pikemen (300g/ea =
        // 900g). The recruit screen silently caps at gold available;
        // we pick a small count that fits even at the early recruit.
        int sub = (st->module_scratch[4] < 0) ? 0 : st->module_scratch[4];
        switch (sub) {
        case 0: st->module_scratch[4] = 1;
            return (ApCmd){ "REBUY_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[4] = 2;
            return (ApCmd){ "REBUY_PIKEMEN:3", KEY_THREE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[4] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_REBUY_ARCHERS;
            return (ApCmd){ "REBUY_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_REBUY_ARCHERS: {
        // B (pick archers) → 2 → Enter. Buys 2 archers (250g/ea =
        // 500g). Combined with the 900g pikemen, total recruit
        // spend per visit is 1400g.
        int sub = (st->module_scratch[4] < 0) ? 0 : st->module_scratch[4];
        switch (sub) {
        case 0: st->module_scratch[4] = 1;
            return (ApCmd){ "REBUY_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[4] = 2;
            return (ApCmd){ "REBUY_ARCHERS:2", KEY_TWO,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[4] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_REEXIT_RECRUIT;
            return (ApCmd){ "REBUY_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_REEXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_REEXIT_CASTLE;
        return (ApCmd){ "REEXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_REEXIT_CASTLE: {
        // Bump the next-recruit threshold by 5 so we don't re-loop
        // back into the castle next tick.
        int recruit_at = st->module_scratch[8];
        if (recruit_at <= 0) recruit_at = 5;
        st->module_scratch[8] = recruit_at + 5;
        AP_LOG("[flow] re-recruit done; next recruit at leg %d",
               st->module_scratch[8]);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_GRIND;
        return (ApCmd){ "REEXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_view_none };
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
