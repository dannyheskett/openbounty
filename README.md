# OpenBounty

A faithful raylib reimplementation of King's Bounty (1990, New World
Computing).

This document is a no-fluff dump of everything currently known: how the
game works, what it ships with, and how to build and run it.

---

## Releases

Pre-built binaries are published on the GitHub Releases page:

> https://github.com/dannyheskett/openbounty/releases

Four targets are produced for every release:

- **Linux x86_64** (tar.gz): built on Ubuntu 22.04, glibc 2.35+; runs on
  most current desktop distros.
- **Windows x86_64 + i686** (zip): single .exe, no installer, no DLLs.
- **macOS universal** (zip): arm64 + x86_64. Ad-hoc signed; on first
  run, right-click → Open to bypass Gatekeeper, or run
  `xattr -dr com.apple.quarantine ./openbounty`.
- **Web / WebAssembly** (zip): `.html`/`.js`/`.wasm`/`.data`, served over
  HTTP. Unlike the desktop archives this one embeds the asset pack, so it
  runs as-is. A hosted build is at <https://danheskett.com/dist/openbounty/>.

The desktop builds ship **without game data**. To play, supply your own
asset pack from a legally-owned copy of the King's Bounty DOS
distribution:

```sh
./openbounty --extract /path/to/KB.EXE
```

This produces a `kings-bounty.openbounty` pack file alongside the
binary, which the engine picks up on next launch.

Releases are sequential build numbers under `release-N` tags. Every push
to `main` runs the release workflow, which picks the next N, builds all
four targets, and publishes them; the build number reaches the user via
the archive filenames (`openbounty-build-3-...`) and the `--version`
output:

```sh
./openbounty --version       # → openbounty build 3
```

The number is derived from the most recent `release-N` git tag (0 when
there are none). Override with `OPENBOUNTY_VERSION=N` on the make
command line.

See [`docs/RELEASE-PROCESS.md`](docs/RELEASE-PROCESS.md) for the
maintainer's release procedure.

---

## 1. Repository layout

The codebase splits into **engine** (game logic, raylib-free, builds
as `libobengine.a`) and **shell** (renderer, audio, input, screens, links the engine library). The two binaries (`openbounty` for play
and `openbounty-test` for tests) both link the same engine archive.

```
.
├── Makefile                  # Linux + Windows + macOS cross-compile targets
├── run.sh                    # `make && ./build/debug/openbounty`
│
├── engine/                   # ENGINE: pure game logic, builds to libobengine.a
│   ├── README.md             # Architecture + linking instructions
│   ├── include/              # Public API (consumers add -Iengine/include)
│   │   ├── game.h            #   Game state, GameInit, mechanics
│   │   ├── map.h             #   Map struct, MapLoadZoneWithPlacements
│   │   ├── fog.h             #   Fog of war
│   │   ├── adventure.h       #   Walkability + tile-step interact dispatch
│   │   ├── step.h            #   `step_try`, one-tile movement + bookkeeping
│   │   ├── combat.h          #   Combat state + engine combat API
│   │   ├── flows.h           #   Encounter / week-end / endgame entry points
│   │   ├── savegame.h        #   JSON save read/write
│   │   ├── savepath.h        #   OS-aware save dir resolution
│   │   ├── state_serialize.h #   Full-state JSON snapshot
│   │   ├── tables.h          #   Troop/spell/artifact catalog lookups
│   │   ├── resources.h       #   game.json schema + ResZone/ResTown/...
│   │   ├── tile.h            #   Terrain + interactive enums
│   │   ├── pending.h         #   Deferred-action / continuation scratch
│   │   ├── spells_adventure.h#   Adventure spells (bridge/gate/find-villain…)
│   │   ├── assets_bytes.h    #   LoadAssetBytes (engine-side asset reads)
│   │   ├── end_screen.h      #   screen_end_game_open contract
│   │   ├── ui_host.h         #   Callbacks the SHELL must provide
│   │   ├── view_kind.h       #   ViewKind enum (engine/shell shared)
│   │   ├── dwelling_kind.h   #   DwellingKind enum (engine/shell shared)
│   │   └── fatal.h
│   ├── headless/             # raylib stub headers for headless consumers
│   │   ├── raylib.h          #   bridge: includes raylib_stub.h
│   │   ├── raylib_stub.h     #   no-op types/functions matching real raylib
│   │   └── input_keys.h      #   OB_KEY_* constants (raylib-compatible ints)
│   ├── game.c                # Game state, init, salting, mechanics
│   ├── map.c                 # Tile grid, .dat parsing, placement stamping
│   ├── fog.c                 # Per-tile visibility
│   ├── adventure.c           # Walkability + tile-step interact dispatch
│   ├── step.c                # `step_try`, one-tile movement
│   ├── combat.c              # Combat state, AI, headless turn loop, damage
│   ├── combat_log.c          # Combat log line append (pure data)
│   ├── flows.c               # Encounter / week-end / endgame flows
│   ├── savegame.c            # JSON save read/write
│   ├── savepath.c            # OS-aware save dir resolution
│   ├── state_serialize.c     # JSON snapshot builder
│   ├── tables.c              # Catalog lookups
│   ├── resources.c           # game.json parser
│   ├── tile.c                # Tile semantics
│   ├── pending.c             # Continuation state
│   ├── spells_adventure.c    # Adventure spell effects
│   ├── assets_bytes.c        # LoadAssetBytes / UnloadAssetBytes
│   ├── fatal.c               # Fatal-error helper
│   └── host_noop.c           # Default no-op host callbacks (for headless
│                             #   consumers that don't need real UI)
│
├── src/                      # SHELL: renderer + audio + input + screens
│   ├── main.c                # CLI parsing, init sequence, main loop skeleton
│   ├── shell_menu.{c,h}      # Game-menu Save/Load/New/Quit callbacks
│   ├── shell_tempdeath.{c,h} # Defeat / dismiss-last-army handler
│   ├── shell_weekend.{c,h}   # Astrology + budget end-of-week dialogs
│   ├── shell_audience.{c,h}  # King Maximus audience flow
│   ├── shell_cheats.{c,h}    # F10 debug cheat menu
│   ├── shell_fastquit.{c,h}  # Ctrl+Q status-bar prompt
│   ├── shell_frame.{c,h}     # Per-frame draw dispatcher
│   ├── shell_promptdispatch.{c,h} # Bottom-frame prompt-result router
│   ├── shell_actions.{c,h}   # Adventure-mode InputState.action dispatcher
│   ├── shell_earlyexit.{c,h} # --extract / --pack-dir CLI modes
│   ├── shell_ctx.h           # ShellCtx struct (pointer bundle for the above)
│   ├── combat_loop.{c,h}     # Rendered combat: RunCombat + modal input
│   ├── combat_render.{c,h}   # Combat field renderer
│   ├── views.{c,h}           # View-stack manager
│   ├── views_render.{c,h}    # Per-view rendering
│   ├── overlay.{c,h}         # Options / controls / character overlays
│   ├── hud.{c,h}             # Sidebar HUD
│   ├── chrome.{c,h}          # Outerworld frame + status bar
│   ├── map_render.{c,h}      # Map viewport
│   ├── layout.h              # Pixel constants (320x200 design space)
│   ├── palette.{c,h}         # 256-color VGA palette
│   ├── bfont.{c,h}           # 8x8 bitmap font
│   ├── ui.{c,h}              # Bottom-frame dialog primitives
│   ├── prompt.{c,h}          # Yes-no / numeric / text-input prompts
│   ├── input.{c,h}           # Overworld keybindings + gamepad mapping
│   ├── startup.{c,h}         # Splash → title → credits → class → name
│   ├── end_cartoon.{c,h}     # Victory cartoon
│   ├── pack_select.{c,h}     # Pack picker UI
│   ├── assets.{c,h}          # Shell-side texture loader (LoadAssetTexture)
│   ├── audio.{c,h}           # Music + sfx mixer
│   ├── screenshot.{c,h}      # Backtick screenshot helper
│   ├── sprites.{c,h}         # Sprite sheet loading
│   ├── tile_cache.{c,h}      # Texture cache for map tiles
│   ├── recorder.{c,h}        # --movie capture (state + framebuffer PNG/tick)
│   ├── encode_dialog.{c,h}   # MP4 progress dialog
│   ├── encode_mp4*.c         # H.264 encoder + MP4 muxer
│   └── screens/              # Screen-flow modules (location/dialog)
│       ├── home_castle.{c,h}
│       ├── own_castle.{c,h}
│       ├── recruit_soldiers.{c,h}
│       ├── dwelling.{c,h}
│       ├── alcove.{c,h}
│       └── end_game.{c,h}
│
├── assets/kings-bounty/
│   ├── game.json             # All gameplay data (see §3)
│   ├── data/palette.bin      # 768-byte VGA palette
│   ├── audio/                # OGG music + WAV sfx
│   ├── art/                  # Sprites, tiles, fonts, UI chrome
│   └── maps/*.dat            # ASCII tile-code map files
│
├── tests/                    # All tests + the library boundary check
│   ├── main.c                # Test runner entry (greatest)
│   ├── fixtures.{c,h}        # Shared init helpers
│   ├── stubs.c               # main.c stand-in symbols for the test binary
│   ├── combat_internal.h     # Legacy alias header (re-includes combat.h)
│   ├── unit/                 # 17 suites, 103 tests, single-function checks
│   ├── regression/           # 2 suites, 26 tests, combat-digest goldens
│   ├── e2e/                  # 8 suites, 42 tests, multi-step flows
│   ├── fixtures/save_v1.dat  # Golden save fixture
│   └── library/consumer.c    # Boundary-check minimal consumer (build-time)
│
├── third_party/
│   ├── cjson/                # cJSON library (vendored)
│   ├── miniz/                # ZIP read/write (for .openbounty packs)
│   ├── greatest/             # Single-header test framework
│   ├── minih264 / minimp4/   # MP4 record encoder
│   ├── raylib/               # raylib source (for reference)
│   └── raylib-install*/      # Prebuilt raylib for linux + win64 + win32 + mac
│
├── tools/                    # C-only asset extraction
│   └── extract*.c            # Pack extractor (KB.EXE → .openbounty)
├── tests/unit/               # Unit tests (greatest framework)
├── scripts/                  # raylib build scripts + combat regression runner
├── dist/                     # Release-staging templates
├── legacy/bin/               # Default extractor input dir (DOS distribution)
├── screenshots/              # Manual backtick captures
└── docs/                     # OPENBOUNTY-SPEC.md (reproduction-grade spec),
                              #   PACK-FORMAT.md, RELEASE-PROCESS.md, OPENKB-SPEC.md
```

---

## 2. Build environment

raylib is not committed. Each `scripts/build_raylib_<platform>.sh` clones
raylib (pinned to 6.0) and installs its headers and `libraylib.a` into a
gitignored `third_party/raylib-install*/` directory. Run the script for your
platform once on a fresh clone; CI runs them before every build. Bump all
five together when moving raylib versions.

### Linux (development)

Requirements:

- `gcc` (C99)
- raylib built once: `./scripts/build_raylib_linux.sh`
- System libs: `pthread`, `dl`, `rt`, `X11`, `m`

Compile flags: `-std=c99 -Wall -Wextra -O2`

```
./scripts/build_raylib_linux.sh   # once, on a fresh clone
make            # debug build -> build/debug/openbounty (-O0 -g)
make run        # build + run (debug)
make release    # optimized + portable build -> build/release/openbounty (-O2)
make run-release
make test       # build + run the test suite (only on demand)
make clean      # rm -rf build
```

### Windows (cross-compile from Linux)

Requirements:

- `x86_64-w64-mingw32-gcc` and `i686-w64-mingw32-gcc` toolchains
- raylib built once: `./scripts/build_raylib_windows.sh` (installs both
  `third_party/raylib-install-win64/` and `-win32/`)

```
make windows    # builds build/openbounty-x64.exe and build/openbounty-x86.exe
```

### Web (WebAssembly, via Emscripten)

Requirements:

- The Emscripten SDK on `PATH` (`emcc`). Locally:
  `git clone https://github.com/emscripten-core/emsdk third_party/emsdk`,
  then `emsdk install latest && emsdk activate latest` and
  `source third_party/emsdk/emsdk_env.sh`. CI uses `setup-emsdk`.
- raylib built once: `./scripts/build_raylib_web.sh`

```
make web        # -> build/web/openbounty.{html,js,wasm,data}
make web-serve  # build + serve on http://localhost:8080
```

The pack is embedded in `openbounty.data` via `--preload-file`, so the build
is self-contained. All four files are needed, and they must be served over
HTTP, browsers refuse to fetch `.wasm`/`.data` over `file://`. Saves persist
in IndexedDB. `make web` is not part of `make dist`; the release workflow
packages it separately via `make dist-web`.

Windows builds always have `EMBED_ASSETS` defined, so the resulting
single `.exe` is self-contained (no external asset directory needed).
They link statically (`-static -static-libgcc -mwindows`), no DLLs.

### Embedded-asset mode

`scripts/embed_assets.sh` walks `assets/` and emits `build/embedded.c` +
`build/embedded.h` containing every file as a byte array plus a path →
data lookup table. When compiled with `-DEMBED_ASSETS`, the asset loader
(`src/assets.c`) reads from these tables instead of the filesystem.
Used for both the Linux release binary and all Windows builds.

---

## 3. Command-line flags

The build produces two binaries, each with its own CLI surface, the
game (`build/debug/openbounty`) and the test runner (`build/openbounty-test`), plus a compile-only library boundary check that emits no binary.

### `build/debug/openbounty`, the game

The game binary covers normal play plus a few build-time / data-prep
modes (extractor, pack builder, headless combat digest).

| Flag | Argument | Effect |
|---|---|---|
| `--version`, `-v` | - | Print `openbounty build <N>` and exit. |
| `--help`, `-h` | - | Print usage and exit. |
| `--fullscreen` | - | Toggle fullscreen after window creation. |
| `--pack` | `<name\|path>` | Select a pack: bare name resolves via pack discovery, path opens a `.openbounty` zip or directory containing `game.json`. |
| `--save-dir` | `<dir>` | Override the user save directory (where saves and discovered packs live). |
| `--seed` | `N` | Select catalog world `N` (`0`–`255`) for a reproducible run. A pack holds 256 worlds; the index is expanded to a full-width RNG seed internally (§11). Out of range, negative, or unparseable is a hard error, nothing runs on a misunderstood command line. Without `--seed`, a world is derived from time + name + class. |
| `--movie` | `[<path>]` | Record gameplay to an MP4. With no argument, writes to `<user-data>/openbounty/movie-<timestamp>.mp4`. With a path, writes there. At shutdown an "Encoding…" dialog runs the muxer; intermediate per-tick frames live in a hidden temp dir, deleted afterward. |
| `--demo` | - | Demo mode, the human-like player agent (`demo/`, `docs/DEMO-SPEC.md`) plays the live game at a watchable pace. With `--headless`, plays to an ending with no window and prints the `[DEMO OVER]` report (exit 0 = WON). |
| `--autoplay` | - | Autoplay (the pack-winnability oracle (`autoplay/`, `docs/AUTOPLAY-SPECS.md`). One mechanism: a single snapshot-tree search run once from boot, where a node is a reached world state and one expansion attempts exactly one objective. Greedy is not a separate stage) it is the tree's first descent. The expansion set is bounded and declared (per-node branching cap, frontier beam, stagnation cut, runaway watchdog), so a run ends when it commits a clear or exhausts that set. Visible mode resolves headlessly then replays on the live world. With `--headless`, drives to its verdict and prints `[VERDICT READY]` (exit 0 = SOLVED, 1 = NOT-SOLVED, 2 = setup failure). SOLVED means a full clear was reached and committed; NOT-SOLVED reports the best objective count reached and means the declared expansion set ran out, it is not a proof of unwinnability. |
| `--autoplay-hero` | `=<class>` | Modifier for `--autoplay` / `--validate-pack`: the class the oracle plays, by pack class id (default `knight`). A class the pack does not define is a hard error. |
| `--autoplay-level` | `=<easy\|normal\|hard\|impossible>` | Modifier for `--autoplay` / `--validate-pack`: the difficulty the oracle plays (default `normal`). The level sets the day budget the run is proved against, via the pack's `time.days_per_difficulty`. |
| `--autoplay-speed` | `=<slow\|normal\|fast>` | Modifier for visible `--autoplay`: replay pacing (default `normal`). No effect headless. |
| `--validate-pack` | `[LO [HI]]` | Pack-author winnability report: run the headless oracle over catalog worlds LO..HI (default `0..255`, the whole catalog), one seed at a time, and print a table, per seed the verdict, objectives cleared, days, score, moves, and elapsed time, and on a miss the first objective that blocked it and why. A totals row closes with `PASS`/`FAIL` and per-seed averages. Exit 0 iff every seed in the range solved. |
| `--headless` | - | Modifier for `--demo` / `--autoplay`: no window opens; the run plays to its ending and exits with the mode's verdict code. |
| `--verbose` | - | Turn on the agent diagnostic channels (all at once; there is no per-channel selection). Observation-only: a gated-off run is bit-for-bit identical. |
| `--extract` | - | Build an asset pack from a user's DOS distribution and exit. Input: `legacy/bin/KB.EXE` if present, else `./KB.EXE`. Output: `<user-data>/openbounty/<pack_id>.openbounty`. |
| `--out-dir` | `<dir>` | Modifier for `--extract`: emit a loose asset tree to `<dir>` instead of a zip. |
| `--pack-dir` | `<src> <dst>` | Zip a pre-extracted asset tree into a `.openbounty` archive. Used by the Makefile to build the shipped pack. |

Normal play: `./build/debug/openbounty` (no flags).

### `build/openbounty-test`, test runner

Driven by the [greatest](https://github.com/silentbicycle/greatest)
single-header framework. 202 tests across 31 suites, layered:

| Layer | Suites | Tests | What it covers |
|---|---:|---:|---|
| **unit** (`tests/unit/`) | 19 | 122 | Single-function or small-scope state checks: combat math, RNG, map/fog/tile state, table lookups, JSON serialization, player-IO queue. |
| **regression** (`tests/regression/`) | 2 | 26 | Pinned golden outputs: combat-formula digests, save-file fixture round-trips. A failure means behavior changed; investigation decides intent vs bug. |
| **e2e** (`tests/e2e/`) | 8 | 49 | Multi-step flows across systems: game flow, chest, contract, economy, score, combat input, save round-trips. |
| **autoplay** (`tests/autoplay/`) | 2 | 5 | The oracle's determinism plumbing: world snapshot/rollback bit-identity and the recording sink's fingerprints and mark/rollback. |

Suite names carry their layer as a prefix (`unit_terrain_suite`,
`regression_combat_digests_suite`, `e2e_game_flow_suite`,
`autoplay_worldsnap_suite`), so greatest's `-s` substring filter selects a
layer.

| Flag | Argument | Effect |
|---|---|---|
| `-h`, `--help` | - | Print usage and exit. |
| `-l` | - | List all suites and tests (dry run), then exit. |
| `-f` | - | Stop the runner after the first failure. |
| `-a` | - | Abort on first failure (implies `-f`). |
| `-v` | - | Verbose output (per-test status, timings). |
| `-s` | `SUITE` | Only run suites whose name contains the substring. |
| `-t` | `TEST` | Only run tests whose name contains the substring. |
| `-e` | - | Require exact-name match for `-s` / `-t` instead of substring. |
| `-x` | `EXCLUDE` | Exclude tests whose name contains the substring. |

Examples:
```
./build/openbounty-test                       # run everything (202 tests)
./build/openbounty-test -s unit_              # 122 unit tests
./build/openbounty-test -s regression_        # 26 regression tests
./build/openbounty-test -s e2e_               # 49 e2e tests
./build/openbounty-test -s autoplay_          # 5 autoplay tests
./build/openbounty-test -s combat_            # all combat (any layer)
./build/openbounty-test -t damage -v          # one test, verbose
./build/openbounty-test -l                    # list everything
./build/openbounty-test -f                    # stop on first failure
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
| `make` / `make all` | Builds the two binaries (`openbounty`, `openbounty-test`) + `libobengine.a` + library boundary check + pack zips. |
| `make test` | Runs `build/openbounty-test`: 202 tests via greatest (unit, regression, e2e, autoplay), including the combat-formula golden digests. |
| `make release` | `build/release/openbounty`, `-O2` stripped. |
| `make windows` | Cross-compile `openbounty-x64.exe` and `openbounty-x86.exe`. |
| `make windows-debug` | Same with console attached for stderr. |
| `make mac` | Cross-compile `openbounty-mac` (universal binary). |
| `make web` | WebAssembly build -> `build/web/openbounty.{html,js,wasm,data}` (needs emsdk). |
| `make web-serve` | Build + serve the web build on `http://localhost:8080`. |
| `make extract` | Wrapper for `./build/debug/openbounty --extract`. |
| `make extract-pack` | Regenerates `assets/kings-bounty/` from a user's DOS files. |
| `make dist-{linux,windows,mac}` | Build distribution archives. |
| `make dist-web` | Zip the WASM bundle (not part of `make dist`, needs emsdk). |
| `make clean` | Removes `build/` and `dist/` archives. |

---

## 4. Runtime data

### Window and rendering

- Window: 640×400 base (`CL_WINDOW_W/H` = `CL_SCREEN_W/H` × `CL_SCALE`
  in `src/layout.h`), resizable. The base size is fixed in code, not a
  game.json field.
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

- One save format: **JSON, version 8** (`SAVE_VERSION` in
  `engine/include/savegame.h`). Catalog references use string IDs
  (e.g. `troop_id: "knights"`). A catalog game stores its world index
  (`seed_index`) rather than the expanded seed, so the world reloads exactly
  (§11).
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
relative paths look the same, they're matched against the embedded
table.

---

## 5. Game configuration (`assets/kings-bounty/game.json`)

All gameplay data is JSON-driven. Top-level keys:

| Key | Contents |
|---|---|
| `title` | "King's Bounty" |
| `version` | 1 (pack schema version) |
| `pack_id` / `pack_kind` / `pack_name` | Pack identity (e.g. `"kings-bounty"`, `"base"`), see `docs/PACK-FORMAT.md` |
| `world` | global flags: `max_army_slots=5`, `fog_sight=3`, `starting_zone="continentia"`, `default_name="Hero"`, `default_options=[4,1,1,1,1,1]` |
| `time` | `day_steps=40`, `week_days=5`, `days_per_difficulty={easy:900, normal:600, hard:400, impossible:200}` |
| `economy` | `boat_cost_normal=500`, `boat_cost_cheap=100` (with anchor artifact), `siege_cost=3000`, `alcove_cost=5000` |
| `contract` | `cycle_length=5`, `initial_last_contract=4` |
| `combat` | `morale_chart` (5×5 table), `number_names` (6 quantifier strings) |
| `controls` | `settings[]`, the controls-menu rows (delay, sounds, walk_beep, animation, army_size, cga [hidden], music, volume), persisted per-game in `Game.stats.options[]` |
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

### Compile-time caps (in `engine/include/game.h`)

- `GAME_NAME_LEN = 16`
- `GAME_ARMY_SLOTS = 5`
- `GAME_CONTINENTS = 4`
- `GAME_TOWNS = 26`
- `GAME_CASTLES = 26`
- `GAME_MAX_MUTATIONS = 1024` (consumed tiles, raised from 64 so a
  full four-continent playthrough doesn't overflow and respawn chests)
- `GAME_MAX_DWELLINGS = 64` (per-game dwelling state rows)
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

Implemented in `GameComputeScore` (`engine/game.c`).

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
4. The hero's current tile is *not* excluded, if the foe lands on the
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
  combat module (`engine/combat.c` + `src/combat_loop.c`).
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

Turn-based tactical combat on a 6×5 grid. Split:

- **Engine half** (`engine/combat.c`): state, AI, headless turn loop,
  damage formula, combat spells. No raylib. Exposes `Combat` struct,
  `combat_init`, `combat_ai_action`, `combat_run_headless`,
  `combat_test_digest`, the `spell_*` helpers.
- **Shell half** (`src/combat_loop.c`): `RunCombat` + modal player
  input + target picker + per-frame present. Uses raylib.
- **Renderer** (`src/combat_render.c`): draws the battlefield, log
  panel, banners.

Player input includes movement, wait/skip (Space/W), shoot (S),
**fly (F, only when active unit has TROOP_ABIL_FLY)**, use magic (U),
give up (G), open controls/options/army/character views.

The combat-formula digests (25 golden cases) live in
`tests/regression/test_combat_digests.c` and run via `make test`.

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
artifacts, telecaves, navmaps, orbs) are *not* in the .dat file, they're applied by `stamp_objects` and `stamp_placements` from the
zone's JSON arrays + the salt-time placements at zone load.

---

## 11. Random generation

- World catalog: a pack holds 256 worlds, selected by an 8-bit index
  (`--seed 0`–`255`). Without `--seed` the index is derived from system
  time + name + class. The index (not the expanded seed) is what the
  save stores, so a world reloads exactly.
- Seed expansion: `GameSeedFromIndex` (`engine/game.c`) avalanches the
  index into a full-width `uint64_t` before anything reads it. This is
  load-bearing, not cosmetic: the engine consumes the seed at three
  widths, `(seed >> 8)` for spawn rolls (`engine/flows.c`), `(unsigned)`
  truncation for chest/dwelling/weekly-salt hashes, and all 64 bits in the
  world LCG, so a raw `0`–`255` seed would hand the first of those a
  constant 0 and leave the rest with 8 bits of entropy. Expanding first
  keeps all 256 worlds distinct through every path.
- RNG: linear-congruential, `game_rng_next(min, max)` in `engine/game.c`.
- Salting (artifacts, dwellings, navmaps, orbs, telecaves, friendly
  foes, villain placement, scepter location, dwelling troop kinds,
  town spell selection) all use this RNG, so a given index produces a
  reproducible game.
- Catalog identity: a world is a pure function of (index, the
  `GameSeedFromIndex` constants, the order of `game_rng_next` calls in
  `GameInit`, pack contents). Changing any of the last three re-maps every
  world and invalidates recorded per-world results.
- Salt budget per zone is configurable in `game.json` under each
  zone's `salt` block.

---

## 12. Tools (`tools/`)

C-only asset extraction. The extractor source compiles into the main
`openbounty` binary; `./build/debug/openbounty --extract` is the invocation.

The extractor reads a KB.EXE distribution and writes a complete
`.openbounty` pack (palette, font, sprites, tiles, chrome, audio
metadata, game.json). It is broken into one translation unit per
pipeline stage: `extract_unpack.c`, `extract_lzw.c`, `extract_vga.c`,
`extract_png.c`, `extract_chrome.c`, `extract_gamejson.c`, plus the
dispatcher `extract.c`.

Packing a loose asset tree into a `.openbounty` zip is done by the
engine binary itself: `./build/debug/openbounty --pack-dir <src> <out_zip>`.

There is no Python in this project.

---

## 13. Where to look first

- **Adding a game-state feature**: `engine/game.c` (mechanics + RNG
  salting), `engine/include/game.h` (struct fields), and possibly the
  relevant flow handler in `src/shell_*.c`. State changes belong in
  the engine; UI flow belongs in the shell.
- **Fixing a dialog text bug**: most strings live in
  `assets/kings-bounty/game.json` (`strings` section). They're loaded
  via `engine/resources.c` and pack-overridable. Hard-coded literals
  may also exist in `src/shell_*.c` flow handlers.
- **Debugging movement**: `engine/step.c step_try` is the entry point;
  `engine/adventure.c adventure_walkable_*` controls what's passable.
- **Debugging an interact**: `engine/adventure.c adventure_handle_interact`
  classifies the tile; `engine/step.c` reads the result and dispatches
  to flows / screen openers (`engine/flows.c`, the host callbacks in
  `engine/include/ui_host.h`).
- **Save format change**: `engine/savegame.c` and bump `SAVE_VERSION`
  in `engine/include/savegame.h`. Update or replace the golden
  `tests/fixtures/save_v1.dat`.
- **Adding a CLI flag**: `src/main.c main()` parses argv; for an
  early-exit mode use `src/shell_earlyexit.c`. The current set is
  documented in §3.
- **Adding a combat ability/spell**: math in `engine/combat.c`,
  player input wiring in `src/combat_loop.c`. Add a golden digest in
  `tests/regression/test_combat_digests.c` if it changes the formula.
- **Adding an adventure spell**: implementation in
  `engine/spells_adventure.c`, dispatch in `dispatch_adventure_spell`.
  Modal continuations route through `engine/pending.h`.
