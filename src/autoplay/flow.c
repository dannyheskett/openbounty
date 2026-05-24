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

    // -- PHASE 1: walk the 12 home-landmass chests in nearest-neighbor
    //    order, fighting any foe en route (always take gold on
    //    chests). Final leg is Hunterville — entering its tile diverts
    //    to BUY_SIEGE, then EXIT_TOWN, then DONE.
    case AP_FLOW_PHASE1: {
        if (prompt_is_active()) {
            const char *kind = prompt_kind_str();
            if (kind && strcmp(kind, "yes_no") == 0) {
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
            // A/B chest prompt — always take gold.
            return (ApCmd){ "PHASE1:a_chest", KEY_A,
                            assert_prompt_gone };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "PHASE1:space_dialog", KEY_SPACE,
                            assert_dialog_closed };
        }
        // Town view (Hunterville reached): divert into BUY_SIEGE,
        // which transitions through EXIT_TOWN to DONE.
        if (views_active() == VIEW_TOWN) {
            st->module_scratch[4] = 0;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_BUY_SIEGE;
            return (ApCmd){ "PHASE1:in_town", 0, assert_always_true };
        }
        {
            // Home-landmass chests, ordered so the path crosses the
            // hardest foe tiles while the army is at full HP. Last
            // run's fight log identified two heavy hitters: the foe
            // near (27, 57) (cost ~80 HP, last fight wiped us) and
            // the foe near (8, 47) (cost ~128 HP). chest_12 (25, 57)
            // forces the (27, 57) fight first; chest_02 (6, 46)
            // routes past (8, 47); remaining chests follow in
            // nearest-neighbor order from the new starting point.
            // The two heaviest fights on the home landmass are at
            // (27, 57) and (8, 47). Last run shows both are
            // *survivable at full HP* but lethal once the army has
            // taken any damage. Hit both with 300 HP by doing the
            // chest legs whose nav-paths cross those tiles first.
            static const struct { int x, y; const char *name; } legs[] = {
                { 25, 57, "chest_12" },  // path crosses (27,57)
                {  6, 46, "chest_02" },  // path crosses (8,47)
                {  8, 60, "chest_01" },
                {  8, 42, "chest_03" },
                { 10, 53, "chest_04" },
                { 10, 54, "chest_05" },
                { 23, 46, "chest_06" },
                { 21, 50, "chest_07" },
                { 11, 41, "chest_08" },
                {  3, 48, "chest_09" },
                { 37, 56, "chest_10" },
                { 29, 56, "chest_11" },
                { 12, 60, "hunterville" },
            };
            const int n_legs = (int)(sizeof(legs) / sizeof(legs[0]));
            int leg = (st->module_scratch[0] < 0) ? 0 : st->module_scratch[0];
            if (leg >= n_legs) {
                AP_LOG("[phase1] all legs done unexpectedly: "
                       "pos=(%d,%d) gold=%d",
                       g->position.x, g->position.y, g->stats.gold);
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_DONE;
                return (ApCmd){ "PHASE1:fallthrough", 0,
                                assert_always_true };
            }
            int gx = legs[leg].x, gy = legs[leg].y;
            // Goal reached? Advance. (Hunterville is bounce-back — the
            // view-town divert above handles its advance.)
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
            // Resume PHASE1 — the only navigating phase.
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_PHASE1;
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
            return (ApCmd){ "BUY_SIEGE:space_info", KEY_SPACE,
                            assert_always_true };
        default:
            if (!g->stats.siege_weapons) {
                AP_LOG("[flow] BUY_SIEGE: siege_weapons still 0 "
                       "after purchase (gold=%d)", g->stats.gold);
            } else {
                AP_LOG("[flow] siege_weapons=1, gold=%d",
                       g->stats.gold);
            }
            st->module_scratch[4] = -1;
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_EXIT_TOWN;
            return (ApCmd){ "BUY_SIEGE:done", 0, assert_always_true };
        }
    }

    case AP_FLOW_EXIT_TOWN: {
        if (views_active() == VIEW_NONE) {
            AP_LOG("[phase1] complete: pos=(%d,%d) gold=%d hp=%d "
                   "siege=%d",
                   g->position.x, g->position.y, g->stats.gold,
                   ap_army_total_hp(g), g->stats.siege_weapons);
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_DONE;
            return (ApCmd){ "EXIT_TOWN:done", 0, assert_always_true };
        }
        return (ApCmd){ "EXIT_TOWN:esc", KEY_ESCAPE,
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
