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
- **Build:** commit `680e4f0` on staging, release build.
- **Result:** every seed has cleared every objective at every difficulty
  measured: easy, normal and hard.

## Days to clear, per seed and difficulty

"days" has been the number of game days the run spent to clear every
objective; "score" has been the engine's end-of-game score; "moves" has been
the recorded `REC_MOVE` count (the turn tally).

| seed | easy (900 days) | | | normal (600 days) | | | hard (400 days) | | |
|-----:|-----:|------:|------:|-----:|------:|------:|-----:|------:|------:|
| | days | score | moves | days | score | moves | days | score | moves |
|    0 |  450 |   629 | 16241 |  450 |  1259 | 16241 |  395 |  2500 | 15827 |
|    1 |  270 |  3245 | 13641 |  270 |  6491 | 13641 |  270 | 12982 | 13641 |
|    2 |  310 |  3863 | 14342 |  310 |  7727 | 14342 |  310 | 15454 | 14342 |
|    3 |  665 |  1912 | 12784 |  453 |  8033 | 11438 |  380 |  5278 | 11988 |
|    4 |  565 |  1920 | 14614 |  565 |  3840 | 14614 |  397 |  8792 | 14758 |
|    5 |  770 |  2492 | 22067 |  496 |  1183 | 19659 |  366 | 14034 | 14640 |
|    6 |  655 |  2374 | 18814 |  375 |  6741 | 13984 |  375 | 13482 | 13984 |
|    7 |  415 |  3445 | 13781 |  415 |  6891 | 13781 |  399 | 13934 | 11756 |
|    8 |  584 |  2025 | 19742 |  584 |  4051 | 19742 |  365 | 12406 | 13269 |
|    9 |  540 |   817 | 15595 |  540 |  1635 | 15595 |  390 |  6892 | 17868 |
|   10 |  662 |  2430 | 15201 |  502 |  6663 | 14001 |  342 | 12264 |  9777 |
| **mean** | **535** | **2287** | | **451** | **4956** | | **363** | **10729** | |

## What the table has shown

- **A budget that has not bound has not changed play.** Easy and normal have
  produced the IDENTICAL line, same days, same moves, to the number, on
  seeds 0, 1, 2, 4, 7, 8 and 9: exactly the seeds whose easy run has fitted under
  600 days. On those the search has explored the same tree and committed the
  same plan at both budgets, and only the score has differed, by the
  difficulty multiplier.
- **A budget that has bound has changed the plan, not just the deadline.**
  The four seeds that have needed more than 600 days at easy have found
  genuinely different, shorter lines at normal: seed 3 has cleared in 453
  days instead of 665, seed 5 in 496 instead of 770. At hard's 400 days the
  same has happened again to every seed whose normal run has needed more:
  seed 8 has cleared in 365 days instead of 584, its move count down from
  19742 to 13269. Seeds 1, 2 and 6, already under 400 at normal, have
  reproduced exactly.
- **Hard's budget has been tight.** Seed 7 has cleared in 399 of hard's 400
  days, and seeds 4 and 0 in 397 and 395. The budget has been the pack's
  (AP-015); these seeds have been the measure of how tight it is.
- **Score has been the difficulty multiplier applied to one base figure**
  (`engine/game.c GameComputeScore`): easy has halved it with integer
  division, normal has multiplied by 1, hard by 2, impossible by 4. Where the
  line of play has been the same, the relationship has been exact: seed 2 has
  scored 3863 / 7727 / 15454. Where the budget has forced a different line
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
