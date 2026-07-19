// demo/demo_combat_policy.h
//
// Dedicated player-side combat policy for demo mode. Tuned to win (finish
// low-HP stacks, neutralize ranged, use spells when magic is known) and
// deterministic (no wall-clock, no unseeded randomness) -- distinct from the
// enemy combat_ai_action. Engine-only: built on the public combat primitives
// in combat.h. No src/.

#ifndef OB_DEMO_COMBAT_POLICY_H
#define OB_DEMO_COMBAT_POLICY_H

#include "game.h"
#include "combat.h"

// The PlayerCombatPolicy callback: invoked for the acting player unit; returns
// nonzero when it consumed the turn. `ctx` is unused (the policy reads
// everything it needs from `c`). Pass this as player_fn to
// combat_run_headless_ex / combat_run_headless_rec.
int demo_combat_policy(Combat *c, void *ctx);

#endif
