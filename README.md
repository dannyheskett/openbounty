# OpenBounty

A faithful raylib reimplementation of King's Bounty (1990, New World
Computing), and the engine behind **Glory of Rome**, an original pack that
has run on desktop, the web, iOS and Android.

This document has described how the game works, what it ships with, and how
to build and run it.

---

## Releases

Pre-built binaries have been published on the GitHub Releases page:

> https://github.com/dannyheskett/openbounty/releases

Every release has carried two desktop products for each of Linux x86_64,
Windows x86_64 and i686, and macOS universal, plus the web and mobile builds
(`docs/RELEASE-PROCESS.md` has listed the file names):

- **OpenBounty** (`openbounty-build-N-*`): the engine alone, with **no game
  data**. It plays King's Bounty from a pack the player builds from a
  legally-owned copy of the DOS distribution: run `./openbounty --extract` in
  the directory that holds `KB.EXE`, and it writes
  `kings-bounty.openbounty` into the user data directory, where the next
  launch finds it.
- **Glory of Rome** (`gloryofrome-build-N-*`): the same binary with
  `assets/glory-of-rome.openbounty` beside it; it starts with no flags.
- **Web / WebAssembly**: two zips of `.html`/`.js`/`.wasm`/`.data`, served
  over HTTP, each with its game embedded: `openbounty-build-N-web-wasm.zip`
  (King's Bounty, danheskett.com/dist/openbounty/) and
  `gloryofrome-build-N-web-wasm.zip` (Glory of Rome,
  danheskett.com/dist/gloryofrome/).
- **iOS** (`.ipa`, also on TestFlight) and **Android** (sideload `.apk` and
  the Play `.aab`): Glory of Rome only.

The Linux build has been made on Ubuntu 22.04 (glibc 2.35+). The Windows
builds have been a single .exe with no installer and no DLLs. The macOS build
has been ad-hoc signed: on first run, right-click → Open to bypass
Gatekeeper, or run `xattr -dr com.apple.quarantine ./openbounty`.

Releases have been sequential build numbers under `release-N` tags. Every
push to `main` has run the release workflow, which has picked the next N,
built every target, and published them; the build number has reached the user
via the archive filenames and the `--version` output:

```sh
./openbounty --version       # → openbounty build 3
```

The number has been derived from the most recent `release-N` git tag (0 when
there are none). Override with `OPENBOUNTY_VERSION=N` on the make command
line. [`docs/RELEASE-PROCESS.md`](docs/RELEASE-PROCESS.md) has been the
maintainer's release procedure.

---

## 1. Repository layout

The codebase has split into **engine** (game logic, raylib-free, built as
`libobengine.a`) and **shell** (renderer, audio, input, screens, linking the
engine library). The two binaries (`openbounty` for play and
`openbounty-test` for tests) have both linked the same engine archive. The
shell has reached the platform only through five seams (`src/gfx.h`,
`src/frame_host.h`, `src/input_host.h`, `src/audio_backend.h`,
`src/font_backend.h`): raylib implements them everywhere except iOS, where
`ios/` implements them natively (`docs/IOS-BACKEND.md`).

```
.
├── Makefile                  # Every target: desktop, web, Android, iOS, dist
├── run.sh                    # `make && ./build/debug/openbounty`
├── art.html                  # Review page for the Rome art, refreshed from disk
│
├── engine/                   # ENGINE: pure game logic -> libobengine.a
│   ├── README.md             #   Architecture, file map, linking instructions
│   ├── include/              #   Public API (consumers add -Iengine/include)
│   ├── headless/             #   raylib stub headers for headless consumers
│   └── *.c                   #   game, map, fog, step, combat, flows, pack,
│                             #   savegame, resources, player_io, ...
│
├── src/                      # SHELL: renderer + audio + input + screens
│   ├── main.c                #   CLI parsing, init sequence, main loop
│   ├── shell_*.c             #   Main-loop pieces: actions, menu, prompts,
│   │                         #   cheats, gate, gallery, demo/autoplay adapters
│   ├── gfx_raylib.c, frame_host.c, input_host.c, audio_raylib.c, font_raylib.c
│   │                         #   The raylib side of the five seams
│   ├── layout.c, present.c   #   Buffer, scale and viewport (REQ-528)
│   ├── touch.c, textsel.c    #   Touch regions and the on-screen keyboard
│   ├── combat_loop.c, combat_render.c, combat_replay.c
│   ├── startup.c             #   Splash, title, credits, class, name
│   ├── plat_android.c, plat_ios.c, safe_area.c
│   ├── modern/               #   Modern-mode panels, menus and location screens
│   ├── legacy/               #   Legacy-mode draw layer, frozen
│   ├── screens/              #   Shared location-screen flows
│   └── recorder.c, encode_*.c  # --movie capture and the MP4 encoder
│
├── demo/                     # Demo mode, the human-like player (DEMO-SPEC)
├── autoplay/                 # Autoplay, the winnability oracle (AUTOPLAY-SPECS)
├── ios/                      # Native iOS backend: Metal, AVAudioEngine, UIKit
├── android/                  # Manifest, activity, icons, Play listing
├── web/shell.html            # The WebAssembly page template
│
├── assets/
│   ├── kings-bounty/         # The reference pack (extracted, never shipped)
│   └── glory-of-rome/        # The Glory of Rome pack
│
├── art/                      # Rome art sources: generation jobs, map sources,
│                             #   terrain primitives, references
├── tests/                    # unit/, e2e/, regression/, autoplay/, library/
├── third_party/              # cJSON, miniz, greatest, minih264, minimp4, stb,
│                             #   Liberation Sans; raylib-install*/ (built)
├── tools/                    # C pack extractor; Rome art and map tools (Python)
├── scripts/                  # raylib builds, store and release scripts
├── dist/                     # Release README templates
├── docs/                     # Specs and procedures (see below)
└── legacy/bin/               # Default extractor input (not in git)
```

`docs/`: `OPENBOUNTY-SPEC.md` (the reproduction-grade spec), `PACK-FORMAT.md`,
`MODERN-RESOLUTION.md`, `UI-PANELS.md`, `MENUS.md`, `ART-SPEC.md`,
`ART-PIPELINE.md`, `ART-WORKLIST.md`, `ROME-ART.md` (every prompt, generated),
`GLORY-OF-ROME.md`, `DEMO-SPEC.md`, `AUTOPLAY-SPECS.md`,
`AUTOPLAY-BASELINE.md`, `IOS-BACKEND.md`, `RELEASE-PROCESS.md`,
`STORE-SUBMISSION.md`, and `OPENKB-SPEC.md` (the predecessor project).

---

## 2. Build environment

raylib has not been committed. Each `scripts/build_raylib_<platform>.sh` has
cloned raylib (pinned to 6.0) and installed its headers and `libraylib.a`
into a gitignored `third_party/raylib-install*/` directory. Run the script
for your platform once on a fresh clone; CI has run them before every build.
Bump all five together when moving raylib versions.

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
make test       # build + run the test suite
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

They have linked statically (`-static -static-libgcc -mwindows`), no DLLs.
Like every desktop build, the `.exe` has read its pack from discovery (§4,
Asset paths).

### Web (WebAssembly, via Emscripten)

Requirements:

- The Emscripten SDK on `PATH` (`emcc`). Locally:
  `git clone https://github.com/emscripten-core/emsdk third_party/emsdk`,
  then `emsdk install latest && emsdk activate latest` and
  `source third_party/emsdk/emsdk_env.sh`. CI has used `setup-emsdk`.
- raylib built once: `./scripts/build_raylib_web.sh`

```
make web               # both packs -> build/web/<pack>/openbounty.{html,js,wasm,data}
make web-glory-of-rome # one pack only
make web-kings-bounty
make web-serve         # build both + serve on http://localhost:8080
```

A wasm module has embedded its pack, so there has been one build per pack,
each in its own `build/web/<pack>/`. The pack has gone into `openbounty.data`
via `--preload-file`, so each build has been self-contained. All four files
have been needed, served over HTTP: browsers refuse to fetch `.wasm`/`.data`
over `file://`. Saves have persisted in IndexedDB. `make web` has not been
part of `make dist`; `make dist-web` has packaged each build as its own zip,
`openbounty-*` for King's Bounty and `gloryofrome-*` for Glory of Rome, and
the site has served each at its own URL.

### Android

```
./scripts/build_raylib_android.sh --ndk <ndk>        # raylib 6.0, arm64-v8a static
make android ANDROID_NDK=<ndk> ANDROID_SDK_ROOT=<sdk>   # -> build/gloryofrome.apk
make android-play ANDROID_NDK=<ndk> ANDROID_SDK_ROOT=<sdk>  # -> the upload-signed .aab
```

The NDK and SDK paths have come only from the command line: the script has
taken `--ndk`, `--api` and `--arch`, and the Makefile has stopped with an
error when `ANDROID_NDK` or `ANDROID_SDK_ROOT` arrives through the
environment.

**Mobile has shipped Glory of Rome only** -- one pack, bundled in the APK's
`assets/`, opened by `src/plat_android.c` (`pack_open_mem`, so it has not
depended on raylib's `fopen` wrap, which miniz can bypass via `fopen64`).
There has been no pack discovery and no picker. Saves have gone to the app's
private directory.

The app has been a `NativeActivity` (no Gradle): `javac` -> `d8` -> `aapt` ->
`zipalign` -> `apksigner`, all from the Makefile. It has been
**landscape-locked**, and the display-cutout insets have been pushed from the
Activity into `src/safe_area.c`, which `present_scaled` has used to fit and
centre the game clear of the camera and the gesture bar. The system Back
gesture has answered wherever the shell reads Escape (`src/input_host.c`).

The launcher icon has been `android/res/mipmap-xxxhdpi/ic_launcher.png`,
composed from the pack's own title art by `tools/romeart.py icon`.

### iOS

```
make mac                                        # the pack tool for macOS
make ios-sim PACK_TOOL=build/openbounty-mac     # Simulator .app
make ios     PACK_TOOL=build/openbounty-mac     # device .ipa (signed when IOS_SIGN_IDENTITY is set)
```

iOS has drawn with its own Metal backend (`ios/gfx_metal.mm`) -- there has
been no raylib in the build -- behind the same seams every platform uses
(`src/gfx.h`, `src/frame_host.h`, `src/input_host.h`, `src/audio_backend.h`,
`src/font_backend.h`). The game has run on its own thread with its loops
intact; UIKit and a `CADisplayLink` have run on the main thread. `make` has
type-checked every iOS-bound shell file with `-DPLATFORM_IOS` and no raylib
include path (`build/ios-purity.stamp`), so the split has held on machines
without Xcode. See `docs/IOS-BACKEND.md`.

Like Android, iOS has shipped **Glory of Rome only**: the pack has been a
bundle resource opened with `pack_open_mem`, and saves have gone to the app's
`Documents/saves`. All iOS building has happened on GitHub's macOS runners.

### Assets at run time

Every build has read its game data from a pack -- a `.openbounty` zip, or a
loose directory holding `game.json` -- found by discovery (§4, Asset paths).
Engine-side byte reads have gone through `LoadAssetBytes`
(`engine/assets_bytes.c`) and shell-side texture loads through
`LoadAssetTexture` (`src/assets.c`); the engine has never touched a GPU
texture.

---

## 3. Command-line flags

The build has produced two binaries, each with its own CLI surface, the game
(`build/debug/openbounty`) and the test runner (`build/openbounty-test`), plus
a compile-only library boundary check that emits no binary.

### `build/debug/openbounty`, the game

The game binary has covered normal play plus the tool modes (extractor, pack
builder, gallery, demo, autoplay and pack validation).

| Flag | Argument | Effect |
|---|---|---|
| `--version`, `-v` | - | Has printed `openbounty build <N>` and exited. |
| `--help`, `-h` | - | Has printed usage and exited. |
| `--fullscreen` | - | Has toggled fullscreen after window creation. |
| `--pack` | `<name\|path>` | Has selected a pack: a bare name resolves via pack discovery, a path opens a `.openbounty` zip or a directory containing `game.json`. |
| `--lang` | `<code>` | Has loaded the pack's `strings/<code>.json` instead of its base language (`world.language`, default `en`), falling back to the base file when that one is absent. A missing key has been a hard error. |
| `--save-dir` | `<dir>` | Has overridden the user data directory (where saves and discovered packs live). |
| `--seed` | `N` | Has selected catalog world `N` (`0`–`255`) for a reproducible run. A pack has held 256 worlds; the index has been expanded to a full-width RNG seed internally (§11). Out of range, negative, or unparseable has been a hard error (exit 2): nothing runs on a misunderstood command line. Without `--seed`, a world has been derived from time + name + class. |
| `--movie` | `[<path>]` | Has recorded gameplay to an MP4: with no argument to `<user-data>/movie-<timestamp>.mp4`, with a path there. At shutdown an "Encoding…" dialog has run the muxer; intermediate per-tick frames have lived in a hidden temp dir, deleted afterward. The file is written only on a clean shutdown. |
| `--debug` | - | Has added the Debug page of cheats to the game menu. Without it no cheat has been reachable. |
| `--gallery` | `<dir>` | Has captured every modern screen to `<dir>/<name>.png` and exited: views, prompts, dialogs, town and castle pages, menus, combat. No input has been read. Each capture has also checked that a tap reaches the rows the screen drew; a failed check has printed `[tapcheck] FAIL` and made the exit code non-zero. |
| `--demo` | - | Demo mode: the human-like player agent (`demo/`, `docs/DEMO-SPEC.md`) has played the live game at a watchable pace. With `--headless`, it has played to an ending with no window and printed the `[DEMO OVER]` report (exit 0 = WON). |
| `--autoplay` | - | Autoplay, the pack-winnability oracle (`autoplay/`, `docs/AUTOPLAY-SPECS.md`). One mechanism: a single snapshot-tree search run once from boot, where a node is a reached world state and one expansion attempts exactly one objective. Greedy has not been a separate stage: it has been the tree's first descent. The expansion set has been bounded and declared (per-node branching cap, frontier beam, stagnation cut, runaway watchdog), so a run has ended when it commits a clear or exhausts that set. Visible mode has resolved headlessly then replayed on the live world. With `--headless`, it has driven to its verdict and printed `[VERDICT READY]` (exit 0 = SOLVED, 1 = NOT-SOLVED, 2 = setup failure). SOLVED has meant a full clear reached and committed; NOT-SOLVED has reported the best objective count reached and meant the declared expansion set ran out, not that the seed is unwinnable. |
| `--autoplay-hero` | `=<class>` | Modifier for `--autoplay` / `--validate-pack`: the class the oracle plays, by pack class id (default `knight`). A class the pack does not define has been a hard error. |
| `--autoplay-level` | `=<easy\|normal\|hard\|impossible>` | Modifier for `--autoplay` / `--validate-pack`: the difficulty the oracle plays (default `normal`). The level has set the day budget the run is proved against, via the pack's `time.days_per_difficulty`. |
| `--autoplay-speed` | `=<slow\|normal\|fast>` | Modifier for visible `--autoplay`: replay pacing (default `normal`). No effect headless. |
| `--validate-pack` | `[LO [HI]]` | Pack-author winnability report: the headless oracle has run over catalog worlds LO..HI (default `0..255`, the whole catalog), one seed at a time, and printed a table: per seed the verdict, objectives cleared, days, score, moves, and elapsed time, and on a miss the first objective that blocked it and why. A totals row has closed with `PASS`/`FAIL` and per-seed averages. Exit 0 only when every seed in the range has solved. |
| `--headless` | - | Modifier for `--demo` / `--autoplay`: no window has opened; the run has played to its ending and exited with the mode's verdict code. |
| `--verbose` | - | Has turned on the agent diagnostic channels (all at once; there has been no per-channel selection). Observation-only: a gated-off run has been bit-for-bit identical. |
| `--extract` | - | Has built an asset pack from a user's DOS distribution and exited. It has taken no path: input `legacy/bin/KB.EXE` if present, else `./KB.EXE`; output `<user-data>/<pack_id>.openbounty`. |
| `--out-dir` | `<dir>` | Modifier for `--extract`: a loose asset tree to `<dir>` instead of a zip. |
| `--pack-dir` | `<src> <dst>` | Has zipped a pre-extracted asset tree into a `.openbounty` archive. The Makefile has used it to build the shipped packs. |

Normal play: `./build/debug/openbounty` (no flags).

### `build/openbounty-test`, test runner

Driven by the [greatest](https://github.com/silentbicycle/greatest)
single-header framework: 371 tests across 54 suites, layered.

| Layer | Suites | Tests | What it has covered |
|---|---:|---:|---|
| **unit** (`tests/unit/`) | 40 | 281 | Single-function or small-scope state checks: combat math, RNG, map/fog/tile state, table lookups, JSON serialization, player-IO queue, layout, touch. |
| **regression** (`tests/regression/`) | 3 | 29 | Pinned golden outputs: combat-formula digests, save-file fixture round-trips. A failure has meant behavior changed; investigation decides intent vs bug. |
| **e2e** (`tests/e2e/`) | 9 | 55 | Multi-step flows across systems: game flow, chest, contract, economy, score, combat input, save round-trips, a pack with no content limits. |
| **autoplay** (`tests/autoplay/`) | 2 | 6 | The oracle's determinism plumbing: world snapshot/rollback bit-identity and the recording sink's fingerprints and mark/rollback. |

Suite names have carried their layer as a prefix (`unit_terrain_suite`,
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
./build/openbounty-test                       # run everything (371 tests)
./build/openbounty-test -s unit_              # 281 unit tests
./build/openbounty-test -s regression_        # 29 regression tests
./build/openbounty-test -s e2e_               # 55 e2e tests
./build/openbounty-test -s autoplay_          # 6 autoplay tests
./build/openbounty-test -s combat_            # all combat (any layer)
./build/openbounty-test -t damage -v          # one test, verbose
./build/openbounty-test -l                    # list everything
./build/openbounty-test -f                    # stop on first failure
```

### Library boundary check (no binary emitted)

`make all` has run a compile-only verification: `tests/library/consumer.c` +
`engine/host_noop.c` + `libobengine.a` linked with **only**
`-Iengine/headless -Iengine/include` (no `-Isrc`) and **only** `-lm
-lpthread` (no raylib, no X11). The output binary has been discarded;
`build/libtest-pass.stamp` has been touched on success. If the engine starts
depending on shell headers or shell symbols, this build step has failed and
`make all` has failed. There has been no runtime test binary.

---

### Recap of make targets

| Target | What it has done |
|---|---|
| `make` / `make all` | Built the two binaries (`openbounty`, `openbounty-test`) + `libobengine.a` + library boundary check + **iOS purity check** + pack zips. |
| `make test` | Run `build/openbounty-test`: 371 tests via greatest (unit, regression, e2e, autoplay), including the combat-formula golden digests. |
| `make release` | `build/release/openbounty`, `-O2` stripped. |
| `make windows` | Cross-compiled `openbounty-x64.exe` and `openbounty-x86.exe`. |
| `make windows-debug` | The same with a console attached for stderr. |
| `make mac` | `openbounty-mac` (universal binary, on macOS). |
| `make web` | WebAssembly build, one per pack -> `build/web/<pack>/openbounty.{html,js,wasm,data}` (needs emsdk). |
| `make web-serve` | Built both web builds + served them on `http://localhost:8080/<pack>/openbounty.html`. |
| `make android` / `make android-play` | Glory of Rome APK (`build/gloryofrome.apk`) / the upload-signed AAB (needs the Android NDK + SDK build-tools). |
| `make ios-sim` / `make ios` | Glory of Rome Simulator `.app` / device `.ipa` (on macOS). |
| `make extract` | Wrapper for `./build/debug/openbounty --extract`. |
| `make extract-pack` | Regenerated `assets/kings-bounty/` from a user's DOS files. |
| `make dist-{linux,windows,mac}` | OpenBounty distribution archives. |
| `make dist-rome-{linux,windows,mac}` | Glory of Rome distribution archives. |
| `make dist-web`, `make dist-android`, `make dist-android-play`, `make dist-ios` | The two web zips and the mobile packages in `dist/`. |
| `make clean` | Removed `build/` and `dist/` archives. |

---

## 4. Runtime data

### Window and rendering

- **Legacy mode** (King's Bounty): the render target has been **320×200**
  (`CL_SCREEN_W/H`, the original VGA mode), integer-scaled to fit the window
  preserving aspect ratio, minimum 2×. The base window has been 640×400
  (`CL_WINDOW_W/H` = `CL_SCREEN_W/H` × `CL_SCALE` in `src/layout.h`).
- **Modern mode** (Glory of Rome): the pack has declared a buffer (800×532
  for Rome) that is a floor, not a fixed size. The scale has been the largest
  whole number the surface can show -- there has been no zoom setting -- and
  the world map has spent what that leaves over in whole tiles; every other
  screen has kept the declared size, centred (REQ-528). The desktop window
  has opened at the declared buffer times the largest whole scale the
  monitor allows.
- The window has been resizable. Fullscreen toggle: Alt+Enter.
- 256-color palette loaded from the pack's `palettes/palette.bin` (for King's
  Bounty, extracted from the original DOS MCGA.DRV).
- King's Bounty's font has been `art/font/kb-font.png` (8×8 bitmap,
  `src/bfont.c`); Glory of Rome has declared a TrueType face
  (`docs/PACK-FORMAT.md` §2.2).
- King's Bounty's map tiles and sprites have been 48×34 pixels; Glory of
  Rome's have been 96×96 (`docs/ART-SPEC.md`).

### Save files

- One save format: **JSON, version 11** (`SAVE_VERSION` in
  `engine/include/savegame.h`). Catalog references have used string IDs
  (e.g. `troop_id: "knights"`). A catalog game has stored its world index
  (`seed_index`) rather than the expanded seed, so the world reloads exactly
  (§11).
- Save slots: 10 (`SAVE_SLOT_COUNT` in `engine/include/savepath.h`).
- Save files: `<user-data>/saves/<pack_id>/save_0.dat` … `save_9.dat`, one
  directory per pack (`engine/savepath.c`), where `<user-data>` has been:
  - **Linux**: `$XDG_DATA_HOME/openbounty` if set, else
    `~/.local/share/openbounty`.
  - **Windows**: `%APPDATA%\OpenBounty`.
  - **macOS**: `~/Library/Application Support/OpenBounty`.
  - **Web**: `/saves`, an IndexedDB-backed mount the page syncs.
- `--save-dir <dir>`, iOS (`Documents/saves`) and Android (the app's private
  `saves` directory) have used a flat directory with no per-pack level.
- Fog of war has been encoded as 4 tiles per hex nibble (1 bit per tile).
- The scepter location has been stored in plaintext.

### Asset paths

A game's data has been a **pack**: a `.openbounty` zip, or a directory
holding `game.json`. `--pack <name|path>` has named one; otherwise discovery
has walked, in order, the `.openbounty` zips in the working directory, in
`<user-data>`, and in `<exe>/assets/` (`engine/pack.c pack_discover`). One
pack found has opened directly; several have opened the pack picker; none has
fallen back to a first-run extraction from `KB.EXE` in `legacy/bin/` or the
working directory (`src/main.c`). Paths inside a pack have been relative to
its root and read through `LoadAssetBytes` / `LoadAssetTexture`.

---

## 5. Game configuration (`assets/kings-bounty/game.json`)

All gameplay data has been JSON-driven; `docs/PACK-FORMAT.md` has been the
full reference. King's Bounty's top-level keys:

| Key | Contents |
|---|---|
| `title` | "King's Bounty" |
| `version` | 1 (pack schema version) |
| `pack_id` / `pack_kind` / `pack_name` | Pack identity (e.g. `"kings-bounty"`, `"base"`), see `docs/PACK-FORMAT.md` |
| `world` | global flags: `language="en"`, `max_army_slots=5`, `starting_zone="continentia"`, `zone_noun`, `default_name="Hero"`, `default_options=[4,1,1,1,1]` |
| `time` | `day_steps=40`, `week_days=5`, `days_per_difficulty={easy:900, normal:600, hard:400, impossible:200}` |
| `economy` | `boat_cost_normal=500`, `boat_cost_cheap=100` (with anchor artifact), `siege_cost=3000`, `alcove_cost=5000` |
| `contract` | `cycle_length=5`, `initial_last_contract=4` |
| `combat` | `morale_chart` (5×5 table), `number_names` (6 quantifier strings) |
| `controls` | `settings[]`, the controls-menu rows (delay, sounds, walk_beep, animation, army_size, cga [hidden], music, volume), persisted per-game in `Game.stats.options[]` |
| `credits` | Credits screen: an optional `image`, `groups` of a label and its names, and `copyright` lines |
| `ending` | Victory cartoon parameters (tile paths, frame count, etc.) |
| `spawn` | Per-continent troop spawn tables (`troop_pool[4][N]` and `chance_curve[4][N]`) |
| `tile_codes` | Map of `0x00..0x7F` byte → `{name, terrain, blocks_foot, is_bridge, art}`. The `.dat` map files reference these by ASCII char. |
| `sprites` | Sprite sheet paths and frame counts |
| `troops` | 25 troop definitions (id, name, HP, damage range, skill, recruit cost, growth, abilities, tier counts) |
| `spells` | 14 spell definitions (id, name, cost, kind: combat/adventure) |
| `artifacts` | 8 artifact definitions (id, name, power flag, effect text) |
| `villains` | 17 villain definitions (id, name, reward gold, army composition, zone) |
| `castles` | Castle catalog |
| `towns` | Town catalog |
| `zones` | 4 continents: `continentia`, `forestria`, `archipelia`, `saharia`, each with map path, dimensions, hero/home spawn, signs, towns, castles, chests, artifacts, dwellings, wandering_armies, salt budget. |

Every user-visible string has lived beside `game.json` in `strings/en.json`
(rank titles, ending text, audience dialogs, etc.).

### Compile-time caps (in `engine/include/game.h`)

- `GAME_NAME_LEN = 16`
- `GAME_ARMY_SLOTS = 5`

Everything else -- towns, castles, zones, consumed tiles, dwellings,
placements, foes, the catalogs -- has been sized from the pack at load
(`tests/e2e/test_no_limits.c`).

---

## 6. Game rules and mechanics

The rules below have been King's Bounty's; Glory of Rome has renamed them and
kept the numbers (`docs/GLORY-OF-ROME.md`).

### Classes (4)

| ID | Name | Starting gold | Starting army |
|---|---|---|---|
| `knight` | Knight | 7500 | 20 Militia + 2 Archers |
| `paladin` | Paladin | 10000 | 20 Peasants + 20 Militia |
| `sorceress` | Sorceress | 10000 | 30 Peasants + 10 Sprites |
| `barbarian` | Barbarian | 7500 | 20 Wolves |

Each class has had 4 ranks. Each rank has defined `leadership`,
`commission`, `villains_needed`, `max_spells`, `spell_power`. Ranks have been
reached by audience with the king after capturing the required number of
villains.

### Difficulties

| ID | Days | Score multiplier |
|---|---|---|
| Easy | 900 | ×0.5 |
| Normal | 600 | ×1 |
| Hard | 400 | ×2 |
| Impossible | 200 | ×4 |

Days have ticked down by 1 per `day_steps` (40) overworld steps. Weeks have
been `week_days` (5) days. Day rollover has advanced `days_left`; week
rollover has fired the end-of-week sequence (astrology + budget).

### Score formula

```
score = 500 × villains_caught + 250 × artifacts_found
       + 100 × castles_owned − followers_killed
       all multiplied by the difficulty modifier, never below 0
```

Implemented in `GameComputeScore` (`engine/game.c`), from the pack's
`economy.scoring`.

### Movement

- One step per keypress (no auto-repeat).
- Arrow keys + numpad 1–9 + Home/End/PgUp/PgDn for 8-direction movement.
- Numpad 5 = "rest one day" (consumes a step, no movement).
- Walking on land, sailing in boat (water + bridges only), flying with
  Mount=Fly bypasses ground/water restrictions and skips interactive tiles.
- Stepping onto an interactive tile has triggered its handler. Most interact
  tiles have bounced the hero back to the previous square (castles, towns,
  some foes); chests / artifacts / signs / dwellings / orbs / navmaps /
  telecaves have not bounced.
- Desert tiles have consumed the entire day's step budget (one tile per day).

### Foes

- Foes have occupied single tiles with a `wandering_army` sprite.
- Two flavors per `FoeState.friendly`:
  - **Friendly**: stepping on has triggered a recruit dialog ("They offer to
    join your army for X gold").
  - **Hostile**: stepping on has bounced back and prompted attack.
- Foe sources:
  - Static foes from each zone's `wandering_armies[]` JSON array,
    registered as hostile at GameInit.
  - Salted friendly foes from `salt_continent` placed on random chest slots.
- Hostile foes have chased the hero each step (see "foes_follow" below).
- Friendly foes have shared the same follow logic (one foe table,
  classification by flag).

### foes_follow

Each overworld step has called `GameFoesFollow`. For every foe within 2
tiles of the hero's previous position (`last_x, last_y`):

1. Evaluate all 9 cells of the foe's 3×3 neighborhood.
2. Score each cell by Euclidean distance to the hero's previous position.
3. Non-center cells that are unwalkable (or another interactive) get a
   sentinel max-distance score so they're never picked.
4. The hero's current tile is *not* excluded: if the foe lands on the hero,
   that's the combat trigger.
5. The foe moves to the lowest-score cell.
6. If the chosen cell is the hero's tile, the function returns the foe's
   index so the caller fires the attack/recruit flow; otherwise the map tile
   is updated and the previous tile cleared.

### Salting (`salt_continent`)

At GameInit, for every continent, the engine has:

1. Registered every `wandering_armies[]` entry as a hostile foe with a
   rolled garrison (uses the per-continent tier spawn table).
2. Built a "barrel" of all chest slots in that zone.
3. Tagged random barrel slots with the salt budget kinds: artifacts, navmaps,
   orbs, telecaves, dwellings, friendly foes.
4. Created a `SaltedPlacement` (or `FoeState` for friendlies) for each tagged
   slot. Friendly foes have used the same `g->foes[]` table as hostiles.
5. Left `MapLoadZoneWithPlacements` to stamp these onto the loaded map.

### Day/week tick (`GameOnStep` → `end_day`)

On each step the engine has:

1. Decremented `time_stop` and skipped the day tick, if `time_stop > 0`.
2. Zeroed `steps_left_today` on desert terrain, else decremented it.
3. Run `end_day` while `steps_left_today <= 0`, which may fire week rollover.
4. At week rollover: paid commission, deducted upkeep + boat rental,
   repossessed the boat if unpaid, rolled the astrology troop, repopulated
   castles, grown enemy garrisons, grown dwellings.

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

Adventure spells have been cast via `U` on the overworld; combat spells have
fired from the in-combat spells menu.

Adventure spell effects:

- **Bridge**: prompts for direction, places 2 bridge tiles on water.
- **Time Stop**: adds `spell_power × 10` steps (min 10) to `time_stop`.
  Steps during `time_stop` don't tick the day.
- **Find Villain**: marks the active villain's castle as known on the map.
- **Castle Gate / Town Gate**: prompt for a visited castle/town, teleport
  the hero there.
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
| Book of Necros | No effect (the original's unimplemented slot, REQ-333) |
| Anchor of Admirability | Boat rental costs 100 instead of 500 |

Powers have been applied on pickup in `GameClaimArtifact`. Querying with
`GameHasPower(g, ARTIFACT_POWER_*)` has resolved the runtime effect (e.g.
`boat_cost`).

### Win and lose conditions

- **Win**: find the buried scepter (use Search on its tile). It has
  triggered `show_win_game` (cartoon + win text dialog).
- **Lose**: `days_left` reaches 0. It has triggered `show_lose_game`.

### Out-of-game flows

- **Audience with the King** (visit home castle): if the villain count has
  reached the next rank's threshold, the hero has been promoted, through as
  many ranks as the count reaches. Otherwise: "I can aid you better after
  you've captured N more villains."
- **Recruit at home castle**: per-troop quantity prompt with leadership /
  gold checks.
- **Garrison castle** (own non-home castle): swap troops between army and
  garrison.
- **Siege castle** (enemy): "Lay siege (y/n)?" -> dispatches to the combat
  module (`engine/combat.c` + `src/combat_loop.c`).
- **Visit town**: A) New contract / B) Rent boat (500 / 1 week) /
  C) Gather information / D) Buy spell / E) Buy siege weapons (3000).
- **Visit dwelling**: numeric prompt for troop count, capped by population
  and player leadership.
- **Visit telecave**: teleports to the paired telecave.
- **Visit alcove** (Aurange): pay 5000 gold to learn magic.
- **Read sign**: opens a flavor dialog.
- **Open chest**: rolls one of (gold-or-leadership / commission boost /
  spell power / max spells / new spell / empty); special outcomes for navmap
  / orb tiles.

---

## 7. Combat

Turn-based tactical combat on a 6×5 grid. Split:

- **Engine half** (`engine/combat.c`): state, AI, headless turn loop, damage
  formula, combat spells. No raylib. It has exposed the `Combat` struct,
  `combat_init`, `combat_ai_action`, `combat_run_headless`,
  `combat_test_digest`, the `spell_*` helpers.
- **Shell half** (`src/combat_loop.c`): `RunCombat` + modal player input +
  target picker + per-frame present. Uses raylib.
- **Renderer** (`src/combat_render.c`): draws the battlefield, log panel,
  banners.

Player input has included movement, wait/skip (Space/W), shoot (S), **fly
(F, only when the active unit has TROOP_ABIL_FLY)**, use magic (U), give up
(G), and the controls/options/army/character views. In modern mode every one
of them has also been a row in the combat menu (`docs/MENUS.md`), which a
shooter's opens on Shoot (REQ-533).

The combat-formula digests (25 golden cases) have lived in
`tests/regression/test_combat_digests.c` and run via `make test`.

---

## 8. Keybindings

Adventure mode (overworld). In modern mode every action has also been a menu
row or a tap (`docs/MENUS.md`).

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
| `O` | Options (legacy); the game menu (modern) |

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
| Esc | Dismiss view / cancel prompt; in modern mode, open the game menu |

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

Legacy mode's conventions follow; modern mode's panels have been in
`docs/UI-PANELS.md`.

### Adventure HUD

- **Top status bar**: `Options / Controls / Days Left:NNN`, swapped for
  `Press 'ESC' to exit` while a view or dialog is open.
- **Map viewport**: 5×5 tiles (240×170) with the hero centered. The camera
  has clamped at zone edges.
- **Right sidebar**: 48px wide. It has shown the portrait, the
  contract/siege/magic/puzzle icons and the gold counter.
- **Bottom**: drops out for dialogs and prompts.

### Dialogs

Two flavors:

- **`open_dialog(header, body)`** (`src/ui.c`): bottom-frame box with an
  optional header line + multi-line body, dismissed by any key. It has
  supported a paginated body (split on form-feed `\f`).
- **`prompt_yes_no_open` / `prompt_numeric_open` / `prompt_text_input_open`**
  (`src/prompt.c`): these have blocked input until the prompt resolves; the
  result has been dispatched via the `pending_flow` state machine in
  `main.c`.

### View screens

Full-screen overlays with a yellow border, drawn on top of the overworld.
The status bar has read `Press 'ESC' to exit`.

---

## 10. Map data format

Maps have been ASCII files at `maps/<zone>.dat` in the pack.

- Lines starting with `#` are comments.
- Each subsequent line is one row (y=0 at top).
- Each character is one tile, mapped via `tile_codes` in `game.json`
  (`0x20..0x7E` ASCII chars correspond to tile codes).
- Width and height have come from each zone's `width`/`height` JSON fields:
  64×64 for every King's Bounty zone; Glory of Rome's have been 64×128,
  64×64, 64×28 and 64×44.

Tile codes have carried: art name, terrain category (grass / forest /
mountain / water / desert), `blocks_foot` flag, `is_bridge` flag.
Walkability: terrain in {grass, desert} OR `is_bridge` OR
`interactive != INTERACT_NONE` (interactive tiles override terrain blocking
so the player can step on them to trigger interaction).

Interactive tiles (signs, towns, castles, chests, dwellings, foes,
artifacts, telecaves, navmaps, orbs) have not been in the .dat file:
`stamp_objects` and `stamp_placements` have applied them from the zone's
JSON arrays + the salt-time placements at zone load. Glory of Rome's `.dat`
files have been built from sources in `art/maps/` by `tools/mapbuild.py`.

---

## 11. Random generation

- World catalog: a pack has held 256 worlds, selected by an 8-bit index
  (`--seed 0`–`255`). Without `--seed` the index has been derived from
  system time + name + class. The index (not the expanded seed) has been
  what the save stores, so a world reloads exactly.
- Seed expansion: `GameSeedFromIndex` (`engine/game.c`) has avalanched the
  index into a full-width `uint64_t` before anything reads it. This has been
  load-bearing, not cosmetic: the engine consumes the seed at three widths,
  `(seed >> 8)` for spawn rolls (`engine/flows.c`), `(unsigned)` truncation
  for chest/dwelling/weekly-salt hashes, and all 64 bits in the world LCG,
  so a raw `0`–`255` seed would hand the first of those a constant 0 and
  leave the rest with 8 bits of entropy. Expanding first has kept all 256
  worlds distinct through every path.
- RNG: linear-congruential, `game_rng_next(min, max)` in `engine/game.c`.
- Salting (artifacts, dwellings, navmaps, orbs, telecaves, friendly foes,
  villain placement, scepter location, dwelling troop kinds, town spell
  selection) has all used this RNG, so a given index has produced a
  reproducible game.
- Catalog identity: a world has been a pure function of (index, the
  `GameSeedFromIndex` constants, the order of `game_rng_next` calls in
  `GameInit`, pack contents). Changing any of the last three re-maps every
  world and invalidates recorded per-world results.
- The salt budget per zone has been configurable in `game.json` under each
  zone's `salt` block.

---

## 12. Tools (`tools/`)

The pack extractor has been C and has compiled into the main `openbounty`
binary; `./build/debug/openbounty --extract` has been the invocation.

The extractor has read a KB.EXE distribution and written a complete
`.openbounty` pack (palette, font, sprites, tiles, chrome, audio metadata,
game.json). It has been broken into one translation unit per pipeline stage:
`extract_unpack.c`, `extract_lzw.c`, `extract_vga.c`, `extract_png.c`,
`extract_chrome.c`, `extract_gamejson.c`, plus the dispatcher `extract.c`.

Packing a loose asset tree into a `.openbounty` zip has been done by the
engine binary itself: `./build/debug/openbounty --pack-dir <src> <out_zip>`.

The game and its build have used no Python. The rest of `tools/` has been
the Glory of Rome authoring tools, which the build never runs: `romeart.py`
(tile compositing, the art record, the launcher icon); `mapbuild.py`,
`mapcheck.py` and `maprender.py` (maps); `classpicker.py`, `splashlogo.py`,
`splashtitle.py`, `siegewalls.py` and `siegeslice.py` (screen and combat
art); `loopreview.py` (animation review); and the paid-API drivers
`rdgen.py`, `pltileset.py` and `pltilespro.py`. `capture.sh` and `walkthrough.sh` have
driven a running window for screenshots; `detcheck.sh` has checked
determinism.

---

## 13. Where to look first

- **Adding a game-state feature**: `engine/game.c` (mechanics + RNG
  salting), `engine/include/game.h` (struct fields), and possibly the
  relevant flow handler in `src/shell_*.c`. State changes belong in the
  engine; UI flow belongs in the shell.
- **Fixing a dialog text bug**: the text has lived in the pack's
  `strings/en.json`, loaded via `engine/resources.c`; the engine has carried
  no text of its own.
- **Debugging movement**: `GameStep` (`engine/step.c`) has been the entry
  point; `engine/adventure.c adventure_walkable_*` has controlled what's
  passable.
- **Debugging an interact**: `engine/adventure.c adventure_handle_interact`
  has classified the tile; `engine/step.c` has read the result and
  dispatched to flows / screen openers (`engine/flows.c`, the host callbacks
  in `engine/include/ui_host.h`).
- **Save format change**: `engine/savegame.c`, and bump `SAVE_VERSION` in
  `engine/include/savegame.h`. Update or replace the golden
  `tests/fixtures/save_v1.dat`.
- **Adding a CLI flag**: `src/main.c main()` has parsed argv; for an
  early-exit mode use `src/shell_earlyexit.c`. §3 has documented the set.
- **Adding a combat ability/spell**: math in `engine/combat.c`, player input
  wiring in `src/combat_loop.c`. Add a golden digest in
  `tests/regression/test_combat_digests.c` if it changes the formula.
- **Adding an adventure spell**: implementation in
  `engine/spells_adventure.c`, dispatch in `dispatch_adventure_spell`.
  Modal continuations have routed through `engine/include/pending.h`.
