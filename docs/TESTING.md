# Testing

What has checked openbounty, and how to run and extend it. README §3 has
listed the test runner's flags.

## The test binary

```sh
make test                                # build build/openbounty-test and run it all
./build/openbounty-test -s unit_         # one layer
./build/openbounty-test -s save -v       # suites matching "save", verbose
```

`make test` has built one binary from the shell, demo, autoplay and tool
sources, every test file and `libobengine.a`, and has depended on every
header, so an edited header has rebuilt it. Tests have run from the
repository root: they have read `assets/kings-bounty` (the fixture pack,
`tests/fixtures.h`) and `assets/glory-of-rome`, and have written their
scratch files under `build/`.

| Layer | Directory | Suite prefix | What it has covered |
|---|---|---|---|
| Unit | `tests/unit/` | `unit_` | One function or a small piece of state: combat maths, RNG, map, fog, tables, JSON, layout, touch, the flag list. |
| Regression | `tests/regression/` | `regression_` | Pinned outputs: the combat digests and the golden save `tests/fixtures/save_v1.dat`. |
| End to end | `tests/e2e/` | `e2e_` | Flows across systems: chests, contracts, the economy, magic rules, saves, Goto. |
| Autoplay | `tests/autoplay/` | `autoplay_` | The oracle's snapshot, rollback and recording plumbing. |

`tests/unit/test_legacy_freeze.c` has pinned the legacy presentation: a
change that has moved a legacy pixel has failed there.

## Adding a test

1. Write `TEST name(void) { ...; PASS(); }` in the layer's directory, in an
   existing file for that area or a new `test_<area>.c` ending in
   `SUITE(<layer>_<area>_suite) { RUN_TEST(name); }`.
2. A new suite has been registered twice in `tests/main.c`: a
   `SUITE_EXTERN(...)` line and a `RUN_SUITE(...)` line. The Makefile has
   picked up new files by wildcard.
3. `fx_init_game_full` (`tests/fixtures.h`) has given a seeded game on the
   fixture pack with its map and fog; `fx_free_game_full` has released it.
   Tests on The Glory of Rome have opened `assets/glory-of-rome` with
   `pack_open` and `pack_stack_push`, and popped it before any assert can
   fail.
4. A test writing a file has written it under `build/` and removed it.

## Before a pull request

| Change | Check |
|---|---|
| Any | `make`, `make test` |
| Gameplay rules | Autoplay on a few worlds of each pack: `./build/release/openbounty --pack <pack> --autoplay --headless --seed N` (exit 0 = solved); for a wider sweep, `--validate-pack LO HI` |
| The Glory of Rome screens | `make release`, then `./build/release/openbounty --pack glory-of-rome --gallery <dir>`: every screen to PNG, with the tap check (`[tapcheck] FAIL` has failed the run) |
| King's Bounty screens | The same gallery with `--pack kings-bounty`, and `test_legacy_freeze` |
| A Rome map | `make check-maps` (`scripts/check_maps.sh`): each `.dat` rebuilt from its `art/maps` source byte for byte, then `tools/mapbuild.py check` and `tools/mapcheck.py` on every zone |
| The save format | The round-trip suites and the golden fixture (`OPENBOUNTY-SPEC.md` §27) |

## Continuous integration

Every pull request to `staging` or `main`, and every push to `staging`, has
run `.github/workflows/ci.yml`:

- **linux:** `make test`, `make all release`, The Glory of Rome package with
  its pack check, the store-listing check, the docs' link check
  (`scripts/check_doc_links.py`), and `scripts/check_maps.sh` on every zone
  of The Glory of Rome.
- **windows**, **mac**, **web:** the cross-compiled, universal and WebAssembly
  builds.
- **android:** the APK, its contents checked, started on an emulator.
- **ios:** the Simulator app, installed and started on a booted Simulator;
  the check has failed unless the app is still running 30 seconds later.

with the **guard** check from `.github/workflows/attribution-guard.yml` on
the push and on the pull request: eight checks in all, each of which has had
to pass before a merge.
