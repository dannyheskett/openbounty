# Autoplay, Winnability Baseline

A measured record of how the headless autoplay oracle performs on the reference
pack, seed by seed and difficulty by difficulty. This is a snapshot of measured
behavior, not a specification: the requirements live in `AUTOPLAY-SPECS.md`.
Re-measure and update this file whenever autoplay behavior changes.

## Measurement

- **Commit:** `fc0190d`.
- **Pack:** `assets/kings-bounty`.
- **Command:** one run per difficulty, ```sh
  ./build/release/openbounty --validate-pack 0 4 --autoplay-level=easy
  ./build/release/openbounty --validate-pack 0 4 --autoplay-level=normal
  ./build/release/openbounty --validate-pack 0 4 --autoplay-level=hard
  ```

- **Day budgets** come entirely from the pack's `time.days_per_difficulty`
  (AP-015): easy 900, normal 600, hard 400, impossible 200. There is no
  arbitrary calendar override.
- **Hero:** the default knight (`AUTOPLAY_HERO_CLASS`).
- **Result:** 15 / 15 runs cleared, every objective, all 280, on every seed at
  all three difficulties.

## Days to clear, per seed and difficulty

"days" is the number of game days the run spent to clear all 280 objectives;
"score" is the engine's end-of-game score; "moves" is the recorded `REC_MOVE`
count (the turn tally).

| seed | easy (900 days) | | | normal (600 days) | | | hard (400 days) | | |
|-----:|-----:|------:|------:|-----:|------:|------:|-----:|------:|------:|
| | days | score | moves | days | score | moves | days | score | moves |
|    0 |  470 |  4444 | 14014 |  470 |  8888 | 14014 |  225 | 22124 |  8192 |
|    1 |  300 |  4247 | 16078 |  300 |  8494 | 16078 |  300 | 16988 | 16078 |
|    2 |  300 |  5023 | 12368 |  300 | 10047 | 12368 |  300 | 20094 | 12368 |
|    3 |  201 |  4385 |  8822 |  201 |  8771 |  8822 |  201 | 17542 |  8822 |
|    4 |  540 |  4300 | 16581 |  540 |  8601 | 16581 |  325 | 21484 | 15734 |
| **mean** | **362** | **4479** | | **362** | **8960** | | **270** | **19646** | |

Wall clock for the whole 5-seed sweep on the reference machine: easy 2:21,
normal 1:56, hard 17:09.

## What the table shows

- **A budget that does not bind does not change play.** Easy and normal produce
  the IDENTICAL line on all five seeds, same days, same moves, to the number.
  The most any seed needs here is 540 days, so neither the 900-day nor the
  600-day budget constrains the search, and both explore the same tree and
  commit the same plan. Only the score differs, and only because of the
  difficulty multiplier.
- **A budget that binds changes the plan, not just the deadline.** At hard's
  400 days, the two seeds that needed more than that at normal find genuinely
  different, shorter lines: seed 0 clears in **225** days instead of 470, and
  seed 4 in **325** instead of 540. Neither is a truncation of the longer run, seed 0's move count drops from 14014 to 8192. The remaining three seeds
  (201–300 days) already fit under 400 and reproduce exactly.
- **Score is the difficulty multiplier applied to one base figure**
  (`engine/game.c GameComputeScore`): easy halves it with integer division,
  normal multiplies by 1, hard by 2, impossible by 4. Where the line of play is
  the same, the relationship is exact, seed 2 scores 5023 / 10047 / 20094, and
  10047 / 2 = 5023 by truncation. Where the budget forced a different line
  (seeds 0 and 4) the scores are not multiples of each other, because the run
  captured a different set of castles and artifacts and spent different losses.
- **Search cost rises sharply as the budget tightens.** The hard sweep takes
  about nine times the normal sweep's wall clock, and nearly all of it is the
  two seeds where the budget binds (seed 0 at 11:03, seed 4 at 5:09). A budget
  the first descent cannot satisfy is what makes the search actually
  backtrack.
- **A low or zero score is a costly line, not a failed one.** Score charges for
  temp deaths, so a run that spent them can clear every objective and still
  score poorly. The verdict and the score answer different questions.

## Reproducing this table

Build release, then run the three commands above. The search is deterministic (same build, seed and difficulty give the same plan) so every column reproduces
exactly on the same commit. `--validate-pack` exits 0 only when every seed in
the range solved.
