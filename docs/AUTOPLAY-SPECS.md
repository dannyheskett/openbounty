# Autoplay, Specification

Autoplay has been the headless automated player and the pack-winnability
oracle: given a seed it has driven an entire OpenBounty game to a terminal
verdict, every objective cleared, or a partial result whose every miss carries
a truthful cause. It has shared the one engine and the one world model with
the visible shell (§36 of `OPENBOUNTY-SPEC.md`), so a seed played by autoplay
and by a human has been the same world. Where demo mode has played under a player's
constraints, autoplay has proved: it has read the world directly rather than
through the fog, and it has been free to snapshot and roll back to explore
many lines of play before committing one. (Both modes have simulated a fight
before entering it; that has not been the distinction, the timeline has. See
`DEMO-SPEC.md` DM-010 / DM-013.) Its verdict has been a statement about the
pack and the seed, not about luck.

**Conventions.**
- Each item has carried a stable identifier `AP-NNN`. This document has owned
  the `AP-` namespace; `OPENBOUNTY-SPEC.md` has owned `REQ-NNN`,
  `DEMO-SPEC.md` `DM-NNN`.
- Items have been written in the present-perfect factual tense ("the planner
  *has selected* X").
- Code citations have named a **file and function** (e.g.
  `autoplay/planner.c planner`), not a line number, so they survive edits.
  Autoplay code has lived under `autoplay/`; it has called the engine
  (`engine/`) but never the shell (`src/`). The one shell adapter that knows
  autoplay has been `src/shell_autoplay.c` (dependency arrow shell → autoplay
  → engine), mirroring `src/shell_demo.c`.
- No magic numbers: where a bound, price, or cap appears, the item has named
  the constant and its definition site, or the `game.json` / engine source it
  has been derived from at runtime.
- "The module" has referred to the recruiting subsystem
  (`autoplay/exec_recruit.c`), the substantive core of this spec. "The
  system" has referred to autoplay as a whole.
- Three standing rulings have governed the design, **R-A** (termination
  bounds), **R-B** (plan-cost comparison), **R-C** (the gate-charge law),
  folded into the sections they govern (§11, §8, §12) and cited by those
  names.

**Out of scope.** Rendering, audio, input, and the visible shell's own flows
have been `OPENBOUNTY-SPEC.md`'s subject; the human-like player
`DEMO-SPEC.md`'s. This document has covered the autoplay planner, executor,
mover, recruiter, and their measurement layer.

---

## Table of contents

**Part I: Architecture**
1. [Scope and layering](#1-scope-and-layering)
2. [Entry, boot, and verdict](#2-entry-boot-and-verdict)
3. [Determinism, recording, and replay](#3-determinism-recording-and-replay)
4. [Snapshot and rollback](#4-snapshot-and-rollback)

**Part II: Planning**
5. [Objectives and the plan-step set](#5-objectives-and-the-plan-step-set)
6. [The planner step core and the snapshot-tree search](#6-the-planner-step-core-and-the-snapshot-tree-search)
7. [Prerequisites](#7-prerequisites)
8. [Plan cost, the lexicographic comparison (R-B)](#8-plan-cost-the-lexicographic-comparison-r-b)

**Part III: Execution**
9. [The executor primitives and typed causes](#9-the-executor-primitives-and-typed-causes)
10. [The single movement function](#10-the-single-movement-function)
11. [Termination bounds (R-A)](#11-termination-bounds-r-a)
12. [The gate-charge law (R-C)](#12-the-gate-charge-law-r-c)

**Part IV: Recruiting**
13. [The recruiting seam and sources](#13-the-recruiting-seam-and-sources)
14. [Army search and the win predictor](#14-army-search-and-the-win-predictor)
15. [Commit](#15-commit)
16. [Economy and solvency](#16-economy-and-solvency)

**Part V: Measurement**
17. [Diagnostics](#17-diagnostics)

**Part VI: Non-goals**
18. [Deliberately not modelled](#18-deliberately-not-modelled)

---

# Part I, Architecture

## 1. Scope and layering

- **AP-001.** The system has been exactly five levels, each calling only
  downward: **autoplay** (`autoplay/autoplay.c`) → **search**
  (`autoplay/search.c`) → **planner step core** (`autoplay/planner.c`) →
  **executor** (the flat helper set across `autoplay/primitives.c`,
  `exec_move.c`, `exec_fight.c`, `exec_recruit.c`, `exec_loc.c`,
  `exec_replay.c`) → **engine** (`engine/`). The search has been the only
  component that chooses which line of play to extend; the planner core the
  only one that orders candidates and snapshots state; `execute_why` /
  `move_to` have been pure downward calls (`autoplay/exec.h`).
- **AP-002.** The executor has been a FLAT set of functions, split across
  translation units only for readability, of exactly two kinds
  (`autoplay/exec.h`): a **primitive** (one per `PrimKind`, orchestration
  only, never touching the engine directly) and a **helper** (the only code
  that touches engine/game state). Three helpers, `move_to`, `exec_fight`,
  `exec_recruit`, have wrapped real algorithms and owned private static
  internals in their own translation unit.
- **AP-003.** Every engine-changing action a helper has performed has been
  emitted to a write-only recording sink (`autoplay/recording.h`), so that
  headless == visible == replay, byte-for-byte (§3).

## 2. Entry, boot, and verdict

- **AP-010.** A run has begun in `src/main.c` (the `--autoplay --headless`
  path, dispatched before any window is created), which has built an
  `AutoplayConfig`, `seed_index` (the `--seed N` catalog world `0`–`255`, else
  `AUTOPLAY_DEFAULT_SEED_INDEX = 1`, `autoplay/autoplay.h`) and the pack dir
  (`--pack`, else the loose `assets/kings-bounty` tree of a source checkout,
  not discovery), and called `autoplay/autoplay.c autoplay_run`. The oracle has been ONE
  mechanism: a single snapshot-tree search (`autoplay/search.c search_run`,
  §6) run once from the boot state. Greedy play has not been a separate
  stage: it has been the tree's FIRST DESCENT, taken by following the step
  core's own candidate order, and the keystone promotions have been
  root-adjacent branches the search has reached by backtracking. The verdict has
  been BINARY and mapped to the process exit code: `0` = SOLVED, `1` =
  NOT-SOLVED, `2` = setup failure.
- **AP-011.** `autoplay/autoplay.c` boot has opened the pack, loaded the
  read-only `Resources` from `game.json`, allocated `Game`/`Map`/`Fog`, called
  `GameInitSeeded` with `cfg->seed_index` and the autoplay hero profile
  (`AUTOPLAY_HERO_NAME`; the class and difficulty defaults
  `AUTOPLAY_HERO_CLASS = "knight"` and
  `AUTOPLAY_HERO_DIFFICULTY = DIFFICULTY_NORMAL`, `autoplay/autoplay.h`, each
  overridable per run by `--autoplay-hero` / `--autoplay-level`, AP-015), and
  loaded the home zone: the engine-only consumer pattern, the same boot shape
  demo mode has used. `GameInitSeeded` has expanded the index into `game->seed`
  (`REQ-181a`), so the boot has never pre-set the seed itself. The same
  constants have fed the visible mode, so "same seed" has meant "same world".
- **AP-012.** `autoplay_run` has called `autoplay/search.c search_run`
  exactly once; that single call has driven the whole game to its verdict.
  The entry file has held no loop of its own. It has then copied the progress
  counts out, derived `days_used` from the pack's start calendar, scored the
  committed world with `GameComputeScore`, and counted `REC_MOVE` primitives
  for the turn tally.
- **AP-013.** `autoplay_run` has emitted the ONE authoritative terminal line
  `[VERDICT READY] <best_done>/<obj_total> completed; verdict=<SOLVED|NOT-SOLVED>`.
  The denominator has been runtime data, the enumerated objective universe
  for the pack and seed under play (§5), never a literal. `best_done` has been
  the most objectives any node has reached, universe-normalized (AP-205); it has
  equalled `obj_total` iff SOLVED and been the "how close" figure on
  NOT-SOLVED.
- **AP-014.** `--validate-pack [LO [HI]]` (default `0..255`, the whole
  catalog; a range outside `0..255` or with `LO > HI` rejected) has run the
  headless oracle for each catalog world inline and sequentially, and
  rendered a terminal table (`src/main.c validate_pack_run`) built for PACK
  AUTHORS: an ephemeral status line while a seed resolves, then one
  finalized row per seed carrying the verdict, objectives cleared, days,
  score, moves, and elapsed time, and, on a NOT-SOLVED row, the first
  objective the oracle has not cleared and its typed cause
  (`autoplay_unmet_label` / `autoplay_unmet_cause`). A closing totals row has
  reported `PASS`/`FAIL`, the solved ratio, the per-seed average days / score
  / time, and the total wall clock. The oracle's own `[AUTOPLAY]` /
  `[SEARCH]` / `[VERDICT]` output has been silenced for the sweep
  (`ob_diag_set_quiet`) so only the table has shown. The sweep has exited `0` only when every seed
  in the range has solved, `1` otherwise, and `2` on a setup failure, which,
  being seed-independent (an unreadable pack, an unknown hero class), has
  aborted the whole sweep rather than repeating for every seed. Per-run state
  has been reset at each boot: the recording sink, the day ledger
  (`ledger_reset`), the recruiter's realize-failure memory
  (`recruit_exclusions_reset`), and the pending-flow / adventure-spell UI
  globals. Two statics have deliberately persisted across runs: the
  combat-prediction memo (separated per run by the seed folded into its key,
  AP-133) and the recruiter's last realize reserve (reset at each
  `exec_recruit` entry, AP-156).
- **AP-015.** THE DAY BUDGET HAS COME ENTIRELY FROM THE CHOSEN DIFFICULTY,
  the pack's `time.days_per_difficulty` knob, selected by
  `--autoplay-level=<easy|normal|hard|impossible>` (default `normal`) and
  applied through `GameInitSeeded` (`autoplay/autoplay.c autoplay_run`).
  There has been no bespoke calendar override, and deliberately so: every
  real difficulty has been a whole multiple of `time.week_days`, so the weekly
  economy (commission, astrology, growth, restock) has stayed phase-locked and the
  run has played the pack's own calendar for that level. An arbitrary day count
  would desynchronize the weekly world, because `passed = start_days -
  days_left` has driven week ids, astrology, contract windows, and the growth
  predictor alike. `--autoplay-hero=<class>` has selected the class by pack
  class id (default `knight`, `AUTOPLAY_HERO_CLASS`); a class the pack has
  not defined has been a hard error that has failed the run rather than silently
  substituting a default.
- **AP-016.** The two verdicts have meant precisely this:
  - **SOLVED**: a full clear has been reached AND COMMITTED. Some node's
    enumeration has shown every objective done, and that node's world and
    its root-to-node recording have been what the run has left live. It has been a
    positive result about the seed: this line of play has existed and has been
    simulated.
  - **NOT-SOLVED**: the search has exhausted its DECLARED EXPANSION SET
    without reaching such a node. That set has been bounded by the per-node
    branching cap, the frontier beam, the stagnation cut, and the line-local
    cycle rules (AP-203); the most-advanced node has been committed instead,
    and its planner memory has supplied the truthful `[UNMET]` causes. It has
    deliberately NOT been a proof of unwinnability, and there has been no
    separate "unwinnable" exit code: every one of those bounds can only
    weaken a NOT-SOLVED claim, never a SOLVED one.
- **AP-017.** A verdict has never been reported over a recording that cannot
  be replayed. When the sink has dropped a push at capacity
  (`recsink_truncated`), `autoplay_run` has failed the run outright,
  setup-failure semantics, exit `2`, rather than print a verdict the replay
  cannot reproduce.

## 3. Determinism, recording, and replay

- **AP-020.** The world has been byte-deterministic: given the same seed and
  the same recorded actions, the engine has reproduced the identical world
  (floating point has been used only for animation and rendering, never
  gameplay, `REQ-185`). Combat has been re-resolved at replay time by the same
  pure function of (seed, encounter identity, mode) (`REQ-395`), so a proven
  outcome has recurred exactly.
- **AP-021.** Every engine-changing action has been recorded into a `RecSink`
  (`autoplay/recording.h`) as one `RecPrim`: `REC_MOVE` (one `GameStep`),
  `REC_ANSWER` (a pending-flow answer carrying the combat outcome), or
  `REC_ACTION` (a direct engine mutation, tagged by `RecActionKind`:
  garrison, ungarrison, dismiss, dismiss-last, buy-troop, buy-spell,
  buy-siege, rent-boat, cancel-boat, take-contract, cast-adventure-spell,
  gate-town, gate-castle, travel-zone, search, spend-week, mount-fly, land,
  and discard-spell). Push helpers: `autoplay/recording.c` `rec_push_move` /
  `rec_push_action` / `rec_push_answer`.
- **AP-022.** Each `RecPrim` has carried a world-fingerprint stamp computed by
  the one function `autoplay/recording.c rec_world_fp` (FNV-1a over
  zone/position/day/gold/leadership/travel-mode/contract/army/spellbook) and
  applied inside the one push helper. Recorder and replay have used this
  single function, so they cannot disagree on "same state".
- **AP-023.** Replay has been driven by the one dumb applier
  `autoplay/plan.c plan_exec_step` (one `RecPrim` per call), the sole replay
  path, consumed by the visible mode (AP-024): `REC_MOVE`→`GameStep`,
  `REC_ANSWER`→(combat re-run for combat-bearing flows via
  `autoplay/exec_replay.c autoplay_apply_recorded_combat`)→`player_io_answer`,
  `REC_ACTION`→`autoplay/exec_replay.c autoplay_apply_rec_action`. A
  per-primitive fingerprint mismatch has been a HARD FAILURE: the applier has
  reported `[REPLAY-DIVERGE]` with both fingerprints and the prim kind, then
  aborted. Continuing past a mismatch would act on a world the recording has
  not described, so the run has stopped at the first diverging primitive
  rather than silently playing on.
- **AP-024.** `--autoplay` without `--headless` has been the visible mode:
  the run has been resolved headlessly first (the same single `search_run`
  call on a separate world), then `src/shell_autoplay.c` has replayed the
  recording on the live shell world through `plan_exec_step` at a watchable
  pace, with fights animated from their `CombatTurnRecord` by the shared
  combat animator (`src/combat_replay.c`) before the identical resolution has been
  applied. On completion the shell has handed the cleared board to the human
  (the same win hand-off demo mode has performed).
- **AP-025.** The stamp has been of the PRE-action state, so the recorder has
  had to push BEFORE it has mutated the engine: replay has checked the fingerprint as a
  precondition, and a stamp taken after the mutation would describe a world
  the replay has not reached yet. Where an action's id has been knowable only after
  the mutation (`RA_TAKE_CONTRACT`, whose id has been `GameTakeNextContract`'s
  return), the caller has captured `rec_world_fp(g)` first and pushed through
  `rec_push_action_fp`, the one sanctioned way to stamp out of band, never a
  post-mutation fingerprint.
- **AP-026.** A lost fight has been a temp death, and replay has MIRRORED the
  live side's response to it: the engine has teleported the hero home, and the
  live run has then run `exec_temp_death` (zone map reload plus the
  bookkeeping). When `player_io_answer` has reported `pres.temp_death`, the
  applier has run the same `exec_temp_death`. Without the mirror the replayed
  hero would walk the new zone's coordinates on the OLD zone's map, an
  invisible desync that surfaces only when a terrain difference changes a
  day-spend.

## 4. Snapshot and rollback

- **AP-030.** An attempt has been atomic. Before executing anything,
  `autoplay/planner.c planner_attempt` has recorded a recording mark
  (`recsink_mark`) and a full world snapshot (`autoplay/worldsnap.c
  worldsnap_capture`). On any failure -- a prerequisite, the objective, or a
  mid-attempt recruit -- it has restored the snapshot and rolled the
  recording back to the mark. The whole chain has committed together or not
  at all.
- **AP-031.** `worldsnap_capture` has copied `Game` (`GameCopy`, its heap
  tables included), `Map` (the header, string pool and tiles), `Fog`
  (`FogCopy`), the world RNG (`GameRngSnapshot`), and the diagnostic ledger
  (`ledger_snap`) into a `WorldSnapshot` (`autoplay/worldsnap.h`), so the copy
  has been complete. `worldsnap_restore` has reversed it bit-identically,
  restored the RNG, unsnapped the ledger, and reset the `pending_*` flow
  globals (a capture has always been taken at `FLOW_NONE`).
- **AP-032.** Because a failed attempt has restored the world
  bit-identically, memoized projections have stayed valid across failures,
  and identical world states have yielded identical decisions. There has been
  no runtime per-step assertion; divergence detection has lived in replay
  (§3).
- **AP-033.** Planning has left no residue: the adventure-spell UI
  continuations (`bridge_state`, `gate_state`, process globals outside
  `Game`, which `worldsnap_restore` cannot revert) have been reset via
  `spells_adventure_reset_ui` after any planning pass that has probed gate or
  bridge casts, so a probe has never left a live continuation armed.

---

# Part II, Planning

## 5. Objectives and the plan-step set

- **AP-040.** The planner has enumerated one `PlanStepSet`
  (`autoplay/goals.h`, `PlanStep steps[STEP_MAX]` + `count`) covering every
  objective in the world: chests, artifacts, navmaps, orbs, the alcoves and
  the one-time vistas read from every zone, monster and villain castles,
  hostile wandering foes, and the buried scepter added last
  (`autoplay/goals.c plansteps_enumerate` and its per-kind helpers).
  `obj_total` has equalled `set->count`.
- **AP-041.** A `PlanStep` (`autoplay/goals.h`) has held: `kind` (a
  `PlanKind`: `STEP_CHEST`, `STEP_ARTIFACT`, `STEP_NAVMAP`, `STEP_ORB`,
  `STEP_ALCOVE`, `STEP_SIEGE_WEAPONS`, `STEP_MONSTER_CASTLE`, `STEP_VILLAIN`,
  `STEP_FOE`, `STEP_SCEPTER`, `STEP_VISTA`, `STEP_MUSTER`), a target point,
  `zone_index`, a stable `handle` (placement / castle / villain / event /
  troop id), and a display `label`. `STEP_SIEGE_WEAPONS` and `STEP_MUSTER`
  have existed only as prerequisite candidates (§7), never as enumerated
  objectives.
- **AP-042.** Completion has been a single uniform per-kind predicate
  `autoplay/goals.c planstep_is_done`: consumables → tile in `g->consumed`;
  alcove → `GameHasRites` for the alcove's zone (`knows_magic` without rites
  per zone); siege weapons → `siege_weapons`; monster castle → taken once
  (`CastleRecord.taken`, or player-owned; a castle left empty has fallen back to
  the monsters at the week's end, REQ-302, and has not been an objective again);
  villain → `villains_caught`; scepter → `stats.won`; foe → gone by
  `placement_id`+zone; vista → `GameEventFired`; muster → the demanded troop
  in the army.

## 6. The planner step core and the snapshot-tree search

- **AP-050.** The planner has been a RE-ENTRANT STEP CORE
  (`autoplay/planner.h`), not a loop: `planner_open` has enumerated the objective
  universe and initialized run state, `planner_candidates` has run one cycle
  head (logistics, mover pricing, ordering, the promotion tiers),
  `planner_step` has performed ONE atomic attempt, `planner_refresh_done` has re-read
  the done predicates, `planner_done` has been the goal test, and `planner_report`
  has printed the truthful end-of-line causes. The search (AP-200) has driven these
  one decision at a time; there has been no full-game planner loop.
- **AP-057.** `planner_candidates` has ordered the open objectives
  cheapest-first by the mover's query-mode quote (§10) so in-zone work
  has scheduled ahead of crossings, cross-zone ties breaking by zone so one sail
  has been followed by that zone's whole slate. Three promotion tiers have then
  refined that order: the first-villain keystone (AP-055), the scarce-winner
  conflict tier (AP-054), and the alcove keystone (AP-081). The finale gate
  (AP-052) has been applied here: the scepter has been offered only when every other
  objective has been done.
- **AP-058.** All planner memory for one line of play has ridden in a
  `PlannerRun` value (`autoplay/planner.h`), never in statics, so a search
  node can carry it: per-objective `done` / `defer_why` / `defer_cause` /
  `ever_failed` / `stuck_cycles`, the armed wait pass, the escape budget, and
  the identity-keyed failure history. That history has been keyed by a hash of
  (kind, zone, x, y, handle), NOT by enumeration index: an open on a
  progressed world has re-enumerated a SHRUNK universe (dead foes and consumed
  tiles have dropped out), so indices have shifted between opens while the history has had to
  follow the objective. `stuck_cycles` has been CALENDAR-FREE (it has counted
  opens, not days), so the day budget has never shifted a promotion decision.
- **AP-051.** Each attempt (`planner_attempt`) has been do-or-fail under a
  world snapshot: the stranding pre-gate, the gate-kit re-stock, the
  objective's unmet hard gates (siege weapons; the villain's contract has been
  handled inside the siege primitive, AP-061), then the objective itself
  through `execute_why`. On failure the snapshot and the recording tail have
  rolled back in full, with two exceptions: an attempt that ACCOMPLISHED its
  goal predicate has admitted unless the hero has ended MAROONED, and a PREFOUGHT
  siege has kept the world without admitting (a contract-less win has reset the
  lord's garrison, real progress a restore would erase; the engine has recorded it
  in `villains_prefought`). The STRANDING rules in full (`exec_step_strands`
  + `move_reachable_nodes_avoid` / `move_escape_jaw`):
  - **Pre-gate.** A stand-on objective (consumable / dig) whose end tile has had
    no exit has been skipped; a SINGLE-MOUTH pocket whose mouth an unbeatable
    hostile has camped within follow range has been a JAW TRAP and skipped this
    cycle (`[STRAND]`).
  - **Post-success probe.** After a success, unbeatable hostiles near the
    hero have counted as WALLS; when the reachable set has collapsed (below the
    pocket node bound `AUTOPLAY_POCKET_NODES`, `autoplay/exec.h`) the hero has been
    inside a closing jaw. Two second chances have run before judging: fight
    the jaw open from INSIDE (the sealer has been an objective, and the pocket has often
    held its own funding; a win has committed both), else walk out
    (`move_escape_jaw`). Only a failed escape has been a maroon: the success
    has rolled back, the nearest sealer has been recorded as the objective's BLOCKER,
    and the defer has carried the jaw fight's truthful cause (gold / stock when
    the break-out is money-bound).
- **AP-052.** One selection ruling has shaped the ordering, the **finale
  gate**: a `STEP_SCEPTER` candidate has been attempted only when every other
  objective has been done. The King's-quest-last rule, no exceptions.
- **AP-053.** THE NO-BURN LAW: play has never spent calendar merely waiting.
  Every wait site -- `exec_ensure_gold` (money: boat fares, the alcove fee)
  and the plan-realization restock loop (army stock, AP-137) -- has failed
  immediately with a typed defer unless the **wait-allowed flag** has been armed
  (`autoplay/exec.h exec_set_wait_allowed` / `exec_wait_allowed`). The law
  has been applied at NODE granularity: a node whose candidates have all failed to
  produce a child (deferring PROVEN useless from that state) has not been
  immediately abandoned. It has had ONE re-listing with the flag armed (each
  wait bounded by its own need), carried in `PlannerRun.wait_armed` and
  disarmed on the FIRST success, because the world has then changed and
  deferring has worked again. A line still progressing on its own has therefore
  never waited, and all calendar burning has been back-stacked to the point
  where that line has proved stuck. THE FUNDED WAIT has ridden the same pass: a
  fight whose search has failed gold-bound has never reached a wait site (there has been no
  plan to realize), so under the armed flag `exec_recruit` has bisected the
  smallest budget that has bought any winner (the same funnel in existence mode),
  has played the income weeks that shortfall has needed (`exec_ensure_gold`, with no
  calendar look-ahead: the engine's own TIME failure has ended a wait the
  days cannot cover), and has searched again on the richer world, all rolled
  back with the attempt if the fight still cannot be won (mechanics in
  AP-162). The funded wait has acted only on the engine's own arithmetic: it
  has committed calendar to a shortfall only while the weekly net
  (`GameWeeklyNetGold`) has been positive, has waited exactly the weeks that net has needed,
  and has refused a need the income cannot meet, so the wait has been bounded by what
  the pack's economy can deliver, never speculative. The principle behind back-stacking all waiting to the
  fixpoint: a fight strangled only by calendar (an endgame lord an in-line
  attempt cannot fund) has become winnable when the whole run's accumulated
  income has been made available at the proven end, at the cost of admitting more
  calendar spend, a trade the law has taken only after deferring has been
  proven useless, so it has never lengthened a run that has been clearing on its own.
- **AP-055.** KEYSTONE PROMOTION AND CYCLE LOGISTICS, two mechanisms that
  have run at each cycle start, before the attempt loop:
  - the FIRST-VILLAIN KEYSTONE (`planner_candidates`, `s_keystone_kind`):
    while no villain has been caught, the cheapest villain in the hero's zone
    has gone to the front of the order, so the contract economy has started
    early;
  - the ALCOVE KEYSTONE PROMOTION (`planner_candidates`): the alcove has unlocked
    the whole spell economy (AP-081), so an alcove still unmet with the
    wallet already covering its cost (`GameAlcoveCost`), whose FIRST failure
    lies more than `KEYSTONE_STUCK_CYCLES` opens in the past
    (`autoplay/exec.h`), has been promoted to the FRONT of the order. The
    bound has counted OPENS, not calendar days, so the day budget has never
    shifted the decision. An alcove that has resolved within its natural
    cheapest-first slot has never promoted.
  - transport/stop LOGISTICS (`planner_logistics`, with magic learned and a
    solvent wallet): surplus raise / instant-army charges have been cast off
    and idle combat charges field-discarded (the engine's own flow, recorded)
    to free shared book capacity (AP-112); the castle-gate book has stocked in
    BULK TIERS once the book has widened (the R-C floor always; larger targets at
    the widened-book thresholds documented at their definition site; every
    held charge above the floor has been one zero-day crossing, the endgame's
    dominant sink); town gates have topped to the floor once the book has afforded
    both transports; and Time Stop charges have filled the remaining room
    after a deep forcing pass (cast-offs plus cheapest-combat discards), each
    cast before a step banking `GameTimeStopStepsPerCast` free steps, the
    engine's own gold-to-calendar exchange (decisive where full-day terrain
    has made every unstopped step cost the day). Each stocking trip has been
    atomic like any attempt.
- **AP-056.** THE SACRIFICE ESCAPE: when a node has produced no child even
  after its one wait-armed re-listing (AP-053), and the calendar has still
  had days left, one further expansion has taken the game's own escape hatch, a
  deliberate defeat whose temp death has teleported the hero home for the price of
  the standing army. Two forms, tried in order: attacking an adjacent hostile
  foe (within the engine's follow gate), else, with no foe to lose to (a
  boatless, dockless pocket), dismissing the whole army through the engine's
  "sent back to King" confirm chain (`autoplay/exec_loc.c
  exec_dismiss_last_escape`, recorded as `RA_DISMISS_LAST`). The escape
  budget has been at most 4 per consecutive futile streak: any successful
  attempt has re-armed it (the bound has existed to stop futile escape loops, not to
  ration recoveries that have kept buying progress; a maroon-prone geography can
  need one per island). The escape has been recorded like any fight and reported on the
  `[PLANNER]` channel; only when no such escape has existed has the node exhausted
  and the search backtracked.
- **AP-054.** The cheapest-first ordering has been refined against one
  conflict: when two SIEGE candidates' projected winners have drawn on the same
  FINITE troop stock, the cheap one's shopping can consume the dwellings the
  expensive one's only live winner has needed, leaving that candidate -- winnable
  at the moment it was first projected -- never projectable again once the
  shared stock has gone. A SCARCE-WINNER CONFLICT TIER has therefore refined
  the selection: before a non-held-win siege best has been attempted, each other
  open (not-done) siege candidate with a live projected winner has been
  probed with two pure queries (`autoplay/exec_recruit.c
  recruit_winner_finite_draw`, the R-B-cheapest winner's finite-source
  purchases, and `recruit_winner_survives_less`, the same projection with that
  draw deducted from the finite sources, optionally relaxed to the dwelling
  restock ceiling); when the other candidate's winner has DIED under the best's
  draw while the best's own winner has survived the reverse draw (live, or
  recoverable at the restock ceiling), the fragile winner has been attempted
  first. Sieges only (foes have never bound elite stock), one swap per cycle,
  projection-only probes (memoized sims, no state change), an ordering
  refinement inside the ratified selection shape, not a bound on any plan.
  Verbose channel: one `[PLANNER] scarce-winner swap` line per firing.

### The snapshot-tree search

- **AP-200.** THE SEARCH HAS BEEN THE ORACLE (`autoplay/search.c
  search_run`), run once from the boot state. A NODE has been a REACHED WORLD
  STATE carrying everything needed to resume from it: its world snapshot, its
  `PlannerRun` planner memory, the recording prims ITS OWN EDGE appended, its
  ordered candidate list with a cursor, and its lineage pointer to its parent.
  An EDGE has been one attempt. Expanding a node has meant restoring it and
  attempting EXACTLY ONE objective through `planner_step`, never a playout,
  never a rollout to the end of the game. Nothing has ever been simulated
  twice: every edge in the tree has been simulated once, when it was created.
- **AP-201.** The frontier has been an AVL multiset (`autoplay/baltree.c`),
  chosen because it has given all three operations a memory-bounded frontier
  has needed: pop-min (take the next node), insert (add children and re-insert a
  node with branches left), and pop-max (evict when over the cap). A heap
  has given the first two and not the third.
- **AP-202.** Ordering has been STRICT DEPTH-FIRST. The key has been the
  negated creation sequence (`node_key` = `-seq`), so pop-min has returned the
  NEWEST node (the current line's tip) and a re-inserted parent has kept its
  original, older key, so a child and its whole subtree have drained before the
  parent's next branch. That has been exact backtracking DFS expressed on the
  tree, and pop-max has evicted the ROOT-MOST pending alternatives, which have been
  the last resort. THE CURRENT LINE HAS HAD ABSOLUTE PRIORITY; backtracking
  has happened only on exhaustion. Two best-first keys have been measured and
  rejected: `f = g + h` over elapsed days, and fewest-open-first. Both have
  turned the frontier into a swamp of near-identical siblings (segments have cost
  0–2 days and successful reorderings have collapsed into states already seen), and
  the descent has never committed (118k expansions at 14 objectives done; 155k at
  12). Determinism has followed from the key: same build and seed have given
  the same creation order, the same pops, the same plan.
- **AP-203.** The DECLARED EXPANSION SET, the bounds that have made NOT-SOLVED a
  statement about a capped search rather than about the seed (AP-016). Each
  has been a deliberate trade, and each can only weaken a NOT-SOLVED claim:
  - **Branching cap.** `SEARCH_MAX_CHILDREN` child states per node, after
    which its remaining alternatives have been trimmed. Failures have still fallen through
    the WHOLE candidate list (that has been the cheapest-first ordering doing its
    job) but successful branching has been bounded. The ROOT has been exempt, so
    its alternatives have always remained available to a restart.
  - **Beam.** `SEARCH_MAX_LIVE_NODES` live nodes; over that, pop-max has evicted
    the oldest pending alternatives, counted and reported, never silent.
  - **Stagnation cut.** A descent that has completed nothing for
    `SEARCH_STAGNATION_CUT` expansions has been a doomed region: the frontier
    has been pruned back to the root, whose next untried candidate has opened a fresh
    descent under a different opening objective. Measured on seed 17, where
    low-calendar subtrees have failed with `cause=time` about 170 expansions per node
    and plain DFS has drained them all before returning.
  - **Line-local cycle rules.** A child whose state signature equals any
    ANCESTOR on its own path has been a true loop and been dropped, and
    consecutive KEPT edges (world kept, nothing completed, the PREFOUGHT
    shape) have been capped at `SEARCH_MAX_KEPT_STREAK`, because an unbounded
    KEPT chain has been a calendar-burning descent at constant progress (measured:
    15k expansions pinned at 28 objectives, tip 385 days deep).
  - **Dead leaves.** A child born with the calendar exhausted and work still
    open can only fail every later attempt with `cause=time`, so it has not
    been inserted. Declared trade: a finish that would need every remaining
    gate to cost zero days from exactly 0 days left has been given up.
  - **Runaway watchdog.** `SEARCH_MAX_EXPANSIONS`, sized far above any real
    run (R-A.2); hitting it has been a defect to fix, not a scheduling
    signal.
- **AP-209.** There has deliberately been NO GLOBAL duplicate-state set. A
  closed set without reopening has been unsound here: a state first reached
  by a sibling line that later dies would block the main line from ever
  entering it (measured: the descent has stalled at 28 objectives behind exactly that).
  Cycle protection has been line-local instead (AP-203).
- **AP-204.** A node has carried the FULL world (`WorldSnapshot`: Game + Map +
  Fog + RNG + ledger, about 1 MB), and memory has been paid for with a
  smaller beam. The slim alternative (Game only, with the map rebuilt by a
  zone reload) has been measured WRONG: a mid-game fresh stamp has not been
  byte-equal to the boot map plus the line's incremental writes (foes stamped
  at moved positions, cleared-tile residue), and the recording cannot
  reproduce a map the line has not derived from prims. Lifetime has been
  refcounted along the lineage, so discarding a branch has freed it up to its
  first still-referenced ancestor. The root has also been held by the search
  itself for its whole run: as the oldest node it has been the first the beam
  has evicted, and a stagnation cut that has re-listed it has then taken a fresh frontier
  reference, so its descendants have never outlived it. Candidate lists have been priced
  LAZILY at a node's first pop: about 40% of created nodes have never been popped
  and have never paid the pricing pass.
- **AP-205.** Progress has been UNIVERSE-NORMALIZED. Each node's enumeration
  has SHRUNK as objectives have completed, so "done within this node's universe" has
  not been comparable across nodes. The OPEN count has been the same quantity in
  every view, so true progress has been `root total - open`. The goal test has been
  `open == 0`.
- **AP-206.** The recording of a node has been the concatenation of the edge
  deltas from the root down to it, rebuilt on every restore
  (`node_rebuild_recording`); the root's own delta has been the whole
  pre-search prefix, so a rebuild has reproduced the boot segment too. An edge's
  delta base has been marked BEFORE the candidate pricing pass, because the
  cycle head has run LOGISTICS, which can record prims and move the world: those
  prims have belonged to the child's edge, or the rebuilt recording would lose the
  stocking trips whose effects its own fingerprints have assumed.
- **AP-207.** The outcome state has been COMMITTED, never re-simulated: on a
  hit the winning node, on a miss the most-advanced node. Its world and
  rebuilt recording have become live and its planner memory has supplied the
  `[UNMET]` causes, so the reported verdict, the committed world, and the
  replayable recording have all described the same line of play.
- **AP-208.** Because a restore has jumped between DIFFERENT lines of play, the
  search has reset the process-global working state the planner's own
  same-play rollback has deliberately kept: the mover's nav memo
  (`nav_cache_invalidate`), the recruiter's realize-failure memory
  (`recruit_exclusions_reset`), and the calendar-death latch. It has
  additionally enabled the recruit-search memo (`recruit_cache_enable`)
  around its own expansions; the memo key has been complete, so an enabled
  cache has returned the same winner the search would have computed.

- **AP-046.** **Vistas have been objectives.** Every `events` entry a zone
  has declared (REQ-221b) has been enumerated as a `STEP_VISTA` whose target has been
  the trigger tile and whose handle has been the event id (`autoplay/goals.c`); it
  has been done when `GameEventFired` has said so. The executor (`exec_vista`,
  `autoplay/primitives.c`) has bought any rite the vista requires at a town
  that has sold it, then stood on the tile, where the engine has played the scene and
  applied its effects; the prerequisite gate has marked it `PREREQ_MAGIC`
  while the hero cannot hold that rite, and `PREREQ_RELIC` while an artifact
  it has asked for has stayed unfound. This has been how a pack has gated ground behind
  something other than a fight: without it, Rome's Po plain would be
  unreachable, and every objective behind the Rubicon with it.

## 7. Prerequisites

- **AP-060.** `autoplay/prereq.c` has encoded only the engine-enforced hard
  gates, in two parts. `prereq_unmet` has emitted the prerequisite
  CANDIDATES an attempt has run first: for a monster or villain siege with no
  siege weapons, one BUY-SIEGE candidate (`prereq_make_buy_siege`, resolved
  to the nearest town at execution), and for a gate foe that has demanded one arm
  (REQ-296a), one MUSTER candidate (`prereq_make_muster`, fetching that
  troop). `planner_attempt` has executed each candidate under the same
  snapshot before the objective, so a failing gate has rolled the whole
  chain back. `prereq_gated` has been the static gate, one O(1) read per
  objective: `PREREQ_ZONE` (the objective's zone not yet discovered),
  `PREREQ_CONTRACT` (a villain whose contract has not been obtainable at that point),
  `PREREQ_SIEGE` (no siege weapons), `PREREQ_MAGIC` (an alcove whose rites
  the wallet cannot yet buy, or a vista whose rite the hero cannot hold),
  `PREREQ_RELIC` (a vista's artifact unfound) and `PREREQ_FINALE` (the
  scepter while other work has been open, AP-052). A gate foe's demand has
  deliberately not been a static gate: gating it would demote the gate in the
  candidate ordering and leave the objectives behind it to thrash, so the
  MUSTER candidate has fetched the arm inside the attempt instead.
- **AP-061.** The villain's active-contract requirement and the
  water-position / army-strength "soft" prerequisites have deliberately not
  been modeled as discrete steps; they have been handled inside
  `siege_or_slay` / `exec_ensure_contract` and by reachability + combat
  prediction respectively (`autoplay/prereq.h`). `exec_ensure_contract`'s
  take loop has tolerated EMPTIED cycle slots: once catches have outnumbered
  refills (`GameFulfillContract` has advanced `max_contract` whether or not a
  refill has landed), a rotation step can return no contract while the target
  has sat in the NEXT slot, so the loop has kept taking through a full cycle
  instead of stopping at the first empty slot, exactly as a player has re-pressed
  the town's contract row.

## 8. Plan cost, the lexicographic comparison (R-B)

- **AP-070.** Where multiple winning plans have existed, the module has
  selected among them by total real cost, compared **lexicographically:
  calendar days first, gold second** (ratified R-B; `autoplay/exec_recruit.c
  build_candidate` / `cand_cmp`). Days have been the resource the game ends
  on; gold has been a means to an end and has only broken ties.

      cost(plan) = ( travel_days , gold_outlay )

  The gold_outlay term has been the plan's purchases at pack prices: the
  cheapest-first quote fill of every troop buy (`quote_fill`), the raise
  charge shortfall, doubled when an off-zone castle fight's approach has had to
  sail (the crossing's week boundary has reset the pre-cast lift, so the full
  `k` charges have been bought twice; a zero-day castle-gate approach into the
  fight's zone has waived the premium, `fight_zone_gate_ready`), and, when the
  plan has carried a spell kit (AP-136), the kit book's combat-charge shortfall.

- **AP-071.** `travel_days` has been one engine week quantum
  (`res->time.week_days`) per DISTINCT off-zone zone the plan's buys
  have actually toured (`build_candidate`'s zone mask; garrison draws have travelled by
  gate and priced zero), a plan gathered where the hero has stood has priced 0
  days, so among winners the cheapest has been the easiest to GATHER. Every term
  has been derived at runtime from the pack.
- **AP-072.** There has been no gold-to-days exchange rate. A scalar rate
  under a small commission would make gold artificially dear in days and rank
  calendar-monstrous world tours "cheaper" than local wallet spends;
  comparison has therefore stayed lexicographic (R-B), not scalarized.

---

# Part III, Execution

## 9. The executor primitives and typed causes

- **AP-080.** `autoplay/primitives.c execute_why` has been the executor entry
  the planner has called. It has dispatched a `PlanStep` by kind to its
  implementing helper, each running a bounded move/act loop
  (`EXEC_MAX_ROUNDS = 12`, `autoplay/primitives.h`, a runaway guard per R-A,
  §11), and returned both a display `why` string and a machine-readable
  `ExecCause`.
- **AP-188.** `exec_fetch` has handled the two ways an arrival fails to fire
  the consumable. THE HOVER BOUNCE: interactive tiles have fired on foot ENTRY
  only, and landing on one has been illegal (`GameCanLandAt`), so a flight leg can
  end parked ON the goal with nothing fired, so the fetch has stepped aside,
  landed, and walked back in. THE FLIGHT FALLBACK: a consumable
  foot-unreachable in the hero's own zone (a grass hollow inside a blocking
  ring has had no foot entry) has been reached by fielding an all-flying army
  once per attempt (`fetch_field_fliers`: dismiss the non-fliers, recorded,
  recruiting one flier from an in-zone dwelling first when none has been held) and
  retrying the approach; the mover's fly legs have been the door.
- **AP-081.** There has been one primitive helper per objective family
  (`autoplay/primitives.c`): `exec_fetch` (walk onto a consumable: chest /
  artifact / navmap / orb), `exec_learn` (the alcove), `exec_vista` (a
  one-time vista, AP-046), `exec_town_buy_siege` and `exec_muster` (the
  prerequisites, AP-060), `siege_or_slay` (castles, villains, and wandering
  foes), and `exec_dig` (the scepter win). A primitive has been orchestration
  only; the engine touches have lived in the shared helpers (AP-002). The
  alcove lesson has been a purchase (`GameAlcoveCost`; the engine's accept
  has refused without charging when the wallet has been short), so
  `exec_learn` has deferred a broke arrival typed `EXEC_CAUSE_GOLD` before
  stepping onto the tile; `exec_ensure_gold` itself has played income weeks
  for the fee only under the wait-allowed pass (AP-053), because a run that
  has ended without magic has lost the whole sustain-kit candidate class: every
  objective whose only winner has needed a combat spell has stayed unreachable.
  Deferring the alcove to fixpoint time, where the funded wait can cover the
  fee, has therefore recovered that entire class of objectives instead of
  stranding the run short.
- **AP-082.** `siege_or_slay` (`autoplay/primitives.c`) has been
  simulate-first: it has predicted the held army (`exec_fight_winnable`);
  taken the contract for a villain (`exec_ensure_contract`); climbed the
  raise ladder (`exec_raise_k_for_win` → `exec_raise_for_fight`), falling
  through to the recruiter (`exec_recruit`) when no lift alone has won;
  PRE-STOCKED the castle-gate transport before a rich off-zone approach
  whose fight zone has held a gate destination (so the approach has ridden day-free
  gates and the pre-cast lift has survived, AP-186); approached, re-arming the
  lift in the fight's zone when a crossing reset it; verified the fight
  LIVE at the gate (`exec_fight_winnable` on the standing army and book) --
  a failed live verify has declined the prompt and earned ONE second chance
  (re-arm the ladder the current leadership has needed, else re-run the recruit
  once: later stocking can have evicted the plan's decisive kit charges) --
  then fought (`exec_fight`). The same primitive has handled a wandering foe,
  engaging its live tile (AP-086).
- **AP-083.** The typed failure seam has been the `ExecCause` enum
  (`autoplay/exec.h`): `NONE`, `OTHER`, `NO_WINNING_ARMY`, `STRANDED`
  (done-but-marooned → roll back), `TIME` (calendar exhausted, terminal),
  `PREFOUGHT` (keep-without-admit), and the binding-constraint causes `GOLD`,
  `STOCK`, `LEADERSHIP`, `REACH`. A no-winning-army verdict has named the
  actual binding constraint (§14), never a self-imposed limitation. The `why`
  string has been report-only.
- **AP-084.** The combat policy's SUSTAIN term (`autoplay/exec_fight.c`), the
  license for a melee stack to kite instead of closing, has counted magic as
  standing output only via the round's cast latch (`Combat.spells_this_round`),
  set exactly when a strictly-positive probed cast has fired this round; the
  probes have run once, inside the round's cast attempt, and have never been
  repeated for the sustain test. Charge POSSESSION has not sustained: a held
  book whose every probe has been zero (resurrect with nothing dead, freeze against
  an all-IMMUNE garrison) has let a melee stack kite an idle book forever,
  handing the engine's no-progress cutoff a false LOSS with the enemy
  untouched, a verdict the win-predictor would inherit, since the sim and the
  live fight have shared this policy.
- **AP-085.** The gold-chest answer (`autoplay/primitives.c
  answer_chest_choice`) has taken the PERMANENT leadership option
  (`leadership_base += gold/50`, ×2 with the Crown) while any villain has
  remained uncaught and the wallet has already covered one week's outgoings
  (upkeep + boat fare, from the engine's own `GameArmyWeeklyUpkeep` /
  `GameBoatCost` arithmetic, never a hand-rolled burn formula); gold has been the answer
  only when cash-poor or with nothing left to control. Taking leadership only
  while some earlier milestone is unmet would pin `leadership_base` at its
  rank floor while gold accumulated unspendably, and a run could arrive at its
  terminal siege control-bound (the winning army over its leadership cap) with
  a wallet it had no way to convert into the leadership the fight needed;
  taking leadership from chests whenever control has still been the scarce resource
  has kept the cap growing in step with the army the endgame has required.
- **AP-086.** A hostile foe CO-LOCATED with the hero (the foe has followed onto
  the hero's tile) has been re-provoked, not re-fought: the follow-collision's
  flow has been the only engagement a shared tile has offered, and once it has been consumed (a
  past decline while the army was weak) no step can re-fire the interactive
  from inside the tile, `exec_fight` without a pending flow has been a no-op, the
  pursuit's arrival test has already been satisfied, and the round budget would
  spin to the watchdog. `autoplay/primitives.c engage_reached_foe` has therefore, on a
  co-located foe with `FLOW_NONE` pending, stepped OFF to an adjacent legal
  non-interactive tile (legality by the hero's LIVE travel state, the
  engine's own `adventure_walkable_*` predicate) and back ONTO the foe's live
  tile, so the return step has fired `INTERACT_FOE` (or the step-off itself
  has collided with the following foe); the fight has then proceeded as any provoked
  fight. When no legal re-provoke step has existed (walled in), the failure has
  been TYPED (`EXEC_CAUSE_REACH`, defer label `slay:contact-blocked`) so the
  planner has deferred instead of re-entering the spin. Without the re-provoke, a
  foe left co-located by one consumed collision flow has been unreachable by any
  later attempt and has turned every strong-army retry into a round-budget spin
  (the AP-102 watchdog defect class); with the re-provoke the same foe has
  fallen at the first attempt whose army can win it.
- **AP-184.** THE WEEK HAS BEEN THE ONLY CALENDAR QUANTUM. Every calendar
  spend has gone through `exec_spend_week` (`autoplay/exec_loc.c`), which
  has advanced the exact day count to the NEXT week boundary, day-granular at
  boundaries by construction, under THE NO-BURN LAW (AP-053). There has been
  no sub-week rest action, because no plan has needed one: every wait the module
  has taken has been sized by the weekly economy (income, restock, growth), which
  has only moved at those boundaries.

## 10. The single movement function

- **AP-090.** All transit (walk, boat, flight, gate teleport, bridge) has
  routed through the one function `autoplay/exec_move.c move_to`. Its
  signature has returned the arrival index of the cheapest reachable target
  (arrival = onto a passable target tile or adjacent to a bouncer), `-1` to
  defer, or in `commit=false` query mode the chosen index with the step-cost
  written out (`MV_XZONE_COST` + zone hops for a cross-zone target, so
  in-zone work has always been scheduled ahead of a sail).
- **AP-091.** `move_to` has been: the already-satisfied check
  (`mv_satisfied`, reused at every give-up so a bounce-arrival has never been
  mislabeled unreachable); one pricing relaxation over every target (query
  mode has returned here); then, committing, a bounded leg loop: cross-zone
  crossing (`do_crossing`), same-zone gate leg when strictly cheaper
  (`mv_gate_leg_score`), the drive (`drive_leg_ex`, handing rent-dock
  boardings back for the town visit + rent), flow answering with
  declined-foe avoidance, and the once-per-leg-loop recovery levers
  (AP-103). The A* kernel (`nav5_run`, a layered (x, y, mode) search with a
  lazy-deletion heap) has been folded into this file.
- **AP-092.** Gates have been transit: a held gate charge has been priced like
  any other leg but at **zero calendar days**, so a castable landing whose
  walk remainder is strictly cheaper than the direct route has won
  (`mv_gate_castable` / `mv_gate_leg_score`). Castability has been pure, a
  charge already held and the last-charge law holding (§12); acquiring charges
  has been the executor's decision, never transit's.
- **AP-187.** THE ZERO-DAY STOP RESTOCK LOOP (`autoplay/exec_move.c
  maybe_gate_restock_stops`, at `move_to`'s top after the satisfied check):
  with the stop book dry mid-attempt, magic learned, a deep wallet and a
  widened book (the thresholds at the function's definition), two town-gate
  charges held, and no flow pending, the mover has hopped by town gate to a
  VISITED town selling Time Stop, stepped in (gate landings have sat beside the
  town, `REQ-322`), bought stop charges into the free book less the two slots
  the return refill has needed, and gated back to the town-gate seller's own town
  (legal from one charge under the seller exemption, AP-110, and refilled to
  the floor on landing), converting gold into stop charges without spending a
  day or leaving the working zone. The loop has been available only where
  the town-gate seller has shared the hero's zone (elsewhere the return leg would
  strand the march) and has been suppressed during realize trips
  (`exec_set_gate_restock_suppressed`: those have carried exact candidate armies a
  mid-move teleport would scramble).
- **AP-093.** Boat state has been resolved in the leg loop: a hero not yet
  sailing has been put to sea by `nav_ensure_sailing`, renting through
  `nav_rent_boat` when no boardable boat has been in the zone, and the A* itself
  has carried RENT-DOCK BOARDING EDGES (every same-zone town dock the wallet
  can rent at has been a priced boarding point; the drive has performed the town visit +
  rent when the path has used one), so water-locked in-zone targets have priced and
  driven without a separate crossing decision. Recovery levers -- a
  stranded-boat re-rent, and a last-resort gate-out -- have each fired at
  most once per `move_to_once` call (§11).
- **AP-094.** Hostile-foe tiles have been FIGHT-THROUGH edges, not walls
  (foes have chased the hero and routinely sealed choke points): the A* has priced
  them with a penalty, the drive has stepped on and the responder fought when
  the live prediction has won; a DECLINED fight has added that foe's tile to the
  call's avoid set so re-paths have routed around the refusal instead of
  re-provoking it.
- **AP-181.** The mover has considered the Bridge adventure spell (`REQ-322`:
  up to 2 consecutive water tiles converted to walkable bridge grass, cost
  from the pack's spell catalog, one charge) as a candidate leg, priced like
  any other transit by its calendar-day and gold cost (R-B, §8) and adopted
  only when the bridged route has been strictly cheaper than the walk / boat / gate
  alternatives. A bridge cast has been recorded as an `RA_CAST_ADV_SPELL`
  action (AP-021) so headless == visible == replay. Charge stocking has
  followed the plan's leg count with no reserved floor of its own; acquiring
  charges has been the executor's decision, never transit's (AP-092). A
  bridge has shortcut a water gap that would otherwise force a boat rent plus
  a week's sail, turning a long-coast or off-zone approach into a two-tile
  walk for one cheap charge; without it the mover would overprice such an
  approach and could defer as unreachable an objective a human has reached
  directly. No site has stocked bridge charges: the mover has spent only
  charges already held (chest rewards being the source in practice), and the
  junk-eviction pass (AP-112) has been free to discard idle bridge charges to
  free book room.
- **AP-182.** NO THROUGH-CAVE ROUTING. The A* kernel has carried a telecave
  pair-hop edge (`REQ-354`: index-paired `INTERACT_TELECAVE` tiles, 0↔1 /
  2↔3 within a zone; `autoplay/exec_move.c nav5_run` / `telecave_pair`), but
  the edge has been reachable only when the cave tile has itself been a goal: foot
  transit has treated telecave tiles as non-transparent interactives, so the
  mover has never routed THROUGH a cave, and a route a human would take by
  hopping has been priced at its walking cost. This has been deliberate, and
  measured: any real day saving has shifted the committed calendar enough to
  reorder the cheapest-first tour (§5), even with the planner's ordering
  quotes left untouched, so cheap hop-reachable work has scheduled ahead of the
  villain contracts that have funded the whole economy and the run has starved. On 30
  reference seeds, pricing hops throughout has cleared 27, restricting them
  to the drive 28 (22 of them faster), and the mover without hops all 30.
  Telecaves have remained non-objectives (they have not been added to the
  `PlanStepSet`, AP-040); traversal, where it has occurred, has recorded as
  ordinary `REC_MOVE` steps.

## 11. Termination bounds (R-A)

- **AP-100.** Within a line of play the module has imposed no *behavioral*
  limit: no bound has ever been the reason an otherwise-legal step has gone
  unattempted along the line being played. Three bound classes have been
  ratified as compatible (R-A):
- **AP-101.** **Engine bounds.** Any loop bounded by a quantity the engine
  itself enforces -- the remaining calendar (`days_left`), the week length, a
  shortfall divided by the engine's own income -- has not been a self-imposed
  restriction. Waiting, restock accumulation (AP-137), and gold-waits (§16)
  have been bounded this way and only this way.
- **AP-102.** **Runaway watchdogs.** A guard sized far beyond any reachable
  behavior -- the movement leg budget `NAV_MAX_LEGS = 32`
  (`autoplay/exec.h`), the executor round budget `EXEC_MAX_ROUNDS = 12`
  (`autoplay/primitives.h`), the raise-cast ceiling `RAISE_K_MAX`
  (`autoplay/exec.h`) -- whose only purpose has been to convert an
  infinite-loop bug into a clean defer, has been permitted, and each has been
  documented at its definition. Hitting one has been a defect to fix, never a
  scheduling signal.
- **AP-103.** **Once-per-call recovery levers.** A recovery inside one
  movement pass (the re-rent, the gate-out (§10)) has been limited to
  firing once per `move_to_once` invocation. Because the planner has re-invoked
  movement each round, the lever has re-armed every attempt; the REACH
  restock retry (AP-189) has run one further `move_to_once`, so a lever can
  fire at most twice per outer `move_to` call. The once-guard has bounded
  churn within a pass, not the search across calls (R-A.2; the pattern has been
  documented at the levers' declarations).
- **AP-104.** **Declared search bounds.** The third class, and the only one
  that CAN leave a legal plan unattempted: the snapshot-tree search's branching
  cap, frontier beam, stagnation cut, line-local cycle rules, and dead-leaf
  drop (AP-203). They have been permitted because they have been declared rather
  than hidden: each has been named at its definition site with the
  measurement that motivated it, each has been counted and reported on the
  `[SEARCH]` summary line, and their effect on the verdict has been stated
  exactly (AP-016): they can shrink the set a NOT-SOLVED claim covers, and
  they can never turn a losing line into a SOLVED one. A bound of this class
  has had to satisfy all three: declared, counted, and verdict-safe.

## 12. The gate-charge law (R-C)

- **AP-110.** A gate cast has never spent the last held gate charge -- that
  charge has been the escape from whatever the landing turns out to be --
  except at the town that sells the gate spell, where a landing has refilled by
  definition (ratified R-C). The law's floor `GATE_LAW_MIN_CHARGES = 2`
  (`autoplay/exec.h`) has been derived from the law, not tuned: a cast has
  been legal only from a supply of two (or one, to the seller).
- **AP-111.** The law has been enforced at the cast (`autoplay/exec_loc.c
  exec_gate_to`: the floor test, plus the seller refill: a landing at the
  gate seller's town that would leave the book below the floor has stepped into
  the town, `REQ-322`, and rebought to the floor while gold has allowed) and at
  transit's castability test (`autoplay/exec_move.c mv_gate_castable`). The
  seller-exemption's refill has been enforced, not assumed. The crossing leg
  (`do_crossing`) has kept town gates in play at ONE held charge -- a cast to
  the seller's own town has been legal from one and has landed on a refill -- while
  castle gates have stayed behind the two-charge pre-filter (no castle
  destination has been a seller).
- **AP-112.** The shared spell-charge capacity (`max_spells`, summed across
  the spellbook, `REQ-321`: the cap has counted TOTAL CHARGES, so every charge of
  one transport has displaced a charge of the other) has been allocated among
  transport, leadership, and combat spells by comparison of outcomes (§14),
  subject only to the R-C floor. Held gate kits have topped up to the floor
  and no further (`exec_topup_gate_kit`, before every attempt); stocking past
  the
  floor has occurred only at the measured sites: the cycle-start bulk tiers
  (AP-055), the siege approach pre-stock (AP-082/AP-186), and the
  floor+2 restock recoveries (AP-189).
- **AP-186.** Gate-charge stocking around approaches has been of two kinds,
  each documented at its site:
  - **Proactive, floor-targeted:** the per-attempt kit top-up to the law
    floor (`exec_topup_gate_kit`, AP-051/AP-112) and the siege approach
    pre-stock (AP-082): before a rich off-zone castle approach with a gate
    destination in the fight's zone, a transport book below the floor has stocked
    to floor+2 so the approach has ridden day-free gates and the pre-cast lift
    has survived the crossing.
  - **Reactive recoveries:** the floor+2 restocks that have fired only after a
    crossing or committing move has actually FAILED for want of castable
    charges (AP-189).
  Both have targeted the law floor or a fixed small margin above it; no site
  has hoarded charges beyond its own approach's need.
- **AP-189.** THE GATE-RESTOCK RECOVERIES (`autoplay/exec_move.c`): two
  otherwise-failing terminal paths have earned one forced restock + one
  retry each, both gated on magic learned and a deep wallet (the thresholds
  at their definition sites) and both using the room-forcing stocker (a full
  endgame book has refused the plain buy):
  - `do_crossing`: gates refused AND the sail failed (typically a pocket
    landing with no dock and books burned below the law floor), the recovery has restocked
    both gate books at their sellers, then retried the crossing once. The
    stocking trip itself can cross zones; a recursion guard
    (`s_stocking_gates`) has kept it from re-entering.
  - `move_to`: a committing move that failed on REACH with a gate book below
    the law floor, the recovery has restocked to floor+2 and re-run `move_to_once` once
    (queries have stayed pure; the retry has re-armed the once-per-pass levers, AP-103).
  A crossing or move that can still succeed has never taken these paths.
- **AP-190.** THE SELF-MAROON GUARD (`autoplay/exec_move.c
  gate_dest_foot_nodes`): a LAST-PAIR gate cast (one that leaves the book
  below the law floor) has flood-filled the destination zone's map on foot
  from the landing; a pocket landing (below the pocket node bound, AP-051's
  probe) with no castable gate left afterward has been a trap, not a
  crossing, and the destination has been skipped, unless the move's own
  final target has sat inside the pocket (arriving has been the point; the planner's
  maroon probe has judged the aftermath). Unknown geometry (a zone that has failed to
  load) has never blocked.

---

# Part IV, Recruiting

## 13. The recruiting seam and sources

- **AP-120.** The recruiting entry has been `autoplay/exec_recruit.c
  exec_recruit`, dispatched on a `RecruitRequest` (`autoplay/exec.h`): the
  combat mode, the target identity and garrison, and the astrology weeks
  before the fight (AP-183). Given a target and the world state, it has
  produced and carried out an acquisition plan -- troops, leadership lifts,
  and combat-spell charges -- whose fielded army the engine simulation
  has verified to win, or it has deferred with a truthful cause.
- **AP-121.** The module has treated as constraints only quantities the engine
  has enforced: gold, per-stack leadership caps, spell-charge capacity and counts,
  source stock, travel legality and cost, and weekly economics. It has imposed
  no categorical restriction of its own (§11).
- **AP-122.** Every engine-legal acquisition source has been considered,
  enumerated by `autoplay/exec_recruit.c recruit_sources_enumerate` into a
  `RecruitSource[]` whose **array order has been the buy preference**:
  1. `RSRC_GARRISON`: troops already banked in owned-castle garrisons,
     `unit_cost = 0` (already paid), enumerated first.
  2. `RSRC_INZONE_DWELLING`: dwellings in the hero's current zone (a walk).
  3. `RSRC_INSTANT_ARMY`: the class's conjured troops, castable anywhere
     (`zone = ""`), `avail` effectively unbounded, `unit_cost` the per-unit
     spell cost `instant_army.cost / ((spell_power+1) * multiplier[rank]) + 1`.
  4. `RSRC_HOME_CASTLE`: the home-castle pool (`dwelling == "castle"`
     troops), effectively unbounded, reached by travelling to the home gate.
  5. `RSRC_OFFZONE_DWELLING`: dwellings in another zone (a whole-map trip),
     never excluded.
  A `RecruitSource` has carried the source `kind`, `troop_idx`, `avail`,
  `unit_cost`, `zone_index`, `(x, y)`, and `src_id` (the owning castle for a
  garrison stack). Held troops have been counted directly.

## 14. Army search and the win predictor

- **AP-130.** The plan catalog has been built once by `autoplay/exec_recruit.c
  plan_build`: all troops sorted strongest-first, arranged into pure
  morale-group sets, mixed spreads, and ONE SINGLETON GROUP PER AVAILABLE
  TROOP (`PlanGroup`, up to `PLAN_MAX_GROUPS`): multi-slot groups have filled every
  slot to the control cap and priced the whole spread, so only a 1-troop group
  has ever built the lone troop (a pure shooter troop has taken no retaliation and
  has won sieges at a fraction of the spread's gold),
  with the strongest-first and reversed slot orderings both enumerated as
  first-class tuples, and the kit-spell axis enumerating every combat spell
  in the pack's catalog once magic is learned (AP-180). A high-morale army
  has been reachable only as a single morale group (§14 of
  `OPENBOUNTY-SPEC.md`).
- **AP-131.** The search (`exec_recruit.c search_all`) has been a SIM FUNNEL
  over one flat enumeration: every (group, ordering, kit spell, kit charge
  count, raise-cast count `k`) tuple has been BUILT -- pure arithmetic against
  per-troop market quotes (`TroopQuote`: the troop's sources as a
  cheapest-first (cost, stock, zone) list, plus the held count), no engine
  call -- and the built candidates have collapsed before any simulation. Sizing,
  gold, and days have all priced by FILLING that source list cheapest-first
  (`quote_fill`; the realize's buys have walked the source-preference order of
  AP-122, so a plan's priced gold can differ from its spend where an
  unbounded source has undercut a preferred one), so each source's stock and
  cost have bound together (a zero-cost garrison troop has subsidized exactly its
  banked count, never unlimited buys), and the R-B days key has been one week
  quantum per DISTINCT off-zone zone the plan's buys have actually toured
  (`build_candidate`'s zone mask): a plan gathered where the hero has stood
  has priced 0, so among winners the cheapest has been the easiest to GATHER. The
  funnel's stages have been:
  1. **Fold** (in-cell). A `k` step whose army hp-worth moved less than ~3%
     (`SIM_FOLD_DEN`) of the cell's last kept entry has folded into it, keeping
     the cheapest member. The byte-identical-army case has been provably lossless:
     leadership's only combat effect has been the per-stack control check
     `hp*count <= leadership` (`engine/combat.c`), and every candidate has been
     in-control at its own `k` by sizing.
  2. **Dominate.** Candidates have sorted by the R-B cost order (days, then gold,
     then enumeration order, a deterministic total order); a candidate no
     stronger (hp-worth) than one already kept at lower-or-equal cost with
     the same kit spell and at least as many charges has been dropped. Survivors
     have formed a cost/strength frontier per kit class.
  3. **Rung.** At most `SIM_RUNGS` (16) survivors have been marked for simulation:
     per days-class its cheapest, its strongest, and gold-even rungs between.
  4. **Sim + refine.** Rungs have simulated cheapest-first through the one oracle
     (`cand_sim` → `predict_combat_cached`); the first winning rung has been the
     R-B-cheapest winning rung, and a bisection of the frontier gap between
     it and the last losing rung has recovered the cheapest winner the frontier
     resolution can name. That winner has been the `Candidate` `realize_plan`
     has built, same object, no re-derivation.
  The funnel has made the search tractable on any pack: it has collapsed a
  candidate universe that has been astronomically large under direct simulation
  down to at most `SIM_RUNGS` simulations per search, without simulating
  tuples the pure-arithmetic pre-collapse has already proven dominated. The
  accepted trade: optimality has been over the folded frontier, not the raw
  universe; a winner can overpay by up to one fold step (~3%), and a win
  hiding between two losing rungs has fallen through to the typed probe causes
  like any other no-winner search. `--verbose` has logged the accounting: one
  `[SIM-STACK]` line per search (universe / kept / pareto / rungs) and one
  `[SIM-RUNG]` line per marked rung (index, days, gold, hp-worth, kit, k),
  beside the per-simulation `[RECRUIT-SIM]` lines.
- **AP-132.** The leadership lift has been modeled on the engine: each
  `raise_control` cast has added `GameRaiseControlAmount` (`spell_power *
  100`, no floor) to `leadership_current`, which has reset to `leadership_base`
  at each week boundary (`REQ-361`). `achievable_k` has bounded the
  achievable casts by held charges plus wallet-affordable charges, capped at
  `RAISE_K_MAX` (§11). The realize has applied the lift
  (`realize_plan_body`: same-zone plans pre-cast, off-zone-touring plans
  have CARRIED the charges instead since every crossing's week boundary would burn
  a pre-cast, cycling buy → cast → buy when the book cannot hold the whole
  stock at once), and `exec_rearm_raise_for` has re-armed it for a later
  fight.
- **AP-133.** The win has been verified against the army exactly as it has been
  fielded -- slot order, morale interactions, and control effects, per
  engine rules: the candidate's slot order has been the fielded combat-row order
  (`cand_fielded_army`). The predictor has simulated `(mode, target
  garrison grown by the plan's weeks, army override, what-if
  leadership/spellbook)` on an RNG-snapshot/restored throwaway copy;
  `predict_combat_cached` has memoized it keyed by mode, target identity,
  garrison, army, leadership, spellbook, grow-weeks, seed, `knows_magic`,
  and `spell_power`. The key has omitted the calendar day, which the
  grown-garrison arithmetic has read, so two grow-weeks sims at different weeks
  can share a verdict. The module has never delivered the hero to a fight
  the simulation has not won.
- **AP-134.** When no engine-legal plan within the search has won,
  `rfw_probe_cause` has run relaxation probes in widening order: relax gold;
  then gold + stock (sources lifted to `max_population`); then
  gold + leadership (`RAISE_K_MAX`), each through the SAME funnel in probe
  mode (existence, not cost: rungs have simulated strongest-first and stopped at the
  first win), naming the FIRST relaxation that has produced a winner as the
  binding `ExecCause` (gold / stock / leadership, else no-winning-army), so
  the reported cause has been a fact about the game, not the search.
- **AP-135.** PLAN DAY-COSTS HAVE BEEN PRICED INSIDE THE FUNNEL, as part of
  the R-B sort key (AP-131), and nowhere else. The recruiter has exposed no
  standalone day-cost estimate: nothing outside the army search has consumed one,
  and a second estimator could disagree with the key the search has actually
  ranked by.
- **AP-136.** Combat-spell (kit) allocation has been two first-class axes of
  the same flat enumeration, the kit spell (none / any combat spell from
  AP-130's catalog, gated on LIVE `knows_magic`) and the charge count (a
  fixed geometric ladder, `charge_steps` at its definition in `search_all`;
  book room has bound at realize/verify time, not at enumeration), priced into
  each candidate's gold as the charge shortfall at pack prices and judged by
  the same oracle as every other dimension; there has been no separate
  ladder, and an already-winning spell-less candidate has simply been the
  cheaper rung. The combat-mechanic facts that have made the sustain choice
  necessary (an all-IMMUNE garrison shrugging off damage spells but not
  resurrect) have fallen out of the simulation, never out of per-spell
  heuristics.
- **AP-137.** No separate accumulation estimate has existed: a plan whose
  sources cannot fill a slot in one trip has accumulated across restock weeks
  inside `realize_plan` itself (bank held progress, spend the week, retry),
  in the wait-allowed pass only, each wait ended by a shortfall the oracle
  has already won with, a restock week that has bought nothing (waiting proven
  useless → STOCK), or the calendar (`exec_spend_week` failing → TIME). There
  has been no calendar look-ahead: the loop's backstop has been a fixed count
  (`RECRUIT_RESTOCK_MAX_ROUNDS`, `autoplay/exec.h`), calendar-free so the day
  budget has never changed play. Troops banked in owned-castle garrisons have
  paid no upkeep, never fallen out of control, and merged by troop id, so
  repeated buy legs plus restock weeks have accumulated armies no single trip could
  field.
- **AP-180.** The kit search has enumerated every combat spell in the pack's
  catalog once the hero `knows_magic` (`REQ-389`) as a first-class kit axis:
  in the reference pack, `clone`
  (friendly multiplier), `teleport`, `fireball`, `lightning`, `freeze`
  (control), `resurrect` (sustain), and `turn_undead` (vs UNDEAD only) have
  all been reachable candidates. Selection has stayed BY-OUTCOME: each spell
  has been one kit tuple built and priced through the single funnel (AP-131) and
  judged by the single oracle (AP-133 / AP-136), so a spell has been chosen only
  when it has yielded the R-B-cheapest winning rung: no per-spell heuristics, no
  reserved slots. The IMMUNE/UNDEAD fizzle rules (`REQ-386`, `REQ-388`) and
  the sustain-possession policy (AP-084) have fallen out of the sim
  unchanged. Winning play against an oversized garrison has been spell
  attrition, not a bigger army: `clone` has multiplied an elite troop,
  `resurrect` has out-lasted a mega-garrison's damage, `freeze` has removed a decisive
  enemy troop for a round, and `turn_undead` / `lightning` have landed damage where
  `fireball` has been wasted.
- **AP-183.** The win predictor and the projection have accounted for the
  target's astrology growth (`REQ-371`: non-player castle and hostile-foe
  stacks whose `troop_id` has matched the weekly creature have grown by
  `growth_per_week`) across any calendar the plan spends before the fight. A
  projected winner has been verified against the garrison as it has stood on
  the day the fight has actually occurred, after the plan's restock / gold weeks
  (AP-137), not only against today's garrison. The predictor's
  `grow_garrison` (`autoplay/exec_fight.c`) has mirrored the engine's own
  week-boundary garrison-growth arithmetic on the engine's
  `GamePickAstrologyCreature` schedule (`REQ-370`), growing matching stacks
  by `growth_per_week` per modelled week; it has deliberately modelled
  garrison growth only (not dwelling repopulation or the peasant-week army
  conversion, which the live weeks have applied for real). Without the fight-day
  check, a funded (AP-053) or restock (AP-137) wait would hand the defender a
  larger garrison than the one the plan has proved it could beat.

## 15. Commit

- **AP-138.** THE REALIZE-FAILURE MEMORY (`autoplay/exec_recruit.c`
  `s_excl` / `excl_add` / `excl_has`): a chosen winner whose realize has starved
  of STOCK has proved the paper quotes wrong for that (target, troop): the
  shortfall-sim has rejected the partial fill and every purchasable source has run
  dry mid-plan. Re-picking the identical plan next cycle would be a
  deterministic loop, so the starved troop has been remembered per target and
  plan groups relying on it skipped for that target. This has been recruiter
  memory, not world state: it has DELIBERATELY survived the attempt rollback
  (that has been its purpose) and has reset per run (`recruit_exclusions_reset`).
  When the exclusions exhaust a target's winner set, they have been cleared
  (`excl_clear`) and the full universe searched once: the memory has been
  recorded at the wallet of a past failure, and a richer attempt has deserved the
  full universe (a permanently-poisoned last villain has been a lost game).
- **AP-140.** `autoplay/exec_recruit.c exec_recruit` has been the recruiting
  core. It has searched (`search_all`, AP-131); with no winner and the
  exclusions exhausted it has cleared them and searched once more (AP-138);
  with no winner under the armed wait flag it has run the funded wait
  (AP-162); with still no winner it has run `rfw_probe_cause` and deferred
  with the typed cause (AP-134). A winner has been realized in place
  (`realize_plan`); a realize that starved of STOCK has recorded the
  exclusion and re-searched at the spent-down wallet (up to the retry bound
  at its definition), picking the next-cheapest winner that CAN be bought;
  the waste has been recorded, and the enclosing attempt rollback (AP-030)
  has restored it with everything else. There has been no per-winner snapshot
  inside the recruiter; the attempt has been the unit of atomicity.
- **AP-141.** The realize (`realize_plan_body`) has run, in order: banked or
  dismissed displaced troops (`bank_or_dismiss`, preservation before
  dismissal); set the approach reserve (AP-156); stocked the candidate's kit
  charges FIRST (`stock_combat_spells`, a shortfall has NOT been fatal by itself:
  the live verify has simmed the book as stocked, so a short kit has either still won
  or failed the verify and rolled back); applied the lift (AP-132: pre-cast
  for same-zone plans, carried as charges for off-zone tours); bought each
  slot across every source of its troop in source-preference order
  (`plan_buy_troop`, whose watchdog has scaled with the ask since bulk plans
  legitimately sweep dozens of small dwellings, and which, at a home-pool
  source, has re-lifted with held raise charges and clamped to the LIVE cap on
  arrival: travel can cross a week boundary that resets the lift, and the
  engine has REFUSED an over-cap buy rather than clamping it; a slot short of
  its count has accepted any shortfall the oracle still wins with, else spent
  restock weeks, AP-137); re-topped the raise stock for the at-gate re-lift
  capped at what the book can hold; and checked at-gate deliverability: a
  lift the book cannot hold and the spent-down wallet cannot re-buy has failed
  the realize as STOCK through the exclusion memory (AP-138) instead of
  committing a paper winner that would lose its live verify. Charge stocking
  itself (`stock_spell_charges`) has forced book room at the cap (the
  engine has REFUSED a buy at `max_spells`, it has not clamped) with harmless
  cast-offs (raise, instant army, a live time-stop window), the cheapest
  combat charge not being stocked (field-discard), and adventure junk
  (AP-112), so a full book has never masqueraded as a gold failure.
- **AP-156.** THE APPROACH RESERVE (`autoplay/exec_recruit.c
  s_realize_reserve`): a realize for an off-zone castle fight carrying a
  lift, or one whose buy tour has left the zone (the approach has then crossed
  BACK; either way a week boundary has threatened the lift), has reserved, out
  of every purchase except the raise spell itself (the lift has been the reserve's
  beneficiary), the approach's working capital: two castle-gate charges plus
  the full `k × raise_cost` at-gate re-arm, since a realize that has spent the
  whole wallet has stranded a committed winner at a sail crossing. The reserve has
  stayed readable after the realize (`exec_last_realize_reserve`) until the
  next `exec_recruit` entry: the siege approach has keyed its transport pre-stock
  (AP-186) on whether the plan banked approach money.
- **AP-142.** The plan has considered plans that combine multiple sources and
  plans that accumulate troops across deliberate calendar weeks using
  engine-provided storage, in addition to single-trip purchases: the
  multi-source buy has been `plan_buy_troop`'s across-sources top-up; the
  accumulation path AP-137's in-place restock loop.

## 16. Economy and solvency

- **AP-160.** The module has accounted for weekly economics using the engine's
  own arithmetic and has not knowingly committed the hero to an insolvency the
  plan itself has not resolved. Upkeep, commission, and boat rental have been
  priced by the engine's week-end ordering (`REQ-361`, `GameWeeklyNetGold`);
  every wait has been sized by dividing the shortfall by that weekly net
  (AP-161).
- **AP-161.** When an attempt has been blocked on money -- the boat fare, a
  buy -- `autoplay/exec_recruit.c exec_ensure_gold` has played for it:
  shaped the wait world (`exec_prepare_gold_wait`: trimmed the army toward
  its single cheapest troop, garrisoning at an owned castle before
  dismissing; swapped even that troop for one unit from the cheapest-upkeep
  in-zone dwelling when its own upkeep has still eaten the commission, buying
  before dismissing so the army has never emptied; and LAST, returned the boat
  rental, but only when the weekly net has still been non-positive, since a boat has been
  weekly insurance but also the only ride home from a dockless shore, and a
  wait that has funded in a week or two has never repaid stranding the buys'
  crossing),
  and, when the weekly net has been positive, waited exactly the weeks the
  engine's own income has needed to cover the shortfall (an engine bound per
  R-A), all under the enclosing snapshot so a still-failing objective has rolled
  the weeks back. When the net has been non-positive, waiting could not help
  and the attempt has deferred with the honest cause. THE FUNDED-WAIT ORDER
  LAW: the budget bisect has run AFTER the wait-world prep, because a held
  troop has been quote credit, and dismissing it after the bisect would invalidate
  the budget the wait has been sized against. A mid-move money need (`nav_rent_boat`'s
  fare) has used the no-trim variant (`exec_ensure_gold_no_trim`): a
  committed fight army has never been dismissed to afford a boat fare, so it has
  waited on the STANDING army's income and a bleeding army has failed fast.
- **AP-162.** THE FUNDED-WAIT MECHANICS (`exec_recruit`, wait-allowed pass
  only, AP-053): after the wait-world prep, an existence probe at an
  unbounded wallet has proved any winner has existed at all; a bisection has then
  found the smallest budget that has bought one; the funded target has added a
  fixed working-capital margin (the divisor at its definition site;
  charge-trip ratchets, transport refills, and the lift's own consumption have been
  overhead the candidate price has not carried). THE GROWTH-AWARE WAIT: the
  bisect has priced TODAY's garrison, but the wait's own weeks have grown it (AP-183),
  so the need has been re-probed at the garrison as it has stood when the
  wallet has arrived, and a RECEDING target (growth has added more budget than the
  whole wait has delivered) has been refused outright so cheaper candidates have run
  first. Once funded (`exec_ensure_gold`), the search has re-run at the
  bisect's own proven budget (widening the wallet would re-space the rung
  sampler off the proven winner; the surplus has stayed for the realize's working
  capital), falling back to the full wallet and finally to the existence
  probe's own winner rather than deferring a fight the wait has funded.
- **AP-163.** THE STOCK WAIT, the funded wait's twin for troop supply,
  riding the same armed wait-allowed pass (AP-053) at the same site in
  `exec_recruit` (after the funded wait declines): a fight that has failed even
  at an unbounded wallet has not been money-bound; when the restock-ceiling probe
  (`stock_relaxed_wins`, dwelling stocks lifted to `max_population`) has won,
  the binding constraint has been supply the engine's own week boundary
  has refilled. Under the armed pass (after the funded-wait block's world prep)
  the wait has spent restock weeks, re-searching the REAL world each week
  (restock and income have accrued together), and stopped when a winner has appeared, a
  week has added no new dwelling stock anywhere (`dwelling_stock_total`: every
  source at its ceiling), the weekly-grown target has receded past even the
  restock ceiling, or the engine's own TIME failure has ended it. A run that has cleared without it has
  never fired it.

---

# Part V, Measurement

## 17. Diagnostics

- **AP-170.** All module diagnostics have been permanent, CLI-gated, and
  behaviour-inert, never temporary instrumentation. Every
  channel has read one process flag, set once from the `--verbose`
  command-line flag (`src/main.c` → `autoplay/diag.c ob_diag_set_verbose`,
  before any autoplay path has run) and read through `ob_diag_verbose`; the
  diagnostic has been emitted only when the flag has been set, and no game state
  has been read that the emission could alter. `--verbose` has turned on
  **all** channels at once; there has been no per-channel selection, and no
  autoplay behavior has read environment variables (the same rule DM-030
  has recorded for demo mode; the engine's save-path resolution has been the one
  environment consumer in the game's own code, outside autoplay).
- **AP-174.** DEFAULT OUTPUT HAS BEEN A SUMMARY, not a trace. A run without
  `--verbose` has printed only: the `[AUTOPLAY] begin` line; a `[SEARCH]` progress
  heartbeat every 1024 expansions; one compact `[SEARCH] timing` line; the one
  `[SEARCH] done` summary carrying every counter (expansions, best-done,
  frontier, evictions, restarts, and the per-result tallies); on a miss the
  `[UNMET]` roll-up (AP-175); and the one `[VERDICT READY]` line. Everything
  else has been a `--verbose` channel. The COUNTERS have always been
  maintained -- they have been cheap and they have been how performance questions have been
  answered -- so only the PRINTING has been gated.
- **AP-171.** The channels, all gated by `--verbose` and each emitting its own
  distinct tag, have been:
  - the search trail (`autoplay/search.c`), `[SEARCH]`: one `node` line per
    expansion (node sequence, depth in days, progress, days left, which branch,
    what it is about to try), one `order` line per priced node showing the head
    of the candidate ordering (where the promotion tiers have become visible), the
    per-restart stagnation line, the full per-stage timing distributions, and
    the recruit-memo hit rate.
  - the planner trail (`autoplay/planner.c`), `[PLANNER]`: one line per
    attempt (label, ok/done, typed cause, day, gold), the maroon probe with
    its neighborhood dump, the logistics state (book room / stop charges),
    the scarce-winner swap, and the sacrifice escape (AP-056).
  - the recruiter trace (`autoplay/exec_recruit.c`), `[RECRUIT]`
    project / commit / funded-wait lines, plus one `unrealizable drop` line per
    army search (the tally of over-budget troops and the cheapest one
    dropped; a line per DROPPED CANDIDATE has measured 3.3M of 3.3M verbose lines
    on one seed, burying every other channel), and the funnel accounting: one `[SIM-STACK]` line per search
    (universe / kept / pareto / rungs), `[SIM-RUNG]` per marked rung, and
    `[RECRUIT-SIM]` per simulation (target, what-if leadership, growth,
    verdict, surviving enemy HP).
  - the mover trace (`autoplay/exec_move.c`), `[NAV]`: boat rents (and
    refusals), gate teleports, time-stop casts, unreachable-target dumps
    with the hero's neighborhood, and the per-crossing provenance line
    feeding the `[LEDGER]` CROSSING tally with its zone pairs.
  - the calendar/economy ledger (`autoplay/exec_ledger.c`), the `[LEDGER]`
    day accounting (AP-172).
  - the stranding audit (`autoplay/primitives.c`), `[STRAND]`, one line per
    stranding pre-gate skip (AP-051).
  - the engagement contact trace (`autoplay/primitives.c
    engage_reached_foe`), one `[SLAY-CONTACT]` line per engagement attempt: hero tile / travel
    mode / mount, the foe's live tile, and the pending flow before and after
    (AP-173 posture, it has read only state the engagement has already read; this
    channel has measured the AP-086 co-location trap directly).
  - the prerequisite dump (`autoplay/prereq.c prereq_dump`), `[PREREQ]`,
    once per open; and the muster, realize and gold-wait traces, `[MUSTER]`
    (`autoplay/primitives.c`), `[REALIZE]` and `[ENSURE]`
    (`autoplay/exec_recruit.c`).
- **AP-172.** The ledger has been behaviour-inert because its counters have lived
  in file-statics outside `Game`, so world fingerprints and saves have stayed
  untouched.
  `day_acct_add` has fed a gross tally (kept across rollbacks) and a committed
  tally, tagged `DAY_ACCT_{OTHER,APPROACH,RECRUIT,CONTRACT,GOLDWAIT,
  CROSSING,RECRUITMOVE,STOCKMOVE}`. A `LedgerSnap` embedded in the
  `WorldSnapshot` (`ledger_snap` / `ledger_unsnap`) has rolled the tallies
  back with a rolled-back attempt (§4).
  `ledger_report` has emitted the `[LEDGER]` block once, from the committed
  node's report at search end, and only under `--verbose`.
- **AP-175.** THE `[UNMET]` REPORT has named, for each objective the run has
  not completed, the truthful typed cause its OWN LAST attempt has produced, never
  a guess and never a generic failure. A miss can leave hundreds of
  objectives open, so the two forms have differed by volume, not by honesty:
  under `--verbose` one line per unmet objective; by default a roll-up BY
  CAUSE, most common first, each line carrying the tally and one
  representative objective, so the report has been bounded by the number of
  distinct causes. The scepter held back by
  the finale rule (AP-052) has reported `finale-gated:N-objectives-unmet`
  rather than the bare "cause=none" that state would otherwise print.
- **AP-173.** A behaviour-inert diagnostic has been allowed to read the very
  simulation the module has already run, e.g. the enemy-survivor scalars
  (`CombatTurnRecord.ai_stacks` / `ai_hp`) a candidate fight has already
  produced, because such a read has added no engine call and changed no state. A
  gate-unset run has stayed bit-identical to the same run gated.

---

# Part VI, Non-goals

## 18. Deliberately not modelled

- **AP-185.** The following engine features have deliberately not been
  modelled; no requirement has covered them and none has been a defect:
  - the **Find Villain** spell and the town **Gather Information** action
    (`REQ-322`, `REQ-291`): the planner has read world state directly, so it
    has needed neither intel source;
  - the **King's audience / rank-up report** (`REQ-305`): under
    `oracle_mode` rank-up has fired automatically on villain capture
    (`REQ-208`), so the audience has been UI-only for autoplay;
  - **the difficulty-selection SCREEN** (`REQ-210`): autoplay has taken its
    difficulty from `--autoplay-level` and its class from `--autoplay-hero`
    (AP-015), so it has never driven the interactive character-creation
    flow;
  - **signposts** (`REQ-354`): informational text with no bearing on the
    verdict.
