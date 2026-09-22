# Autoplay, Winnability Baseline

How the headless autoplay oracle has performed on the reference pack, seed by
seed and difficulty by difficulty, measured on the commit named below. It has
recorded measured behaviour, not requirements -- those have been
`AUTOPLAY-SPECS.md` -- and a change to autoplay's behaviour has come with a new
measurement here.

## Measurement

- **Commit:** `fc0190d`.
- **Pack:** `assets/kings-bounty`.
- **Command:** one run per difficulty:

  ```sh
  ./build/release/openbounty --validate-pack 0 4 --autoplay-level=easy
  ./build/release/openbounty --validate-pack 0 4 --autoplay-level=normal
  ./build/release/openbounty --validate-pack 0 4 --autoplay-level=hard
  ```

- **Day budgets** have come entirely from the pack's
  `time.days_per_difficulty` (AP-015): easy 900, normal 600, hard 400,
  impossible 200. There has been no arbitrary calendar override.
- **Hero:** the default knight (`AUTOPLAY_HERO_CLASS`).
- **Result:** 15 / 15 runs have cleared every objective, all 280, on every
  seed at all three difficulties.

## Days to clear, per seed and difficulty

"days" has been the number of game days the run spent to clear all 280
objectives; "score" has been the engine's end-of-game score; "moves" has been
the recorded `REC_MOVE` count (the turn tally).

| seed | easy (900 days) | | | normal (600 days) | | | hard (400 days) | | |
|-----:|-----:|------:|------:|-----:|------:|------:|-----:|------:|------:|
| | days | score | moves | days | score | moves | days | score | moves |
|    0 |  470 |  4444 | 14014 |  470 |  8888 | 14014 |  225 | 22124 |  8192 |
|    1 |  300 |  4247 | 16078 |  300 |  8494 | 16078 |  300 | 16988 | 16078 |
|    2 |  300 |  5023 | 12368 |  300 | 10047 | 12368 |  300 | 20094 | 12368 |
|    3 |  201 |  4385 |  8822 |  201 |  8771 |  8822 |  201 | 17542 |  8822 |
|    4 |  540 |  4300 | 16581 |  540 |  8601 | 16581 |  325 | 21484 | 15734 |
| **mean** | **362** | **4479** | | **362** | **8960** | | **270** | **19646** | |

Wall clock for the whole 5-seed sweep on the reference machine has been: easy
2:21, normal 1:56, hard 17:09.

## What the table has shown

- **A budget that has not bound has not changed play.** Easy and normal have
  produced the IDENTICAL line on all five seeds, same days, same moves, to the
  number. The most any seed has needed here is 540 days, so neither the
  900-day nor the 600-day budget has constrained the search, and both have
  explored the same tree and committed the same plan. Only the score has
  differed, and only because of the difficulty multiplier.
- **A budget that has bound has changed the plan, not just the deadline.** At
  hard's 400 days, the two seeds that needed more than that at normal have
  found genuinely different, shorter lines: seed 0 has cleared in **225** days
  instead of 470, and seed 4 in **325** instead of 540. Neither has been a
  truncation of the longer run: seed 0's move count has dropped from 14014 to
  8192. The remaining three seeds (201–300 days) have already fitted under 400
  and have reproduced exactly.
- **Score has been the difficulty multiplier applied to one base figure**
  (`engine/game.c GameComputeScore`): easy has halved it with integer
  division, normal has multiplied by 1, hard by 2, impossible by 4. Where the
  line of play has been the same, the relationship has been exact: seed 2 has
  scored 5023 / 10047 / 20094, and 10047 / 2 = 5023 by truncation. Where the
  budget has forced a different line (seeds 0 and 4) the scores have not been
  multiples of each other, because the run has captured a different set of
  castles and artifacts and spent different losses.
- **Search cost has risen sharply as the budget has tightened.** The hard
  sweep has taken about nine times the normal sweep's wall clock, and nearly
  all of it has been the two seeds where the budget has bound (seed 0 at 11:03,
  seed 4 at 5:09). A budget the first descent has not satisfied is what has made
  the search actually backtrack.
- **A low or zero score has been a costly line, not a failed one.** Score has
  charged for temp deaths, so a run that has spent them has been able to clear every
  objective and still have scored poorly. The verdict and the score have answered
  different questions.

## Reproducing this table

Build release, then run the three commands above. The search has been
deterministic (same build, seed and difficulty have given the same plan), so
every column has reproduced exactly on the same commit. `--validate-pack` has
exited 0 only when every seed in the range has solved.
