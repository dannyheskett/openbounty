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
#include "tables.h"
#include "pending.h"

#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stddef.h>
#include <string.h>

// Dump the hero's army composition + (when active) the foe garrison
// for the pending castle or wandering-army encounter. Called right
// before pressing Y to engage in the GRIND / HUNT yes_no handlers.
static void dump_combat_start(const Game *g, const char *who) {
    AP_LOG("[combat] %s @ pos=(%d,%d) gold=%d siege=%d",
           who ? who : "(?)", g->position.x, g->position.y,
           g->stats.gold, g->stats.siege_weapons);
    int hero_hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count == 0) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        int hp = t ? t->hit_points * g->army[i].count : 0;
        hero_hp += hp;
        AP_LOG("[combat]   hero[%d]: %d x %s (hp=%d)",
               i, g->army[i].count, g->army[i].id, hp);
    }
    AP_LOG("[combat]   hero total HP=%d", hero_hp);
    const char *hdr = prompt_header_text();
    if (hdr) AP_LOG("[combat]   foe prompt header: '%s'", hdr);

    // If a castle attack: dump the castle garrison.
    if (pending_castle_id[0]) {
        const CastleRecord *cr = GameFindCastleConst(g, pending_castle_id);
        if (cr) {
            AP_LOG("[combat]   castle='%s' owner=%d villain='%s'",
                   cr->id, (int)cr->owner_kind, cr->villain_id);
            int foe_hp = 0;
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (!cr->garrison[i].id[0] || cr->garrison[i].count == 0) continue;
                const TroopDef *t = troop_by_id(cr->garrison[i].id);
                int hp = t ? t->hit_points * cr->garrison[i].count : 0;
                foe_hp += hp;
                AP_LOG("[combat]   foe[%d]: %d x %s (hp=%d)",
                       i, cr->garrison[i].count, cr->garrison[i].id, hp);
            }
            AP_LOG("[combat]   foe total HP=%d ratio=%.2f",
                   foe_hp, foe_hp > 0 ? (float)hero_hp/(float)foe_hp : 0.0f);
        }
    }
    // If a wandering-army encounter: dump its troops.
    if (pending_foe_id[0]) {
        const FoeState *fs = GameFindFoeConst(g, pending_foe_id);
        if (fs) {
            AP_LOG("[combat]   foe_id='%s' friendly=%d alive=%d "
                   "pos=(%d,%d)", fs->placement_id, (int)fs->friendly,
                   (int)fs->alive, fs->x, fs->y);
        }
    }
}

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
        // Reset PHASE1 scratch on entry.
        st->module_scratch[0] = 0;  // leg index
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE1;
        return (ApCmd){ "EXIT_CASTLE:esc", KEY_ESCAPE, assert_view_none };
    }

    // -- PHASE 1: walk the 11 home-landmass chests reachable on foot
    //    from spawn while strictly avoiding the two lethal foes:
    //    trolls at (26,57) and giants at (7,46). Always take
    //    leadership (B) on gold chests. Run ends after the final leg
    //    returns to the home castle gate.
    case AP_FLOW_PHASE1: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE1;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE1:y_foe");
                return (ApCmd){ "PHASE1:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE1:enter_dismiss", KEY_ENTER,
                                assert_prompt_gone };
            }
            // A/B chest prompt — take leadership.
            return (ApCmd){ "PHASE1:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE1:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        {
            // Held-Karp TSP tour over the 9 chests reachable on foot
            // from spawn with chebyshev-2 exclusion around trolls
            // (26,57) and giants (7,46). Positions are runtime-true
            // chests only — the JSON's 75 treasure_chest_NNN slots
            // get re-salted at game-init into a mix of chests,
            // dwellings, telecaves, artifacts, navmaps, orbs, and
            // friendly foes; only slots that remain SALT_NONE
            // (chests) are walk-on-and-collect. Tour cost 206 steps
            // round-trip from gate. Final leg returns to the gate so
            // the run terminates cleanly at home.
            static const struct { int x, y; const char *name; } legs[] = {
                {  8, 60, "chest_slot_0" },
                { 10, 53, "chest_slot_12" },
                { 10, 54, "chest_slot_11" },
                { 37, 56, "chest_slot_8" },
                { 29, 56, "chest_slot_7" },
                {  3, 48, "chest_slot_17" },
                { 11, 41, "chest_slot_29" },
                { 21, 50, "chest_slot_14" },
                { 23, 46, "chest_slot_20" },
                { 11, 57, "return_gate" },
            };
            const int n_legs = (int)(sizeof(legs) / sizeof(legs[0]));
            int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
            if (leg >= n_legs) {
                AP_LOG("[phase1] complete: pos=(%d,%d) gold=%d hp=%d",
                       g->position.x, g->position.y, g->stats.gold,
                       ap_army_total_hp(g));
                st->module_scratch[0] = 0;  // reset for PHASE2 nav
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE2_NAV_TOWN;
                return (ApCmd){ "PHASE1:done", 0, assert_always_true };
            }
            int gx = legs[leg].x, gy = legs[leg].y;
            // Goal reached? Advance.
            if (g->position.x == gx && g->position.y == gy) {
                AP_LOG("[phase1] leg %d (%s) done: pos=(%d,%d) gold=%d",
                       leg, legs[leg].name,
                       g->position.x, g->position.y, g->stats.gold);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE1:leg_done", 0,
                                assert_always_true };
            }
            int key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase1] leg %d (%s) no path from (%d,%d) "
                       "to (%d,%d)", leg, legs[leg].name,
                       g->position.x, g->position.y, gx, gy);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE1:no_path", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE1:nav", key, assert_always_true };
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
            AP_LOG("[combat] result @ pos=(%d,%d) gold=%d hp_now=%d "
                   "defeat=%d", g->position.x, g->position.y,
                   g->stats.gold, ap_army_total_hp(g), (int)defeat);
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
                AP_LOG("[combat]   survivor[%d]: %d x %s",
                       i, g->army[i].count, g->army[i].id);
            }
            if (defeat) {
                AP_LOG("[flow] combat defeat — ending run gracefully");
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_DONE;
                return (ApCmd){ "POST_COMBAT:defeat", 0, assert_always_true };
            }
            // Resume whichever navigating phase started this combat.
            // module_scratch[3] holds the resume target; defaults to
            // PHASE1 if never set.
            AutoplayPhase resume = (AutoplayPhase)st->module_scratch[3];
            if (resume != AP_FLOW_PHASE1 &&
                resume != AP_FLOW_PHASE2_NAV_CASTLE &&
                resume != AP_FLOW_PHASE3_NAV_HOME &&
                resume != AP_FLOW_PHASE3_HUNT &&
                resume != AP_FLOW_PHASE3_NAV_HOME_2 &&
                resume != AP_FLOW_PHASE4_TOUR &&
                resume != AP_FLOW_PHASE5_NAV_CASTLE &&
                resume != AP_FLOW_PHASE5_NAV_HOME) {
                resume = AP_FLOW_PHASE1;
            }
            *out_phase_done = true;
            *out_next_phase = resume;
            return (ApCmd){ "POST_COMBAT:noop", 0, assert_always_true };
        }
        if (dialog_is_active()) {
            // Don't assert dialog closes — multi-page victory dialogs
            // (villain capture especially) take several SPACE presses
            // to clear.
            return (ApCmd){ "POST_COMBAT:space", KEY_SPACE,
                            assert_always_true };
        }
        return (ApCmd){ "POST_COMBAT:wait", 0, assert_always_true };
    }

    // -- PHASE 2 step 1: walk from gate (11,57) to Hunterville (12,60).
    case AP_FLOW_PHASE2_NAV_TOWN: {
        if (views_active() == VIEW_TOWN) {
            // Town view opened — step inside and start the menu loop.
            st->module_scratch[4] = 0;  // reset town action sub-index
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE2_TOWN_ACTIONS;
            return (ApCmd){ "PHASE2_NAV_TOWN:entered", 0,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE2_NAV_TOWN:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        int key = ap_nav_step(g, m, 12, 60);
        if (key == 0) {
            AP_LOG("[phase2] no path to Hunterville from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE2_NAV_TOWN:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE2_NAV_TOWN:nav", key, assert_always_true };
    }

    // -- PHASE 2 step 2: town menu — buy siege (E), rent boat (B),
    //    take contract (A → Murray, the cycle's first villain).
    //    Siege and contract open a town info panel that we dismiss
    //    with SPACE; boat rental on success opens no panel. State
    //    machine: clear any panel first; otherwise press the next
    //    action key based on observable game state.
    case AP_FLOW_PHASE2_TOWN_ACTIONS: {
        if (views_town_info_text() != NULL) {
            return (ApCmd){ "PHASE2_TOWN:space_info", KEY_SPACE,
                            assert_always_true };
        }
        if (!g->stats.siege_weapons) {
            return (ApCmd){ "PHASE2_TOWN:e_siege", KEY_E,
                            assert_always_true };
        }
        if (!g->boat.has_boat) {
            return (ApCmd){ "PHASE2_TOWN:b_boat", KEY_B,
                            assert_always_true };
        }
        if (!g->contract.active_id[0]) {
            return (ApCmd){ "PHASE2_TOWN:a_contract", KEY_A,
                            assert_always_true };
        }
        AP_LOG("[phase2] town actions done: siege=%d boat=%d "
               "contract=%s gold=%d",
               g->stats.siege_weapons,
               g->boat.has_boat ? 1 : 0,
               g->contract.active_id,
               g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE2_EXIT_TOWN;
        return (ApCmd){ "PHASE2_TOWN:done", 0, assert_always_true };
    }

    // -- PHASE 2 step 3: ESC out of the town view.
    case AP_FLOW_PHASE2_EXIT_TOWN: {
        if (views_active() == VIEW_NONE) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE2_NAV_CASTLE;
            return (ApCmd){ "PHASE2_EXIT_TOWN:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE2_EXIT_TOWN:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 2 step 4: sail/walk to Murray's castle gate at
    //    azram (30,36). The engine's multi-mode BFS handles boat
    //    boarding, sailing, and disembarking automatically.
    case AP_FLOW_PHASE2_NAV_CASTLE: {
        // Contract cleared = Murray captured. Hand off to Phase 3.
        if (!g->contract.active_id[0]) {
            AP_LOG("[phase2] Murray captured — contract fulfilled. "
                   "pos=(%d,%d) gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_NAV_HOME;
            return (ApCmd){ "PHASE2_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                // Castle attack prompt — fight Murray.
                st->module_scratch[3] = AP_FLOW_PHASE2_NAV_CASTLE;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE2_NAV_CASTLE:y_castle");
                return (ApCmd){ "PHASE2_NAV_CASTLE:y_castle", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE2_NAV_CASTLE:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            // A/B prompt en route — take leadership.
            return (ApCmd){ "PHASE2_NAV_CASTLE:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE2_NAV_CASTLE:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        // Goal: stand on azram's gate tile.
        if (g->position.x == 30 && g->position.y == 36) {
            AP_LOG("[phase2] reached azram gate (no fight triggered) "
                   "gold=%d hp=%d siege=%d",
                   g->stats.gold, ap_army_total_hp(g),
                   g->stats.siege_weapons);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE2_NAV_CASTLE:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, 30, 36);
        if (key == 0) {
            AP_LOG("[phase2] no path to azram from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE2_NAV_CASTLE:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE2_NAV_CASTLE:nav", key,
                        assert_always_true };
    }

    // -- PHASE 3 step 1: walk from azram back to king_maximus gate
    //    (11,57). Multi-mode BFS handles re-boarding the boat and
    //    sailing back. Lethal foes (trolls/giants) are NOT yet
    //    cleared, so the BFS could route through their envelopes —
    //    however, the gate-to-azram round trip in Phase 2 already
    //    proved the engine's multi-mode nav finds a sailing path
    //    that avoids them, and reversing that path is symmetric.
    case AP_FLOW_PHASE3_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                // Wandering-army intercept on the way home — fight it.
                st->module_scratch[3] = AP_FLOW_PHASE3_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE3_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE3_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE3_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE3_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE3_NAV_HOME:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_OPEN_RECRUIT;
            return (ApCmd){ "PHASE3_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            AP_LOG("[phase3] no path to king_maximus gate from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE3_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE3_NAV_HOME:nav", key,
                        assert_always_true };
    }

    // -- PHASE 3 step 2: step onto gate (audience or
    //    direct-to-castle screen), press A to open recruit.
    case AP_FLOW_PHASE3_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE3_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            // Castle screen open — press A to enter recruit.
            return (ApCmd){ "PHASE3_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE3_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // Standing on gate but no view yet — press UP again to
        // trigger the gate handler (or any key in the engine's
        // step path). UP into the wall just re-fires the gate.
        return (ApCmd){ "PHASE3_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    // -- PHASE 3 step 3-5: max-buy archers, pikemen, militia in
    //    that priority order. Typing "999" is silently clamped by
    //    the recruit screen to min(leadership_cap, (gold-500)/cost).
    //    Archers first because they're our anti-troll/giant
    //    ranged firepower; pikemen as melee absorbers; militia
    //    fills remaining leadership cheaply.
    case AP_FLOW_PHASE3_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE3_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE3_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_EXIT_RECRUIT;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE3_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE3_EXIT_CASTLE;
        return (ApCmd){ "PHASE3_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE3_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase3] recruit done: gold=%d hp=%d", g->stats.gold,
                   ap_army_total_hp(g));
            st->module_scratch[0] = 0;  // reset for HUNT leg index
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_HUNT;
            return (ApCmd){ "PHASE3_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE3_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 3 step 6: chest cleanup on the home landmass.
    //    The path from gate -> (8,42) enters the giants'
    //    chebyshev-2 envelope at (8,48); we fight (and win,
    //    verified seed-1) wandering_army_009 there. With the
    //    giants now dead, chest (6,46) becomes a free 6-step
    //    pickup. Trolls (26,57) remain alive — at Knight rank
    //    we can't reliably win that fight, so chest (25,57) is
    //    deferred to a future ranked-up phase.
    case AP_FLOW_PHASE3_HUNT: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE3_HUNT;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE3_HUNT:y_foe");
                return (ApCmd){ "PHASE3_HUNT:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE3_HUNT:enter_dismiss", KEY_ENTER,
                                assert_prompt_gone };
            }
            return (ApCmd){ "PHASE3_HUNT:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE3_HUNT:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        {
            static const struct { int x, y; const char *name; } legs[] = {
                {  8, 42, "chest_slot_27" },
                {  6, 46, "chest_slot_19" },
            };
            const int n_legs = (int)(sizeof(legs) / sizeof(legs[0]));
            int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
            if (leg >= n_legs) {
                AP_LOG("[phase3] hunt complete: pos=(%d,%d) gold=%d hp=%d",
                       g->position.x, g->position.y, g->stats.gold,
                       ap_army_total_hp(g));
                st->module_scratch[0] = 0;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE3_NAV_HOME_2;
                return (ApCmd){ "PHASE3_HUNT:done", 0,
                                assert_always_true };
            }
            int gx = legs[leg].x, gy = legs[leg].y;
            if (g->position.x == gx && g->position.y == gy) {
                AP_LOG("[phase3] leg %d (%s) done: pos=(%d,%d) "
                       "gold=%d hp=%d",
                       leg, legs[leg].name, g->position.x,
                       g->position.y, g->stats.gold,
                       ap_army_total_hp(g));
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE3_HUNT:leg_done", 0,
                                assert_always_true };
            }
            int key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase3] leg %d (%s) no path from (%d,%d)",
                       leg, legs[leg].name, g->position.x,
                       g->position.y);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE3_HUNT:no_path", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE3_HUNT:nav", key,
                            assert_always_true };
        }
    }

    // -- PHASE 3 extension: nav back to king_maximus, re-recruit
    //    again, then walk to Hunterville and take the next
    //    contract (Hack — the second villain in the cycle).
    case AP_FLOW_PHASE3_NAV_HOME_2: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE3_NAV_HOME_2;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE3_NAV_HOME_2:y_foe");
                return (ApCmd){ "PHASE3_NAV_HOME_2:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE3_NAV_HOME_2:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE3_NAV_HOME_2:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE3_NAV_HOME_2:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_OPEN_RECRUIT_2;
            return (ApCmd){ "PHASE3_NAV_HOME_2:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            AP_LOG("[phase3] NAV_HOME_2 no path from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE3_NAV_HOME_2:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE3_NAV_HOME_2:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE3_OPEN_RECRUIT_2: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_RECRUIT_ARCHERS_2;
            return (ApCmd){ "PHASE3_OPEN_RECRUIT_2:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE3_OPEN_RECRUIT_2:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE3_OPEN_RECRUIT_2:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE3_OPEN_RECRUIT_2:up", KEY_UP,
                        assert_always_true };
    }

    case AP_FLOW_PHASE3_RECRUIT_ARCHERS_2: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS_2:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_RECRUIT_PIKEMEN_2;
            return (ApCmd){ "PHASE3_RECRUIT_ARCHERS_2:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE3_RECRUIT_PIKEMEN_2: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN_2:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_RECRUIT_MILITIA_2;
            return (ApCmd){ "PHASE3_RECRUIT_PIKEMEN_2:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE3_RECRUIT_MILITIA_2: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA_2:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA_2:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_EXIT_RECRUIT_2;
            return (ApCmd){ "PHASE3_RECRUIT_MILITIA_2:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE3_EXIT_RECRUIT_2: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE3_EXIT_CASTLE_2;
        return (ApCmd){ "PHASE3_EXIT_RECRUIT_2:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE3_EXIT_CASTLE_2: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase3] re-recruit done: gold=%d hp=%d",
                   g->stats.gold, ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_NAV_TOWN;
            return (ApCmd){ "PHASE3_EXIT_CASTLE_2:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE3_EXIT_CASTLE_2:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    case AP_FLOW_PHASE3_NAV_TOWN: {
        if (views_active() == VIEW_TOWN) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_TOWN_ACTIONS;
            return (ApCmd){ "PHASE3_NAV_TOWN:entered", 0,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE3_NAV_TOWN:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        int key = ap_nav_step(g, m, 12, 60);
        if (key == 0) {
            AP_LOG("[phase3] NAV_TOWN no path from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE3_NAV_TOWN:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE3_NAV_TOWN:nav", key,
                        assert_always_true };
    }

    // Take the next contract (Hack) — Murray is captured so the
    // cycle advances past him on the next A-press. Dismiss the
    // info panel, then exit.
    case AP_FLOW_PHASE3_TOWN_ACTIONS: {
        if (views_town_info_text() != NULL) {
            return (ApCmd){ "PHASE3_TOWN:space_info", KEY_SPACE,
                            assert_always_true };
        }
        if (!g->contract.active_id[0]) {
            return (ApCmd){ "PHASE3_TOWN:a_contract", KEY_A,
                            assert_always_true };
        }
        AP_LOG("[phase3] Hack contract taken: active=%s gold=%d",
               g->contract.active_id, g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE3_EXIT_TOWN;
        return (ApCmd){ "PHASE3_TOWN:done", 0, assert_always_true };
    }

    case AP_FLOW_PHASE3_EXIT_TOWN: {
        if (views_active() == VIEW_NONE) {
            st->module_scratch[0] = 0;  // reset PHASE4 leg index
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE4_TOUR;
            return (ApCmd){ "PHASE3_EXIT_TOWN:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE3_EXIT_TOWN:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 4: chest tour over all uncollected Continentia
    //    chests reachable from gate via boat+foot without entering
    //    any alive wandering-army's chebyshev-2 pursuit envelope.
    //    Always pick KEY_B (leadership) so the run accumulates
    //    leadership_base for the Hack fight in Phase 5.
    //    Leg order from a nearest-neighbor TSP over the 28 nodes.
    case AP_FLOW_PHASE4_TOUR: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                // A foe wandered into us — fight defensively.
                st->module_scratch[3] = AP_FLOW_PHASE4_TOUR;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE4_TOUR:y_foe");
                return (ApCmd){ "PHASE4_TOUR:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE4_TOUR:enter_dismiss", KEY_ENTER,
                                assert_prompt_gone };
            }
            // A/B chest prompt — leadership until we have enough
            // headroom to recruit a Hack-beating army, then gold so we
            // can actually afford the recruit. Threshold of 200
            // leadership_base = ~7 more archers above the carried
            // 13 + 9 pikemen, and frees the rest of the tour to pile
            // up gold for the post-tour recruit.
            if (g->stats.leadership_base < 160) {
                return (ApCmd){ "PHASE4_TOUR:b_chest", KEY_B,
                                assert_prompt_gone };
            }
            return (ApCmd){ "PHASE4_TOUR:a_chest", KEY_A,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE4_TOUR:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        {
            // 28-leg NN tour computed offline. Tour cost ~662 steps
            // round-trip from gate. Final leg returns to gate.
            static const struct { int x, y; const char *name; } legs[] = {
                {  3, 59, "chest_slot_3" },
                { 22, 60, "chest_slot_1" },
                { 33, 60, "chest_slot_2" },
                { 50, 59, "chest_slot_5" },
                { 52, 46, "chest_slot_21" },
                { 41, 45, "chest_slot_23" },
                { 41, 39, "chest_slot_31" },
                { 39, 34, "chest_slot_34" },
                { 31, 37, "chest_slot_33" },
                { 17, 29, "chest_slot_41" },
                { 23, 28, "chest_slot_42" },
                { 25, 42, "chest_slot_28" },
                { 29, 44, "chest_slot_25" },
                { 49, 28, "chest_slot_43" },
                { 54, 23, "chest_slot_50" },
                { 44, 22, "chest_slot_54" },
                { 47, 16, "chest_slot_59" },
                { 46, 19, "chest_slot_56" },
                { 45,  4, "chest_slot_72" },
                { 59,  3, "chest_slot_74" },
                { 56, 14, "chest_slot_62" },
                { 60, 27, "chest_slot_45" },
                { 59, 30, "chest_slot_39" },
                { 27,  8, "chest_slot_64" },
                { 20, 14, "chest_slot_60" },
                { 17, 22, "chest_slot_52" },
                { 13, 20, "chest_slot_55" },
                {  5,  7, "chest_slot_65" },
                { 11, 57, "return_gate" },
            };
            const int n_legs = (int)(sizeof(legs) / sizeof(legs[0]));
            int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
            if (leg >= n_legs) {
                AP_LOG("[phase4] tour complete: pos=(%d,%d) gold=%d "
                       "hp=%d lead_base=%d",
                       g->position.x, g->position.y, g->stats.gold,
                       ap_army_total_hp(g), g->stats.leadership_base);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE4_OPEN_RECRUIT;
                return (ApCmd){ "PHASE4_TOUR:done", 0,
                                assert_always_true };
            }
            int gx = legs[leg].x, gy = legs[leg].y;
            if (g->position.x == gx && g->position.y == gy) {
                AP_LOG("[phase4] leg %d (%s) done: pos=(%d,%d) gold=%d "
                       "lead_base=%d",
                       leg, legs[leg].name, g->position.x, g->position.y,
                       g->stats.gold, g->stats.leadership_base);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE4_TOUR:leg_done", 0,
                                assert_always_true };
            }
            int key = ap_nav_step_avoiding_foes(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase4] leg %d (%s) no foe-free path from "
                       "(%d,%d), skipping",
                       leg, legs[leg].name, g->position.x, g->position.y);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE4_TOUR:no_path", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE4_TOUR:nav", key,
                            assert_always_true };
        }
    }

    // -- Post-Phase-4 re-recruit: walk into king_maximus and
    //    max-buy archers + pikemen + militia again so the army
    //    grows to fill the new leadership cap.
    case AP_FLOW_PHASE4_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE4_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE4_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE4_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE4_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE4_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    case AP_FLOW_PHASE4_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE4_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE4_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE4_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE4_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE4_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE4_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE4_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE4_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE4_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE4_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE4_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE4_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE4_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE4_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE4_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE4_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE4_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE4_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE4_EXIT_RECRUIT;
            return (ApCmd){ "PHASE4_RECRUIT_MILITIA:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE4_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE4_EXIT_CASTLE;
        return (ApCmd){ "PHASE4_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE4_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase4] post-tour re-recruit done: gold=%d "
                   "hp=%d lead_base=%d",
                   g->stats.gold, ap_army_total_hp(g),
                   g->stats.leadership_base);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE5_NAV_CASTLE;
            return (ApCmd){ "PHASE4_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE4_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 5 step 1: sail/walk to Hack's castle gate at
    //    faxis (22,14). Multi-mode BFS handles boat boarding,
    //    sailing, and disembarking.
    case AP_FLOW_PHASE5_NAV_CASTLE: {
        // Contract cleared = Hack captured. Sail home.
        if (!g->contract.active_id[0]) {
            AP_LOG("[phase5] Hack captured — contract fulfilled. "
                   "pos=(%d,%d) gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE5_NAV_HOME;
            return (ApCmd){ "PHASE5_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE5_NAV_CASTLE;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE5_NAV_CASTLE:y_castle");
                return (ApCmd){ "PHASE5_NAV_CASTLE:y_castle", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE5_NAV_CASTLE:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE5_NAV_CASTLE:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE5_NAV_CASTLE:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 22 && g->position.y == 14) {
            AP_LOG("[phase5] reached faxis gate (no fight triggered) "
                   "gold=%d hp=%d siege=%d",
                   g->stats.gold, ap_army_total_hp(g),
                   g->stats.siege_weapons);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE5_NAV_CASTLE:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, 22, 14);
        if (key == 0) {
            AP_LOG("[phase5] no path to faxis from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE5_NAV_CASTLE:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE5_NAV_CASTLE:nav", key,
                        assert_always_true };
    }

    // -- PHASE 5 step 2: sail/walk back to king_maximus gate.
    case AP_FLOW_PHASE5_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE5_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE5_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE5_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE5_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE5_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE5_NAV_HOME:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            AP_LOG("[phase5] reached king_maximus gate. pos=(%d,%d) "
                   "gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE5_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            AP_LOG("[phase5] NAV_HOME no path from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE5_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE5_NAV_HOME:nav", key,
                        assert_always_true };
    }

    // BUY_SIEGE / EXIT_TOWN retained as legacy stubs (unreached
    // by the current flow) so the AutoplayPhase enum's enumerators
    // all have switch cases and the build stays clean.
    case AP_FLOW_BUY_SIEGE:
    case AP_FLOW_EXIT_TOWN:
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        return (ApCmd){ "LEGACY:skip", 0, assert_always_true };


    case AP_FLOW_DONE: {
        *out_phase_done = true;
        *out_next_phase = AP_ALL_DONE;
        return (ApCmd){ "DONE", 0, assert_always_true };
    }

    default:
        return (ApCmd){ "UNKNOWN_PHASE", 0, assert_dialog_open };
    }
}
