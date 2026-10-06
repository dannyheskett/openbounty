# Autoplay, Winnability Baseline

How the headless autoplay oracle has performed on the reference pack, seed by
seed and difficulty by difficulty. It has recorded measured behaviour, not
requirements -- those have been `AUTOPLAY-SPECS.md` -- and a change to
autoplay's behaviour has come with a new measurement here.

## Measurement

- **Pack:** `assets/kings-bounty`.
- **Command:** one run per difficulty:

  ```sh
  ./build/release/openbounty --validate-pack 0 10 --autoplay-level=easy
  ./build/release/openbounty --validate-pack 0 10 --autoplay-level=normal
  ./build/release/openbounty --validate-pack 0 10 --autoplay-level=hard
  ```

- **Day budgets** have come entirely from the pack's
  `time.days_per_difficulty` (AP-015): easy 900, normal 600, hard 400,
  impossible 200. There has been no arbitrary calendar override.
- **Hero:** the default knight (`AUTOPLAY_HERO_CLASS`).
- **Result:** every seed has cleared every objective at every difficulty
  measured: easy, normal and hard.

## Days to clear, per seed and difficulty

"days" has been the number of game days the run spent to clear every
objective; "score" has been the engine's end-of-game score; "moves" has been
the recorded `REC_MOVE` count (the turn tally).

| seed | easy (900 days) | | | normal (600 days) | | | hard (400 days) | | |
|-----:|-----:|------:|------:|-----:|------:|------:|-----:|------:|------:|
| | days | score | moves | days | score | moves | days | score | moves |
|    0 |  452 |  2734 | 17389 |  452 |  5469 | 17389 |  300 | 10804 | 12038 |
|    1 |  275 |  3054 | 12434 |  275 |  6108 | 12434 |  275 | 12216 | 12434 |
|    2 |  390 |  3813 | 17439 |  390 |  7627 | 17439 |  390 | 15254 | 17439 |
|    3 |  899 |  1748 | 20901 |  511 |  5347 | 11657 |  398 | 12544 | 11274 |
|    4 |  385 |  2955 | 16189 |  385 |  5910 | 16189 |  385 | 11820 | 16189 |
|    5 |  390 |  3207 | 18665 |  390 |  6415 | 18665 |  390 | 12830 | 18665 |
|    6 |  745 |  2166 | 18557 |  595 |  3862 | 16528 |  350 |  7836 | 17960 |
|    7 |  440 |  3525 | 14719 |  440 |  7051 | 14719 |  255 | 15076 | 11484 |
|    8 |  724 |  1375 | 20801 |  599 |  3041 | 19427 |  395 |  3834 | 14187 |
|    9 |  445 |  1596 | 15837 |  445 |  3193 | 15837 |  396 |  7262 | 16162 |
|   10 |  720 |  3284 | 16210 |  575 |  6227 | 14736 |  350 | 11540 | 10831 |
| **mean** | **533** | **2677** | | **459** | **5477** | | **353** | **11001** | |

## What the table has shown

- **A budget that has not bound has not changed play.** Easy and normal have
  produced the IDENTICAL line, same days, same moves, to the number, on
  seeds 0, 1, 2, 4, 5, 7 and 9: exactly the seeds whose easy run has fitted under
  600 days. On those the search has explored the same tree and committed the
  same plan at both budgets, and only the score has differed, by the
  difficulty multiplier.
- **A budget that has bound has changed the plan, not just the deadline.**
  The four seeds that have needed more than 600 days at easy have found
  genuinely different, shorter lines at normal: seed 3 has cleared in 511
  days instead of 899, seed 8 in 599 instead of 724. At hard's 400 days the
  same has happened again to every seed whose normal run has needed more:
  seed 0 has cleared in 300 days instead of 452, its move count down from
  17389 to 12038. Seeds 1, 2, 4 and 5, already under 400 at normal, have
  reproduced exactly.
- **Hard's budget has been tight.** Seed 3 has cleared in 398 of hard's 400
  days, and seeds 8 and 9 in 395 and 396. The budget has been the pack's
  (AP-015); these seeds have been the measure of how tight it is.
- **Score has been the difficulty multiplier applied to one base figure**
  (`engine/game.c GameComputeScore`): easy has halved it with integer
  division, normal has multiplied by 1, hard by 2, impossible by 4. Where the
  line of play has been the same, the relationship has been exact: seed 2 has
  scored 3813 / 7627 / 15254. Where the budget has forced a different line
  the scores have not been multiples of each other, because the run has
  captured a different set of castles and artifacts and spent different
  losses.
- **Search cost has risen sharply as the budget has tightened.** The seeds
  where the budget has bound have taken most of a sweep's time: a budget the
  first descent has not satisfied is what has made the search backtrack.
- **A low or zero score has been a costly line, not a failed one.** Score has
  charged for temp deaths, so a run that has spent them has been able to
  clear every objective and still have scored poorly. The verdict and the
  score have answered different questions.

## Reproducing this table

Build release, then run the three commands above. The search has been
deterministic (same build, seed and difficulty have given the same plan), so
every column has reproduced exactly on the same build. `--validate-pack` has
exited 0 only when every seed in the range has solved, and all three sweeps
have exited 0 on this table.
