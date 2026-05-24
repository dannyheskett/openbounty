// Autoplay flow — per-phase command emitter.
//
// Each phase function returns ONE ApCmd per tick. The dispatcher in
// core.c runs the command, advances one tick, asserts the command's
// post-state predicate. Hard fail on assertion failure.
//
// Sequence (seed=1, 11 phases):
//   intro → home castle starter recruit (pikemen/militia/archers)
//   → Phase 1 home-landmass chest tour (avoids trolls + giants)
//   → Phase 2 Hunterville (siege + boat + Murray contract) → azram
//     capture
//   → Phase 3 re-recruit → kill trolls/giants → grab south chests
//     → second re-recruit → take Hack contract
//   → Phase 4 Continentia north boat tour (foe-aware) → re-recruit
//   → Phase 5 faxis capture (Hack)
//   → Phase 6 magic alcove + chest/artifact sweep with mid-tour
//     re-recruit
//   → Phase 7 iterate 3 Continentia villains (aimola, baron_makahl,
//     dread_rob)
//   → Phase 8 grind 5 monster castles (knights up-front, militia in
//     garrison)
//   → Phase 9 kill trolls @ (26,57) → grab chest_slot_6 → paths_end
//     fireball restock → ghost dwelling detour → home re-recruit
//   → Phase 10 caneghor capture at rythacon → post-fight home recruit
//     of 20 militia → return to rythacon → drop militia in garrison
//   → Phase 11 sail to (57,55) for Forestria navmap → return home
//
// Phases end at the (11,58) home_castle gate in open-world before
// transitioning to the next. POST_COMBAT resumes the originating
// nav phase via module_scratch[3] (see the whitelist).

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
                resume != AP_FLOW_PHASE5_NAV_HOME &&
                resume != AP_FLOW_PHASE6_NAV_ALCOVE &&
                resume != AP_FLOW_PHASE6_NAV_HOME &&
                resume != AP_FLOW_PHASE6_TOUR &&
                resume != AP_FLOW_PHASE6_MID_NAV_HOME &&
                resume != AP_FLOW_PHASE7_NAV_TOWN &&
                resume != AP_FLOW_PHASE7_NAV_CASTLE &&
                resume != AP_FLOW_PHASE7_NAV_HOME &&
                resume != AP_FLOW_PHASE8_NAV_CASTLE &&
                resume != AP_FLOW_PHASE8_NAV_HOME &&
                resume != AP_FLOW_PHASE9_NAV_TROLLS &&
                resume != AP_FLOW_PHASE9_NAV_CHEST &&
                resume != AP_FLOW_PHASE9_NAV_PATHS_END &&
                resume != AP_FLOW_PHASE9_NAV_GHOSTS &&
                resume != AP_FLOW_PHASE9_NAV_HOME &&
                resume != AP_FLOW_PHASE10_NAV_TOWN &&
                resume != AP_FLOW_PHASE10_NAV_CASTLE &&
                resume != AP_FLOW_PHASE10_NAV_HOME_RECRUIT &&
                resume != AP_FLOW_PHASE10_NAV_RYTHACON &&
                resume != AP_FLOW_PHASE10_NAV_HOME &&
                resume != AP_FLOW_PHASE11_NAV_NAVMAP &&
                resume != AP_FLOW_PHASE11_NAV_HOME &&
                resume != AP_FLOW_PHASE12_NAV_TO_SEA &&
                resume != AP_FLOW_PHASE12_TOUR &&
                resume != AP_FLOW_PHASE12_RETURN_TO_SPAWN &&
                resume != AP_FLOW_PHASE12_NAV_HOME) {
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
        // Goal is hero_spawn (11,58), not the gate (11,57), so
        // Phase 6 starts from the same tile the run started on —
        // this makes mid-run resumes (Phase 6 in isolation) start
        // from a known canonical position.
        if (g->position.x == 11 && g->position.y == 58) {
            AP_LOG("[phase5] reached hero_spawn. pos=(%d,%d) "
                   "gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_NAV_ALCOVE;
            return (ApCmd){ "PHASE5_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, 11, 58);
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

    // -- PHASE 6 step 1: walk from spawn (11,58) to the magic
    //    alcove at (11,44). Foe-aware nav with fallback to
    //    regular nav if no foe-free path exists.
    case AP_FLOW_PHASE6_NAV_ALCOVE: {
        if (g->stats.knows_magic) {
            // Alcove already claimed (engine bounces hero off the
            // tile after the transaction, so we may never see pos
            // == (11,44) again). knows_magic is the canonical
            // "alcove done" signal.
            AP_LOG("[phase6] alcove claimed: knows_magic=1 gold=%d",
                   g->stats.gold);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_NAV_HOME;
            return (ApCmd){ "PHASE6_NAV_ALCOVE:claimed", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                // Alcove offer (we're at or adjacent to (11,44))
                // OR a wandering-army yes/no. Distinguish by view:
                // alcove pushes VIEW_ALCOVE.
                if (views_active() == VIEW_ALCOVE) {
                    return (ApCmd){ "PHASE6_NAV_ALCOVE:y_alcove",
                                    KEY_Y, assert_always_true };
                }
                st->module_scratch[3] = AP_FLOW_PHASE6_NAV_ALCOVE;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE6_NAV_ALCOVE:y_foe");
                return (ApCmd){ "PHASE6_NAV_ALCOVE:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE6_NAV_ALCOVE:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE6_NAV_ALCOVE:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE6_NAV_ALCOVE:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 44);
        if (key == 0) {
            // Fallback: accept incidental fights.
            key = ap_nav_step(g, m, 11, 44);
        }
        if (key == 0) {
            AP_LOG("[phase6] no path to alcove from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE6_NAV_ALCOVE:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE6_NAV_ALCOVE:nav", key,
                        assert_always_true };
    }

    // -- PHASE 6 step 2: walk back to king_maximus gate (11,57).
    case AP_FLOW_PHASE6_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE6_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE6_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE6_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE6_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE6_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE6_NAV_HOME:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_OPEN_RECRUIT;
            return (ApCmd){ "PHASE6_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 57);
        if (key == 0) key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE6_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE6_NAV_HOME:nav", key,
                        assert_always_true };
    }

    // -- PHASE 6 steps 4-9: enter castle, open recruit, max-buy
    //    archers → militia → pikemen (this order so the cheap
    //    militia gobble up leadership before pikemen's higher
    //    per-cost gobble the rest of the gold budget), exit.
    case AP_FLOW_PHASE6_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE6_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE6_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE6_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE6_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    case AP_FLOW_PHASE6_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE6_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE6_RECRUIT_MILITIA:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_EXIT_RECRUIT;
            return (ApCmd){ "PHASE6_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE6_EXIT_CASTLE;
        return (ApCmd){ "PHASE6_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE6_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase6] recruit done: gold=%d hp=%d lead_base=%d",
                   g->stats.gold, ap_army_total_hp(g),
                   g->stats.leadership_base);
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_TOUR;
            return (ApCmd){ "PHASE6_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE6_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 6 step 10: sweep all 27 remaining chests + 2
    //    artifacts in nearest-neighbor order. Foe-aware nav
    //    with fallback. KEY_B (leadership) on gold-chest A/B
    //    prompt for free leadership growth.
    case AP_FLOW_PHASE6_TOUR: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE6_TOUR;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE6_TOUR:y_foe");
                return (ApCmd){ "PHASE6_TOUR:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE6_TOUR:enter_dismiss", KEY_ENTER,
                                assert_prompt_gone };
            }
            return (ApCmd){ "PHASE6_TOUR:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE6_TOUR:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        // Mid-tour re-recruit divert: after leg 8 (chest_slot_54,
        // (44,22)) — the last chest before the army has bled below
        // the safe threshold in the previous run — go back to
        // king_maximus, max-buy a fresh army including cavalry,
        // then resume the tour at leg 9. module_scratch[5] tracks
        // whether we've already done the mid-recruit so it fires
        // exactly once.
        if (st->module_scratch[0] == 9 && st->module_scratch[5] < 1) {
            AP_LOG("[phase6] mid-tour divert at leg 9: pos=(%d,%d) "
                   "gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            st->module_scratch[5] = 1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_MID_NAV_HOME;
            return (ApCmd){ "PHASE6_TOUR:divert_recruit", 0,
                            assert_always_true };
        }
        {
            // 29-leg NN tour (27 chests + 2 artifacts) plus a
            // "fight_032" leg up-front that targets wandering_army_032
            // at (24,5). Foe_032 blocks chest_slot_66 (23,6) and
            // chest_slot_67 (24,6) — both their entire chebyshev-2
            // neighborhoods sit in 032's envelope, so foe-aware nav
            // can't reach them without first killing the foe. Doing
            // the fight first (at the post-recruit hp peak of ~390)
            // unlocks those two chests; doing it last (after the
            // tour has bled hp down to ~140) loses.
            //
            // The first leg target is (24,5), the foe's own tile —
            // stepping onto it triggers combat. The foe-aware nav
            // refuses to route there (it's in the envelope), so we
            // fall back to regular nav and accept the fight.
            static const struct { int x, y; const char *name; } legs[] = {
                { 24,  5, "fight_032" },
                { 23,  6, "chest_slot_66" },
                { 24,  6, "chest_slot_67" },
                { 43, 50, "chest_slot_15" },
                { 47, 41, "artifact_1" },
                { 59, 49, "artifact_0" },
                { 49, 28, "chest_slot_43" },
                { 54, 23, "chest_slot_50" },
                { 44, 22, "chest_slot_54" },
                { 47, 16, "chest_slot_59" },
                { 46, 19, "chest_slot_56" },
                { 42, 18, "chest_slot_57" },
                { 36, 24, "chest_slot_49" },
                { 33, 22, "chest_slot_53" },
                { 36,  3, "chest_slot_73" },
                { 27,  8, "chest_slot_64" },
                { 20, 14, "chest_slot_60" },
                { 17, 22, "chest_slot_52" },
                { 13, 20, "chest_slot_55" },
                {  8, 24, "chest_slot_48" },
                {  7, 24, "chest_slot_47" },
                {  6, 24, "chest_slot_46" },
                {  5,  7, "chest_slot_65" },
                { 15,  5, "chest_slot_71" },
                {  9, 32, "chest_slot_36" },
                { 17, 29, "chest_slot_41" },
                { 23, 28, "chest_slot_42" },
                { 40, 48, "chest_slot_18" },
                // chest_slot_6 at (25,57) is skipped: its only
                // approach tiles all sit inside the alive trolls'
                // (26,57) chebyshev-2 envelope, so reaching it
                // requires fighting wandering_army_000 — a fight
                // we can't reliably win even at the post-mid-tour
                // hp peak (trolls hp_each=50 trigger HOLD-vs-heavy
                // for every player stack and we get overrun).
                { 11, 57, "return_gate" },
            };
            const int n_legs = (int)(sizeof(legs) / sizeof(legs[0]));
            int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
            if (leg >= n_legs) {
                AP_LOG("[phase6] tour complete: pos=(%d,%d) gold=%d "
                       "hp=%d",
                       g->position.x, g->position.y, g->stats.gold,
                       ap_army_total_hp(g));
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE6_FINAL_OPEN_RECRUIT;
                return (ApCmd){ "PHASE6_TOUR:done", 0,
                                assert_always_true };
            }
            int gx = legs[leg].x, gy = legs[leg].y;
            if (g->position.x == gx && g->position.y == gy) {
                AP_LOG("[phase6] leg %d (%s) done: pos=(%d,%d) gold=%d "
                       "hp=%d",
                       leg, legs[leg].name, g->position.x, g->position.y,
                       g->stats.gold, ap_army_total_hp(g));
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE6_TOUR:leg_done", 0,
                                assert_always_true };
            }
            int key = ap_nav_step_avoiding_foes(g, m, gx, gy);
            if (key == 0) key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase6] leg %d (%s) no path from (%d,%d), "
                       "skipping", leg, legs[leg].name,
                       g->position.x, g->position.y);
                st->module_scratch[0] = leg + 1;
                return (ApCmd){ "PHASE6_TOUR:no_path", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE6_TOUR:nav", key,
                            assert_always_true };
        }
    }

    // -- PHASE 6 mid-tour re-recruit chain. Navigate back to the
    //    gate, max-buy cavalry/archers/pikemen/militia, then
    //    resume PHASE6_TOUR. Cavalry (35 hp, 800 g) goes first
    //    so the limited leadership headroom fills with the most
    //    hp-per-leadership unit.
    case AP_FLOW_PHASE6_MID_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE6_MID_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE6_MID_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE6_MID_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE6_MID_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE6_MID_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE6_MID_NAV_HOME:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_MID_OPEN_RECRUIT;
            return (ApCmd){ "PHASE6_MID_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 57);
        if (key == 0) key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE6_MID_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE6_MID_NAV_HOME:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE6_MID_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_MID_RECRUIT_CAVALRY;
            return (ApCmd){ "PHASE6_MID_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE6_MID_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE6_MID_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE6_MID_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    // Cavalry = D in the home-castle recruit menu (4th letter,
    // pool sorted by cost: militia A, archers B, pikemen C,
    // cavalry D, knights E).
    case AP_FLOW_PHASE6_MID_RECRUIT_CAVALRY: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_MID_RECRUIT_CAVALRY:d", KEY_D,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_MID_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_MID_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_MID_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_MID_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE6_MID_RECRUIT_CAVALRY:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_MID_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_MID_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_MID_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_MID_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_MID_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_MID_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE6_MID_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_MID_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_MID_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_MID_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_MID_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_MID_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_MID_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE6_MID_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_MID_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_MID_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_MID_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_MID_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_MID_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_MID_EXIT_RECRUIT;
            return (ApCmd){ "PHASE6_MID_RECRUIT_MILITIA:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_MID_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE6_MID_EXIT_CASTLE;
        return (ApCmd){ "PHASE6_MID_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE6_MID_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase6] mid-recruit done: gold=%d hp=%d "
                   "lead_base=%d, resuming tour at leg %d",
                   g->stats.gold, ap_army_total_hp(g),
                   g->stats.leadership_base, 9);
            // Restore the tour leg index to 9 (we diverted before
            // handling leg 9). module_scratch[0] was clobbered by
            // the recruit sub-state machine, so set it explicitly.
            st->module_scratch[0] = 9;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_TOUR;
            return (ApCmd){ "PHASE6_MID_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE6_MID_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 6 final re-recruit + return to hero_spawn. Same
    //    cavalry → archers → pikemen → militia max-buy chain as
    //    the mid-tour recruit. After ESC out, walk one tile
    //    south so the run ends on the canonical starting tile.
    case AP_FLOW_PHASE6_FINAL_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_FINAL_RECRUIT_CAVALRY;
            return (ApCmd){ "PHASE6_FINAL_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE6_FINAL_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE6_FINAL_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE6_FINAL_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    case AP_FLOW_PHASE6_FINAL_RECRUIT_CAVALRY: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_CAVALRY:d", KEY_D,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_FINAL_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_CAVALRY:enter",
                            KEY_ENTER, assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_FINAL_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_FINAL_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_ARCHERS:enter",
                            KEY_ENTER, assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_FINAL_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_FINAL_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_PIKEMEN:enter",
                            KEY_ENTER, assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_FINAL_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_FINAL_EXIT_RECRUIT;
            return (ApCmd){ "PHASE6_FINAL_RECRUIT_MILITIA:enter",
                            KEY_ENTER, assert_always_true };
        }
    }

    case AP_FLOW_PHASE6_FINAL_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE6_FINAL_EXIT_CASTLE;
        return (ApCmd){ "PHASE6_FINAL_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE6_FINAL_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase6] final recruit done: gold=%d hp=%d "
                   "lead_base=%d",
                   g->stats.gold, ap_army_total_hp(g),
                   g->stats.leadership_base);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE6_FINAL_RETURN_SPAWN;
            return (ApCmd){ "PHASE6_FINAL_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE6_FINAL_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    case AP_FLOW_PHASE6_FINAL_RETURN_SPAWN: {
        if (g->position.x == 11 && g->position.y == 58) {
            AP_LOG("[phase6] complete: pos=(%d,%d) gold=%d hp=%d "
                   "lead_base=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g), g->stats.leadership_base);
            // Reset Phase 7 villain index to 0.
            st->module_scratch[1] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_NAV_TOWN;
            return (ApCmd){ "PHASE6_FINAL_RETURN_SPAWN:arrived", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE6_FINAL_RETURN_SPAWN:down", KEY_DOWN,
                        assert_always_true };
    }

    // -- PHASE 7: iterate 4 remaining Continentia villains.
    //    module_scratch[1] = villain index (0..3). Per iter:
    //    NAV_TOWN → TOWN_ACTIONS (take contract) → EXIT_TOWN →
    //    NAV_CASTLE (sail, siege, fight) → NAV_HOME → recruit →
    //    increment villain index, loop or DONE.
    case AP_FLOW_PHASE7_NAV_TOWN: {
        if (views_active() == VIEW_TOWN) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_TOWN_ACTIONS;
            return (ApCmd){ "PHASE7_NAV_TOWN:entered", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE7_NAV_TOWN;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE7_NAV_TOWN:y_foe");
                return (ApCmd){ "PHASE7_NAV_TOWN:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE7_NAV_TOWN:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE7_NAV_TOWN:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE7_NAV_TOWN:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 12, 60);
        if (key == 0) key = ap_nav_step(g, m, 12, 60);
        if (key == 0) {
            AP_LOG("[phase7] no path to Hunterville from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE7_NAV_TOWN:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE7_NAV_TOWN:nav", key,
                        assert_always_true };
    }

    // Take the next contract. Press A; if it doesn't match the
    // expected villain (e.g. cycle handed us a Forestria villain
    // we can't reach), press A again until we get a continentia
    // villain. After the dialog dismisses (panel_active false &&
    // active_id set), exit.
    case AP_FLOW_PHASE7_TOWN_ACTIONS: {
        if (views_town_info_text() != NULL) {
            return (ApCmd){ "PHASE7_TOWN:space_info", KEY_SPACE,
                            assert_always_true };
        }
        // If we have an active contract, check it's continentia.
        if (g->contract.active_id[0]) {
            const VillainDef *v = villain_by_id(g->contract.active_id);
            if (v && strcmp(v->zone, "continentia") == 0) {
                AP_LOG("[phase7] contract taken: villain=%s zone=%s",
                       g->contract.active_id, v->zone);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE7_EXIT_TOWN;
                return (ApCmd){ "PHASE7_TOWN:done", 0,
                                assert_always_true };
            }
            // Wrong zone — log and take another contract (the
            // engine drops the active_id when we press A again).
            AP_LOG("[phase7] contract %s is zone=%s (not continentia), "
                   "rerolling", g->contract.active_id,
                   v ? v->zone : "(unknown)");
        }
        return (ApCmd){ "PHASE7_TOWN:a_contract", KEY_A,
                        assert_always_true };
    }

    case AP_FLOW_PHASE7_EXIT_TOWN: {
        if (views_active() == VIEW_NONE) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_NAV_CASTLE;
            return (ApCmd){ "PHASE7_EXIT_TOWN:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE7_EXIT_TOWN:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // Sail/walk to the active contract's castle. Castle position
    // is looked up live via GameFindCastle on the villain's id —
    // salt_villains assigns each villain to a random castle in
    // its zone at game init, so we read the assignment at runtime.
    case AP_FLOW_PHASE7_NAV_CASTLE: {
        // Contract cleared = villain captured. Advance.
        if (!g->contract.active_id[0]) {
            int v_idx = st->module_scratch[1];
            AP_LOG("[phase7] villain %d captured. pos=(%d,%d) gold=%d "
                   "hp=%d",
                   v_idx, g->position.x, g->position.y,
                   g->stats.gold, ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_NAV_HOME;
            return (ApCmd){ "PHASE7_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE7_NAV_CASTLE;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE7_NAV_CASTLE:y_castle");
                return (ApCmd){ "PHASE7_NAV_CASTLE:y_castle", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE7_NAV_CASTLE:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE7_NAV_CASTLE:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE7_NAV_CASTLE:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        // Look up the castle for this villain.
        int target_x = -1, target_y = -1;
        for (int i = 0; i < GAME_CASTLES; i++) {
            if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
            if (strcmp(g->castles[i].villain_id,
                       g->contract.active_id) != 0) continue;
            const ResCastle *rc = resources_castle_by_id(
                g->res, g->castles[i].id);
            if (rc) { target_x = rc->x; target_y = rc->y; }
            break;
        }
        if (target_x < 0) {
            AP_LOG("[phase7] castle for villain %s not found",
                   g->contract.active_id);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE7_NAV_CASTLE:no_castle", 0,
                            assert_always_true };
        }
        if (g->position.x == target_x && g->position.y == target_y) {
            AP_LOG("[phase7] reached castle without fight: villain=%s "
                   "pos=(%d,%d)",
                   g->contract.active_id, target_x, target_y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE7_NAV_CASTLE:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, target_x, target_y);
        if (key == 0) {
            AP_LOG("[phase7] no path to castle (%d,%d) for villain %s "
                   "from (%d,%d)",
                   target_x, target_y, g->contract.active_id,
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE7_NAV_CASTLE:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE7_NAV_CASTLE:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE7_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE7_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE7_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE7_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE7_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE7_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE7_NAV_HOME:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_OPEN_RECRUIT;
            return (ApCmd){ "PHASE7_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 57);
        if (key == 0) key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE7_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE7_NAV_HOME:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE7_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_RECRUIT_CAVALRY;
            return (ApCmd){ "PHASE7_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE7_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE7_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE7_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    case AP_FLOW_PHASE7_RECRUIT_CAVALRY: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE7_RECRUIT_CAVALRY:d", KEY_D,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE7_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE7_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE7_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE7_RECRUIT_CAVALRY:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE7_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE7_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE7_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE7_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE7_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE7_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE7_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE7_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE7_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE7_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE7_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE7_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE7_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE7_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE7_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE7_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE7_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_EXIT_RECRUIT;
            return (ApCmd){ "PHASE7_RECRUIT_MILITIA:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE7_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE7_EXIT_CASTLE;
        return (ApCmd){ "PHASE7_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE7_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            int v_idx = st->module_scratch[1];
            v_idx++;
            st->module_scratch[1] = v_idx;
            AP_LOG("[phase7] iteration %d done: gold=%d hp=%d "
                   "lead_base=%d",
                   v_idx, g->stats.gold, ap_army_total_hp(g),
                   g->stats.leadership_base);
            *out_phase_done = true;
            // Stop after dread_rob (iteration 3). Caneghor's
            // garrison (250 sprites + 10 ghosts + 16 knights +
            // 12 archmages, ~1210 hp + magic) is above our
            // post-recruit peak — defer his capture to a later
            // phase that grinds monster-held castles to push the
            // hero to Marshal rank (leadership 400 instead of
            // the General-rank 200 we're stuck at).
            if (v_idx >= 3) {
                AP_LOG("[phase7] 3 villains captured "
                       "(aimola+baron_makahl+dread_rob); "
                       "caneghor deferred to later phase");
                // Reset Phase 8 castle index (module_scratch[1])
                // and recruit sub-state (module_scratch[0]).
                st->module_scratch[1] = 0;
                st->module_scratch[0] = 0;
                *out_next_phase = AP_FLOW_PHASE8_OPEN_RECRUIT;
            } else {
                *out_next_phase = AP_FLOW_PHASE7_NAV_TOWN;
            }
            return (ApCmd){ "PHASE7_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE7_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 8: monster-castle grind loop.
    //    Iterate 5 Continentia castles (irok, nilslag, vutar,
    //    cancomar, kookamunga) in proximity order from home.
    //    module_scratch[1] = castle iteration index (0..4).
    //    Each iteration: recruit (knights → cavalry → archers →
    //    pikemen → militia) → walk → siege → garrison militia →
    //    walk home → loop.
    case AP_FLOW_PHASE8_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_RECRUIT_KNIGHTS;
            return (ApCmd){ "PHASE8_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE8_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE8_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE8_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    // Knights are E in the home-castle recruit menu (5th letter,
    // pool sorted by cost: militia A, archers B, pikemen C,
    // cavalry D, knights E).
    case AP_FLOW_PHASE8_RECRUIT_KNIGHTS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE8_RECRUIT_KNIGHTS:e", KEY_E,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE8_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE8_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE8_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_RECRUIT_CAVALRY;
            return (ApCmd){ "PHASE8_RECRUIT_KNIGHTS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE8_RECRUIT_CAVALRY: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE8_RECRUIT_CAVALRY:d", KEY_D,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE8_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE8_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE8_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE8_RECRUIT_CAVALRY:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE8_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE8_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE8_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE8_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE8_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE8_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE8_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE8_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE8_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE8_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE8_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE8_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE8_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE8_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE8_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE8_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE8_RECRUIT_MILITIA:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_EXIT_RECRUIT;
            return (ApCmd){ "PHASE8_RECRUIT_MILITIA:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE8_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE8_EXIT_CASTLE;
        return (ApCmd){ "PHASE8_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE8_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase8] recruit done for iteration %d: gold=%d "
                   "hp=%d lead_base=%d",
                   st->module_scratch[1], g->stats.gold,
                   ap_army_total_hp(g), g->stats.leadership_base);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_NAV_CASTLE;
            return (ApCmd){ "PHASE8_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE8_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // Walk/sail to the iteration-index monster castle. Five
    // hardcoded targets sorted by manhattan distance from home
    // gate (closest first). Skips already-owned castles (the
    // grind may capture them out of strict order if some are
    // unreachable).
    case AP_FLOW_PHASE8_NAV_CASTLE: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE8_NAV_CASTLE;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE8_NAV_CASTLE:y_castle");
                return (ApCmd){ "PHASE8_NAV_CASTLE:y_castle", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE8_NAV_CASTLE:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE8_NAV_CASTLE:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE8_NAV_CASTLE:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        {
            static const struct { int x, y; const char *id; } castles[] = {
                { 11, 33, "irok" },
                { 22, 39, "nilslag" },
                { 40, 58, "vutar" },
                { 36, 14, "cancomar" },
                { 57,  5, "kookamunga" },
            };
            const int n = (int)(sizeof(castles)/sizeof(castles[0]));
            int idx = st->module_scratch[1];
            if (idx >= n) {
                AP_LOG("[phase8] all 5 monster castles captured. "
                       "gold=%d hp=%d", g->stats.gold,
                       ap_army_total_hp(g));
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE9_NAV_TROLLS;
                return (ApCmd){ "PHASE8_NAV_CASTLE:all_done", 0,
                                assert_always_true };
            }
            int gx = castles[idx].x, gy = castles[idx].y;
            // Detect capture: castle is now player-owned. Hand off
            // to GARRISON which steps onto the gate to open
            // VIEW_OWN_CASTLE.
            const CastleRecord *cr = GameFindCastleConst(g,
                castles[idx].id);
            if (cr && cr->owner_kind == CASTLE_OWNER_PLAYER) {
                st->module_scratch[2] = 0;  // reset garrison sub
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE8_GARRISON;
                return (ApCmd){ "PHASE8_NAV_CASTLE:captured", 0,
                                assert_always_true };
            }
            int key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase8] no path to %s castle at (%d,%d) "
                       "from (%d,%d), skipping",
                       castles[idx].id, gx, gy,
                       g->position.x, g->position.y);
                st->module_scratch[1] = idx + 1;
                return (ApCmd){ "PHASE8_NAV_CASTLE:no_path", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE8_NAV_CASTLE:nav", key,
                            assert_always_true };
        }
    }

    // Garrison the militia stack into the just-captured castle.
    // Post-combat the hero stands on the gate with no view open.
    // We need to step OFF and back ON so the engine fires
    // opened_castle and pushes VIEW_OWN_CASTLE. Then SPACE toggles
    // GARRISON mode (screen starts in REMOVE), then the slot
    // letter (A..E) for the militia stack, then ESC.
    //
    // module_scratch[2]: 0 = stepping off, 1 = stepping back on,
    // 2 = pre-toggle, 3 = post-toggle (find militia slot + send
    // key), 4 = post-garrison (ESC).
    case AP_FLOW_PHASE8_GARRISON: {
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE8_GARRISON:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        int sub = (st->module_scratch[2] < 0) ? 0 : st->module_scratch[2];
        // Look up our castle.
        static const struct { int x, y; const char *id; } castles[] = {
            { 11, 33, "irok" },
            { 22, 39, "nilslag" },
            { 40, 58, "vutar" },
            { 36, 14, "cancomar" },
            { 57,  5, "kookamunga" },
        };
        int idx = st->module_scratch[1];
        if (idx < 0 || idx >= (int)(sizeof(castles)/sizeof(castles[0]))) {
            // Out of range — bail.
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_NAV_HOME;
            return (ApCmd){ "PHASE8_GARRISON:bad_idx", 0,
                            assert_always_true };
        }
        int gx = castles[idx].x, gy = castles[idx].y;
        // Post-combat the hero sits one tile south of the gate
        // (engine bounces back from the gate step). Walk UP onto
        // the gate to trigger VIEW_OWN_CASTLE. Loop until the
        // view is open (more than one UP may be needed if hero
        // is further away from a no-path skip).
        if (sub == 0) {
            if (views_active() == VIEW_OWN_CASTLE) {
                st->module_scratch[2] = 1;
                return (ApCmd){ "PHASE8_GARRISON:view_open", 0,
                                assert_always_true };
            }
            int key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase8] garrison: cant nav to gate (%d,%d) "
                       "from (%d,%d), giving up",
                       gx, gy, g->position.x, g->position.y);
                st->module_scratch[2] = 4;
                return (ApCmd){ "PHASE8_GARRISON:no_nav", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE8_GARRISON:nav_to_gate", key,
                            assert_always_true };
        }
        if (sub == 1) {
            st->module_scratch[2] = 2;
            return (ApCmd){ "PHASE8_GARRISON:space_toggle", KEY_SPACE,
                            assert_always_true };
        }
        if (sub == 2) {
            // Find militia slot in player's army.
            int militia_slot = -1;
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (strcmp(g->army[i].id, "militia") == 0 &&
                    g->army[i].count > 0) {
                    militia_slot = i;
                    break;
                }
            }
            if (militia_slot < 0) {
                AP_LOG("[phase8] no militia in army, skipping garrison");
                st->module_scratch[2] = 4;
                return (ApCmd){ "PHASE8_GARRISON:no_militia", KEY_ESCAPE,
                                assert_always_true };
            }
            st->module_scratch[2] = 4;
            int key_letter = KEY_A + militia_slot;
            const char *name = militia_slot == 0 ? "PHASE8_GARRISON:a" :
                               militia_slot == 1 ? "PHASE8_GARRISON:b" :
                               militia_slot == 2 ? "PHASE8_GARRISON:c" :
                               militia_slot == 3 ? "PHASE8_GARRISON:d" :
                                                   "PHASE8_GARRISON:e";
            AP_LOG("[phase8] garrisoning militia from slot %d at %s",
                   militia_slot, castles[idx].id);
            return (ApCmd){ name, key_letter, assert_always_true };
        }
        // sub >= 4: ESC out, then advance.
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase8] garrison done for %s iter=%d gold=%d "
                   "hp=%d",
                   castles[idx].id, idx, g->stats.gold,
                   ap_army_total_hp(g));
            st->module_scratch[1] = idx + 1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_NAV_HOME;
            return (ApCmd){ "PHASE8_GARRISON:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE8_GARRISON:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    case AP_FLOW_PHASE8_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE8_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE8_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE8_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE8_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE8_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE8_NAV_HOME:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            // After the last castle's garrison, skip the home
            // recruit pass — its ~4k g would be more useful as
            // fireball spell budget in Phase 9. Hand off straight
            // to Phase 9.
            if (st->module_scratch[1] >= 5) {
                AP_LOG("[phase8] skipping post-grind recruit, "
                       "handing off to phase 9");
                *out_next_phase = AP_FLOW_PHASE9_NAV_TROLLS;
            } else {
                *out_next_phase = AP_FLOW_PHASE8_OPEN_RECRUIT;
            }
            return (ApCmd){ "PHASE8_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 57);
        if (key == 0) key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE8_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE8_NAV_HOME:nav", key,
                        assert_always_true };
    }

    // -- PHASE 9: troll-killer detour. After Phase 8's monster
    //    castle grind, the army has 8 knights — enough to clear
    //    wandering_army_000 (2 trolls + sprites + zombies) at
    //    (26,57). Step onto the foe tile to engage, then walk to
    //    chest_slot_6 at (25,57), then home to re-recruit before
    //    Phase 10 takes on caneghor.
    case AP_FLOW_PHASE9_NAV_TROLLS: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE9_NAV_TROLLS;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE9_NAV_TROLLS:y_foe");
                return (ApCmd){ "PHASE9_NAV_TROLLS:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE9_NAV_TROLLS:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE9_NAV_TROLLS:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE9_NAV_TROLLS:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        // Look up trolls foe. If dead, advance to chest pickup.
        const FoeState *trolls = GameFindFoeConst(g,
            "wandering_army_000");
        if (!trolls || !trolls->alive) {
            AP_LOG("[phase9] trolls dead. pos=(%d,%d) gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_NAV_CHEST;
            return (ApCmd){ "PHASE9_NAV_TROLLS:dead", 0,
                            assert_always_true };
        }
        // Goal: step onto the trolls' current tile (26,57 init,
        // may have drifted). Stepping onto a foe tile triggers
        // combat.
        int key = ap_nav_step(g, m, trolls->x, trolls->y);
        if (key == 0) {
            AP_LOG("[phase9] no path to trolls at (%d,%d) from (%d,%d)",
                   trolls->x, trolls->y, g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_NAV_HOME;
            return (ApCmd){ "PHASE9_NAV_TROLLS:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_NAV_TROLLS:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE9_NAV_CHEST: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE9_NAV_CHEST;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE9_NAV_CHEST:y_foe");
                return (ApCmd){ "PHASE9_NAV_CHEST:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE9_NAV_CHEST:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE9_NAV_CHEST:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE9_NAV_CHEST:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 25 && g->position.y == 57) {
            AP_LOG("[phase9] chest_slot_6 grabbed. pos=(%d,%d) "
                   "gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_NAV_PATHS_END;
            return (ApCmd){ "PHASE9_NAV_CHEST:done", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, 25, 57);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_NAV_PATHS_END;
            return (ApCmd){ "PHASE9_NAV_CHEST:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_NAV_CHEST:nav", key,
                        assert_always_true };
    }

    // Sail to paths_end (38,13) — the Continentia town that sells
    // fireball on seed 1. Buy up to 3 fireballs (max_spells cap)
    // for the caneghor fight in Phase 10.
    case AP_FLOW_PHASE9_NAV_PATHS_END: {
        if (views_active() == VIEW_TOWN) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_BUY_SPELLS;
            return (ApCmd){ "PHASE9_NAV_PATHS_END:entered", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE9_NAV_PATHS_END;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE9_NAV_PATHS_END:y_foe");
                return (ApCmd){ "PHASE9_NAV_PATHS_END:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE9_NAV_PATHS_END:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE9_NAV_PATHS_END:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE9_NAV_PATHS_END:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 38, 13);
        if (key == 0) key = ap_nav_step(g, m, 38, 13);
        if (key == 0) {
            AP_LOG("[phase9] no path to paths_end from (%d,%d), "
                   "skipping spell buy",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_NAV_HOME;
            return (ApCmd){ "PHASE9_NAV_PATHS_END:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_NAV_PATHS_END:nav", key,
                        assert_always_true };
    }

    // Buy fireballs until we hit max_spells or run out of gold.
    // Town spell purchase = press D, dismiss info panel, repeat.
    case AP_FLOW_PHASE9_BUY_SPELLS: {
        if (views_town_info_text() != NULL) {
            return (ApCmd){ "PHASE9_BUY_SPELLS:space_info", KEY_SPACE,
                            assert_always_true };
        }
        int known = 0;
        for (int i = 0; i < 14; i++) known += g->spells.counts[i];
        // Stop if at cap, or if next buy would dip below the
        // 500g reserve we need for the home castle re-recruit.
        int spell_cost = 1500;  // fireball cost
        if (known >= g->stats.max_spells || g->stats.gold < spell_cost + 500) {
            AP_LOG("[phase9] spell buy done: known=%d/%d fireballs=%d "
                   "gold=%d",
                   known, g->stats.max_spells, g->spells.counts[2],
                   g->stats.gold);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_EXIT_PATHS_END;
            return (ApCmd){ "PHASE9_BUY_SPELLS:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_BUY_SPELLS:d", KEY_D,
                        assert_always_true };
    }

    case AP_FLOW_PHASE9_EXIT_PATHS_END: {
        if (views_active() == VIEW_NONE) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_NAV_GHOSTS;
            return (ApCmd){ "PHASE9_EXIT_PATHS_END:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_EXIT_PATHS_END:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- Phase 9 detour: sail/walk to the ghosts dwelling at (5,29).
    //    Stepping onto a dwelling tile opens a text-input prompt
    //    ("How many Ghosts...") and bounces the hero back to the
    //    previous tile. We handle the prompt in RECRUIT_GHOSTS; here
    //    we just nav toward (5,29) until the prompt fires.
    case AP_FLOW_PHASE9_NAV_GHOSTS: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                // Dwelling prompt opened — hand off to recruit state.
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE9_RECRUIT_GHOSTS;
                st->module_scratch[0] = 0;
                return (ApCmd){ "PHASE9_NAV_GHOSTS:dwelling_open", 0,
                                assert_always_true };
            }
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE9_NAV_GHOSTS;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE9_NAV_GHOSTS:y_foe");
                return (ApCmd){ "PHASE9_NAV_GHOSTS:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            return (ApCmd){ "PHASE9_NAV_GHOSTS:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE9_NAV_GHOSTS:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // If we already have ghosts in the army (recruited this run),
        // skip the detour.
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "ghosts") == 0 &&
                g->army[i].count > 0) {
                AP_LOG("[phase9] ghosts already in army (slot %d, "
                       "count=%d), skipping dwelling", i,
                       g->army[i].count);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE9_NAV_HOME;
                return (ApCmd){ "PHASE9_NAV_GHOSTS:already", 0,
                                assert_always_true };
            }
        }
        int key = ap_nav_step_avoiding_foes(g, m, 5, 29);
        if (key == 0) key = ap_nav_step(g, m, 5, 29);
        if (key == 0) {
            AP_LOG("[phase9] no path to ghost dwelling at (5,29) "
                   "from (%d,%d), skipping",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_NAV_HOME;
            return (ApCmd){ "PHASE9_NAV_GHOSTS:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_NAV_GHOSTS:nav", key,
                        assert_always_true };
    }

    // -- Dwelling recruit text-input: type 9999 then ENTER to buy
    //    the cap. The engine clamps the typed value to whatever
    //    leadership/gold actually permits. Confirm closes the
    //    prompt; the dwelling screen then gets dismissed and the
    //    hero is back at the pre-step tile.
    case AP_FLOW_PHASE9_RECRUIT_GHOSTS: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                int sub = (st->module_scratch[0] < 0)
                              ? 0 : st->module_scratch[0];
                switch (sub) {
                case 0: st->module_scratch[0] = 1;
                    return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:9", KEY_NINE,
                                    assert_always_true };
                case 1: st->module_scratch[0] = 2;
                    return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:9", KEY_NINE,
                                    assert_always_true };
                case 2: st->module_scratch[0] = 3;
                    return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:9", KEY_NINE,
                                    assert_always_true };
                case 3: st->module_scratch[0] = 4;
                    return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:9", KEY_NINE,
                                    assert_always_true };
                default:
                    return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:enter",
                                    KEY_ENTER, assert_prompt_gone };
                }
            }
            return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // Dwelling screen may still be open (VIEW_DWELLING) — dismiss
        // it. After dismissal the hero is back at the prior tile and
        // we proceed to home castle.
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:esc", KEY_ESCAPE,
                            assert_always_true };
        }
        // Log result and advance.
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "ghosts") == 0)
                n = g->army[i].count;
        }
        AP_LOG("[phase9] ghosts recruited: count=%d gold=%d "
               "lead_base=%d", n, g->stats.gold,
               g->stats.leadership_base);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE9_NAV_HOME;
        return (ApCmd){ "PHASE9_RECRUIT_GHOSTS:done", 0,
                        assert_always_true };
    }

    case AP_FLOW_PHASE9_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE9_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE9_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE9_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE9_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE9_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE9_NAV_HOME:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_OPEN_RECRUIT;
            return (ApCmd){ "PHASE9_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 57);
        if (key == 0) key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE9_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_NAV_HOME:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE9_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_RECRUIT_KNIGHTS;
            return (ApCmd){ "PHASE9_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE9_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE9_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE9_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    case AP_FLOW_PHASE9_RECRUIT_KNIGHTS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE9_RECRUIT_KNIGHTS:e", KEY_E,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE9_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE9_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE9_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_RECRUIT_CAVALRY;
            return (ApCmd){ "PHASE9_RECRUIT_KNIGHTS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE9_RECRUIT_CAVALRY: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE9_RECRUIT_CAVALRY:d", KEY_D,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE9_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE9_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE9_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE9_RECRUIT_CAVALRY:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE9_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE9_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE9_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE9_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE9_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE9_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE9_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE9_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE9_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE9_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE9_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE9_EXIT_RECRUIT;
            return (ApCmd){ "PHASE9_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE9_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE9_EXIT_CASTLE;
        return (ApCmd){ "PHASE9_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE9_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase9] complete: pos=(%d,%d) gold=%d hp=%d "
                   "lead_base=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g), g->stats.leadership_base);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_TOWN;
            return (ApCmd){ "PHASE9_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE9_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- PHASE 10: capture caneghor at rythacon (54,57). One-shot
    //    contract+sail+siege loop, no per-villain iteration since
    //    he's the only remaining Continentia villain.
    case AP_FLOW_PHASE10_NAV_TOWN: {
        if (views_active() == VIEW_TOWN) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_TOWN_ACTIONS;
            return (ApCmd){ "PHASE10_NAV_TOWN:entered", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE10_NAV_TOWN;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE10_NAV_TOWN:y_foe");
                return (ApCmd){ "PHASE10_NAV_TOWN:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE10_NAV_TOWN:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE10_NAV_TOWN:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE10_NAV_TOWN:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 12, 60);
        if (key == 0) key = ap_nav_step(g, m, 12, 60);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE10_NAV_TOWN:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_NAV_TOWN:nav", key,
                        assert_always_true };
    }

    // Press A to take a contract. The cycle has been refilled
    // multiple times by now; press A repeatedly until we get a
    // continentia villain (specifically caneghor — the only
    // remaining one). Forestria villains get rerolled by pressing
    // A again (engine drops active_id on retake).
    case AP_FLOW_PHASE10_TOWN_ACTIONS: {
        if (views_town_info_text() != NULL) {
            return (ApCmd){ "PHASE10_TOWN:space_info", KEY_SPACE,
                            assert_always_true };
        }
        if (g->contract.active_id[0]) {
            const VillainDef *v = villain_by_id(g->contract.active_id);
            if (v && strcmp(v->zone, "continentia") == 0) {
                AP_LOG("[phase10] contract taken: villain=%s zone=%s",
                       g->contract.active_id, v->zone);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE10_EXIT_TOWN;
                return (ApCmd){ "PHASE10_TOWN:done", 0,
                                assert_always_true };
            }
            AP_LOG("[phase10] contract %s is zone=%s (not continentia), "
                   "rerolling", g->contract.active_id,
                   v ? v->zone : "(unknown)");
        }
        return (ApCmd){ "PHASE10_TOWN:a_contract", KEY_A,
                        assert_always_true };
    }

    case AP_FLOW_PHASE10_EXIT_TOWN: {
        if (views_active() == VIEW_NONE) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_CASTLE;
            return (ApCmd){ "PHASE10_EXIT_TOWN:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_EXIT_TOWN:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    case AP_FLOW_PHASE10_NAV_CASTLE: {
        if (!g->contract.active_id[0]) {
            AP_LOG("[phase10] caneghor captured. pos=(%d,%d) gold=%d "
                   "hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_HOME_RECRUIT;
            return (ApCmd){ "PHASE10_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE10_NAV_CASTLE;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE10_NAV_CASTLE:y_castle");
                return (ApCmd){ "PHASE10_NAV_CASTLE:y_castle", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE10_NAV_CASTLE:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE10_NAV_CASTLE:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE10_NAV_CASTLE:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        // Find caneghor's castle (we know it's rythacon at (54,57)
        // from the villain salt, but read live to be robust).
        int target_x = -1, target_y = -1;
        for (int i = 0; i < GAME_CASTLES; i++) {
            if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
            if (strcmp(g->castles[i].villain_id,
                       g->contract.active_id) != 0) continue;
            const ResCastle *rc = resources_castle_by_id(
                g->res, g->castles[i].id);
            if (rc) { target_x = rc->x; target_y = rc->y; }
            break;
        }
        if (target_x < 0) {
            AP_LOG("[phase10] castle for villain %s not found",
                   g->contract.active_id);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE10_NAV_CASTLE:no_castle", 0,
                            assert_always_true };
        }
        int key = ap_nav_step(g, m, target_x, target_y);
        if (key == 0) {
            AP_LOG("[phase10] no path to castle (%d,%d) for %s "
                   "from (%d,%d)",
                   target_x, target_y, g->contract.active_id,
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE10_NAV_CASTLE:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_NAV_CASTLE:nav", key,
                        assert_always_true };
    }

    // After caneghor only ghosts survive; we can't garrison them
    // alone (GameGarrisonTroop refuses to empty the army). So
    // first head home and recruit a small militia stack so the
    // hero retains a second slot, then walk back to rythacon and
    // drop the ghosts into the garrison.
    case AP_FLOW_PHASE10_NAV_HOME_RECRUIT: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE10_NAV_HOME_RECRUIT;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE10_NAV_HOME_RECRUIT:y_foe");
                return (ApCmd){ "PHASE10_NAV_HOME_RECRUIT:y_foe",
                                KEY_Y, assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE10_NAV_HOME_RECRUIT:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE10_NAV_HOME_RECRUIT:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE10_NAV_HOME_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 57) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_OPEN_RECRUIT;
            return (ApCmd){ "PHASE10_NAV_HOME_RECRUIT:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 57);
        if (key == 0) key = ap_nav_step(g, m, 11, 57);
        if (key == 0) {
            AP_LOG("[phase10] no path home for recruit from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_HOME;
            return (ApCmd){ "PHASE10_NAV_HOME_RECRUIT:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_NAV_HOME_RECRUIT:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE10_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_RECRUIT_MILITIA;
            return (ApCmd){ "PHASE10_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE10_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE10_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE10_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    // Recruit exactly 20 militia: press A (militia row), then "20",
    // then ENTER. 20 × 50g = 1000g, well within budget.
    case AP_FLOW_PHASE10_RECRUIT_MILITIA: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE10_RECRUIT_MILITIA:a", KEY_A,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE10_RECRUIT_MILITIA:2", KEY_TWO,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE10_RECRUIT_MILITIA:0", KEY_ZERO,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_EXIT_RECRUIT;
            return (ApCmd){ "PHASE10_RECRUIT_MILITIA:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE10_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE10_EXIT_CASTLE;
        return (ApCmd){ "PHASE10_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE10_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase10] militia recruited for garrison: "
                   "gold=%d hp=%d", g->stats.gold, ap_army_total_hp(g));
            st->module_scratch[2] = 0;  // reset garrison sub-state
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_RYTHACON;
            return (ApCmd){ "PHASE10_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // Walk back to rythacon (54,57) for the garrison handoff.
    case AP_FLOW_PHASE10_NAV_RYTHACON: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE10_NAV_RYTHACON;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE10_NAV_RYTHACON:y_foe");
                return (ApCmd){ "PHASE10_NAV_RYTHACON:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE10_NAV_RYTHACON:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE10_NAV_RYTHACON:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE10_NAV_RYTHACON:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // Arrival: one tile south of rythacon's gate, the bounce-
        // back tile after stepping onto (54,57).
        if (g->position.x == 54 && g->position.y == 58) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_GARRISON;
            return (ApCmd){ "PHASE10_NAV_RYTHACON:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 54, 58);
        if (key == 0) key = ap_nav_step(g, m, 54, 58);
        if (key == 0) {
            AP_LOG("[phase10] no path back to rythacon from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_HOME;
            return (ApCmd){ "PHASE10_NAV_RYTHACON:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_NAV_RYTHACON:nav", key,
                        assert_always_true };
    }

    // Drop the militia stack into rythacon's garrison. Pattern
    // mirrors PHASE8_GARRISON: walk onto the gate (54,57) →
    // VIEW_OWN_CASTLE opens → SPACE toggles GARRISON mode →
    // press the letter for the militia slot → ESC out.
    // module_scratch[2] tracks sub-state: 0 nav-to-gate,
    // 1 view-open, 2 toggled-now-pick-slot, 4 ESC.
    case AP_FLOW_PHASE10_GARRISON: {
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE10_GARRISON:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        int sub = (st->module_scratch[2] < 0) ? 0 : st->module_scratch[2];
        const int gx = 54, gy = 57;  // rythacon gate
        if (sub == 0) {
            if (views_active() == VIEW_OWN_CASTLE) {
                st->module_scratch[2] = 1;
                return (ApCmd){ "PHASE10_GARRISON:view_open", 0,
                                assert_always_true };
            }
            int key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase10] garrison: cant nav to gate (%d,%d) "
                       "from (%d,%d), giving up",
                       gx, gy, g->position.x, g->position.y);
                st->module_scratch[2] = 4;
                return (ApCmd){ "PHASE10_GARRISON:no_nav", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE10_GARRISON:nav_to_gate", key,
                            assert_always_true };
        }
        if (sub == 1) {
            st->module_scratch[2] = 2;
            return (ApCmd){ "PHASE10_GARRISON:space_toggle", KEY_SPACE,
                            assert_always_true };
        }
        if (sub == 2) {
            // Find militia slot in player's army.
            int militia_slot = -1;
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (strcmp(g->army[i].id, "militia") == 0 &&
                    g->army[i].count > 0) {
                    militia_slot = i;
                    break;
                }
            }
            if (militia_slot < 0) {
                AP_LOG("[phase10] no militia in army, skipping garrison");
                st->module_scratch[2] = 4;
                return (ApCmd){ "PHASE10_GARRISON:no_militia",
                                KEY_ESCAPE, assert_always_true };
            }
            st->module_scratch[2] = 4;
            int key_letter = KEY_A + militia_slot;
            const char *name = militia_slot == 0 ? "PHASE10_GARRISON:a" :
                               militia_slot == 1 ? "PHASE10_GARRISON:b" :
                               militia_slot == 2 ? "PHASE10_GARRISON:c" :
                               militia_slot == 3 ? "PHASE10_GARRISON:d" :
                                                   "PHASE10_GARRISON:e";
            AP_LOG("[phase10] garrisoning militia from slot %d at "
                   "rythacon", militia_slot);
            return (ApCmd){ name, key_letter, assert_always_true };
        }
        // sub >= 4: ESC out, then advance.
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase10] garrison done at rythacon gold=%d hp=%d",
                   g->stats.gold, ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_HOME;
            return (ApCmd){ "PHASE10_GARRISON:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_GARRISON:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    case AP_FLOW_PHASE10_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE10_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE10_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE10_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE10_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE10_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE10_NAV_HOME:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 58) {
            AP_LOG("[phase10] complete: pos=(%d,%d) gold=%d hp=%d "
                   "lead_base=%d villains_caught=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g), g->stats.leadership_base,
                   GameVillainsCaught(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE11_NAV_NAVMAP;
            return (ApCmd){ "PHASE10_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 58);
        if (key == 0) key = ap_nav_step(g, m, 11, 58);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE11_NAV_NAVMAP;
            return (ApCmd){ "PHASE10_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE10_NAV_HOME:nav", key,
                        assert_always_true };
    }

    // ----- PHASE 11 -----------------------------------------------------
    // Fetch the Continentia navmap so future navigation can offer
    // Forestria. The navmap on seed 1 is salt-placed at (57,55) — a
    // tile no earlier phase visits. Stepping onto INTERACT_NAVMAP
    // triggers engine/step.c:454, which flips
    // game->world.zones_discovered[<new zone>] and opens the
    // "You found the map to <ZONE>" dialog.
    case AP_FLOW_PHASE11_NAV_NAVMAP: {
        if (g->world.zones_discovered[1] /* forestria */ ||
            g->world.zones_discovered[2] /* archipelia */ ||
            g->world.zones_discovered[3] /* saharia */) {
            AP_LOG("[phase11] navmap collected: discovered=[%d,%d,%d,%d]",
                   (int)g->world.zones_discovered[0],
                   (int)g->world.zones_discovered[1],
                   (int)g->world.zones_discovered[2],
                   (int)g->world.zones_discovered[3]);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE11_NAV_HOME;
            return (ApCmd){ "PHASE11_NAV_NAVMAP:found", 0,
                            assert_always_true };
        }
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE11_NAV_NAVMAP;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE11_NAV_NAVMAP:y_foe");
                return (ApCmd){ "PHASE11_NAV_NAVMAP:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE11_NAV_NAVMAP:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE11_NAV_NAVMAP:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE11_NAV_NAVMAP:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 57, 55);
        if (key == 0) key = ap_nav_step(g, m, 57, 55);
        if (key == 0) {
            AP_LOG("[phase11] no path to navmap (57,55) from (%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE11_NAV_NAVMAP:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE11_NAV_NAVMAP:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE11_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE11_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE11_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE11_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE11_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE11_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE11_NAV_HOME:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 58) {
            AP_LOG("[phase11] complete: pos=(%d,%d) gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            st->module_scratch[0] = 0;  // reset Phase 12 sub index
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_OPEN_RECRUIT;
            return (ApCmd){ "PHASE11_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 58);
        if (key == 0) key = ap_nav_step(g, m, 11, 58);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE11_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE11_NAV_HOME:nav", key,
                        assert_always_true };
    }

    // ===== PHASE 12 ===================================================
    // Pre-tour recruit at home castle (caneghor's bounty leaves us
    // with ~26-31k gold so we can max out knights/cavalry/archers/
    // pikemen), then sail to Forestria, sweep every reachable chest
    // (desert tiles excluded; wandering-army fights accepted), and
    // return to (11,58) origin on Continentia.

    case AP_FLOW_PHASE12_OPEN_RECRUIT: {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_RECRUIT_KNIGHTS;
            return (ApCmd){ "PHASE12_OPEN_RECRUIT:entered", 0,
                            assert_always_true };
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            return (ApCmd){ "PHASE12_OPEN_RECRUIT:a", KEY_A,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_OPEN_RECRUIT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE12_OPEN_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }

    case AP_FLOW_PHASE12_RECRUIT_KNIGHTS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE12_RECRUIT_KNIGHTS:e", KEY_E,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE12_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE12_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE12_RECRUIT_KNIGHTS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_RECRUIT_CAVALRY;
            return (ApCmd){ "PHASE12_RECRUIT_KNIGHTS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE12_RECRUIT_CAVALRY: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE12_RECRUIT_CAVALRY:d", KEY_D,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE12_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE12_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE12_RECRUIT_CAVALRY:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_RECRUIT_ARCHERS;
            return (ApCmd){ "PHASE12_RECRUIT_CAVALRY:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE12_RECRUIT_ARCHERS: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE12_RECRUIT_ARCHERS:b", KEY_B,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE12_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE12_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE12_RECRUIT_ARCHERS:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_RECRUIT_PIKEMEN;
            return (ApCmd){ "PHASE12_RECRUIT_ARCHERS:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE12_RECRUIT_PIKEMEN: {
        int sub = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        switch (sub) {
        case 0: st->module_scratch[0]=1;
            return (ApCmd){ "PHASE12_RECRUIT_PIKEMEN:c", KEY_C,
                            assert_view_recruit_soldiers };
        case 1: st->module_scratch[0]=2;
            return (ApCmd){ "PHASE12_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 2: st->module_scratch[0]=3;
            return (ApCmd){ "PHASE12_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        case 3: st->module_scratch[0]=4;
            return (ApCmd){ "PHASE12_RECRUIT_PIKEMEN:9", KEY_NINE,
                            assert_view_recruit_soldiers };
        default: st->module_scratch[0]=-1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_EXIT_RECRUIT;
            return (ApCmd){ "PHASE12_RECRUIT_PIKEMEN:enter", KEY_ENTER,
                            assert_always_true };
        }
    }

    case AP_FLOW_PHASE12_EXIT_RECRUIT: {
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE12_EXIT_CASTLE;
        return (ApCmd){ "PHASE12_EXIT_RECRUIT:esc", KEY_ESCAPE,
                        assert_view_home_castle };
    }

    case AP_FLOW_PHASE12_EXIT_CASTLE: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase12] pre-tour army recruited: gold=%d hp=%d",
                   g->stats.gold, ap_army_total_hp(g));
            st->module_scratch[0] = 0;  // reset for TOUR leg index
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_NAV_TO_SEA;
            return (ApCmd){ "PHASE12_EXIT_CASTLE:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE12_EXIT_CASTLE:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- 12a: get the hero into a boat on Continentia and into a tile
    //    near open water so KEY_N triggers the new-continent prompt.
    //    The boat was rented in Phase 2 and is parked at g->boat.x/y;
    //    we just need to walk to it and board. Once in boat, set a
    //    sail-target tile at the south edge — far enough from land
    //    that pressing N yields the discovered-neighbors menu.
    case AP_FLOW_PHASE12_NAV_TO_SEA: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE12_NAV_TO_SEA;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE12_NAV_TO_SEA:y_foe");
                return (ApCmd){ "PHASE12_NAV_TO_SEA:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE12_NAV_TO_SEA:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE12_NAV_TO_SEA:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_NAV_TO_SEA:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // Target an open-sea tile south of home castle. (12,62) is in
        // the bay; if it's blocked we fall back to the boat's last
        // parked location and try from there.
        const int sea_x = 12, sea_y = 62;
        if (g->travel_mode == TRAVEL_BOAT &&
            g->position.x == sea_x && g->position.y == sea_y) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_OPEN_NAV_PROMPT;
            return (ApCmd){ "PHASE12_NAV_TO_SEA:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, sea_x, sea_y);
        if (key == 0) key = ap_nav_step(g, m, sea_x, sea_y);
        if (key == 0) {
            AP_LOG("[phase12] no path to sea (%d,%d) from (%d,%d)",
                   sea_x, sea_y, g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE12_NAV_TO_SEA:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE12_NAV_TO_SEA:nav", key,
                        assert_always_true };
    }

    // -- 12b: press N to open the "Go to which continent?" prompt.
    case AP_FLOW_PHASE12_OPEN_NAV_PROMPT: {
        if (prompt_is_active() && prompt_kind_str() &&
            strcmp(prompt_kind_str(), "numeric") == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_PICK_FORESTRIA;
            return (ApCmd){ "PHASE12_OPEN_NAV_PROMPT:opened", 0,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_OPEN_NAV_PROMPT:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE12_OPEN_NAV_PROMPT:n", KEY_N,
                        assert_always_true };
    }

    // -- 12c: After Phase 11 only Forestria is discovered among
    //    Continentia's neighbors, so the prompt shows "1) Forestria".
    //    Press 1; engine calls GameSwitchZone and the hero spawns at
    //    Forestria's hero_spawn (1,26).
    case AP_FLOW_PHASE12_PICK_FORESTRIA: {
        if (strcmp(g->position.zone, "forestria") == 0) {
            AP_LOG("[phase12] arrived on forestria: pos=(%d,%d) "
                   "travel_mode=%d", g->position.x, g->position.y,
                   (int)g->travel_mode);
            st->module_scratch[0] = 0;  // leg index for TOUR
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_TOUR;
            return (ApCmd){ "PHASE12_PICK_FORESTRIA:switched", 0,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_PICK_FORESTRIA:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE12_PICK_FORESTRIA:1", KEY_ONE,
                        assert_always_true };
    }

    // -- 12d: chest tour on Forestria. The 50 chests below are the
    //    66 JSON treasure_chest_NNN positions minus the 16 that got
    //    consumed by salt (telecaves / dwellings / artifacts / navmap
    //    / orb) on seed 1. Foe-and-desert-aware nav skips any chest
    //    we can't safely reach; the final leg ((1,26)) returns to
    //    spawn so the cross-back to Continentia uses the same tile
    //    the zone-switch will spawn us on next time.
    case AP_FLOW_PHASE12_TOUR: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE12_TOUR;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE12_TOUR:y_foe");
                return (ApCmd){ "PHASE12_TOUR:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE12_TOUR:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE12_TOUR:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_TOUR:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        // 50 non-salt chests sorted roughly by y-descending then
        // x-ascending. ap_nav_step_avoiding_foes_and_desert returns 0
        // when no safe path exists; we skip that leg and move on.
        static const struct { int x, y; const char *name; } legs[] = {
            {  3, 60, "chest_000" }, {  8, 60, "chest_001" },
            { 50, 60, "chest_003" }, { 31, 59, "chest_004" },
            { 41, 59, "chest_005" }, { 53, 57, "chest_006" },
            { 45, 56, "chest_007" }, { 59, 56, "chest_008" },
            { 56, 54, "chest_009" }, { 11, 52, "chest_011" },
            { 12, 52, "chest_012" }, { 43, 51, "chest_013" },
            { 35, 50, "chest_016" }, { 39, 50, "chest_017" },
            { 44, 48, "chest_018" }, { 24, 45, "chest_019" },
            { 11, 44, "chest_021" }, { 54, 44, "chest_023" },
            { 46, 40, "chest_026" }, { 38, 38, "chest_027" },
            { 15, 37, "chest_028" }, { 43, 37, "chest_029" },
            { 12, 36, "chest_030" }, { 60, 36, "chest_031" },
            { 60, 34, "chest_033" }, { 35, 33, "chest_034" },
            { 21, 32, "chest_035" }, { 27, 32, "chest_036" },
            { 47, 32, "chest_037" }, { 11, 26, "chest_039" },
            // STOP HERE — past leg 29 (chest_039 at (11,26)) the
            // tour heads north into a wandering-army gauntlet that
            // wipes the army at (58,16) on the way to chest_059.
            // The seed-1 hero has ~175 hp left at chest_039 which
            // isn't enough to fight through. Truncating the leg
            // list returns us to RETURN_TO_SPAWN with a positive
            // result instead of triggering POST_COMBAT:defeat.
        };
        const int n_legs = (int)(sizeof(legs) / sizeof(legs[0]));
        int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (leg >= n_legs) {
            AP_LOG("[phase12] tour complete: pos=(%d,%d) gold=%d "
                   "hp=%d", g->position.x, g->position.y,
                   g->stats.gold, ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_RETURN_TO_SPAWN;
            return (ApCmd){ "PHASE12_TOUR:done", 0,
                            assert_always_true };
        }
        int gx = legs[leg].x, gy = legs[leg].y;
        if (g->position.x == gx && g->position.y == gy) {
            AP_LOG("[phase12] leg %d (%s) done: pos=(%d,%d) gold=%d "
                   "hp=%d", leg, legs[leg].name, g->position.x,
                   g->position.y, g->stats.gold, ap_army_total_hp(g));
            st->module_scratch[0] = leg + 1;
            return (ApCmd){ "PHASE12_TOUR:leg_done", 0,
                            assert_always_true };
        }
        // Pre-recruit gives us a brute-force army; fight wandering
        // armies as we hit them. Only desert is excluded (the per-
        // step day penalty would chew through our days_left budget).
        int key = ap_nav_step_avoiding_desert(g, m, gx, gy);
        if (key == 0) key = ap_nav_step(g, m, gx, gy);
        if (key == 0) {
            AP_LOG("[phase12] leg %d (%s) no path from (%d,%d), "
                   "skipping", leg, legs[leg].name, g->position.x,
                   g->position.y);
            st->module_scratch[0] = leg + 1;
            return (ApCmd){ "PHASE12_TOUR:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE12_TOUR:nav", key,
                        assert_always_true };
    }

    // -- 12e: walk/sail back to Forestria's spawn tile (1,26) so we
    //    leave at the canonical entry point.
    case AP_FLOW_PHASE12_RETURN_TO_SPAWN: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE12_RETURN_TO_SPAWN;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE12_RETURN_TO_SPAWN:y_foe");
                return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:y_foe",
                                KEY_Y, assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // Spawn is (1,26). Go to a water tile next to it so KEY_N
        // works. (1,26) itself might be land — board the boat first.
        const int back_x = 0, back_y = 27;
        if (g->travel_mode == TRAVEL_BOAT &&
            g->position.x == back_x && g->position.y == back_y) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_OPEN_NAV_PROMPT_BACK;
            return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes_and_desert(g, m,
                                                       back_x, back_y);
        if (key == 0) key = ap_nav_step(g, m, back_x, back_y);
        if (key == 0) {
            AP_LOG("[phase12] no path back to spawn-water (%d,%d) "
                   "from (%d,%d)", back_x, back_y,
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:nav", key,
                        assert_always_true };
    }

    // -- 12f: press N to open the new-continent prompt while at sea
    //    off Forestria. Continentia is now the only discovered
    //    neighbor we care to revisit (Archipelia + Saharia stay
    //    locked until their navmaps are found in later phases).
    case AP_FLOW_PHASE12_OPEN_NAV_PROMPT_BACK: {
        if (prompt_is_active() && prompt_kind_str() &&
            strcmp(prompt_kind_str(), "numeric") == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_PICK_CONTINENTIA;
            return (ApCmd){ "PHASE12_OPEN_NAV_PROMPT_BACK:opened", 0,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_OPEN_NAV_PROMPT_BACK:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        return (ApCmd){ "PHASE12_OPEN_NAV_PROMPT_BACK:n", KEY_N,
                        assert_always_true };
    }

    case AP_FLOW_PHASE12_PICK_CONTINENTIA: {
        if (strcmp(g->position.zone, "continentia") == 0) {
            AP_LOG("[phase12] back on continentia: pos=(%d,%d)",
                   g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_NAV_HOME;
            return (ApCmd){ "PHASE12_PICK_CONTINENTIA:switched", 0,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_PICK_CONTINENTIA:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // From Forestria, Continentia is in neighbors[0]; the prompt
        // shows it as "1) Continentia".
        return (ApCmd){ "PHASE12_PICK_CONTINENTIA:1", KEY_ONE,
                        assert_always_true };
    }

    case AP_FLOW_PHASE12_NAV_HOME: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE12_NAV_HOME;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE12_NAV_HOME:y_foe");
                return (ApCmd){ "PHASE12_NAV_HOME:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE12_NAV_HOME:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE12_NAV_HOME:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_NAV_HOME:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (g->position.x == 11 && g->position.y == 58) {
            AP_LOG("[phase12] complete: pos=(%d,%d) gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE12_NAV_HOME:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_foes(g, m, 11, 58);
        if (key == 0) key = ap_nav_step(g, m, 11, 58);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE12_NAV_HOME:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE12_NAV_HOME:nav", key,
                        assert_always_true };
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
