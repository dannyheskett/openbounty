#ifndef OB_AUTOPLAY_INTERNAL_H
#define OB_AUTOPLAY_INTERNAL_H

// Autoplay private interface.
//
// Contract:
//   - Each tick the dispatcher asks the active phase function for ONE
//     command. The command says: "press key K this tick; on the next
//     tick I expect predicate P(g) to hold."
//   - Dispatcher sets the live input key, advances one engine tick,
//     then on the NEXT tick evaluates P. Pass → next command. Fail →
//     dump state and exit FAIL.
//   - Phases are stateless functions called once per tick. They
//     decide the next command from CURRENT game state. They never
//     spin internally waiting for state to change.
//   - For seed=1 every position, view, and sequence is deterministic.
//     Walk steps are hardcoded keys, not BFS.

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "shell_run.h"

// =========================================================================
// Phase enum (frozen — the minimal smoke flow)
// =========================================================================

typedef enum {
    AP_FLOW_FIRST = 0,
    AP_FLOW_DISMISS_INTRO = AP_FLOW_FIRST,
    AP_FLOW_WALK_TO_GATE,
    AP_FLOW_STEP_ONTO_GATE,
    AP_FLOW_OPEN_RECRUIT,
    AP_FLOW_RECRUIT_PIKEMEN,
    AP_FLOW_RECRUIT_MILITIA,
    AP_FLOW_RECRUIT_ARCHERS,
    AP_FLOW_EXIT_RECRUIT,
    AP_FLOW_EXIT_CASTLE,
    AP_FLOW_GRIND,
    AP_FLOW_COMBAT,
    AP_FLOW_POST_COMBAT,
    AP_FLOW_BUY_SIEGE,
    AP_FLOW_RENT_BOAT,
    AP_FLOW_EXIT_TOWN,
    // Re-recruit at the home castle between chest legs. Reached when
    // GRIND detects VIEW_HOME_CASTLE (the nav routed onto the gate).
    AP_FLOW_REOPEN_RECRUIT,
    AP_FLOW_REBUY_PIKEMEN,
    AP_FLOW_REBUY_ARCHERS,
    AP_FLOW_REBUY_MILITIA,
    AP_FLOW_REEXIT_RECRUIT,
    AP_FLOW_REEXIT_CASTLE,
    AP_FLOW_DONE,
    AP_FLOW_LAST = AP_FLOW_DONE,

    AP_ALL_DONE,
} AutoplayPhase;

// =========================================================================
// The command + phase contract
// =========================================================================

typedef struct {
    const char *name;             // for the assertion-failed log line
    int         key;              // 0 = no input this tick
    bool      (*assert_post)(const Game *g);  // run on the NEXT tick
} ApCmd;

typedef struct AutoplayState AutoplayState;

typedef ApCmd (*ApPhaseFn)(const Game *g, const Map *m,
                           AutoplayState *st,
                           bool *out_phase_done,
                           AutoplayPhase *out_next_phase);

// =========================================================================
// Autoplay state (slim)
// =========================================================================

struct AutoplayState {
    AutoplayPhase phase;
    int           tick;             // monotonic, for logs

    // Pending command awaiting next-tick assertion.
    ApCmd  pending;
    bool   pending_active;

    // Module-private scratch (sub-step index inside multi-command
    // phases like RECRUIT_PIKEMEN; foe coord cache; etc.).
    int  module_scratch[16];

    // Pre-tick snapshot — captured by the dispatcher BEFORE setting
    // the live key. Predicates read these via ap_pre_* globals to
    // assert deltas (e.g. army_hp == pre_army_hp + 100).
    int  pre_gold;
    int  pre_army_hp;
    int  pre_pos_x;
    int  pre_pos_y;
};

// Dispatcher publishes the pre-snapshot so that predicate functions
// (which only get a `const Game *`) can compare post-tick against
// pre-tick. Set just before the dispatcher calls a phase function.
extern int ap_pre_gold;
extern int ap_pre_army_hp;
extern int ap_pre_pos_x;
extern int ap_pre_pos_y;

// =========================================================================
// Logging
// =========================================================================

#define AP_LOG(fmt, ...) fprintf(stderr, "[autoplay] " fmt "\n", ##__VA_ARGS__)

// =========================================================================
// Shared helpers (defined in core.c)
// =========================================================================

void ap_queue_standard_startup(void);
int  ap_army_total_hp(const Game *g);

// Combat decision helpers (used by autoplay_before_frame inside
// RunCombat's loop). Forward-decl Combat to avoid pulling combat.h
// into every flow.c predicate.
struct Combat;
int  ap_closest_enemy(const struct Combat *c, int ux, int uy,
                      int *out_dist);
int  ap_highest_threat_enemy(const struct Combat *c, int ux, int uy,
                             int *out_dist);

// State dump for assertion failures.
void ap_dump_state(const char *why, const Game *g, const AutoplayState *st);

// =========================================================================
// Module entry: the minimal flow's phase dispatch.
// =========================================================================

ApCmd ap_flow_phase(const Game *g, const Map *m,
                       AutoplayState *st,
                       bool *out_phase_done,
                       AutoplayPhase *out_next_phase);

#endif
