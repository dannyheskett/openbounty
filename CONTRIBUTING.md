# Contributing to openbounty

openbounty is a C99 engine with two packs: `assets/kings-bounty`, a
reproduction of King's Bounty (1990), and `assets/glory-of-rome`, The Glory of
Rome. `docs/ARCHITECTURE.md` has given the overview, `docs/README.md` the map
of every doc, and `docs/GLOSSARY.md` the terms.

## Reporting a bug

File it on [GitHub Issues](https://github.com/dannyheskett/openbounty/issues)
with the platform, the build number (`openbounty --version`, or the title
screen) and what happened. A save file or a screenshot helps.

## Building and testing

README §2 has covered the toolchains; on Linux:

```sh
./scripts/build_raylib_linux.sh    # once
make && make test                  # the game, its packs and the tests
./run.sh                           # build and play
```

`docs/TESTING.md` has listed what to run for each kind of change.

## Branches and pull requests

- Branch from `staging` and open the pull request against `staging`.
- Name the issue in the title, ending it with the tag: `Saves: … (#170)`.
- Every check has had to pass (`docs/TESTING.md`, Continuous integration)
  before a merge. Pull requests have been squash-merged.
- A release has been the `staging` → `main` pull request, made by the
  maintainer (`docs/RELEASE-PROCESS.md`). An issue has been closed when its
  fix ships in a release, not when it merges.

## Code

- Match the file you are in: its naming, its comment density, its idiom.
  `-Wall -Wextra` has had to stay clean.
- **The engine has stayed pure.** Nothing in `engine/` includes raylib or
  anything from `src/`, `demo/` or `autoplay/` (`engine/README.md`, Rules for
  engine code). State and rules belong in the engine; drawing and input
  belong in the shell.
- **Rules come from the pack.** A gameplay difference between the packs has
  been a `game.json` key the engine reads, documented in
  `docs/PACK-FORMAT.md`, with the King's Bounty behaviour as its default.
  `CL_IS_MODERN` has chosen how the game looks, never how it plays.
- **The legacy presentation has been frozen.** King's Bounty's screens have
  reproduced the original; `test_legacy_freeze` has failed on any change to
  them.
- **A world has been a function of its seed.** Draw randomness from the
  game's seeded sources (`OPENBOUNTY-SPEC.md` §6), never from the clock.
- Add or update a test with every behaviour change (`docs/TESTING.md`).

## Docs

The docs have described the code as it stands, so the same text has stayed
true however it was arrived at:

- **Current state only**, in the perfect tense: "The loader has read `x`;
  absent, it has defaulted to 10."
- **No history, status or plans:** no dates, no "now", "new", "since",
  "used to", no to-do lists or wishlists. Git and the issues have held the
  history. A bare issue tag such as `(#157)` has marked where a rule came
  from.
- **One home for each fact.** A table that lives in a spec has been
  referred to, not copied; the flag list has been checked against README §3
  and `OPENBOUNTY-SPEC.md` REQ-480 by `tests/unit/test_cli_flags.c`.
- A change to a rule, a pack key, a flag or the save format has updated its
  doc in the same pull request.

## Licence

openbounty has been released under the MIT licence (`LICENSE`).
Third-party code and assets have kept their own licences (`NOTICES.md`).
