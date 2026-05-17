# OpenBounty

A faithful raylib reimplementation of King's Bounty (1990, New World
Computing).

This document is a no-fluff dump of everything currently known: how the
game works, what it ships with, and how to build and run it.

---

## Releases

Pre-built binaries are published on the GitHub Releases page:

> https://github.com/danheskett/openbounty/releases

Three platforms are produced for every tagged release:

- **Linux x86_64** (tar.gz) — built on Ubuntu 22.04, glibc 2.35+; runs on
  most current desktop distros.
- **Windows x86_64 + i686** (zip) — single .exe, no installer, no DLLs.
- **macOS universal** (zip) — arm64 + x86_64. Ad-hoc signed; on first
  run, right-click → Open to bypass Gatekeeper, or run
  `xattr -dr com.apple.quarantine ./openbounty`.

OpenBounty ships **without game data**. To play, supply your own asset
pack from a legally-owned copy of the King's Bounty DOS distribution:

```sh
./openbounty --extract /path/to/KB.EXE
```

This produces a `kings-bounty.openbounty` pack file alongside the
binary, which the engine picks up on next launch.

Releases are sequential build numbers: `v1`, `v2`, `v3`, etc. Tag
`v3` and the workflow builds and publishes the binaries; the build
number reaches the user via the archive filenames
(`openbounty-build-3-...`) and the `--version` output:

```sh
./openbounty --version       # → openbounty build 3
```

For untagged dev builds, the build number comes from the repo-root
`VERSION` file (auto-bumped by the release workflow on every tag).
Override with `OPENBOUNTY_VERSION=N` on the make command line.

See [`docs/RELEASE-PROCESS.md`](docs/RELEASE-PROCESS.md) for the
maintainer's release procedure.

---

## 1. Repository layout

```
.
├── Makefile                  # Linux + Windows + macOS cross-compile targets
├── run.sh                    # `make && ./build/openbounty`
├── src/                      # All C source (single flat tree, no subdirs
│                             #   except src/screens/)
│   ├── main.c                # Bootstrap, main game loop, --extract path
│   ├── game.{c,h}            # Game state, init, salting, mechanics
│   ├── map.{c,h}             # Tile grid, .dat parsing, placement stamping
│   ├── fog.{c,h}             # Per-tile visibility (per continent)
│   ├── adventure.{c,h}       # Walkability + tile-step interact dispatch
│   ├── step.{c,h}            # `step_try` -- one-tile movement + bookkeeping
│   ├── flows.{c,h}           # Encounter / week-end / endgame entry points
│   ├── savegame.{c,h}        # JSON save (read/write)
│   ├── savepath.{c,h}        # OS-aware save dir resolution
│   ├── state_serialize.{c,h} # Full-state JSON snapshot (recorder + tests)
│   ├── tables.{c,h}          # Lookup helpers (troops/spells/artifacts/etc.)
│   ├── resources.{c,h}       # game.json parser + ResZone/ResTown/etc.
│   ├── tile.{c,h}            # Terrain + interactive enums
│   ├── tile_cache.{c,h}      # Texture cache for map tiles
│   ├── sprites.{c,h}         # Sprite sheet loading
│   ├── views.{c,h}           # Town / overlay view-stack manager
│   ├── views_render.{c,h}    # Per-view rendering
│   ├── overlay.{c,h}         # Options / controls / character overlays
│   ├── hud.{c,h}             # Sidebar HUD (gold, contract, magic icons)
│   ├── chrome.{c,h}          # Outerworld frame + status bar
│   ├── map_render.{c,h}      # 5x5 tile viewport with hero centered
│   ├── layout.h              # Pixel constants (320x200 design space)
│   ├── palette.{c,h}         # 256-color VGA palette
│   ├── bfont.{c,h}           # 8x8 bitmap font
│   ├── ui.{c,h}              # Bottom-frame dialog primitives
│   ├── prompt.{c,h}          # Bottom-frame yes-no / numeric / text input
│   ├── input.{c,h}           # Overworld keybindings + gamepad mapping
│   ├── startup.{c,h}         # Splash -> title -> credits -> class -> name
│   ├── end_cartoon.{c,h}     # Victory cartoon
│   ├── pending.{c,h}         # Queued banner / one-shot dialog state
│   ├── spells_adventure.{c,h}# Out-of-combat spell effects
│   ├── combat.{c,h}          # Combat module (turn loop, AI, spells, RNG)
│   ├── combat_render.{c,h}   # Combat field renderer
│   ├── pack.{c,h}            # Asset-pack open / read (zip + loose tree)
│   ├── pack_select.{c,h}     # Pack picker UI
│   ├── assets.{c,h}          # File-or-embedded asset loader
│   ├── audio.{c,h}           # Music + sfx mixer
│   ├── screenshot.{c,h}      # Backtick screenshot helper
│   ├── recorder.{c,h}        # State+frame ring for tests / replay
│   ├── harness.{c,h}         # Headless test harness (Unix socket protocol)
│   ├── harness_input.{c,h}   # Input shim that swaps raylib for harness
│   ├── encode_dialog.{c,h}   # MP4 record dialog
│   ├── encode_mp4*.c         # H.264 encoder + MP4 muxer
│   ├── fatal.{c,h}           # User-friendly fatal-error dialog
│   └── screens/              # Screen-flow modules (one per location/dialog)
│       ├── home_castle.{c,h}
│       ├── own_castle.{c,h}
│       ├── recruit_soldiers.{c,h}
│       ├── dwelling.{c,h}
│       ├── alcove.{c,h}
│       └── end_game.{c,h}
├── assets/kings-bounty/
│   ├── game.json             # All gameplay data (see §3)
│   ├── data/palette.bin      # 768-byte VGA palette
│   ├── audio/                # OGG music + WAV sfx
│   ├── art/                  # Sprites, tiles, fonts, UI chrome
│   └── maps/*.dat            # ASCII tile-code map files
├── third_party/
│   ├── cjson/                # cJSON library (vendored)
│   ├── miniz/                # ZIP read/write (for .openbounty packs)
│   ├── greatest/             # Single-header test framework
│   ├── minih264 / minimp4/   # MP4 record encoder
│   ├── raylib/               # raylib source (for reference)
│   └── raylib-install*/      # Prebuilt raylib for linux + win64 + win32 + mac
├── tools/                    # C-only asset extraction + test tooling
│   ├── extract*.c            # Pack extractor (KB.EXE -> .openbounty)
│   ├── playtest.c            # Scenario runner against the harness
│   └── scenarios/*.json      # Scripted test scenarios
├── tests/unit/               # Unit tests (greatest framework)
├── scripts/                  # raylib build scripts + combat regression runner
├── dist/                     # Release-staging templates
├── legacy/bin/               # Default extractor input dir (DOS distribution)
├── screenshots/              # Manual backtick captures
└── docs/                     # Internal development notes (specs, plans)
```

---

## 2. Build environment

### Linux (development)

Requirements:

- `gcc` (C99)
- The vendored raylib at `third_party/raylib-install/` (header in
  `include/`, static lib in `lib/`)
- System libs: `pthread`, `dl`, `rt`, `X11`, `m`

Compile flags: `-std=c99 -Wall -Wextra -O2`

```
make            # builds build/openbounty (assets read from disk)
make run        # build + run
make release    # builds build/openbounty-release with assets embedded
make run-release
make clean      # rm -rf build
```

### Windows (cross-compile from Linux)

Requirements:

- `x86_64-w64-mingw32-gcc` and `i686-w64-mingw32-gcc` toolchains
- Prebuilt raylib at `third_party/raylib-install-win64/` and
  `third_party/raylib-install-win32/`

```
make windows    # builds build/openbounty-x64.exe and build/openbounty-x86.exe
```

Windows builds always have `EMBED_ASSETS` defined, so the resulting
single `.exe` is self-contained (no external asset directory needed).
They link statically (`-static -static-libgcc -mwindows`) — no DLLs.

### Embedded-asset mode

`scripts/embed_assets.sh` walks `assets/` and emits `build/embedded.c` +
`build/embedded.h` containing every file as a byte array plus a path →
data lookup table. When compiled with `-DEMBED_ASSETS`, the asset loader
(`src/assets.c`) reads from these tables instead of the filesystem.
Used for both the Linux release binary and all Windows builds.

---

## 3. Command-line flags

The build produces four primary binaries. Each has its own CLI surface.

### `build/openbounty` — the game

The game binary covers normal play plus a few build-time / data-prep
modes (extractor, pack builder, headless combat digest).

| Flag | Argument | Effect |
|---|---|---|
| `--version`, `-v` | — | Print `openbounty build <N>` and exit. |
| `--help`, `-h` | — | Print usage and exit. |
| `--fullscreen` | — | Toggle fullscreen after window creation. |
| `--pack` | `<name\|path>` | Select a pack: bare name resolves via pack discovery, path opens a `.openbounty` zip or directory containing `game.json`. |
| `--save-dir` | `<dir>` | Override the user save directory (where saves and discovered packs live). |
| `--seed` | `N` | Force a deterministic per-game RNG seed. Default derives from time + name + class. Useful for reproducible runs. |
| `--record` | `<dir>` | Recorder dumps captures (state JSON + framebuffer PNG) to `<dir>`. Without this flag captures stay in memory only. |
| `--record-cap` | `N` | Override the recorder's in-memory ring-buffer size. |
| `--encode-movie` | — | After the session ends, mux captured frames into an MP4 (uses `<record-dir>`). |
| `--extract` | — | Build an asset pack from a user's DOS distribution and exit. Input: `legacy/bin/KB.EXE` if present, else `./KB.EXE`. Output: `<user-data>/openbounty/<pack_id>.openbounty`. |
| `--out-dir` | `<dir>` | Modifier for `--extract`: emit a loose asset tree to `<dir>` instead of a zip. |
| `--pack-dir` | `<src> <dst>` | Zip a pre-extracted asset tree into a `.openbounty` archive. Used by the Makefile to build the shipped pack. |
| `--combat-test` | `SEED:ATTACKER:N:DEFENDER:N:ROUNDS` | Headless combat-digest mode. Loads resources, runs a deterministic scripted battle, prints a 64-bit hex digest, exits. No window. Same formula the in-binary `combat_digests_suite` tests pin in CI. |

Normal play: `./build/openbounty` (no flags).

### `build/openbounty-test` — test runner

Driven by the [greatest](https://github.com/silentbicycle/greatest)
single-header framework. 171 tests across 27 suites: unit tests for
engine state/AI/combat/map/fog/save/etc., plus the combat-formula
golden-digest suite.

| Flag | Argument | Effect |
|---|---|---|
| `-h`, `--help` | — | Print usage and exit. |
| `-l` | — | List all suites and tests (dry run), then exit. |
| `-f` | — | Stop the runner after the first failure. |
| `-a` | — | Abort on first failure (implies `-f`). |
| `-v` | — | Verbose output (per-test status, timings). |
| `-s` | `SUITE` | Only run suites whose name contains the substring. |
| `-t` | `TEST` | Only run tests whose name contains the substring. |
| `-e` | — | Require exact-name match for `-s` / `-t` instead of substring. |
| `-x` | `EXCLUDE` | Exclude tests whose name contains the substring. |

Examples:
```
./build/openbounty-test                     # run everything
./build/openbounty-test -l                  # list tests
./build/openbounty-test -s combat_rng       # one suite
./build/openbounty-test -t damage -v        # one test, verbose
./build/openbounty-test -s combat_digests   # just the formula goldens
./build/openbounty-test -f                  # stop on first failure
```

### `build/openbounty-engplay` — headless playtest driver

Drives the engine via direct function calls — no window, no audio, no
60 fps wallclock pacing. Links the raylib stub so no graphics deps are
needed at runtime.

| Flag | Argument | Effect |
|---|---|---|
| `-h`, `--help` | — | Print usage and exit. |
| `--pack` | `<dir>` | Pack directory. Default `assets/kings-bounty`. |
| `--seed` | `N` | Deterministic seed. Default 42. |
| `--class` | `0..3` | 0=knight, 1=paladin, 2=sorceress, 3=barbarian. Default 0. |
| `--name` | `<s>` | Hero name. Default `Bot`. |
| `--difficulty` | `0..3` | 0=easy, 1=normal, 2=hard, 3=impossible. Default 1. |
| `--steps` | `DIR:N` | Walk N tiles in direction DIR. DIR is one of `U`, `D`, `L`, `R`, `NE`, `NW`, `SE`, `SW`. May repeat to chain batches. |
| `--dump` | — | Print full state JSON on stdout at end. |
| `--quiet` | — | Suppress per-step progress on stderr. |

Examples:
```
./build/openbounty-engplay                                       # init game, do nothing
./build/openbounty-engplay --steps R:10 --steps D:5 --dump       # walk and dump state
./build/openbounty-engplay --seed 100 --class 2 --name Mage      # different setup
```

### Library boundary check (no binary emitted)

`make all` runs a compile-only verification: `tests/library/consumer.c`
+ `engine/host_noop.c` + `libobengine.a` are linked with **only**
`-Iengine/headless -Iengine/include` (no `-Isrc`) and **only**
`-lm -lpthread` (no raylib, no X11). The output binary is discarded;
`build/libtest-pass.stamp` is touched on success. If the engine starts
depending on shell headers or shell symbols, this build step fails and
`make all` fails. There is no runtime test binary.

---

### Recap of make targets

| Target | What it does |
|---|---|
| `make` / `make all` | Builds all three binaries (`openbounty`, `openbounty-test`, `openbounty-engplay`) + `libobengine.a` + library boundary check + pack zips. |
| `make test` | Runs `build/openbounty-test`: 171 tests via greatest, including the combat-formula golden digests. |
| `make release` | `build/openbounty-release`, `-O2` stripped. |
| `make windows` | Cross-compile `openbounty-x64.exe` and `openbounty-x86.exe`. |
| `make windows-debug` | Same with console attached for stderr. |
| `make mac` | Cross-compile `openbounty-mac` (universal binary). |
| `make extract` | Wrapper for `./build/openbounty --extract`. |
| `make extract-pack` | Regenerates `assets/kings-bounty/` from a user's DOS files. |
| `make dist-{linux,windows,mac}` | Build distribution archives. |
| `make clean` | Removes `build/` and `dist/` archives. |

---

## 4. Runtime data

### Window and rendering

- Window: 960×600 base (`window.base_w/base_h` in game.json), resizable.
- Internal render target: **320×200** (`CL_SCREEN_W/H`, the original
  VGA mode). The render target is integer-scaled to fit the window
  preserving aspect ratio (minimum scale 2×).
- Fullscreen toggle: Alt+Enter.
- 256-color palette loaded from `assets/kings-bounty/data/palette.bin`
  (extracted from the original DOS MCGA.DRV).
- Font: `kb-font.png` (8×8 KB-style bitmap, `bfont.c`).
- All map tiles are 48×34 pixels.
- Sprites are 48×34, except hero/troop frame cycles which are
  `<name>_00..03.png`.

### Save files

- One save format: **JSON, version 6** (`SAVE_VERSION` in
  `src/savegame.h`). Catalog references use string IDs
  (e.g. `troop_id: "knights"`).
- Save slots: 10 (`SAVE_SLOT_COUNT`).
- Save filenames: `save_0.dat` … `save_9.dat`.
- Save directory:
  - **Linux**: `$XDG_DATA_HOME/openbounty/` if set, else
    `~/.local/share/openbounty/`. Created on first save.
  - **Windows**: `%APPDATA%\OpenBounty\`.
- Fog of war is encoded as 4 tiles per hex nibble (1 bit per tile).
- The scepter location is stored in plaintext.

### Asset paths

Assets are located via `LoadAsset*` functions in `src/assets.c`. The
canonical asset prefix is `assets/kings-bounty/`. In embedded mode,
relative paths look the same — they're matched against the embedded
table.

---

## 5. Game configuration (`assets/kings-bounty/game.json`)

All gameplay data is JSON-driven. Top-level keys:

| Key | Contents |
|---|---|
| `title` | "King's Bounty" |
| `version` | 1 |
| `window` | base window size: 960×600 |
| `world` | global flags: `max_army_slots=5`, `fog_sight=3`, `starting_zone="continentia"`, `default_name="Hero"`, `default_options=[4,1,1,1,1,1]` |
| `time` | `day_steps=40`, `week_days=5`, `days_per_difficulty={easy:900, normal:600, hard:400, impossible:200}` |
| `economy` | `boat_cost_normal=500`, `boat_cost_cheap=100` (with anchor artifact), `siege_cost=3000`, `alcove_cost=5000` |
| `contract` | `cycle_length=5`, `initial_last_contract=4` |
| `combat` | `morale_chart` (5×5 table), `number_names` (6 quantifier strings) |
| `controls` | Names of the 6 controls-menu rows (delay/sounds/walk-beep/animation/army-size/CGA — the 6th is missing in our renderer, see §7 known gaps) |
| `credits` | Credits screen lines (currently just title + version per project decision) |
| `ending` | Victory cartoon parameters (tile paths, frame count, etc.) |
| `spawn` | Per-continent troop spawn tables (`troop_pool[4][N]` and `chance_curve[4][N]`) |
| `tile_codes` | Map of `0x00..0x7F` byte → `{name, terrain, blocks_foot, is_bridge, art}`. The `.dat` map files reference these by ASCII char. |
| `sprites` | Sprite sheet paths and frame counts |
| `strings` | Localized text (rank titles, ending text, audience dialogs, etc.) |
| `troops` | 25 troop definitions (id, name, HP, damage range, skill, recruit cost, growth, abilities, tier counts) |
| `spells` | 14 spell definitions (id, name, cost, kind: combat/adventure) |
| `artifacts` | 8 artifact definitions (id, name, power flag, effect text) |
| `villains` | 17 villain definitions (id, name, reward gold, army composition, zone) |
| `castles` | Castle catalog |
| `towns` | Town catalog |
| `zones` | 4 continents: `continentia`, `forestria`, `archipelia`, `saharia`, each with map path, dimensions, hero/home spawn, signs, towns, castles, chests, artifacts, dwellings, wandering_armies, salt budget. |

### Compile-time caps (in `src/game.h`)

- `GAME_NAME_LEN = 16`
- `GAME_ARMY_SLOTS = 5`
- `GAME_CONTINENTS = 4`
- `GAME_TOWNS = 26`
- `GAME_CASTLES = 26`
- `GAME_MAX_MUTATIONS = 64` (consumed tiles)
- `GAME_MAX_DWELLINGS = 64` (per-zone dwelling state rows)
- `GAME_MAX_PLACEMENTS = 128` (salt-time random placements)
- `GAME_MAX_FOES = 64`
- `CONTRACT_CYCLE_MAX = 8`

---

## 6. Game rules and mechanics

### Classes (4)

| ID | Name | Starting gold | Starting army |
|---|---|---|---|
| `knight` | Knight | 7500 | 20 Militia + 2 Archers |
| `paladin` | Paladin | 10000 | 20 Peasants + 20 Militia |
| `sorceress` | Sorceress | 10000 | 30 Peasants + 10 Sprites |
| `barbarian` | Barbarian | 7500 | 20 Wolves |

Each class has 4 ranks. Each rank defines `leadership`, `commission`,
`villains_needed`, `max_spells`, `spell_power`. Ranks are reached by
audience with the king after capturing the required number of villains.

### Difficulties

| ID | Days | Score multiplier |
|---|---|---|
| Easy | 900 | ×0.5 |
| Normal | 600 | ×1 |
| Hard | 400 | ×2 |
| Impossible? | 200 | ×4 |

Days tick down by 1 per `day_steps` (40) overworld steps. Weeks are
`week_days` (5) days. Day rollover advances `days_left`; week rollover
fires the end-of-week sequence (astrology + budget).

### Score formula

```
score = 500 × villains_caught + 250 × artifacts_found
       + 100 × castles_owned − followers_killed
       all multiplied by the difficulty modifier
```

Implemented in `GameComputeScore` (`src/game.c`).

### Movement

- One step per keypress (no auto-repeat).
- Arrow keys + numpad 1–9 + Home/End/PgUp/PgDn for 8-direction movement.
- Numpad 5 = "rest one day" (consumes a step, no movement).
- Walking on land, sailing in boat (water + bridges only), flying with
  Mount=Fly bypasses ground/water restrictions and skips interactive
  tiles.
- Stepping onto an interactive tile triggers its handler. Most interact
  tiles bounce the hero back to the previous square (castles, towns,
  some foes); chests / artifacts / signs / dwellings / orbs / navmaps /
  telecaves don't bounce.
- Desert tiles consume the entire day's step budget (one tile per day).

### Foes

- Foes occupy single tiles with a `wandering_army` sprite.
- Two flavors per `FoeState.friendly`:
  - **Friendly**: stepping on triggers a recruit dialog ("They offer to
    join your army for X gold").
  - **Hostile**: stepping on bounces back and prompts attack.
- Foe sources:
  - Static foes from each zone's `wandering_armies[]` JSON array,
    registered as hostile at GameInit.
  - Salted friendly foes from `salt_continent` placed on random chest
    slots.
- Hostile foes chase the hero each step (see "foes_follow" below).
- Friendly foes share the same follow logic (one foe table,
  classification by flag).

### foes_follow

Each overworld step calls `GameFoesFollow`. For every foe within 2
tiles of the hero's previous position (`last_x, last_y`):

1. Evaluate all 9 cells of the foe's 3×3 neighborhood.
2. Score each cell by Euclidean distance to the hero's previous
   position.
3. Non-center cells that are unwalkable (or another interactive) get a
   sentinel max-distance score so they're never picked.
4. The hero's current tile is *not* excluded — if the foe lands on the
   hero, that's the combat trigger.
5. Foe moves to the lowest-score cell.
6. If the chosen cell is the hero's tile, the function returns the
   foe's index so the caller fires the attack/recruit flow; otherwise
   the map tile is updated and the previous tile cleared.

### Salting (`salt_continent`)

At GameInit, for every continent:

1. Register every `wandering_armies[]` entry as a hostile foe with a
   rolled garrison (uses the per-continent tier spawn table).
2. Build a "barrel" of all chest slots in that zone.
3. Tag random barrel slots with the salt budget kinds: artifacts,
   navmaps, orbs, telecaves, dwellings, friendly foes.
4. For each tagged slot, create a `SaltedPlacement` (or `FoeState` for
   friendlies). Friendly foes use the same `g->foes[]` table as
   hostiles.
5. `MapLoadZoneWithPlacements` later stamps these onto the loaded map.

### Day/week tick (`GameOnStep` → `end_day`)

On each step:

1. If `time_stop > 0`, decrement and skip the day tick.
2. If terrain is desert, zero `steps_left_today`; else decrement.
3. While `steps_left_today <= 0`: run `end_day`. May fire week rollover.
4. Week rollover: pay commission, deduct upkeep + boat rental, repossess
   boat if can't pay, roll astrology troop, repopulate castles, grow
   enemy garrisons, grow dwellings.

### Spells

14 spells split into combat and adventure:

| Combat | Adventure |
|---|---|
| Clone (2000) | Bridge (100) |
| Teleport (500) | Time Stop (200) |
| Fireball (1500) | Find Villain (1000) |
| Lightning (500) | Castle Gate (1000) |
| Freeze (300) | Town Gate (500) |
| Resurrect (5000) | Instant Army (1000) |
| Turn Undead (2000) | Raise Control (500) |

Cast via `U` on the overworld (adventure spells only). Combat spells
fire from the in-combat spells menu.

Adventure spells implemented:

- **Bridge**: prompts for direction, places 2 bridge tiles on water.
- **Time Stop**: adds `spell_power × 10` steps (min 10) to `time_stop`.
  Steps during `time_stop` don't tick the day.
- **Find Villain**: marks the active villain's castle as known on map.
- **Castle Gate / Town Gate**: prompt for a visited castle/town,
  teleport hero there.
- **Instant Army**: spawns a troop slot.
- **Raise Control**: bumps `leadership_current`.

### Artifacts (8)

| ID | Effect |
|---|---|
| Sword of Prowess | +50% army damage |
| Shield of Protection | -25% army damage taken |
| Crown of Command | Doubles leadership |
| Articles of Nobility | Increases weekly commission |
| Amulet of Augmentation | Doubles spell power |
| Ring of Heroism | Doubles max spells carried |
| Book of Necros | Effect unconfirmed |
| Anchor of Admirability | Boat rental costs 100 instead of 500 |

Powers are applied on pickup in `GameClaimArtifact`. Querying with
`GameHasPower(g, ARTIFACT_POWER_*)` resolves the runtime effect (e.g.
`boat_cost`).

### Win and lose conditions

- **Win**: find the buried scepter (use Search on its tile). Triggers
  `show_win_game` (cartoon + win text dialog).
- **Lose**: `days_left` reaches 0. Triggers `show_lose_game`.

### Out-of-game flows

- **Audience with the King** (visit home castle): if villain count >=
  next rank's threshold, promote (currently auto-loops through
  multiple ranks). Else show
  "I can aid you better after you've captured N more villains."
- **Recruit at home castle**: per-troop quantity prompt with
  leadership / gold checks.
- **Garrison castle** (own non-home castle): swap troops between army
  and garrison.
- **Siege castle** (enemy): "Lay siege (y/n)?" -> dispatches to the
  combat module (`src/combat.c`).
- **Visit town**: A) New contract / B) Rent boat (500 / 1 week) /
  C) Gather information / D) Buy spell / E) Buy siege weapons (3000).
- **Visit dwelling**: numeric prompt for troop count, capped by
  population and player leadership.
- **Visit telecave**: teleports to paired telecave.
- **Visit alcove** (Aurange): pay 5000 gold to learn magic.
- **Read sign**: opens a flavor dialog.
- **Open chest**: rolls one of (gold-or-leadership / commission boost /
  spell power / max spells / new spell / empty); special outcomes for
  navmap / orb tiles.

---

## 7. Combat

Turn-based tactical combat on a 6×5 grid. Implemented in `src/combat.c`
and `src/combat_render.c`. Includes troop morale, melee and ranged
attacks, in-combat spells, AI movement and targeting, and pathfinding.

---

## 8. Keybindings

Adventure mode (overworld).

### Movement

| Key | Action |
|---|---|
| ↑ / Numpad 8 | Move N |
| ↓ / Numpad 2 | Move S |
| ← / Numpad 4 | Move W |
| → / Numpad 6 | Move E |
| Numpad 7 / Home | Move NW |
| Numpad 9 / PgUp | Move NE |
| Numpad 1 / End | Move SW |
| Numpad 3 / PgDn | Move SE |
| Numpad 5 | Rest one day in place |

### Views (toggle on/off)

| Key | View |
|---|---|
| `A` | Army (per-troop stats) |
| `V` | Character (sheet) |
| `I` | Contract |
| `P` | Puzzle (5×5 reveal grid) |
| `M` | Worldmap minimap |
| `C` | Controls (settings) |
| `O` | Options (keybind reference) |

### Actions

| Key | Action |
|---|---|
| `S` | Search current tile (10 days) |
| `U` | Use magic (cast adventure spell) |
| `W` | Wait until end of week |
| `F` | Fly (mount) |
| `L` | Land (dismount) |
| `D` | Dismiss army |
| `N` | New continent (sail) |

### Meta

| Key | Action |
|---|---|
| `Q` | Save and quit prompt |
| `Ctrl+Q` | Fast quit (no save) prompt |
| Alt+Enter | Toggle fullscreen |
| Backtick (\`) | Manual screenshot to `screenshots/shot_NNNN.png` |
| Esc | Dismiss view / cancel prompt |

### Dialog dismiss

| Key | Action |
|---|---|
| Any non-modifier key | Advance / dismiss |
| `Y` / `N` | Yes / No on yes-no prompts |
| `1`–`9` (or numpad) | Choice on numeric prompts |
| Enter / Space | Confirm |
| Esc | Cancel / dismiss |

---

## 9. UI conventions

### Adventure HUD

- **Top status bar** (yellow border, 320×?): `Options / Controls /
  Days Left:NNN`. Title text changes based on overlay.
- **Map viewport**: 5×5 tile (240×170) with hero centered. Camera
  clamps at zone edges.
- **Right sidebar**: 48px wide. Shows portrait, contract/siege/magic/
  puzzle icons, gold counter.
- **Bottom**: drops out for dialogs and prompts.

### Dialogs

Two flavors:

- **`open_dialog(header, body)`** (`src/ui.c`): bottom-frame box with
  optional header line + multi-line body. Dismissed by any key.
  Supports paginated body (split on form-feed `\f`).
- **`prompt_yes_no_open` / `prompt_numeric_open` / `prompt_text_input_open`**
  (`src/prompt.c`): block input until the prompt resolves;
  result dispatched via `pending_flow` state machine in `main.c`.

### View screens

Full-screen overlays with yellow border, drawn on top of the
overworld. Title bar currently reads `Press 'ESC' to exit`.

---

## 10. Map data format

Maps are ASCII files at `assets/kings-bounty/maps/<zone>.dat`.

- Lines starting with `#` are comments.
- Each subsequent line is one row (y=0 at top, y=63 at bottom).
- Each character is one tile, mapped via `tile_codes` in `game.json`
  (`0x20..0x7E` ASCII chars correspond to tile codes).
- Maps are 64×64 (width and height come from each zone's `width`/
  `height` JSON fields).

Tile codes carry: art name, terrain category (grass / forest /
mountain / water / desert), `blocks_foot` flag, `is_bridge` flag.
Walkability: terrain in {grass, desert} OR `is_bridge` OR
`interactive != INTERACT_NONE` (interactive tiles override terrain
blocking so the player can step on them to trigger interaction).

Interactive tiles (signs, towns, castles, chests, dwellings, foes,
artifacts, telecaves, navmaps, orbs) are *not* in the .dat file —
they're applied by `stamp_objects` and `stamp_placements` from the
zone's JSON arrays + the salt-time placements at zone load.

---

## 11. Random generation

- Seed: derived from system time at game creation; stored in the save.
- RNG: linear-congruential, `game_rng_next(min, max)` in `src/game.c`.
- Salting (artifacts, dwellings, navmaps, orbs, telecaves, friendly
  foes, villain placement, scepter location, dwelling troop kinds,
  town spell selection) all use this RNG, so a given seed produces a
  reproducible game.
- Salt budget per zone is configurable in `game.json` under each
  zone's `salt` block.

---

## 12. Tools (`tools/`)

C-only utilities. Build with `make tools` (or implicitly via `make
extract`).

| Binary           | Purpose |
|---|---|
| `extract`        | Reads a KB.EXE distribution and writes a complete `.openbounty` pack (palette, font, sprites, tiles, chrome, audio metadata, game.json). Invoked from the engine via `--extract`. |
| `playtest`       | Drives the engine through the harness socket using JSON scenarios under `tools/scenarios/`. |

Packing a loose asset tree into a `.openbounty` zip is done by the engine itself: `./build/openbounty --pack-dir <src> <out_zip>`.

The extractor is broken into one TU per pipeline stage
(`extract_unpack.c`, `extract_lzw.c`, `extract_vga.c`, `extract_png.c`,
`extract_chrome.c`, `extract_gamejson.c`) plus the dispatcher
(`extract.c`).

There is no Python in this project; the legacy Python scripts have all
been ported to C and folded into `extract`.

---

## 13. Where to look first

- **Adding a feature**: start in `src/game.c` for state changes,
  `src/main.c` for input/flow, `src/views.c` and `src/screens/` for
  screens.
- **Fixing a dialog text bug**: find the literal in `src/main.c` or
  `src/views.c`, or in `assets/kings-bounty/game.json` (`strings`
  section). User-facing text is loaded through `src/resources.c`, so
  most strings are pack-overridable.
- **Debugging movement**: `src/step.c step_try` is the entry point;
  `src/adventure.c adventure_walkable_*` controls what's passable.
- **Debugging an interact**: `src/adventure.c adventure_handle_interact`
  classifies the tile; `src/main.c` reads the result and dispatches.
- **Save format change**: `src/savegame.c` and bump `SAVE_VERSION`
  in `src/savegame.h`.
- **Adding a CLI flag**: `src/main.c main()` (currently takes none).
