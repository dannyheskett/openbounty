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
- **Result:** every seed has cleared every objective at easy and at normal.
  At hard every seed but 3 has; seed 3 has run out of days with objectives
  left, its first blocker a chest in Archipelia it has had no time to reach.

## Days to clear, per seed and difficulty

"days" has been the number of game days the run spent to clear every
objective; "score" has been the engine's end-of-game score; "moves" has been
the recorded `REC_MOVE` count (the turn tally).

| seed | easy (900 days) | | | normal (600 days) | | | hard (400 days) | | |
|-----:|-----:|------:|------:|-----:|------:|------:|-----:|------:|------:|
| | days | score | moves | days | score | moves | days | score | moves |
|    0 |  452 |  3984 | 17389 |  452 |  7969 | 17389 |  310 | 16958 | 12072 |
|    1 |  286 |  3826 | 12379 |  286 |  7652 | 12379 |  286 | 15304 | 12379 |
|    2 |  365 |  5181 | 14872 |  365 | 10362 | 14872 |  365 | 20724 | 14872 |
|    3 |  812 |  3075 | 15298 |  495 |  7096 | 11133 | not solved: 399 days used, score 15956, 9405 moves | | |
|    4 |  586 |  4398 | 16381 |  586 |  8796 | 16381 |  310 | 17380 | 15107 |
|    5 |  885 |  3015 | 21933 |  390 |  8815 | 18665 |  390 | 17630 | 18665 |
|    6 |  625 |  3001 | 16905 |  560 |  9017 | 18866 |  395 | 14132 | 15023 |
|    7 |  430 |  4695 | 13336 |  430 |  9391 | 13336 |  255 | 19506 | 11453 |
|    8 |  805 |  2677 | 21803 |  520 |  7919 | 15221 |  395 | 16440 | 14046 |
|    9 |  655 |  2471 | 21742 |  599 |  6581 | 24583 |  350 | 19838 | 17867 |
|   10 |  701 |  4402 | 16936 |  475 |  9489 | 13206 |  350 | 16740 | 10831 |
| **mean of the solved** | **600** | **3702** | | **469** | **8462** | | **341** | **17465** | |

## What the table has shown

- **A budget that has not bound has not changed play.** Easy and normal have
  produced the IDENTICAL line, same days, same moves, to the number, on
  seeds 0, 1, 2, 4 and 7: exactly the seeds whose easy run has fitted under
  600 days. On those the search has explored the same tree and committed the
  same plan at both budgets, and only the score has differed, by the
  difficulty multiplier.
- **A budget that has bound has changed the plan, not just the deadline.**
  The six seeds that have needed more than 600 days at easy have found
  genuinely different, shorter lines at normal: seed 5 has cleared in 390
  days instead of 885, seed 3 in 495 instead of 812. At hard's 400 days the
  same has happened again to every seed whose normal run has needed more:
  seed 0 has cleared in 310 days instead of 452, its move count down from
  17389 to 12072. Seeds 1, 2 and 5, already under 400 at normal, have
  reproduced exactly.
- **A budget the search has not met has stayed unmet.** Hard's 400 days have
  not held seed 3: the run has spent 399 of them and stopped with objectives
  left, the first of them a chest in Archipelia. The budget has been the
  pack's (AP-015); this seed has been the measure of how tight it is.
- **Score has been the difficulty multiplier applied to one base figure**
  (`engine/game.c GameComputeScore`): easy has halved it with integer
  division, normal has multiplied by 1, hard by 2, impossible by 4. Where the
  line of play has been the same, the relationship has been exact: seed 2 has
  scored 5181 / 10362 / 20724. Where the budget has forced a different line
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
exited 0 only when every seed in the range has solved, so the hard sweep has
exited 1 on this table.
