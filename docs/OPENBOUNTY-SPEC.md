# OpenBounty, Specification

Reproduction-grade record of the implementation as built. This has been the
single authoritative specification for the game rules and the implementation
together.

A complete reimplementation of OpenBounty has been possible from this document
alone, paired with the asset pack at `assets/kings-bounty/` (sprites, palette,
maps, audio).

OpenBounty has been a faithful raylib reimplementation of King's Bounty (1990,
New World Computing), and the engine behind Glory of Rome, an original modern
pack (`assets/glory-of-rome/`, `GLORY-OF-ROME.md`). It has descended from
**OpenKB** (an earlier SDL 1.2 reimplementation) and deliberately diverged from
it in several architectural respects (§1.10); gameplay-significant constants
and formulas have matched OpenKB except where a deviation is explicitly
flagged (§38).

**Conventions.**
- Each requirement has carried a stable identifier of the form `REQ-NNN`.
  Where a numeric value or table appears in the asset pack, the requirement
  has named the JSON path (e.g. `game.json:economy.chest.chance_gold`) rather
  than copying the value, so the data and the spec stay synchronised.
- Requirements have been written in the present perfect tense ("the game has
  done X"), as a factual reproduction-grade record.
- Code citations have named a **file and function** (e.g. `engine/game.c
  GameOnStep`) rather than a line number, so they survive edits. The code has
  lived under `engine/` (the raylib-free engine library) and `src/` (the
  shell); see §1.
- **Provenance citations have been a separate thing.** Comments in the source
  occasionally cite `play.c`, `bounty.c`, or a bare `game.c` with a line
  number. Those have named files in the **OpenKB source and the DOS
  decompilation this port has been derived from**, not files in this repository,
  and they have been recorded so a reader can trace where a formula or table
  came from. `OPENKB-SPEC.md` has documented that predecessor. Any citation
  naming a path under `engine/` or `src/` has referred to this repository.
- "The game" has referred to the running OpenBounty binary. "The player" has
  referred to the human user. "The hero" has referred to the in-world avatar.
- Coordinates have been written `(x, y)` with `x` increasing east and `y`
  increasing south; origin `(0, 0)` has sat at the top-left of every zone map.

**Out of scope of the requirements (documented for context):**
- Pixel-exact UI layout has not been mandated; the spec names panels, regions,
  and metrics, but small visual deltas have been considered conformant.
- Prompt-to-prompt input timing (debounce, key-repeat) has been left to the
  implementation.

---

## Table of contents

**Part I: Architecture & implementation**
1. [Top-level architecture](#1-top-level-architecture)
2. [Data types and conventions](#2-data-types-and-conventions)
3. [Global constants and limits](#3-global-constants-and-limits)
4. [The `Game` struct, complete game state](#4-the-game-struct--complete-game-state)

**Part II: Gameplay**
5. [Game model and lifecycle](#5-game-model-and-lifecycle)
6. [RNG and determinism](#6-rng-and-determinism)
7. [Time, days, and weeks](#7-time-days-and-weeks)
8. [Character, classes, and ranks](#8-character-classes-and-ranks)
9. [World, zones, and tiles](#9-world-zones-and-tiles)
10. [Salt: per-zone object placement](#10-salt-per-zone-object-placement)
11. [Movement, mounts, and travel](#11-movement-mounts-and-travel)
12. [Adventure-mode actions](#12-adventure-mode-actions)
13. [Troops and the army](#13-troops-and-the-army)
14. [Morale and out-of-control](#14-morale-and-out-of-control)
15. [Foes, encounters, and recruiting](#15-foes-encounters-and-recruiting)
16. [Towns](#16-towns)
17. [Castles](#17-castles)
18. [Dwellings](#18-dwellings)
19. [Spells: catalog and adventure casts](#19-spells-catalog-and-adventure-casts)
20. [Artifacts](#20-artifacts)
21. [Villains and contracts](#21-villains-and-contracts)
22. [Chests and rewards](#22-chests-and-rewards)
23. [Economy: gold, commission, upkeep](#23-economy-gold-commission-upkeep)
24. [Astrology and weekly events](#24-astrology-and-weekly-events)
25. [Combat](#25-combat)
26. [Scoring, victory, and defeat](#26-scoring-victory-and-defeat)

**Part III: Engine, save, UI, platform**
27. [Save format and slots](#27-save-format-and-slots)
28. [Resource system](#28-resource-system)
29. [UI: views, HUD, dialogs, prompts](#29-ui-views-hud-dialogs-prompts)
30. [Input and controls](#30-input-and-controls)
31. [Cheats and debug](#31-cheats-and-debug)
32. [Audio](#32-audio)
33. [Rendering](#33-rendering)
34. [CLI, packs, and platform](#34-cli-packs-and-platform)
35. [Recorder, encoder, and harness](#35-recorder-encoder-and-harness)
36. [Autoplay planner](#36-autoplay-planner)
37. [Tools, asset extraction](#37-tools--asset-extraction)

**Part IV: Deviations & data**
38. [Known deviations from OpenKB](#38-known-deviations-from-openkb)
- [Appendix A, Complete data tables (from `game.json`)](#appendix-a--complete-data-tables-from-gamejson)

---

# Part I, Architecture & implementation

## 1. Top-level architecture

### 1.1 The engine / shell split

- **REQ-001.** The codebase has split into two halves with a hard boundary:
  - **engine** (`engine/`): pure game logic, mechanics, and state. It has
    built as the static archive `libobengine.a` and has been **free of raylib,
    audio, window, and GPU dependencies**. It has vendored cJSON and miniz
    inside the archive. It has emitted abstract events to its host through the
    callbacks declared in `engine/include/ui_host.h`.
  - **shell** (`src/`): renderer, audio, input, and screen flows. It has
    linked `libobengine.a` and implemented the host callbacks for real
    (rendering prompts, dialogs, views, playing audio, capturing frames).
- **REQ-002.** The engine has exposed its public API through headers in
  `engine/include/`; consumers have added `-Iengine/include`. The shell's own headers
  have lived in `src/`. No engine `.c` file has included a `src/` header, and
  no engine `.c` file has included `raylib.h` outside `engine/headless/`.
- **REQ-003.** The boundary has been **verified at build time**, not merely by
  convention. `make all` has linked `tests/library/consumer.c` +
  `engine/host_noop.c` + the demo and autoplay objects + `libobengine.a`
  using only `-Iengine/headless -Iengine/include -Ithird_party/cjson` and
  only `-lm -lpthread` (no raylib, no X11), with `--whole-archive` so every
  engine object is pulled in. If any engine object has come to depend on a
  shell header or symbol, this link has failed and `make all` has failed. The
  same rule has first grepped the engine sources (less `host_noop.c`) for a
  direct `open_dialog(` call and failed on one. The output binary has been
  discarded; a stamp file (`build/libtest-pass.stamp`) has recorded success.
- **REQ-004.** Two real binaries have been produced from the same engine
  archive: `build/debug/openbounty` (the game; `build/release/openbounty`
  from `make release`) and `build/openbounty-test` (the greatest test
  runner, built by `make test`).

### 1.2 Process flow

- **REQ-010.** `main` (`src/main.c`) has parsed argv, resolved the active pack,
  loaded `Resources` from `game.json`, created the window and render target,
  loaded assets, and entered the macro-state machine (§5.2). Early-exit CLI
  modes have been handled before window creation: `--version` and `--help`
  inside the argv parse in `src/main.c`, `--extract` and `--pack-dir` in
  `src/shell_earlyexit.c`.
- **REQ-011.** Engine combat (state, AI, headless turn loop, damage formula,
  combat spells) has lived in `engine/combat.c`. The **rendered** combat loop
  (`RunCombat`, modal player input, target picker, per-frame present) has lived
  in `src/combat_loop.c` (shell). Both halves have shared the one `Combat`
  struct defined in `engine/include/combat.h`.

### 1.3 Build system

- **REQ-020.** The build has been a single hand-written `Makefile` (no CMake,
  no autotools). Compile flags: `-std=c99 -Wall -Wextra`, plus `-O0 -g` for
  the debug build (`make`) and `-O2 -DNDEBUG` for the release build. The
  engine archive has been compiled `-O2 -fPIC -DOB_HEADLESS` with
  `-Iengine/headless -Iengine/include` whichever build asked for it.
- **REQ-021.** Principal targets:
  - `make` / `make all`: the game binary + `libobengine.a` + the pack zips +
    the library boundary check + the iOS purity check + the touch guard and
    the page guard (two greps over the shell that fail the build when a file
    reads raw touch regions or draws a panel outside the page engine).
  - `make test` has run `build/openbounty-test`: the **entire** greatest suite
    (unit + regression + e2e + autoplay, including the combat-formula golden
    digests). There has been no separate playtest or scenario runner.
  - `make release`: `build/release/openbounty`, `-O2`, static libgcc.
  - `make windows` / `make windows-debug`: Win64 + Win32 cross-compile
    (mingw-w64), static link, no DLLs.
  - `make mac`: macOS universal (arm64 + x86_64).
  - `make web`: one WebAssembly bundle per pack.
  - `make android` / `make android-play`: the Glory of Rome APK / AAB.
  - `make ios-sim` / `make ios`: the Glory of Rome Simulator app / device
    `.ipa`.
  - `make extract` / `make extract-pack`: wrappers around
    `./build/debug/openbounty --extract`.
  - `make dist-{linux,windows,mac}` / `make dist`: OpenBounty archives;
    `make dist-rome-{linux,windows,mac}`: Glory of Rome archives;
    `make dist-web`, `dist-android`, `dist-android-play`, `dist-ios`.
  - `make clean`: removes `build/` and `dist/` archives.
- **REQ-022.** The game and its build have used **no Python**. Asset
  extraction and pack building have been pure C, compiled into the
  `openbounty` binary (§37). Python has appeared only in tools the build never
  runs: the Rome art and map authoring scripts in `tools/` and the
  store-upload scripts in `scripts/`.

### 1.4 Vendor code

- **REQ-030.** `third_party/` has vendored: cJSON (`cjson/`, JSON parse),
  miniz (`miniz/`, ZIP read/write for `.openbounty` packs), greatest
  (`greatest/`, single-header test framework), minih264 + minimp4
  (`--movie` MP4 encoder/muxer), stb (`stb/`: `stb_image`, `stb_truetype` and
  `stb_vorbis`, the iOS backend's image, font and music decoders), Liberation
  Sans (`fonts/`, the pack picker's face, compiled in as `src/font_sans.inc`)
  and `emsdk/`, the place for a local Emscripten checkout (CI installs its
  own). raylib has not been vendored:
  `scripts/build_raylib_{linux,windows,mac,web,android}.sh` have built it into
  `third_party/raylib-install*`.
- **REQ-031.** cJSON and miniz have been compiled **into** `libobengine.a` so
  consumers of the engine library have not needed their own copies.

### 1.5 Memory model

- **REQ-040.** The game has held a single root `Game` struct in memory
  (`engine/include/game.h`); no persistent gameplay state has existed outside
  that struct, the loaded `Resources` (read-only), and platform handles
  (window, audio device, render target).
- **REQ-041.** The `Game` struct has owned its tables on the heap: every table
  sized by the pack (towns, castles, spells, artifacts, villains, zones, the
  contract cycle) and every list that grows in play (consumed tiles,
  dwellings, placements, foes), each with a parallel `*_count` field
  (`engine/include/game.h`). A `Game` has started zeroed, been sized by
  `GameInit` / `SaveGameRead`, been copied only with `GameCopy` and released
  with `GameFree`; a plain `=` copy would share the other's tables. The
  struct's only compile-time sizes have been `GAME_NAME_LEN`,
  `GAME_ARMY_SLOTS` and the seven-entry `Stats.options[]` (§3).
- **REQ-042.** The `Game` has held a `const Resources *res` pointer to the
  loaded asset pack; the engine has never mutated `*res` after startup.

### 1.6 Coordinate system

- **REQ-050.** All gameplay coordinates have been integer tile indices in
  zone-local space; `(0, 0)` has been the top-left of each zone, `x`
  increasing east and `y` increasing south. There has been no sub-tile
  position.

### 1.7 Naming conventions

- **REQ-060.** Public engine functions acting on the whole game have used the
  `Game*` prefix (`GameInit`, `GameOnStep`, `GameBuyTroop`, …). Combat engine
  functions have used the `combat_*` prefix. Shell screen flows have used
  `screen_*` / view-specific names. Host callbacks the shell has to provide
  have been declared in `engine/include/ui_host.h`.

### 1.8 Source layout

- **REQ-070.** Engine sources (`engine/*.c`), each with a header in
  `engine/include/` except `combat_log.c` (declared in `combat.h`) and
  `host_noop.c` (the default bodies of `ui_host.h`), have been:

  | Source | Responsibility |
  |---|---|
  | `game.c` | Game state, `GameInit`, salting, mechanics, RNG, scoring |
  | `map.c` | Tile grid, `.dat` parsing, placement stamping |
  | `fog.c` | Per-tile fog of war |
  | `adventure.c` | Walkability + tile-step interact dispatch |
  | `step.c` | `GameStep`, one-tile movement + bookkeeping |
  | `combat.c` | Combat state, AI, headless turn loop, damage, combat spells |
  | `combat_log.c` | Combat log line append (pure data) |
  | `flows.c` | Encounter / week-end / endgame flows |
  | `flow_resolve.c` | Apply-cores: the state half of each prompt flow |
  | `player_io.c` | The player-IO request queue every consumer drains |
  | `spells_adventure.c` | Adventure-mode spell effects |
  | `savegame.c` | JSON save read/write |
  | `savepath.c` | OS-aware save dir resolution |
  | `state_serialize.c` | Full-state JSON snapshot builder |
  | `tables.c` | Troop/spell/artifact/class/villain catalog lookups |
  | `resources.c` | `game.json` parser |
  | `tile.c` | Tile/terrain semantics |
  | `pending.c` | Deferred-action / continuation scratch |
  | `pack.c` | Pack discovery, open, zip read |
  | `assets_bytes.c` | `LoadAssetBytes` (engine-side byte reads) |
  | `fatal.c` | Fatal-error helper |
  | `host_noop.c` | Default no-op host callbacks (headless consumers) |

- **REQ-071.** Engine public headers (`engine/include/`) have been:
  `game.h`, `game_fwd.h`, `map.h`, `fog.h`, `adventure.h`, `step.h`,
  `combat.h`, `flows.h`, `flow_answer.h`, `flow_resolve.h`, `player_io.h`,
  `savegame.h`, `savepath.h`, `state_serialize.h`, `tables.h`, `resources.h`,
  `tile.h`, `pending.h`, `pack.h`, `spells_adventure.h`, `assets_bytes.h`,
  `end_screen.h`, `ui_host.h`, `view_kind.h`, `dwelling_kind.h`, `fatal.h`.
  Plus `engine/headless/` (raylib stub headers `raylib.h`, `raylib_stub.h`,
  `input_keys.h`) for headless builds.

- **REQ-072.** Shell sources (`src/*.c`) have been: `main.c` (CLI + init +
  main loop); the `shell_*` flow modules (`shell_menu`, `shell_tempdeath`,
  `shell_weekend`, `shell_audience`, `shell_cheats`, `shell_gate`,
  `shell_fastquit`, `shell_frame`, `shell_promptdispatch`, `shell_actions`,
  `shell_earlyexit`, `shell_gallery`, and the agent adapters `shell_demo` and
  `shell_autoplay`); `combat_loop`, `combat_render`, `combat_replay`; `views`
  + `views_render`; `overlay`, `hud`, `chrome`, `lattice`, `map_render`, `ui`,
  `prompt`, `select`, `textsel`, `text`, `input`, `touch`, `uitouch`, `startup`,
  `end_cartoon`, `pack_select`, `assets`, `audio`, `screenshot`, `sprites`,
  `tile_cache`, `tilevar`, `palette`, `bfont`, `layout`, `present`,
  `safe_area`, `recorder`, `encode_dialog`, `encode_mp4*`; the raylib side of
  the platform seams (`gfx_raylib`, `frame_host`, `input_host`,
  `audio_raylib`, `font_raylib`); and `plat_android` / `plat_ios`. The
  subdirectories have held the modern draw layer and screens (`src/modern/`),
  the frozen legacy draw layer (`src/legacy/`), and the shared location and
  dialog screen modules (`src/screens/`: `home_castle`, `own_castle`,
  `recruit_soldiers`, `dwelling`, `alcove`, `end_game`).

### 1.9 Important global state

- **REQ-080.** Outside the `Game` struct, the only durable state has been:
  the loaded `Resources` (read-only after startup), the active `Map` and `Fog`
  for the hero's current zone (held by the shell's main loop, referenced by
  `Game.position.zone`), the world RNG state (a process-global LCG, §6.1), and
  platform handles. Per-continent fog snapshots have lived inside
  `Game.world.continent_fog[]` and swapped with the active `Fog` on zone change.

### 1.10 Engine choices vs OpenKB

- **REQ-090.** OpenBounty has descended from OpenKB but diverged
  deliberately. These have been intended architectural choices, not bugs:

  | Concern | OpenKB | OpenBounty |
  |---|---|---|
  | Window / render | SDL 1.2 | raylib 6; native Metal on iOS |
  | Audio | SDL_mixer | raylib audio; AVAudioEngine on iOS |
  | Net | SDL_net (combat) | none |
  | Saves | 20,421-byte binary | JSON (version 11) |
  | Asset bundling | DOS `.CC` packs / module dirs | single `assets/kings-bounty/` tree + `.openbounty` packs |
  | Module system | discovery + chain-of-responsibility loader | N/A, one active pack |
  | Render target | 320×200, scaled | legacy: 320×200, integer-scaled to a 640×400 base window; modern: the surface divided by the largest whole scale at which the pack's smallest screen fits (REQ-528) |
  | Palette | EGA / CGA / Hercules build-time | VGA only at runtime |
  | RNG | libc `rand()` | Java-style LCG seeded from `g->seed` |
  | Tile data | 128-byte tile-id space | per-tile struct (terrain + interact + flags) |
  | Sign text | global string-list indexed | per-tile `sign_title` / `sign_body` |

- **REQ-091.** The gameplay-significant constants and formulas have all matched
  OpenKB or have been flagged as known deviations in §38.

---

## 2. Data types and conventions

### 2.1 Primitive types

- **REQ-100.** The engine has used fixed-width integer types from `<stdint.h>`
  where width matters (`uint64_t` for the seed and RNG state) and plain `int`
  for tile coordinates, counts, gold, and indices. Booleans have used
  `<stdbool.h>` `bool`.

### 2.2 Endian

- **REQ-101.** Gameplay state has been endian-independent: saves have been
  JSON text, not raw memory dumps, so no byte-order handling has been required
  at the gameplay layer. The vendored miniz has handled ZIP byte order
  internally.

### 2.3 String identifiers

- **REQ-102.** Catalog entries (troops, spells, castles, towns, villains,
  artifacts, classes, zones) have been referenced by **string id**
  (lowercase snake_case), not array index. Ids have been stable across pack
  versions and have been what appears in save files (`troop_id: "knights"`).
  This has kept saves readable and pack-portable.
- **REQ-103.** String ids have been stored in fixed-size char arrays
  (commonly `[24]` or `[32]`; see the per-struct field widths in §4). An empty
  id (`id[0] == '\0'`) has meant "no entry / empty slot".

### 2.4 Memory ownership

- **REQ-104.** `Game` has owned its tables (REQ-041). `Game.res` has been a
  borrowed pointer into the process-lifetime `Resources` (never freed by the
  game, never mutated). `Map` and `Fog` have been owned by the shell's main
  loop, not by `Game`.

### 2.5 Numeric ranges

- **REQ-105.** Gold, leadership, commission, and counts have been plain `int`
  and have not been expected to overflow within a legal playthrough. Score has
  been clamped at `>= 0` (§26). The day counter has been bounded by the
  difficulty table (max 900).

---

## 3. Global constants and limits

### 3.1 No limit on content

- **REQ-110.** Nothing a pack lists has had a compile-time cap. Catalogs
  (troops, spells, classes, villains, artifacts, towns, castles, zones and
  every per-zone object list), maps and their string pools, fog, and game
  state (towns, castles, spellbook, artifacts, villains, zones, contract
  cycle, consumed tiles, dwellings, placements, foes, the player-IO queue)
  have been heap, sized from the pack or grown in play. The one exception has
  been the controls panel: the loader has kept at most eight
  `controls.settings` rows (`Resources.controls.items[8]`) and the per-game
  settings table has held seven (`Stats.options[7]`, REQ-113). `Game`, `Map`
  and `Fog` have been copied only with `GameCopy` / `FogCopy` / `MapAlloc`
  and released with `GameFree` / `FogFree` / `MapFree`; a test has failed
  the build on a by-value copy.
- **REQ-111.** Tunable constants that *define gameplay* (day/week lengths,
  costs, contract cycle length, difficulty table, hostile armies per zone)
  have lived in `game.json` and been read through `g->res`.

### 3.2 What has stayed fixed

- **REQ-112.** Text field lengths (`RES_ID_LEN=32`, `RES_NAME_LEN=48`,
  `RES_PATH_LEN=128`, `RES_BANNER_LEN=320`, `GAME_NAME_LEN=16`, ...); an
  over-long string has been a load error. `RES_TILE_CODE_COUNT=256` (a map
  cell is one byte); a map has held at most 65,535 distinct strings (a tile
  field is 16 bits).
- **REQ-113.** Game rules: `GAME_ARMY_SLOTS=5`, the 6×5 combat field,
  `CLASS_MAX_RANKS=4`, four difficulties, four continent tiers
  (`RES_SPAWN_TIERS`, chest odds), the 5×5 puzzle grid, seven options.
  Autoplay and the demo player have kept their own table sizes and ignored
  content beyond them. The `kings-bounty` pack has had 25 troops, 14 spells,
  4 classes, 17 villains, 8 artifacts (§Appendix A); `glory-of-rome` has had
  27 troops and the same 14 / 4 / 17 / 8.

### 3.4 Enums

- **REQ-120.** `Difficulty` (`engine/include/game.h`): `DIFFICULTY_EASY=0`,
  `_NORMAL=1`, `_HARD=2`, `_IMPOSSIBLE=3`.
- **REQ-121.** `Mount` (`engine/include/game.h`): `MOUNT_RIDE=0`,
  `MOUNT_SAIL`, `MOUNT_FLY`. `TravelMode`: `TRAVEL_WALK=0`, `TRAVEL_BOAT`.
  `Mount` has been the long-term possession; `TravelMode` the current
  movement state.
- **REQ-122.** `CastleOwnerKind` (`engine/include/game.h`):
  `CASTLE_OWNER_PLAYER=0`, `_MONSTERS`, `_VILLAIN`, `_SPECIAL`.
- **REQ-123.** `Terrain` (`engine/include/tile.h`): `TERRAIN_GRASS=0`,
  `_FOREST`, `_MOUNTAIN`, `_WATER`, `_DESERT`, `_RIVER` (inland water: it
  has blocked walking and boats, taken a bridge and been flown over),
  `TERRAIN_COUNT`.
- **REQ-124.** `Interact` (`engine/include/tile.h`): `INTERACT_NONE=0`,
  `_CASTLE_GATE`, `_TOWN`, `_TREASURE_CHEST`, `_SIGN`, `_ARTIFACT`,
  `_DWELLING_PLAINS`, `_DWELLING_FOREST`, `_DWELLING_HILLS`,
  `_DWELLING_DUNGEON`, `_ALCOVE`, `_ORB`, `_TELECAVE`, `_NAVMAP`, `_FOE`,
  `INTERACT_COUNT`.
- **REQ-125.** `DwellingKind` (`engine/include/dwelling_kind.h`):
  `DWELLING_KIND_PLAINS=0`, `_FOREST`, `_HILL`, `_DUNGEON`.
- **REQ-126.** `ViewKind` (`engine/include/view_kind.h`): `VIEW_NONE=0`,
  `VIEW_MENU`, `_CHARACTER`, `_ARMY`, `_SPELLS`, `_GATE` (the Town/Castle Gate
  destination picker, REQ-322), `_CONTRACT`, `_PUZZLE`, `_WORLDMAP`,
  `_OPTIONS`, `_CONTROLS`, `_TOWN`, `_HOME_CASTLE`, `_OWN_CASTLE`,
  `_DWELLING`, `_ALCOVE`, `_RECRUIT_SOLDIERS`, `_WIN`, `_LOSE`.

### 3.5 Troop ability flags (`engine/include/tables.h`)

- **REQ-130.** The ability mask has been an 8-bit field:
  `TROOP_ABIL_FLY=0x01`, `_REGEN=0x02`, `_MAGIC=0x04`, `_IMMUNE=0x08`,
  `_ABSORB=0x10`, `_LEECH=0x20`, `_SCYTHE=0x40`, `_UNDEAD=0x80`. Semantics
  in §13.2 / §25.7.

### 3.6 Artifact power enum (`engine/include/tables.h`)

- **REQ-131.** `ArtifactPower`: `ARTIFACT_POWER_UNKNOWN=0` (the default when
  the JSON string is missing or unrecognised, as the Book's `unknown_xxx1`
  is), `_INCREASED_DAMAGE`, `_QUARTER_PROTECTION`,
  `_DOUBLE_LEADERSHIP`, `_INCREASE_COMMISSION`, `_DOUBLE_SPELL_POWER`,
  `_DOUBLE_MAX_SPELLS`, `_CHEAPER_BOATS`.

### 3.7 Combat-grid constants (`engine/include/combat.h`)

- **REQ-132.** `COMBAT_W=6`, `COMBAT_H=5`, `COMBAT_SIDES=2`, `COMBAT_SLOTS=5`,
  `COMBAT_SIDE_PLAYER=0`, `COMBAT_SIDE_AI=1`, `COMBAT_BANNER_LEN=80`,
  `COMBAT_LOG_LINES=8`, `COMBAT_LOG_LINE_LEN=80`. Per-side combat artifact
  bits: `COMBAT_POWER_INCREASED_DAMAGE=(1<<0)`,
  `COMBAT_POWER_QUARTER_PROTECTION=(1<<1)` (internal, never serialized).

### 3.8 Cost / time constants

- **REQ-133.** Costs and time constants have been data, not code. Cost
  defaults in `kings-bounty`: `economy.boat_cost_normal=500`,
  `boat_cost_cheap=100`, `siege_cost=3000`, `alcove_cost=5000` (§23,
  §Appendix A). Time: `time.day_steps=40`, `week_days=5`,
  `days_per_difficulty` (an object keyed `easy` 900, `normal` 600, `hard`
  400, `impossible` 200). Map dimensions have had no
  ceiling: `Map.tiles` has been heap, sized to each zone's own
  `width`/`height`, and the save has encoded fog from those (REQ-413). The
  cost has been memory: every autoplay search node has copied the used map
  area (AP-204), so the frontier beam has paid proportionally.

---

## 4. The `Game` struct, complete game state

### 4.1 Overview

- **REQ-140.** `struct Game` (`engine/include/game.h`) has held the full
  adventure-screen state. Its fields have mirrored the JSON save schema so
  serialization is 1:1 (§27). All enumerations have been keyed by string ids
  from `tables.h`, keeping save files readable and avoiding magic numbers.
- **REQ-141.** The struct has **not** owned the map tiles or fog; those have
  lived in `Map`/`Fog` (`engine/include/map.h`, `fog.h`). The game has
  referenced them by zone id; the caller has loaded the matching map when
  `position.zone` changes.

### 4.2 Top-level fields

- **REQ-142.** Declaration order and meaning:

  | Field | Type | Meaning |
  |---|---|---|
  | `res` | `const Resources *` | Loaded pack; never owned, never mutated |
  | `version` | `int` | Always `SAVE_VERSION` (11) at runtime |
  | `seed` | `uint64_t` | Expanded RNG seed for this game (derived from `seed_index`) |
  | `seed_from_catalog` | `bool` | False only on the raw-seed path (a caller pinned `seed` directly) |
  | `seed_index` | `int` | `0`–`255` catalog world when `seed_from_catalog` |
  | `oracle_mode` | `bool` | Autoplay/demo session flag, never serialized: rank promotion fires on capture rather than only at an audience |
  | `character` | `Character` | Name, class+rank, difficulty, mount |
  | `stats` | `Stats` | Gold, leadership, spell power, day/step counters, options |
  | `position` | `Position` | Zone id, `(x,y)`, `(last_x,last_y)`, facing, the location screen the hero is in |
  | `travel_mode` | `TravelMode` | Walking vs in boat |
  | `anim_frame` / `anim_moving` | `int` / `bool` | Free-running animation tick shared by hero, boat and foes, and the walk-cycle flag that picks walk over idle art; presentation state, neither saved |
  | `hud_visible` | `bool` | Floating HUD bar toggle (persisted) |
  | `army[5]` | `ArmyStack` | The player's army |
  | `spells` | `Spellbook` | Per-spell charge counts |
  | `contract` | `Contract` | Active contract, rotation cycle, villains caught |
  | `artifacts` | `Artifacts` | Found flags |
  | `world` | `WorldProgress` | Zones discovered, rites known, orbs found, per-continent fog |
  | `boat` | `BoatState` | Rental flag + parked coords |
  | `towns` + `town_count` | `TownRecord *` | Per-town visited + spell-for-sale, parallel to `res->towns` |
  | `castles` + `castle_count` | `CastleRecord *` | Per-castle owner + garrison, parallel to `res->castles` |
  | `scepter` | `ScepterLocation` | Buried scepter zone + `(x,y)` |
  | `consumed` + `consumed_count` | `TileMutation *` | Permanently consumed tiles |
  | `events_done` + `events_done_count` | `EventFired *` | One-time vistas already played (zone + event id) |
  | `dwellings` + `dwelling_count` | `DwellingState *` | Per-dwelling recruit pools |
  | `placements` + `placement_count` | `SaltedPlacement *` | Salt-time placements |
  | `foes` + `foe_count` | `FoeState *` | Hostile + friendly foe rows (all continents share the one table) |
  | `player_io` | `PlayerIoQueue` | The player-IO request queue (engine/include/player_io.h) |

  Every pointer field has been heap with a `*_count` beside it (and a `*_cap`
  for the lists that grow in play, REQ-041).

### 4.3 Sub-struct field semantics

- **REQ-143.** `Stats` has held: `gold`, `commission_weekly`,
  `leadership_base`, `leadership_current`, `followers_killed`, `score`,
  `spell_power`, `max_spells`, `knows_magic` (bool), `siege_weapons` (flag),
  `time_stop` (overworld steps where the day does not advance),
  `steps_left_today`, `days_left`, `game_over` (set when `days_left` hits 0),
  `won` (set when the scepter is recovered), `last_commission` and
  `last_astrology_troop` (UI carry from the most recent week-end), `blessed`
  and `tributes` (the modern audience's Blessing and Tribute), and
  `options[7]` (the controls-menu settings, persisted per game, parallel to
  `res->controls.items[]`).
- **REQ-144.** `Character` has held: `name[16]`, `cls` (a `ClassState`:
  class id, `rank_index` 0..3, denormalized `rank_id` + `rank_title`),
  `difficulty`, and `mount`.
- **REQ-145.** `Position` has held `zone[24]`, `x`, `y`, `last_x`, `last_y`
  (previous tile, for bump-back), `facing_left` and `facing`, and the
  location the hero is standing in: `in_town`, `home_castle`, `own_castle`,
  and `dwelling_troop` with `dwelling_x`, `dwelling_y`.
- **REQ-146.** `ArmyStack` has held `id[32]` (troop id; empty = empty slot)
  and `count`. `Unit` (used inside garrisons and foe rows) has held `id[24]`
  and `count`.
- **REQ-147.** `Spellbook` has held `count` and `counts` (heap), parallel to
  the spell catalog. `GameKnownSpells` has summed them.
- **REQ-148.** `Contract` has held `active_id[24]` (current contract, empty =
  none), `cycle` (heap, `cycle_count` entries; length from
  `res->contract.cycle_length`), `last_contract` (last slot issued),
  `max_contract` (next villain to rotate in), and `villains_caught` and
  `villains_prefought` (heap, `villain_count` entries, indexed by
  `VillainDef.index`).
- **REQ-149.** `Artifacts` has held `count` and `found` (heap), parallel to
  the artifact catalog. `WorldProgress` has held `zone_count` and, per zone,
  `zones_discovered`, `zone_rites` (the Augur's rites, modern), `orbs_found`,
  and `continent_fog` (per-continent fog snapshots; the active continent's
  fog has lived in the shell's standalone `Fog` and been swapped in/out on
  zone change). The puzzle view has derived its reveal state directly from
  `contract.villains_caught` + `artifacts.found`; there has been no separate
  puzzle-reveal bookkeeping.
- **REQ-150.** `BoatState`: `has_boat`, `x`, `y`, `zone[24]`.
  `ScepterLocation`: `zone[24]`, `x`, `y`, and `key` (rolled from the world
  RNG when the scepter is buried, never read again and not saved; the roll
  keeps the RNG call order of OpenKB's `spawn_game`).
- **REQ-151.** `TownRecord`: `id[24]`, `visited`, `spell_for_sale[24]`.
  `CastleRecord`: `id[24]`, `visited`, `known` (revealed by Find Villain
  etc.), `owner_kind`, `villain_id[24]` (when owner is a villain),
  `garrison[5]`.
- **REQ-152.** `TileMutation`: `zone[24]`, `x`, `y`, a tile permanently
  consumed (artifact picked up, chest opened). On load the caller has
  re-applied these so the tile renders and behaves as plain terrain.
- **REQ-153.** `DwellingState`: `zone[24]`, `x`, `y`, `troop_id[32]`
  (deterministic, set on first visit), `count` (current available recruits),
  `max_population`.
- **REQ-154.** `SaltedPlacement`: `zone[24]`, `x`, `y`, `kind` (an `Interact`
  enum value stored as `int` for save stability), `id[32]` (payload, troop
  id for a dwelling, artifact id, etc.; may be empty).
- **REQ-155.** `FoeState`: `zone[24]`, `x`, `y`, `origin_x`, `origin_y` (the
  spawn tile, unchanged as the foe wanders; a friendly foe's origin is the
  chest slot it was salted onto, which the map never draws as a chest),
  `placement_id[32]`, `garrison[5]`, `alive`, `friendly` (true → recruit
  dialog; false → attack prompt), `is_static` (never moves; the fight is
  forced on contact), `requires_troop[32]` (the one arm a gate army admits,
  empty = anyone), `scene_index` and `scene_title[48]` (the picture and
  heading shown when it turns the hero back; `-1` / empty = none). Friendly
  and hostile foes have shared the one `foes` table; classification has been
  by the `friendly` flag.

### 4.4 Invariants

- **REQ-156.** `version` has always equalled `SAVE_VERSION` at runtime.
- **REQ-157.** An empty army/garrison slot has been encoded by `id[0]=='\0'`
  and/or `count==0`. `GameCompactArmy` has kept non-empty troops contiguous
  at the front, preserving order.
- **REQ-158.** The player army has never been allowed to become entirely
  empty through garrisoning (`GameGarrisonTroop` refuses the last troop); it
  has become empty only through defeat / dismiss-last, which triggers temp
  death.
- **REQ-159.** Save schema parity: every persisted field above has had a 1:1
  JSON representation; serialization has round-tripped without loss (§27).

---

# Part II, Gameplay

## 5. Game model and lifecycle

### 5.1 Top-level structure

- **REQ-160.** The game has held a single root `Game` struct (§4); no
  persistent gameplay state has existed outside it, the read-only `Resources`,
  and platform handles.

### 5.2 Lifecycle states

- **REQ-161.** The process has progressed through these macro-states in order:
  **STARTUP** → **CHARACTER CREATION** (or **LOAD**) → **ADVENTURE** ↔
  **COMBAT** → **END GAME** → **EXIT**.
- **REQ-162.** **STARTUP** (`src/startup.c`). Legacy: publisher splash, title
  splash, then the credits when the pack supplies any, each 2.5 s or any key.
  Modern: the publisher splash, then the title menu of `DESIGN-SPEC.md`
  DSGN-0146 to DSGN-0148 (New Game, Load Saved Game, Credits and, on desktop
  and web, Exit).
- **REQ-163.** **CLASS SELECT**. Legacy: four classes on `A`/`B`/`C`/`D`, `L`
  for Load, `Esc` to quit. Modern: the class painting, Left/Right or a tap
  picking a figure and Continue confirming (REQ-532); `Esc` has returned to
  the title menu.
- **REQ-164.** **SAVE PICKER**. Legacy: `L` has opened ten slots plus a "New"
  row; an existing slot has loaded, an empty slot or "New" has fallen through
  to character creation. Modern: Load Saved Game has opened the in-game Load
  page of five slots (`MODERN_SAVE_SLOTS`); `Esc` has returned to the title
  menu.
- **REQ-165.** **CHARACTER CREATION**. Legacy: name entry (first letter
  capitalised), then difficulty (Easy/Normal/Hard/Impossible, shown with
  starting days and score multiplier) on the same panel, then an intro
  banner. Modern: difficulty, then the name, each a page whose Back has
  stepped one screen back (REQ-532), and no intro.
- **REQ-166.** A pack has held a catalog of 256 worlds, selected by an 8-bit
  index. `--seed N` has supplied that index directly (`0`–`255`); an
  out-of-range, negative, or unparseable value has been a hard error (exit 2,
  `src/main.c`). Without `--seed`, the index has been the clock folded with
  a x31 hash of the name and `class_index * 2654435761`, masked `AND 0xFF`. `GameInitSeeded` has expanded the index into
  `Game.seed` via `GameSeedFromIndex` (REQ-181a) and recorded it in
  `Game.seed_index`; `GameInit` has remained the raw-seed path, honoring a
  caller-preset non-zero `Game.seed` verbatim and leaving `seed_from_catalog`
  false.
- **REQ-167.** **ADVENTURE** has been the main loop, exited only by combat
  trigger or game end (win via search-on-scepter; loss via `days_left == 0`).
- **REQ-168.** **COMBAT** has run a separate state machine (§25) on a tactical
  grid; on completion, control has returned to adventure with results
  applied.
- **REQ-169.** **END GAME** has shown the ending screen (§26.5) with the
  final score; once it is dismissed the game has been over and the next key
  press has ended the process (**EXIT**). Only New Game from the game menu
  has gone back to the title (`back_to_title`, `src/main.c`).

### 5.3 Main loop

- **REQ-170.** Each frame has performed, in order: audio tick, `Alt+Enter` fullscreen toggle, input dispatch, animation update,
  render to the offscreen target (320×200 in legacy, the pack's buffer in
  modern), blit to the window at a whole-number scale. Per-frame draw
  dispatch has been `src/shell_frame.c`.
- **REQ-171.** In legacy mode the window has been initialised at **640×400**
  (`CL_WINDOW_W/H` = `CL_SCREEN_W/H` × `CL_SCALE` = 320×200 × 2;
  `src/layout.h`); in modern mode at the pack's smallest screen, then resized to
  the largest whole multiple of it that the monitor's work area holds
  (REQ-528). In both, the window has been
  created with `FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT`, target FPS 60, and
  `KEY_NULL` as the raylib exit key (so `Escape` does not close the
  window).
- **REQ-172.** Input dispatch has followed a strict overlay hierarchy
  (highest priority first): a modern town, menu or castle action waiting on
  its Yes/No → fast-quit prompt → the demo or autoplay agent's own tick →
  the gate picker → active prompt → active view → active dialog → a finished
  game (any key exits) → adventure-mode actions → movement.
- **REQ-173.** HUD visibility has been remembered when an overlay opens:
  `hud_visible` has been forced false and restored to the player's setting on
  dismiss.
- **REQ-174.** Animation frames (`Game.anim_frame`) have advanced at intervals
  of `0.05 + options[0] * 0.05` s (0.05..0.30 s); with the animation toggle
  (`options[3]`) off, `anim_frame` has stayed 0.

---

## 6. RNG and determinism

### 6.1 PRNG

- **REQ-180.** A single deterministic Java-style LCG has driven all world
  randomness (`engine/game.c`): `state = (state * 25214903917 + 11) &
  0xFFFFFFFFFFFFFFFF`; `result = state >> 32`. It has been process-global
  state, snapshot/restorable via `GameRngSnapshot` / `GameRngRestore` (§36).
- **REQ-181.** The PRNG has been seeded from `Game.seed` XORed with
  `0x5DEECE66D` at game start.
- **REQ-181a.** `GameSeedFromIndex(index)` has expanded an 8-bit catalog index
  into the full-width `Game.seed` with a splitmix64 finalizer. The expansion has
  been required, not cosmetic: the engine has read the seed at three widths,
  `(seed >> 8)` for spawn rolls (REQ-186a), `(unsigned)` truncation in
  `chest_rand` / the dwelling pick / the weekly growth salt, and all 64 bits in
  the LCG (REQ-180), so a raw `0`–`255` seed would supply `(seed >> 8)` a
  constant 0 for every world. All 256 indices have stayed distinct through each
  of those paths. The mixer constants, together with the order of
  `game_rng_next` calls in `GameInit`, have defined catalog identity: changing
  either re-maps every world.
- **REQ-182.** `game_rng_next(min, max)` has returned a value uniformly in the
  inclusive range `[min, max]`.
- **REQ-183.** A per-tile hash (`chest_rand(game, x, y, salt)`) has produced a
  deterministic per-tile value from `(seed, x, y, salt)`, so re-rolling the
  same chest has yielded the same outcome until consumed.
- **REQ-184.** Combat has used an **independent** LCG state (`Combat.rng_state`,
  §25.14) so combat rolls have never advanced the world RNG.

### 6.2 Determinism guarantees

- **REQ-185.** Given the same catalog index (or raw seed) and the same
  sequence of player inputs, the entire game has been reproduced bit-for-bit
  (floating point has been used only for animation and rendering, never
  gameplay).
- **REQ-186.** All randomised game-start placements (zones, villains, scepter,
  dwellings, foes, telecaves, navmaps, orbs, artifacts, town spells) have
  derived from `seed`, so loading a save has restored identical placements. A
  catalog save has stored `seed_index` rather than `seed` and re-derived the
  seed on load (REQ-166); this has been exact, whereas a raw `seed` written as
  a JSON number has been lossy above 2^53.
- **REQ-186a.** Town spell selection has computed `(seed XOR (slot + 1)) mod
  spells_count()` in `uint64_t`, never `unsigned long`: that type has been
  32-bit on Windows and 64-bit elsewhere, so it would give one world different
  town spells per platform once the seed carries entropy above bit 31.
- **REQ-187.** Real-time animation, audio, and rendering have not affected
  gameplay state.

---

## 7. Time, days, and weeks

### 7.1 Time units

- **REQ-190.** A **day** has consisted of `time.day_steps = 40` step
  opportunities. A **week** has consisted of `time.week_days = 5` days. The
  starting `days_left` has come from `time.days_per_difficulty[]`: Easy=900,
  Normal=600, Hard=400, Impossible=200.

### 7.2 Step processing

- **REQ-191.** Each completed overworld move has called `GameOnStep`
  (`engine/game.c`), which has: (1) if `time_stop > 0`, decremented it and
  returned without touching day state; (2) else if the destination terrain is
  desert and the hero is not flying (`GameStep` passes the flag), set
  `steps_left_today = 0`; (3) else decremented `steps_left_today`; (4) while
  `steps_left_today <= 0`, fired the day rollover.
- **REQ-192.** Day rollover has decremented `days_left`, reset
  `steps_left_today` to `time.day_steps`, and set `game_over = true` when
  `days_left` reaches 0. A week boundary has been detected when `days_left %
  week_days == 0`, firing week-end processing (§24).

### 7.3 Time spend helpers

- **REQ-193.** `GameSpendDays(g, n, &paid)` has spent `n` days (firing one day
  rollover each), accumulating commission paid into `*paid`.
  `GameSpendWeek(g, &paid)` has spent enough days to cross exactly one week
  boundary; End Week and zone-switch sailing have used it.
- **REQ-194.** Search (key `S`) has cost `tuning.search_cost_days` (10 in
  both packs) regardless of result, except that revealing the buried scepter
  ends the game as a win immediately.
- **REQ-195.** `time_stop` has been reset to 0 at every day rollover. While
  it is positive a step has not touched the day counter at all (REQ-191), so
  a cast has been walked off step by step before the day can roll; a Search
  or End Week, which rolls days directly, has discarded what was left.

---

## 8. Character, classes, and ranks

### 8.1 Classes

- **REQ-200.** There have been exactly four classes: Knight (0), Paladin (1),
  Sorceress (2), Barbarian (3), declared in `game.json:classes[]`.
- **REQ-201.** Each class has had a `starting_gold` and a `starting_troops`
  array. Initial values: Knight 7,500g (20 militia, 2 archers); Paladin
  10,000g (20 peasants, 20 militia); Sorceress 10,000g (30 peasants, 10
  sprites); Barbarian 7,500g (20 wolves). (See §Appendix A for the catalog
  values.)
- **REQ-202.** Each class has had four ranks (0..3) with fields `id`, `name`,
  `villains_needed`, `leadership`, `max_spells`, `spell_power`, `commission`,
  `knows_magic`, `instant_army` (troop catalog index for Instant Army).
  `ClassDef` / `RankDef` have been defined in `engine/include/tables.h`.

### 8.2 Per-rank tables

- **REQ-203.** The four classes have followed these per-rank tables (delta
  values; cumulative stats accumulate by summing rank 0..n). `glory-of-rome`
  has kept every number and rank id and renamed the titles
  (`GLORY-OF-ROME.md`).

**Knight:**

| Rank | Title | Villains | Leadership | Max Spells | Spell Power | Commission | knows_magic | instant_army |
|---|---|---|---|---|---|---|---|---|
| 0 | Knight | 0 | 100 | 2 | 1 | 1000 | false | 0 |
| 1 | General | 2 | 100 | 3 | 1 | 1000 | false | 2 |
| 2 | Marshal | 8 | 300 | 4 | 1 | 2000 | false | 8 |
| 3 | Lord | 14 | 500 | 5 | 2 | 4000 | false | 14 |

**Paladin:**

| Rank | Title | Villains | Leadership | Max Spells | Spell Power | Commission | knows_magic | instant_army |
|---|---|---|---|---|---|---|---|---|
| 0 | Paladin | 0 | 80 | 3 | 1 | 1000 | false | 0 |
| 1 | Crusader | 2 | 80 | 3 | 1 | 1000 | false | 2 |
| 2 | Avenger | 7 | 240 | 6 | 2 | 2000 | false | 8 |
| 3 | Champion | 13 | 400 | 5 | 2 | 4000 | false | 18 |

**Sorceress:**

| Rank | Title | Villains | Leadership | Max Spells | Spell Power | Commission | knows_magic | instant_army |
|---|---|---|---|---|---|---|---|---|
| 0 | Sorceress | 0 | 60 | 5 | 2 | 3000 | **true** | 1 |
| 1 | Magician | 3 | 60 | 8 | 3 | 1000 | false | 6 |
| 2 | Mage | 6 | 180 | 10 | 5 | 1000 | false | 9 |
| 3 | Archmage | 12 | 300 | 12 | 5 | 1000 | false | 19 |

**Barbarian:**

| Rank | Title | Villains | Leadership | Max Spells | Spell Power | Commission | knows_magic | instant_army |
|---|---|---|---|---|---|---|---|---|
| 0 | Barbarian | 0 | 100 | 2 | 0 | 2000 | false | 0 |
| 1 | Chieftain | 1 | 100 | 2 | 1 | 2000 | false | 3 |
| 2 | Warlord | 5 | 300 | 3 | 1 | 2000 | false | 7 |
| 3 | Overlord | 10 | 500 | 3 | 1 | 2000 | false | 15 |

### 8.3 Stats and leadership

- **REQ-204.** `leadership_base` has been the sum of `leadership` deltas
  through the current rank; `leadership_current` has been the live value,
  modified by alcove, Raise Control, chest leadership, and combat losses.
- **REQ-205.** `commission_weekly` has been the cumulative `commission`
  deltas, optionally augmented by the `INCREASE_COMMISSION` artifact (§20.4).
- **REQ-206.** `max_spells` and `spell_power` have accumulated similarly; the
  `DOUBLE_MAX_SPELLS` and `DOUBLE_SPELL_POWER` artifacts have applied a
  multiplicative doubling on pickup.
- **REQ-207.** `knows_magic` has been initialised from the rank-0 class flag;
  only the Sorceress has set it true at creation. Any class has been able to
  learn magic at the Archmage Aurange alcove (§18.5).

### 8.4 Rank-up

- **REQ-208.** After each villain capture, `GameMaybeRankUp` has checked the
  next rank's `villains_needed`; while `villains_caught >=
  ranks[rank+1].villains_needed`, the rank has advanced and stats have
  recomputed. On rank-up, `leadership_base`/`commission_weekly`/`max_spells`/
  `spell_power` have gained the new ranks' deltas additively, so every
  chest-accumulated bonus survives, and `leadership_current` has gained the
  same delta. Rank advancement has opened no popup of its own; the King's
  audience (§17.7) has reported it on the next visit. Outside autoplay and
  demo (`Game.oracle_mode`), promotion has happened only at an audience.

### 8.5 Instant-army count formula

- **REQ-209.** Instant Army (the adventure spell, §19.4) has summoned
  `class.ranks[rank].instant_army` troops with count `(spell_power + 1) *
  instant_army_multiplier[rank]`, multiplier `[3, 2, 1, 1]` for ranks 0..3
  (minimum 1).

### 8.6 Difficulty and mount

- **REQ-210.** `Difficulty` has affected `days_left` (§7.1) and the final
  score multiplier (§26.2) only, not starting gold, army, garrisons, foe spawn
  rates, or chest contents.
- **REQ-211.** The mount has been one of `MOUNT_RIDE` (walking, default),
  `MOUNT_SAIL` (boat), `MOUNT_FLY` (flying). Flying (key `F`) has required
  `GamePlayerCanFly` (every non-empty troop has `TROOP_ABIL_FLY` and
  `skill_level >= 2`); landing (key `L`) has required a grass,
  non-interactive, non-foot-blocking destination tile.

---

## 9. World, zones, and tiles

### 9.1 Zones

- **REQ-220.** The world has consisted of the pack's zones
  (`game.json:zones[]`): `kings-bounty` has four, `continentia`, `forestria`,
  `archipelia`, `saharia`, each 64×64 tiles; `glory-of-rome` has four,
  `italia` (64×128), `galliae` (64×64), `africa` (64×28) and `oriens` (64×44).
- **REQ-221.** Each zone has declared: `id`, display `name`, `map` (the
  `.dat` path), `width`, `height`, `hero_spawn` `{x, y}`, optional
  `home_spawn` + `is_home`, optional `magic_alcove`, `neighbors[]` (zone ids
  reachable by sailing), a `salt` config (§10), and per-feature lists
  (`towns`, `castles`, `signs`, `chests`, `artifacts`, `dwellings`,
  `wandering_armies`). `glory-of-rome` has added `events`, `arrivals`,
  `alcove_art`, `alcove_cost`, `army_art`, `town_backdrop`, `tile_set`,
  `tile_set_arts`, and the `boatmaster`, `pontifex` and `siegemaster`
  figures (`PACK-FORMAT.md` §6). Exactly one zone has had `is_home: true`
  (Continentia; Italia in Rome).
- **REQ-221b.** **One-time vistas (`events`).** A zone has been able to
  declare `events`, a list of one-time moments. Each has had an `id`, a
  trigger tile `(x, y)`, a `scene` image, a `title` and `body`, a `requires`
  list and an `effects` list. Stepping onto the tile with every precondition
  held (`GameTryFireEvent`, `engine/game.c`) has spent what the
  preconditions mark `consume`, written each effect's `tile` (a `tile_codes`
  key) onto the map, recorded the id in `events_done`, and queued the scene as
  a `PIO_NOTE_SCENE` with `REQ_FACE_EVENT`, drawn full width with the pack's
  art and a single Continue. It has never fired again, and a vista has never
  bounced the hero back. Preconditions have been `spell` (charges), `troop`
  (in the army), `gold` (held) and `artifact` (found), each with a `count` and
  an optional `consume`; troops and artifacts have been held, never spent. An
  effect has been either a `tile` (a tile_codes key written onto the map) or
  `"reveal": true`, which lifts the fog over the whole zone -- the fog is
  saved, so only tile effects are re-applied on load. `events_done` has been
  saved, and `GameApplyTileMutations` has re-applied every played vista's
  tiles whenever the zone loads, so the change outlives a zone switch and a
  reload. Lists have been heap, sized by the pack; `kings-bounty` has
  declared none. `glory-of-rome` has declared the Rubicon (the Pontifex rite,
  one charge, consumed, opens the bridge the Po plain is behind), the Pharos
  of Alexandria (3,000 gold, paid, reveals the whole of Africa) and the Temple
  of Ocean in Galliae (both of that zone's relics, held not spent: the crag on
  the islet's near side becomes grass and two bridge tiles lay a causeway to
  it).
- **REQ-296a.** **A gate army has been able to demand one arm.** A static
  army with `"requires_troop": "<troop id>"` has refused the fight unless that
  troop stands in the hero's army (`GameFoeBarsHero`, `engine/game.c`):
  stepping onto it has bounced the hero back with the `foe_requires_troop`
  banner, drawn as a scene when the army also names one (`"scene"`, headed by
  its own `"title"` and sharing the vistas' art list). A dwelling has been
  pinnable to a breed with `"troop"` instead of rolling from the zone's pool.
  Autoplay has treated the demand as a prerequisite candidate (`exec_muster`,
  `autoplay/primitives.c`): it marches to the dwelling that breeds the arm,
  gives up its weakest troop for a slot if the army is full, and recruits
  what the purse and leadership allow. `glory-of-rome` has held the Armenian
  pass against everything but the Elephanti, bred at Apamea, with Artaxata
  behind it.
- **REQ-230d.** **A chest has been able to carry a declared purse.** A zone
  chest with `"gold": N` has always held exactly N and rolled nothing
  (`GameRollChest`, `engine/game.c`); the leadership offer has stayed N/50,
  doubled by the artifact power as usual. Galliae's island chest has held
  5,000.
- **REQ-229h.** **A tile code has been able to name its `ground`.** A
  landmark tile (the Pharos) has been transparent around its art, so its code
  names the art drawn under it; `fill_tile_from_code` (`engine/map.c`) has set
  the tile's ground from it and the renderer has laid that down first, exactly
  as it does under an object tile. Absent, a tile has been its own ground.
- **REQ-221d.** **Town backdrops per continent.** The town screen's picture
  has been the town's own `backdrop` when it names one, else its zone's
  `town_backdrop`, else the pack's shared `sprites.ui.town_backdrop`
  (`town_backdrop_for`, `src/modern/overlay.c`). All three have been listed in
  the art manifest, so the pack zip carries what it declares. `glory-of-rome`
  has given each continent its own town street and kept the original picture
  for Roma alone; `kings-bounty` has declared neither and drawn the shared
  one.
- **REQ-221c.** **Sailing has been a scene, with a confirmation.** When a
  pack ships `sprites.ui.sail_backdrop` and the string
  `body_navigate_confirm`, the modern shell has drawn the sail-to decision,
  from the first list on, as a place page over that picture: one row per
  province plus Cancel, then a yes/no confirmation ("Sail for %ZONE%?") over
  the same picture, drawn by `modern_overlay_draw_sail`
  (`src/modern/overlay.c`; `DESIGN-SPEC.md` DSGN-0133). The two steps have
  lived in the SHELL (`src/shell_promptdispatch.c`): the engine has received
  exactly one answer, the province, so autoplay, recordings and replays see
  one decision, and a pack with neither key has kept the bottom-frame list
  (`kings-bounty`). Declining the confirmation has put the province list
  back up; cancelling it has ended the sail.
- **REQ-221a.** **Arrival by origin.** A zone has been able to declare
  `arrivals`, an object keyed by the zone sailed from, each `{x, y}`.
  `GameSwitchZone` has landed the hero at the entry for the zone being left,
  else at `hero_spawn` (`resources_zone_arrival`). A landing on water has
  arrived in the boat, as any water spawn does. Gate spells and defeat have
  still overridden the landing afterwards. `glory-of-rome` has declared an
  arrival for every neighbour, each a sea tile touching the coast beside the
  port a ship from there would make for; `tools/mapbuild.py check` has
  required each to be on the open sea and touching land, and counted its sea
  as sailed when proving the gates. `kings-bounty` has declared none.

### 9.2 Coordinates

- **REQ-222.** The origin has been the top-left of each zone; `x` east, `y`
  south. All gameplay coordinates have been integer tile indices; there has
  been no sub-tile position.

### 9.3 Terrain

- **REQ-223.** Terrain (`engine/include/tile.h`): `TERRAIN_GRASS`, `_FOREST`,
  `_MOUNTAIN`, `_WATER`, `_DESERT`, `_RIVER` (art `river_*`; inland water
  that blocks walking and boats alike and is crossed by a bridge or flown
  over).
- **REQ-224.** Walkability on foot (`TerrainWalkable`, `engine/tile.c`): grass
  and desert have been walkable; forest, mountain, water and river have not.
  A tile flagged `is_bridge` (art `bridge_h` / `bridge_v`, grass terrain)
  has been walkable on foot and traversable in a boat. Stepping onto a
  desert tile has set `steps_left_today = 0` immediately (§7.2).

### 9.4 Interactive overlay

- **REQ-225.** A tile has carried at most one interactive overlay (the
  `Interact` enum, §3.4); `INTERACT_NONE` has meant none. Castle wall tiles
  have been non-interactive but set `blocks_foot = true`.

### 9.5 Tile data

- **REQ-226.** Each `Tile` (`engine/include/map.h`) has stored: `art` (sprite
  filename root), `ground` (the terrain art drawn beneath, REQ-229f),
  `terrain` (derived from art at load via `TerrainFromArt`), `interactive`,
  optional `id` (named instance, e.g. "kings_castle"), `blocks_foot`,
  `is_bridge`, optional `sign_title` / `sign_body`, and `boat_spawn_x/y` (for
  town tiles).

### 9.6 Map file format

- **REQ-227.** Maps have been plain-text files (`maps/<zone>.dat` in the
  pack), one ASCII byte per tile, row-major. Lines beginning with `#` and
  blank lines have been ignored. Each byte has been looked up in
  `Resources.tile_codes` to produce an art name plus terrain/blocking flags;
  short rows have been padded with grass. `kings-bounty` has used 54 distinct
  tile codes (§Appendix A).
- **REQ-228a.** A town has been stamped with its catalog entry's `art` stem
  when one is declared (`engine/map.c stamp_objects`), else `town`. The art
  manifest has listed each declared stem once and the shared `town` tile only
  while some town lacks an `art` of its own.
- **REQ-165b.** In a siege the shell has drawn a decorative band above row 0
  from `sprites.ui.siege_back_wall` (end cells from `_left` / `_right`) over
  field tiles, when the pack names them; outside the grid, so nothing in play
  changes, and packs without the keys have drawn nothing.
- **REQ-165c.** When `sprites.ui.siege_grid` names a prefix, the engine has
  expanded it to one path per cell of the band plus board
  (`resources_siege_grid_path`, `COMBAT_W x (COMBAT_H + 1)` entries in the
  manifest) and the shell, in a siege only and only when every cell loaded,
  has drawn each cell's own tile as the ground, row 0 in the band above the
  board, and skipped the wall codes 5..10 at the obstacle stamp; the
  `siege_back_wall*` band has not been drawn then, and `sprites.combat[5..10]`
  have left the manifest (Rome has shipped no wall pieces). `castle_omap` and
  movement have been untouched. Absent, REQ-165b and the per-code pieces have
  applied (`kings-bounty`).
- **REQ-165d.** With `sprites.ui.combat_ground` `"terrain"`
  (`resources_combat_ground_is_terrain`), the shell has set the combat ground
  before every fight (`combat_render_set_ground`, from
  `shell_promptdispatch.c`) to the hero's map tile, water falling back to
  grass, and drawn it under every cell and the siege band in place of
  `sprites.combat[0]`, which the manifest then omits. Absent or `"field"`, the
  field tile has drawn (`kings-bounty`).
- **REQ-165a.** When `sprites.ui.panel_frame` names a palette colour the
  legacy shell has drawn a frame round every panel slot
  (`legacy_panel_frame`: HUD panels, inventory cells, contract face) so the
  art carries none; absent, nothing has been drawn and the art has carried
  its own frame. A modern screen has drawn no panel frame whatever the key
  says.
- **REQ-163a.** A class entry has optionally carried a `hero` block (walk,
  idle, boat, tile, and the modern `disgraced` scene shown at temp death)
  parsed into `Resources.class_hero[]`
  (`resources_class_hero`); the shell has drawn the chosen class's sets for
  the map hero and the win-cartoon tile (`sprites_hero_anim`,
  `sprites_end_hero`) and fallen back to the pack-wide `sprites.hero` and
  `ending.hero_tile` for anything undeclared (`kings-bounty` declares none).
  The cartoon's grass backdrop has been `ending.grass_tile` when declared,
  else the map's `grass` tile from the tile cache, so `glory-of-rome` has
  declared neither `grass_tile` nor `hero_tile` and shipped neither file.
- **REQ-228b.** A wandering foe has been stamped with its zone's `army_art`
  stem when the zone declares one (`Map.army_art`, read by both foe stamp
  sites), else `wandering_army`; the map has drawn that tile for every foe.
  The manifest has listed each declared stem once and the shared tile only
  while some zone lacks one.
- **REQ-227a.** A zone that declares `tile_set` has had every terrain art
  name it stamps prefixed with `<tile_set>/` (`engine/map.c MapTerrainArt`),
  including the grass padding, the grass or water a cleared object reverts
  to, and bridge tiles built by the Bridge spell, so the shell's tile cache
  loads `art/tiles/<tile_set>/<art>.png`. Object art stamped from the zone
  lists has never been prefixed. A zone without the key has drawn the shared
  set. The art manifest has listed the shared terrain only while some zone
  draws it, and each declared set once.

### 9.7 Castle and town tile placement

- **REQ-228.** A castle has been stamped as one of two footprints, chosen per
  catalog entry by `castles[].footprint` (`engine/resources.c parse_castles`,
  `engine/map.c stamp_objects`):
  - **`3x2`**, the default when the key is absent: a block centred on the
    gate. The gate tile (interactive `CASTLE_GATE`, walkable) sits at
    `(x, y)`; the five surrounding tiles are wall pieces
    (`castle_tl/br/tr/ml/mr`, non-interactive, `blocks_foot = true`).
  - **`1x1`**: the gate tile alone, drawn with the single `castle` art, so the
    castle sits on the map the way a town does. No wall tiles; the eight
    neighbours keep their `.dat` terrain. The `castle` art is transparent: the
    renderer (`src/map_render.c map_render_draw`) draws a tile's plain terrain
    beneath every object tile before the object's own art, so the castle
    stands on the ground it occupies (ART-SPEC §4). An opaque object covers
    that ground.
  An unrecognised value has printed a notice and stamped `3x2`. Castles have
  been able to declare extra decorative wall pieces (the King's castle has
  24), honoured for either footprint. A town has been a single
  `INTERACT_TOWN` tile with a `boat_spawn_x/y` used when a boat is rented.
  When a castle's `gate` object is absent, the gate landing tile has been
  computed as `(x, y+1)`. The art manifest (`resources_art_manifest`) has
  listed a footprint's castle art only when some castle in the pack uses it
  (`map_castle_art_names`), so a pack ships only the pieces it stamps.
  Everything else about a castle -- the visit flow, sieges, garrisons,
  contracts, the Castle Gate landing, the foe doorstep rule -- has keyed off
  the gate tile and been the same for both footprints.

### 9.8 Terrain edge variants (baked, not generated)

- **REQ-229.** Every terrain except grass has shipped **twelve edge
  variants** alongside its plain tile (`water_edge_00..11`,
  `forest_edge_01..12`, `mountain_edge_01..12`, `desert_edge_01..12`; 48 of
  the reference pack's 54 tile codes). They have been the transition pieces
  that blend a terrain into its neighbour, and they have been **baked into
  the `.dat` files by the map author**, not generated at runtime. **A `.dat`
  has held the fully rendered map; nothing about its appearance has been
  computed at game time.** This has been a ratified decision, not an
  accident of implementation: `furnish_map` (`engine/game.c`) has been
  retained as a permanent **no-op** mirroring OpenKB's `spawn_game` call
  sequence, where a real furnishing pass (`rogue.c`, `OPENKB-SPEC.md` §12.5)
  rewrote base terrain bytes into edge bytes at load. OpenBounty has not done
  that. The consequences have been intended: a `.dat` is self-contained and
  renders identically in the game, in an editor, and in any third-party tool,
  with no shared algorithm to keep in agreement; load does no per-tile work;
  and a pack author's saved file is exactly what a player sees. A `.dat`
  written with only the plain terrain codes has therefore rendered with hard
  stair-stepped coastlines; `continentia` has used all 54 codes, and its edge
  tiles have outnumbered its plain ones.

- **REQ-229a.** The variant has been selected by which of the tile's eight
  neighbours carry a **different terrain**, cardinals taking precedence over
  diagonals. Two families have existed, differing in both base and
  permutation (matching OpenKB's `tile_offset` table, which gives water its
  own row):

  | Differing neighbours | water | forest / mountain / desert |
  |---|---|---|
  | N | `10` | `11` |
  | S | `11` | `12` |
  | E | `08` | `09` |
  | W | `09` | `10` |
  | N and E | `00` | `03` |
  | N and W | `01` | `01` |
  | S and W | `02` | `02` |
  | S and E | `03` | `04` |
  | NE only (no cardinal) | `05` | `06` |
  | SE only | `04` | `05` |
  | SW only | `06` | `07` |
  | NW only | `07` | `08` |

  Water has been 0-based (`00`–`11`); the other three 1-based (`01`–`12`)
  with no `00`. A tile with no differing neighbour has used the plain terrain
  code. Three or more differing cardinals (a one-tile spit) have used the
  REQ-229e variants.

- **REQ-229b.** This table has been **derived from the reference pack's
  maps**, by classifying every edge tile in all four zones by its neighbour
  pattern, then re-applied as a rule and measured against what the authors
  actually placed. Of the **7,870** edge tiles in the four zones it has
  reproduced **7,590 (96.4%)** exactly; **169 (2.1%)** have sat on patterns
  the table leaves undefined (three or more differing cardinals, or more than
  one differing diagonal with no cardinal), and **111 (1.4%)** have
  disagreed, the expected residue of a hand-drawn map. The convention has
  therefore been exact enough to author against, and a generator applying it
  produces coastlines indistinguishable from the originals.

- **REQ-229e.** Seven more variants have closed the shapes REQ-229a leaves
  undefined, keyed by the tile's OPEN (differing) cardinals: `13` N+S, `14`
  E+W (one-wide strips), `15` N+E+S, `16` E+S+W, `17` S+W+N, `18` W+N+E
  (spits, attached on the remaining side), `19` all four (an island). Water
  has been 0-based (`12`..`18`). `glory-of-rome` has shipped all seven for
  forest and mountain and the two strips and the island for water; its four
  zones have contained no other shape. `tools/mapbuild.py build` has assigned
  them when it bakes a map from its source; the art has come from the same
  lattice and stitching tools as the twelve, so every side that is open is a
  terminal edge and every closed side the standard interface.

- **REQ-229f.** A tile has kept its own terrain art (`Tile.ground`) beside
  the art it draws. An object stamped on a cell (a foe, a chest, a town) has
  replaced only the drawn art; the renderer has drawn `ground` beneath the
  object, and `MapClearInteractive` has restored `ground` when the object
  goes, so a road or a grass variant survives a foe walking over it. Only
  grass-terrain ground has been restored: on any other ground (desert, a
  dwelling on a mountain edge) the cleared cell has become plain grass, and
  water has stayed water.

### 9.9 Roads (grass-terrain tile codes)

- **REQ-229c.** A road has been a **tile, not an object**: a `tile_codes`
  entry with `terrain: grass` and its own art, exactly like `grass_variant`.
  The engine has needed no road concept: walkability and move cost come from
  the terrain (grass, cost 1), salt and foe logic see grass, and the renderer
  draws the entry's art. The map author has baked the road pieces into the
  `.dat` like the edge variants (REQ-229). `glory-of-rome` has shipped
  twenty-four pieces, codes `f`..`y` and `\x80`..`\x83`: straights
  `road_ns`/`road_ew`; the four curves `road_ne`, `road_es`, `road_sw`,
  `road_wn` (named by their two exits); the diagonals `road_nesw`/
  `road_nwse`; eight joins from a straight exit to a diagonal corner
  (`road_n_sw`, `road_n_se`, `road_s_nw`, `road_s_ne`, `road_e_nw`,
  `road_e_sw`, `road_w_ne`, `road_w_se`); and the four **companions**
  `road_c_nw/ne/sw/se`, grass with the road's triangle in one corner. A
  diagonal passes through a tile corner that two side neighbours share, so
  the author places the companions on those two cells (a `road_nwse` at
  (x, y) takes `road_c_sw` at (x+1, y) and `road_c_ne` at (x, y+1); a
  `road_nesw` takes `road_c_se` at (x-1, y)... see `tools/romeart.py sweep`).
  Last, the four **ends** `road_n`, `road_e`, `road_s`, `road_w`, named by
  their one exit: the road enters through that side at the full band width
  and stops inside the tile, so a run can finish in open grass rather than
  only where an object replaces its code (an object on a road cell does that,
  which is why a road needs no end piece beside a gate or town).

  Every straight exit has been a 32 px band centred on the side and every
  diagonal exit the same corner triangle, so any piece joins any other, ends
  included; `tools/romeart.py sweep` has checked that contract on every run.
  The pieces have not been drawn by hand: the tool sweeps them out of a
  PixelLab terrain set, filling each piece's signed-distance shape with the
  set's road tile and leaving the pack's own grass outside (see
  docs/ART-PIPELINE.md). Rome's surface has been cobblestone, from
  `art/jobs/t32_cobble_203.json`, with a two-pixel edging course a shade
  darker than the paving painted by the sweep's `--rim` / `--rim-shade` --
  the set's own transition tiles are discarded, so an edging described in a
  prompt never reaches the game. An end's last stretch has been cut off at a
  slanted front and frayed by the boundary noise, so the paving breaks up into
  loose stones instead of tapering to a point; the fray has been scaled to
  zero at the exit side, where the contract has to hold exactly.

- **REQ-229d.** A tile code has been able to declare cosmetic **`variants`**,
  any number of art names (heap) with the same terrain and flags (a name may repeat to
  weight it; the base art counts once more). The shell (`src/tilevar.c`) has
  picked one per cell when it draws, from the cell's x, y and a seed drawn
  once per launch, so a field of one code is not a single stamp and shuffles
  between launches. This has been the one stated exception to "nothing about
  appearance is computed at game time" (REQ-229): the choice has been
  draw-time only and cosmetic. The `.dat`, the engine, saves, replays and
  byte determinism have never seen it. The ground drawn under an object has
  gone through the same pick. Every variant has had to join every other and
  the base at any edge, which the pack guarantees by keeping variant edges
  identical to the base (`glory-of-rome`: `grass_01..10` on `grass_variant`,
  the grass with a patch of dry grass inside, from `tools/romeart.py grass`). The legacy pack
  has declared no variants.

## 10. Salt: per-zone object placement

### 10.1 Salt budget

- **REQ-229g.** **The magic alcove has named its own art.** A zone's
  `alcove_art` has named the alcove's map tile (per zone, exactly as
  `army_art` does), and `sprites.ui.alcove_backdrop`, `sprites.ui.alcove_figure`
  and `sprites.ui.alcove_figure_animation` have named the location backdrop
  and the figure's frames. Each has been optional and each has fallen back to
  the reference pack's choice: the hills-dwelling tile, the hill cave's
  backdrop and the `gnomes` troop sprite, which is what `kings-bounty`
  shows. The figure has cycled on the troop tick and filled the same
  tile-shaped slot a troop strip does, so the backdrop geometry has been one
  rule for both. The location kind `SCREEN_LOC_ALCOVE` (7,
  `src/screens/alcove.c`) has existed so the screen asks for its own
  backdrop instead of the hill cave's.
- **REQ-230.** Each zone has declared a `salt` block: `artifacts`, `navmaps`,
  `orbs`, `telecaves`, `dwellings`, `friendly_foes` (counts, each 0 when
  absent), plus `preferred_troops[]` and `dwelling_range[lo, hi]` for
  dwelling troop selection. Every zone of both packs has declared
  artifacts=2, navmaps=1, orbs=1, telecaves=2, dwellings=10,
  friendly_foes=5.

### 10.2 Algorithm

- **REQ-231.** `salt_continent` (`engine/game.c`) has run once per zone at
  game init. It has: (1) registered every static foe army on the zone as a
  hostile `FoeState`; (2) built a barrel of `chest_count` slots (one per chest
  placeholder), each tagged `SALT_NONE`; (3) for each kind in order
  (artifacts, navmaps, orbs, telecaves, dwellings, friendly foes) repeatedly
  picked a random unclaimed barrel slot and tagged it, until the quota is
  met or a guard counter (`barrel_len * 20`) runs out; (4) walked the
  barrel and emitted a `SaltedPlacement` (or `FoeState`) per tagged slot.
  When the guard runs out early, the missing items have been silently
  skipped. Chests declared `fixed` have not entered the barrel, and a zone
  with fewer placeholders than the budget has been skipped whole.

### 10.3 Slot semantics

- **REQ-232.** `SALT_ARTIFACT` has placed `INTERACT_ARTIFACT` by matching
  `(zone, local_idx)` against the artifact catalog. `SALT_NAVMAP`/`SALT_ORB`/
  `SALT_TELECAVE` have placed the corresponding interactive with id
  `<kind>_<n>`. `SALT_DWELLING` has picked a troop (zone `preferred_troops[]`
  first, else `dwelling_range`), derived the dwelling kind from the troop's
  `dwelling` field, placed `INTERACT_DWELLING_*`, and registered a pinned
  `DwellingState`. `SALT_FRIENDLY` has created a `FoeState` with
  `friendly = true` and a placeholder garrison (re-rolled fresh on accept,
  §15.5).

### 10.4 Salt of villains

- **REQ-233.** `salt_villains` (`engine/game.c`) has run after
  `salt_continent`. For each villain in catalog order, a retry loop (guarded
  by `ncastles * 20` tries, castles excluded from contracts at the tail of
  the list trimmed from the draw) has picked a random castle in the
  villain's declared `zone` that is not excluded from contracts and not
  already villain-owned; the chosen castle has got `owner_kind =
  CASTLE_OWNER_VILLAIN`, `villain_id`, and a garrison populated from the
  villain's pre-built army.

### 10.5 Salt of spells

- **REQ-234.** `salt_spells` (`engine/game.c`) has assigned exactly one spell
  to each town's `spell_for_sale` in three phases: (1) **pinned**: each town
  with a non-empty `pinned_spell` receives it and the spell is marked
  claimed; (2) **random**: each unclaimed spell is placed in a random
  spell-less town; (3) **fallback**: any still-empty town receives a random
  spell from the fully-claimed pool.

### 10.6 Scepter burial

- **REQ-235.** `bury_scepter` (`engine/game.c`) has taken the zone index
  `GameInitSeeded` draws as `game_rng_next(0, 3)` (the four continents of
  the reference world; a draw past the pack's zone count buries nothing),
  loaded its map, counted all tiles whose terrain
  is `TERRAIN_GRASS`, interactive is `INTERACT_NONE`, and `blocks_foot` is
  false; picked the Nth such tile (N uniform in `[0, count-1]`); and stored
  the tile's zone id, x, and y in `Game.scepter`. The scepter has not been
  visible on the map; searching (key `S`) on the buried tile has triggered
  the win flow (§26).

---

## 11. Movement, mounts, and travel

### 11.1 Eight-directional movement

- **REQ-240.** Movement has supported four cardinals (arrows, numpad
  2/4/6/8) and four diagonals (numpad 7/9/1/3 or Home/PgUp/End/PgDn), as
  single-step `(±1, ±1)` deltas, plus a tap on the map in modern mode. The
  one-tile step entry point has been `GameStep` (`engine/step.c`);
  walkability has been decided by `engine/adventure.c`.
- **REQ-241.** A blocked move has played the bump sound and left position,
  facing, and travel mode unchanged. A successful move has updated
  `position.x/y`, set `facing_left = (dx < 0)`, revealed the fog at the new
  position (`FogRevealFor`: exactly the pack's declared viewport,
  `render.tiles_w` x `tiles_h`, around the hero -- 5x5 in both packs -- so the tile past each edge of the view stays
  unexplored until walked towards and shows the fog fade, the same in both
  modes), and called `GameOnStep`.

### 11.2 Walking, sailing, flying

- **REQ-242.** While walking, walkability has followed §9.3. In a boat, the
  hero has been able to enter water, bridge, and grass/desert tiles (the last
  triggering disembark). While flying, every terrain has been walkable and
  interactive tiles have not fired.
- **REQ-243.** Stepping from walk mode onto the parked boat has set
  `travel_mode = TRAVEL_BOAT`. Stepping in boat mode from water/bridge onto
  land has parked the boat at the previous water tile and set `travel_mode =
  TRAVEL_WALK`. Boat-mode movement on land has only ever been a single
  disembark step.

### 11.3 Boat rental

- **REQ-244.** A boat has been rented at any town menu (`B`) for
  `GameBoatCost` (`economy.boat_cost_normal = 500`, or `boat_cost_cheap = 100`
  with the Anchor of Admirability) and parked at the town's `boat_x/boat_y`.
  Rental has been cancellable at the same menu; while the hero is still
  sailing (`TRAVEL_BOAT`) cancellation has been refused
  (`GameCancelBoat`). A rented boat has cost one weekly
  rental at each week boundary; on bankruptcy it has been repossessed.

### 11.4 Sail navigation

- **REQ-245.** In boat mode, key `N` has opened a prompt listing those
  `neighbors` of the current zone whose navmap has been found
  (`zones_discovered`); selecting one has called `GameSwitchZone`
  and `GameSpendWeek` (one week passes during the journey), with the scene
  and confirmation of REQ-221c where the pack declares them. Outside boat
  mode, `N` has produced a "must be sailing" dialog and consumed no time.

### 11.5 Bounce-back

- **REQ-246.** When the hero has stepped onto a tile that opens a bouncing
  interactive flow (towns, castles, dwellings, alcove, hostile foes) and the
  player dismisses it without committing, position/travel-mode/boat coords
  have reverted to the pre-step values. Non-bouncing interactives (chest,
  navmap, orb, telecave, sign, friendly-foe accept, artifact) have left the
  hero on the destination tile.

---

## 12. Adventure-mode actions

### 12.1 Action key bindings

- **REQ-250.** Adventure-mode keys (one action per press; no autorepeat
  within a turn), dispatched through `src/shell_actions.c` / `src/input.c`,
  have been: `A` view army, `C` view controls, `D` dismiss army, `F` fly, `I`
  view contract, `L` land, `M` worldmap, `N` navigate (sail), `O` options
  (legacy) or the game menu (modern), `P` view puzzle, `Q` save-and-quit,
  `Ctrl+Q` fast quit, `S` search, `U` cast spell, `V` view character, `W` end
  week, `Numpad 5` (and, in modern mode, the number-row `5`) rest one day,
  `Esc` close overlay (and, in modern mode with nothing open, the game
  menu). Cheats have been reached only through the Debug page, with
  `--debug` (§31).

### 12.2 Gamepad mapping

- **REQ-251.** D-pad / left stick → 8-direction movement; A → search; X →
  cast spell; Y → end week; LB → army; RB → character; LT → fly; RT → land;
  Start → worldmap; Back → options; B → cancel.

### 12.3 Search / dismiss / end week

- **REQ-252.** `S` has opened a yes/no prompt from the `body_search` string
  ("It will take %DAYS% days to search this area. Search?", with
  `tuning.search_cost_days`); Yes has compared zone/x/y to `Game.scepter`:
  on a match the win flow has fired; otherwise
  `GameSpendDays(search_cost_days)` has run and the "revealed nothing"
  banner has shown.
- **REQ-253.** `D` has opened a numeric (1..5) prompt of non-empty slots.
  Choosing any but the last has zeroed that troop and run `GameCompactArmy`.
  Choosing the last remaining troop has chained into a yes/no prompt; Yes has
  run temp death (`GameTempDeath`: clear army, zero siege weapons, grant the
  pack's `tuning.temp_death` troop and count, by default the cheapest
  recruit in the catalog times 20, which is 20 peasants in both packs, drop
  the boat rental, teleport to the home spawn, dismount).
- **REQ-254.** `W` has called `GameSpendWeek` and scheduled the post-week
  dialog sequence (§24.5); running out of days has fired the lose flow.

---

## 13. Troops and the army

### 13.1 Catalog

- **REQ-260.** The troop catalog (`game.json:troops[]`; `TroopDef` in
  `engine/include/tables.h`) has been sized by the pack: 25 entries in
  `kings-bounty`, indexed 0..24 (§Appendix A), and 27 in `glory-of-rome`.
  Each troop has carried: `id`, `name`, `sprite`, `portrait` (modern still),
  `anim` (heap frame list), `skill_level`, `hit_points`, `move_rate`,
  `melee_min`/`melee_max`, `ranged_min`/`ranged_max`/`ranged_ammo`,
  `recruit_cost`, `spoils_factor`, an ability mask, `dwelling` kind,
  `max_population`, `growth_per_week`, a morale group A..E, and a
  `tier_counts[4]` row (foe-garrison troop sizes per continent tier, used by
  `roll_creature`, §15.4).

### 13.2 Ability flags

- **REQ-261.** The 8-bit ability mask (§3.5): `FLY` (flies over impassable
  terrain; required for hero flight), `REGEN` (regenerates HP between
  rounds), `MAGIC` (casts combat spells / fixed ranged), `IMMUNE` (immune to
  magic damage), `ABSORB` (converted to peasants on a Peasants astrology
  week, and grows on kills in combat), `LEECH` (heals from melee dealt),
  `SCYTHE` (an 11-in-100 roll that slays half the target troop on top of
  the blow, REQ-385), `UNDEAD` (the only troops Turn Undead bites; morale
  has applied to them like any other troop).

### 13.3 The army

- **REQ-262.** The player army has had five slots (`GAME_ARMY_SLOTS = 5`);
  each has held a troop id and count. Adding a troop that matches an existing
  slot has incremented it; otherwise the first empty slot has been used; with
  no empty slot and no match, the add has failed and the source flow has
  shown a banner. `GameCompactArmy` has kept non-empty slots contiguous,
  preserving order.

### 13.4 Recruitment cap and upkeep

- **REQ-263.** `GameMaxRecruitable(troop_id)`: `same_troop_consumed =
  sum(slot.count * troop.hp)` over slots of that troop; `free_leadership =
  leadership_current - same_troop_consumed`; the result has been
  `free_leadership / troop.hp` (0 / "n/a" when free leadership is negative).
  Buying has cost `recruit_cost * count`; the gold check has used strict `<`
  (insufficient when `gold < cost`).
- **REQ-264.** At each week boundary, each non-empty slot has paid `upkeep =
  count * (recruit_cost / 10)` gold (integer division), deducted after
  commission is credited.

---

## 14. Morale and out-of-control

### 14.1 Morale groups and chart

- **REQ-270.** Each troop has belonged to one of five groups A..E (§Appendix
  A). The 5×5 morale chart (`game.json:combat.morale_chart`):

  ```
           A   B   C   D   E
      A    N   N   N   N   N
      B    N   N   N   N   N
      C    N   N   H   N   N
      D    L   N   L   H   N
      E    L   L   L   N   N
  ```

  Row = the troop whose morale is computed; column = another troop present.
- **REQ-271.** Per-troop morale: a troop alone in the army has been High;
  otherwise each other non-empty slot has been consulted on the chart and the
  results counted: any `L` → Low; all `H` (≥1) → High; else Normal.

### 14.2 Out-of-control

- **REQ-272.** A troop has been out of control (OOC) when
  `(troop.hp * count) > leadership_current`. In the army view this has shown
  as "Out of Control" in red (mutually exclusive with the Low/Normal/High
  labels). In combat an OOC unit has still taken turns but attacked its own
  side (§25.4).

---

## 15. Foes, encounters, and recruiting

### 15.1 Foe state and sources

- **REQ-280.** A `FoeState` (§4.3) has held zone, `(x,y)`, `placement_id`,
  `alive`, `friendly`, and a 5-troop garrison. The foe list has grown as foes
  are salted, and static zone armies and salt-placed friendly foes have
  shared it. Keeping OpenKB's per-continent split, `salt_continent` has
  raised every army a zone declares in `wandering_armies` (no cap: the list
  is sized by the pack; King's Bounty declares at most 35 a zone, OpenKB's
  `foe_coords[4][40]` less 5 friendly) plus its `friendly_foes` friendlies.
  Hostile foes have come from the zone's `wandering_armies` (held as
  `ResZone.armies[]`) with garrisons pre-rolled at salt time
  (`roll_hostile_garrison`) unless the entry declares its own `army`;
  friendly foes have been salt-placed with placeholder garrisons re-rolled
  on join (§15.5).

### 15.2 Spawn pool

- **REQ-281.** `tier_chance_curve[4][4]` (`game.json:spawn.tier_chance_curve`):

  ```
  Tier 0 (Continentia): [60, 90, 98, 101]
  Tier 1 (Forestria):   [20, 70, 95, 99]
  Tier 2 (Archipelia):  [10, 20, 50, 90]
  Tier 3 (Saharia):     [3,  6,  10, 40]
  ```

  `spawn.kind_chance_curve` has been an optional per-kind override, one
  curve set per kind or `null` to keep the tier curve; `glory-of-rome` has
  given its six-troop plains kind five thresholds a tier and left the other
  three kinds on the tier curve. `kings-bounty` has declared none.

- **REQ-282.** `tier_troop_pool` (`game.json:spawn.tier_troop_pool`, one
  troop list per kind, sized by the pack; `kings-bounty` five a kind,
  `glory-of-rome` six on the plains with `archers` third):

  ```
  Kind 0 (plains):  peasants, wolves, nomads, barbarians, archmages
  Kind 1 (forest):  sprites, gnomes, elves, trolls, druids
  Kind 2 (hill):    orcs, dwarves, ogres, giants, dragons
  Kind 3 (dungeon): skeletons, zombies, ghosts, vampires, demons
  ```

### 15.3 Roll

- **REQ-283.** `roll_creature(continent_tier)` (`engine/game.c`, the castle
  roll): `kind = rng(0,3)`; `chance = rng(1,100)`; walk the kind's curve for
  the smallest slot where `chance <= curve[slot]` (else the last slot,
  `resources_spawn_slot`); `troop_id = troop_pool[kind][slot]`; `count =
  troop.tier_counts[tier]`, clamped to ≥ 2, with no jitter.
  `roll_hostile_garrison` (the foe roll) has filled 1..3 slots
  (`1 + rng(0,2)`), each with its own `kind` and `chance` rolls through the
  same slot walk and `count = base + rng(0, base / 2)` where `base` is
  `tier_counts[tier]` clamped to ≥ 2.

### 15.4 Encounter flows

- **REQ-284.** Stepping onto a hostile foe has shown the garrison (the foe
  view in modern mode) and a fight decision: Fight has entered combat, No /
  Evade has bounced back. A static guardian's fight has not been declinable
  (`pending_foe_forced`): its decision has offered Fight alone. Stepping onto
  a friendly foe has re-rolled a fresh
  troop offer; Yes with a free slot has run `GameAddTroop` (no gold cost) and
  consumed the foe; Yes with no slot, or No, has consumed the foe with the
  "flee in terror" banner.

### 15.5 Hero-on-foe

- **REQ-285.** Hostile (and friendly) foes have proactively walked toward the
  hero each turn via `GameFoesFollow` (`engine/game.c`): for each foe within
  Chebyshev distance 2 of the hero's *previous* position, all 9 cells of the
  foe's 3×3 neighborhood have been scored by Euclidean distance to that
  position (`foe_dist_sq`); unwalkable or occupied non-center cells have been
  skipped; the hero's current tile has **not** been excluded (landing on it
  is the combat trigger); a hero afloat has sat on water no foe can stand
  on. The foe has moved to the lowest-score cell; if that is the
  hero's tile, `GameFoesFollow` has returned the foe index so the caller fires
  the attack/recruit flow. Foe motion has not consumed the hero's day budget
  and has stopped at impassable terrain. A static guardian has never moved;
  a foe has never stepped onto another foe, onto desert, a bridge, a town or
  any other interactive tile, or into the approach of a castle gate; and a
  flying hero's tile has been skipped outside oracle mode. A hostile foe that lands on the hero
  has opened the same Fight/Evade decision as stepping onto it, and declining
  has bounced back the same way (REQ-246): the hero returns to the tile,
  travel mode and boat of before that step, and the foe, unstamped while it
  shared the hero's tile, is stamped where it stands so it is drawn
  (`flow_apply_evade_bounce`; openKB `game.c`: `walk = !attack_foe(game)`
  swaps the hero back to `last_x, last_y`).

---

## 16. Towns

- **REQ-290.** The town catalog (`game.json:towns[]`) has been sized by the
  pack: 26 towns in both shipped packs, `kings-bounty` naming one per letter
  A..Z. That naming has been a convention of that pack, not an engine
  requirement: a pack may declare any number and name them freely (REQ-322
  selects gate destinations from a list, not by first letter). Each town has
  carried id, name, zone, `(x,y)`, gate coords, boat coords, an intel castle,
  and an optional pinned spell (full table in §Appendix A). Each `TownRecord`
  has tracked `visited` and `spell_for_sale`.
- **REQ-291.** The town view (`src/screens/` / `src/views_render.c`, and the
  modern town screen of `DESIGN-SPEC.md` DSGN-0140) has offered five actions:
  - **A) Get New Contract**: `GameTakeNextContract`; shows the villain id,
    reward, and last-known zone, or "no contracts" when none remain.
  - **B) Rent / Cancel boat**: toggles `boat.has_boat` at `GameBoatCost`;
    refuses mid-sail; strict `<` gold check.
  - **C) Gather information**: intel on the town's `intel_castle`: name,
    owner, and garrison list with `GameNumberName` bucket labels; unavailable
    when the castle is excluded from intel.
  - **D) `<Spell>` spell (`<cost>`)**: buys `spell_for_sale` if
    `GameKnownSpells < max_spells` and `gold > cost` (strict, OpenKB-faithful);
    deducts gold, increments the spell count. In `glory-of-rome` a zone's
    temple has taught only once that zone's rites are known (REQ-314a).
  - **E) Buy siege weapons (3000) / owned**: sets `siege_weapons = 1`; the
    flag has gated the enemy castle gate (REQ-303, REQ-394).
  Every action has been at-most-once per visit; the view has persisted until
  `Esc`.

---

## 17. Castles

### 17.1 Catalog

- **REQ-300.** The castle catalog has been sized by the pack: `kings-bounty`
  has held **27** castles, 26 villain/monster castles (named A..Z by that
  pack's convention) plus King Maximus's castle (full table + difficulty
  tiers in §Appendix A); `glory-of-rome` has held 26. No count has been
  required (§3.1), but a pack has had to declare, per zone, more
  contract-eligible castles than that zone's villain count, or
  `salt_villains`' retry loop (REQ-233) exhausts its guard and villains
  silently fail to place. Each `CastleRecord` has tracked `visited`, `known`,
  `owner_kind`, `villain_id` (when applicable), and a 5-slot garrison.

### 17.2 Owner kinds

- **REQ-301.** `CASTLE_OWNER_VILLAIN` has been populated by `salt_villains`,
  its garrison the villain's pre-built army. `CASTLE_OWNER_MONSTERS` has been
  populated by `repopulate_castle` using the castle's `difficulty_tier` (5×
  `roll_creature`). `CASTLE_OWNER_PLAYER` has followed capture and supported
  garrison/ungarrison via the Own Castle view. `CASTLE_OWNER_SPECIAL` has been
  only the King's castle: never siegeable, never repopulated.

### 17.3 Repopulation

- **REQ-302.** At game init, every monster castle has been seeded by
  `repopulate_castle`. A player-owned castle has never been repopulated: an
  emptied garrison has stayed empty. Astrology growth
  (§24.3) has applied weekly to non-player castle troops matching the
  astrology creature.

### 17.4 Visit / siege / own / audience

- **REQ-303.** Stepping onto a castle gate: player-owned → **Own Castle**
  view; monsters → siege-monster flow; villain → siege-villain flow; the
  King's castle → audience. Without siege weapons a monster or villain gate
  has bounced the hero silently, as the original did (REQ-394). With them a
  siege flow has shown a yes/no prompt, deliberately without the garrison;
  Yes has entered combat, No has bounced back. Any visit has set a villain
  castle `known`. A combat win has set
  `owner_kind = CASTLE_OWNER_PLAYER`; a villain-castle win has additionally
  fulfilled the contract (§21.3).
- **REQ-304.** **Own Castle** (`src/screens/own_castle.c`) has shown the
  5-slot garrison with letter selectors A..E plus Space to toggle direction
  (army ↔ garrison); in modern mode, Garrison and Withdraw pages with a
  How-many step (`docs/DESIGN-SPEC.md`). Each transfer has merged into a
  matching slot or filled the first empty one; the player army has never been
  allowed to become entirely empty.
- **REQ-305.** **King's audience** (`src/shell_audience.c`,
  `src/screens/home_castle.c`) has shown the intro, a rank-up report when the
  next rank's `villains_needed` is met, a "capture N more villains" message
  otherwise, and a final-rank "recover my Scepter" line. The King's castle
  has doubled as the **Home Castle** view, which also offers (`A`) Recruit
  Soldiers for castle-class troops (`src/screens/recruit_soldiers.c`). With
  `audiences` in `game.json` the modern audience has offered Promotion,
  Blessing and Tribute (REQ-430q).

---

## 18. Dwellings

- **REQ-310.** Dwelling kinds: `plains`, `forest`, `hills` (singular `hill`
  in the troop catalog), `dungeon`; each `TroopDef` has named exactly one via
  its `dwelling` field. Castle-kind troops (militia, archers, pikemen,
  knights, cavalry in `kings-bounty`) have been recruitable only at the home
  castle's recruit screen, from a pool without limit; their `max_population`
  has been 0 except knights' 250, which nothing has read.
- **REQ-311.** Each `DwellingState` has tracked `(zone,x,y)`, `troop_id`,
  `max_population`, `count`; the list has grown as dwellings are created. A
  dwelling has been created lazily on first visit if not salt-placed; the
  troop has been picked deterministically by `(seed, x, y)`
  (`GameDwellingTroopAt`); the initial `count` has been the troop's
  `max_population`. Salt-placed dwellings have been pinned to their troop.
- **REQ-312.** Visiting a dwelling has bounced the hero and opened the
  dwelling screen (`src/screens/dwelling.c`) showing kind, count, cost per
  unit, gold, and the `recruit_cost * count <= gold && count <=
  GameMaxRecruitable` clamp. Yes + a count has run `GameBuyTroop` (deduct
  gold, add to army, decrement dwelling count).
- **REQ-313.** **Astrology growth** (§24.2): only dwellings whose `troop_id`
  matches the astrology creature have refilled to `max_population`;
  non-matching dwellings have kept their current count.
- **REQ-314.** **Alcove** (Aurange): each zone has declared one
  `magic_alcove_x/y` (`INTERACT_ALCOVE`). When the class sets `knows_magic`
  at start (Sorceress), every alcove has been marked consumed at init.
  Visiting an unconsumed alcove (`src/screens/alcove.c`) has bounced the hero
  and offered magic for `GameAlcoveCost`: the zone's `alcove_cost` when it
  declares one, else `economy.alcove_cost` (5000 in `kings-bounty`). Yes +
  sufficient gold (strict `<`) has set `knows_magic`, deducted the cost and
  consumed the tile (`flow_apply_alcove`, `engine/flow_resolve.c`).
- **REQ-314a.** **Rites per zone.** With `game.json` `magic.rites_per_zone`
  true (`glory-of-rome`), magic has been learned zone by zone: each zone's
  alcove (the Augur) has taught that zone's rites (`Game.world.zone_rites`),
  a town has sold its spell only when the hero holds the rites of the town's
  zone (`GameTownHasRites`; `GameBuySpell` answers `SPELL_BUY_NO_RITES`
  otherwise, and the temple names the Augur's position), and a class that
  knows magic at creation has started with the home zone's rites only, only
  that alcove consumed. Rome's zones have priced their rites at 2,500 /
  5,000 / 7,500 / 10,000. Without the key, one purchase has taught magic
  everywhere.

---

## 19. Spells: catalog and adventure casts

### 19.1 Catalog

- **REQ-320.** The spell catalog has held 14 spells (`game.json:spells[]`),
  indexed 0..13: combat (0..6), clone(2000), teleport(500), fireball(1500),
  lightning(500), freeze(300), resurrect(5000), turn_undead(2000); adventure
  (7..13), bridge(100), time_stop(200), find_villain(1000),
  castle_gate(1000), town_gate(500), instant_army(1000), raise_control(500).
  (Full table in §Appendix A.)

### 19.2 Counts, storage, buying

- **REQ-321.** `Game.spells.counts` has held one count per spell.
  `GameKnownSpells` has summed them; `max_spells` has capped purchases;
  casting has decremented, buying incremented. Spells have been bought at the
  town menu (§16) for the spell's `cost`. Buying has not required
  `knows_magic` (OpenKB-faithful) except under rites per zone (REQ-314a);
  casting has. The gold check has been strict: a purchase fails when `gold <=
  cost`.

### 19.3 Adventure spell effects

- **REQ-322.** Adventure spells have been implemented in
  `engine/spells_adventure.c` (dispatched via `dispatch_adventure_spell`),
  with `GameCastTimeStop` / `GameCastFindVillain` exposed directly on
  `engine/include/game.h`. Modal continuations have routed through
  `engine/include/pending.h`.
  - **Bridge** (100): on cast, awaits a direction key; then converts the
    first one or two consecutive water or river tiles in `(dx,dy)` to
    `is_bridge` grass with the appropriate art (river tiles taking the
    `bridge_river_*` pieces), stopping at the first tile that is neither.
    The count decrements only on success.
  - **Time Stop** (200): adds `spell_power * 10` to `time_stop`, with no
    floor.
    Steps in the time-stop window do not advance the day.
  - **Find Villain** (1000): scans castles for the active contract's villain
    and sets that castle `known = true`. With no active contract, no effect
    and no decrement.
  - **Castle Gate** (1000) / **Town Gate** (500): open a cursored destination
    picker over the visited, gate-eligible castles / towns. The cast function
    (`cast_castle_gate` / `cast_town_gate`) counts eligible destinations,
    shows the "none" banner and stops when there are none, and otherwise arms
    `gate_state = GATE_STATE_SELECT` plus `gate_mode` (0 castle / 1 town). The
    shell pump (`src/shell_gate.c gate_menu_tick`) then asks the engine for
    the list through `GameGateDestinations` and opens `VIEW_GATE`
    (`views_gate_open`); the view's input branch calls `GameGateTeleport`
    (boat-aware) on selection. The charge is consumed only on a committed
    teleport, never on cast or cancel. Castle Gate excludes the home/audience
    castle. Cross-zone destinations are allowed. Town Gate's default landing
    is the town's own tile when no gate is declared. Destinations are
    chosen from a list, **not** addressed by first letter: the engine builds
    `GateDestination` rows (`engine/include/game.h`: display `name`,
    destination `zone`, landing `x`/`y`) so neither the shell nor autoplay
    reaches into `g->towns` / `g->castles` directly. Nothing has required
    castle or town names to begin with distinct letters; the list has been
    sized by the engine (`GameGateDestsMax`).
  - **Instant Army** (1000): resolves the troop via
    `class.ranks[rank].instant_army` and count `(spell_power + 1) *
    [3,2,1,1][rank]` (§8.5), added via `GameAddTroop` (free). No empty slot →
    no-room banner, no consume.
  - **Raise Control** (500): adds `spell_power * 100` to `leadership_current`.

### 19.4 Casting gate

- **REQ-323.** `U` in adventure mode with `knows_magic` has opened
  `VIEW_SPELLS` (combat spells 0..6 left column, adventure 7..13 right);
  without it, a "no magic" dialog has hinted at the alcove. Only adventure
  spells have been castable from adventure mode; combat spells have fired
  from the in-combat menu (§25.8).

---

## 20. Artifacts

### 20.1 Catalog and placement

- **REQ-330.** The artifact catalog has held 8 artifacts
  (`game.json:artifacts[]`; full table in §Appendix A). Each zone has held
  two (`local_idx` 0 and 1); `salt_continent` has placed artifact tiles by
  matching `(zone, local_idx)`.

### 20.2 Pickup and powers

- **REQ-331.** Stepping onto an artifact tile has called
  `GameClaimArtifact(idx)`: set `artifacts.found[idx]`, applied the instant
  power, consumed the tile, and shown the banner.
- **REQ-332.** Pickup-time (instant) effects: `DOUBLE_LEADERSHIP` (Crown),
  `leadership_base *= 2; leadership_current = leadership_base`;
  `INCREASE_COMMISSION` (Articles), `commission_weekly += 2000`;
  `DOUBLE_SPELL_POWER` (Amulet), `spell_power *= 2`; `DOUBLE_MAX_SPELLS`
  (Ring), `max_spells *= 2`.
- **REQ-333.** Live-checked (query-time) effects via `GameHasPower`:
  `INCREASED_DAMAGE` (Sword), combat damage +50%; `QUARTER_PROTECTION`
  (Shield), combat damage taken ×0.75; `CHEAPER_BOATS` (Anchor), boat rental
  100 instead of 500; `UNKNOWN` (Book of Necros), no effect, faithful to the
  unimplemented original.

---

## 21. Villains and contracts

### 21.1 Catalog

- **REQ-340.** The villain catalog has held 17 villains
  (`game.json:villains[]`; full table + descriptions in §Appendix A).
  Per-zone counts: `[6, 4, 4, 3]` (Continentia, Forestria, Archipelia,
  Saharia; Italia, Galliae, Africa, Oriens in Rome). Each villain has had a
  fixed army of up to five troops, copied verbatim into its host castle at
  salt time.

### 21.2 Contract cycle

- **REQ-341.** `Contract` has held `active_id`, `cycle` (rotating villain
  ids, `contract.cycle_length` = 5), `last_contract` (index 0..4 of the most
  recently issued slot), `max_contract` (next villain to rotate in), and
  `villains_caught`. At init, `cycle` has been seeded with the first five
  villain ids, `last_contract = 4`, `max_contract = 5`.
  `GameTakeNextContract` has incremented `last_contract` (wrapping 0..4) and
  copied `cycle[last_contract]` to `active_id`.

### 21.3 Fulfilment

- **REQ-342.** A combat win on a villain castle has called
  `GameFulfillContract(villain_id)`: credited `gold += villain.reward`, set
  the caught flag, cleared `active_id`, and rotated the next uncaught villain
  (from `max_contract`) into the cycle slot the caught villain held,
  incrementing `max_contract`. Capture has also set the castle's `owner_kind =
  CASTLE_OWNER_PLAYER` and, under `oracle_mode` only (REQ-208), promoted the
  hero at once; in play the rank has waited for the audience (§8.4). A win on
  a villain castle without the matching contract has freed the lord instead:
  the castle has kept its owner, its garrison has been rebuilt and the
  villain marked `villains_prefought`, so catching him has needed the
  contract and a second fight. The Find
  Villain spell has marked the active contract's castle `known = true`
  (§19.3).

---

## 22. Chests and rewards

### 22.1 Chest table and roll

- **REQ-350.** `economy.chest` has held chance/value tables indexed by zone
  tier (0..3); the `kings-bounty` values:

  ```
  chance_gold        = [61, 66, 76, 71]
  chance_commission  = [81, 86, 86, 81]
  chance_spell_power = [83, 89, 89, 86]
  chance_max_spells  = [86, 92, 93, 91]
  chance_new_spell   = [101, 101, 101, 101]
  gold_min           = [0, 4, 9, 19]
  gold_max           = [5, 16, 21, 31]
  commission_min     = [9, 49, 99, 199]
  commission_max     = [41, 51, 101, 301]
  max_spells_base    = [1, 1, 2, 2]
  ```

- **REQ-351.** `GameRollChest(zone, x, y)` (`engine/game.c`) has rolled
  `chance = (chest_rand(g,x,y,1) % 100) + 1` and walked cumulative thresholds
  in order (gold → commission → spell_power → max_spells → new_spell → empty),
  taking the first whose threshold exceeds `chance`. `chance_spell_power`
  has sat three to five points below `chance_max_spells` in every tier, so
  the **max_spells** outcome has had that window (REQ-525). A chest with a
  declared `gold` has rolled nothing (REQ-230d).
- **REQ-352.** `GamePeekChest` has been a read-only equivalent (no mutation)
  used by the autoplay planner (§36) to pre-plan chest choices.

### 22.2 Outcomes

- **REQ-353.** The outcomes have been: **Gold**: `points = gold_min + 1 +
  rand % gold_max` (from `gold_min + 1` to `gold_min + gold_max`); `gold =
  points * 100`; an A/B prompt offers take-gold vs distribute (`+leadership
  = gold/50`, doubled with `DOUBLE_LEADERSHIP`). Resolved via
  `GameAcceptChestGold` / `GameAcceptChestLeadership`. **Commission**:
  `commission_weekly += commission_min + 1 + rand % commission_max`. **Spell power**: `+1`. **Max spells**
  `+= max_spells_base[zi]`, doubled by `DOUBLE_MAX_SPELLS`.
  **New spell**: a random spell, count `(chest_rand % (zi+1)) + 1`. **Empty**:
  no change. Every outcome has consumed the chest tile (added to
  `consumed`).

### 22.3 Other chest types

- **REQ-354.** A **navmap chest** has revealed the zone its id's digits name,
  else the first undiscovered zone (`world.zones_discovered`), and been
  consumed. An **orb chest** has revealed the entire current zone's fog
  (`world.orbs_found[zi] = true`); the worldmap has then supported a toggle
  between fog-gated and fully-revealed views. **Telecaves**, drawn with the
  dungeon dwelling's tile, have been paired by index within a zone (0↔1,
  2↔3); stepping on one has teleported to its pair (an odd telecave is a
  one-way dead-end). **Signposts**: each sign tile has carried per-tile
  `sign_title` / `sign_body` shown in a dialog; there has been no global sign
  index.

---

## 23. Economy: gold, commission, upkeep

### 23.1 Sources and sinks

- **REQ-360.** Gold sources have been: starting gold
  (`class.starting_gold`), weekly commission (`gold += commission_weekly`),
  chest gold, villain reward. Gold sinks have been: troop recruitment
  (`recruit_cost * count`), boat rental (per week), spell purchase (`cost`),
  siege weapons (3000, one-time), the alcove (`GameAlcoveCost`, once per zone
  under rites per zone), and in modern Rome the Tribute (REQ-430q).

### 23.2 Week-end ordering

- **REQ-361.** End-of-week processing has run in this order (`engine/game.c`
  day/week rollover): (1) `time_stop = 0`; (2) `leadership_current =
  leadership_base`; (3) `astrology = GamePickAstrologyCreature(week_id)`;
  (4) `gold += commission_weekly`, `last_commission = commission_weekly`;
  (5) `gold -= sum(slot.count * (recruit_cost / 10))`; (6) if `boat.has_boat`,
  `gold -= GameBoatCost`, repossessing the boat on shortfall; (7) `gold =
  max(0, gold)`; (8) astrology effects (§24).

---

## 24. Astrology and weekly events

### 24.1 Pick

- **REQ-370.** `GamePickAstrologyCreature(week_id)` (`engine/game.c`) has
  returned troop 0 (peasants) when `(week_id & 3) == 0` (every 4th week from week 0);
  otherwise `1 + ((seed XOR week_id) * 1664525 + 1013904223) mod (troops_count
  - 1)` (deterministic, so a reload shows the same creature).

### 24.2 Effects

- **REQ-371.** `GameApplyAstrology(troop_idx)`: dwellings whose `troop_id`
  matches have refilled to `max_population` (non-matching have kept their
  count). For each castle the player does not own other than the King's, and
  each live foe, friendly included, every troop matching the astrology
  creature has grown by
  `troop.growth_per_week`. When the week is Peasants, every player-army slot
  with `TROOP_ABIL_ABSORB` (ghosts) has had its id rewritten to `peasants`
  (count preserved). Dwellings have grown at no other time.

### 24.3 Week-end dialog

- **REQ-372.** After processing, a two-phase dialog sequence has been queued
  (`src/shell_weekend.c`): **Phase 1 (Astrology)** has shown the new week's
  creature; **Phase 2 (Budget)** has shown gold on hand, commission paid, boat
  cost (if any), per-troop upkeep, and the final balance. Any key has
  dismissed it.

---

## 25. Combat

The engine half of combat has lived in `engine/combat.c` (state, AI,
headless turn loop, damage formula, combat spells); the rendered loop
(`RunCombat`, modal input, target picker, per-frame present) in
`src/combat_loop.c`; the battlefield renderer in `src/combat_render.c`. Both
halves have shared the `Combat` struct in `engine/include/combat.h`, and
golden-digest regression tests have pinned the formulas.

### 25.1 Arena

- **REQ-380.** The grid has been 6 columns × 5 rows (`COMBAT_W = 6`,
  `COMBAT_H = 5`), each cell one map tile (48×34 design pixels in legacy, the
  pack's tile in modern) holding at most one unit. Each side has had up to 5
  slots (`COMBAT_SLOTS = 5`); player = side 0 (`COMBAT_SIDE_PLAYER`), AI =
  side 1. A wandering foe has fielded only its first three troops, unless it
  is a static guardian in modern mode, which has fielded all five
  (`full_band`). Two modes: **field** (`COMBAT_MODE_FOE`, open field, scattered
  obstacles) and **castle** (`COMBAT_MODE_CASTLE`, siege layout with walls).
  The player has started one troop per row (slot i → column 0, row i). The
  obstacle map (`omap`) has used codes 1..3 for field obstacles and 5..10 for
  castle walls (0 = open); the unit map (`umap`) has held packed unit ids
  (1-based; 0 = empty). Both maps have been sized `[H+1][W+1]` for off-by-one
  guards.

### 25.2 Combat state

- **REQ-381.** The `Combat` struct has held `units[2][5]`, `omap`, `umap`,
  `spoils[2]`, `powers[2]` (per-side artifact bits), `heroes[2]` (`g` for
  player, `NULL` for AI), `turn`, `phase`, `attack_seq`, `spells_this_round`, `side`,
  `unit_id`, `castle` (siege flag), `first_kill_seen`, `stacks_destroyed`,
  `rng_state`, `banner[80]`, `log_lines[8][80]`, `log_count`, `cursor_x/y`,
  `target_filter`, `picker_active`, `cursor_frame`, a pick+cast state machine
  (`cast_phase`, `cast_spell_idx`, `pick_reason`, `pick_filter`, first-target
  and destination scratch), `result` (0 running / 1 player win / 2 AI win),
  `villain_id`, `mode`, `target_name`.
- **REQ-382.** A `CombatUnit` has held `troop_idx` (-1 = empty), `count`,
  `turn_count` (snapshot at turn start, for the damage formula), `max_count`
  (snapshot at combat start, the ABSORB/LEECH cap), `dead`, `frame`, `injury`
  (sub-HP residual), `acted`, `retaliated`, `moves`, `shots`, `flights`,
  `frozen`, `out_of_control`, `(x,y)`, `hit_flash`.

### 25.3 Turn order

- **REQ-383.** The player side has acted first each round; within a side,
  lowest slot index first. `combat_next_unit` has scanned `[unit_id+1..4]`
  for the first unit with `count > 0 && !acted`, then wrapped via `phase++`
  to scan `[0..unit_id]`. `combat_reset_turn` has reset `acted`/`retaliated`,
  refreshed `moves = move_rate` and `flights = 2` for FLY units, and
  (REGEN/Trolls) reset `injury = 0` at the start of the player side. A
  `frozen` unit has skipped its turn (`acted = true`), then had `frozen`
  cleared by the next reset.

### 25.4 Move and fly

- **REQ-384.** `combat_move_unit` has moved one tile: out-of-bounds or
  obstacle → blocked; friendly under-control unit in target → blocked;
  hostile (or own-side OOC) unit in target → melee attack (`acted = true`);
  empty cell → relocate, `moves -= 1`, `acted` when `moves == 0`.
  `combat_fly_unit` has ignored obstacles/units and landed only in empty
  cells; `flights -= 1`, `acted` when `flights == 0`. OOC units have kept
  taking turns but attacked their own side.

### 25.5 Damage formula

- **REQ-385.** `combat_deal_damage` (`engine/combat.c`) has computed final
  damage:
  1. **External path** (spell): `final_damage = external_damage`.
  2. **Internal path**: `dmg = is_ranged ? (MAGIC ? ranged_min :
     rand(ranged_min, ranged_max)) : rand(melee_min, melee_max)`; `total =
     dmg * turn_count`; `skill_diff = attacker.skill + 5 - target.skill`;
     `final_damage = (total * skill_diff) / 10`. **SCYTHE** (Demon): an
     11-in-100 roll (`combat_rand(1,100) > 89`) adds `target.hp *
     ceil(target.count / 2)` after the morale/artifact passes.
  3. **Morale** (attacker has hero and is in control): Low → `/2`; High →
     `×1.5`; Normal → unchanged. The combat rank
     (`troop_morale_for_unit`) has been the lowest chart result over the
     side's live units, the attacker itself included, each looked up as
     `morale_result(other, self)`; the army view's label (REQ-271) has
     looked the pairs up the other way round and left the troop itself out,
     so the two have been able to differ where the chart is asymmetric.
  4. **Artifact attacker** `INCREASED_DAMAGE` → `×1.5`.
  5. **Artifact target** `QUARTER_PROTECTION` → `×0.75`.
  6. Accumulate `+= target.injury`; add the SCYTHE bonus.
  7. `kills = final_damage / target.hp`; `injury = final_damage % target.hp`;
     the outcome updates count or marks the unit dead.
- **REQ-386.** A MAGIC ranged attack against an `IMMUNE` defender has
  returned -1 (fizzle, no state change); non-MAGIC attacks have ignored
  IMMUNE. Ranged attacks have decremented `shots` regardless of outcome. When
  the target side has the hero, `kills` have been added to
  `g->stats.followers_killed`.

### 25.6 Retaliation

- **REQ-387.** Retaliation has fired iff: not external; not already a
  retaliation pass; the defender has not retaliated this round; the defender
  survived; the attack is melee (ranged sets `retaliated` early to skip). It
  has been a recursive `combat_deal_damage` from defender to attacker with
  `retaliation = true`; the flag has been set first to prevent infinite
  recursion, and reset at the unit's next turn.

### 25.7 Special-ability post-effects

- **REQ-388.** **ABSORB** (Ghosts): `count += kills`. **LEECH** (Vampires):
  `count += final_damage / hp`, clamped to `max_count`. **REGEN** (Trolls):
  `injury = 0` at the start of the player side. **MAGIC** (Druids,
  Archmages): ranged uses fixed `ranged_min`, cancelled by IMMUNE. **IMMUNE**
  (Dragons): blocks MAGIC attacks and Fireball/Lightning/Freeze/Turn Undead.
  **SCYTHE** (Demons): see REQ-385. **UNDEAD**: the only targets Turn Undead
  bites; nothing else in combat has read the flag.

### 25.8 Combat spells

- **REQ-389.** One spell per round (`spells_this_round < 1`); casting has
  required the hero's class+rank `knows_magic`. SP has come from
  `g->stats.spell_power` (the Amulet doubles it at pickup).
  - **Clone** (2000): `clones = (sp*10 + injury) / hp`; `count += clones`,
    `max_count += clones`.
  - **Teleport** (500): two-pick workflow (unit, then empty destination).
  - **Fireball** (1500): external `25 * sp`; blocked by IMMUNE.
  - **Lightning** (500): external `10 * sp`; blocked by IMMUNE.
  - **Freeze** (300): sets `frozen`; blocked by IMMUNE.
  - **Resurrect** (5000): `revived = max(1, sp)`, clamped so `count +
    revived <= max_count`.
  - **Turn Undead** (2000): external `50 * sp` vs UNDEAD only; blocked by
    IMMUNE.
  The target picker (`combat_cell_passes_filter`, shell-side
  `combat_pick_target`) has supported filters `ANY`, `EMPTY`, `ANY_UNIT`,
  `FRIENDLY`, `ENEMY`, `UNDEAD`.

### 25.9 Player input

- **REQ-390.** Movement via arrows / numpad 8/4/6/2 (cardinals) and
  Home/PgUp/End/PgDn or numpad 7/9/1/3 (diagonals); numpad 5 = wait. `S` has
  shot (rejected when `shots == 0` or surrounded). `U` has opened the spell
  menu (A..G for spells 0..6). `G` (give up) has asked Yes/No and Yes has set
  `result = 2`; `Esc` with nothing armed has done nothing in legacy, and in
  modern mode has opened the combat menu, whose Game page holds Give up
  (REQ-430s, `docs/DESIGN-SPEC.md`). `C` has shown the controls
  overlay (modal, not consuming the turn).

### 25.10 AI behaviour

- **REQ-391.** `combat_ai_action` (`engine/combat.c`) has selected, in
  priority: frozen → skip; close (1-tile) target → melee; ranged when shots >
  0 and not surrounded → far target; fly when FLY and flights > 0 → land
  adjacent to a far target; walk toward closest/far target; else pass.
  `ai_pick_target` has scored far ranged enemies = 10000, others = `1000 -
  hp` (lower-HP preferred); OOC attackers have treated their own side as
  targetable. Movement tie-breaks have iterated `dy ∈ {+1,0,-1}` outer,
  `dx ∈ {-1,0,+1}` inner.

### 25.11 Log, banner, result, spoils

- **REQ-392.** Up to 8 log lines (`COMBAT_LOG_LINES`) have ring-buffered from
  the pack's `combat_log` strings (`melee_hit`, `retaliate`, `ranged_hit`,
  `frozen`, `immune`, `cloned`, `resurrected`, `teleported`, etc.). The
  banner has shown the actor's name + M/S counters before the first kill,
  then "<actor> vs <target> killing N".
- **REQ-393.** **Win** (`result = 1`): all defenders dead; spoils =
  `sum(troop.spoils * 5 * count)` over killed enemies credited to gold;
  survivors written back to `g->army` with `GameCompactArmy`. **Loss / flee**
  (`result = 2`): all attackers dead, or give up; this has triggered temp
  death (REQ-253: clear army, grant the pack's temp-death troop, drop the
  boat, teleport home, dismount).

### 25.12 Siege weapons

- **REQ-394.** The `siege_weapons` flag has been checked at the enemy castle
  gate (`engine/step.c`): without it a monster or villain castle has bounced
  the hero silently, the original DOS behaviour (§38.2). Combat itself has
  never read it.

### 25.13 Combat RNG

- **REQ-395.** Combat has used an independent LCG (`Combat.rng_state`) seeded
  by `combat_seed_rng` as a pure function of (world seed, the encounter's
  stable identity (a foe's `placement_id` or castle id) and the combat mode),
  so a fight's RNG has not depended on casualty history and the autoplay
  planner's prediction has matched the live outcome. `combat_rand(c, min,
  max)` has advanced `state = state * 25214903917 + 11` and returned `min +
  (state >> 32) % (max - min + 1)`.

### 25.14 Headless loop and test harness

- **REQ-396.** `combat_run_headless(g, mode, target, cap_rounds)` has driven
  both sides with `combat_ai_action` (no raylib, no input, no animation),
  mirroring `RunCombat`'s setup + writeback; it has ended as a LOSS at
  `cap_rounds * 64` actions, or after eight rounds of 64 actions in which no
  count, injury, ammo or charge has changed. `combat_test_digest(seed,
  attacker, count_a, defender, count_b, rounds)` has run a deterministic
  two-troop fight and returned a 64-bit digest; the golden cases in
  `tests/regression/test_combat_digests.c` have run under `make test` to
  catch formula regressions. `combat_run_headless` / `combat_test_digest`
  have also been exercised by `tests/unit/test_combat_ai.c`.

### 25.15 Render

- **REQ-397.** `src/combat_render.c` has drawn the arena at the field origin
  (`CL_COMBAT_X`, `CL_COMBAT_Y`: 16, 22 in legacy, where the field is 288×170
  in cells of 48×34; centred in the pane in modern, cells one tile). Render
  order: the field ground, obstacles, units (with count badge), damage-flash
  overlay, picker cursor (when `picker_active`), then chrome and title bar.
  Only the active unit has animated (frames at ~150 ms); on frame wrap the AI
  has taken its action.

- **REQ-398.** **Beat order of a blow (modern only).** `combat_hit_unit`
  (`engine/combat.c`) has dealt the damage, set the target's `hit_flash = 3`
  and bumped `attack_seq` in one call, so the engine's numbers change before
  the swing is drawn. The shell has staged what the player SEES
  (`src/combat_loop.c`, `src/combat_render.c`); every step has been a length
  of time, not a count of ticks, so every troop swings in the same time:

  1. **0 ms** -- the attacker's strip starts. The target still shows its
     pre-blow count (`turn_count`) and no splat.
  2. **The strip's last frame** -- the blow lands: the count drops and the
     splat appears. The whole swing is 360 ms whatever its frame count
     (`ATTACK_STRIP_S`: 90 ms a frame at four frames, 60 ms at six).
  3. **+300 ms** -- the splat is done (`SPLAT_TICK_S` x 3, its own clock, so
     idle troops keep their 150 ms cycle). A troop the blow killed stays on
     the field under its splat, without a badge, until now; then it leaves
     and the fight moves on.
  4. **End of a fight** -- after the killing blow's splat, the field is held
     0.5 s (`FIGHT_END_HOLD`) before victory or defeat, so the last blow and
     the emptied field are seen.

  That has come to about 0.6 s a blow and 1.1 s from the last swing to the
  ending.

  Legacy has kept King's Bounty's timing: `attack_anim_start` returns early
  when not modern, so no strip plays, and the burst is drawn on the frame of
  the hit and decays from there.

---

## 26. Scoring, victory, and defeat

### 26.1 Score

- **REQ-400.** `GameComputeScore` (`engine/game.c`): `score = 500 *
  villains_caught + 250 * artifacts_found + 100 * castles_owned -
  followers_killed` (the weights from `economy.scoring`). When difficulty is
  Easy and `easy_halves` is true, the score has been halved; otherwise it has
  been multiplied by `difficulty_multiplier[difficulty]`, 1 / 2 / 4 for
  Normal / Hard / Impossible (the 5-slot table's fifth entry, 8, has been
  unused). The result has been clamped at ≥ 0, recomputed at every state
  mutation and stored in `Game.stats.score`.

### 26.2 Win and lose

- **REQ-401.** The **win** has fired when the player searches (key `S`) on
  the buried scepter tile (matching zone, x, y), running `show_win_game`
  (`engine/flows.c`). The **lose** has fired when `days_left` reaches 0 (at
  the next day rollover), running `show_lose_game`.
- **REQ-402.** Both end-game screens (`src/screens/end_game.c`,
  `engine/include/end_screen.h`) have shown the pack's `win` or `lose` body
  beside the ending picture: in legacy a DBLUE panel with the text at the
  left and the still `ending_win` / `ending_lose` image at the right, in
  modern a page (`DESIGN-SPEC.md` DSGN-0143). Substitution tokens: `%NAME%`,
  `%RANK%`, `%SCORE%`. The victory cartoon (`src/end_cartoon.c`) has run as
  its own screen, invoked by the host before `show_win_game`; the flow itself
  has been render-free.

---

# Part III, Engine, save, UI, platform

## 27. Save format and slots

### 27.1 Format

- **REQ-410.** Saves have been **JSON, version 11** (`SAVE_VERSION` in
  `engine/include/savegame.h`). Catalog references have used string ids
  (`troop_id: "knights"`), so saves have been human-readable and
  pack-portable. Read/write has been `engine/savegame.c`; the full-state
  snapshot builder `engine/state_serialize.c`.
- **REQ-411.** Save slots: 10 (`SAVE_SLOT_COUNT` in
  `engine/include/savepath.h`). Filenames: `save_0.dat` … `save_9.dat`.
- **REQ-412.** Save directory (`engine/savepath.c`):
  `<user-data>/saves/<pack_id>/`, one directory per pack, where
  `<user-data>` has been:
  - **Linux**: `$XDG_DATA_HOME/openbounty/` if set, else
    `~/.local/share/openbounty/`. Created on first save.
  - **macOS**: `~/Library/Application Support/OpenBounty/`.
  - **Windows**: `%APPDATA%\OpenBounty\`.
  - **Web**: `/saves`, an IndexedDB-backed mount the page syncs.
  `--save-dir <dir>`, iOS (the app's `Documents/saves`) and Android (the
  app's private `saves` directory) have used a flat directory with no
  per-pack level.
- **REQ-413.** Fog of war has been encoded compactly in the save (per-tile
  bits), from each zone's own width and height. The scepter location has been
  stored in the save in plain form.

### 27.2 Schema and load

- **REQ-414.** The save schema has mirrored the `Game` struct field-for-field
  (§4), so serialization has round-tripped without loss. The one deliberate
  exception has been the seed: a catalog game writes `seed_from_catalog:
  true` and `seed_index`, and re-derives `Game.seed` on load (REQ-166,
  REQ-186); a raw-seed game writes `seed_from_catalog: false` and the `seed`
  itself. Writing the index rather than the expanded seed has made the
  round-trip exact: cJSON numbers are doubles, so a full-width seed would not
  survive above 2^53. A save-format change has bumped `SAVE_VERSION` and
  updated or replaced the golden fixture (`tests/fixtures/save_v1.dat`) and
  the round-trip regression test. A save written by another pack has been
  refused (`SAVE_ERR_PACK`).
- **REQ-415.** On load, the caller has re-applied the `consumed` tile
  mutations to the loaded map (`GameApplyTileMutations`) and restored
  per-continent fog, so consumed tiles render and behave as plain terrain.

---

## 28. Resource system

### 28.1 `Resources`

- **REQ-420.** `Resources` (`engine/include/resources.h`) has been loaded
  once at startup (`engine/resources.c`) from the pack's `game.json` and its
  `strings/<lang>.json` (the base `world.language`, or `--lang`), and treated
  as read-only thereafter. It has held: world/time/economy/tuning/contract/
  combat blocks; the troop/spell/artifact/villain/class catalogs;
  castle/town/zone tables; the tile-code table; sprite paths; colors; audio
  metadata; controls rows; and every string (banners, UI labels, villain
  descriptions, win/lose text, count buckets). Every table has been heap,
  sized by the pack (§3.1).
- **REQ-421.** Where an optional numeric value is absent from `game.json`, the
  engine has substituted a documented default. The engine has carried no text
  of its own: a pack missing any required string key has been refused at load,
  with every missing key printed.

### 28.2 Asset loading

- **REQ-422.** Every build has read its game data from a pack: a
  `.openbounty` zip, or a loose directory holding `game.json`. Engine-side
  byte reads have gone through `LoadAssetBytes` (`engine/assets_bytes.c`);
  shell-side texture loads through `LoadAssetTexture` (`src/assets.c`). The
  engine has never touched GPU textures.

---

## 29. UI: views, HUD, dialogs, prompts

`docs/DESIGN-SPEC.md` has recorded every modern panel's size and position and
every menu; this section has held the rules.

### 29.1 Adventure HUD and chrome

- **REQ-430.** The legacy adventure layout (`src/chrome.c`, `src/hud.c`,
  `src/map_render.c`, constants in `src/layout.h`): a **top status bar**
  (yellow border) showing Options / Controls / Days Left; a **map viewport**
  (5×5 tiles, 240×170, hero centred, camera clamped at zone edges); a **right
  sidebar** (48px: portrait, contract/siege/magic/puzzle icons, gold); and a
  **bottom** region that drops out for dialogs and prompts. The modern layout
  has had no status bar: the frame, a one-tile column either side of the map,
  and the map between them (REQ-430a, `DESIGN-SPEC.md` DSGN-0003).
- **REQ-430a.** **A declared smallest screen.** A modern pack has been able to
  declare the smallest screen it is drawn on with `render.native_w` /
  `native_h` (`CL_IS_NATIVE`, `src/layout.c`). Across it `layout_init` has
  laid out the frame (`RES_MODERN_FRAME`), the left column (one tile), a band
  (`RES_MODERN_GAP`), the map, a band, the right column and the frame; down
  it the frame, the map and the frame. `resources_load` has rejected a
  declared screen too small for the frame, both columns and their bands, and
  the larger of the viewport and the battlefield. The size has been a floor,
  not a fixed size: the scale and the map's growth have followed REQ-528. The
  hero's cell has been centred across the map on the row that holds the
  map's middle, the rows standing flush with the columns' tiles; every cell
  the map shows has been drawn, part cells at its edges and foot included,
  and the camera has never clamped (`map_view`, `src/map_render.c`).
- **REQ-430b.** **Code-drawn chrome.** A modern pack without
  `sprites.ui.chrome_overworld` has had the gold lattice (`src/lattice.c`): a
  cross-hatch pattern built once as a texture at `ui_scale` and tiled from the
  screen origin. `chrome_draw` has filled the frame ring and the band beside
  each column with it. Each column has been one tile to a row, with no frame
  per tile: a 2 px lattice band across every edge of every tile and the
  column's dark ground (`lattice_ground`) below its tiles
  (`hud_column_finish`, `src/hud.c`). Every page's ring has been drawn by the
  page engine, as thick as the frame (REQ-430j). Legacy has kept its bitmap
  chrome and its one-pixel window lines (`legacy_window_frame`,
  `legacy_panel_frame`, `src/ui.c`), which have drawn nothing in modern.
  Splash, title and class-picker art has been drawn at the largest whole
  scale that fits the screen, on black (`page_art`; legacy at 1x); the
  victory cartoon has had the frame's lattice round it. Rome has shipped no chrome bitmap or bar strip.
- **REQ-430c.** **Pack-declared TrueType font.** A modern pack has been able
  to declare a `font` block (`file`, `size`, `caps`, `license`; `ResFont`,
  `engine/resources.c`, both paths in the manifest). The shell has had two
  text backends behind the `bfont_*` names: the bitmap strip in its
  `8 * ui_scale` cell (legacy, and any pack without the block), and
  `src/text.c`, which has rasterised the face at `size` through raylib's
  `LoadFontData` (stb_truetype on iOS), drawn every glyph centred in one
  fixed cell (the face's widest advance) on its baseline, and uppercased
  when `caps` has been set. `bfont_preload_metrics` has run before
  `layout_init` (CPU only) so `BFONT_GLYPH_H` has been the face's line
  height and `BFONT_GLYPH_W` the widest advance over printable ASCII and the
  four arrow glyphs; `CL_STATUS_H` and
  `CL_PANEL_H` (`src/layout.h`) have been expressed in those and evaluated to
  9 and 68 in legacy. `bfont_take_line` has wrapped to a pixel width: legacy
  by `max_w / 8` characters keeping every newline; modern by the face's cell,
  every newline kept as authored, breaking after a word's own hyphen where
  that has come later than a space, and hyphenating a word longer than the
  line. Rome has shipped Press Start 2P (SIL OFL) at 16, a 16 px cell.
  Modern pages have wrapped to their own inner width; legacy views have
  taken the map pane's full height (`VIEW_H`), the content rect.
- **REQ-430d.** **Rendered at zoom.** For a declared smallest screen
  (`CL_IS_NATIVE`) the render target has been the screen times the scale
  (`present_refit`), and every frame site has drawn through
  `present_begin`/`present_end`, a camera at that zoom, so every draw call
  has kept design coordinates and art has been pixel-identical to an integer
  blit; `present_scaled` has blitted the target 1:1, centred in the safe
  area, or fitted it down into a smaller surface (`present_fit_down`, with a
  smooth filter while fitted down), and stored the blit rect so
  `present_window_to_screen` has yielded design pixels. The map scissor has
  multiplied by `present_get_zoom`. `bfont_set_zoom` has rebuilt the
  TrueType atlas at size times zoom; design metrics have never changed, only
  sharpness. Legacy: plain `BeginTextureMode`, a 320x200 target.
- **REQ-430e.** **One list reader (modern).** `ml_list_input`
  (`src/modern/mlist.c`) has read every modern list: Up/Down and KP8/KP2 have
  moved the cursor, wrapping; Enter, KP Enter and Space have acted on the
  cursor's row; a tapped row (`touch_tapped_row`) has been select-and-act; a
  row's own key -- its shortcut letter or its digit -- has acted on that row;
  Escape has been Back. A row that cannot be chosen has taken the cursor and
  not acted. `sel_input` (`src/select.c`) has wrapped it for the lists that
  have used it, and returned nothing in legacy; `sel_row` has drawn legacy's
  plain text and its tap region, so every legacy screen has kept its own
  handling and pixels.
- **REQ-430f.** **Keyboard detection and the letter selector (modern).**
  `input_host` has latched which physical devices have been used: a real key
  event (not an injected one), a touch contact, a gamepad button or stick
  (`input_host_note_gamepad`, called from `src/input.c`), and has kept the
  one used last (`input_last_device`). `input_has_keyboard` has been true
  once a key has been seen, and before that true unless touch or a gamepad
  has been seen first; `input_text_mode` has mapped it to typed entry or the
  selector. `src/textsel.c` has been the selector: an in-game grid drawn in
  the buffer, A..Z SPC DEL OK or 7 8 9 DEL / 4 5 6 OK / 1 2 3 0 for numbers,
  moved by arrows, keypad or the gamepad d-pad and stick
  (`input_gamepad_dir`), picked by Enter or the A button
  (`input_gamepad_confirm`), deleted by Backspace or B, or tapped
  (`TOUCH_LIST_TEXTSEL`); the cursor cell has been filled `YELLOW`. It has written
  the field's buffer directly, through the same bounds the typed path
  applies. In modern it has served the hero name whenever the device last
  used has not been the keyboard (REQ-531), with typing still accepted
  alongside. Legacy has kept typed entry and its window-chrome keyboard and
  digit pad.
- **REQ-430g.** **Dim once (modern).** A floating page (REQ-430j) has first
  darkened what is behind it -- the whole base screen, or the page it opens
  over -- with black at the pack's `render.dim` percent, then drawn itself,
  so the page has been what the eye lands on and the world has stayed
  readable behind it. Nothing has been darkened twice. A page that fills has
  dimmed nothing, since it covers what it would dim; toasts and the class
  caption have not dimmed. `overlay_dim_alpha` (`src/overlay.c`) and the page
  engine's `dim` (`src/modern/page.c`); legacy has never dimmed.
- **REQ-430h.** **Every modern panel a page.** Every modern panel -- the
  message, the questions, the town, castle, dwelling, temple and recruit
  screens, the views and the menus -- has been a page of REQ-430j, never a
  legacy rect. Dialog headers have wrapped like body text (the audience
  passes the Emperor's words as the header); the four legacy arrow control
  codes have rendered as the font's arrow glyphs; recruit rows have padded the
  name to the longest in the pool; empty army slots have stayed
  panel-coloured; the character card has printed zeros; Escape on the map has
  opened the game menu (`INPUT_ACTION_GAME_MENU`). Legacy has kept its
  one-sided 5 px margin and its own drawing for all of these.
- **REQ-430i.** **The draw layer has been forked; legacy frozen.** The
  overlay, the detail views and the prompt panel have each existed twice:
  `src/legacy/` has held the DOS original's drawing and `src/modern/` the
  modern UI's, with `src/overlay.c`, `src/views_render.c` and `src/prompt.c`
  as dispatchers that have kept the public entry points, the layer order and
  any state (the dialog's text and page, the world map's reveal flag, the
  prompt's state machine) and sent only the drawing to one side or the
  other, through `*_impl.h`. Legacy's copies have been frozen: their
  behaviour has been the specification, so they have not been edited to
  serve anything modern needs, and new UI work has landed in `src/modern/`
  alone. `tests/unit/test_legacy_freeze.c` has held legacy's geometry and
  pure logic to fixed values -- chrome bands, map, sidebar, content and panel
  rects, window scale, the 30-column wrap, dialog paging, prompt state, and
  the two modern-only selectors staying inert -- so a modern change that
  would move a legacy pixel has failed the build. `src/layout.h` has
  deliberately NOT been forked: both paths have drawn into one coordinate
  system.
- **REQ-430j.** **Pages (modern).** Everything drawn over the base screen
  has been a page, placed by one engine (`src/modern/page.c`;
  `DESIGN-SPEC.md` DSGN-0030 to DSGN-0040). Every page has been one of three
  kinds, each one size on a screen, taken from the pack's declared screen: a
  **full page** (the space inside the smallest screen's frame: a place, a
  sheet, the world map, the spells), a **menu page** (a full page less a
  ring and a gap on every side: a menu, a list, a count, a question with more
  than two answers) and a **message** (the smallest map's width less a ring
  and a gap each side, as tall as it holds, on the foot of the map or of the
  battlefield). A full page has **floated** where the space inside the frame
  holds it with its ring, as thick as the frame, and a gap as wide on every
  side, and **filled** that space otherwise, with no ring of its own; every
  full page has done the same on the same screen. Menu pages and messages
  have always floated. A floating ring has never stood closer than a frame's
  thickness to the frame, and no page has drawn a ring over chrome. One page
  has been open at a time: a step has replaced a page's words and rows in
  place. Taps: a page has been modal -- nothing registered under it has
  taken a tap; a page with a single action has done it on a tap anywhere; on
  a page with choices a tap on nothing inside has done nothing and a tap
  outside it -- on the dimmed screen, or on the frame round a page that fills
  -- has been its exit; every page with choices has ended in its exit row,
  Escape has been the exit and Enter the highlighted row. A build guard
  (`PAGE_STAMP`, the Makefile) has failed `make all` when a shell file other
  than the page engine, the chrome, the lattice and `src/ui.c` draws a ring
  of its own. The legacy location screens have reached their text rect
  through `screens_text_rect`, which the freeze tests pin to the legacy
  panel.
- **REQ-430k.** **Menu driven (modern) and `--debug`.** In modern every
  action has been a row reached by the arrows and Enter or a tap; keys have
  remained as shortcuts, each shown at its row while the keyboard is in use
  and acting on its row inside a menu, and nothing has been reachable only by
  a key (`docs/DESIGN-SPEC.md`). The home castle has had Recruit and Audience
  rows; the own castle Garrison and Withdraw rows; the world map, with the
  orb, has had a row that swaps your map and the whole map; numeric and A/B
  prompts have answered by rows the opener names (`prompt_set_choices`: the
  provinces, the troops to dismiss, the treasure's two uses from the pack's
  strings) or, when it names none, by the answers themselves, 1 to N or A and
  B -- nothing has been read out of the words; Ctrl+Q has asked with the
  yes/no prompt; a tap has skipped the end cartoon. The debug cheats have
  been reachable only when the game is started with `--debug`, as a Debug
  page in the game menu; without the flag no key or row has reached them in
  either mode.
- **REQ-430l.** **Standard select rows (modern).** A list of choices has been
  drawn as rows two to a tile -- a row and its 2 px rule half a tile, never
  shorter than a text line plus padding -- and, on a touch device, two thirds
  of a tile, one height for the whole session; stacked from the top of their
  column with the rule under each, and never stretched to fill the column;
  the height below them has stayed empty, and a list longer than its column
  has scrolled to keep the cursor in view. Defined once as `ml_row_h` /
  `ML_ROW_RULE` in `src/modern/mlayout.h`.
- **REQ-430m.** **Modern castles and the count.** The home castle and owned
  castles have been rooms and persons (`DESIGN-SPEC.md` DSGN-0141): pages of
  rows ending in Back or Leave, Esc back a level. Recruit has listed the
  castle troops with their statistics; Audience has always been available and
  promoted when a promotion is due, showing the castle's
  `special.promotion[rank]` image and the rank's gains; the ruler's portrait
  and standing figure have come from `special.portrait` / `special.figure`.
  Garrison and Withdraw have moved any part of a troop
  (`GameGarrisonTroopCount`, `GameUngarrisonTroopCount`; a whole troop is
  exactly the original move, and only a whole last troop is refused). Counts
  have been chosen with the How-many block of `DESIGN-SPEC.md` DSGN-0055.
- **REQ-430n.** **Question dialogs and lists on standard rows (modern).**
  A question with two answers (Yes/No from `strings.prompts.yes` / `no`) has
  been the message box on the foot of the map -- of the battlefield in a
  fight -- whatever has been open: its words, a lattice band, and the two
  rows. A numbered or lettered question has been a menu page, its words as
  the description and one row per choice (each up to two lines of its row),
  a numbered one with Cancel on the foot; a count has been a menu page with
  the How-many block and Continue and Cancel. The game menu, Controls, title
  menu, load picker, difficulty rows, world map, spells, gate picker and
  combat menu have used the same rows (`src/modern/mlist.c`).
- **REQ-430o.** **Foe view and the evade rule.** A hostile foe on the map has
  opened the modern foe view (`DESIGN-SPEC.md` DSGN-0132) with Fight and
  Evade. With `game.json` `foes.evade_needs_free_square` set,
  `GameFoeCanEvade` has allowed Evade only while one of the 8 squares around
  the hero is walkable for how they travel and has no object or foe on it;
  the engine has recorded the result for the pending decision
  (`pending_foe_evade_blocked`, judged after any bounce back), and autoplay
  and the demo have had to fight when it has been set. Packs without the
  setting have kept the free decline.
- **REQ-430p.** **Title sequence (modern).** With `sprites.ui.title_battle`,
  `title_eagle` and `title_words` all declared, the title menu has opened on
  the words and eagle standard over purple; the battle has faded in from
  1.0 s to 2.5 s, the eagle has slid left from 2.5 s to 3.5 s, and the menu
  has faded in from 3.0 s, drawn at the screen's zoom. Any key or tap has
  skipped to the end; the sequence has played once per run, and the credits,
  the load picker and a return to the title have shown the finished screen.
  Without all three the title has been `splash_title`, still.
- **REQ-430q.** **Blessing and Tribute (modern home castle).** With
  `game.json` `audiences`, `GameSeekBlessing` has granted once, when every
  artifact is found (enemies left or not), leadership +
  `blessing_leadership_pct` (50) of the base; `GamePayTribute` has taken
  `tribute_cost` (50000) gold, any number of times, for leadership +
  `tribute_leadership_pct` (25) and spell power and spell capacity each +
  `tribute_magic_pct` (25) of what the hero has then (at least 1 of a stat
  above 0). A short purse has paid nothing. `stats.blessed` and
  `stats.tributes` have been saved only for such a pack; autoplay has used
  neither.
- **REQ-430r.** **Temple and dwelling screens (modern).** VIEW_ALCOVE and
  VIEW_DWELLING have drawn their own full screens over the FLOW_ALCOVE yes/no
  and FLOW_RECRUIT count prompts (`DESIGN-SPEC.md` DSGN-0142). The shell has
  kept the view open through the answer's message and closed it when no
  prompt, dialog or queued request has remained. With
  `magic.rites_per_zone` a known zone's temple has raised the alcove view
  behind its message.
- **REQ-430s.** **Game and combat menus (modern).** Drill-down menus
  (`src/modern/gamemenu.c`; the combat pages in `src/combat_loop.c`
  `combat_menu_page`): one column of rows per page ending in Back, the path
  in the title strip, the row under the cursor described under it, each page
  opening on its first row that can be chosen. The game menu has opened with
  Escape or the left column's Menu tile; its pages have been Menu (Hero,
  World, Game, then Close and Exit on the foot of the page, REQ-529), Hero,
  World and Game (Debug first with `--debug`, Save, Load, Controls, New
  Game). The combat menu has opened with Enter, Escape, a tap on the active
  unit or the command column's Menu tile, on its Unit page (REQ-536); its
  top page has held Unit, Hero, Game and Close, and Game has held Controls,
  Back and Give up last. Rows that do not apply have been greyed with the
  reason rather than removed. In-game Save and Load have picked one of five
  slots; overwriting, loading, a new game and Exit have asked Yes/No in the
  page's place.
- **REQ-430t.** **A message with a face (modern).** A queued message or
  question has been able to carry a picture hint (`PlayerRequest.face` /
  `face_index`: enemy, troop, artifact, portrait); the modern shell has shown
  it in the message box, the picture at 1x at its left with the words beside
  it and the answers along the foot (`DESIGN-SPEC.md` DSGN-0062). Legacy has
  ignored the hint. The capture message has been composed from
  `banners.capture_*` (King's Bounty has kept its original wording) and carried
  the captured enemy's face.

### 29.2 Views

- **REQ-431.** Full-screen overlays (`ViewKind`, §3.4) have been managed by
  `src/views.c` and drawn by `src/views_render.c`, with per-location screens
  in `src/screens/`. Toggle views: Army (`A`), Character (`V`), Contract
  (`I`), Puzzle (`P`, 5×5 grid derived from villains_caught +
  artifacts_found), Worldmap (`M`), Controls (`C`), Options (`O`, legacy).
  Location views: Town, Home Castle, Own Castle, Dwelling, Alcove, Recruit
  Soldiers. Spell-driven view: Gate, the Town/Castle Gate destination picker
  opened by a cast rather than by a key (REQ-322). End views: Win, Lose.

### 29.3 Dialogs and prompts

- **REQ-432.** Two dialog flavors: `open_dialog(header, body)` (`src/ui.c`), a
  bottom-frame box dismissed by any key, paged by the renderer's own wrap
  (`overlay_dialog_page_count`); and the blocking prompts (`src/prompt.c`:
  `prompt_yes_no_open`, `prompt_numeric_open`, `prompt_text_input_open`)
  whose results have been dispatched through a pending-flow state machine
  (`src/shell_promptdispatch.c`, `engine/include/pending.h`). The engine has
  never rendered; it has requested prompts and dialogs through the player-IO
  queue and the host callbacks in `engine/include/ui_host.h`, which the shell
  implements.

---

## 30. Input and controls

- **REQ-440.** Keyboard bindings (adventure §12; combat §25.9) have been owned
  by `src/input.c`, which has also mapped the gamepad. The raylib exit key has
  been disabled so `Escape` dismisses overlays rather than closing the window.
- **REQ-441.** The Controls menu (`VIEW_CONTROLS`) has exposed per-game
  settings persisted in `Game.stats.options[7]` (parallel to
  `res->controls.items[]`): animation delay, sounds, walk-beep, animation
  toggle, CGA, music, volume. Meta keys: Alt+Enter fullscreen, backtick
  screenshot (`screenshots/shot_NNNN.png`, `src/screenshot.c`, the folder
  created on first use), `Q` save-and-quit, `Ctrl+Q` fast quit
  (`src/shell_fastquit.c`).
- **REQ-442.** **Touch input** (`src/touch.c`) has translated taps into
  synthetic key events injected at the `input_host` shim
  (`input_host_inject_key/_char`), so every screen has kept its keyboard
  handling and the recorder/replay see a keyboard-shaped input stream. The
  pointer has been read only while a touch contact exists: a desktop mouse
  drives nothing. Screens have registered per-frame tap regions while they
  run: plain rects mapped to a key, cursor-list rows
  (`touch_region_row`/`touch_tapped_row`), scrolling lists
  (`touch_region_scroll`), the adventure/combat tile viewport (tap →
  direction key relative to the centre tile / active unit; hold repeats one
  discrete keypress per beat), and the combat picker grid (tap → cursor jump
  + confirm). Injected keys have been one-frame edges cleared by
  `touch_frame()`, which runs from `frame_host_end_frame()` after the
  poll/yield.
- **REQ-443.** **Touch chrome (legacy)**: on-screen buttons (adventure/combat action
  bars, ESC, Yes/No / 1-N / A-B prompt bars, digit pad, A-Z keyboard for name
  entry) have been drawn by `touch_draw_chrome()` from `present_scaled`, in
  window pixels over the letterbox margins, outside the design-space render
  target, sized by REQ-530. Chrome has rendered only after a real touch
  contact has been seen (`input_touch_active`), so a keyboard session draws
  none of it. Tap positions have mapped back to design space via
  `present_window_to_screen` (the inverse of the whole-number blit). Modern
  has requested and drawn none of it: `touch_request` has dropped a request
  made in modern (REQ-530).

---

## 31. Cheats and debug

- **REQ-450.** Debug cheats (`src/shell_cheats.c`: granting gold, spells,
  leadership, revealing the map, discovering the next zone, and so on) have
  been reachable only
  when the game is started with `--debug`, as a Debug page in the game menu
  (REQ-430k); without the flag no key or row reaches them, in either mode.

---

## 32. Audio

- **REQ-460.** Audio (`src/audio.c`) has played OGG music tracks and WAV
  sound effects, driven by abstract engine events (`audio_play_tune` host
  callback), through the `src/audio_backend.h` seam: raylib's audio device
  (`src/audio_raylib.c`) everywhere but iOS, `AVAudioEngine`
  (`ios/audio_ios.mm`) there. Track and SFX paths have come from
  `game.json:audio`. Volume, ducking, and the sound on/off option have been
  handled shell-side; the engine has only emitted tune/sfx events.

---

## 33. Rendering

- **REQ-470.** **Legacy.** The internal render target has been **320×200**
  (`CL_SCREEN_W/H`, the original VGA mode), integer-scaled to fit the window
  preserving aspect ratio (minimum 2×). The base window has been 640×400
  (§5.3). A 256-color VGA palette has been loaded from the pack's
  `palettes/palette.bin` (768 bytes; `src/palette.c`). The font has been an
  8×8 bitmap (`kb-font.png`, `src/bfont.c`). Map tiles have been 48×34;
  sprites 48×34, hero/troop frame cycles `<name>_00..03.png`. **Modern**: the
  pack has declared its tile size, viewport, buffer and font (REQ-430a,
  REQ-430c, REQ-528). In both, tiles have been cached as textures by
  `src/tile_cache.c`, sprite sheets loaded via `src/sprites.c`, and the end
  cartoon drawn by `src/end_cartoon.c`. Every draw has gone through the
  `src/gfx.h` seam (`src/gfx_raylib.c`; `ios/gfx_metal.mm` on iOS).

- **REQ-528.** **The scale has been the surface's, and the map has spent what
  it leaves.** Modern mode has had no zoom setting: `present_scale` has
  returned the largest whole number, 3 at most, at which the pack's declared
  smallest screen (`render.native_w/native_h`) fits the surface -- the
  window, the web canvas or a phone's safe area. The screen has been the
  surface divided by that scale (`layout_grow_native`, `src/layout.c`), so
  the surface has been used whole. A desktop window has held its scale while
  an edge is dragged, raising it only at the size the game gave the window
  or when the window is maximised, restored or made full screen
  (`held_zoom`, `src/present.c`).

  Only the map has grown. The frame, both columns and their bands have kept
  their size; the map has taken everything between the columns, the hero's
  cell centred across it, with the part cells at its edges drawn too. Both
  columns have run the map's full height.

  Every other screen has been a page (REQ-430j), each kind one size on a
  screen: a full page has floated over the dimmed base screen where the
  screen has room for it and its ring, and filled the space inside the frame
  otherwise; menu pages and messages have always floated. A battle has taken
  the base screen's places: its commands in the left column's, its turn in
  the right column's and its field in the map's, flush with the map's top.
  The title, the class art and the end cartoon have been drawn full-bleed at
  the largest whole multiple that fits the screen.

  The desktop window has opened at the smallest screen and, once it exists,
  been resized to the largest whole multiple of it that the monitor's work
  area holds (`frame_host_window_room` and `frame_host_window_place`,
  `src/frame_host.c`, called from `main`, `src/main.c`); its minimum size
  has been the smallest screen. The web canvas has followed the browser
  window, fitted down smoothly below the smallest screen, and mobile has
  taken its safe area. Legacy mode has kept its fixed 320x200, auto-fit with
  the 2x floor.

- **REQ-533.** **The two columns have been one tile wide and always there.**
  The left column has held Menu, Map, Army, Search and Puzzle
  (`src/modern/rail.c`), the right column Contract, Siege, Magic, Gold and
  Days (`src/hud.c`); each tile has fired the action its key fires
  (`shell_dispatch_action`). Both have shown at every size and on every
  device, both have run the full height of the map, and neither has shown a
  key or registered a tap while a page is open. The puzzle tile has covered
  each piece still to be won; the Days tile has shown Time Stop's steps
  left, in another colour, while it runs; the gold and the days have been
  shortened alike when too wide for their tile; with the keyboard in use
  each tile has shown its key in a corner.

- **REQ-534.** **One widget layer has owned every touch target.**
  `src/uitouch.h` has been the only caller of the raw region API: a screen has
  said what a thing IS -- an isolated button, a tiled row, a bar, the map --
  and the widget has decided how a finger finds it. The page engine
  (`src/modern/page.c`) has been the only caller of `touch_page`, a page's
  tap rule. A build guard (`TOUCH_STAMP`, the Makefile) has failed
  `make all` when any other shell file has registered a region or a page's
  taps, the way the library-boundary check has fenced the engine.

  Three modes, every widget: **legacy** has registered exactly the rect it
  draws, because the DOS pitch has been the spec; **modern with touch** has
  grown an ISOLATED target to a touch unit, and never a tiled one, because
  growing one tile makes it swallow its neighbours -- a row, a cell or an
  icon has been sized where it is DRAWN instead (`ml_row_h`,
  `textsel_cell_w/h`); **modern with a keyboard** has been unchanged, since
  every widget has injected the key the screen already reads.

  Chrome registered with `ui_bar` (legacy's pre-game band) has carried a
  priority flag: inside its own rect it has taken the tap from a region
  registered before it. No extra reach beyond its own rect.

- **REQ-535.** **One dismissal rule.** In modern the page has decided it
  (REQ-430j): a page with a single action -- a message, a sheet, an outcome
  -- has done it on a tap anywhere, and a page the player chooses on has
  waited for a row, a tap outside it -- on the dimmed screen, or on the frame
  round a page that fills -- being its exit. Legacy has kept
  `views_closes_on_tap` (`src/views.h`) for the same split: a view with rows
  has waited for a row or the band, and one with nothing to choose has closed
  on a tap anywhere.

- **REQ-530.** **Touch controls have been sized in physical units.** Legacy's
  on-screen controls have sized themselves from `touch_unit()`
  (`src/touch.c`): 11% of the window's short side, floored at 44px, which is
  Apple's 44pt and Android's 48dp on the phones this ships to. The action
  bars, the keyboard, the digit pad and the corner buttons have all derived
  from it. In modern the short side has been capped at 1284 device pixels
  first, a large phone's, and a finger has been answered in the buffer: a
  touch device's rows have been two thirds of a tile (REQ-430l), and the
  design-space touch unit has been that row height, fixed for the session
  (`touch_unit_design`).

  The window-pixel chrome -- the action bars, the corner buttons, the
  keyboard and the digit pad -- has been **legacy's alone**. Every one of
  them has drawn its label through `gfx_label`, which the iOS backend has not
  implemented; modern has answered a finger with the screens themselves,
  drawn in the buffer with the pack's own font.

  Small **design-space** regions have been answered by a forgiving second
  pass in `resolve_tap`: a tap that hits nothing exactly has taken the
  nearest region within the slack -- half a touch unit in legacy, half a row
  in modern -- nearest first, so an exact hit has never been stolen from a
  neighbour. While a page is open only the page's own regions have been in
  reach, and only for a tap inside it. That has made a small target
  answerable on a phone without changing what is drawn. A tap on a frame
  whose screen has just changed (a resized window) has been dropped.

- **REQ-531.** **Naming the hero without a keyboard has used the letter
  grid.** While the device last used has been a finger or a gamepad
  (`input_last_device`), the name page has held the in-buffer letter grid
  (`src/textsel.c`), laid out in the page with cells at least
  `textsel_min_cell_w()` wide -- four glyphs, so `DEL` and `SPC` clear their
  neighbours -- and typing has still been accepted alongside it. With the
  keyboard last used, the page has shown how to type instead
  (`src/startup.c`). Legacy has kept its window-chrome keyboard.

- **REQ-532.** **Choosing a class has been two steps, the same two for every
  input.** Picking a figure -- an arrow key, a pad or a tap -- has given it
  the gold outline and brought up its description in a caption on the
  screen's foot; the caption's one row, **Continue**, has finished the
  choice, and Back in its strip, or Escape, has returned to the title
  (`src/startup.c`). Difficulty and then the name have followed, each a
  page ending in Back, which has stepped back one screen: the name to the
  difficulty, the difficulty to the class.

- **REQ-536.** **The combat commands have kept one order.** The Unit page of
  the combat menu and the command column beside the field have listed Shoot,
  Wait, Fly and Cast in that order for every troop, greying what the unit
  cannot do this turn, so a command has been in the same place every turn
  (`src/combat_loop.c`). The menu has opened with its cursor on the first
  command the unit can use; on the foe's turn every command tile has been
  greyed.

- **REQ-529.** **No Exit on a phone.** The title menu's Exit row and the game
  menu's Exit footer have been compiled out under `PLATFORM_IOS` and
  `PLATFORM_ANDROID` (`src/startup.c`, `src/modern/gamemenu.c`). iOS has had
  no notion of an app quitting itself and Apple has refused a control that
  says otherwise; Android's system has handled it. Desktop and web have kept both
  rows.

---

## 34. CLI, packs, and platform

### 34.1 CLI flags (`build/debug/openbounty`)

- **REQ-480.** Parsed in `src/main.c` (early-exit modes in
  `src/shell_earlyexit.c`): `--version`/`-v`, `--help`/`-h`, `--fullscreen`,
  `--pack <name|path>`, `--lang <code>`, `--save-dir <dir>`, `--seed N`
  (catalog world `0`–`255`, REQ-166), `--movie [<path>]`, `--debug` (the
  Debug page, §31), `--gallery <dir>` (every modern screen to PNG, with the
  tap check), `--window WxH` (the window at a device's size) and `--touch`
  (a touch device), `--demo` (the human-like agent, `DEMO-SPEC.md`),
  `--autoplay`
  (the winnability oracle, `AUTOPLAY-SPECS.md`) with its modifiers
  `--autoplay-hero=<class>`, `--autoplay-level=<easy|normal|hard|impossible>`
  and `--autoplay-speed=<slow|normal|fast>`, `--validate-pack [LO [HI]]` (the
  pack-author winnability report), `--headless` (modifier for the agent
  modes), `--verbose` (agent diagnostics), `--extract`, `--out-dir <dir>`
  (modifier for `--extract`), `--pack-dir <src> <dst>`. Normal play has taken
  no flags; README §3 has described each. Parsing has been STRICT: a flag
  needing a value with none, a value outside its range, an unknown flag, or a
  stray token has printed the reason to stderr and exited `2` without running
  anything.

### 34.2 Packs

- **REQ-481.** A pack has been a self-contained tree (`game.json` +
  `strings/` + `art/` + `audio/` + `maps/` + `palettes/`) or a zipped
  `.openbounty` archive; the engine has treated loose dirs and archives
  interchangeably (`engine/pack.c`, miniz). With no `--pack`, discovery
  (`engine/pack.c pack_discover`) has scanned the `.openbounty` zips in the
  working directory, the user data directory and `<exe>/assets`, in that
  order; one found has opened directly, and more than one has run the pack
  picker (`src/pack_select.c`) before character creation. A bare `--pack`
  name has also found a loose `<name>/game.json` under those roots. The pack
  schema version has been 1 (`PACK-FORMAT.md`). The OpenBounty desktop
  archives have shipped **without** game data -- the user supplies a pack via
  `--extract` (§37) -- while Glory of Rome's archives and apps have carried
  their own pack. `docs/PACK-FORMAT.md` has documented the full format.

### 34.3 Platform

- **REQ-482.** Every release (`docs/RELEASE-PROCESS.md`) has carried: the
  OpenBounty engine and the Glory of Rome package for Linux x86_64
  (tar.gz), Windows x86_64 + i686 (zip, single static .exe, no DLLs) and
  macOS universal (zip, arm64 + x86_64, ad-hoc signed); two Web/WebAssembly
  zips (`.html`/`.js`/`.wasm`/`.data`), `openbounty-*` with King's Bounty
  embedded and `gloryofrome-*` with Glory of Rome; the iOS `.ipa`
  (native Metal, `IOS-BACKEND.md`); and the Android APK and AAB. Releases have
  been sequential build numbers under `release-N` tags; the build number has
  been embedded at compile time and exposed via `--version`.

---

## 35. Recorder, encoder, and harness

- **REQ-490.** `--movie [<path>]` has recorded gameplay to an MP4
  (`src/recorder.c`, `src/encode_mp4*.c`, `src/encode_dialog.c`; vendored
  minih264 + minimp4). Intermediate per-tick frames have lived in
  `/tmp/openbounty-movie-<pid>`; at shutdown an "Encoding…" dialog has run the muxer and the temp
  frames have been deleted, so the file exists only after a clean shutdown.
  With no path argument, output has gone to
  `<user-data>/movie-<timestamp>.mp4`.
- **REQ-491.** There has been no scripted-input harness: `src/frame_host.c`
  and `src/input_host.c` have been the window and input seams (REQ-442), and
  the gameplay tests have driven the engine directly. The engine's JSON state
  snapshot (`engine/state_serialize.c`) has served the save writer, the
  recorder and the state tests.

---

## 36. Autoplay planner

- **REQ-500.** The engine has exposed a determinism-preserving introspection
  surface for an autoplay planner: `GameRngSnapshot` / `GameRngRestore`
  (`engine/game.c`) snapshot and restore the process-global world RNG so the
  planner's plan-time engine replays do not perturb the live game's RNG
  sequence; `GamePeekChest` (§22.1) returns what a chest *would* yield without
  mutating state; and combat's stable per-encounter RNG seeding (§25.13) makes
  a fight's outcome a pure function of (seed, encounter identity, mode) so a
  predicted result matches the live one. These have existed so an automated
  player can plan ahead deterministically.

---

## 37. Tools, asset extraction

- **REQ-510.** Asset extraction has been pure C compiled into the
  `openbounty` binary (`tools/extract*.c`); the invocation has been
  `./build/debug/openbounty --extract`, which takes no path. The extractor has
  read a user's DOS King's Bounty distribution (`KB.EXE`, etc.; input
  `legacy/bin/KB.EXE` if present, else `./KB.EXE`) and written a complete
  `.openbounty` pack (palette, font, sprites, tiles, chrome, audio metadata,
  `game.json`, strings) to `<user-data>/<pack_id>.openbounty`. It has been
  split one translation unit per pipeline stage: `extract_unpack.c`,
  `extract_lzw.c`, `extract_vga.c`, `extract_png.c`, `extract_chrome.c`,
  `extract_gamejson.c`, `extract_io.c`, plus the dispatcher `extract.c`.
  `--out-dir` has emitted a loose tree instead of a zip; `--pack-dir <src>
  <dst>` has zipped a pre-extracted tree into a `.openbounty` archive. The
  King's Bounty pack has **not** been distributed (the DOS-extracted assets
  are copyright-restricted). The Python in `tools/` has been the Glory of Rome
  authoring tools (REQ-022), which no build step runs.

---

# Part IV, Deviations & data

## 38. Known deviations from OpenKB

These have been the standing differences between OpenBounty and OpenKB: where
the architecture diverges without changing gameplay (§38.1), where gameplay
itself differs from OpenKB or DOS (§38.2), where an OpenKB behaviour is
preserved even though DOS differs (§38.3), and what is deliberately out of
scope (§38.4).

### 38.1 Architectural divergences (no gameplay change)

- **REQ-520.** raylib (and native Metal on iOS) instead of SDL 1.2; JSON saves
  instead of the OpenKB 20,421-byte binary (saves are not interchangeable with
  OpenKB or DOS); `.openbounty` packs instead of DOS `.CC` packs / module
  dirs; Java-style LCG seeded from `g->seed` instead of libc `rand()`;
  per-tile terrain+interact struct instead of a 128-byte tile-id space;
  per-tile sign text instead of a global indexed list. See §1.10.

### 38.2 Gameplay deviations from OpenKB / DOS

- **REQ-521.** **Pikemen cost**: OpenBounty has used 300 (DOS-original);
  OpenKB inherited 800.
- **REQ-522.** **Siege-weapons gate**: OpenBounty has bounced the hero off a
  monster or villain castle gate without siege weapons, the DOS behaviour;
  OpenKB has never checked the flag (§25.12).
- **REQ-523.** **Astrology dwelling refresh**: this has matched OpenKB
  exactly: only the matching dwelling refills to `max_population`;
  non-matching dwellings keep their current count (§24.2).
- **REQ-525.** **The max-spells chest outcome has been reachable.** OpenKB's chest
  tables set `chance_spell_power == chance_max_spells`, so its max_spells
  outcome can never roll; OpenBounty's have lowered `chance_spell_power`
  (§22.1) to open a window for it. This and the Pikemen cost have been the
  catalog-data deviations between OpenBounty and OpenKB.

### 38.3 OpenKB-faithful behaviours (preserved even where DOS differs)

- **REQ-526.** **Strict gold checks**: spell, boat and siege purchases have
  failed at `gold <= cost`, matching OpenKB; the alcove has failed only at
  `gold < cost`.

### 38.4 Non-goals

- **REQ-527.** **Multiplayer**: OpenKB carried SDL_net combat; OpenBounty has
  had none, by design. **Module system**: a single active pack has stood in
  for OpenKB's discovery + chain-of-responsibility loader. **DOS binary save
  compatibility** has not been a goal; saves are JSON.

---

## Appendix A, Complete data tables (from `game.json`)

These tables have been reproduced from `assets/kings-bounty/game.json` (the
reference `kings-bounty` pack; Glory of Rome's are in `GLORY-OF-ROME.md`). The
pack's values have been the authoritative source; the spec body names JSON
paths rather than copying values, so this appendix has been the one place the
full tables appear.

### A.1 Troops (25)

| # | id | Name | SL | HP | MV | Recruit | Spoils | Dwelling | MaxPop | Growth/Wk | Morale |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | peasants | Peasants | 1 | 1 | 1 | 10 | 1 | plains | 250 | 6 | A |
| 1 | sprites | Sprites | 1 | 1 | 1 | 15 | 1 | forest | 200 | 6 | C |
| 2 | militia | Militia | 2 | 2 | 2 | 50 | 5 | castle | 0 | 5 | A |
| 3 | wolves | Wolves | 2 | 3 | 3 | 40 | 4 | plains | 150 | 5 | D |
| 4 | skeletons | Skeletons | 2 | 3 | 2 | 40 | 4 | dungeon | 150 | 5 | E |
| 5 | zombies | Zombies | 2 | 5 | 1 | 50 | 5 | dungeon | 100 | 5 | E |
| 6 | gnomes | Gnomes | 2 | 5 | 1 | 60 | 6 | forest | 250 | 5 | C |
| 7 | orcs | Orcs | 2 | 5 | 2 | 75 | 7 | hill | 200 | 5 | D |
| 8 | archers | Archers | 2 | 10 | 2 | 250 | 25 | castle | 0 | 5 | B |
| 9 | elves | Elves | 3 | 10 | 3 | 200 | 20 | forest | 100 | 4 | C |
| 10 | pikemen | Pikemen | 3 | 10 | 2 | 300 | 30 | castle | 0 | 4 | B |
| 11 | nomads | Nomads | 3 | 15 | 2 | 300 | 30 | plains | 150 | 4 | C |
| 12 | dwarves | Dwarves | 3 | 20 | 1 | 350 | 30 | hill | 100 | 4 | C |
| 13 | ghosts | Ghosts | 4 | 10 | 3 | 400 | 40 | dungeon | 25 | 3 | E |
| 14 | knights | Knights | 5 | 35 | 1 | 1000 | 100 | castle | 250 | 3 | B |
| 15 | ogres | Ogres | 4 | 40 | 1 | 750 | 75 | hill | 200 | 3 | D |
| 16 | barbarians | Barbarians | 4 | 40 | 3 | 750 | 75 | plains | 100 | 3 | C |
| 17 | trolls | Trolls | 4 | 50 | 1 | 1000 | 100 | forest | 25 | 3 | D |
| 18 | cavalry | Cavalry | 4 | 20 | 4 | 800 | 80 | castle | 0 | 2 | B |
| 19 | druids | Druids | 5 | 25 | 2 | 700 | 70 | forest | 25 | 2 | C |
| 20 | archmages | Archmages | 5 | 25 | 1 | 1200 | 120 | plains | 25 | 2 | C |
| 21 | vampires | Vampires | 5 | 30 | 1 | 1500 | 150 | dungeon | 50 | 2 | E |
| 22 | giants | Giants | 5 | 60 | 3 | 2000 | 200 | hill | 50 | 2 | C |
| 23 | demons | Demons | 6 | 50 | 1 | 3000 | 300 | dungeon | 25 | 1 | E |
| 24 | dragons | Dragons | 6 | 200 | 1 | 5000 | 500 | hill | 25 | 1 | D |

Abilities (mask, §3.5): sprites FLY; skeletons/zombies UNDEAD; ghosts
ABSORB+UNDEAD; trolls REGEN; druids MAGIC; archmages FLY+MAGIC; vampires
FLY+LEECH+UNDEAD; demons FLY+SCYTHE; dragons FLY+IMMUNE.

### A.2 Spells (14)

| # | id | Name | Cost | Kind |
|---|---|---|---|---|
| 0 | clone | Clone | 2000 | combat |
| 1 | teleport | Teleport | 500 | combat |
| 2 | fireball | Fireball | 1500 | combat |
| 3 | lightning | Lightning | 500 | combat |
| 4 | freeze | Freeze | 300 | combat |
| 5 | resurrect | Resurrect | 5000 | combat |
| 6 | turn_undead | Turn Undead | 2000 | combat |
| 7 | bridge | Bridge | 100 | adventure |
| 8 | time_stop | Time Stop | 200 | adventure |
| 9 | find_villain | Find Villain | 1000 | adventure |
| 10 | castle_gate | Castle Gate | 1000 | adventure |
| 11 | town_gate | Town Gate | 500 | adventure |
| 12 | instant_army | Instant Army | 1000 | adventure |
| 13 | raise_control | Raise Control | 500 | adventure |

### A.3 Artifacts (8)

| # | id | Name | Zone | local_idx | Power (JSON → enum) |
|---|---|---|---|---|---|
| 0 | sword | The Sword of Prowess | saharia | 1 | `increased_damage` → INCREASED_DAMAGE |
| 1 | shield | The Shield of Protection | forestria | 0 | `quarter_protection` → QUARTER_PROTECTION |
| 2 | crown | The Crown of Command | archipelia | 0 | `double_leadership` → DOUBLE_LEADERSHIP |
| 3 | articles | The Articles of Nobility | continentia | 1 | `increase_commission` → INCREASE_COMMISSION |
| 4 | amulet | The Amulet of Augmentation | saharia | 0 | `double_spell_power` → DOUBLE_SPELL_POWER |
| 5 | ring | The Ring of Heroism | continentia | 0 | `double_max_spells` → DOUBLE_MAX_SPELLS |
| 6 | book | The Book of Necros | archipelia | 1 | `unknown_xxx1` → UNKNOWN (no effect) |
| 7 | anchor | The Anchor of Admirability | forestria | 1 | `cheaper_boat_rental` → CHEAPER_BOATS |

### A.4 Classes (4)

| id | Name | Starting gold | Starting army |
|---|---|---|---|
| knight | Knight | 7500 | 20 Militia + 2 Archers |
| paladin | Paladin | 10000 | 20 Peasants + 20 Militia |
| sorceress | Sorceress | 10000 | 30 Peasants + 10 Sprites |
| barbarian | Barbarian | 7500 | 20 Wolves |

Per-rank tables are in §8.2.

### A.5 Villains (17)

| # | id | Name | Zone | Reward |
|---|---|---|---|---|
| 0 | murray | Murray the Miser | continentia | 5000 |
| 1 | hack | Hack the Rogue | continentia | 6000 |
| 2 | aimola | Princess Aimola | continentia | 7000 |
| 3 | baron_makahl | Baron Johnno Makahl | continentia | 8000 |
| 4 | dread_rob | Dread Pirate Rob | continentia | 9000 |
| 5 | caneghor | Canegor the Mystic | continentia | 10000 |
| 6 | moradon | Sir Moradon the Cruel | forestria | 12000 |
| 7 | barrowpine | Prince Barrowpine | forestria | 14000 |
| 8 | bargash | Bargash Eyesore | forestria | 16000 |
| 9 | rinaldus | Rinaldus Drybone | forestria | 18000 |
| 10 | ragface | Ragface | archipelia | 20000 |
| 11 | mahk | Mahk Bellowspeak | archipelia | 25000 |
| 12 | auric | Auric Whiteskin | archipelia | 30000 |
| 13 | czar_nickolai | Czar Nickolai the Mad | archipelia | 35000 |
| 14 | magus | Magus Deathspell | saharia | 40000 |
| 15 | urthrax | Urthrax Killspite | saharia | 45000 |
| 16 | arech | Arech Dragonbreath | saharia | 50000 |

Per-zone counts: `[6, 4, 4, 3]`. Each villain has had a fixed army
(`game.json:villains[].army`) copied into its host castle at salt time.
Per-villain `features` / `crimes` flavor text has lived in
`strings/en.json:villain_descriptions` (17 entries).

### A.6 Towns (26)

| id | Name | Zone | x | y |
|---|---|---|---|---|
| riverton | Riverton | continentia | 29 | 51 |
| underfoot | Underfoot | forestria | 58 | 59 |
| paths_end | Path's End | continentia | 38 | 13 |
| anomaly | Anomaly | forestria | 34 | 40 |
| topshore | Topshore | archipelia | 5 | 13 |
| lakeview | Lakeview | continentia | 17 | 19 |
| simpleton | Simpleton | archipelia | 13 | 3 |
| centrapf | Centrapf | archipelia | 9 | 24 |
| quiln_point | Quiln Point | continentia | 14 | 36 |
| midland | Midland | forestria | 58 | 30 |
| xoctan | Xoctan | continentia | 51 | 35 |
| overthere | Overthere | archipelia | 57 | 6 |
| elans_landing | Elan's Landing | forestria | 3 | 26 |
| kings_haven | King's Haven | continentia | 17 | 42 |
| bayside | Bayside | continentia | 41 | 5 |
| nyre | Nyre | continentia | 50 | 50 |
| dark_corner | Dark Corner | forestria | 58 | 3 |
| isla_vista | Isla Vista | continentia | 57 | 58 |
| grimwold | Grimwold | saharia | 9 | 3 |
| japper | Japper | archipelia | 13 | 56 |
| vengeance | Vengeance | saharia | 7 | 60 |
| hunterville | Hunterville | continentia | 12 | 60 |
| fjord | Fjord | continentia | 46 | 28 |
| yakonia | Yakonia | archipelia | 49 | 55 |
| woods_end | Woods End | forestria | 3 | 55 |
| zaezoizu | Zaezoizu | saharia | 58 | 15 |

Each town has additionally carried gate coords, boat coords, an intel
castle, and an optional pinned spell (`game.json:towns[]`). Hunterville has
pinned the Bridge spell.

### A.7 Castles (27)

| id | Name | Zone | Tier |
|---|---|---|---|
| azram | Azram | continentia | 0 |
| basefit | Basefit | forestria | 1 |
| cancomar | Cancomar | continentia | 0 |
| duvock | Duvock | forestria | 1 |
| endryx | Endryx | archipelia | 2 |
| faxis | Faxis | continentia | 0 |
| goobare | Goobare | archipelia | 2 |
| hyppus | Hyppus | archipelia | 2 |
| irok | Irok | continentia | 0 |
| jhan | Jhan | forestria | 1 |
| kookamunga | Kookamunga | continentia | 0 |
| lorsche | Lorsche | archipelia | 2 |
| mooseweigh | Mooseweigh | forestria | 1 |
| nilslag | Nilslag | continentia | 0 |
| ophiraund | Ophiraund | continentia | 0 |
| portalis | Portalis | continentia | 0 |
| quinderwitch | Quinderwitch | forestria | 1 |
| rythacon | Rythacon | continentia | 0 |
| spockana | Spockana | saharia | 3 |
| tylitch | Tylitch | archipelia | 2 |
| uzare | Uzare | saharia | 3 |
| vutar | Vutar | continentia | 0 |
| wankelforte | Wankelforte | continentia | 0 |
| xelox | Xelox | archipelia | 2 |
| yeneverre | Yeneverre | forestria | 1 |
| zyzzarzaz | Zyzzarzaz | saharia | 3 |
| king_maximus | of King Maximus | continentia |, (special) |

### A.8 Other data blocks

- **Zones (4):** `continentia` (home), `forestria`, `archipelia`, `saharia`;
  each 64×64, with hero/home spawn, neighbors, and a salt budget
  (`game.json:zones[]`).
- **tile_codes:** 54 entries (`game.json:tile_codes`), mapping ASCII map
  characters to terrain + art + flags.
- **banners:** every banner and prompt template (`strings/en.json:banners`).
- **economy / spawn / score / combat blocks:** values reproduced inline in
  §22.1 (chest), §15.2 (spawn), §26.1 (score), §14.1 (morale chart).
