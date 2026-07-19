# Autoplay, Specification

Autoplay is the headless automated player and the pack-winnability oracle:
given a seed it has driven an entire OpenBounty game to a terminal verdict, every objective cleared, or a partial result whose every miss carries a
truthful cause. It shares the one engine and the one world model with the
visible shell (§36 of `OPENBOUNTY-SPEC.md`), so a seed played by autoplay and
by a human is the same world. Where demo mode plays under a player's
constraints, autoplay proves: it reads the world directly rather than through
the fog, and it may snapshot and roll back to explore many lines of play before
committing one. (Both modes simulate a fight before entering it; that is not
the distinction, the timeline is. See `DEMO-SPEC.md` DM-010 / DM-013.) Its
verdict is a statement about the pack and the seed, not about luck.

**Conventions.**
- Each item carries a stable identifier `AP-NNN`. This document owns the `AP-`
  namespace; `OPENBOUNTY-SPEC.md` owns `REQ-NNN`, `DEMO-SPEC.md` owns `DM-NNN`.
- Items are written in the present-perfect factual tense ("the planner *has
  selected* X").
- Code citations name a **file and function** (e.g. `autoplay/planner.c
  planner`), not a line number, so they survive edits. Autoplay code lives
  under `autoplay/`; it calls the engine (`engine/`) but never the shell
  (`src/`). The one shell adapter that knows autoplay is `src/shell_autoplay.c`
  (dependency arrow shell → autoplay → engine), mirroring `src/shell_demo.c`.
- No magic numbers: where a bound, price, or cap appears, the item names the
  constant and its definition site, or the `game.json` / engine source it is
  derived from at runtime.
- "The module" has referred to the recruiting subsystem
  (`autoplay/exec_recruit.c`), the substantive core of this spec. "The system"
  has referred to autoplay as a whole.
- Three standing rulings govern the design, **R-A** (termination bounds),
  **R-B** (plan-cost comparison), **R-C** (the gate-charge law), folded into
  the sections they govern (§11, §8, §12) and cited by those names.

**Out of scope.** Rendering, audio, input, and the visible shell's own flows
are `OPENBOUNTY-SPEC.md`'s subject; the human-like player is `DEMO-SPEC.md`'s.
This document covers the autoplay planner, executor, mover, recruiter, and
their measurement layer.

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
8. [Plan cost, the lexicographic comparison (R-B)](#8-plan-cost--the-lexicographic-comparison-r-b)

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
  `exec_replay.c`) → **engine** (`engine/`). The search is the only component
  that chooses which line of play to extend; the planner core is the only one
  that orders candidates and snapshots state; `execute_why` / `move_to` are
  pure downward calls (`autoplay/exec.h`).
- **AP-002.** The executor has been a FLAT set of functions, split across
  translation units only for readability, of exactly two kinds
  (`autoplay/exec.h`): a **primitive** (one per `PrimKind`, orchestration only,
  never touching the engine directly) and a **helper** (the only code that
  touches engine/game state). Three helpers, `move_to`, `exec_fight`,
  `exec_recruit`, wrap real algorithms and own private static internals in
  their own translation unit.
- **AP-003.** Every engine-changing action a helper has performed has been
  emitted to a write-only recording sink (`autoplay/recording.h`), so that
  headless == visible == replay, byte-for-byte (§3).

## 2. Entry, boot, and verdict

- **AP-010.** A run has begun in `src/main.c` (the `--autoplay --headless`
  path, dispatched before any window is created), which built an
  `AutoplayConfig`, `seed_index` (the `--seed N` catalog world `0`–`255`, else
  `AUTOPLAY_DEFAULT_SEED_INDEX = 1`, `autoplay/autoplay.h`) and the pack dir, and called `autoplay/autoplay.c autoplay_run`. The oracle has been ONE
  mechanism: a single snapshot-tree search (`autoplay/search.c search_run`, §6)
  run once from the boot state. Greedy play is not a separate stage: it is the
  tree's FIRST DESCENT, taken by following the step core's own candidate
  order, and the keystone promotions are root-adjacent branches the search
  reaches by backtracking.
  The verdict has been BINARY and mapped to the process exit code: `0` =
  SOLVED, `1` = NOT-SOLVED, `2` = setup failure.
- **AP-011.** `autoplay/autoplay.c` boot has opened the pack, loaded the
  read-only `Resources` from `game.json`, allocated `Game`/`Map`/`Fog`, called
  `GameInitSeeded` with `cfg->seed_index` and the autoplay hero profile
  (`AUTOPLAY_HERO_NAME`; the class and difficulty defaults
  `AUTOPLAY_HERO_CLASS = "knight"` and
  `AUTOPLAY_HERO_DIFFICULTY = DIFFICULTY_NORMAL`, `autoplay/autoplay.h`, each
  overridable per run by `--autoplay-hero` / `--autoplay-level`, AP-015), and
  loaded the home zone, the engine-only consumer pattern, the same boot shape
  demo mode uses. `GameInitSeeded` expands the index into `game->seed`
  (`REQ-181a`), so the boot never pre-sets the seed itself. The same constants
  feed the visible mode, so "same seed" has meant "same world".
- **AP-012.** `autoplay_run` has called `autoplay/search.c search_run` exactly
  once; that single call has driven the whole game to its verdict. The entry
  file has held no loop of its own. It has then copied the progress counts out,
  derived `days_used` from the pack's start calendar, scored the committed
  world with `GameComputeScore`, and counted `REC_MOVE` primitives for the turn
  tally.
- **AP-013.** `autoplay_run` has emitted the ONE authoritative terminal line
  `[VERDICT READY] <best_done>/<obj_total> completed; verdict=<SOLVED|NOT-SOLVED>`.
  The denominator has been runtime data, the enumerated objective universe for
  the pack and seed under play (§5), never a literal. `best_done` has been the
  most objectives any node reached, universe-normalized (AP-205); it equals
  `obj_total` iff SOLVED and is the "how close" figure on NOT-SOLVED.
- **AP-014.** `--validate-pack [LO [HI]]` (default `0..255`, the whole
  catalog; a range outside `0..255` or with `LO > HI` is rejected) has run the
  headless oracle for each catalog world inline and sequentially, and rendered
  a terminal table (`src/main.c validate_pack_run`) built for PACK AUTHORS: an
  ephemeral status line while a seed resolves, then one finalized row per seed
  carrying the verdict, objectives cleared, days, score, moves, and elapsed
  time, and, on a NOT-SOLVED row, the first objective the oracle could not
  clear and its typed cause (`autoplay_unmet_label` / `autoplay_unmet_cause`).
  A closing totals row reports `PASS`/`FAIL`, the solved ratio, the per-seed
  average days / score / time, and the total wall clock. The oracle's own
  `[AUTOPLAY]` / `[SEARCH]` / `[VERDICT]` output is silenced for the sweep
  (`ob_diag_set_quiet`) so only the table shows. Exit `0` iff every seed in
  the range solved, `1` otherwise, and `2` on a setup failure, which, being
  seed-independent (an unreadable pack, an unknown hero class), aborts the
  whole sweep rather than repeating for every seed.
  Per-run state has been reset at each boot: the recording sink, the day
  ledger (`ledger_reset`), the recruiter's realize-failure memory
  (`recruit_exclusions_reset`), and the pending-flow / adventure-spell UI
  globals. Two statics have deliberately persisted across runs: the
  combat-prediction memo (separated per run by the seed folded into its key,
  AP-133) and the recruiter's last realize reserve (reset at each
  `exec_recruit` entry, AP-156).
- **AP-015.** THE DAY BUDGET HAS COME ENTIRELY FROM THE CHOSEN DIFFICULTY, the pack's `time.days_per_difficulty` knob, selected by
  `--autoplay-level=<easy|normal|hard|impossible>` (default `normal`) and
  applied through `GameInitSeeded` (`autoplay/autoplay.c autoplay_run`). There
  is no bespoke calendar override, and deliberately so: every real difficulty
  is a whole multiple of `time.week_days`, so the weekly economy, commission,
  astrology, growth, restock, stays phase-locked and the run plays the pack's
  own calendar for that level. An arbitrary day count would desynchronize the
  weekly world, because `passed = start_days - days_left` drives week ids,
  astrology, contract windows, and the growth predictor alike.
  `--autoplay-hero=<class>` selects the class by pack class id (default
  `knight`, `AUTOPLAY_HERO_CLASS`); a class the pack does not define is a hard
  error that fails the run rather than silently substituting a default.
- **AP-016.** The two verdicts have meant precisely this:
  - **SOLVED**: a full clear was reached AND COMMITTED. Some node's
    enumeration showed every objective done, and that node's world and its
    root-to-node recording are what the run leaves live. It is a positive
    result about the seed: this line of play exists and was simulated.
  - **NOT-SOLVED**: the search exhausted its DECLARED EXPANSION SET without
    reaching such a node. That set is bounded by the per-node branching cap,
    the frontier beam, the stagnation cut, and the line-local cycle rules
    (AP-203); the most-advanced node is committed instead, and its planner
    memory supplies the truthful `[UNMET]` causes. It has deliberately NOT
    been a proof of unwinnability, and there has been no separate "unwinnable"
    exit code: every one of those bounds can only weaken a NOT-SOLVED claim,
    never a SOLVED one.
- **AP-017.** A verdict has never been reported over a recording that cannot
  be replayed. When the sink dropped a push at capacity (`recsink_truncated`),
  `autoplay_run` has failed the run outright, setup-failure semantics, exit
  `2`, rather than print a verdict the replay could not reproduce.

## 3. Determinism, recording, and replay

- **AP-020.** The world has been byte-deterministic: given the same seed and
  the same recorded actions, the engine has reproduced the identical world
  (floating point is used only for animation and rendering, never gameplay, `REQ-185`). Combat has been re-resolved at replay time by the same pure
  function of (seed, encounter identity, mode) (`REQ-395`), so a proven
  outcome recurs exactly.
- **AP-021.** Every engine-changing action has been recorded into a `RecSink`
  (`autoplay/recording.h`) as one `RecPrim`: `REC_MOVE` (one `GameStep`),
  `REC_ANSWER` (a pending-flow answer carrying the combat outcome), or
  `REC_ACTION` (a direct engine mutation, tagged by `RecActionKind`, garrison, ungarrison, dismiss, dismiss-last, buy-troop, buy-spell,
  buy-siege, rent-boat, cancel-boat, take-contract, cast-adventure-spell,
  gate-town, gate-castle, travel-zone, search, spend-week, mount-fly, land,
  and discard-spell). Push helpers: `autoplay/recording.c` `rec_push_move` /
  `rec_push_action` / `rec_push_answer`.
- **AP-022.** Each `RecPrim` has carried a world-fingerprint stamp computed by
  the one function `autoplay/recording.c rec_world_fp` (FNV-1a over
  zone/position/day/gold/leadership/travel-mode/contract/army/spellbook) and
  applied inside the one push helper. Recorder and replay have used this single
  function, so they cannot disagree on "same state".
- **AP-023.** Replay has been driven by the one dumb applier
  `autoplay/plan.c plan_exec_step` (one `RecPrim` per call), the sole replay
  path, consumed by the visible mode (AP-024): `REC_MOVE`→`GameStep`,
  `REC_ANSWER`→(combat re-run for combat-bearing flows via
  `autoplay/exec_replay.c autoplay_apply_recorded_combat`)→`player_io_answer`,
  `REC_ACTION`→`autoplay/exec_replay.c autoplay_apply_rec_action`. A
  per-primitive fingerprint mismatch has been a HARD FAILURE: the applier has
  reported `[REPLAY-DIVERGE]` with both fingerprints and the prim kind, then
  aborted. Continuing past a mismatch would act on a world the recording does
  not describe, so the run stops at the first diverging primitive rather than
  silently playing on.
- **AP-024.** `--autoplay` without `--headless` has been the visible mode: the
  run is resolved headlessly first (the same single `search_run` call on a
  separate world), then `src/shell_autoplay.c` has replayed the recording on
  the live shell world through `plan_exec_step` at a watchable pace, with
  fights animated from their `CombatTurnRecord` by the shared combat animator
  (`src/combat_replay.c`) before the identical resolution is applied. On
  completion the shell hands the cleared board to the human (the same win
  hand-off demo mode performs).
- **AP-025.** The stamp has been of the PRE-action state, so the recorder MUST
  push BEFORE it mutates the engine, replay checks the fingerprint as a
  precondition, and a stamp taken after the mutation describes a world the
  replay has not reached yet. Where an action's id is only knowable after the
  mutation (`RA_TAKE_CONTRACT`, whose id is `GameTakeNextContract`'s return),
  the caller has captured `rec_world_fp(g)` first and pushed through
  `rec_push_action_fp`, the one sanctioned way to stamp out of band, never a
  post-mutation fingerprint.
- **AP-026.** A lost fight is a temp death, and replay has MIRRORED the live
  side's response to it: the engine teleports the hero home, and the live run
  then ran `exec_temp_death` (zone map reload plus the bookkeeping). When
  `player_io_answer` reports `pres.temp_death`, the applier has run the same
  `exec_temp_death`. Without the mirror the replayed hero walks the new zone's
  coordinates on the OLD zone's map, an invisible desync that surfaces only
  when a terrain difference changes a day-spend (measured on seed 15: one day
  of drift at a desert tile the stale map called grass).

## 4. Snapshot and rollback

- **AP-030.** An attempt has been atomic. Before executing anything,
  `autoplay/planner.c planner_attempt` has recorded a recording mark
  (`recsink_mark`) and a full world snapshot (`autoplay/worldsnap.c
  worldsnap_capture`). On any failure, a prerequisite, the objective, or a
  mid-attempt recruit, it has restored the snapshot and rolled the recording
  back to the mark. The whole chain has committed together or not at all.
- **AP-031.** `worldsnap_capture` has value-copied `Game`, `Map`, `Fog`, the
  world RNG (`GameRngSnapshot`), and the diagnostic ledger (`ledger_snap`)
  into a `WorldSnapshot` (`autoplay/worldsnap.h`). These are flat structs with
  no owned heap (Game's only pointer is the shared read-only `res`; map tiles
  are inline), so the copy has been complete. `worldsnap_restore` has reversed
  it bit-identically, restored the RNG, unsnapped the ledger, and reset the
  `pending_*` flow globals (a capture has always been taken at `FLOW_NONE`).
- **AP-032.** Because a failed attempt has restored the world bit-identically,
  memoized projections have stayed valid across failures, and identical world
  states have yielded identical decisions. There has been no runtime per-step
  assertion; divergence detection lives in replay (§3).
- **AP-033.** Planning has left no residue: the adventure-spell UI
  continuations (`bridge_state`, `gate_state`, process globals outside
  `Game`, which `worldsnap_restore` cannot revert) have been reset via
  `spells_adventure_reset_ui` after any planning pass that probes gate or
  bridge casts, so a probe never leaves a live continuation armed.

---

# Part II, Planning

## 5. Objectives and the plan-step set

- **AP-040.** The planner has enumerated one `PlanStepSet` (`autoplay/goals.h`,
  `PlanStep steps[STEP_MAX]` + `count`) covering every objective in the world:
  chests, artifacts, navmaps, orbs, and the alcove read from every zone's
  loaded map, monster and villain castles, hostile wandering foes, and the
  buried scepter added last (`autoplay/goals.c plansteps_enumerate` and its
  per-kind helpers). `obj_total` has equalled `set->count`.
- **AP-041.** A `PlanStep` (`autoplay/goals.h`) has held: `kind` (a `PlanKind`, `STEP_CHEST`, `STEP_ARTIFACT`, `STEP_NAVMAP`, `STEP_ORB`, `STEP_ALCOVE`,
  `STEP_SIEGE_WEAPONS`, `STEP_MONSTER_CASTLE`, `STEP_VILLAIN`, `STEP_FOE`,
  `STEP_SCEPTER`), a target point, `zone_index`, a stable `handle`
  (placement / castle / villain id), and a display `label`.
  `STEP_SIEGE_WEAPONS` exists only as a prerequisite candidate (§7), never as
  an enumerated objective.
- **AP-042.** Completion has been a single uniform per-kind predicate
  `autoplay/goals.c planstep_is_done`: consumables → tile in `g->consumed[]`;
  alcove → `knows_magic`; siege weapons → `siege_weapons`; monster castle →
  player-owned; villain → `villains_caught[]`; scepter → `stats.won`; foe →
  gone by `placement_id`+zone.

## 6. The planner step core and the snapshot-tree search

- **AP-050.** The planner has been a RE-ENTRANT STEP CORE (`autoplay/planner.h`),
  not a loop: `planner_open` enumerates the objective universe and initializes
  run state, `planner_candidates` runs one cycle head (logistics, mover
  pricing, ordering, the promotion tiers), `planner_step` performs ONE atomic
  attempt, `planner_refresh_done` re-reads the done predicates, `planner_done`
  is the goal test, and `planner_report` prints the truthful end-of-line
  causes. The search (AP-200) drives these one decision at a time; there is no
  full-game planner loop.
- **AP-057.** `planner_candidates` has ordered the open objectives
  cheapest-first by the mover's query-mode quote (§10) so in-zone work
  schedules ahead of crossings, cross-zone ties breaking by zone so one sail is
  followed by that zone's whole slate. Three promotion tiers then refine that
  order: the first-villain keystone (AP-055), the scarce-winner conflict tier
  (AP-054), and the alcove keystone (AP-081). The finale gate (AP-052) is
  applied here, the scepter is offered only when every other objective is
  done.
- **AP-058.** All planner memory for one line of play has ridden in a
  `PlannerRun` value (`autoplay/planner.h`), never in statics, so a search node
  can carry it: per-objective `done` / `defer_why` / `defer_cause` /
  `ever_failed` / `stuck_cycles`, the armed wait pass, the escape budget, and
  the identity-keyed failure history. That history is keyed by a hash of
  (kind, zone, x, y, handle), NOT by enumeration index: an open on a progressed
  world re-enumerates a SHRUNK universe, dead foes and consumed tiles drop
  out, so indices shift between opens while the history must follow the
  objective. `stuck_cycles` is CALENDAR-FREE (it counts opens, not days), so
  the day budget never shifts a promotion decision.
- **AP-051.** Each attempt (`planner_attempt`) has been do-or-fail under a
  world snapshot: the stranding pre-gate, the gate-kit re-stock, the
  objective's unmet hard gates (siege weapons, the villain's contract is
  handled inside the siege primitive, AP-061), then the objective itself
  through `execute_why`.
  On failure the snapshot and the recording tail roll back in full, with two
  exceptions: an attempt that ACCOMPLISHED its goal predicate admits unless
  the hero ended MAROONED, and a PREFOUGHT siege keeps the world without
  admitting (a contract-less win resets the lord's garrison, real progress a
  restore would erase; the engine records it in `villains_prefought[]`).
  The STRANDING rules in full (`exec_step_strands` +
  `move_reachable_nodes_avoid` / `move_escape_jaw`):
  - **Pre-gate.** A stand-on objective (consumable / dig) whose end tile has
    no exit is skipped; a SINGLE-MOUTH pocket whose mouth an unbeatable
    hostile camps within follow range is a JAW TRAP and is skipped this
    cycle (`[STRAND]`).
  - **Post-success probe.** After a success, unbeatable hostiles near the
    hero count as WALLS; when the reachable set collapses (below the pocket
    node bound `AUTOPLAY_POCKET_NODES`, `autoplay/exec.h`) the
    hero is inside a closing jaw. Two second chances run before judging:
    fight the jaw open from INSIDE (the sealer is an objective, and the
    pocket often holds its own funding, a win commits both), else walk out
    (`move_escape_jaw`). Only a failed escape is a maroon: the success rolls
    back, the nearest sealer is recorded as the objective's BLOCKER, and the
    defer carries the jaw fight's truthful cause (gold / stock when the
    break-out was money-bound).
- **AP-052.** One selection ruling has shaped the ordering: the **finale gate**: a `STEP_SCEPTER` candidate is attempted only when every other objective is
  done. The King's-quest-last rule, no exceptions.
- **AP-053.** THE NO-BURN LAW: play never spends calendar merely waiting.
  Every wait site, `exec_ensure_gold` (money: boat fares, the alcove fee)
  and the plan-realization restock loop (army stock, AP-137), has failed
  immediately with a typed defer unless the **wait-allowed flag** was armed
  (`autoplay/exec.h exec_set_wait_allowed` / `exec_wait_allowed`). The law is
  applied at NODE granularity: a node whose candidates all failed to produce
  a child (deferring PROVEN useless from that state) is not immediately
  abandoned. It gets ONE re-listing with the flag armed (each wait bounded by
  its own need), carried in `PlannerRun.wait_armed` and disarmed on the FIRST
  success, because the world then changed and deferring works again. A line
  still progressing on its own therefore never waits, and all calendar
  burning is back-stacked to the point where that line proved stuck. THE FUNDED WAIT rides the same pass: a fight whose
  search fails gold-bound never reaches a wait site (there is no plan to
  realize), so under the armed flag `exec_recruit` bisects the smallest
  budget that buys any winner (the same funnel in existence mode), verifies
  the weekly NET income can deliver it within the calendar with one week of
  slack, plays for it (`exec_ensure_gold`), and searches again on the richer
  world, all rolled back with the attempt if the fight still cannot be won
  (mechanics in AP-162). The funded wait acts only on the engine's own
  arithmetic: it commits calendar to a shortfall exactly when the weekly net
  (`GameWeeklyNetGold`) can cover that shortfall within the remaining days
  with a week of slack, and refuses a need that sits past the income
  ceiling, so the wait is bounded by what the pack's economy can actually
  deliver, never speculative. The principle behind
  back-stacking all waiting to the fixpoint: a fight strangled only by
  calendar (an endgame lord an in-line attempt could not fund) becomes
  winnable when the whole run's accumulated income is made available at the
  proven end, at the cost of admitting more calendar spend, a trade the law
  takes only after deferring has been proven useless, so it never lengthens a
  run that was clearing on its own.
- **AP-055.** KEYSTONE PROMOTION AND CYCLE LOGISTICS, two mechanisms that
  run at each cycle start, before the attempt loop:
  - the ALCOVE KEYSTONE PROMOTION (`planner_candidates`): the alcove unlocks the whole
    spell economy (AP-081), so an alcove still unmet with the wallet already
    covering `economy.alcove_cost`, whose FIRST failure lies more than one
    `KEYSTONE_STUCK_CYCLES` opens in the past (`autoplay/exec.h`), is promoted
    to the FRONT of the order. The bound counts OPENS, not calendar days, so
    the day budget never shifts the decision. An alcove that resolves within its
    natural cheapest-first slot never promotes.
  - transport/stop LOGISTICS (`planner_logistics`, with magic learned and a
    solvent wallet): surplus raise / instant-army charges are cast off and
    idle combat charges field-discarded (the engine's own flow, recorded) to
    free shared book capacity (AP-112); the castle-gate book stocks in BULK
    TIERS once the book widens (the R-C floor always; larger targets at the
    widened-book thresholds documented at their definition site, every held
    charge above the floor is one zero-day crossing, the endgame's dominant
    sink); town gates top to the floor once the book affords both transports;
    and Time Stop charges fill the remaining room after a deep forcing pass
    (cast-offs plus cheapest-combat discards), each cast before a step banks
    `GameTimeStopStepsPerCast` free steps, the engine's own gold-to-calendar
    exchange (decisive where full-day terrain makes every unstopped step
    cost the day). Each stocking trip is atomic like any attempt.
- **AP-056.** THE SACRIFICE ESCAPE: when a node has produced no child even
  after its one wait-armed re-listing (AP-053), and the calendar still
  stands, one further expansion has taken the game's own escape
  hatch, a deliberate defeat whose temp death teleports the hero home for
  the price of the standing army. Two forms, tried in order: attacking an
  adjacent hostile foe (within the engine's follow gate), else, with no foe
  to lose to (a boatless, dockless pocket), dismissing the whole army
  through the engine's "sent back to King" confirm chain
  (`autoplay/exec_loc.c exec_dismiss_last_escape`, recorded as
  `RA_DISMISS_LAST`). The escape budget has been at most 4 per consecutive
  futile streak: any successful attempt re-arms it (the bound exists to stop
  futile escape loops, not to ration recoveries that keep buying progress, a maroon-prone geography can need one per island). Recorded like any
  fight and reported on the `[PLANNER]` channel; only when no such escape
  exists does the node exhaust and the search backtrack.
- **AP-054.** The cheapest-first ordering has been refined against one
  conflict: when two SIEGE candidates' projected winners draw on the same
  FINITE troop stock, the cheap one's shopping can consume the dwellings the
  expensive one's only live winner needs, leaving that candidate, winnable at
  the moment it was first projected, never projectable again once the shared
  stock is gone. A SCARCE-WINNER CONFLICT TIER has therefore refined the
  selection: before a non-held-win siege best is attempted, each other
  open (not-done) siege candidate with a live projected winner is probed with two
  pure queries (`autoplay/exec_recruit.c recruit_winner_finite_draw`, the
  R-B-cheapest winner's finite-source purchases, and
  `recruit_winner_survives_less`, the same projection with that draw deducted
  from the finite sources, optionally relaxed to the dwelling restock
  ceiling); when the other candidate's winner DIES under the best's draw while
  the best's own winner survives the reverse draw (live, or recoverable at the
  restock ceiling), the fragile winner is attempted first. Sieges only (foes
  never bind elite stock), one swap per cycle, projection-only probes
  (memoized sims, no state change), an ordering refinement inside the
  ratified selection shape, not a bound on any plan. Verbose channel: one
  `[PLANNER] scarce-winner swap` line per firing.

### The snapshot-tree search

- **AP-200.** THE SEARCH IS THE ORACLE (`autoplay/search.c search_run`), run
  once from the boot state. A NODE is a REACHED WORLD STATE, and it carries
  everything needed to resume from it: its world snapshot, its `PlannerRun`
  planner memory, the recording prims ITS OWN EDGE appended, its ordered
  candidate list with a cursor, and its lineage pointer to its parent. An EDGE
  is one attempt. Expanding a node means restoring it and attempting EXACTLY
  ONE objective through `planner_step`, never a playout, never a rollout to
  the end of the game. Nothing is ever simulated twice: every edge in the tree
  was simulated once, when it was created.
- **AP-201.** The frontier has been an AVL multiset (`autoplay/baltree.c`),
  chosen because it gives all three operations a memory-bounded frontier needs:
  pop-min (take the next node), insert (add children and re-insert a node with
  branches left), and pop-max (evict when over the cap). A heap gives the first
  two and not the third.
- **AP-202.** Ordering has been STRICT DEPTH-FIRST. The key is the negated
  creation sequence (`node_key` = `-seq`), so pop-min returns the NEWEST node (the current line's tip) and a re-inserted parent keeps its original, older
  key, so a child and its whole subtree drain before the parent's next branch.
  That is exact backtracking DFS expressed on the tree, and pop-max evicts the
  ROOT-MOST pending alternatives, which are the last resort. THE CURRENT LINE
  HAS ABSOLUTE PRIORITY; backtracking happens only on exhaustion. Two
  best-first keys were measured and abandoned first: `f = g + h` over elapsed
  days, and fewest-open-first. Both turned the frontier into a swamp of
  near-identical siblings, segments cost 0–2 days and successful reorderings
  collapse into states already seen, and the descent never committed (118k
  expansions at 14/280 done; 155k at 12/280). Determinism follows from the key:
  same build and seed give the same creation order, the same pops, the same
  plan.
- **AP-203.** The DECLARED EXPANSION SET, the bounds that make NOT-SOLVED a
  statement about a capped search rather than about the seed (AP-016). Each is
  a deliberate trade, and each can only weaken a NOT-SOLVED claim:
  - **Branching cap.** `SEARCH_MAX_CHILDREN` child states per node, after which
    its remaining alternatives are trimmed. Failures still fall through the
    WHOLE candidate list (that is the cheapest-first ordering doing its job) but successful branching is bounded. The ROOT is exempt, so its
    alternatives always remain available to a restart.
  - **Beam.** `SEARCH_MAX_LIVE_NODES` live nodes; over that, pop-max evicts the
    oldest pending alternatives, counted and reported, never silent.
  - **Stagnation cut.** A descent that has completed nothing for
    `SEARCH_STAGNATION_CUT` expansions is a doomed region: the frontier is
    pruned back to the root, whose next untried candidate opens a fresh
    descent under a different opening objective. Measured on
    seed 17, where low-calendar subtrees fail with `cause=time` about 170
    expansions per node and plain DFS drained them all before returning.
  - **Line-local cycle rules.** A child whose state signature equals any
    ANCESTOR on its own path is a true loop and is dropped, and consecutive
    KEPT edges (world kept, nothing completed, the PREFOUGHT shape) are capped
    at `SEARCH_MAX_KEPT_STREAK`, because an unbounded KEPT chain is a
    calendar-burning descent at constant progress (measured: 15k expansions
    pinned at 28/280, tip 385 days deep).
  - **Dead leaves.** A child born with the calendar exhausted and work still
    open can only fail every later attempt with `cause=time`, so it is not
    inserted. Declared trade: a finish that would need every remaining gate to
    cost zero days from exactly 0 days left is given up.
  - **Runaway watchdog.** `SEARCH_MAX_EXPANSIONS`, sized far above any real
    run (R-A.2); hitting it is a defect to fix, not a scheduling signal.
- **AP-209.** There has deliberately been NO GLOBAL duplicate-state set. A
  closed set without reopening is unsound here: a state first reached by a
  sibling line that later dies would block the main line from ever entering it
  (measured: the descent stalled at 28/280 behind exactly that). Cycle
  protection is line-local instead (AP-203).
- **AP-204.** A node has carried the FULL world (`WorldSnapshot`: Game + Map +
  Fog + RNG + ledger, about 1 MB), and memory is paid for with a smaller beam.
  The slim alternative (Game only, with the map rebuilt by a zone reload) was measured WRONG: a mid-game fresh stamp is not byte-equal to the boot map
  plus the line's incremental writes (foes stamped at moved positions,
  cleared-tile residue), and the recording cannot reproduce a map the line did
  not derive from prims (seed 15 diverged at prim 32 of its own replay).
  Lifetime is refcounted along the lineage, so discarding a branch frees it up
  to its first still-referenced ancestor, and candidate lists are priced
  LAZILY at a node's first pop, about 40% of created nodes are never popped
  and never pay the pricing pass.
- **AP-205.** Progress has been UNIVERSE-NORMALIZED. Each node's enumeration
  SHRINKS as objectives complete, so "done within this node's universe" is not
  comparable across nodes. The OPEN count is the same quantity in every view,
  so true progress is `root total - open`. The goal test is `open == 0`.
- **AP-206.** The recording of a node has been the concatenation of the edge
  deltas from the root down to it, rebuilt on every restore
  (`node_rebuild_recording`); the root's own delta is the whole pre-search
  prefix, so a rebuild reproduces the boot segment too. An edge's delta base is
  marked BEFORE the candidate pricing pass, because the cycle head runs
  LOGISTICS, which may record prims and move the world, those prims belong to
  the child's edge, or the rebuilt recording silently loses the stocking trips
  whose effects its own fingerprints assume (measured: all four gate seeds
  diverged mid-recording at exactly such an orphaned segment).
- **AP-207.** The outcome state has been COMMITTED, never re-simulated: on a
  hit the winning node, on a miss the most-advanced node. Its world and rebuilt
  recording become live and its planner memory supplies the `[UNMET]` causes,
  so the reported verdict, the committed world, and the replayable recording
  all describe the same line of play.
- **AP-208.** Because a restore jumps between DIFFERENT lines of play, the
  search has reset the process-global working state the planner's own same-play
  rollback deliberately keeps: the mover's nav memo (`nav_cache_invalidate`),
  the recruiter's realize-failure memory (`recruit_exclusions_reset`), and the
  calendar-death latch. It has additionally enabled the recruit-search memo
  (`recruit_cache_enable`) around its own expansions; the memo key is complete,
  so an enabled cache returns the same winner the search would have computed.

## 7. Prerequisites

- **AP-060.** `autoplay/prereq.c` has encoded only the engine-enforced hard
  gates. `prereq_unmet` has emitted, for a monster or villain siege with no
  siege weapons, one BUY-SIEGE candidate (`prereq_make_buy_siege`, resolved to
  the nearest town at execution). `planner_attempt` has executed each
  candidate under the same snapshot before the objective, so a failing gate
  has rolled the whole chain back.
- **AP-061.** The villain's active-contract requirement and the
  water-position / army-strength "soft" prerequisites have deliberately not
  been modeled as discrete steps; they have been handled inside
  `siege_or_slay` / `exec_ensure_contract` and by reachability + combat
  prediction respectively
  (`autoplay/prereq.h`). `exec_ensure_contract`'s take loop has tolerated
  EMPTIED cycle slots: once catches outnumber refills (`GameFulfillContract`
  advances `max_contract` whether or not a refill lands), a rotation step can
  return no contract while the target sits in the NEXT slot, the loop has
  kept taking through a full cycle instead of stopping at the first empty
  slot, exactly as a player re-presses the town's contract row.

## 8. Plan cost, the lexicographic comparison (R-B)

- **AP-070.** Where multiple winning plans have existed, the module has
  selected among them by total real cost, compared **lexicographically:
  calendar days first, gold second** (ratified R-B; `autoplay/exec_recruit.c
  build_candidate` / `cand_cmp`). Days are the resource the game ends on;
  gold is a means to an end and has only broken ties.

      cost(plan) = ( travel_days , gold_outlay )

  The gold_outlay term has been the plan's purchases at pack prices: the
  cheapest-first quote fill of every troop buy (`quote_fill`), the raise
  charge shortfall, doubled when an off-zone castle fight's approach must
  sail (the crossing's week boundary resets the pre-cast lift, so the full
  `k` charges are bought twice; a zero-day castle-gate approach into the
  fight's zone waives the premium, `fight_zone_gate_ready`), and, when the
  plan carries a spell kit (AP-136), the kit book's combat-charge shortfall.

- **AP-071.** `travel_days` has been one engine week quantum
  (`res->time.week_days`) per DISTINCT off-zone zone the plan's buys
  actually tour (`build_candidate`'s zone mask; garrison draws travel by
  gate and price zero), a plan gathered where the hero stands prices 0
  days, so among winners the cheapest is the easiest to GATHER. Every term
  has been derived at runtime from the pack.
- **AP-072.** There has been no gold-to-days exchange rate. A scalar rate
  under a small commission makes gold artificially dear in days and ranks
  calendar-monstrous world tours "cheaper" than local wallet spends;
  comparison has therefore stayed lexicographic (R-B), not scalarized.

---

# Part III, Execution

## 9. The executor primitives and typed causes

- **AP-080.** `autoplay/primitives.c execute_why` has been the executor entry
  the planner calls. It has dispatched a `PlanStep` by kind to its
  implementing helper, each running a bounded move/act loop
  (`EXEC_MAX_ROUNDS = 12`, `autoplay/primitives.h`, a runaway guard per R-A,
  §11), returning both a display `why` string and a machine-readable
  `ExecCause`.
- **AP-188.** `exec_fetch` has handled the two ways an arrival fails to fire
  the consumable. THE HOVER BOUNCE: interactive tiles fire on foot ENTRY
  only, and landing on one is illegal (`GameCanLandAt`), so a flight leg can
  end parked ON the goal with nothing fired, the fetch has stepped aside,
  landed, and walked back in. THE FLIGHT FALLBACK: a consumable
  foot-unreachable in the hero's own zone (a grass hollow inside a blocking
  ring has no foot entry) has been reached by fielding an all-flying army
  once per attempt (`fetch_field_fliers`: dismiss the non-fliers, recorded, recruiting one flier from an in-zone dwelling first when none is held) and
  retrying the approach; the mover's fly legs are the door.
- **AP-081.** There has been one primitive helper per objective family
  (`autoplay/primitives.c`): `exec_fetch` (walk onto a consumable, chest / artifact / navmap / orb), `exec_learn` (the alcove),
  `exec_town_buy_siege` (the siege-weapons prerequisite, AP-060),
  `siege_or_slay` (castles, villains, and wandering foes), and `exec_dig`
  (the scepter win). A primitive has been orchestration only; the engine
  touches have lived in the shared helpers (AP-002). The alcove lesson is a purchase (`economy.alcove_cost`; the
  engine's accept refuses without charging when the wallet is short), so
  `exec_learn` has deferred a broke arrival typed `EXEC_CAUSE_GOLD` before
  stepping onto the tile; `exec_ensure_gold` itself plays income weeks for the
  fee only under the wait-allowed pass (AP-053), because a run that ends
  without magic loses the whole sustain-kit candidate class, every objective
  whose only winner needs a combat spell stays unreachable. Deferring the
  alcove to fixpoint time, where the funded wait can cover the fee, therefore
  recovers that entire class of objectives instead of stranding the run short.
- **AP-082.** `siege_or_slay` (`autoplay/primitives.c`) has been
  simulate-first: it has predicted the held army (`exec_fight_winnable`);
  taken the contract for a villain (`exec_ensure_contract`); climbed the
  raise ladder (`exec_raise_k_for_win` → `exec_raise_for_fight`), falling
  through to the recruiter (`exec_recruit`) when no lift alone wins;
  PRE-STOCKED the castle-gate transport before a rich off-zone approach
  whose fight zone holds a gate destination (so the approach rides day-free
  gates and the pre-cast lift survives, AP-186); approached, re-arming the
  lift in the fight's zone when a crossing reset it; verified the fight
  LIVE at the gate (`exec_fight_winnable` on the standing army and book), a failed live verify declines the prompt and earns ONE second chance
  (re-arm the ladder the current leadership needs, else re-run the recruit
  once: later stocking can have evicted the plan's decisive kit charges), then fought (`exec_fight`). The same primitive has handled a wandering
  foe, engaging its live tile (AP-086).
- **AP-083.** The typed failure seam has been the `ExecCause` enum
  (`autoplay/primitives.h`): `NONE`, `OTHER`, `NO_WINNING_ARMY`, `STRANDED`
  (done-but-marooned → roll back), `TIME` (calendar exhausted, terminal),
  `PREFOUGHT` (keep-without-admit), and the binding-constraint causes `GOLD`,
  `STOCK`, `LEADERSHIP`, `REACH`. A no-winning-army verdict has named the
  actual binding constraint (§14), never a self-imposed limitation. The `why`
  string has been report-only.
- **AP-084.** The combat policy's SUSTAIN term (`autoplay/exec_fight.c`), the
  license for a melee stack to kite instead of closing, has counted magic as
  standing output only via the round's cast latch (`Combat.spells_this_round`),
  set exactly when a strictly-positive probed cast fired this round; the
  probes have run once, inside the round's cast attempt, and have never been
  repeated for the sustain test. Charge POSSESSION has not sustained: a held
  book whose every probe is zero (resurrect with nothing dead, freeze against
  an all-IMMUNE garrison) lets a melee stack kite an idle book forever,
  handing the engine's no-progress cutoff a false LOSS with the enemy
  untouched, a verdict the win-predictor would inherit, since the sim and the
  live fight share this policy.
- **AP-085.** The gold-chest answer (`autoplay/primitives.c
  answer_chest_choice`) has taken the PERMANENT leadership option
  (`leadership_base += gold/50`, ×2 with the Crown) while any villain has
  remained uncaught and the wallet already covered one week's outgoings
  (upkeep + boat fare, from the engine's own `GameArmyWeeklyUpkeep` /
  `GameBoatCost` arithmetic, never a hand-rolled burn formula); gold has been the answer
  only when cash-poor or with nothing left to control. Taking leadership only
  while some earlier milestone is unmet would pin `leadership_base` at its
  rank floor while gold accumulated unspendably, and a run could arrive at its
  terminal siege control-bound (the winning army over its leadership cap) with
  a wallet it had no way to convert into the leadership the fight needed;
  taking leadership from chests whenever control is still the scarce resource
  keeps the cap growing in step with the army the endgame requires.
- **AP-086.** A hostile foe CO-LOCATED with the hero (the foe followed onto
  the hero's tile) has been re-provoked, not re-fought: the follow-collision's
  flow is the only engagement a shared tile offers, and once it is consumed (a
  past decline while the army was weak) no step can re-fire the interactive
  from inside the tile, `exec_fight` without a pending flow is a no-op, the
  pursuit's arrival test is already satisfied, and the round budget spins to
  the watchdog. `autoplay/primitives.c engage_reached_foe` has therefore, on a
  co-located foe with `FLOW_NONE` pending, stepped OFF to an adjacent legal
  non-interactive tile (legality by the hero's LIVE travel state, the
  engine's own `adventure_walkable_*` predicate) and back ONTO the foe's live
  tile, so the return step fires `INTERACT_FOE` (or the step-off itself
  collides with the following foe); the fight then proceeds as any provoked
  fight. When no legal re-provoke step exists (walled in), the failure has
  been TYPED (`EXEC_CAUSE_REACH`, defer label `slay:contact-blocked`) so the
  planner defers instead of re-entering the spin. Without the re-provoke, a
  foe left co-located by one consumed collision flow is unreachable by any
  later attempt and turns every strong-army retry into a round-budget spin, the AP-102 watchdog defect class; with the re-provoke the same foe falls at
  the first attempt whose army can win it.
- **AP-184.** THE WEEK HAS BEEN THE ONLY CALENDAR QUANTUM. Every calendar
  spend goes through `exec_spend_week` (`autoplay/exec_loc.c`), which advances
  the exact day count to the NEXT week boundary, day-granular at boundaries
  by construction, under THE NO-BURN LAW (AP-053). There is no sub-week rest
  action, because no plan needs one: every wait the module takes is sized by
  the weekly economy (income, restock, growth), which only moves at those
  boundaries.

## 10. The single movement function

- **AP-090.** All transit (walk, boat, flight, gate teleport, bridge) has routed through the one function `autoplay/exec_move.c move_to`. Its
  signature has returned the arrival index of the cheapest reachable target
  (arrival = onto a passable target tile or adjacent to a bouncer), `-1` to
  defer, or in `commit=false` query mode the chosen index with the step-cost
  written out (`MV_XZONE_COST` + zone hops for a cross-zone target, so
  in-zone work is always scheduled ahead of a sail).
- **AP-091.** `move_to` has been: the already-satisfied check
  (`mv_satisfied`, reused at every give-up so a bounce-arrival is never
  mislabeled unreachable); one pricing relaxation over every target (query
  mode returns here); then, committing, a bounded leg loop, cross-zone
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
  charges held, and no flow pending, the mover has hopped by town gate to a VISITED
  town selling Time Stop, stepped in (gate landings sit beside the town,
  `REQ-322`), bought stop charges into the free book less the two slots the
  return refill needs, and gated back to the town-gate seller's own town, legal from one charge under the seller exemption (AP-110) and refilled to
  the floor on landing, converting gold into stop charges without spending
  a day or leaving the working zone. The loop has been available only where
  the town-gate seller shares the hero's zone (elsewhere the return leg would
  strand the march) and has been suppressed during realize trips
  (`exec_set_gate_restock_suppressed`: those carry exact candidate armies a
  mid-move teleport would scramble).
- **AP-093.** Boat state has been resolved in the leg loop: a hero not yet
  sailing has been put to sea by `nav_ensure_sailing`, renting through
  `nav_rent_boat` when no boardable boat is in the zone, and the A* itself
  has carried RENT-DOCK BOARDING EDGES (every same-zone town dock the wallet
  can rent at is a priced boarding point; the drive performs the town visit +
  rent when the path uses one), so water-locked in-zone targets price and
  drive without a separate crossing decision. Recovery levers, a
  stranded-boat re-rent, and a last-resort gate-out, have each fired at
  most once per `move_to` call (§11).
- **AP-094.** Hostile-foe tiles have been FIGHT-THROUGH edges, not walls
  (foes chase the hero and routinely seal choke points): the A* prices them
  with a penalty, the drive steps on and the responder fights when the live
  prediction wins; a DECLINED fight adds that foe's tile to the call's avoid
  set so re-paths route around the refusal instead of re-provoking it.
- **AP-181.** The mover has considered the Bridge adventure spell (`REQ-322`:
  up to 2 consecutive water tiles converted to walkable bridge grass, cost
  from the pack's spell catalog, one charge) as a candidate leg, priced like
  any other transit by its calendar-day and gold cost (R-B, §8) and adopted
  only when the bridged route is strictly cheaper than the walk / boat / gate
  alternatives. A bridge cast has been recorded as an `RA_CAST_ADV_SPELL`
  action (AP-021) so headless == visible == replay. Charge stocking has
  followed the plan's leg count with no reserved floor of its own; acquiring
  charges has been the executor's decision, never transit's (AP-092). Bridge
  shortcuts a water gap that would otherwise force a boat rent plus a week's
  sail, turning a long-coast or off-zone approach into a two-tile walk for one
  cheap charge; without it the mover overprices such an approach and can defer
  as unreachable an objective a human reaches directly. No site has stocked
  bridge charges: the mover spends only charges already held (chest rewards
  are the source in practice), and the junk-eviction pass (AP-112) may
  discard idle bridge charges to free book room, acquiring charges has
  remained the executor's decision, and no current plan makes it.
- **AP-182.** KNOWN GAP: THROUGH-CAVE ROUTING. The A* kernel has carried a
  telecave pair-hop edge (`REQ-354`: index-paired `INTERACT_TELECAVE` tiles,
  0↔1 / 2↔3 within a zone; `autoplay/exec_move.c nav5_run` /
  `telecave_pair`), but the edge has been reachable only when the cave tile is
  itself a goal, foot transit treats telecave tiles as non-transparent
  interactives, so the mover has never routed THROUGH a cave, and a route a
  human would take by hopping is priced at its walking cost.
  The hop mechanism itself is correct; what blocks it is a second-order
  effect on selection, and it is measured. Any real day saving shifts the
  committed calendar enough to reorder the cheapest-first tour (§5), even
  with the planner's ordering quotes left untouched: cheap hop-reachable work
  then schedules ahead of the villain contracts that fund the whole economy,
  and the run starves. Pricing hops throughout clears 27 of 30 reference
  seeds; restricting them to the drive clears 28 of 30, 22 of them faster.
  The shipped mover, which does neither, clears 30 of 30, so the gap costs
  nothing a verdict depends on, and closing it would cost seeds.
  Telecaves remain non-objectives (they are not added to the `PlanStepSet`,
  AP-040); traversal, where it does occur, records as ordinary `REC_MOVE`
  steps.

## 11. Termination bounds (R-A)

- **AP-100.** Within a line of play the module has imposed no *behavioral*
  limit, no bound has ever been the reason an otherwise-legal step went
  unattempted along the line being played. Three bound classes have been
  ratified as compatible (R-A):
- **AP-101.** **Engine bounds.** Any loop bounded by a quantity the engine
  itself enforces, the remaining calendar (`days_left`), the week length, a
  shortfall divided by the engine's own income, has not been a self-imposed
  restriction. Waiting, restock accumulation (AP-137), and gold-waits (§16)
  have been bounded this way and only this way.
- **AP-102.** **Runaway watchdogs.** A guard sized far beyond any reachable
  behavior, the movement leg budget `NAV_MAX_LEGS = 32`
  (`autoplay/exec.h`), the executor round budget `EXEC_MAX_ROUNDS = 12`
  (`autoplay/primitives.h`), the raise-cast ceiling `RAISE_K_MAX`
  (`autoplay/exec.h`), whose only purpose has been to convert an
  infinite-loop bug into a clean defer, has been permitted, and each has been
  documented at its definition. Hitting one has been a defect to fix, never a
  scheduling signal.
- **AP-103.** **Once-per-call recovery levers.** A recovery inside one
  movement pass (the re-rent, the gate-out (§10)) has been limited to
  firing once per `move_to_once` invocation. Because the planner re-invokes
  movement each round, the lever has re-armed every attempt; the REACH
  restock retry (AP-189) runs one further `move_to_once`, so a lever can
  fire at most twice per outer `move_to` call. The once-guard bounds churn
  within a pass, not the search across calls (R-A.2; the pattern is
  documented at the levers' declarations).
- **AP-104.** **Declared search bounds.** The third class, and the only one
  that CAN leave a legal plan unattempted: the snapshot-tree search's branching
  cap, frontier beam, stagnation cut, line-local cycle rules, and dead-leaf
  drop (AP-203). They are permitted because they are declared rather than
  hidden, each is named at its definition site with the measurement that
  motivated it, each is counted and reported on the `[SEARCH]` summary line,
  and their effect on the verdict is stated exactly (AP-016): they can shrink
  the set a NOT-SOLVED claim covers, and they can never turn a losing line
  into a SOLVED one. A bound of this class must satisfy all three, declared,
  counted, and verdict-safe.

## 12. The gate-charge law (R-C)

- **AP-110.** A gate cast has never spent the last held gate charge, that
  charge has been the escape from whatever the landing turned out to be, except at the town that sells the gate spell, where a landing refills by
  definition (ratified R-C). The law's floor `GATE_LAW_MIN_CHARGES = 2`
  (`autoplay/exec.h`) has been derived from the law, not tuned: a cast has
  been legal only from a supply of two (or one, to the seller).
- **AP-111.** The law has been enforced at the cast (`autoplay/exec_loc.c
  exec_gate_to`: the floor test, plus the seller refill, a landing at the
  gate seller's town that would leave the book below the floor steps into
  the town, `REQ-322`, and rebuys to the floor while gold allows) and at
  transit's castability test (`autoplay/exec_move.c mv_gate_castable`). The
  seller-exemption's refill has been enforced, not assumed. The crossing leg
  (`do_crossing`) has kept town gates in play at ONE held charge, a cast to
  the seller's own town is legal from one and lands on a refill, while
  castle gates have stayed behind the two-charge pre-filter (no castle
  destination is a seller).
- **AP-112.** The shared spell-charge capacity (`max_spells`, summed across
  the spellbook, `REQ-321`: the cap counts TOTAL CHARGES, so every charge of
  one transport displaces a charge of the other) has been allocated among
  transport, leadership, and combat spells by comparison of outcomes (§14), subject only to the R-C floor. Held gate kits top up to the floor and no
  further (`exec_topup_gate_kit`, before every attempt); stocking past the
  floor has occurred only at the measured sites: the cycle-start bulk tiers
  (AP-055), the siege approach pre-stock (AP-082/AP-186), and the
  floor+2 restock recoveries (AP-189).
- **AP-186.** Gate-charge stocking around approaches has been of two kinds,
  each documented at its site:
  - **Proactive, floor-targeted:** the per-attempt kit top-up to the law
    floor (`exec_topup_gate_kit`, AP-051/AP-112) and the siege approach
    pre-stock (AP-082), before a rich off-zone castle approach with a gate
    destination in the fight's zone, a transport book below the floor stocks
    to floor+2 so the approach rides day-free gates and the pre-cast lift
    survives the crossing.
  - **Reactive recoveries:** the floor+2 restocks that fire only after a
    crossing or committing move has actually FAILED for want of castable
    charges (AP-189).
  Both target the law floor or a fixed small margin above it; no site has
  hoarded charges beyond its own approach's need.
- **AP-189.** THE GATE-RESTOCK RECOVERIES (`autoplay/exec_move.c`): two
  previously-failing terminal paths have earned one forced restock + one
  retry each, both gated on magic learned and a deep wallet (the thresholds
  at their definition sites) and both using the room-forcing stocker (a full
  endgame book refuses the plain buy):
  - `do_crossing`: gates refused AND the sail failed (typically a pocket
    landing with no dock and books burned below the law floor), restock
    both gate books at their sellers, then retry the crossing once. The
    stocking trip itself may cross zones; a recursion guard
    (`s_stocking_gates`) keeps it from re-entering.
  - `move_to`: a committing move that failed on REACH with a gate book below
    the law floor, restock to floor+2 and re-run `move_to_once` once
    (queries stay pure; the retry re-arms the once-per-pass levers, AP-103).
  A crossing or move that could still succeed never takes these paths.
- **AP-190.** THE SELF-MAROON GUARD (`autoplay/exec_move.c
  gate_dest_foot_nodes`): a LAST-PAIR gate cast (one that leaves the book
  below the law floor) flood-fills the destination zone's map on foot from
  the landing; a pocket landing (below the pocket node bound, AP-051's
  probe) with no castable gate left afterward is a trap, not a crossing (the destination is skipped) unless the move's own final target sits
  inside the pocket (arriving IS the point; the planner's maroon probe
  judges the aftermath). Unknown geometry (a zone that fails to load) never
  blocks.

---

# Part IV, Recruiting

## 13. The recruiting seam and sources

- **AP-120.** The recruiting entry has been `autoplay/exec_recruit.c
  exec_recruit`, dispatched on a `RecruitRequest` (`autoplay/exec.h`): the
  combat mode, the target identity and garrison, and the astrology weeks
  before the fight (AP-183). Given a target and the world state, it has
  produced and carried out an acquisition plan, troops, leadership lifts,
  and combat-spell charges, whose fielded army the engine simulation
  verifies to win, or it has deferred with a truthful cause.
- **AP-121.** The module has treated as constraints only quantities the engine
  enforces: gold, per-stack leadership caps, spell-charge capacity and counts,
  source stock, travel legality and cost, and weekly economics. It has imposed
  no categorical restriction of its own (§11).
- **AP-122.** Every engine-legal acquisition source has been considered,
  enumerated by `autoplay/exec_recruit.c recruit_sources_enumerate` into a
  `RecruitSource[]` whose **array order has been the buy preference**:
  1. `RSRC_GARRISON`, troops already banked in owned-castle garrisons,
     `unit_cost = 0` (already paid), enumerated first.
  2. `RSRC_INZONE_DWELLING`, dwellings in the hero's current zone (a walk).
  3. `RSRC_INSTANT_ARMY`, the class's conjured troops, castable anywhere
     (`zone = ""`), `avail` effectively unbounded, `unit_cost` the per-unit
     spell cost `instant_army.cost / ((spell_power+1) * multiplier[rank]) + 1`.
  4. `RSRC_HOME_CASTLE`, the home-castle pool (`dwelling == "castle"`
     troops), effectively unbounded, reached by travelling to the home gate.
  5. `RSRC_OFFZONE_DWELLING`, dwellings in another zone (a whole-map trip),
     never excluded.
  A `RecruitSource` has carried the source `kind`, `troop_idx`, `avail`,
  `unit_cost`, `zone_index`, `(x, y)`, and `src_id` (the owning castle for a
  garrison stack). Held troops have been counted directly.

## 14. Army search and the win predictor

- **AP-130.** The plan catalog has been built once by `autoplay/exec_recruit.c
  plan_build`: all troops sorted strongest-first, arranged into pure
  morale-group sets, mixed spreads, and ONE SINGLETON GROUP PER AVAILABLE
  TROOP (`PlanGroup`, up to `PLAN_MAX_GROUPS`), multi-slot groups fill every
  slot to the control cap and price the whole spread, so only a 1-troop group
  ever builds the lone stack (a pure shooter stack takes no retaliation and
  has won sieges at a fraction of the spread's gold),
  with the strongest-first and reversed slot orderings both enumerated as
  first-class tuples, and the kit-spell axis enumerating every combat spell
  in the pack's catalog once magic is learned (AP-180). A high-morale army
  has been reachable only as a single morale group (§14 of
  `OPENBOUNTY-SPEC.md`).
- **AP-131.** The search (`exec_recruit.c search_all`) has been a SIM FUNNEL
  over one flat enumeration: every (group, ordering, kit spell, kit charge
  count, raise-cast count `k`) tuple has been BUILT, pure arithmetic against
  per-troop market quotes (`TroopQuote`: the troop's sources as a
  cheapest-first (cost, stock, zone) list, plus the held count), no engine
  call, and the built candidates collapsed before any simulation. Sizing,
  gold, and days have all priced by FILLING that source list cheapest-first
  (`quote_fill`; the realize's buys walk the source-preference order of
  AP-122, so a plan's priced gold can differ from its spend where an
  unbounded source undercuts a preferred one), so each source's stock and
  cost bind together, a zero-cost garrison stack subsidizes exactly its
  banked count, never unlimited buys, and the R-B days key has been one week
  quantum per DISTINCT off-zone zone the plan's buys actually tour
  (`build_candidate`'s zone mask): a plan gathered where the hero stands prices 0, so among
  winners the cheapest is the easiest to GATHER. The funnel stages:
  1. **Fold** (in-cell). A `k` step whose army hp-worth moved less than ~3%
     (`SIM_FOLD_DEN`) of the cell's last kept entry folds into it, keeping
     the cheapest member. The byte-identical-army case is provably lossless:
     leadership's only combat effect is the per-stack control check
     `hp*count <= leadership` (`engine/combat.c`), and every candidate is
     in-control at its own `k` by sizing.
  2. **Dominate.** Candidates sort by the R-B cost order (days, then gold,
     then enumeration order, a deterministic total order); a candidate no
     stronger (hp-worth) than one already kept at lower-or-equal cost with
     the same kit spell and at least as many charges is dropped. Survivors
     form a cost/strength frontier per kit class.
  3. **Rung.** At most `SIM_RUNGS` (16) survivors are marked for simulation:
     per days-class its cheapest, its strongest, and gold-even rungs between.
  4. **Sim + refine.** Rungs simulate cheapest-first through the one oracle
     (`cand_sim` → `predict_combat_cached`); the first winning rung is the
     R-B-cheapest winning rung, and a bisection of the frontier gap between
     it and the last losing rung recovers the cheapest winner the frontier
     resolution can name. That winner IS the `Candidate` `realize_plan`
     builds, same object, no re-derivation.
  The funnel's purpose is to make the search tractable on any pack: it
  collapses a candidate universe that is astronomically large under direct
  simulation down to at most `SIM_RUNGS` simulations per search, without
  simulating tuples the pure-arithmetic pre-collapse has already proven
  dominated. The accepted trade: optimality is over the folded frontier, not
  the raw universe, a winner can overpay by up to one fold step (~3%), and a
  win hiding between two losing rungs falls through to the typed probe causes
  like any other no-winner search. `--verbose` logs the accounting: one
  `[SIM-STACK]` line per search (universe / kept / pareto / rungs) and one
  `[SIM-RUNG]` line per marked rung (index, days, gold, hp-worth, kit, k),
  beside the per-simulation `[RECRUIT-SIM]` lines.
- **AP-132.** The leadership lift has been modeled on the engine: each
  `raise_control` cast has added `GameRaiseControlAmount` (`spell_power *
  100`, floor 100) to `leadership_current`, which resets to `leadership_base`
  at each week boundary (`REQ-361`). `achievable_k` has bounded the
  achievable casts by held charges plus wallet-affordable charges, capped at
  `RAISE_K_MAX` (§11). The realize has applied the lift
  (`realize_plan_body`: same-zone plans pre-cast, off-zone-touring plans
  CARRY the charges instead, every crossing's week boundary would burn a
  pre-cast, cycling buy → cast → buy when the book cannot hold the whole
  stock at once), and `exec_rearm_raise_for` has re-armed it for a later
  fight.
- **AP-133.** The win has been verified against the army as it will actually
  be fielded, slot order, morale interactions, and control effects, per
  engine rules: the candidate's slot order IS the fielded combat-row order
  (`cand_fielded_army`). The predictor has simulated `(mode, target
  garrison grown by the plan's weeks, army override, what-if
  leadership/spellbook)` on an RNG-snapshot/restored throwaway copy;
  `predict_combat_cached` has memoized it keyed by mode, target identity,
  garrison, army, leadership, spellbook, grow-weeks, seed, `knows_magic`,
  and `spell_power`. KNOWN LIMITATION: the key omits the calendar day,
  which the grown-garrison arithmetic reads, two grow-weeks sims at
  different weeks can share a verdict. The module has never delivered the
  hero to a fight the simulation does not win.
- **AP-134.** When no engine-legal plan within the search won,
  `rfw_probe_cause` has run relaxation probes in widening order, relax
  gold; then gold + stock (sources lifted to `max_population`); then
  gold + leadership (`RAISE_K_MAX`), each through the SAME funnel in probe
  mode (existence, not cost: rungs simulate strongest-first and stop at the
  first win), naming the FIRST relaxation that produces a winner as the
  binding `ExecCause` (gold / stock / leadership, else no-winning-army), so
  the reported cause has been a fact about the game, not the search.
- **AP-135.** PLAN DAY-COSTS HAVE BEEN PRICED INSIDE THE FUNNEL, as part of
  the R-B sort key (AP-131), and nowhere else. The recruiter exposes no
  standalone day-cost estimate: nothing outside the army search consumes one,
  and a second estimator could disagree with the key the search actually
  ranks by.
- **AP-136.** Combat-spell (kit) allocation has been two first-class axes of
  the same flat enumeration, the kit spell (none / any combat spell from
  AP-130's catalog, gated on LIVE `knows_magic`) and the charge count (a
  fixed geometric ladder, `charge_steps` at its definition in `search_all`;
  book room binds at realize/verify time, not at enumeration), priced into
  each candidate's gold as the charge shortfall at pack prices and judged by
  the same oracle as every other dimension; no separate ladder, and a
  currently-winning spell-less candidate is simply the cheaper rung. The combat-mechanic facts that make the sustain
  choice necessary (an all-IMMUNE garrison shrugging off damage spells but not
  resurrect) fall out of the simulation, never out of per-spell heuristics.
- **AP-137.** No separate accumulation estimate has existed: a plan whose
  sources cannot fill a slot in one trip accumulates across restock weeks
  inside `realize_plan` itself (bank held progress, spend the week, retry), wait-allowed pass only, each wait ended by a shortfall the oracle already
  wins with, a restock week that bought nothing (waiting is proven useless →
  STOCK), the calendar (`exec_spend_week` failing → TIME), or the FUTILITY
  PROJECTION: at the measured per-week fill rate, a slot that cannot
  complete within `days_left` defers STOCK, the wait is no longer
  engine-bounded progress. The loop's watchdog is itself engine-bounded
  (`days_left / week_days + 1`). Banked stacks
  in owned-castle garrisons pay no upkeep, never fall out of control, and
  merge by troop id, so repeated buy legs plus restock weeks accumulate armies
  no single trip could field.
- **AP-180.** The kit search has enumerated every combat spell in the pack's
  catalog once the hero `knows_magic` (`REQ-389`) as a first-class kit axis:
  in the reference pack, `clone`
  (friendly multiplier), `teleport`, `fireball`, `lightning`, `freeze`
  (control), `resurrect` (sustain), and `turn_undead` (vs UNDEAD only) have
  all been reachable candidates. Selection has stayed BY-OUTCOME: each spell
  is one kit tuple built and priced through the single funnel (AP-131) and
  judged by the single oracle (AP-133 / AP-136), so a spell is chosen only
  when it yields the R-B-cheapest winning rung, no per-spell heuristics, no
  reserved slots. The IMMUNE/UNDEAD fizzle rules (`REQ-386`, `REQ-388`) and
  the sustain-possession policy (AP-084) fall out of the sim unchanged.
  Winning play against an oversized garrison is spell attrition, not a bigger
  stack: `clone` multiplies an elite stack, `resurrect` out-lasts a
  mega-garrison's damage, `freeze` removes a decisive enemy stack for a
  round, and `turn_undead` / `lightning` land damage where `fireball` is
  wasted.
- **AP-183.** The win predictor and the projection have accounted for the
  target's astrology growth (`REQ-371`: non-player castle and hostile-foe
  stacks whose `troop_id` matches the weekly creature grow by
  `growth_per_week`) across any calendar the plan spends before the fight. A
  projected winner has been verified against the garrison as it will stand on
  the day the fight actually occurs, after the plan's restock / gold weeks
  (AP-137), not only against today's garrison. The predictor's
  `grow_garrison` (`autoplay/exec_fight.c`) mirrors the engine's own
  week-boundary garrison-growth arithmetic on the engine's
  `GamePickAstrologyCreature` schedule (`REQ-370`), growing matching stacks
  by `growth_per_week` per modelled week; it deliberately models garrison
  growth only (not dwelling repopulation or the peasant-week army
  conversion, which the live weeks apply for real). Without the fight-day
  check, a funded (AP-053) or restock (AP-137) wait can hand the defender a
  larger garrison than the one the plan proved it could beat.

## 15. Commit

- **AP-138.** THE REALIZE-FAILURE MEMORY (`autoplay/exec_recruit.c`
  `s_excl` / `excl_add` / `excl_has`): a chosen winner whose realize starved
  of STOCK proved the paper quotes wrong for that (target, troop), the
  shortfall-sim rejected the partial fill and every purchasable source ran
  dry mid-plan. Re-picking the identical plan next cycle would be a
  deterministic loop, so the starved troop is remembered per target and plan
  groups relying on it are skipped for that target. This is recruiter
  memory, not world state: it DELIBERATELY survives the attempt rollback
  (that is its purpose) and resets per run (`recruit_exclusions_reset`).
  When the exclusions exhaust a target's winner set, they are cleared
  (`excl_clear`) and the full universe searched once, the memory was
  recorded at the wallet of a past failure, and a richer attempt deserves
  the full universe (a permanently-poisoned last villain is a lost game).
- **AP-140.** `autoplay/exec_recruit.c exec_recruit` has been the recruiting
  core. It has searched (`search_all`, AP-131); with no winner and the
  exclusions exhausted it has cleared them and searched once more (AP-138);
  with no winner under the armed wait flag it has run the funded wait
  (AP-162); with still no winner it has run `rfw_probe_cause` and deferred
  with the typed cause (AP-134). A winner has been realized in place
  (`realize_plan`); a realize that starved of STOCK has recorded the
  exclusion and re-searched at the spent-down wallet (up to the retry bound
  at its definition), picking the next-cheapest winner that CAN be bought, the waste is recorded, and the enclosing attempt rollback (AP-030)
  restores it with everything else. There has been no per-winner snapshot
  inside the recruiter; the attempt is the unit of atomicity.
- **AP-141.** The realize (`realize_plan_body`) has run, in order: banked or
  dismissed displaced stacks (`bank_or_dismiss`, preservation before
  dismissal); set the approach reserve (AP-156); stocked the candidate's kit
  charges FIRST (`stock_combat_spells`, a shortfall is NOT fatal by itself:
  the live verify sims the book as stocked, so a short kit either still wins
  or fails the verify and rolls back); applied the lift (AP-132: pre-cast
  for same-zone plans, carried as charges for off-zone tours); bought each
  slot across every source of its troop in source-preference order
  (`plan_buy_troop`, whose watchdog scales with the ask, bulk plans
  legitimately sweep dozens of small dwellings, and which, at a home-pool
  source, re-lifts with held raise charges and clamps to the LIVE cap on
  arrival: travel can cross a week boundary that resets the lift, and the
  engine REFUSES an over-cap buy rather than clamping it; a slot short of
  its count accepts any shortfall the oracle still wins with, else spends
  restock weeks, AP-137); re-topped the raise stock for the at-gate re-lift
  capped at what the book can hold; and checked at-gate deliverability, a
  lift the book cannot hold and the spent-down wallet cannot re-buy fails
  the realize as STOCK through the exclusion memory (AP-138) instead of
  committing a paper winner that loses its live verify. Charge stocking
  itself (`stock_spell_charges`) has forced book room at the cap, the
  engine REFUSES a buy at `max_spells`, it does not clamp, with harmless
  cast-offs (raise, instant army, a live time-stop window), the cheapest
  combat charge not being stocked (field-discard), and adventure junk
  (AP-112), so a full book has never masqueraded as a gold failure.
- **AP-156.** THE APPROACH RESERVE (`autoplay/exec_recruit.c
  s_realize_reserve`): a realize for an off-zone castle fight carrying a
  lift, or one whose buy tour leaves the zone (the approach then crosses
  BACK; either way a week boundary threatens the lift), has reserved, out
  of every purchase except the raise spell itself (the lift is the reserve's
  beneficiary), the approach's working capital: two castle-gate charges plus
  the full `k × raise_cost` at-gate re-arm. A realize that spends the whole
  wallet strands a committed winner at a sail crossing. The reserve has
  stayed readable after the realize (`exec_last_realize_reserve`) until the
  next `exec_recruit` entry: the siege approach keys its transport pre-stock
  (AP-186) on whether the plan banked approach money.
- **AP-142.** The plan has considered plans that combine multiple sources and
  plans that accumulate troops across deliberate calendar weeks using
  engine-provided storage, in addition to single-trip purchases: the
  multi-source buy is `plan_buy_troop`'s across-sources top-up; the
  accumulation path is AP-137's in-place restock loop.

## 16. Economy and solvency

- **AP-160.** The module has accounted for weekly economics using the engine's
  own arithmetic and has not knowingly committed the hero to an insolvency the
  plan itself does not resolve. Upkeep, commission, and boat rental have been
  priced by the engine's week-end ordering (`REQ-361`, `GameWeeklyNetGold`);
  every wait is sized by dividing the shortfall by that weekly net (AP-161).
- **AP-161.** When an attempt has been blocked on money, the boat fare, a
  buy, `autoplay/exec_recruit.c exec_ensure_gold` has played for it: shaped
  the wait world (`exec_prepare_gold_wait`: trimmed the army toward its
  single cheapest stack, garrisoning at an owned castle before dismissing;
  swapped even that stack for one unit from the cheapest-upkeep in-zone
  dwelling when its own upkeep still ate the commission, buying before
  dismissing so the army never empties; and LAST, returned the boat rental,
  but only when the weekly net is still non-positive, a boat is weekly
  insurance but also the only ride home from a dockless shore, and a wait
  that funds in a week or two never repays stranding the buys' crossing),
  and, when the weekly net is positive, waited exactly the weeks the
  engine's own income needs to cover the shortfall (an engine bound per
  R-A), all under the enclosing snapshot so a still-failing objective rolls
  the weeks back. When the net has been non-positive, waiting could not help
  and the attempt has deferred with the honest cause. THE FUNDED-WAIT ORDER
  LAW: the budget bisect has run AFTER the wait-world prep, a held stack is
  quote credit, and dismissing it after the bisect invalidates the budget
  the wait was sized against. A mid-move money need (`nav_rent_boat`'s
  fare) has used the no-trim variant (`exec_ensure_gold_no_trim`): a
  committed fight army must never be dismissed to afford a boat fare, so it
  waits on the STANDING army's income and a bleeding army fails fast.
- **AP-162.** THE FUNDED-WAIT MECHANICS (`exec_recruit`, wait-allowed pass
  only, AP-053): after the wait-world prep, an existence probe at an
  unbounded wallet proves any winner exists at all; a bisection then finds
  the smallest budget that buys one; the funded target adds a fixed
  working-capital margin (the divisor at its definition site, charge-trip
  ratchets, transport refills, and the lift's own consumption are overhead
  the candidate price does not carry). THE GROWTH-AWARE WAIT: the bisect
  priced TODAY's garrison, but the wait's own weeks grow it (AP-183), the
  need is re-probed at the garrison as it will stand when the wallet
  arrives, and a RECEDING target (growth adds more budget than the whole
  wait delivers) is refused outright so cheaper candidates run first. Once
  funded (`exec_ensure_gold`), the search re-runs at the bisect's own proven
  budget (widening the wallet would re-space the rung sampler off the proven
  winner; the surplus stays for the realize's working capital), falling back
  to the full wallet and finally to the existence probe's own winner rather
  than deferring a fight the wait just funded.
- **AP-163.** THE STOCK WAIT, the funded wait's twin for troop supply,
  riding the same armed wait-allowed pass (AP-053) at the same site in
  `exec_recruit` (after the funded wait declines): a fight that fails even
  at an unbounded wallet is not money-bound; when the restock-ceiling probe
  (`stock_relaxed_wins`, dwelling stocks lifted to `max_population`) wins,
  the binding constraint is supply the engine's own week boundary refills.
  Under the armed pass (after the funded-wait block's world prep) the
  wait spends restock weeks, re-searching the REAL world each week (restock
  and income accrue together), and stops when a winner appears, the
  calendar runs short of a week of slack, or the weekly-grown target
  recedes past even the restock ceiling. Passing runs never fire it: the
  30-seed validation sweep is byte-identical with the wait present.

---

# Part V, Measurement

## 17. Diagnostics

- **AP-170.** All module diagnostics have been permanent, CLI-gated, and
  behaviour-inert, never temporary instrumentation added and reverted. Every
  channel has read one process flag, set once from the `--verbose`
  command-line flag (`src/main.c` → `autoplay/diag.c ob_diag_set_verbose`,
  before any autoplay path runs) and read through `ob_diag_verbose`; the
  diagnostic is emitted only when the flag is set, and no game state is read
  that the emission could alter. `--verbose` turns on **all** channels at
  once; there is no per-channel selection, and no autoplay behavior reads
  environment variables (the same rule DM-030 records for demo mode; the
  engine's save-path resolution is the one env consumer, outside autoplay).
- **AP-174.** DEFAULT OUTPUT HAS BEEN A SUMMARY, not a trace. A run without
  `--verbose` prints only: the `[AUTOPLAY] begin` line; a `[SEARCH]` progress
  heartbeat every 1024 expansions; one compact `[SEARCH] timing` line; the one
  `[SEARCH] done` summary carrying every counter (expansions, best-done,
  frontier, evictions, restarts, and the per-result tallies); on a miss the
  `[UNMET]` roll-up (AP-175); and the one `[VERDICT READY]` line. Everything
  else is a `--verbose` channel. The COUNTERS are always maintained, they are
  cheap and they are how performance questions get answered, so only the
  PRINTING is gated.
- **AP-171.** The channels, all gated by `--verbose` and each emitting its own
  distinct tag, have been:
  - the search trail (`autoplay/search.c`), `[SEARCH]`: one `node` line per
    expansion (node sequence, depth in days, progress, days left, which branch,
    what it is about to try), one `order` line per priced node showing the head
    of the candidate ordering (where the promotion tiers become visible), the
    per-restart stagnation line, the full per-stage timing distributions, and
    the recruit-memo hit rate.
  - the planner trail (`autoplay/planner.c`), `[PLANNER]`: one line per
    attempt (label, ok/done, typed cause, day, gold), the maroon probe with
    its neighborhood dump, the logistics state (book room / stop charges),
    the scarce-winner swap, and the sacrifice escape (AP-056).
  - the recruiter trace (`autoplay/exec_recruit.c`), `[RECRUIT]`
    project / commit / funded-wait lines, plus one `unrealizable drop` line per
    army search (the tally of over-budget stacks and the cheapest one dropped, a line per DROPPED CANDIDATE was measured at 3.3M of 3.3M verbose lines on
    one seed, which buried every other channel), and the
    funnel accounting: one `[SIM-STACK]` line per search
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
  - the engagement contact trace (`autoplay/primitives.c engage_reached_foe`), one `[SLAY-CONTACT]` line per engagement attempt: hero tile / travel
    mode / mount, the foe's live tile, and the pending flow before and after
    (AP-173 posture, it reads only state the engagement already reads; this
    channel measures the AP-086 co-location trap directly).
- **AP-172.** The ledger has been behaviour-inert because its counters live in
  file-statics outside `Game`, so world fingerprints and saves are untouched.
  `day_acct_add` has fed a gross tally (kept across rollbacks) and a committed
  tally, tagged `DAY_ACCT_{OTHER,APPROACH,RECRUIT,CONTRACT,GOLDWAIT,
  CROSSING,RECRUITMOVE,STOCKMOVE}`. A `LedgerSnap` embedded in the `WorldSnapshot` (`ledger_snap` /
  `ledger_unsnap`) has reverted the tallies with a rolled-back attempt (§4).
  `ledger_report` has emitted the `[LEDGER]` block once, from the committed
  node's report at search end, and only under `--verbose`.
- **AP-175.** THE `[UNMET]` REPORT has named, for each objective the run did
  not complete, the truthful typed cause its OWN LAST attempt produced, never
  a guess and never a generic failure. A miss can leave hundreds of objectives
  open, so the two forms differ by volume, not by honesty: under `--verbose`
  one line per unmet objective; by default a roll-up BY CAUSE, most common
  first, each line carrying the tally and one representative objective, so the
  report is bounded by the number of distinct causes. The scepter held back by
  the finale rule (AP-052) has reported `finale-gated:N-objectives-unmet`
  rather than the bare "cause=none" that state would otherwise print.
- **AP-173.** A behaviour-inert diagnostic has been allowed to read the very
  simulation the module already ran, e.g. the enemy-survivor scalars
  (`CombatTurnRecord.ai_stacks` / `ai_hp`) a candidate fight already produced, because such a read adds no engine call and changes no state. A
  gate-unset run has stayed bit-identical to the same run gated.

---

# Part VI, Non-goals

## 18. Deliberately not modelled

- **AP-185.** The following engine features have deliberately not been
  modelled; no requirement covers them and none is a defect:
  - the **Find Villain** spell and the town **Gather Information** action
    (`REQ-322`, `REQ-291`), the planner reads world state directly, so it
    needs neither intel source;
  - the **King's audience / rank-up report** (`REQ-305`), rank-up fires
    automatically on villain capture (`REQ-208`), so the audience is UI-only;
  - **the difficulty-selection SCREEN** (`REQ-210`): autoplay takes its
    difficulty from `--autoplay-level` and its class from `--autoplay-hero`
    (AP-015), so it never drives the interactive character-creation flow;
  - **signposts** (`REQ-354`): informational text with no bearing on the
    verdict.
