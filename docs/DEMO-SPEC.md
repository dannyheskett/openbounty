# Demo Mode, Specification

Reproduction-grade record of demo mode as built. This document has owned the
`DM-` namespace; `OPENBOUNTY-SPEC.md` has owned `REQ-`, `AUTOPLAY-SPECS.md`
has owned `AP-`. Items have been written in the present perfect tense ("demo
*has played* X"). Code citations have named a file and function.

Demo mode has been the human-like player: given a seed it has PLAYED the live
game forward under a player's constraints and ended in the game's own
outcomes, WON (scepter recovered), LOST (calendar exhausted), or STUCK (out of
ideas). It has been distinct from autoplay, the pack-winnability oracle:
autoplay has proved, demo has played. The line between them has been the
TIMELINE and the INFORMATION, not foresight: both have simulated a fight
before entering it (DM-013), but demo has done so on one committed timeline
using only what a player can see.

## 1. Scope and boundary

- **DM-001.** Demo mode has lived in `demo/` as a SIBLING of `autoplay/`:
  engine-only (no `src/`, no raylib) and independent of autoplay (no include
  in either direction). Both properties have been build-enforced: demo
  objects have compiled with only `-Idemo -Iengine/headless -Iengine/include`
  (`Makefile` `DEMO_CFLAGS`) and linked in the library-boundary check beside
  the autoplay objects (`Makefile` `$(LIBTEST_STAMP)`), so a cross-fence
  include or a shell-symbol reference has failed `make all`.
- **DM-002.** The one shell adapter allowed to know demo has been
  `src/shell_demo.c` (visible pacing; dependency arrow shell → demo →
  engine), mirroring `src/shell_autoplay.c`.
- **DM-003.** Demo has owned its boot profile (`demo/demo.h` `DEMO_HERO_*`,
  `DEMO_DEFAULT_SEED_INDEX`) separately from autoplay's, so tuning one mode
  has never moved the other's worlds.
- **DM-004.** Demo's player-side combat driver has been its own tuned battle policy
  (`demo/demo_combat_policy.c demo_combat_policy`, driven through
  `demo/demo_brain.c demo_player_fn`), owned entirely inside `demo/`, so
  DM-001's independence has carried no exception. The function has been
  engine-typed and battle-scoped; measured at the decisive margin (leadership
  ~150, the first wandering camp on seed 1) it has won the fight the built-in
  AI loses.

## 2. Player constraints

- **DM-010.** One timeline: there has been no world snapshot/rollback on any
  decision path. Every action has committed; a lost fight has run the game's
  own temp death. The throwaway `Game` copy the combat prediction runs on
  (DM-013) has not been a snapshot of this rule's kind: nothing has ever been
  restored FROM it, and the live world has never moved backwards.
- **DM-011.** Demo has used player-visible information only: the fog
  (`demo/demo_path.c` has pathed only over fog-seen tiles), the prompt banners (fight judgment has read
  the garrison the prompt shows, `demo/demo_brain.c resolve_combat_flow`),
  the views' records, and the puzzle screen (`demo/demo_scepter.c` has matched
  the revealed cells (engine `tables.c puzzle_grid_entity`) against
  explored terrain). There has been no `GamePeekChest` and no `GameRngSnapshot`:
  hidden state has never been read. Public catalog data (troops, morale chart,
  burial rule) has been manual knowledge and fair.
- **DM-012.** The verdict has been the game's own: `[DEMO OVER]` has reported
  WON / LOST / STUCK plus score, villains, artifacts, castles, digs, days.
  Exit code (`--demo --headless`): 0 = WON, 1 = otherwise, 2 = setup failure.
- **DM-013.** COMBAT HAS BEEN PREDICTED, on visible inputs only.
  `demo/demo_brain.c demo_predict_boost` has copied the live `Game`, lifted
  the copy's leadership by `k` raise-control casts, and run the engine's own
  `combat_run_headless_rec` to completion on that copy, returning the
  smallest `k` that wins at the required survivor count (`-1` when no held
  charge wins). It has been called at fight entry (`resolve_combat_flow`) and
  when judging a blocking foe. The copy has been discarded either way; the
  live world has never been rolled back, which is what DM-010 has
  constrained. The prediction has read only what the prompt already shows
  (the target's garrison) plus the catalog rules, so it has been the same
  arithmetic a player does by hand, run exactly. It has also been what makes
  the run watchable: the `CombatTurnRecord` the prediction produces has been
  the animation the viewer sees, so one simulation has served both the
  decision and the presentation.

## 3. CLI

- **DM-020.** The flags have been `--demo` (visible, watchable pace, hands off
  on completion) and `--demo --headless` (no window, plays to an ending), with
  `--seed N` (catalog world `0`–`255`, default `DEMO_DEFAULT_SEED_INDEX = 1`);
  `src/main.c` has parsed them.
- **DM-021.** The run's campaign engine has been the LEADERSHIP CHAIN, each
  link measured on seed 1: chest gold has been taken as permanent leadership
  unless broke; recruiting has been capped in peacetime (upkeep ≤ half the
  commission, so the purse accumulates) and uncapped in WAR (a castle target
  known via the town's free intel screen (`demo/demo_brain.c town_business`)
  or sighted); fights have been entered on the engine's own predicted
  outcome, boosted by raise-control casts when charges are held. Idle boats
  have been cancelled (the rental fee has been half the base commission).
- **DM-030.** **All configuration and diagnostics have been command-line
  flags; the project has taken NO CONFIGURATION from environment variables**
  (the same rule AP-170 has recorded for autoplay). The one environment read
  anywhere in the project has been the engine's user-data directory lookup
  (`engine/savepath.c`: `XDG_DATA_HOME` / `HOME` on Linux, `HOME` on macOS,
  `APPDATA` on Windows), the platform's own convention for locating a user's
  data directory, not a knob, and outside demo mode entirely. Demo's per-tick
  decision trace has been gated by the existing `--verbose` flag, set once at boot
  (`src/main.c` → `demo_set_verbose`, beside autoplay's
  `ob_diag_set_verbose`) and read through `demo_verbose()`
  (`demo/demo_brain.c`). Diagnostics have been observation-only: a gated-off
  run has been behavior-identical.
