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
// Phase enum. Phases 1–11 drive the seed=1 Continentia run from intro
// through the final caneghor capture and Forestria navmap pickup.
// Append new phases before AP_FLOW_DONE; do not reorder existing
// entries (POST_COMBAT's resume whitelist uses the enum values).
// =========================================================================

typedef enum {
    AP_FLOW_FIRST = 0,
    // Startup: intro → enter home castle → buy starting army → exit.
    AP_FLOW_DISMISS_INTRO = AP_FLOW_FIRST,
    AP_FLOW_WALK_TO_GATE,
    AP_FLOW_STEP_ONTO_GATE,
    AP_FLOW_OPEN_RECRUIT,
    AP_FLOW_RECRUIT_PIKEMEN,
    AP_FLOW_RECRUIT_MILITIA,
    AP_FLOW_RECRUIT_ARCHERS,
    AP_FLOW_EXIT_RECRUIT,
    AP_FLOW_EXIT_CASTLE,
    // Phase 1: collect every reachable chest on the home landmass
    // while avoiding lethal foes, then return to home castle gate.
    AP_FLOW_PHASE1,
    AP_FLOW_COMBAT,
    AP_FLOW_POST_COMBAT,
    // Phase 2: nav to Hunterville → buy siege + boat + Murray
    // contract → sail to azram castle → fight Murray.
    AP_FLOW_PHASE2_NAV_TOWN,
    AP_FLOW_PHASE2_TOWN_ACTIONS,
    AP_FLOW_PHASE2_EXIT_TOWN,
    AP_FLOW_PHASE2_NAV_CASTLE,
    // Phase 3: return to king_maximus → re-recruit to full → fight
    // trolls (26,57) → grab chest_slot_6 (25,57) → fight giants
    // (7,46) → grab chest_slot_19 (6,46) and chest_slot_27 (8,42).
    AP_FLOW_PHASE3_NAV_HOME,
    AP_FLOW_PHASE3_OPEN_RECRUIT,
    AP_FLOW_PHASE3_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE3_RECRUIT_MILITIA,
    AP_FLOW_PHASE3_RECRUIT_ARCHERS,
    AP_FLOW_PHASE3_EXIT_RECRUIT,
    AP_FLOW_PHASE3_EXIT_CASTLE,
    AP_FLOW_PHASE3_HUNT,
    // Phase 3 extension: return home → re-recruit again → nav to
    // Hunterville → take Hack contract.
    AP_FLOW_PHASE3_NAV_HOME_2,
    AP_FLOW_PHASE3_OPEN_RECRUIT_2,
    AP_FLOW_PHASE3_RECRUIT_ARCHERS_2,
    AP_FLOW_PHASE3_RECRUIT_PIKEMEN_2,
    AP_FLOW_PHASE3_RECRUIT_MILITIA_2,
    AP_FLOW_PHASE3_EXIT_RECRUIT_2,
    AP_FLOW_PHASE3_EXIT_CASTLE_2,
    AP_FLOW_PHASE3_NAV_TOWN,
    AP_FLOW_PHASE3_TOWN_ACTIONS,
    AP_FLOW_PHASE3_EXIT_TOWN,
    // Phase 4: boat tour through all remaining Continentia chests
    // that are reachable without entering any alive foe's chebyshev-2
    // pursuit envelope. KEY_B (leadership) on every gold chest until
    // we have enough leadership to take Hack; then switch to KEY_A.
    AP_FLOW_PHASE4_TOUR,
    // After Phase 4 returns to gate, re-recruit one more time so
    // the army cap actually reflects the leadership we just bought
    // from chests. Mirrors the v1 recruit chain.
    AP_FLOW_PHASE4_OPEN_RECRUIT,
    AP_FLOW_PHASE4_RECRUIT_ARCHERS,
    AP_FLOW_PHASE4_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE4_RECRUIT_MILITIA,
    AP_FLOW_PHASE4_EXIT_RECRUIT,
    AP_FLOW_PHASE4_EXIT_CASTLE,
    // Phase 5: sail to faxis castle and capture Hack, then sail
    // back to king_maximus.
    AP_FLOW_PHASE5_NAV_CASTLE,
    AP_FLOW_PHASE5_NAV_HOME,
    // Phase 6: nav to the magic alcove at (11,44), accept the
    // offer (5000g for knows_magic), return home, max-recruit
    // again, then sweep every remaining chest + 2 artifacts on
    // Continentia. Foe-aware nav with fallback to regular nav
    // when no foe-free path exists.
    AP_FLOW_PHASE6_NAV_ALCOVE,
    AP_FLOW_PHASE6_NAV_HOME,
    AP_FLOW_PHASE6_OPEN_RECRUIT,
    AP_FLOW_PHASE6_RECRUIT_ARCHERS,
    AP_FLOW_PHASE6_RECRUIT_MILITIA,
    AP_FLOW_PHASE6_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE6_EXIT_RECRUIT,
    AP_FLOW_PHASE6_EXIT_CASTLE,
    AP_FLOW_PHASE6_TOUR,
    // Mid-tour re-recruit: when hp drops below the safety
    // threshold, divert from PHASE6_TOUR back to the castle,
    // max-buy a new army (cavalry too, now that we have more
    // gold), then resume the tour.
    AP_FLOW_PHASE6_MID_NAV_HOME,
    AP_FLOW_PHASE6_MID_OPEN_RECRUIT,
    AP_FLOW_PHASE6_MID_RECRUIT_CAVALRY,
    AP_FLOW_PHASE6_MID_RECRUIT_ARCHERS,
    AP_FLOW_PHASE6_MID_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE6_MID_RECRUIT_MILITIA,
    AP_FLOW_PHASE6_MID_EXIT_RECRUIT,
    AP_FLOW_PHASE6_MID_EXIT_CASTLE,
    // Final re-recruit after the tour finishes, then return to
    // hero_spawn (11,58) so the run ends at the canonical
    // starting tile (same as Phase 5).
    AP_FLOW_PHASE6_FINAL_OPEN_RECRUIT,
    AP_FLOW_PHASE6_FINAL_RECRUIT_CAVALRY,
    AP_FLOW_PHASE6_FINAL_RECRUIT_ARCHERS,
    AP_FLOW_PHASE6_FINAL_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE6_FINAL_RECRUIT_MILITIA,
    AP_FLOW_PHASE6_FINAL_EXIT_RECRUIT,
    AP_FLOW_PHASE6_FINAL_EXIT_CASTLE,
    AP_FLOW_PHASE6_FINAL_RETURN_SPAWN,
    // Phase 7: iterate the 4 remaining Continentia villains
    // (aimola → baron_makahl → dread_rob → caneghor) — for
    // each, walk to Hunterville, take the next contract, sail
    // to the villain's castle, siege/fight, return home, max-
    // buy a fresh army (cavalry first), loop. module_scratch[1]
    // holds the per-villain iteration index (0..3).
    AP_FLOW_PHASE7_NAV_TOWN,
    AP_FLOW_PHASE7_TOWN_ACTIONS,
    AP_FLOW_PHASE7_EXIT_TOWN,
    AP_FLOW_PHASE7_NAV_CASTLE,
    AP_FLOW_PHASE7_NAV_HOME,
    AP_FLOW_PHASE7_OPEN_RECRUIT,
    AP_FLOW_PHASE7_RECRUIT_CAVALRY,
    AP_FLOW_PHASE7_RECRUIT_ARCHERS,
    AP_FLOW_PHASE7_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE7_RECRUIT_MILITIA,
    AP_FLOW_PHASE7_EXIT_RECRUIT,
    AP_FLOW_PHASE7_EXIT_CASTLE,
    // Phase 8: grind 5 monster-owned Continentia castles for
    // gold + score + rank-up. Per iteration: max-buy knights
    // (then cavalry/archers/pikemen/militia) → walk to next
    // monster castle → siege → step back onto the now-owned
    // gate → toggle to garrison mode → drop militia into the
    // castle → ESC → return home → loop.
    AP_FLOW_PHASE8_OPEN_RECRUIT,
    AP_FLOW_PHASE8_RECRUIT_KNIGHTS,
    AP_FLOW_PHASE8_RECRUIT_CAVALRY,
    AP_FLOW_PHASE8_RECRUIT_ARCHERS,
    AP_FLOW_PHASE8_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE8_RECRUIT_MILITIA,
    AP_FLOW_PHASE8_EXIT_RECRUIT,
    AP_FLOW_PHASE8_EXIT_CASTLE,
    AP_FLOW_PHASE8_NAV_CASTLE,
    AP_FLOW_PHASE8_GARRISON,
    AP_FLOW_PHASE8_NAV_HOME,
    // Phase 9: kill the trolls at (26,57) and grab chest_slot_6
    // at (25,57). Step onto the foe tile to trigger combat; the
    // post-Phase-8 army has 8 knights, which beats the HOLD-vs-
    // heavy gate (50 < 3×35) and damages trolls fast enough to
    // overcome their REGEN.
    AP_FLOW_PHASE9_NAV_TROLLS,
    AP_FLOW_PHASE9_NAV_CHEST,
    AP_FLOW_PHASE9_NAV_PATHS_END,
    AP_FLOW_PHASE9_BUY_SPELLS,
    AP_FLOW_PHASE9_EXIT_PATHS_END,
    // Detour to the ghosts dwelling at (5,29) to buy ghosts before
    // returning to home castle. Ghosts (10 hp, 3-4 melee, ABSORB,
    // UNDEAD) are key vs caneghor's archmages. They replace militia
    // in the army roster for Phase 10.
    AP_FLOW_PHASE9_NAV_GHOSTS,
    AP_FLOW_PHASE9_RECRUIT_GHOSTS,
    AP_FLOW_PHASE9_NAV_HOME,
    AP_FLOW_PHASE9_OPEN_RECRUIT,
    AP_FLOW_PHASE9_RECRUIT_KNIGHTS,
    AP_FLOW_PHASE9_RECRUIT_CAVALRY,
    AP_FLOW_PHASE9_RECRUIT_ARCHERS,
    AP_FLOW_PHASE9_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE9_EXIT_RECRUIT,
    AP_FLOW_PHASE9_EXIT_CASTLE,
    // Phase 10: take a contract on caneghor (the 6th villain in
    // the cycle, refilled into slot 0 after murray was captured),
    // sail to rythacon (54,57), siege, return home.
    AP_FLOW_PHASE10_NAV_TOWN,
    AP_FLOW_PHASE10_TOWN_ACTIONS,
    AP_FLOW_PHASE10_EXIT_TOWN,
    AP_FLOW_PHASE10_NAV_CASTLE,
    // After caneghor capture, only ghosts survive (ABSORB pulled
    // every kill back into the stack). GameGarrisonTroop refuses
    // to move a stack if it would leave the hero with zero troops,
    // so we detour: navigate home → recruit 20 militia → walk
    // back to rythacon → garrison ghosts → return home.
    AP_FLOW_PHASE10_NAV_HOME_RECRUIT,
    AP_FLOW_PHASE10_OPEN_RECRUIT,
    AP_FLOW_PHASE10_RECRUIT_MILITIA,
    AP_FLOW_PHASE10_EXIT_RECRUIT,
    AP_FLOW_PHASE10_EXIT_CASTLE,
    AP_FLOW_PHASE10_NAV_RYTHACON,
    AP_FLOW_PHASE10_GARRISON,
    AP_FLOW_PHASE10_NAV_HOME,
    // Phase 11: fetch the Continentia navmap so the new-continent
    // prompt unlocks Forestria (the salt-time navmap on seed 1 sits
    // at (57,55), un-visited by phases 1–10). Sail there, step on
    // the tile to trigger INTERACT_NAVMAP, dismiss the discovery
    // dialog, sail back to home_castle gate to finish the run.
    AP_FLOW_PHASE11_NAV_NAVMAP,
    AP_FLOW_PHASE11_NAV_HOME,
    // Phase 12: first Forestria visit. Board the boat parked near
    // home castle, sail to deep water, press N to open the
    // new-continent prompt, pick the Forestria digit. Engine's
    // GameSwitchZone teleports the hero to Forestria's hero_spawn
    // at (1,26). Tour every chest reachable on foot/boat without
    // crossing a hostile foe's chebyshev-2 envelope or stepping on
    // desert (per-step day penalty). Return to (1,26), re-cross to
    // Continentia, walk back to (11,58) origin.
    // Pre-tour recruit at king_maximus so the Forestria visit goes in
    // with a brute-force army instead of relying on foe-avoidance.
    // After caneghor's bounty we usually carry ~26-31k gold, which is
    // enough to top up knights/cavalry/archers/pikemen to leadership.
    AP_FLOW_PHASE12_OPEN_RECRUIT,
    AP_FLOW_PHASE12_RECRUIT_KNIGHTS,
    AP_FLOW_PHASE12_RECRUIT_CAVALRY,
    AP_FLOW_PHASE12_RECRUIT_ARCHERS,
    AP_FLOW_PHASE12_RECRUIT_PIKEMEN,
    AP_FLOW_PHASE12_EXIT_RECRUIT,
    AP_FLOW_PHASE12_EXIT_CASTLE,
    AP_FLOW_PHASE12_NAV_TO_SEA,
    AP_FLOW_PHASE12_OPEN_NAV_PROMPT,
    AP_FLOW_PHASE12_PICK_FORESTRIA,
    AP_FLOW_PHASE12_TOUR,
    AP_FLOW_PHASE12_RETURN_TO_SPAWN,
    AP_FLOW_PHASE12_OPEN_NAV_PROMPT_BACK,
    AP_FLOW_PHASE12_PICK_CONTINENTIA,
    AP_FLOW_PHASE12_NAV_HOME,
    // Phase 13: second Forestria visit. Sail straight from origin
    // (no pre-recruit), top up at the zombie dwelling for garrison
    // fodder, pick up the shield + anchor artifacts, then iterate
    // Forestria's monster-owned castles, capturing each and dropping
    // a zombie stack into the garrison. Return to (11,58) origin on
    // Continentia.
    AP_FLOW_PHASE13_NAV_TO_SEA,
    AP_FLOW_PHASE13_OPEN_NAV_PROMPT,
    AP_FLOW_PHASE13_PICK_FORESTRIA,
    AP_FLOW_PHASE13_NAV_TO_DWARVES,
    AP_FLOW_PHASE13_RECRUIT_DWARVES,
    AP_FLOW_PHASE13_NAV_TO_ZOMBIES,
    AP_FLOW_PHASE13_RECRUIT_ZOMBIES,
    AP_FLOW_PHASE13_NAV_TO_OGRES,
    AP_FLOW_PHASE13_RECRUIT_OGRES,
    AP_FLOW_PHASE13_NAV_TO_ELVES,
    AP_FLOW_PHASE13_RECRUIT_ELVES,
    AP_FLOW_PHASE13_NAV_TO_ANCHOR,
    AP_FLOW_PHASE13_NAV_TO_SHIELD,
    AP_FLOW_PHASE13_CASTLE_LOOP,
    AP_FLOW_PHASE13_CASTLE_NAV,
    AP_FLOW_PHASE13_CASTLE_GARRISON,
    // Post-castle-loop refill: top up ogres + elves at the same
    // dwellings we visited on the way in. Population regrows each
    // week, and after losing zombies into garrisons + fight attrition
    // we usually have leadership headroom. These run unconditionally
    // (no "already in army" skip — refilling is the whole point).
    AP_FLOW_PHASE13_REFILL_OGRES_NAV,
    AP_FLOW_PHASE13_REFILL_OGRES,
    AP_FLOW_PHASE13_REFILL_ELVES_NAV,
    AP_FLOW_PHASE13_REFILL_ELVES,
    AP_FLOW_PHASE13_RETURN_TO_SPAWN,
    AP_FLOW_PHASE13_OPEN_NAV_PROMPT_BACK,
    AP_FLOW_PHASE13_PICK_CONTINENTIA,
    AP_FLOW_PHASE13_NAV_HOME,
    // Phase 14: still on Forestria after Phase 13's refill. Walk to
    // woods_end town (3,55), buy fireballs to max_spells (the Ring
    // of Heroism doubled the cap back in Phase 11), take Moradon's
    // contract (easiest Forestria villain at 1425 HP, no archmages),
    // sail/walk to his castle, siege, capture. Then hand back to
    // Phase 13's RETURN_TO_SPAWN to head home.
    AP_FLOW_PHASE14_NAV_TO_WOODS_END,
    AP_FLOW_PHASE14_TOWN_ACTIONS,
    AP_FLOW_PHASE14_EXIT_WOODS_END,
    AP_FLOW_PHASE14_NAV_CASTLE,
    AP_FLOW_DONE,

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
