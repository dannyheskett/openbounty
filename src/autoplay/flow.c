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
#include "autoplay/macros.h"
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
                resume != AP_FLOW_PHASE12_MID_RETURN_SPAWN &&
                resume != AP_FLOW_PHASE12_MID_NAV_HOME &&
                resume != AP_FLOW_PHASE12_MID_NAV_SEA &&
                resume != AP_FLOW_PHASE12_RETURN_TO_SPAWN &&
                resume != AP_FLOW_PHASE12_NAV_HOME &&
                resume != AP_FLOW_PHASE13_NAV_TO_SEA &&
                resume != AP_FLOW_PHASE13_NAV_TO_DWARVES &&
                resume != AP_FLOW_PHASE13_NAV_TO_ZOMBIES &&
                resume != AP_FLOW_PHASE13_NAV_TO_OGRES &&
                resume != AP_FLOW_PHASE13_NAV_TO_ELVES &&
                resume != AP_FLOW_PHASE13_NAV_TO_ANCHOR &&
                resume != AP_FLOW_PHASE13_NAV_TO_SHIELD &&
                resume != AP_FLOW_PHASE13_CASTLE_NAV &&
                resume != AP_FLOW_PHASE13_REFILL_OGRES_NAV &&
                resume != AP_FLOW_PHASE13_REFILL_ELVES_NAV &&
                resume != AP_FLOW_PHASE13_RETURN_TO_SPAWN &&
                resume != AP_FLOW_PHASE13_NAV_HOME &&
                resume != AP_FLOW_PHASE14_NAV_TO_WOODS_END &&
                resume != AP_FLOW_PHASE14_CHEST_TOUR &&
                resume != AP_FLOW_PHASE14_NAV_TO_SPAWN &&
                resume != AP_FLOW_PHASE14_NAV_HOME_RECRUIT &&
                resume != AP_FLOW_PHASE14_NAV_HOME_SEA &&
                resume != AP_FLOW_PHASE14_NAV_CASTLE) {
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
    case AP_FLOW_PHASE2_NAV_TOWN:
        return ap_nav_to_town(g, m, st, "hunterville",
                              AP_FLOW_PHASE2_NAV_TOWN,
                              AP_FLOW_PHASE2_TOWN_ACTIONS,
                              out_phase_done, out_next_phase);

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
        if (!g->contract.active_id[0]) {
            AP_LOG("[phase2] Murray captured — contract fulfilled");
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE3_NAV_HOME;
            return (ApCmd){ "PHASE2_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        return ap_nav_to_castle(g, m, st, "azram",
                                AP_FLOW_PHASE2_NAV_CASTLE,
                                AP_FLOW_PHASE2_NAV_CASTLE,
                                out_phase_done, out_next_phase);
    }

    // -- PHASE 3 step 1: walk from azram back to king_maximus gate
    //    (11,57). Multi-mode BFS handles re-boarding the boat and
    //    sailing back. Lethal foes (trolls/giants) are NOT yet
    //    cleared, so the BFS could route through their envelopes —
    //    however, the gate-to-azram round trip in Phase 2 already
    //    proved the engine's multi-mode nav finds a sailing path
    //    that avoids them, and reversing that path is symmetric.
    case AP_FLOW_PHASE3_NAV_HOME:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE3_NAV_HOME,
                                AP_FLOW_PHASE3_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

    // -- PHASE 3 step 2: step onto gate (audience or
    // PHASE3 recruit chain collapsed into ap_rehome_and_recruit. The
    // PHASE3_NAV_HOME phase already walked into the castle, so the
    // helper enters at step 2/3 and just runs the row loop + exits.
    case AP_FLOW_PHASE3_OPEN_RECRUIT:
    case AP_FLOW_PHASE3_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE3_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE3_RECRUIT_MILITIA:
    case AP_FLOW_PHASE3_EXIT_RECRUIT:
    case AP_FLOW_PHASE3_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE3_OPEN_RECRUIT,
                                     AP_FLOW_PHASE3_HUNT,
                                     out_phase_done, out_next_phase);

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
    case AP_FLOW_PHASE3_NAV_HOME_2:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE3_NAV_HOME_2,
                                AP_FLOW_PHASE3_OPEN_RECRUIT_2,
                                out_phase_done, out_next_phase);

    case AP_FLOW_PHASE3_OPEN_RECRUIT_2:
    case AP_FLOW_PHASE3_RECRUIT_ARCHERS_2:
    case AP_FLOW_PHASE3_RECRUIT_PIKEMEN_2:
    case AP_FLOW_PHASE3_RECRUIT_MILITIA_2:
    case AP_FLOW_PHASE3_EXIT_RECRUIT_2:
    case AP_FLOW_PHASE3_EXIT_CASTLE_2:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE3_OPEN_RECRUIT_2,
                                     AP_FLOW_PHASE3_NAV_TOWN,
                                     out_phase_done, out_next_phase);

    case AP_FLOW_PHASE3_NAV_TOWN:
        return ap_nav_to_town(g, m, st, "hunterville",
                              AP_FLOW_PHASE3_NAV_TOWN,
                              AP_FLOW_PHASE3_TOWN_ACTIONS,
                              out_phase_done, out_next_phase);

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
    case AP_FLOW_PHASE4_OPEN_RECRUIT:
    case AP_FLOW_PHASE4_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE4_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE4_RECRUIT_MILITIA:
    case AP_FLOW_PHASE4_EXIT_RECRUIT:
    case AP_FLOW_PHASE4_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE4_OPEN_RECRUIT,
                                     AP_FLOW_PHASE5_NAV_CASTLE,
                                     out_phase_done, out_next_phase);

    // -- PHASE 5 step 1: sail/walk to Hack's castle gate at
    //    faxis (22,14). Multi-mode BFS handles boat boarding,
    //    sailing, and disembarking.
    case AP_FLOW_PHASE5_NAV_CASTLE: {
        if (!g->contract.active_id[0]) {
            AP_LOG("[phase5] Hack captured — contract fulfilled");
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE5_NAV_HOME;
            return (ApCmd){ "PHASE5_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        return ap_nav_to_castle(g, m, st, "faxis",
                                AP_FLOW_PHASE5_NAV_CASTLE,
                                AP_FLOW_PHASE5_NAV_CASTLE,
                                out_phase_done, out_next_phase);
    }

    // -- PHASE 5 step 2: sail/walk back to hero_spawn at (11,58).
    case AP_FLOW_PHASE5_NAV_HOME:
        return ap_nav_to_xy(g, m, st, "PHASE5_NAV_HOME", 11, 58,
                            AP_FLOW_PHASE5_NAV_HOME,
                            AP_FLOW_PHASE6_NAV_ALCOVE,
                            out_phase_done, out_next_phase);

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

    // -- PHASE 6 step 2: walk back to king_maximus gate.
    case AP_FLOW_PHASE6_NAV_HOME:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE6_NAV_HOME,
                                AP_FLOW_PHASE6_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

    // -- PHASE 6 steps 4-9: enter castle, open recruit, max-buy
    //    archers → militia → pikemen (this order so the cheap
    //    militia gobble up leadership before pikemen's higher
    //    per-cost gobble the rest of the gold budget), exit.
    case AP_FLOW_PHASE6_OPEN_RECRUIT:
    case AP_FLOW_PHASE6_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE6_RECRUIT_MILITIA:
    case AP_FLOW_PHASE6_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE6_EXIT_RECRUIT:
    case AP_FLOW_PHASE6_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE6_OPEN_RECRUIT,
                                     AP_FLOW_PHASE6_TOUR,
                                     out_phase_done, out_next_phase);

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
    case AP_FLOW_PHASE6_MID_NAV_HOME:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE6_MID_NAV_HOME,
                                AP_FLOW_PHASE6_MID_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

    case AP_FLOW_PHASE6_MID_OPEN_RECRUIT:
    case AP_FLOW_PHASE6_MID_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE6_MID_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE6_MID_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE6_MID_RECRUIT_MILITIA:
    case AP_FLOW_PHASE6_MID_EXIT_RECRUIT:
    case AP_FLOW_PHASE6_MID_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE6_MID_OPEN_RECRUIT,
                                     AP_FLOW_PHASE6_TOUR,
                                     out_phase_done, out_next_phase);

    // -- PHASE 6 final re-recruit + return to hero_spawn. Same
    //    cavalry → archers → pikemen → militia max-buy chain as
    //    the mid-tour recruit. After ESC out, walk one tile
    //    south so the run ends on the canonical starting tile.
    case AP_FLOW_PHASE6_FINAL_OPEN_RECRUIT:
    case AP_FLOW_PHASE6_FINAL_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE6_FINAL_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE6_FINAL_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE6_FINAL_RECRUIT_MILITIA:
    case AP_FLOW_PHASE6_FINAL_EXIT_RECRUIT:
    case AP_FLOW_PHASE6_FINAL_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE6_FINAL_OPEN_RECRUIT,
                                     AP_FLOW_PHASE6_FINAL_RETURN_SPAWN,
                                     out_phase_done, out_next_phase);

    case AP_FLOW_PHASE6_FINAL_RETURN_SPAWN:
        if (g->position.x == 11 && g->position.y == 58) {
            st->module_scratch[1] = 0;  // reset villain index
        }
        return ap_nav_to_xy(g, m, st, "PHASE6_FINAL_RETURN_SPAWN",
                            11, 58,
                            AP_FLOW_PHASE6_FINAL_RETURN_SPAWN,
                            AP_FLOW_PHASE7_NAV_TOWN,
                            out_phase_done, out_next_phase);

    // -- PHASE 7: iterate 4 remaining Continentia villains.
    //    module_scratch[1] = villain index (0..3). Per iter:
    //    NAV_TOWN → TOWN_ACTIONS (take contract) → EXIT_TOWN →
    //    NAV_CASTLE (sail, siege, fight) → NAV_HOME → recruit →
    //    increment villain index, loop or DONE.
    case AP_FLOW_PHASE7_NAV_TOWN:
        return ap_nav_to_town(g, m, st, "hunterville",
                              AP_FLOW_PHASE7_NAV_TOWN,
                              AP_FLOW_PHASE7_TOWN_ACTIONS,
                              out_phase_done, out_next_phase);

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
        if (!g->contract.active_id[0]) {
            AP_LOG("[phase7] villain captured. pos=(%d,%d) gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE7_NAV_HOME;
            return (ApCmd){ "PHASE7_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        const char *castle_id = NULL;
        for (int i = 0; i < GAME_CASTLES; i++) {
            if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
            if (strcmp(g->castles[i].villain_id,
                       g->contract.active_id) != 0) continue;
            castle_id = g->castles[i].id;
            break;
        }
        if (!castle_id) {
            AP_LOG("[phase7] castle for villain %s not found",
                   g->contract.active_id);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE7_NAV_CASTLE:no_castle", 0,
                            assert_always_true };
        }
        return ap_nav_to_castle(g, m, st, castle_id,
                                AP_FLOW_PHASE7_NAV_CASTLE,
                                AP_FLOW_PHASE7_NAV_CASTLE,
                                out_phase_done, out_next_phase);
    }

    case AP_FLOW_PHASE7_NAV_HOME:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE7_NAV_HOME,
                                AP_FLOW_PHASE7_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

    case AP_FLOW_PHASE7_OPEN_RECRUIT:
    case AP_FLOW_PHASE7_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE7_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE7_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE7_RECRUIT_MILITIA:
    case AP_FLOW_PHASE7_EXIT_RECRUIT:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE7_OPEN_RECRUIT,
                                     AP_FLOW_PHASE7_EXIT_CASTLE,
                                     out_phase_done, out_next_phase);

    case AP_FLOW_PHASE7_EXIT_CASTLE: {
        int v_idx = st->module_scratch[1];
        v_idx++;
        st->module_scratch[1] = v_idx;
        AP_LOG("[phase7] iteration %d done", v_idx);
        *out_phase_done = true;
        if (v_idx >= 3) {
            st->module_scratch[1] = 0;
            st->module_scratch[0] = 0;
            *out_next_phase = AP_FLOW_PHASE8_OPEN_RECRUIT;
        } else {
            *out_next_phase = AP_FLOW_PHASE7_NAV_TOWN;
        }
        return (ApCmd){ "PHASE7_EXIT_CASTLE:done", 0,
                        assert_always_true };
    }

    // -- PHASE 8: monster-castle grind loop.
    //    Iterate 5 Continentia castles (irok, nilslag, vutar,
    //    cancomar, kookamunga) in proximity order from home.
    //    module_scratch[1] = castle iteration index (0..4).
    //    Each iteration: recruit (knights → cavalry → archers →
    //    pikemen → militia) → walk → siege → garrison militia →
    //    walk home → loop.
    case AP_FLOW_PHASE8_OPEN_RECRUIT:
    case AP_FLOW_PHASE8_RECRUIT_KNIGHTS:
    case AP_FLOW_PHASE8_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE8_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE8_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE8_RECRUIT_MILITIA:
    case AP_FLOW_PHASE8_EXIT_RECRUIT:
    case AP_FLOW_PHASE8_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE8_OPEN_RECRUIT,
                                     AP_FLOW_PHASE8_NAV_CASTLE,
                                     out_phase_done, out_next_phase);

    // Walk/sail to the iteration-index monster castle. Five
    // hardcoded targets sorted by manhattan distance from home
    // gate (closest first). Skips already-owned castles (the
    // grind may capture them out of strict order if some are
    // unreachable).
    case AP_FLOW_PHASE8_NAV_CASTLE: {
        static const char *castles[] = {
            "irok", "nilslag", "vutar", "cancomar", "kookamunga"
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
        const CastleRecord *cr = GameFindCastleConst(g, castles[idx]);
        if (cr && cr->owner_kind == CASTLE_OWNER_PLAYER) {
            st->module_scratch[2] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE8_GARRISON;
            return (ApCmd){ "PHASE8_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        return ap_nav_to_castle(g, m, st, castles[idx],
                                AP_FLOW_PHASE8_NAV_CASTLE,
                                AP_FLOW_PHASE8_NAV_CASTLE,
                                out_phase_done, out_next_phase);
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
        AutoplayPhase after = (st->module_scratch[1] >= 5)
            ? AP_FLOW_PHASE9_NAV_TROLLS
            : AP_FLOW_PHASE8_OPEN_RECRUIT;
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE8_NAV_HOME,
                                after,
                                out_phase_done, out_next_phase);
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

    case AP_FLOW_PHASE9_NAV_HOME:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE9_NAV_HOME,
                                AP_FLOW_PHASE9_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

    case AP_FLOW_PHASE9_OPEN_RECRUIT:
    case AP_FLOW_PHASE9_RECRUIT_KNIGHTS:
    case AP_FLOW_PHASE9_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE9_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE9_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE9_EXIT_RECRUIT:
    case AP_FLOW_PHASE9_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE9_OPEN_RECRUIT,
                                     AP_FLOW_PHASE10_NAV_TOWN,
                                     out_phase_done, out_next_phase);

    // -- PHASE 10: capture caneghor at rythacon (54,57). One-shot
    //    contract+sail+siege loop, no per-villain iteration since
    //    he's the only remaining Continentia villain.
    case AP_FLOW_PHASE10_NAV_TOWN:
        return ap_nav_to_town(g, m, st, "hunterville",
                              AP_FLOW_PHASE10_NAV_TOWN,
                              AP_FLOW_PHASE10_TOWN_ACTIONS,
                              out_phase_done, out_next_phase);

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
            AP_LOG("[phase10] caneghor captured");
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE10_NAV_HOME_RECRUIT;
            return (ApCmd){ "PHASE10_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        const char *castle_id = NULL;
        for (int i = 0; i < GAME_CASTLES; i++) {
            if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
            if (strcmp(g->castles[i].villain_id,
                       g->contract.active_id) != 0) continue;
            castle_id = g->castles[i].id;
            break;
        }
        if (!castle_id) {
            AP_LOG("[phase10] castle for villain %s not found",
                   g->contract.active_id);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE10_NAV_CASTLE:no_castle", 0,
                            assert_always_true };
        }
        return ap_nav_to_castle(g, m, st, castle_id,
                                AP_FLOW_PHASE10_NAV_CASTLE,
                                AP_FLOW_PHASE10_NAV_CASTLE,
                                out_phase_done, out_next_phase);
    }

    // After caneghor only ghosts survive; we can't garrison them
    // alone (GameGarrisonTroop refuses to empty the army). So
    // first head home and recruit a small militia stack so the
    // hero retains a second slot, then walk back to rythacon and
    // drop the ghosts into the garrison.
    case AP_FLOW_PHASE10_NAV_HOME_RECRUIT:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE10_NAV_HOME_RECRUIT,
                                AP_FLOW_PHASE10_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

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

    // Walk back to rythacon (54,58) for the garrison handoff.
    case AP_FLOW_PHASE10_NAV_RYTHACON:
        return ap_nav_to_xy(g, m, st, "PHASE10_NAV_RYTHACON",
                            54, 58,
                            AP_FLOW_PHASE10_NAV_RYTHACON,
                            AP_FLOW_PHASE10_GARRISON,
                            out_phase_done, out_next_phase);

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

    case AP_FLOW_PHASE10_NAV_HOME:
        return ap_nav_to_xy(g, m, st, "PHASE10_NAV_HOME", 11, 58,
                            AP_FLOW_PHASE10_NAV_HOME,
                            AP_FLOW_PHASE11_NAV_NAVMAP,
                            out_phase_done, out_next_phase);

    // ----- PHASE 11 -----------------------------------------------------
    // Fetch the Continentia navmap so future navigation can offer
    // Forestria. The navmap on seed 1 is salt-placed at (57,55) — a
    // tile no earlier phase visits. Stepping onto INTERACT_NAVMAP
    // triggers engine/step.c:454, which flips
    // game->world.zones_discovered[<new zone>] and opens the
    // "You found the map to <ZONE>" dialog.
    case AP_FLOW_PHASE11_NAV_NAVMAP:
        // Any neighbor discovered => navmap collected.
        if (g->world.zones_discovered[1] || g->world.zones_discovered[2] ||
            g->world.zones_discovered[3]) {
            AP_LOG("[phase11] navmap collected");
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE11_NAV_HOME;
            return (ApCmd){ "PHASE11_NAV_NAVMAP:found", 0,
                            assert_always_true };
        }
        return ap_nav_to_tile(g, m, st, "PHASE11_NAV_NAVMAP",
                              (int)INTERACT_NAVMAP, NULL,
                              AP_FLOW_PHASE11_NAV_NAVMAP,
                              AP_FLOW_PHASE11_NAV_HOME,
                              out_phase_done, out_next_phase);

    case AP_FLOW_PHASE11_NAV_HOME:
        return ap_nav_to_xy(g, m, st, "PHASE11_NAV_HOME", 11, 58,
                            AP_FLOW_PHASE11_NAV_HOME,
                            AP_FLOW_PHASE12_OPEN_RECRUIT,
                            out_phase_done, out_next_phase);

    // ===== PHASE 12 ===================================================
    // Pre-tour recruit at home castle (caneghor's bounty leaves us
    // with ~26-31k gold so we can max out knights/cavalry/archers/
    // pikemen), then sail to Forestria, sweep every reachable chest
    // (desert tiles excluded; wandering-army fights accepted), and
    // return to (11,58) origin on Continentia.

    case AP_FLOW_PHASE12_OPEN_RECRUIT:
    case AP_FLOW_PHASE12_RECRUIT_KNIGHTS:
    case AP_FLOW_PHASE12_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE12_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE12_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE12_EXIT_RECRUIT:
    case AP_FLOW_PHASE12_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE12_OPEN_RECRUIT,
                                     AP_FLOW_PHASE12_NAV_TO_SEA,
                                     out_phase_done, out_next_phase);

#if 0
    case AP_FLOW_PHASE12_EXIT_RECRUIT_OLD: {
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
#endif

    // -- 12a: get the hero into a boat on Continentia and into a tile
    //    near open water so KEY_N triggers the new-continent prompt.
    //    The boat was rented in Phase 2 and is parked at g->boat.x/y;
    //    we just need to walk to it and board. Once in boat, set a
    //    sail-target tile at the south edge — far enough from land
    //    that pressing N yields the discovered-neighbors menu.
    // 12a-c: collapse sail-to-forestria into a single helper call.
    case AP_FLOW_PHASE12_NAV_TO_SEA:
    case AP_FLOW_PHASE12_OPEN_NAV_PROMPT:
    case AP_FLOW_PHASE12_PICK_FORESTRIA:
        return ap_sail_to_zone(g, m, st, "forestria",
                               AP_FLOW_PHASE12_PICK_FORESTRIA,
                               AP_FLOW_PHASE12_TOUR,
                               out_phase_done, out_next_phase);

    // -- 12d: chest tour on Forestria. The leg list is built at
    //    phase entry from g->res->zones[forestria].chests[] filtered
    //    by the current map: a slot is included only if its tile is
    //    still INTERACT_TREASURE_CHEST, INTERACT_ARTIFACT, or
    //    INTERACT_NAVMAP. This makes the tour seed-independent — on
    //    different seeds the same JSON slots may carry artifacts or
    //    navmaps in different positions and we'll still hit them.
    //
    //    Two passes:
    //      Pass A (module_scratch[13] == 0): foe-avoiding nav. Any
    //        chest with no foe-free path is marked "skipped" and
    //        deferred to pass B.
    //      Pass B (module_scratch[13] == 1): foes-allowed nav over
    //        just the pass-A skipped set. Anything still unreachable
    //        is dropped.
    //
    //    HP < 100 triggers a mid-tour Continentia recruit divert at
    //    any point (any pass, any leg). When we return to TOUR after
    //    the recruit, the leg index resumes where it left off.
    case AP_FLOW_PHASE12_TOUR: {
        // Static state owned by Phase 12 TOUR. Sized for ~80 legs
        // (66 chest slots + 2 artifacts + 1 navmap + headroom).
        #define PHASE12_LEG_CAP 96
        static struct { int x, y; char name[24]; }
            s_legs[PHASE12_LEG_CAP];
        static bool s_skipped[PHASE12_LEG_CAP];
        static int  s_n_legs = 0;
        static bool s_built  = false;

        // Build the leg list once per Phase 12 run. module_scratch[12]
        // is the per-run gate so a re-entry after a mid-tour recruit
        // (TOUR → MID_RETURN_SPAWN → ... → TOUR) doesn't rebuild and
        // wipe progress.
        if (st->module_scratch[12] < 1) {
            s_n_legs = 0;
            for (int i = 0; i < PHASE12_LEG_CAP; i++) s_skipped[i] = false;
            const ResZone *fz = g->res
                ? resources_zone_by_id(g->res, "forestria") : NULL;
            if (fz) {
                for (int i = 0; i < fz->chest_count &&
                                s_n_legs < PHASE12_LEG_CAP; i++) {
                    const ResZoneChest *c = &fz->chests[i];
                    const Tile *t = MapGetTile(m, c->x, c->y);
                    if (!t) continue;
                    Interact it = t->interactive;
                    if (it != INTERACT_TREASURE_CHEST &&
                        it != INTERACT_ARTIFACT &&
                        it != INTERACT_NAVMAP) continue;
                    s_legs[s_n_legs].x = c->x;
                    s_legs[s_n_legs].y = c->y;
                    int k = 0;
                    while (k + 1 < (int)sizeof(s_legs[0].name) &&
                           c->id[k]) {
                        s_legs[s_n_legs].name[k] = c->id[k]; k++;
                    }
                    s_legs[s_n_legs].name[k] = '\0';
                    s_n_legs++;
                }
            }
            st->module_scratch[12] = 1;
            st->module_scratch[13] = 0;  // pass A
            st->module_scratch[0]  = 0;  // first leg
            AP_LOG("[phase12] tour built: %d legs (forestria)",
                   s_n_legs);
            s_built = true;
        }
        (void)s_built;

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
            // ab prompt (chest gold-or-leadership): always take A
            // (gold). Phase 12 funds the Continentia mid-tour
            // re-recruit + weekly boat upkeep across the long chest
            // tour — running out of gold leaves the boat repossessed
            // (engine/game.c:914) and ap_nav_step's town-rental
            // fallback (autoplay/nav.c:381) traps us in a town loop.
            return (ApCmd){ "PHASE12_TOUR:a_chest", KEY_A,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_TOUR:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        if (views_active() == VIEW_TOWN) {
            return (ApCmd){ "PHASE12_TOUR:esc_town", KEY_ESCAPE,
                            assert_always_true };
        }
        // HP divert: if the army drops below 100 hp at any point,
        // sail back to king_maximus and re-recruit. The MID return
        // path lands us back in TOUR with leg index intact.
        if (ap_army_total_hp(g) < 100 && st->module_scratch[7] < 1) {
            AP_LOG("[phase12] hp divert: hp=%d pos=(%d,%d) gold=%d "
                   "leg=%d pass=%d", ap_army_total_hp(g),
                   g->position.x, g->position.y, g->stats.gold,
                   st->module_scratch[0], st->module_scratch[13]);
            st->module_scratch[7] = 1;  // fire once per session;
            // re-arm gets done in MID_EXIT_CASTLE on return.
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_MID_RETURN_SPAWN;
            return (ApCmd){ "PHASE12_TOUR:hp_divert", 0,
                            assert_always_true };
        }

        int pass = (st->module_scratch[13] < 0)
                       ? 0 : st->module_scratch[13];
        int leg  = (st->module_scratch[0]  < 0)
                       ? 0 : st->module_scratch[0];
        // Pass A iterates ALL legs. Pass B iterates only pass-A
        // skipped legs (s_skipped[i] == true).
        // Skip past legs that don't apply to the current pass.
        while (leg < s_n_legs) {
            if (pass == 0) break;             // pass A: take every leg
            if (s_skipped[leg]) break;        // pass B: only skipped
            leg++;
        }
        if (leg >= s_n_legs) {
            if (pass == 0) {
                // Pass A done: switch to pass B starting from leg 0.
                AP_LOG("[phase12] pass A complete; starting pass B");
                st->module_scratch[13] = 1;
                st->module_scratch[0]  = 0;
                return (ApCmd){ "PHASE12_TOUR:pass_b", 0,
                                assert_always_true };
            }
            AP_LOG("[phase12] tour complete: pos=(%d,%d) gold=%d hp=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g));
            // Reset built flag so a future Phase 12 invocation (e.g.
            // a fresh run from --autoplay) rebuilds.
            st->module_scratch[12] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_RETURN_TO_SPAWN;
            return (ApCmd){ "PHASE12_TOUR:done", 0,
                            assert_always_true };
        }
        st->module_scratch[0] = leg;
        int gx = s_legs[leg].x, gy = s_legs[leg].y;
        if (g->position.x == gx && g->position.y == gy) {
            AP_LOG("[phase12] pass=%c leg %d (%s) done: pos=(%d,%d) "
                   "gold=%d hp=%d",
                   pass == 0 ? 'A' : 'B',
                   leg, s_legs[leg].name, g->position.x,
                   g->position.y, g->stats.gold, ap_army_total_hp(g));
            // Successful leg in pass B clears the skip flag (done).
            s_skipped[leg] = false;
            st->module_scratch[0] = leg + 1;
            return (ApCmd){ "PHASE12_TOUR:leg_done", 0,
                            assert_always_true };
        }
        // Pass A uses foe-avoiding nav; pass B uses fights-allowed.
        int key = (pass == 0)
            ? ap_nav_step_avoiding_foes_and_desert(g, m, gx, gy)
            : ap_nav_step_avoiding_desert(g, m, gx, gy);
        if (key == 0 && pass == 1) key = ap_nav_step(g, m, gx, gy);
        if (key == 0) {
            if (pass == 0) {
                AP_LOG("[phase12] pass A leg %d (%s) no safe path "
                       "from (%d,%d), deferring to pass B",
                       leg, s_legs[leg].name, g->position.x,
                       g->position.y);
                s_skipped[leg] = true;
            } else {
                AP_LOG("[phase12] pass B leg %d (%s) unreachable "
                       "from (%d,%d), dropping",
                       leg, s_legs[leg].name, g->position.x,
                       g->position.y);
                s_skipped[leg] = false;
            }
            st->module_scratch[0] = leg + 1;
            return (ApCmd){ "PHASE12_TOUR:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE12_TOUR:nav", key,
                        assert_always_true };
    }

    // ----- Phase 12 mid-tour Continentia re-recruit -----------------

    case AP_FLOW_PHASE12_MID_RETURN_SPAWN: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE12_MID_RETURN_SPAWN;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE12_MID_RETURN_SPAWN:y_foe");
                return (ApCmd){ "PHASE12_MID_RETURN_SPAWN:y_foe",
                                KEY_Y, assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){
                    "PHASE12_MID_RETURN_SPAWN:enter_dismiss",
                    KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE12_MID_RETURN_SPAWN:b_chest",
                            KEY_B, assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE12_MID_RETURN_SPAWN:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        const int back_x = 0, back_y = 27;
        if (g->travel_mode == TRAVEL_BOAT &&
            g->position.x == back_x && g->position.y == back_y) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE12_MID_OPEN_NAV_HOME;
            return (ApCmd){ "PHASE12_MID_RETURN_SPAWN:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_desert(g, m, back_x, back_y);
        if (key == 0) key = ap_nav_step(g, m, back_x, back_y);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE12_MID_RETURN_SPAWN:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE12_MID_RETURN_SPAWN:nav", key,
                        assert_always_true };
    }

    // MID sail back to Continentia, then walk to home castle.
    case AP_FLOW_PHASE12_MID_OPEN_NAV_HOME:
    case AP_FLOW_PHASE12_MID_PICK_CONTINENTIA:
        return ap_sail_to_zone(g, m, st, "continentia",
                               AP_FLOW_PHASE12_MID_PICK_CONTINENTIA,
                               AP_FLOW_PHASE12_MID_NAV_HOME,
                               out_phase_done, out_next_phase);

    case AP_FLOW_PHASE12_MID_NAV_HOME:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE12_MID_NAV_HOME,
                                AP_FLOW_PHASE12_MID_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

    case AP_FLOW_PHASE12_MID_OPEN_RECRUIT:
    case AP_FLOW_PHASE12_MID_RECRUIT_KNIGHTS:
    case AP_FLOW_PHASE12_MID_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE12_MID_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE12_MID_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE12_MID_RECRUIT_MILITIA:
    case AP_FLOW_PHASE12_MID_EXIT_RECRUIT:
    case AP_FLOW_PHASE12_MID_EXIT_CASTLE:
        // Re-arm HP-divert gate as soon as we re-enter the recruit
        // flow; the macro's final state (out_next_phase set) will
        // transition to MID_NAV_SEA for the trip back to Forestria.
        st->module_scratch[7] = 0;
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE12_MID_OPEN_RECRUIT,
                                     AP_FLOW_PHASE12_MID_NAV_SEA,
                                     out_phase_done, out_next_phase);

    case AP_FLOW_PHASE12_MID_NAV_SEA:
        return ap_sail_to_zone(g, m, st, "forestria",
                               AP_FLOW_PHASE12_MID_NAV_SEA,
                               AP_FLOW_PHASE12_TOUR,
                               out_phase_done, out_next_phase);

    // Sail-to-zone collapses MID_OPEN_NAV_FOREST + MID_PICK_FORESTRIA.
    case AP_FLOW_PHASE12_MID_OPEN_NAV_FOREST:
    case AP_FLOW_PHASE12_MID_PICK_FORESTRIA:
        return ap_sail_to_zone(g, m, st, "forestria",
                               AP_FLOW_PHASE12_MID_PICK_FORESTRIA,
                               AP_FLOW_PHASE12_TOUR,
                               out_phase_done, out_next_phase);

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
        if (views_active() == VIEW_TOWN) {
            return (ApCmd){ "PHASE12_RETURN_TO_SPAWN:esc_town",
                            KEY_ESCAPE, assert_always_true };
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
    case AP_FLOW_PHASE12_OPEN_NAV_PROMPT_BACK:
    case AP_FLOW_PHASE12_PICK_CONTINENTIA:
        return ap_sail_to_zone(g, m, st, "continentia",
                               AP_FLOW_PHASE12_PICK_CONTINENTIA,
                               AP_FLOW_PHASE12_NAV_HOME,
                               out_phase_done, out_next_phase);

    case AP_FLOW_PHASE12_NAV_HOME:
        return ap_nav_to_xy(g, m, st, "PHASE12_NAV_HOME", 11, 58,
                            AP_FLOW_PHASE12_NAV_HOME,
                            AP_FLOW_PHASE13_NAV_TO_SEA,
                            out_phase_done, out_next_phase);

#if 0
    case AP_FLOW_PHASE12_NAV_HOME_OLD: {
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
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_NAV_TO_SEA;
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
#endif

    // ===== PHASE 13 ===================================================
    // Sail straight from the Continentia origin to Forestria (no
    // pre-recruit — every week ashore costs ~3-6k upkeep and the
    // sea trip itself is one full week). Top up at the zombie
    // dwelling for garrison fodder, grab the shield + anchor
    // artifacts, then iterate Forestria's monster-owned castles
    // dropping zombies into each garrison. Return to (11,58) origin.

    case AP_FLOW_PHASE13_NAV_TO_SEA:
    case AP_FLOW_PHASE13_OPEN_NAV_PROMPT:
    case AP_FLOW_PHASE13_PICK_FORESTRIA:
        return ap_sail_to_zone(g, m, st, "forestria",
                               AP_FLOW_PHASE13_PICK_FORESTRIA,
                               AP_FLOW_PHASE13_NAV_TO_DWARVES,
                               out_phase_done, out_next_phase);

    // -- Stop at dwarves dwelling (8,50) for a beefy fight stack.
    //    Dwarves: 20 HP each, 2-4 melee, 350g, very high HP per
    //    leadership. Used to absorb hits in Forestria castle sieges
    //    so the zombie stack survives for garrison-time.
    case AP_FLOW_PHASE13_NAV_TO_DWARVES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_RECRUIT_DWARVES;
                st->module_scratch[0] = 0;
                return (ApCmd){ "PHASE13_NAV_TO_DWARVES:dwelling_open",
                                0, assert_always_true };
            }
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE13_NAV_TO_DWARVES;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE13_NAV_TO_DWARVES:y_foe");
                return (ApCmd){ "PHASE13_NAV_TO_DWARVES:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            return (ApCmd){ "PHASE13_NAV_TO_DWARVES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_NAV_TO_DWARVES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // Skip if dwarves already in army.
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "dwarves") == 0 &&
                g->army[i].count > 0) {
                AP_LOG("[phase13] dwarves already in army (slot %d, "
                       "count=%d), skipping", i, g->army[i].count);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ZOMBIES;
                return (ApCmd){ "PHASE13_NAV_TO_DWARVES:already", 0,
                                assert_always_true };
            }
        }
        int key = ap_nav_step_avoiding_desert(g, m, 8, 50);
        if (key == 0) key = ap_nav_step(g, m, 8, 50);
        if (key == 0) {
            AP_LOG("[phase13] no path to dwarves (8,50) from "
                   "(%d,%d), skipping", g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ZOMBIES;
            return (ApCmd){ "PHASE13_NAV_TO_DWARVES:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_NAV_TO_DWARVES:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_RECRUIT_DWARVES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                int sub = (st->module_scratch[0] < 0)
                              ? 0 : st->module_scratch[0];
                switch (sub) {
                case 0: st->module_scratch[0] = 1;
                    return (ApCmd){ "PHASE13_RECRUIT_DWARVES:9",
                                    KEY_NINE, assert_always_true };
                case 1: st->module_scratch[0] = 2;
                    return (ApCmd){ "PHASE13_RECRUIT_DWARVES:9",
                                    KEY_NINE, assert_always_true };
                case 2: st->module_scratch[0] = 3;
                    return (ApCmd){ "PHASE13_RECRUIT_DWARVES:9",
                                    KEY_NINE, assert_always_true };
                case 3: st->module_scratch[0] = 4;
                    return (ApCmd){ "PHASE13_RECRUIT_DWARVES:9",
                                    KEY_NINE, assert_always_true };
                default:
                    return (ApCmd){ "PHASE13_RECRUIT_DWARVES:enter",
                                    KEY_ENTER, assert_prompt_gone };
                }
            }
            return (ApCmd){ "PHASE13_RECRUIT_DWARVES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_RECRUIT_DWARVES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "PHASE13_RECRUIT_DWARVES:esc", KEY_ESCAPE,
                            assert_always_true };
        }
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "dwarves") == 0)
                n = g->army[i].count;
        }
        AP_LOG("[phase13] dwarves recruited: count=%d gold=%d",
               n, g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ZOMBIES;
        return (ApCmd){ "PHASE13_RECRUIT_DWARVES:done", 0,
                        assert_always_true };
    }

    // -- Stop at zombie dwelling (16,50) for garrison fodder.
    //    Stepping onto a dwelling tile opens the text-input recruit
    //    prompt; we type 9999 to buy the cap.
    case AP_FLOW_PHASE13_NAV_TO_ZOMBIES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                // Dwelling prompt opened — hand off to recruit state.
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_RECRUIT_ZOMBIES;
                st->module_scratch[0] = 0;
                return (ApCmd){ "PHASE13_NAV_TO_ZOMBIES:dwelling_open",
                                0, assert_always_true };
            }
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE13_NAV_TO_ZOMBIES;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE13_NAV_TO_ZOMBIES:y_foe");
                return (ApCmd){ "PHASE13_NAV_TO_ZOMBIES:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            return (ApCmd){ "PHASE13_NAV_TO_ZOMBIES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_NAV_TO_ZOMBIES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // Skip if zombies already in army (revisit case).
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "zombies") == 0 &&
                g->army[i].count > 0) {
                AP_LOG("[phase13] zombies already in army (slot %d, "
                       "count=%d), skipping", i, g->army[i].count);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ANCHOR;
                return (ApCmd){ "PHASE13_NAV_TO_ZOMBIES:already", 0,
                                assert_always_true };
            }
        }
        int key = ap_nav_step_avoiding_desert(g, m, 16, 50);
        if (key == 0) key = ap_nav_step(g, m, 16, 50);
        if (key == 0) {
            AP_LOG("[phase13] no path to zombies (16,50) from "
                   "(%d,%d), skipping", g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ANCHOR;
            return (ApCmd){ "PHASE13_NAV_TO_ZOMBIES:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_NAV_TO_ZOMBIES:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_RECRUIT_ZOMBIES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                int sub = (st->module_scratch[0] < 0)
                              ? 0 : st->module_scratch[0];
                switch (sub) {
                case 0: st->module_scratch[0] = 1;
                    return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:9",
                                    KEY_NINE, assert_always_true };
                case 1: st->module_scratch[0] = 2;
                    return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:9",
                                    KEY_NINE, assert_always_true };
                case 2: st->module_scratch[0] = 3;
                    return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:9",
                                    KEY_NINE, assert_always_true };
                case 3: st->module_scratch[0] = 4;
                    return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:9",
                                    KEY_NINE, assert_always_true };
                default:
                    return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:enter",
                                    KEY_ENTER, assert_prompt_gone };
                }
            }
            return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:esc", KEY_ESCAPE,
                            assert_always_true };
        }
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "zombies") == 0)
                n = g->army[i].count;
        }
        AP_LOG("[phase13] zombies recruited: count=%d gold=%d",
               n, g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE13_NAV_TO_OGRES;
        return (ApCmd){ "PHASE13_RECRUIT_ZOMBIES:done", 0,
                        assert_always_true };
    }

    // -- Ogres dwelling (19,42) — 40 HP each, 750g, big melee
    //    tank. Sit between yeneverre castle (19,45) and the rest of
    //    Forestria, so the detour is essentially free.
    case AP_FLOW_PHASE13_NAV_TO_OGRES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_RECRUIT_OGRES;
                st->module_scratch[0] = 0;
                return (ApCmd){ "PHASE13_NAV_TO_OGRES:dwelling_open",
                                0, assert_always_true };
            }
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE13_NAV_TO_OGRES;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE13_NAV_TO_OGRES:y_foe");
                return (ApCmd){ "PHASE13_NAV_TO_OGRES:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            return (ApCmd){ "PHASE13_NAV_TO_OGRES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_NAV_TO_OGRES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "ogres") == 0 &&
                g->army[i].count > 0) {
                AP_LOG("[phase13] ogres already in army (slot %d, "
                       "count=%d), skipping", i, g->army[i].count);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ELVES;
                return (ApCmd){ "PHASE13_NAV_TO_OGRES:already", 0,
                                assert_always_true };
            }
        }
        int key = ap_nav_step_avoiding_desert(g, m, 19, 42);
        if (key == 0) key = ap_nav_step(g, m, 19, 42);
        if (key == 0) {
            AP_LOG("[phase13] no path to ogres (19,42) from (%d,%d), "
                   "skipping", g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ELVES;
            return (ApCmd){ "PHASE13_NAV_TO_OGRES:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_NAV_TO_OGRES:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_RECRUIT_OGRES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                int sub = (st->module_scratch[0] < 0)
                              ? 0 : st->module_scratch[0];
                switch (sub) {
                case 0: st->module_scratch[0] = 1;
                    return (ApCmd){ "PHASE13_RECRUIT_OGRES:9",
                                    KEY_NINE, assert_always_true };
                case 1: st->module_scratch[0] = 2;
                    return (ApCmd){ "PHASE13_RECRUIT_OGRES:9",
                                    KEY_NINE, assert_always_true };
                case 2: st->module_scratch[0] = 3;
                    return (ApCmd){ "PHASE13_RECRUIT_OGRES:9",
                                    KEY_NINE, assert_always_true };
                case 3: st->module_scratch[0] = 4;
                    return (ApCmd){ "PHASE13_RECRUIT_OGRES:9",
                                    KEY_NINE, assert_always_true };
                default:
                    return (ApCmd){ "PHASE13_RECRUIT_OGRES:enter",
                                    KEY_ENTER, assert_prompt_gone };
                }
            }
            return (ApCmd){ "PHASE13_RECRUIT_OGRES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_RECRUIT_OGRES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "PHASE13_RECRUIT_OGRES:esc", KEY_ESCAPE,
                            assert_always_true };
        }
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "ogres") == 0)
                n = g->army[i].count;
        }
        AP_LOG("[phase13] ogres recruited: count=%d gold=%d",
               n, g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ELVES;
        return (ApCmd){ "PHASE13_RECRUIT_OGRES:done", 0,
                        assert_always_true };
    }

    // -- Elves dwelling (42,43) — 10 HP, 200g, ranged 2-4 shot 24.
    //    Best ranged available on Forestria. East of yeneverre/ogres,
    //    en route to the anchor pickup at (43,18).
    case AP_FLOW_PHASE13_NAV_TO_ELVES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_RECRUIT_ELVES;
                st->module_scratch[0] = 0;
                return (ApCmd){ "PHASE13_NAV_TO_ELVES:dwelling_open",
                                0, assert_always_true };
            }
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE13_NAV_TO_ELVES;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE13_NAV_TO_ELVES:y_foe");
                return (ApCmd){ "PHASE13_NAV_TO_ELVES:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            return (ApCmd){ "PHASE13_NAV_TO_ELVES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_NAV_TO_ELVES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "elves") == 0 &&
                g->army[i].count > 0) {
                AP_LOG("[phase13] elves already in army (slot %d, "
                       "count=%d), skipping", i, g->army[i].count);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ANCHOR;
                return (ApCmd){ "PHASE13_NAV_TO_ELVES:already", 0,
                                assert_always_true };
            }
        }
        int key = ap_nav_step_avoiding_desert(g, m, 42, 43);
        if (key == 0) key = ap_nav_step(g, m, 42, 43);
        if (key == 0) {
            AP_LOG("[phase13] no path to elves (42,43) from (%d,%d), "
                   "skipping", g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ANCHOR;
            return (ApCmd){ "PHASE13_NAV_TO_ELVES:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_NAV_TO_ELVES:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_RECRUIT_ELVES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                int sub = (st->module_scratch[0] < 0)
                              ? 0 : st->module_scratch[0];
                switch (sub) {
                case 0: st->module_scratch[0] = 1;
                    return (ApCmd){ "PHASE13_RECRUIT_ELVES:9",
                                    KEY_NINE, assert_always_true };
                case 1: st->module_scratch[0] = 2;
                    return (ApCmd){ "PHASE13_RECRUIT_ELVES:9",
                                    KEY_NINE, assert_always_true };
                case 2: st->module_scratch[0] = 3;
                    return (ApCmd){ "PHASE13_RECRUIT_ELVES:9",
                                    KEY_NINE, assert_always_true };
                case 3: st->module_scratch[0] = 4;
                    return (ApCmd){ "PHASE13_RECRUIT_ELVES:9",
                                    KEY_NINE, assert_always_true };
                default:
                    return (ApCmd){ "PHASE13_RECRUIT_ELVES:enter",
                                    KEY_ENTER, assert_prompt_gone };
                }
            }
            return (ApCmd){ "PHASE13_RECRUIT_ELVES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_RECRUIT_ELVES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "PHASE13_RECRUIT_ELVES:esc", KEY_ESCAPE,
                            assert_always_true };
        }
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "elves") == 0)
                n = g->army[i].count;
        }
        AP_LOG("[phase13] elves recruited: count=%d gold=%d",
               n, g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE13_NAV_TO_ANCHOR;
        return (ApCmd){ "PHASE13_RECRUIT_ELVES:done", 0,
                        assert_always_true };
    }

    // -- Pick up anchor artifact (auto-claimed on step).
    case AP_FLOW_PHASE13_NAV_TO_ANCHOR:
        return ap_nav_to_artifact(g, m, st, "anchor",
                                  AP_FLOW_PHASE13_NAV_TO_ANCHOR,
                                  AP_FLOW_PHASE13_NAV_TO_SHIELD,
                                  out_phase_done, out_next_phase);

    // -- Pick up shield artifact at (51,28).
    case AP_FLOW_PHASE13_NAV_TO_SHIELD:
        return ap_nav_to_artifact(g, m, st, "shield",
                                  AP_FLOW_PHASE13_NAV_TO_SHIELD,
                                  AP_FLOW_PHASE13_CASTLE_LOOP,
                                  out_phase_done, out_next_phase);

    // -- Iterate Forestria's 6 castles. Skip villain-owned, special,
    //    and player-owned. Target monster-owned ones for sieges.
    //    module_scratch[1] = castle iteration index (0..5).
    //    module_scratch[2] = current GARRISON sub-state.
    case AP_FLOW_PHASE13_CASTLE_LOOP: {
        static const struct { int x, y; const char *id; } castles[] = {
            { 19, 44, "yeneverre" },
            { 30, 45, "duvock" },
            { 41, 29, "jhan" },
            { 25, 24, "mooseweigh" },
            { 47, 57, "basefit" },
            { 42,  7, "quinderwitch" },
        };
        const int n_castles = (int)(sizeof(castles)/sizeof(castles[0]));
        int idx = (st->module_scratch[1] < 0) ? 0 : st->module_scratch[1];
        // Skip past any villain/special/player-owned castles.
        while (idx < n_castles) {
            const CastleRecord *cr = GameFindCastleConst(g, castles[idx].id);
            if (!cr) { idx++; continue; }
            if (cr->owner_kind == CASTLE_OWNER_MONSTERS) break;
            AP_LOG("[phase13] skipping castle %s (owner=%d)",
                   castles[idx].id, (int)cr->owner_kind);
            idx++;
        }
        st->module_scratch[1] = idx;
        if (idx >= n_castles) {
            AP_LOG("[phase13] castle loop complete: pos=(%d,%d) "
                   "gold=%d hp=%d", g->position.x, g->position.y,
                   g->stats.gold, ap_army_total_hp(g));
            st->module_scratch[0] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_REFILL_OGRES_NAV;
            return (ApCmd){ "PHASE13_CASTLE_LOOP:done", 0,
                            assert_always_true };
        }
        st->module_scratch[2] = 0;
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE13_CASTLE_NAV;
        return (ApCmd){ "PHASE13_CASTLE_LOOP:advance", 0,
                        assert_always_true };
    }

    // -- Sail/walk to the current castle's gate, fight when prompted.
    case AP_FLOW_PHASE13_CASTLE_NAV: {
        static const char *castles[] = {
            "yeneverre", "duvock", "jhan", "mooseweigh",
            "basefit", "quinderwitch",
        };
        const int n = (int)(sizeof(castles)/sizeof(castles[0]));
        int idx = st->module_scratch[1];
        if (idx < 0 || idx >= n) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_RETURN_TO_SPAWN;
            return (ApCmd){ "PHASE13_CASTLE_NAV:bad_idx", 0,
                            assert_always_true };
        }
        const CastleRecord *cr = GameFindCastleConst(g, castles[idx]);
        if (cr && cr->owner_kind == CASTLE_OWNER_PLAYER) {
            AP_LOG("[phase13] castle %s captured", castles[idx]);
            st->module_scratch[2] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_CASTLE_GARRISON;
            return (ApCmd){ "PHASE13_CASTLE_NAV:captured", 0,
                            assert_always_true };
        }
        return ap_nav_to_castle(g, m, st, castles[idx],
                                AP_FLOW_PHASE13_CASTLE_NAV,
                                AP_FLOW_PHASE13_CASTLE_NAV,
                                out_phase_done, out_next_phase);
    }

    // -- Drop a zombie stack into the freshly captured castle.
    //    Pattern mirrors PHASE8/PHASE10 garrison flow.
    case AP_FLOW_PHASE13_CASTLE_GARRISON: {
        static const struct { int x, y; const char *id; } castles[] = {
            { 19, 44, "yeneverre" },
            { 30, 45, "duvock" },
            { 41, 29, "jhan" },
            { 25, 24, "mooseweigh" },
            { 47, 57, "basefit" },
            { 42,  7, "quinderwitch" },
        };
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_CASTLE_GARRISON:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        int idx = st->module_scratch[1];
        if (idx < 0 || idx >= (int)(sizeof(castles)/sizeof(castles[0]))) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_RETURN_TO_SPAWN;
            return (ApCmd){ "PHASE13_CASTLE_GARRISON:bad_idx", 0,
                            assert_always_true };
        }
        int sub = (st->module_scratch[2] < 0) ? 0 : st->module_scratch[2];
        int gx = castles[idx].x, gy = castles[idx].y;
        if (sub == 0) {
            if (views_active() == VIEW_OWN_CASTLE) {
                st->module_scratch[2] = 1;
                return (ApCmd){ "PHASE13_CASTLE_GARRISON:view_open", 0,
                                assert_always_true };
            }
            int key = ap_nav_step(g, m, gx, gy);
            if (key == 0) {
                AP_LOG("[phase13] castle %s: cant nav to gate, "
                       "giving up", castles[idx].id);
                st->module_scratch[2] = 4;
                return (ApCmd){ "PHASE13_CASTLE_GARRISON:no_nav", 0,
                                assert_always_true };
            }
            return (ApCmd){ "PHASE13_CASTLE_GARRISON:nav_to_gate",
                            key, assert_always_true };
        }
        if (sub == 1) {
            st->module_scratch[2] = 2;
            return (ApCmd){ "PHASE13_CASTLE_GARRISON:space_toggle",
                            KEY_SPACE, assert_always_true };
        }
        if (sub == 2) {
            int zombie_slot = -1;
            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                if (strcmp(g->army[i].id, "zombies") == 0 &&
                    g->army[i].count > 0) {
                    zombie_slot = i;
                    break;
                }
            }
            if (zombie_slot < 0) {
                AP_LOG("[phase13] castle %s: no zombies in army, "
                       "skipping garrison", castles[idx].id);
                st->module_scratch[2] = 4;
                return (ApCmd){ "PHASE13_CASTLE_GARRISON:no_zombies",
                                KEY_ESCAPE, assert_always_true };
            }
            st->module_scratch[2] = 4;
            int key_letter = KEY_A + zombie_slot;
            const char *name =
                zombie_slot == 0 ? "PHASE13_CASTLE_GARRISON:a" :
                zombie_slot == 1 ? "PHASE13_CASTLE_GARRISON:b" :
                zombie_slot == 2 ? "PHASE13_CASTLE_GARRISON:c" :
                zombie_slot == 3 ? "PHASE13_CASTLE_GARRISON:d" :
                                   "PHASE13_CASTLE_GARRISON:e";
            AP_LOG("[phase13] castle %s: garrisoning zombies slot %d",
                   castles[idx].id, zombie_slot);
            return (ApCmd){ name, key_letter, assert_always_true };
        }
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase13] castle %s garrison done gold=%d hp=%d",
                   castles[idx].id, g->stats.gold,
                   ap_army_total_hp(g));
            st->module_scratch[1] = idx + 1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_CASTLE_LOOP;
            return (ApCmd){ "PHASE13_CASTLE_GARRISON:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_CASTLE_GARRISON:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- Post-loop refill: revisit ogres dwelling (19,42) and elves
    //    (42,43) to top up before heading home. Population regrows
    //    weekly; after several castle fights and the zombie garrison
    //    drop, we usually have leadership headroom to take a fresh
    //    stack of each.
    case AP_FLOW_PHASE13_REFILL_OGRES_NAV: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_REFILL_OGRES;
                st->module_scratch[0] = 0;
                return (ApCmd){ "PHASE13_REFILL_OGRES_NAV:dwelling_open",
                                0, assert_always_true };
            }
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE13_REFILL_OGRES_NAV;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE13_REFILL_OGRES_NAV:y_foe");
                return (ApCmd){ "PHASE13_REFILL_OGRES_NAV:y_foe",
                                KEY_Y, assert_combat_resolved };
            }
            return (ApCmd){ "PHASE13_REFILL_OGRES_NAV:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_REFILL_OGRES_NAV:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        int key = ap_nav_step_avoiding_desert(g, m, 19, 42);
        if (key == 0) key = ap_nav_step(g, m, 19, 42);
        if (key == 0) {
            AP_LOG("[phase13] refill ogres: no path from (%d,%d), "
                   "skipping", g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_REFILL_ELVES_NAV;
            return (ApCmd){ "PHASE13_REFILL_OGRES_NAV:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_REFILL_OGRES_NAV:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_REFILL_OGRES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                int sub = (st->module_scratch[0] < 0)
                              ? 0 : st->module_scratch[0];
                switch (sub) {
                case 0: st->module_scratch[0] = 1;
                    return (ApCmd){ "PHASE13_REFILL_OGRES:9",
                                    KEY_NINE, assert_always_true };
                case 1: st->module_scratch[0] = 2;
                    return (ApCmd){ "PHASE13_REFILL_OGRES:9",
                                    KEY_NINE, assert_always_true };
                case 2: st->module_scratch[0] = 3;
                    return (ApCmd){ "PHASE13_REFILL_OGRES:9",
                                    KEY_NINE, assert_always_true };
                case 3: st->module_scratch[0] = 4;
                    return (ApCmd){ "PHASE13_REFILL_OGRES:9",
                                    KEY_NINE, assert_always_true };
                default:
                    return (ApCmd){ "PHASE13_REFILL_OGRES:enter",
                                    KEY_ENTER, assert_prompt_gone };
                }
            }
            return (ApCmd){ "PHASE13_REFILL_OGRES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_REFILL_OGRES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "PHASE13_REFILL_OGRES:esc", KEY_ESCAPE,
                            assert_always_true };
        }
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "ogres") == 0)
                n = g->army[i].count;
        }
        AP_LOG("[phase13] ogres refilled: count=%d gold=%d",
               n, g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE13_REFILL_ELVES_NAV;
        return (ApCmd){ "PHASE13_REFILL_OGRES:done", 0,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_REFILL_ELVES_NAV: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_PHASE13_REFILL_ELVES;
                st->module_scratch[0] = 0;
                return (ApCmd){ "PHASE13_REFILL_ELVES_NAV:dwelling_open",
                                0, assert_always_true };
            }
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE13_REFILL_ELVES_NAV;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE13_REFILL_ELVES_NAV:y_foe");
                return (ApCmd){ "PHASE13_REFILL_ELVES_NAV:y_foe",
                                KEY_Y, assert_combat_resolved };
            }
            return (ApCmd){ "PHASE13_REFILL_ELVES_NAV:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_REFILL_ELVES_NAV:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        int key = ap_nav_step_avoiding_desert(g, m, 42, 43);
        if (key == 0) key = ap_nav_step(g, m, 42, 43);
        if (key == 0) {
            AP_LOG("[phase13] refill elves: no path from (%d,%d), "
                   "skipping", g->position.x, g->position.y);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_RETURN_TO_SPAWN;
            return (ApCmd){ "PHASE13_REFILL_ELVES_NAV:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_REFILL_ELVES_NAV:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_REFILL_ELVES: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "text") == 0) {
                int sub = (st->module_scratch[0] < 0)
                              ? 0 : st->module_scratch[0];
                switch (sub) {
                case 0: st->module_scratch[0] = 1;
                    return (ApCmd){ "PHASE13_REFILL_ELVES:9",
                                    KEY_NINE, assert_always_true };
                case 1: st->module_scratch[0] = 2;
                    return (ApCmd){ "PHASE13_REFILL_ELVES:9",
                                    KEY_NINE, assert_always_true };
                case 2: st->module_scratch[0] = 3;
                    return (ApCmd){ "PHASE13_REFILL_ELVES:9",
                                    KEY_NINE, assert_always_true };
                case 3: st->module_scratch[0] = 4;
                    return (ApCmd){ "PHASE13_REFILL_ELVES:9",
                                    KEY_NINE, assert_always_true };
                default:
                    return (ApCmd){ "PHASE13_REFILL_ELVES:enter",
                                    KEY_ENTER, assert_prompt_gone };
                }
            }
            return (ApCmd){ "PHASE13_REFILL_ELVES:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_REFILL_ELVES:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "PHASE13_REFILL_ELVES:esc", KEY_ESCAPE,
                            assert_always_true };
        }
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] &&
                strcmp(g->army[i].id, "elves") == 0)
                n = g->army[i].count;
        }
        AP_LOG("[phase13] elves refilled: count=%d gold=%d",
               n, g->stats.gold);
        st->module_scratch[1] = 0;  // Phase 14 villain iteration idx
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_PHASE14_NAV_TO_WOODS_END;
        return (ApCmd){ "PHASE13_REFILL_ELVES:done", 0,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_RETURN_TO_SPAWN: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE13_RETURN_TO_SPAWN;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE13_RETURN_TO_SPAWN:y_foe");
                return (ApCmd){ "PHASE13_RETURN_TO_SPAWN:y_foe",
                                KEY_Y, assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE13_RETURN_TO_SPAWN:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            return (ApCmd){ "PHASE13_RETURN_TO_SPAWN:b_chest", KEY_B,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE13_RETURN_TO_SPAWN:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        const int back_x = 0, back_y = 27;
        if (g->travel_mode == TRAVEL_BOAT &&
            g->position.x == back_x && g->position.y == back_y) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_OPEN_NAV_PROMPT_BACK;
            return (ApCmd){ "PHASE13_RETURN_TO_SPAWN:arrived", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_desert(g, m, back_x, back_y);
        if (key == 0) key = ap_nav_step(g, m, back_x, back_y);
        if (key == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "PHASE13_RETURN_TO_SPAWN:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE13_RETURN_TO_SPAWN:nav", key,
                        assert_always_true };
    }

    case AP_FLOW_PHASE13_OPEN_NAV_PROMPT_BACK:
    case AP_FLOW_PHASE13_PICK_CONTINENTIA:
        return ap_sail_to_zone(g, m, st, "continentia",
                               AP_FLOW_PHASE13_PICK_CONTINENTIA,
                               AP_FLOW_PHASE13_NAV_HOME,
                               out_phase_done, out_next_phase);

    case AP_FLOW_PHASE13_NAV_HOME:
        return ap_nav_to_xy(g, m, st, "PHASE13_NAV_HOME", 11, 58,
                            AP_FLOW_PHASE13_NAV_HOME,
                            AP_FLOW_DONE,
                            out_phase_done, out_next_phase);

    // ===== PHASE 14 ===================================================
    // Buy fireballs at woods_end (Forestria's fireball town), take
    // Moradon's contract there, sail to Moradon's castle, siege +
    // capture, then hand off to Phase 13's RETURN_TO_SPAWN to head
    // back to Continentia origin.

    case AP_FLOW_PHASE14_NAV_TO_WOODS_END:
        return ap_nav_to_town(g, m, st, "woods_end",
                              AP_FLOW_PHASE14_NAV_TO_WOODS_END,
                              AP_FLOW_PHASE14_TOWN_ACTIONS,
                              out_phase_done, out_next_phase);

    // -- Town menu loop: buy fireballs to max_spells (D), take
    //    Moradon's contract (A — cycle until active=moradon), then
    //    exit. Town info panels are dismissed with SPACE; D may
    //    bounce back if we already hit max_spells, which is the
    //    cue to stop buying spells and shift to contract.
    case AP_FLOW_PHASE14_TOWN_ACTIONS: {
        // Forestria villain iteration. module_scratch[1] = current
        // index into this table; bumped after each NAV_CASTLE
        // captures the matching villain.
        static const char *villains[] = {
            "moradon", "barrowpine", "bargash", "rinaldus",
        };
        const int n_villains = (int)(sizeof(villains)/sizeof(villains[0]));
        int v_idx = (st->module_scratch[1] < 0)
                        ? 0 : st->module_scratch[1];
        if (v_idx >= n_villains) {
            AP_LOG("[phase14] all Forestria villains captured");
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE14_EXIT_WOODS_END;
            return (ApCmd){ "PHASE14_TOWN:all_done", 0,
                            assert_always_true };
        }
        const char *want = villains[v_idx];
        if (views_town_info_text() != NULL) {
            return (ApCmd){ "PHASE14_TOWN:space_info", KEY_SPACE,
                            assert_always_true };
        }
        // Have target villain's contract? Done with town.
        if (g->contract.active_id[0] &&
            strcmp(g->contract.active_id, want) == 0) {
            AP_LOG("[phase14] contract: %s. gold=%d hp=%d",
                   want, g->stats.gold, ap_army_total_hp(g));
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE14_EXIT_WOODS_END;
            return (ApCmd){ "PHASE14_TOWN:contract_taken", 0,
                            assert_always_true };
        }
        // Spell buy phase: keep pressing D until fireballs hit cap.
        int known = 0;
        for (int i = 0; i < 14; i++) known += g->spells.counts[i];
        const int spell_cost = 1500;
        bool can_buy_spell =
            (known < g->stats.max_spells) &&
            (g->stats.gold >= spell_cost + 2000 /* reserve for fight */);
        if (can_buy_spell) {
            return (ApCmd){ "PHASE14_TOWN:d_spell", KEY_D,
                            assert_always_true };
        }
        if (!g->contract.active_id[0]) {
            AP_LOG("[phase14] spells done (known=%d cap=%d), "
                   "taking contract for %s",
                   known, g->stats.max_spells, want);
            return (ApCmd){ "PHASE14_TOWN:a_contract", KEY_A,
                            assert_always_true };
        }
        // Have a non-target contract → cycle.
        AP_LOG("[phase14] active contract is %s (want %s), rerolling",
               g->contract.active_id, want);
        return (ApCmd){ "PHASE14_TOWN:a_recontract", KEY_A,
                        assert_always_true };
    }

    case AP_FLOW_PHASE14_EXIT_WOODS_END: {
        if (views_active() == VIEW_NONE) {
            *out_phase_done = true;
            // Chest tour now lives in Phase 12 — all 50 chests +
            // navmap + 2 artifacts are collected during the south-
            // to-north Forestria sweep before Phase 13. Skip the
            // (now redundant) chest_tour state and sail straight
            // home for the Continentia recruit before Moradon.
            *out_next_phase = AP_FLOW_PHASE14_NAV_TO_SPAWN;
            return (ApCmd){ "PHASE14_EXIT_WOODS_END:done", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE14_EXIT_WOODS_END:esc", KEY_ESCAPE,
                        assert_always_true };
    }

    // -- Grind the unclaimed north Forestria chests (20 tiles in
    //    seed-1 reality: chest slots 040-065 minus the 6 salt
    //    overlaps — anchor (43,18), orb (16,17), skeletons (51,15),
    //    orcs (14,10), pikemen (32,9), elves (36,8)). KEY_B on every
    //    gold chest to bump leadership; accept every foe encounter
    //    for the score/gold/lead. Path uses ap_nav_step (foes ok),
    //    desert-aware.
    case AP_FLOW_PHASE14_CHEST_TOUR: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
                st->module_scratch[3] = AP_FLOW_PHASE14_CHEST_TOUR;
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_COMBAT;
                dump_combat_start(g, "PHASE14_CHEST_TOUR:y_foe");
                return (ApCmd){ "PHASE14_CHEST_TOUR:y_foe", KEY_Y,
                                assert_combat_resolved };
            }
            if (kind && strcmp(kind, "text") == 0) {
                return (ApCmd){ "PHASE14_CHEST_TOUR:enter_dismiss",
                                KEY_ENTER, assert_prompt_gone };
            }
            // ab prompt (chest gold-or-leadership): leadership is
            // wasted above the next rank's cap. Marshal sets
            // leadership_base = 500 on rank-up (engine/game.c:1232
            // hard-assigns), so any leadership bonus we accept above
            // 500 will be lost when villain #8 is captured. Take B
            // (leadership) while base < 500; take A (gold) after.
            if (g->stats.leadership_base < 500) {
                return (ApCmd){ "PHASE14_CHEST_TOUR:b_chest", KEY_B,
                                assert_prompt_gone };
            }
            return (ApCmd){ "PHASE14_CHEST_TOUR:a_chest", KEY_A,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE14_CHEST_TOUR:space_dialog",
                            KEY_SPACE, assert_dialog_closed };
        }
        // 20-leg tour, sweep y-descending then x-ascending. The
        // start tile (3,55) is woods_end coast; quinderwitch sits
        // at (42,7) so we naturally end up in the right area for
        // the dwelling stops + castle siege.
        static const struct { int x, y; const char *name; } legs[] = {
            { 37, 25, "chest_040" }, { 16, 23, "chest_041" },
            { 23, 23, "chest_042" }, { 14, 17, "chest_044" },
            { 15, 17, "chest_045" }, { 27, 17, "chest_047" },
            { 40, 17, "chest_048" }, { 43, 17, "chest_049" },
            {  8, 16, "chest_050" }, { 38, 16, "chest_051" },
            { 43, 16, "chest_052" }, { 51, 14, "chest_054" },
            { 11, 12, "chest_055" }, { 50, 11, "chest_056" },
            { 57,  9, "chest_059" }, { 32,  8, "chest_060" },
            { 19,  7, "chest_062" }, { 29,  7, "chest_063" },
            { 54,  7, "chest_064" }, { 46,  5, "chest_065" },
        };
        const int n_legs = (int)(sizeof(legs)/sizeof(legs[0]));
        int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
        if (leg >= n_legs) {
            AP_LOG("[phase14] chest tour complete: pos=(%d,%d) gold=%d "
                   "hp=%d lead=%d", g->position.x, g->position.y,
                   g->stats.gold, ap_army_total_hp(g),
                   g->stats.leadership_base);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE14_NAV_TO_SPAWN;
            return (ApCmd){ "PHASE14_CHEST_TOUR:done", 0,
                            assert_always_true };
        }
        int gx = legs[leg].x, gy = legs[leg].y;
        if (g->position.x == gx && g->position.y == gy) {
            AP_LOG("[phase14] leg %d (%s) done: pos=(%d,%d) gold=%d "
                   "lead=%d", leg, legs[leg].name, g->position.x,
                   g->position.y, g->stats.gold,
                   g->stats.leadership_base);
            st->module_scratch[0] = leg + 1;
            return (ApCmd){ "PHASE14_CHEST_TOUR:leg_done", 0,
                            assert_always_true };
        }
        int key = ap_nav_step_avoiding_desert(g, m, gx, gy);
        if (key == 0) key = ap_nav_step(g, m, gx, gy);
        if (key == 0) {
            AP_LOG("[phase14] leg %d (%s) no path from (%d,%d), "
                   "skipping", leg, legs[leg].name, g->position.x,
                   g->position.y);
            st->module_scratch[0] = leg + 1;
            return (ApCmd){ "PHASE14_CHEST_TOUR:no_path", 0,
                            assert_always_true };
        }
        return (ApCmd){ "PHASE14_CHEST_TOUR:nav", key,
                        assert_always_true };
    }

    // -- Stop at orcs dwelling (14,10) on the route from woods_end
    //    to quinderwitch — fills a 4th stack with cheap ranged
    //    troops (5 hp, 75g, 1-2 shot 10). Population hasn't been
    // -- Sail to Forestria spawn-water (0,27) for cross-zone back
    //    to Continentia. The Forestria hero_spawn (1,26) is land;
    //    the adjacent water (0,27) is where the engine parks the
    //    boat on arrival, so we head there to leave.
    case AP_FLOW_PHASE14_NAV_TO_SPAWN:
    case AP_FLOW_PHASE14_OPEN_NAV_HOME:
    case AP_FLOW_PHASE14_PICK_CONTINENTIA:
        return ap_sail_to_zone(g, m, st, "continentia",
                               AP_FLOW_PHASE14_PICK_CONTINENTIA,
                               AP_FLOW_PHASE14_NAV_HOME_RECRUIT,
                               out_phase_done, out_next_phase);

    case AP_FLOW_PHASE14_NAV_HOME_RECRUIT:
        return ap_nav_to_castle(g, m, st, "king_maximus",
                                AP_FLOW_PHASE14_NAV_HOME_RECRUIT,
                                AP_FLOW_PHASE14_OPEN_RECRUIT,
                                out_phase_done, out_next_phase);

    case AP_FLOW_PHASE14_OPEN_RECRUIT:
    case AP_FLOW_PHASE14_RECRUIT_KNIGHTS:
    case AP_FLOW_PHASE14_RECRUIT_CAVALRY:
    case AP_FLOW_PHASE14_RECRUIT_ARCHERS:
    case AP_FLOW_PHASE14_RECRUIT_PIKEMEN:
    case AP_FLOW_PHASE14_RECRUIT_MILITIA:
    case AP_FLOW_PHASE14_EXIT_RECRUIT:
    case AP_FLOW_PHASE14_EXIT_CASTLE:
        return ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
                                     AP_FLOW_PHASE14_OPEN_RECRUIT,
                                     AP_FLOW_PHASE14_NAV_HOME_SEA,
                                     out_phase_done, out_next_phase);

    case AP_FLOW_PHASE14_NAV_HOME_SEA:
    case AP_FLOW_PHASE14_OPEN_NAV_BACK:
    case AP_FLOW_PHASE14_PICK_FORESTRIA_BACK:
        return ap_sail_to_zone(g, m, st, "forestria",
                               AP_FLOW_PHASE14_PICK_FORESTRIA_BACK,
                               AP_FLOW_PHASE14_NAV_CASTLE,
                               out_phase_done, out_next_phase);

    // -- Sail/walk to Moradon's castle. Castle id is read live from
    //    castles[] where villain_id == "moradon" (salt-time assignment
    //    isn't stable across seeds).
    case AP_FLOW_PHASE14_NAV_CASTLE: {
        static const char *villains[] = {
            "moradon", "barrowpine", "bargash", "rinaldus",
        };
        const int n_villains = (int)(sizeof(villains)/sizeof(villains[0]));
        int v_idx = (st->module_scratch[1] < 0)
                        ? 0 : st->module_scratch[1];
        if (!g->contract.active_id[0]) {
            const char *just_caught = (v_idx < n_villains)
                                        ? villains[v_idx] : "?";
            v_idx++;
            st->module_scratch[1] = v_idx;
            AP_LOG("[phase14] villain %d (%s) captured",
                   v_idx - 1, just_caught);
            *out_phase_done = true;
            *out_next_phase = (v_idx >= n_villains)
                ? AP_FLOW_PHASE13_RETURN_TO_SPAWN
                : AP_FLOW_PHASE14_NAV_TO_SPAWN;
            return (ApCmd){ "PHASE14_NAV_CASTLE:captured", 0,
                            assert_always_true };
        }
        const char *want = (v_idx < n_villains) ? villains[v_idx] : "";
        const char *castle_id = NULL;
        for (int i = 0; i < GAME_CASTLES; i++) {
            if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
            if (strcmp(g->castles[i].villain_id, want) != 0) continue;
            castle_id = g->castles[i].id;
            break;
        }
        if (!castle_id) {
            AP_LOG("[phase14] %s castle not found, skipping", want);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE13_RETURN_TO_SPAWN;
            return (ApCmd){ "PHASE14_NAV_CASTLE:no_castle", 0,
                            assert_always_true };
        }
        return ap_nav_to_castle(g, m, st, castle_id,
                                AP_FLOW_PHASE14_NAV_CASTLE,
                                AP_FLOW_PHASE14_NAV_CASTLE,
                                out_phase_done, out_next_phase);
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
