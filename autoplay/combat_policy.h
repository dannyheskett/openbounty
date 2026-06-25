// autoplay/combat_policy.h
//
// Dedicated player-side combat policy + pre-fight prediction.
// Distinct from the enemy combat_ai_action: tuned to win
// (finish low-HP stacks, neutralize ranged, use spells when magic is known),
// and deterministic (no wall-clock, no unseeded randomness).
//
// Engine-only: built on the public combat primitives in combat.h. No src/.

#ifndef OB_AUTOPLAY_COMBAT_POLICY_H
#define OB_AUTOPLAY_COMBAT_POLICY_H

#include "game.h"
#include "combat.h"

// The PlayerCombatPolicy callback: invoked for the acting
// player unit; returns nonzero when it consumed the turn. `ctx` is unused
// (the policy reads everything it needs from `c`). Pass this as player_fn to
// combat_run_headless_ex.
int autoplay_combat_policy(Combat *c, void *ctx);

// Pre-fight prediction: predict the outcome of fighting
// `target` in `mode` WITHOUT perturbing the live game `g`. Runs the fight on a
// discarded copy of *g with the player policy, snapshotting/restoring the
// world RNG so `g` and the global RNG are untouched. Returns the predicted
// CombatResult (WIN means the live fight, run from the same state, will also
// win — the combat RNG is a pure function of seed+identity+mode).
CombatResult autoplay_predict_combat(const Game *g, CombatMode mode,
                                     const CombatTarget *target,
                                     int cap_rounds);

// As above, plus: *out_capped (optional) is set true when the fight terminated by
// EXHAUSTING max_actions (cap-out) rather than a clean win/loss (WS-10). A
// cap-out returns LOSS — so the planner already declines it — but it is NOT a
// clean result; callers log it distinctly so a capped fight is never mistaken for
// winnable. autoplay_predict_combat is the out_capped=NULL wrapper.
CombatResult autoplay_predict_combat_ex(const Game *g, CombatMode mode,
                                        const CombatTarget *target,
                                        int cap_rounds, bool *out_capped);

#endif
