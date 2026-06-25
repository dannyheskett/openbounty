# Recruitment consolidation — the single polymorphic `exec_recruit()` seam

**Status:** Stage 1 + Stage 2 **IMPLEMENTED & VERIFIED**; Stage 3 (policy unification) pending.
**Decision on file:** the executor must do *all* army recruitment and test-recruitment
through ONE surface — `exec_recruit()` — invoked polymorphically. The divergent
search policies (ranking metric, accept gates) are to be unified in **Stage 3**.

### Implementation status (as built)

- **Stage 1 (done)** — the three searches + `recruit_source_foot_ok` moved verbatim into
  `exec_recruit.c`; `exec_recruit()` is now the polymorphic dispatcher over
  `RecruitRequest{mode}` (`RECRUIT_APPLY_TARGET` / `ADD_FOR_WIN` / `RECOMPOSE_FOR_WIN` /
  `BUILD_FOR_WIN`); the old APPLY body is the private `recruit_apply_target`; `exec_army`
  uses APPLY, `exec_slay`/`exec_siege` use the explicit 3-call `||` ladder. Dead
  `exec_dwell`/`PRIM_DWELL` dispatch removed (its smoke test too).
- **Stage 2 (done)** — the six duplicated snippets folded into shared file-scope statics
  (`SDX/SDY`, `dismiss_held_not_in`, `rec_step_off_tile`, `held_ratio_for`,
  `recruit_trial_count`, `pick_best_winner`), honoring H2 (`exec_recruit_one`'s buy clamp
  and `exec_build_for`'s running-budget clamp untouched) and folding only the *dismiss*
  loop of DUP B (not the source-selecting buy).
- **Verification** — headless autoplay output **byte-identical across 14 seeds** (1–12, 20, 42;
  searches fire 290+ times across them), full test suite green (236/236), clean `-Werror`
  build, and a line-by-line adversarial review of all 8 hazards returned EQUIVALENT.

**Deviations from the original plan (deliberate):**
1. The leaves (`exec_recruit_one` / `exec_dismiss` / `exec_recruit_sources`) were **not** made
   `static`/removed from `exec.h` — the isolation unit tests call them directly, and the
   original ask explicitly permits helpers. Singularity is instead enforced by "no production
   caller outside `exec_recruit.c`" (grep-verified) — the substantive *decision/search* logic
   is what is consolidated.
2. `dry_run` / `RecruitResult` were **omitted** (YAGNI): the polymorphic modes already deliver
   "real or trial recruitment"; the pure predict-without-commit lever lands in Stage 3 with its
   first consumer (the "decide once" probe-cost fix).
3. `PRIM_DWELL` / `STEP_RECRUIT_DWELLING` enum values left vestigial (deleting enum members is
   churn; never emitted). Reviving specific-dwelling recruit needs a new `RECRUIT_AT_SOURCE`
   mode — see §5.

---

## 1. Problem

`exec_recruit()` (`autoplay/exec_recruit.c:151`) is *meant* to be the one recruitment
surface, but the "decide a composition and realize it" logic exists in **four**
places, and three of them bypass the seam — each carrying a drifted copy of the same
snippets.

| Decider | What it owns | Routes through `exec_recruit()`? | Live? |
|---|---|---|---|
| `exec_recruit` (`exec_recruit.c:151`) | APPLY a known `ArmyTarget`: dismiss-then-buy, do-or-fail | — *(is the seam)* | via recompose only |
| `exec_army` / PRIM_ARMY (`primitives.c:709`) | wraps `exec_recruit(&p->army)` | yes | **DEAD** — planner emits no `STEP_RECOMPOSE_ARMY` |
| `try_recruit_for_win` (`primitives.c:232`) | **ADD-to-slot** search, survival-ratio rank, no Pyrrhic gate | **no** — calls `exec_recruit_one` directly (`:286`) | yes |
| `try_recompose_for_win` (`primitives.c:319`) | **SWAP-weakest** search, ratio rank, new-type-only | yes — builds target → `exec_recruit` (`:386`) | yes *(only live seam caller)* |
| `exec_build_for` (`primitives.c:419`) | **full greedy rebuild**, max-post-hp rank, 2 absolute gates, off-zone excluded | **no** — `exec_dismiss` + `exec_recruit_one` directly (`:551`,`:563`) | yes |
| `exec_dwell` / PRIM_DWELL (`primitives.c:658`) | single-dwelling buy; answers `FLOW_RECRUIT` itself (`:669`) | **no** — parallel path | **DEAD** — no `STEP_RECRUIT_DWELLING` |
| `exec_home` / PRIM_HOME (`primitives.c:798`) | stub `return false` | n/a | **DEAD** |

The live decision surface is the **3-tier ladder** in `exec_slay` (`primitives.c:624-628`)
and `exec_siege` (`primitives.c:749-753`): after `exec_fight` declines a fight it runs
`try_recruit_for_win || try_recompose_for_win || exec_build_for`, re-approaches, fights again.

Two layers of duplication:

- **Leaves are already single** — `exec_recruit_one` (`:38`), `exec_recruit_sources`/
  `recruit_sources_enumerate` (`:29`/`:247`), `exec_dismiss` (`:196`),
  `predict_combat_eval` (`:366`), `recruit_affordable_count` (`:355`). Nothing
  bypasses the *engine*; the duplication is purely in the *decision + commit* layer.
- **Decision+commit is quadruplicated**, with six copy-pasted snippets that have drifted:
  - **DUP A** — step-off-tile + walk-on-chest: ×3 (`primitives.c:290-305`, `:390-404`, `:577-593`)
  - **DUP B** — dismiss-not-in-composition loop: ×2 (`exec_recruit.c:156-164`, `primitives.c:544-552`)
  - **DUP C** — maxn + affordable clamp: ×4 (`primitives.c:253-258`, `:346-351`, `:448-453`, `exec_recruit.c:128-131`)
  - **DUP D** — held-ratio compute: ×2 (`primitives.c:241-247`, `:336-342`)
  - **DUP E** — greedy winner-pick (`used[]`/`picked`): ×2 (`primitives.c:280-307`, `:368-385`)
  - **DUP F** — `SDX/SDY` 8-neighbour arrays: ×3 (`primitives.c:235-236`, `:323-324`, `:422-423`)

**Cost.** One blocked fight runs the whole ladder ≈ `7·ns + 4` headless combat sims
(`ns` up to 64) across seven `predict_combat_eval` sites
(`primitives.c:244,273,339,362,427,483,509`) — the "decide once; ~6 probe sites re-run
unbounded" problem. The `COMBAT_SIMS` meter (`exec_recruit.c:377-381`) is the permanent
gauge for it — leave it.

## 2. Live-vs-dead (so we don't design against ghosts)

- **Live decision stack:** `main.c:260 → autoplay() → planner() → execute() → exec_*.c → engine`.
  In **headless** mode `autoplay()` runs `planner()` then *dies* (`autoplay.c:429`) — it
  never replays; every mutation lands on the planner's throwaway boot world while it emits
  the `RA_*` recording. The **visible** shell (`shell_autoplay.c:172`) and the **test suite**
  replay that same recording via `plan.c`'s `plan_exec_step`.
- **Dead (do not account for / do not revive):** `exec_army`/PRIM_ARMY, `exec_dwell`/PRIM_DWELL,
  `exec_home`/PRIM_HOME (no planner producer); `plan_build` (referenced, defined nowhere);
  the entire `RA_SET_ARMY` / `autoplay_apply_set_army` recompose-replay branch
  (`autoplay.c:95`, dispatched `plan.c:187-188`) — **no executor emits `RA_SET_ARMY`**, and it
  could not reproduce the dwelling `FLOW_RECRUIT` or cross-zone travel anyway. `driver.c`/
  `nav.c`/`navigator.c`/`recruit.c` do not exist on disk.

## 3. Target: one polymorphic `exec_recruit()`

A single public entry, mode-dispatched, with a `dry_run` flag that is the "test recruitment"
surface. New types live in `recruit.h` beside `ArmyTarget`:

```c
typedef enum {
    RECRUIT_APPLY_TARGET = 0,   // commit a precomputed ArmyTarget (today's exec_recruit; PRIM_ARMY)
    RECRUIT_ADD_FOR_WIN,        // additive add-to-slot search   (was try_recruit_for_win)
    RECRUIT_RECOMPOSE_FOR_WIN,  // swap-weakest search           (was try_recompose_for_win)
    RECRUIT_BUILD_FOR_WIN,      // full greedy rebuild           (was exec_build_for)
} RecruitMode;

typedef struct {
    RecruitMode         mode;
    const ArmyTarget   *target;        // APPLY_TARGET only (NULL otherwise)
    CombatMode          combat_mode;   // *_FOR_WIN: COMBAT_MODE_FOE / COMBAT_MODE_CASTLE
    const CombatTarget *tgt;           // *_FOR_WIN: who we must beat
    int                 min_survivors; // 1 foe / 2 monster castle (CASTLE_GARRISON_MIN_SURVIVORS)
    bool                dry_run;        // decide + predict, never mutate or record (*_FOR_WIN only)
} RecruitRequest;

typedef struct {
    bool       changed;          // army actually mutated (always false when dry_run)
    bool       predicted_win;    // ADVISORY: chosen/target army predicts WIN w/ >= min_survivors
    ArmyTarget chosen;           // the composition decided (set on commit AND dry_run)
    long       committed_worth;  // army_hp_worth after commit (or of `chosen` on dry_run)
} RecruitResult;

bool exec_recruit(Game *g, Map *map, Fog *fog, const Resources *res,
                  const RecruitRequest *req, RecSink *rec, RecruitResult *out /*nullable*/);
```

**Return `bool` = "the request achieved its goal":**

- `APPLY_TARGET` → army now equals `target` (today's do-or-fail value of dismiss-then-buy).
- `*_FOR_WIN` → **the literal return of the moved search body**: a recruit was committed, OR
  (BUILD only) the held army already wins with enough survivors and the early-out returned true.
  **No post-commit re-prediction feeds the bool** (see Hazard H1). `predicted_win` is a
  separate advisory field only.
- `*_FOR_WIN` + `dry_run` → a qualifying composition was *found* (`out->chosen` holds it,
  `out->changed == false`).

**`dry_run` is the test-recruitment lever.** It is the existing search truncated *before* the
first commit leaf. Every pre-commit step is a pure read — `predict_combat_eval` snapshots/
restores world RNG (`exec_recruit.c:382/397`), `GameMaxRecruitable`/`recruit_affordable_count`/
`recruit_source_foot_ok`/nav don't mutate — so a dry-run cannot advance the RNG the *later*
authoritative recorded fight depends on, and never touches `rec`. It is the "decide once,
cache `chosen`, then `APPLY_TARGET` it" path that bounds the `7·ns+4` probe cost. **Caveat:**
a dry-run proves *combat feasibility* of a composition, not *physical realizability* — a source
may be unreachable or `GameBuyTroop` may refuse at commit, so a later `APPLY_TARGET` can still
return false and the caller must tolerate that.

**Singularity is linker-enforced, not convention.** `exec_recruit_one`, `exec_dismiss`, and
`exec_recruit_sources` become `static` in `exec_recruit.c` and their declarations are **deleted
from `exec.h`** (`:144-160`). After the move every caller of those leaves lives inside
`exec_recruit.c`, so this compiles — and no other TU can ever bypass the seam again.

## 4. Internal decomposition

`exec_recruit()` owns these private statics in `exec_recruit.c` (the documented "wraps a real
algorithm, owns private internals" exception, `exec.h:22-25`):

**Stays (already single, reusable):** `recruit_sources_enumerate`, `exec_recruit_one`,
`exec_dismiss`, `predict_combat_eval` (**keep the `COMBAT_SIMS` counter**),
`recruit_affordable_count`, `army_hp_worth`, `army_stack_count`, `army_upkeep`.

**Moves in (was static in `primitives.c`):** `recruit_source_foot_ok` (`primitives.c:211-225`).

**New private statics folding the six dup snippets once:**

- `SDX[8]/SDY[8]` — folds DUP F.
- `rec_step_off_tile(...)` — walk off an interactive tile, answer a walk-on `FLOW_CHEST_CHOICE`
  (take gold). Folds DUP A.
- `held_ratio_for(g,mode,tgt,min_surv) → long` — held army's own survival ratio (`-1` if it
  does not itself win). Folds DUP D.
- `pick_best_winner(wr,used,nw) → int` — strict-`>` max-ratio greedy, ascending-`k` tiebreak.
  Folds DUP E. **Tiebreak must stay strict-`>` over ascending `k`** (Hazard H4).
- `recruit_cap_for(g,src) → int = min(avail-or-maxn, affordable)` — the **trial/snapshot** clamp
  for the search sites only (`primitives.c:253-258,346-351`). **Does NOT replace
  `exec_recruit_one`'s own `min(n,maxn,affordable)` clamp** — that one is shortfall-aware and
  avail-blind and feeds the recorded buy payload (Hazard H2).

**The commit core — `rec_commit(g,map,fog,res, slots,n, src_by_slot /*nullable*/, rec)`:**
today's `exec_recruit` body (`:156-187`) extracted: dismiss-not-in-comp (folds DUP B with
`exec_build_for:544-552`), then buy each shortfall. Buys from `src_by_slot[i]` when provided
(BUILD passes `slot_src[]`); else first-match-by-id (`:182-184`). **APPLY and SWAP pass
`src_by_slot = NULL` (first-match); only ADD and BUILD pass an explicit source** (Hazard H3).

**The search core** — APPLY is "build `chosen` from `req->target`, `rec_commit`." The three
searches collapse onto one skeleton parameterized by a strategy. After the Stage-3 unification
(§6) the only *per-mode* axes left are the search *shape*, not the ranking/gate *policy*:

```c
typedef struct {
    enum { NB_ADD_SLOT, NB_SWAP_WEAK, NB_GREEDY_REBUILD } neighbourhood;
    bool allow_offzone;       // ADD/SWAP: true ; BUILD: false
    bool require_new_type;    // SWAP: true ; ADD: false
    bool need_two_stacks;     // SWAP: true
    bool early_out_if_winning;// BUILD: true ; ADD/SWAP: false
} RecruitStrategy;
static const RecruitStrategy STRAT[4];   // indexed by RecruitMode
```

Ranking and accept gates are **no longer in the table** — they are unified (§6).

## 5. Caller migration

**`exec_army` / PRIM_ARMY** (`primitives.c:709-712`):
```c
RecruitRequest req = { .mode = RECRUIT_APPLY_TARGET, .target = &p->army };
return exec_recruit(g, map, fog, res, &req, rec, NULL);
```

**`exec_slay` 3-tier** (`primitives.c:624-628`) — **explicit ladder of three seam calls**
(keeps control flow, and therefore recorded-action order, identical):
```c
RecruitRequest add = {.mode=RECRUIT_ADD_FOR_WIN,       .combat_mode=COMBAT_MODE_FOE,.tgt=&tgt,.min_survivors=1};
RecruitRequest rcp = {.mode=RECRUIT_RECOMPOSE_FOR_WIN, .combat_mode=COMBAT_MODE_FOE,.tgt=&tgt,.min_survivors=1};
RecruitRequest bld = {.mode=RECRUIT_BUILD_FOR_WIN,     .combat_mode=COMBAT_MODE_FOE,.tgt=&tgt,.min_survivors=1};
bool recruited =
    exec_recruit(g,map,fog,res,&add,rec,NULL)
 || exec_recruit(g,map,fog,res,&rcp,rec,NULL)
 || exec_recruit(g,map,fog,res,&bld,rec,NULL);
```

**`exec_siege` 3-tier** (`primitives.c:749-753`) — identical, `.combat_mode=COMBAT_MODE_CASTLE`,
`.min_survivors=min_surv` (`:735`). Garrison-on-win (`:745`,`:766`) untouched.

**SWAP's internal commit** (`primitives.c:386`) becomes
`RecruitRequest{.mode=RECRUIT_APPLY_TARGET,.target=&target}` with `out=NULL` and **first-match-by-id
source resolution preserved** (Hazard H3). This is the one *forced* edit by the signature change.

**`exec_dwell` / PRIM_DWELL** — **delete it** (the function, the `PRIM_DWELL` dispatch at
`primitives.c:859`, and the `STEP_RECRUIT_DWELLING→PRIM_DWELL` mapping at `:840`). It is dead,
and APPLY *cannot* reproduce its "buy at *this* stepped-on dwelling" semantics (APPLY re-resolves
by first-match, so for a troop sold at multiple sources it would buy elsewhere). If a
`STEP_RECRUIT_DWELLING` producer is ever added, it needs a new `RECRUIT_AT_SOURCE` mode taking a
concrete `RecruitSource` — out of scope here, but recorded so the gap is explicit.

**`exec_home` / PRIM_HOME** — leave the stub in Stage 1; delete `PRIM_HOME` + mapping (`:839`) +
the dead `RA_RECRUIT_HOME` applier (`autoplay.c:237`) in the same cleanup sweep as PRIM_DWELL.

## 6. Unified policy (Stage 3 — committed, separately benchmarked)

The user opted to **unify the divergent ranking + accept policies in this pass** (fixing the
ADD/SWAP Pyrrhic-bypass bug). Today's divergence:

| Axis | ADD / SWAP | BUILD |
|---|---|---|
| ranking metric | survival ratio `post_hp/committed` | absolute max `post_hp` (greedy) |
| not-weaker floor | none | Gate 1: `trial_worth ≥ current_worth` (`:520-531`) |
| Pyrrhic floor | none | Gate 2: `ratio ≥ PRESERVE_MIN_RATIO_PCT` (25%), all modes (`:536-541`) |
| improvement bar | `ratio > held_ratio` | n/a |

**The bug:** ADD/SWAP can commit an army whose ratio improved but is still `< 25%`; on the
re-approach `exec_fight` re-declines it (`exec_fight.c:78`, foe Pyrrhic gate) → wasted recruit +
objective fail. Only BUILD guarantees its output clears `exec_fight`.

**Unified policy (all three searches):**

- **Ranking metric → survival ratio** for every mode. This is the scale-free efficiency objective
  `exec.h:104-110` already documents as the pipeline's goal; BUILD's "max absolute `post_hp`" greedy
  is the outlier (it over-recruits). BUILD's per-slot greedy inner loop switches its "better"
  comparison from `post_hp` to ratio.
- **Accept gate** (replaces both the relative bar and BUILD's two gates):
  1. `WIN ∧ surviving_stacks ≥ min_survivors` (feasibility — unchanged).
  2. `committed_worth ≥ current_army_worth` (not-weaker; BUILD's Gate 1 made universal — preserves
     hero strength for downstream objectives).
  3. **Mode-aware Pyrrhic floor — the bug fix:** for `COMBAT_MODE_FOE`, `ratio ≥ PRESERVE_MIN_RATIO_PCT`
     (guarantees the chosen army clears `exec_fight`'s live foe gate, so no recruit-then-redecline).
     For `COMBAT_MODE_CASTLE`, **no Pyrrhic floor** — matching `exec_fight`, which lets costly castle
     wins through (`exec.h:108-110`); a captured castle is worth a Pyrrhic fight.
  - Note: given the pipeline's entry condition (`exec_fight` only declines on LOSS or foe-Pyrrhic),
    requiring `ratio ≥ 25%` for foes *implies* `ratio > held_ratio`, so the old relative bar is
    subsumed for foes; for castles the not-weaker floor + ratio-max ranking replace it.

**Per-mode axes that STAY (intentional search-shape, not policy):** neighbourhood
(add / swap / rebuild — the cheap→medium→expensive escalation ladder), `allow_offzone`
(ADD/SWAP allow off-zone dwellings; BUILD excludes them), `require_new_type` (SWAP),
`need_two_stacks` (SWAP), `early_out_if_winning` (BUILD).

**Behavior consequences to expect in the benchmark** (this is the intended seed churn):

- BUILD picks different per-slot troops (ratio vs `post_hp`).
- ADD/SWAP now *reject* still-Pyrrhic foe armies they used to commit → some foes that
  previously "recruited then re-declined" now fall through to BUILD (or defer), and some
  previously-wasted recruit detours disappear. Net should be **neutral-to-positive**
  (fewer wasted detours, no army that fails its own re-fight), but it must be measured.
- BUILD for *castles* loses its Pyrrhic Gate 2 → may now recruit a Pyrrhic-but-winning castle
  army it previously rejected. This aligns BUILD with `exec_fight`'s actual castle rule.

Governed by the "never revert a more-correct change for the count" rule: keep the unified
policy if it is net-positive **or** net-neutral-with-fewer-wasted-sims; only revisit a specific
axis if a seed regresses for a diagnosable reason.

## 7. Staged migration

The verification oracle is weak: the per-step fingerprint check is **disabled** in every live
path (`admitted_count==0` at `shell_autoplay.c:191`, `test_autoplay_run.c:93`); only terminal
state + the user's ≥12-seed net+correctness benchmark catch drift, and convergent terminal state
can hide a reordered buy. **Every stage is gated on a full `RA_*`/`REC_*` recording-stream diff,
not just terminal state.**

- **Stage 1 — move + rename + privatize (provably byte-for-byte).** Cut the three searchers +
  `recruit_source_foot_ok` into `exec_recruit.c`, **bodies unchanged**; switch `exec_recruit` to
  `RecruitRequest`; migrate the 4 call sites (§5) to the 3-call ladder / APPLY; rewrite SWAP's
  `:386` to APPLY (first-match); mark the three leaves `static` + delete from `exec.h`; **delete
  dead `exec_dwell`/PRIM_DWELL**. Add the `dry_run`/`RecruitResult` capability but **wire no live
  caller to it** (or add a unit test exercising it — see Hazard H6). Recording-stream diff must be
  empty on all benchmark seeds.
- **Stage 2 — fold the six dup snippets**, one at a time, each stream-diffed (mind H2/H3).
- **Stage 3 — unify policy (§6).** The only stage that *intends* seed churn; benchmark it as its
  own change.

**Files touched:** `autoplay/exec_recruit.c` (new statics, search core, `rec_commit`, signature,
unified policy), `autoplay/exec.h:141-160` (decl change + delete the three leaf decls),
`autoplay/recruit.h` (`RecruitMode`/`RecruitRequest`/`RecruitResult`), `autoplay/primitives.c`
(delete the three searchers + `recruit_source_foot_ok` + `exec_dwell`/PRIM_DWELL; migrate
`exec_army`/`exec_slay`/`exec_siege`; fix the stale `#include "worldsnap.h"` at `:49` — orphaned,
no `worldsnap_*` call in the file). No engine, `autoplay.c`-applier, `plan.c`, or
recording-grammar changes.

## 8. Hazards (from the adversarial review — must honor)

- **H1 (major):** the `*_FOR_WIN` bool must be the moved body's *literal* return — BUILD's
  already-winning early-out (`primitives.c:426-429`) returns `true` with **no commit**; commit-success
  returns `true` with **no post-commit re-prediction**. Any "AND predicted_win" recheck flips the
  `||` ladder (a gold-capped commit can be weaker than the trial that passed the gate). Keep
  `predicted_win` advisory.
- **H2 (blocker):** do **not** fold `exec_recruit_one`'s clamp (`exec_recruit.c:128-131`) into
  `recruit_cap_for`. `exec_recruit_one` computes `min(n,maxn,affordable)` (shortfall-aware,
  avail-blind) and records `buy` as the `RA_RECRUIT_TYPE_N` payload (`:107`); `recruit_cap_for`
  clamps by `avail` and ignores `n`. Substituting would change the recorded buy on nearly every
  dwelling recruit *and* sneak in the avail-cap that Open Q4 defers.
- **H3 (major):** SWAP's recorded source is **first-match-by-id** (`:181-184` via `:386`), not the
  source it simulated. SWAP and APPLY pass `src_by_slot=NULL`; only ADD (`srcs[wi[b]]`) and BUILD
  (`slot_src[]`) pass an explicit source.
- **H4:** `pick_best_winner` keeps strict-`>` over ascending `k` (ties → lowest index). Source
  enumeration order (TIER1 `dwellings[]` index, TIER2 `troops_count()` index, TIER3 off-zone) must
  not be reordered.
- **H5:** reject `{APPLY_TARGET, dry_run=true}` (no search to truncate; a naive NULL-sink commit
  would mutate while suppressing records and advance world RNG). `dry_run` is `*_FOR_WIN` only.
- **H6:** populate `out` from values already computed in the search (`wr[b]`, the chosen trial,
  an `army_hp_worth` read) — never an extra `predict_combat_eval` (would burn sims + shift the
  `COMBAT_SIMS` meter).
- **H7:** in Stage 1, APPLY stays byte-identical to today's `exec_recruit` (`:153-188`) — **no
  step-off** (step-off lives only in the searchers today; the `rec_commit` step-off fold is Stage 2).
- **H8 (replay order):** `rec_commit` emits **all dismisses before all buys** (a dismiss frees the
  leadership a later buy needs); dismiss is recorded **by id** (`:216`, slots compact); dwelling
  recruits ride a `REC_ANSWER` (not `RA_*`) and the zero-buy `FLOW_ANS_NO` answer is still recorded
  to clear the dangling `FLOW_RECRUIT` (`:138`); the post-recruit step-off moves are recorded and
  fix the hero's tile for the next `exec_reach`.

## 9. Scope boundary — army mutations EXCLUDED from the seam

So "all recruitment" is a closed set, these stay out (they are not decide-and-realize recruitment):

- `FLOW_ACCEPT_FRIENDLY` free-join — adds a friendly foe's troops (`exec_fight.c:44`,
  `exec_move.c:133`); a combat mechanic.
- `GameGarrisonTroop` — removes a held stack into a captured castle (`exec_fight.c:184`,
  `exec_garrison`); a garrison deposit.
- `autoplay.c` replay appliers (`autoplay_apply_rec_action`/`_set_army`) — the recording VM, not a
  decision path; `RA_SET_ARMY` is dead.

## 10. Open questions (not resolved by the two decisions taken)

1. **Q4 — latent engine clamp bug:** `flow_apply_recruit` (`flow_resolve.c:252-280`) decrements
   dwelling stock on rc 3/4 even when no buy occurs, and `exec_recruit_one`'s dwelling branch caps
   by `min(maxn, affordable)` not `src->avail` (`:128-131`). Fix the `avail`/location clamp inside
   `exec_recruit_one` as part of this work, or as a separately-benchmarked change?
2. **Q2 — ladder shape:** keep the 3-tier escalation as an explicit 3-call ladder at the call sites
   (recommended; Stage 1 keeps recorded order identical), or later collapse into a single internal
   `RECRUIT_FOR_WIN` mode that owns the escalation? (Pure cleanup, no behavior change; can defer.)
3. **dry_run consumer:** keep `dry_run`/`RecruitResult` as latent capability with a unit test
   (YAGNI-lite), or wait until the "decide once → cache → APPLY" rewrite (the probe-cost fix) gives
   it a real consumer before adding it at all?
