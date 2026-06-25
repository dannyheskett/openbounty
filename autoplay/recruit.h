#ifndef OB_AUTOPLAY_RECRUIT_H
#define OB_AUTOPLAY_RECRUIT_H

// Recruit-source enumeration + pre-fight combat prediction for the executor. (The
// old simulation-driven army optimizer folded out with the AutoplayPlanner at P6;
// what survives is what exec_recruit / exec_fight use.) Prediction runs on a
// discarded copy of the game (RNG snapshot/restore), so the live world is untouched.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "combat.h"   // CombatMode, CombatTarget, CombatResult, ArmyStack
#include "game.h"     // Game, GAME_ARMY_SLOTS

// A target army composition (exec_recruit's goal): up to GAME_ARMY_SLOTS distinct
// (troop, count) stacks.
typedef struct { char id[24]; int count; } TargetStack;
typedef struct { TargetStack slot[GAME_ARMY_SLOTS]; int n; bool wins; } ArmyTarget;

// A single recruit SOURCE — "what can be recruited and where", enumerated by
// recruit_sources_enumerate in strict preference order: the current zone's
// dwellings, then the always-available home pool, then other zones' dwellings.
typedef enum {
    RSRC_INZONE_DWELLING = 0,  // a dwelling in the hero's CURRENT zone — walk to (x,y)
    RSRC_HOME_CASTLE,          // a home-pool troop — sail to the home zone's gate (x,y=-1)
    RSRC_OFFZONE_DWELLING,     // a dwelling in ANOTHER zone — sail to `zone`, walk to (x,y)
} RecruitSrcTier;

typedef struct {
    char           troop_id[24];
    int            avail;      // stock cap; home pool = 1<<28 ("unlimited")
    RecruitSrcTier tier;
    char           zone[24];   // the source's zone id
    int            x, y;       // dwelling body tile; (-1,-1) for the home castle
} RecruitSource;

// Enumerate ALL recruit sources for the current state, in preference order with a
// stable total order within each tier (deterministic truncation at `cap`). Pure
// function of g — no planner state, no nav. Returns the number written (<= cap).
int recruit_sources_enumerate(const Game *g, RecruitSource *out, int cap);

// Total weekly upkeep of the current army (the week-end summary reads this).
int army_upkeep(const Game *g);

// Count of non-empty army stacks (the "survivors" metric for a copy after a fight).
int army_stack_count(const Game *g);

// Total hp-worth of an army: sum(count * troop hit_points) over the 5 slots. The
// army-strength proxy used by exec_fight's army-preservation gate (and reusable
// wherever a single scalar for "how much army is this" is wanted).
long army_hp_worth(const ArmyStack *army);

// How many of troop_id the hero can AFFORD right now (gold / per-unit recruit cost).
// GameBuyTroop is all-or-nothing on gold, so the army-build pipeline must cap recruit
// counts by this, not just by the leadership cap.
int recruit_affordable_count(const Game *g, const char *troop_id);

// Run (mode,tgt) to completion on a discarded copy of g and report the result.
// army_override: if non-NULL, replaces the hero army in the simulation copy (so the
// caller can test a hypothetical army without mutating g). NULL uses g->army as-is.
// *out_survivors (if non-NULL) gets the HERO army's surviving stack count
// (meaningful on a WIN). RNG snapshot/restored. Thin wrapper over predict_combat_eval.
CombatResult predict_combat_survivors(const Game *g, CombatMode mode,
                                      const CombatTarget *tgt,
                                      const ArmyStack *army_override,
                                      int *out_survivors);

// Richer variant: also reports the DEFENDER's surviving stacks / total HP via any
// non-NULL out-param (0 on a player WIN). RNG snapshot/restored.
CombatResult predict_combat_eval(const Game *g, CombatMode mode,
                                 const CombatTarget *tgt,
                                 const ArmyStack *army_override,
                                 int *out_hero_stacks,
                                 int *out_def_stacks, long *out_def_hp,
                                 long *out_hero_hp);

#endif
