# Demo Mode, Specification

**Status:** living document. Reproduction-grade record of demo mode as built.
This document owns the `DM-` namespace; `OPENBOUNTY-SPEC.md` owns `REQ-`,
`AUTOPLAY-SPECS.md` owns `AP-`. Code citations name a file and function.

Demo mode is the human-like player: given a seed it PLAYS the live game
forward under a player's constraints and ends in the game's own outcomes, WON (scepter recovered), LOST (calendar exhausted), or STUCK (out of ideas).
It is distinct from autoplay, the pack-winnability oracle: autoplay proves,
demo plays. The line between them is the TIMELINE and the INFORMATION, not
foresight, both simulate a fight before entering it (DM-013), but demo does
so on one committed timeline using only what a player can see.

## 1. Scope and boundary

- **DM-001.** Demo mode has lived in `demo/` as a SIBLING of `autoplay/`:
  engine-only (no `src/`, no raylib) and independent of autoplay (no include
  in either direction). Both properties have been build-enforced: demo
  objects compile with only `-Idemo -Iengine/headless -Iengine/include`
  (`Makefile` `DEMO_CFLAGS`) and link in the library-boundary check beside
  the autoplay objects (`Makefile` `$(LIBTEST_STAMP)`), so a cross-fence
  include or a shell-symbol reference fails `make all`.
- **DM-002.** The one shell adapter allowed to know demo is
  `src/shell_demo.c` (visible pacing; dependency arrow shell → demo →
  engine), mirroring `src/shell_autoplay.c`.
- **DM-003.** Demo has owned its boot profile (`demo/demo.h` `DEMO_HERO_*`,
  `DEMO_DEFAULT_SEED_INDEX`) separately from autoplay's, so tuning one mode
  never moves the other's worlds.
- **DM-004.** Demo's player-side combat driver is its own tuned battle policy
  (`demo/demo_combat_policy.c demo_combat_policy`, driven through
  `demo/demo_brain.c demo_player_fn`), owned entirely inside `demo/`, so
  DM-001's independence carries no exception. The function is engine-typed and
  battle-scoped; measured at the decisive margin (leadership ~150, the first
  wandering camp on seed 1) it wins the fight the built-in AI loses.

## 2. Player constraints

- **DM-010.** One timeline: no world snapshot/rollback on any decision path.
  Every action commits; a lost fight runs the game's own temp death. The
  throwaway `Game` copy the combat prediction runs on (DM-013) is not a
  snapshot of this rule's kind: nothing is ever restored FROM it, and the live
  world never moves backwards.
- **DM-011.** Player-visible information only: the fog (`demo/demo_path.c`
  paths only over fog-seen tiles), the prompt banners (fight judgment reads
  the garrison the prompt shows, `demo/demo_brain.c resolve_combat_flow`),
  the views' records, and the puzzle screen (`demo/demo_scepter.c` matches
  the revealed cells (engine `tables.c puzzle_grid_entity`) against
  explored terrain). No `GamePeekChest` and no `GameRngSnapshot`: hidden
  state is never read. Public catalog data (troops, morale chart, burial
  rule) is manual knowledge and fair.
- **DM-012.** The verdict is the game's own: `[DEMO OVER]` reports
  WON / LOST / STUCK plus score, villains, artifacts, castles, digs, days.
  Exit code (`--demo --headless`): 0 = WON, 1 = otherwise, 2 = setup failure.
- **DM-013.** COMBAT IS PREDICTED, on visible inputs only.
  `demo/demo_brain.c demo_predict_boost` copies the live `Game`, lifts the
  copy's leadership by `k` raise-control casts, and runs the engine's own
  `combat_run_headless_rec` to completion on that copy, returning the
  smallest `k` that wins at the required survivor count (`-1` when no held
  charge wins). It is called at fight entry (`resolve_combat_flow`) and when
  judging a blocking foe. The copy is discarded either way; the live world is
  never rolled back, which is what DM-010 constrains. The prediction reads
  only what the prompt already shows (the target's garrison) plus the
  catalog rules, so it is the same arithmetic a player does by hand, run
  exactly. It is also what makes the run watchable: the `CombatTurnRecord`
  the prediction produces IS the animation the viewer sees, so one simulation
  serves both the decision and the presentation.

## 3. CLI

- **DM-020.** `--demo` (visible, watchable pace, hands off on completion) and
  `--demo --headless` (no window, plays to an ending), with `--seed N`
  (catalog world `0`–`255`, default `DEMO_DEFAULT_SEED_INDEX = 1`); parsed in
  `src/main.c`.
- **DM-021.** The run's campaign engine is the LEADERSHIP CHAIN, each link
  measured on seed 1: chest gold is taken as permanent leadership unless
  broke; recruiting is capped in peacetime (upkeep ≤ half the commission, so
  the purse accumulates) and uncapped in WAR (a castle target known via the
  town's free intel screen (`demo/demo_brain.c town_business`) or sighted);
  fights are entered on the engine's own predicted outcome, boosted by
  raise-control casts when charges are held. Idle boats are cancelled (the
  rental fee is half the base commission).
- **DM-030.** **All configuration and diagnostics have been command-line
  flags; the project takes NO CONFIGURATION from environment variables** (the
  same rule AP-170 records for autoplay). The one environment read anywhere in
  the project is the engine's user-data directory lookup
  (`engine/savepath.c`: `XDG_DATA_HOME` / `HOME` on Linux, `HOME` on macOS,
  `APPDATA` on Windows), the platform's own convention for locating a user's
  data directory, not a knob, and outside demo mode entirely. Demo's per-tick
  decision trace is gated by the existing `--verbose` flag, set once at boot
  (`src/main.c` → `demo_set_verbose`, beside autoplay's
  `ob_diag_set_verbose`) and read through `demo_verbose()`
  (`demo/demo_brain.c`). Diagnostics are observation-only: a gated-off run
  is behavior-identical.
