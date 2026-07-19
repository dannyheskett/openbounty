// autoplay/prereq.h
//
// Engine-enforced hard gates only (AP-060/AP-061): a monster or villain siege
// with no siege weapons emits one BUY-SIEGE candidate, executed under the same
// attempt snapshot before the objective. The villain's contract and the
// soft water/army prerequisites stay inside exec_siege / exec_ensure_contract
// and reachability + combat prediction.

#ifndef OB_AUTOPLAY_PREREQ_H
#define OB_AUTOPLAY_PREREQ_H

#include "exec.h"
#include "goals.h"

// Fill up to `cap` prerequisite candidates for `step`. Returns the count.
int prereq_unmet(const ExecCtx *ctx, const PlanStep *step,
                 PlanStep *out, int cap);

// ----- Static prerequisite gates (AP-060/AP-061) -----------------------------
// The pack/engine structure that makes an objective ACTIONABLE, read O(1) from
// live state with no simulation -- the branch-prediction primitive for a path
// search. HARD gates cannot be overcome by spending (they need OTHER objectives
// done: navmaps found, villains captured); SOFT gates are one-time gold unlocks
// the executor buys on demand.
typedef enum {
    PREREQ_ZONE     = 1u << 0,  // continent undiscovered (navmap chain)   -- HARD
    PREREQ_CONTRACT = 1u << 1,  // villain outside the contract window      -- HARD
    PREREQ_FINALE   = 1u << 2,  // scepter, other objectives still open     -- HARD
    PREREQ_SIEGE    = 1u << 3,  // castle/villain, no siege weapons yet      -- SOFT
    PREREQ_MAGIC    = 1u << 4,  // alcove, wallet below the alcove cost      -- SOFT
} PrereqBit;

#define PREREQ_HARD (PREREQ_ZONE | PREREQ_CONTRACT | PREREQ_FINALE)

// The unmet prerequisite gates for `step` in the current world. `open_others`
// is the count of not-done NON-scepter objectives (feeds the finale gate only;
// any non-scepter caller may pass 0).
unsigned prereq_gated(const ExecCtx *ctx, const PlanStep *step, int open_others);

// Actionable now := no HARD gate unmet. The cheap pre-filter a search uses to
// drop a branch without paying an attempt for it.
bool prereq_actionable(const ExecCtx *ctx, const PlanStep *step, int open_others);

// Spell capability (the pathing primitive): are gate/bridge/stop casts legal
// yet? Gates the zero-day crossings in the cost model.
bool prereq_magic_enabled(const Game *g);

// Verbose-only dump of the initial gate structure (map validation).
void prereq_dump(const ExecCtx *ctx, const PlanStepSet *set);

#endif
