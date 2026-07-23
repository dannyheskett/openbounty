# OpenBounty, Specification

**Status:** living document. Reproduction-grade record of the implementation
as built. This is the single authoritative specification for the game rules
and the implementation together.

A complete reimplementation of OpenBounty has been possible from this document
alone, paired with the asset pack at `assets/kings-bounty/` (sprites, palette,
maps, audio).

OpenBounty is a faithful raylib reimplementation of King's Bounty (1990, New
World Computing). It descends from **OpenKB** (an earlier SDL 1.2
reimplementation) and deliberately diverges from it in several architectural
respects (§1.10); gameplay-significant constants and formulas match OpenKB
except where a deviation is explicitly flagged (§34).

**Conventions.**
- Each requirement carries a stable identifier of the form `REQ-NNN`. Where a
  numeric value or table appears in the asset pack, the requirement names the
  JSON path (e.g. `game.json:economy.chest.chance_gold`) rather than copying
  the value, so the data and the spec stay synchronised.
- Requirements are written in the present perfect tense ("the game has done
  X"), as a factual reproduction-grade record.
- Code citations name a **file and function** (e.g. `engine/game.c
  GameOnStep`) rather than a line number, so they survive edits. The code lives
  under `engine/` (the raylib-free engine library) and `src/` (the raylib
  shell); see §1.
- **Provenance citations are a separate thing.** Comments in the source
  occasionally cite `play.c`, `bounty.c`, or a bare `game.c` with a line
  number. Those name files in the **OpenKB source and the DOS
  decompilation this port was derived from**, they are not files in this
  repository, and they are recorded so a reader can trace where a formula or
  table came from. `OPENKB-SPEC.md` documents that predecessor. Any citation
  naming a path under `engine/` or `src/` refers to this repository.
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
38. [Known deviations from OpenKB and incomplete features](#38-known-deviations-from-openkb-and-incomplete-features)
- [Appendix A, Complete data tables (from `game.json`)](#appendix-a--complete-data-tables-from-gamejson)

---

# Part I, Architecture & implementation

## 1. Top-level architecture

### 1.1 The engine / shell split

- **REQ-001.** The codebase has split into two halves with a hard boundary:
  - **engine** (`engine/`): pure game logic, mechanics, and state. It has
    built as the static archive `libobengine.a` and has been **free of raylib,
    audio, window, and GPU dependencies**. It vendors cJSON and miniz inside
    the archive. It has emitted abstract events to its host through the
    callbacks declared in `engine/include/ui_host.h`.
  - **shell** (`src/`): renderer, audio, input, and screen flows. It has
    linked `libobengine.a` and implemented the host callbacks for real
    (rendering prompts, dialogs, views, playing audio, capturing frames).
- **REQ-002.** The engine has exposed its public API through headers in
  `engine/include/`; consumers add `-Iengine/include`. The shell's own headers
  have lived in `src/`. No engine `.c` file has included a `src/` header, and
  no engine `.c` file has included `raylib.h` outside `engine/headless/`.
- **REQ-003.** The boundary has been **verified at build time**, not merely by
  convention. `make all` has linked `tests/library/consumer.c` +
  `engine/host_noop.c` + `libobengine.a` using only `-Iengine/headless
  -Iengine/include` and only `-lm -lpthread` (no raylib, no X11), with
  `--whole-archive` so every engine object is pulled in. If any engine object
  has come to depend on a shell header or symbol, this link has failed and
  `make all` has failed. The output binary is discarded; a stamp file
  (`build/libtest-pass.stamp`) records success.
- **REQ-004.** Two real binaries have been produced from the same engine
  archive: `build/openbounty` (the game) and `build/openbounty-test` (the
  greatest test runner).

### 1.2 Process flow

- **REQ-010.** `main` (`src/main.c`) has parsed argv, resolved the active pack,
  loaded `Resources` from `game.json`, created the window and render target,
  loaded assets, and entered the macro-state machine (§5.2). Early-exit CLI
  modes (`--extract`, `--pack-dir`, `--version`, `--help`) have been handled
  before window creation (`src/shell_earlyexit.c`).
- **REQ-011.** Engine combat (state, AI, headless turn loop, damage formula,
  combat spells) has lived in `engine/combat.c`. The **rendered** combat loop
  (`RunCombat`, modal player input, target picker, per-frame present) has lived
  in `src/combat_loop.c` (shell). Both halves have shared the one `Combat`
  struct defined in `engine/include/combat.h`.

### 1.3 Build system

- **REQ-020.** The build has been a single hand-written `Makefile` (no CMake,
  no autotools). Compile flags: `-std=c99 -Wall -Wextra -O2`. The engine
  archive has additionally been compiled `-fPIC -DOB_HEADLESS` with
  `-Iengine/headless -Iengine/include`.
- **REQ-021.** Principal targets:
  - `make` / `make all`, the two binaries + `libobengine.a` + the library
    boundary check + the shipped pack zips.
  - `make test` runs `build/openbounty-test`: the **entire** greatest suite
    (unit + regression + e2e, including the combat-formula golden digests).
    There is no separate playtest or scenario runner.
  - `make release`: `build/openbounty-release`, `-O2`, assets embedded.
  - `make windows` / `make windows-debug`, Win64 + Win32 cross-compile
    (mingw-w64), assets always embedded, static link, no DLLs.
  - `make mac`: macOS universal (arm64 + x86_64).
  - `make extract` / `make extract-pack`, wrappers around `./build/openbounty
    --extract`.
  - `make dist-{linux,windows,mac}` / `make dist`, distribution archives.
  - `make clean`: removes `build/` and `dist/` archives.
- **REQ-022.** There is **no Python** in the project. Asset extraction and pack
  building are pure C, compiled into the `openbounty` binary (§37).

### 1.4 Vendor code

- **REQ-030.** `third_party/` has vendored: cJSON (`cjson/`, JSON parse),
  miniz (`miniz/`, ZIP read/write for `.openbounty` packs), greatest
  (`greatest/`, single-header test framework), minih264 + minimp4
  (`--movie` MP4 encoder/muxer), and raylib (source for reference plus
  prebuilt static archives for linux / win64 / win32 / mac).
- **REQ-031.** cJSON and miniz have been compiled **into** `libobengine.a` so
  consumers of the engine library have not needed their own copies.

### 1.5 Memory model

- **REQ-040.** The game has held a single root `Game` struct in memory
  (`engine/include/game.h`); no persistent gameplay state has existed outside
  that struct, the loaded `Resources` (read-only), and platform handles
  (window, audio device, render target).
- **REQ-041.** The `Game` struct has been a flat value type with fixed-size
  inline arrays (no per-game heap allocation for the core state). All array
  caps have been compile-time constants (§3). Variable-length collections
  (consumed tiles, dwellings, placements, foes) have been bounded arrays with a
  parallel `*_count` field.
- **REQ-042.** The `Game` has held a `const Resources *res` pointer to the
  loaded asset pack; the engine has never mutated `*res` after startup.

### 1.6 Coordinate system

- **REQ-050.** All gameplay coordinates have been integer tile indices in
  zone-local space; `(0, 0)` is the top-left of each zone. `x` increases east,
  `y` increases south. There has been no sub-tile position.

### 1.7 Naming conventions

- **REQ-060.** Public engine functions acting on the whole game have used the
  `Game*` prefix (`GameInit`, `GameOnStep`, `GameBuyTroop`, …). Combat engine
  functions have used the `combat_*` prefix. Shell screen flows have used
  `screen_*` / view-specific names. Host callbacks the shell must provide have
  been declared in `engine/include/ui_host.h`.

### 1.8 Source layout

- **REQ-070.** Engine sources (`engine/*.c`), each with a header in
  `engine/include/`:

  | Source | Responsibility |
  |---|---|
  | `game.c` | Game state, `GameInit`, salting, mechanics, RNG, scoring |
  | `map.c` | Tile grid, `.dat` parsing, placement stamping |
  | `fog.c` | Per-tile fog of war |
  | `adventure.c` | Walkability + tile-step interact dispatch |
  | `step.c` | `step_try`, one-tile movement + bookkeeping |
  | `combat.c` | Combat state, AI, headless turn loop, damage, combat spells |
  | `combat_log.c` | Combat log line append (pure data) |
  | `flows.c` | Encounter / week-end / endgame flows |
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

- **REQ-071.** Engine public headers (`engine/include/`): `game.h`, `map.h`,
  `fog.h`, `adventure.h`, `step.h`, `combat.h`, `flows.h`, `savegame.h`,
  `savepath.h`, `state_serialize.h`, `tables.h`, `resources.h`, `tile.h`,
  `pending.h`, `pack.h`, `spells_adventure.h`, `assets_bytes.h`,
  `end_screen.h`, `ui_host.h`, `view_kind.h`, `dwelling_kind.h`, `fatal.h`.
  Plus `engine/headless/` (raylib stub headers `raylib.h`, `raylib_stub.h`,
  `input_keys.h`) for headless builds.

- **REQ-072.** Shell sources (`src/*.c`): `main.c` (CLI + init + main loop);
  the `shell_*` flow modules (`shell_menu`, `shell_tempdeath`, `shell_weekend`,
  `shell_audience`, `shell_cheats`, `shell_fastquit`, `shell_frame`,
  `shell_promptdispatch`, `shell_actions`, `shell_earlyexit`); `combat_loop` +
  `combat_render`; `views` + `views_render`; `overlay`, `hud`, `chrome`,
  `map_render`, `ui`, `prompt`, `input`, `startup`, `end_cartoon`,
  `pack_select`, `assets`, `audio`, `screenshot`, `sprites`, `tile_cache`,
  `palette`, `bfont`, `recorder`, `frame_host`, `input_host`, `encode_dialog`,
  `encode_mp4*`. The `src/screens/` subdirectory holds location/dialog screen
  modules (`home_castle`, `own_castle`, `recruit_soldiers`, `dwelling`,
  `alcove`, `end_game`).

### 1.9 Important global state

- **REQ-080.** Outside the `Game` struct, the only durable state has been:
  the loaded `Resources` (read-only after startup), the active `Map` and `Fog`
  for the hero's current zone (held by the shell's main loop, referenced by
  `Game.position.zone`), the world RNG state (a process-global LCG, §6.1), and
  platform handles. Per-continent fog snapshots have lived inside
  `Game.world.continent_fog[]` and swapped with the active `Fog` on zone change.

### 1.10 Engine choices vs OpenKB

- **REQ-090.** OpenBounty descends from OpenKB but diverges deliberately. These
  are intended architectural choices, not bugs:

  | Concern | OpenKB | OpenBounty |
  |---|---|---|
  | Window / render | SDL 1.2 | raylib 6 |
  | Audio | SDL_mixer | raylib audio |
  | Net | SDL_net (combat) | none |
  | Saves | 20,421-byte binary | JSON (version 8) |
  | Asset bundling | DOS `.CC` packs / module dirs | single `assets/kings-bounty/` tree + `.openbounty` packs |
  | Module system | discovery + chain-of-responsibility loader | N/A, one active pack |
  | Render target | 320×200, scaled | 320×200, integer-scaled to a 640×400 base window |
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

- **REQ-101.** Gameplay state has been endian-independent: saves are JSON text,
  not raw memory dumps, so no byte-order handling has been required at the
  gameplay layer. The vendored miniz handles ZIP byte order internally.

### 2.3 String identifiers

- **REQ-102.** Catalog entries (troops, spells, castles, towns, villains,
  artifacts, classes, zones) have been referenced by **string id**
  (lowercase snake_case), not array index. Ids are stable across pack versions
  and are what appear in save files (`troop_id: "knights"`). This keeps saves
  readable and pack-portable.
- **REQ-103.** String ids have been stored in fixed-size char arrays
  (commonly `[24]` or `[32]`; see the per-struct field widths in §4). An empty
  id (`id[0] == '\0'`) has meant "no entry / empty slot".

### 2.4 Memory ownership

- **REQ-104.** `Game` has owned its inline state by value. `Game.res` has been
  a borrowed pointer into the process-lifetime `Resources` (never freed by the
  game, never mutated). `Map` and `Fog` have been owned by the shell's main
  loop, not by `Game`. The engine has allocated no per-game heap.

### 2.5 Numeric ranges

- **REQ-105.** Gold, leadership, commission, and counts have been plain `int`
  and have not been expected to overflow within a legal playthrough. Score has
  been clamped at `>= 0` (§26). The day counter has been bounded by the
  difficulty table (max 900).

---

## 3. Global constants and limits

### 3.1 Storage caps (`engine/include/game.h`)

- **REQ-110.** Compile-time storage caps:

  | Constant | Value | Meaning |
  |---|---|---|
  | `GAME_NAME_LEN` | 16 | Hero name buffer |
  | `GAME_ARMY_SLOTS` | 5 | Player army stack slots |
  | `GAME_CONTINENTS` | 4 | Zones in the base pack |
  | `GAME_TOWNS` | 26 | Town record rows |
  | `GAME_CASTLES` | 26 | Castle record rows (player-trackable castles A..Z; the King's castle is the special 27th catalog entry, §17.1) |
  | `GAME_MAX_MUTATIONS` | 1024 | Consumed-tile records (artifact pickups, opened chests). Raised from 64, a four-continent playthrough collects 50+ chests per continent; overflow silently dropped consumed entries and chests respawned on re-entry. |
  | `GAME_MAX_DWELLINGS` | 64 | Per-game dwelling state rows |
  | `GAME_MAX_PLACEMENTS` | 128 | Salt-time randomized placements |
  | `GAME_MAX_FOES` | 160 | Foe state rows (hostile + friendly share the flat table). Sized for all four continents at once: OpenKB's `foe_coords[4][40]` gave each continent its own 40 slots (35 hostile + 5 friendly); the flat shared table must therefore hold 4 × 40 = 160, or the first-salted continents exhaust it and later ones (Archipelia, Saharia) get no foes. `salt_continent` caps each continent to `GAME_MAX_HOSTILE_PER_ZONE` (35) hostiles + 5 friendlies. |
  | `CONTRACT_CYCLE_MAX` | 8 | Contract cycle buffer (real length from `res->contract.cycle_length`) |

- **REQ-111.** Tunable constants that *define gameplay* (day/week lengths,
  costs, contract cycle length, difficulty table) have **not** been compile-time, they live in `game.json` and are read through `g->res`. Only storage caps
  (bounded by struct sizes) have been compile-time.

### 3.2 Resource caps (`engine/include/resources.h`)

- **REQ-112.** The parsed `Resources` has bounded every array at compile time:
  `RES_MAX_TOWNS=32`, `RES_MAX_CASTLES=32`, `RES_MAX_ZONES=8`,
  `RES_MAX_NEIGHBORS=8`, `RES_MAX_ZONE_OBJECTS=256` (per kind, per zone),
  `RES_ID_LEN=32`, `RES_NAME_LEN=48`, `RES_PATH_LEN=128`,
  `RES_TILE_CODE_COUNT=128`, `RES_BANNER_LEN=320`, `RES_MAX_KEYBINDS=24`,
  `RES_MAX_COUNT_BUCKETS=8`, among others.

### 3.3 Catalog caps (`engine/include/tables.h`)

- **REQ-113.** Catalog sizes: `CAT_TROOPS_MAX=32`, `CAT_SPELLS_MAX=32`,
  `CAT_CLASSES_MAX=8`, `CAT_VILLAINS_MAX=32`, `CAT_ARTIFACTS_MAX=16`,
  `CLASS_MAX_RANKS=4`, `CLASS_MAX_STARTING_TROOPS=2`. The shipped
  `kings-bounty` pack has filled these to 25 troops, 14 spells, 4 classes,
  17 villains, 8 artifacts (§Appendix A).

### 3.4 Enums

- **REQ-120.** `Difficulty` (`engine/include/game.h`): `DIFFICULTY_EASY=0`,
  `_NORMAL=1`, `_HARD=2`, `_IMPOSSIBLE=3`.
- **REQ-121.** `Mount` (`engine/include/game.h`): `MOUNT_RIDE=0`, `MOUNT_SAIL`,
  `MOUNT_FLY`. `TravelMode`: `TRAVEL_WALK=0`, `TRAVEL_BOAT`. `Mount` is the
  long-term possession; `TravelMode` is the current movement state.
- **REQ-122.** `CastleOwnerKind` (`engine/include/game.h`):
  `CASTLE_OWNER_PLAYER=0`, `_MONSTERS`, `_VILLAIN`, `_SPECIAL`.
- **REQ-123.** `Terrain` (`engine/include/tile.h`): `TERRAIN_GRASS=0`,
  `_FOREST`, `_MOUNTAIN`, `_WATER`, `_DESERT`, `TERRAIN_COUNT`.
- **REQ-124.** `Interact` (`engine/include/tile.h`): `INTERACT_NONE=0`,
  `_CASTLE_GATE`, `_TOWN`, `_TREASURE_CHEST`, `_SIGN`, `_ARTIFACT`,
  `_DWELLING_PLAINS`, `_DWELLING_FOREST`, `_DWELLING_HILLS`,
  `_DWELLING_DUNGEON`, `_ALCOVE`, `_ORB`, `_TELECAVE`, `_NAVMAP`, `_FOE`,
  `INTERACT_COUNT`.
- **REQ-125.** `DwellingKind` (`engine/include/dwelling_kind.h`):
  `DWELLING_KIND_PLAINS=0`, `_FOREST`, `_HILL`, `_DUNGEON`.
- **REQ-126.** `ViewKind` (`engine/include/view_kind.h`): `VIEW_NONE=0`,
  `VIEW_MENU`, `_CHARACTER`, `_ARMY`, `_SPELLS`, `_CONTRACT`, `_PUZZLE`,
  `_WORLDMAP`, `_OPTIONS`, `_CONTROLS`, `_TOWN`, `_HOME_CASTLE`, `_OWN_CASTLE`,
  `_DWELLING`, `_ALCOVE`, `_RECRUIT_SOLDIERS`, `_WIN`, `_LOSE`.

### 3.5 Troop ability flags (`engine/include/tables.h`)

- **REQ-130.** The ability mask has been an 8-bit field:
  `TROOP_ABIL_FLY=0x01`, `_REGEN=0x02`, `_MAGIC=0x04`, `_IMMUNE=0x08`,
  `_ABSORB=0x10`, `_LEECH=0x20`, `_SCYTHE=0x40`, `_UNDEAD=0x80`. Semantics
  in §13.2 / §25.7.

### 3.6 Artifact power enum (`engine/include/tables.h`)

- **REQ-131.** `ArtifactPower`: `ARTIFACT_POWER_UNKNOWN=0` (default when JSON
  omits the string), `_INCREASED_DAMAGE`, `_QUARTER_PROTECTION`,
  `_DOUBLE_LEADERSHIP`, `_INCREASE_COMMISSION`, `_DOUBLE_SPELL_POWER`,
  `_DOUBLE_MAX_SPELLS`, `_CHEAPER_BOATS`.

### 3.7 Combat-grid constants (`engine/include/combat.h`)

- **REQ-132.** `COMBAT_W=6`, `COMBAT_H=5`, `COMBAT_SIDES=2`, `COMBAT_SLOTS=5`,
  `COMBAT_SIDE_PLAYER=0`, `COMBAT_SIDE_AI=1`, `COMBAT_BANNER_LEN=80`,
  `COMBAT_LOG_LINES=8`, `COMBAT_LOG_LINE_LEN=80`. Per-side combat artifact
  bits: `COMBAT_POWER_INCREASED_DAMAGE=(1<<0)`,
  `COMBAT_POWER_QUARTER_PROTECTION=(1<<1)` (internal, never serialized).

### 3.8 Cost / time constants

- **REQ-133.** Costs and time constants are data, not code. Cost defaults in
  the shipped pack: `economy.boat_cost_normal=500`, `boat_cost_cheap=100`,
  `siege_cost=3000`, `alcove_cost=5000` (§23, §Appendix A). Time:
  `time.day_steps=40`, `week_days=5`, `days_per_difficulty=[900,600,400,200]`.
  Map dimensions: `MAP_MAX_W=64`, `MAP_MAX_H=64` (`engine/include/map.h`).

---

## 4. The `Game` struct, complete game state

### 4.1 Overview

- **REQ-140.** `struct Game` (`engine/include/game.h`) has held the full
  adventure-screen state. Its fields mirror the JSON save schema so
  serialization is 1:1 (§27). All enumerations have been keyed by string ids
  from `tables.h`, keeping save files readable and avoiding magic numbers.
- **REQ-141.** The struct has **not** owned the map tiles or fog; those live in
  `Map`/`Fog` (`engine/include/map.h`, `fog.h`). The game references them by
  zone id; the caller loads the matching map when `position.zone` changes.

### 4.2 Top-level fields

- **REQ-142.** Declaration order and meaning:

  | Field | Type | Meaning |
  |---|---|---|
  | `res` | `const Resources *` | Loaded pack; never owned, never mutated |
  | `version` | `int` | Always `SAVE_VERSION` (8) at runtime |
  | `seed` | `uint64_t` | Expanded RNG seed for this game (derived from `seed_index`) |
  | `seed_from_catalog` | `bool` | False only on the raw-seed path (a caller pinned `seed` directly) |
  | `seed_index` | `int` | `0`–`255` catalog world when `seed_from_catalog` |
  | `character` | `Character` | Name, class+rank, difficulty, mount |
  | `stats` | `Stats` | Gold, leadership, spell power, day/step counters, options |
  | `position` | `Position` | Zone id, `(x,y)`, `(last_x,last_y)`, facing |
  | `travel_mode` | `TravelMode` | Walking vs in boat |
  | `anim_frame` | `int` | 0..3, shared by hero + boat sprites |
  | `hud_visible` | `bool` | Floating HUD bar toggle (persisted) |
  | `army[5]` | `ArmyStack` | Player army stacks |
  | `spells` | `Spellbook` | Per-spell charge counts (14) |
  | `contract` | `Contract` | Active contract + rotation cycle |
  | `artifacts` | `Artifacts` | Found flags (8) |
  | `world` | `WorldProgress` | Zones discovered, orbs found, per-continent fog |
  | `boat` | `BoatState` | Rental flag + parked coords |
  | `towns[26]` | `TownRecord` | Per-town visited + spell-for-sale |
  | `castles[26]` | `CastleRecord` | Per-castle owner + garrison |
  | `scepter` | `ScepterLocation` | Buried scepter zone + `(x,y)` |
  | `consumed[1024]` + `consumed_count` | `TileMutation` | Permanently consumed tiles |
  | `dwellings[64]` + `dwelling_count` | `DwellingState` | Per-dwelling recruit pools |
  | `placements[128]` + `placement_count` | `SaltedPlacement` | Salt-time placements |
  | `foes[160]` + `foe_count` | `FoeState` | Hostile + friendly foe rows (all four continents share the flat table) |

### 4.3 Sub-struct field semantics

- **REQ-143.** `Stats` has held: `gold`, `commission_weekly`,
  `leadership_base`, `leadership_current`, `followers_killed`, `score`,
  `spell_power`, `max_spells`, `knows_magic` (bool), `siege_weapons` (flag),
  `time_stop` (overworld steps where the day does not advance),
  `steps_left_today`, `days_left`, `game_over` (set when `days_left` hits 0),
  `last_commission` and `last_astrology_troop` (UI carry from the most recent
  week-end), and `options[7]` (the controls-menu settings, persisted per game,
  parallel to `res->controls.items[]`).
- **REQ-144.** `Character` has held: `name[16]`, `cls` (a `ClassState`:
  class id, `rank_index` 0..3, denormalized `rank_id` + `rank_title`),
  `difficulty`, and `mount`.
- **REQ-145.** `Position` has held `zone[24]`, `x`, `y`, `last_x`, `last_y`
  (previous tile, for bump-back), and `facing_left`.
- **REQ-146.** `ArmyStack` has held `id[32]` (troop id; empty = empty slot) and
  `count`. `Unit` (used inside garrisons and foe rows) has held `id[24]` and
  `count`.
- **REQ-147.** `Spellbook` has held `counts[14]`, parallel to the 14-spell
  catalog. `GameKnownSpells` sums them.
- **REQ-148.** `Contract` has held `active_id[24]` (current contract, empty =
  none), `cycle[CONTRACT_CYCLE_MAX][24]` (rotation buffer; real length from
  `res->contract.cycle_length`), `last_contract` (last slot issued),
  `max_contract` (next villain to rotate in), and `villains_caught[17]`.
- **REQ-149.** `Artifacts` has held `found[8]`, parallel to the artifact
  catalog. `WorldProgress` has held `zones_discovered[4]`, `orbs_found[4]`, and
  `continent_fog[4]` (per-continent fog snapshots; the active continent's fog
  lives in the shell's standalone `Fog` and is swapped in/out on zone change).
  The puzzle view derives its reveal state directly from
  `contract.villains_caught[]` + `artifacts.found[]`, there is no separate
  puzzle-reveal bookkeeping.
- **REQ-150.** `BoatState`: `has_boat`, `x`, `y`, `zone[24]`.
  `ScepterLocation`: `zone[24]`, `x`, `y`, and `key` (XOR key kept for
  save-load parity).
- **REQ-151.** `TownRecord`: `id[24]`, `visited`, `spell_for_sale[24]`.
  `CastleRecord`: `id[24]`, `visited`, `known` (revealed by Find Villain etc.),
  `owner_kind`, `villain_id[24]` (when owner is a villain), `garrison[5]`.
- **REQ-152.** `TileMutation`: `zone[24]`, `x`, `y`, a tile permanently
  consumed (artifact picked up, chest opened). On load the caller re-applies
  these so the tile renders and behaves as plain terrain.
- **REQ-153.** `DwellingState`: `zone[24]`, `x`, `y`, `troop_id[32]`
  (deterministic, set on first visit), `count` (current available recruits),
  `max_population`.
- **REQ-154.** `SaltedPlacement`: `zone[24]`, `x`, `y`, `kind` (an `Interact`
  enum value stored as `int` for save stability), `id[32]` (payload, troop id
  for a dwelling, artifact id, etc.; may be empty).
- **REQ-155.** `FoeState`: `zone[24]`, `x`, `y`, `placement_id[32]`,
  `garrison[5]`, `alive`, `friendly` (true → recruit dialog; false → attack
  prompt). Friendly and hostile foes share the one `foes[]` table;
  classification is by the `friendly` flag.

### 4.4 Invariants

- **REQ-156.** `version` has always equalled `SAVE_VERSION` at runtime.
- **REQ-157.** An empty army/garrison slot has been encoded by `id[0]=='\0'`
  and/or `count==0`. `GameCompactArmy` keeps non-empty stacks contiguous at the
  front, preserving order.
- **REQ-158.** The player army has never been allowed to become entirely empty
  through garrisoning (`GameGarrisonTroop` refuses the last stack); it can
  become empty only through defeat / dismiss-last, which triggers temp death.
- **REQ-159.** Save schema parity: every persisted field above has a 1:1 JSON
  representation; serialization round-trips without loss (§27).

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
- **REQ-162.** **STARTUP** has shown publisher splash, title splash, then an
  optional credits screen (each ~2.5 s or any key; credits skipped when the
  pack supplies none). Driven by `src/startup.c`.
- **REQ-163.** **CLASS SELECT** has presented four classes (`A`/`B`/`C`/`D`
  for Knight/Paladin/Sorceress/Barbarian) plus `L` for Load and `Esc` to quit.
- **REQ-164.** `L` has opened the **SAVE PICKER** (10 slots, 0..9, plus a
  "New" row); an existing slot loads, an empty slot or "New" falls through to
  character creation.
- **REQ-165.** **CHARACTER CREATION**: name entry (first letter capitalised),
  difficulty selection (Easy/Normal/Hard/Impossible, shown with starting days
  and score multiplier), and an intro banner.
- **REQ-166.** A pack has held a catalog of 256 worlds, selected by an 8-bit
  index. `--seed N` has supplied that index directly (`0`–`255`); out-of-range,
  negative, or unparseable values have printed a notice and fallen back to
  world 0. Without `--seed`, the index has been `(time(NULL) XOR hash(name) XOR
  class_index) AND 0xFF`. `GameInitSeeded` has expanded the index into
  `Game.seed` via `GameSeedFromIndex` (REQ-181a) and recorded it in
  `Game.seed_index`; `GameInit` has remained the raw-seed path, honoring a
  caller-preset non-zero `Game.seed` verbatim and leaving `seed_from_catalog`
  false.
- **REQ-167.** **ADVENTURE** has been the main loop, exited only by combat
  trigger or game end (win via search-on-scepter; loss via `days_left == 0`).
- **REQ-168.** **COMBAT** has run a separate state machine (§25) on a tactical
  grid; on completion, control returns to adventure with results applied.
- **REQ-169.** **END GAME** has shown a win cartoon (§26.5) or a lose dialog
  with the final score; any key returns to title and ultimately **EXIT**.

### 5.3 Main loop

- **REQ-170.** Each frame has performed, in order: harness tick (§35), audio
  tick, `Alt+Enter` fullscreen toggle, `F10` cheat-menu toggle (§31), input
  dispatch, animation update, render to the 320×200 offscreen target, blit to
  the window with integer scaling. Per-frame draw dispatch is `src/shell_frame.c`.
- **REQ-171.** The window has been initialised at **640×400**
  (`CL_WINDOW_W/H` = `CL_SCREEN_W/H` × `CL_SCALE` = 320×200 × 2;
  `src/layout.h`) with `FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT`, minimum
  size 320×200, target FPS 60, and `KEY_NULL` as the raylib exit key (so
  `Escape` does not close the window). The window size is fixed in code, not a
  `game.json` field.
- **REQ-172.** Input dispatch has followed a strict overlay hierarchy
  (highest priority first): fast-quit prompt → active prompt → active view →
  active dialog → adventure-mode actions → movement.
- **REQ-173.** HUD visibility has been remembered when an overlay opens:
  `hud_visible` is forced false and restored to the player's setting on dismiss.
- **REQ-174.** Animation frames (`Game.anim_frame`) have advanced at intervals
  of `0.05 + options[0] * 0.05` s (0.05..0.30 s); with the animation toggle
  (`options[3]`) off, `anim_frame` stays 0.

---

## 6. RNG and determinism

### 6.1 PRNG

- **REQ-180.** A single deterministic Java-style LCG has driven all world
  randomness (`engine/game.c`): `state = (state * 25214903917 + 11) &
  0xFFFFFFFFFFFFFFFF`; `result = state >> 32`. It is a process-global state
  snapshot/restorable via `GameRngSnapshot` / `GameRngRestore` (§36).
- **REQ-181.** The PRNG has been seeded from `Game.seed` XORed with
  `0x5DEECE66D` at game start.
- **REQ-181a.** `GameSeedFromIndex(index)` has expanded an 8-bit catalog index
  into the full-width `Game.seed` with a splitmix64 finalizer. The expansion has
  been required, not cosmetic: the engine reads the seed at three widths, `(seed >> 8)` for spawn rolls (REQ-186a), `(unsigned)` truncation in
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
  same chest yields the same outcome until consumed.
- **REQ-184.** Combat has used an **independent** LCG state (`Combat.rng_state`,
  §25.14) so combat rolls never advance the world RNG.

### 6.2 Determinism guarantees

- **REQ-185.** Given the same catalog index (or raw seed) and the same sequence of player inputs, the
  entire game has been reproduced bit-for-bit (floating point is used only for
  animation and rendering, never gameplay).
- **REQ-186.** All randomised game-start placements (zones, villains, scepter,
  dwellings, foes, telecaves, navmaps, orbs, artifacts, town spells) derive
  from `seed`, so loading a save restores identical placements. A catalog save
  has stored `seed_index` rather than `seed` and re-derived the seed on load
  (REQ-166); this is exact, whereas a raw `seed` is written as a JSON number and
  so is lossy above 2^53.
- **REQ-186a.** Town spell selection has computed `(seed XOR (slot + 1)) mod
  spells_count()` in `uint64_t`. `unsigned long` was not used: it is 32-bit on
  Windows and 64-bit elsewhere, which gave one world different town spells per
  platform once the seed carried entropy above bit 31.
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
  desert, set `steps_left_today = 0`; (3) else decremented `steps_left_today`;
  (4) if `steps_left_today <= 0`, fired the day rollover.
- **REQ-192.** Day rollover has decremented `days_left`, reset
  `steps_left_today` to `time.day_steps`, and set `game_over = true` when
  `days_left` reaches 0. A week boundary is detected when `days_left %
  week_days == 0`, firing week-end processing (§24).

### 7.3 Time spend helpers

- **REQ-193.** `GameSpendDays(g, n, &paid)` has spent `n` days (firing one day
  rollover each), accumulating commission paid into `*paid`.
  `GameSpendWeek(g, &paid)` has spent enough days to cross exactly one week
  boundary; used by End Week and by zone-switch sailing.
- **REQ-194.** Search (key `S`) has cost 10 days regardless of result, except
  that revealing the buried scepter ends the game as a win immediately.
- **REQ-195.** `time_stop` has been reset to 0 at every day rollover, except
  that the Time Stop spell applies its bonus before the rollover and so carries
  over until consumed.

---

## 8. Character, classes, and ranks

### 8.1 Classes

- **REQ-200.** Exactly four classes: Knight (0), Paladin (1), Sorceress (2),
  Barbarian (3), declared in `game.json:classes[]`.
- **REQ-201.** Each class has a `starting_gold` and a `starting_troops` array.
  Initial values: Knight 7,500g (20 militia, 2 archers); Paladin 10,000g
  (20 peasants, 20 militia); Sorceress 10,000g (30 peasants, 10 sprites);
  Barbarian 7,500g (20 wolves). (See §Appendix A for the catalog values.)
- **REQ-202.** Each class has four ranks (0..3) with fields `id`, `name`,
  `villains_needed`, `leadership`, `max_spells`, `spell_power`, `commission`,
  `knows_magic`, `instant_army` (troop catalog index for Instant Army).
  `ClassDef` / `RankDef` are defined in `engine/include/tables.h`.

### 8.2 Per-rank tables

- **REQ-203.** The four classes follow these per-rank tables (delta values;
  cumulative stats accumulate by summing rank 0..n).

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
- **REQ-205.** `commission_weekly` has been the cumulative `commission` deltas,
  optionally augmented by the `INCREASE_COMMISSION` artifact (§20.4).
- **REQ-206.** `max_spells` and `spell_power` accumulate similarly; the
  `DOUBLE_MAX_SPELLS` and `DOUBLE_SPELL_POWER` artifacts apply a multiplicative
  doubling on pickup.
- **REQ-207.** `knows_magic` has been initialised from the rank-0 class flag;
  only the Sorceress sets it true at creation. Any class can learn magic at the
  Archmage Aurange alcove (§18.5).

### 8.4 Rank-up

- **REQ-208.** After each villain capture, `GameMaybeRankUp` has checked the
  next rank's `villains_needed`; while `villains_caught >=
  ranks[rank+1].villains_needed`, the rank advances and stats recompute. On
  rank-up, `leadership_base`/`commission_weekly`/`max_spells`/`spell_power`
  recompute cumulatively and `leadership_current` resets to the new
  `leadership_base`. Rank advancement opens no popup of its own; the King's
  audience (§17.7) reports it on the next visit.

### 8.5 Instant-army count formula

- **REQ-209.** Instant Army (the adventure spell, §19.4) summons
  `class.ranks[rank].instant_army` troops with count `(spell_power + 1) *
  instant_army_multiplier[rank]`, multiplier `[3, 2, 1, 1]` for ranks 0..3
  (minimum 1).

### 8.6 Difficulty and mount

- **REQ-210.** `Difficulty` affects `days_left` (§7.1) and the final score
  multiplier (§26.2) only, not starting gold, army, garrisons, foe spawn
  rates, or chest contents.
- **REQ-211.** The mount has been one of `MOUNT_RIDE` (walking, default),
  `MOUNT_SAIL` (boat), `MOUNT_FLY` (flying). Flying (key `F`) has required
  `GamePlayerCanFly` (every non-empty stack has `TROOP_ABIL_FLY` and
  `skill_level >= 2`); landing (key `L`) has required a grass, non-interactive,
  non-foot-blocking destination tile.

---

## 9. World, zones, and tiles

### 9.1 Zones

- **REQ-220.** The world has consisted of four zones: `continentia`,
  `forestria`, `archipelia`, `saharia` (`game.json:zones[]`), each 64×64 tiles.
- **REQ-221.** Each zone has declared: `id`, display `name`, `map_path`,
  `hero_spawn_x/y`, optional `home_spawn_x/y` + `is_home`, optional
  `magic_alcove_x/y`, `neighbors[]` (zone ids reachable by sailing), a `salt`
  config (§10), and per-feature lists (towns, castles, signs, chests,
  dwellings, armies). Exactly one zone has `is_home: true` (Continentia).

### 9.2 Coordinates

- **REQ-222.** The origin is the top-left of each zone; `x` east, `y` south.
  All gameplay coordinates are integer tile indices; there is no sub-tile
  position.

### 9.3 Terrain

- **REQ-223.** Terrain (`engine/include/tile.h`): `TERRAIN_GRASS`, `_FOREST`,
  `_MOUNTAIN`, `_WATER`, `_DESERT`.
- **REQ-224.** Walkability on foot (`TerrainWalkable`, `engine/tile.c`): grass
  and desert are walkable; forest, mountain, water are not. A grass tile
  flagged `is_bridge` is walkable on foot and traversable in a boat. Stepping
  onto a desert tile sets `steps_left_today = 0` immediately (§7.2).

### 9.4 Interactive overlay

- **REQ-225.** A tile has carried at most one interactive overlay (the
  `Interact` enum, §3.4); `INTERACT_NONE` means none. Castle wall tiles are
  non-interactive but set `blocks_foot = true`.

### 9.5 Tile data

- **REQ-226.** Each `Tile` (`engine/include/map.h`) has stored: `art` (sprite
  filename root), `terrain` (derived from art at load via `TerrainFromArt`),
  `interactive`, optional `id` (named instance, e.g. "kings_castle"),
  `blocks_foot`, `is_bridge`, optional `sign_title` / `sign_body`, and
  `boat_spawn_x/y` (for town tiles).

### 9.6 Map file format

- **REQ-227.** Maps have been plain-text files (`assets/kings-bounty/maps/<zone>.dat`),
  one ASCII byte per tile, row-major. Lines beginning with `#` and blank lines
  are ignored. Each byte is looked up in `Resources.tile_codes[128]` to produce
  an art name plus terrain/blocking flags; short rows are padded with grass.
  The shipped pack uses 54 distinct tile codes (§Appendix A).

### 9.7 Castle and town tile placement

- **REQ-228.** A castle has been stamped as a 3×2 footprint centred on its
  `gate_x/gate_y`: the gate tile (interactive `CASTLE_GATE`, walkable) sits at
  `(x, y)`; the five surrounding tiles are wall pieces (non-interactive,
  `blocks_foot = true`). Castles may declare extra decorative wall pieces (the
  King's castle has 24). A town has been a single `INTERACT_TOWN` tile with a
  `boat_spawn_x/y` used when a boat is rented. When a castle's `gate` object is
  absent, the gate landing tile is computed as `(x, y+1)`.

---

## 10. Salt: per-zone object placement

### 10.1 Salt budget

- **REQ-230.** Each zone has declared a `salt` block: `artifacts`, `navmaps`,
  `orbs`, `telecaves`, `dwellings`, `friendly_foes` (counts), plus
  `preferred_troops[]` and `dwelling_range[lo, hi]` for dwelling troop
  selection. The default per-zone budget: artifacts=2, navmaps=1, orbs=1,
  telecaves=2, dwellings=10, friendly_foes=5.

### 10.2 Algorithm

- **REQ-231.** `salt_continent` (`engine/game.c`) has run once per zone at game
  init. It has: (1) registered every static foe army on the zone as a hostile
  `FoeState`; (2) built a barrel of `chest_count` slots (one per chest
  placeholder), each tagged `SALT_NONE`; (3) for each kind in order (artifacts, navmaps, orbs, telecaves, dwellings, friendly foes) repeatedly
  picked a random unclaimed barrel slot and tagged it, until the quota was met
  or a guard counter (`barrel_len * 20`) was exhausted; (4) walked the barrel
  and emitted a `SaltedPlacement` (or `FoeState`) per tagged slot. When the
  guard is exhausted early, missing items are silently skipped.

### 10.3 Slot semantics

- **REQ-232.** `SALT_ARTIFACT` placed `INTERACT_ARTIFACT` by matching `(zone,
  local_idx)` against the artifact catalog. `SALT_NAVMAP`/`SALT_ORB`/
  `SALT_TELECAVE` placed the corresponding interactive with id `<kind>_<n>`.
  `SALT_DWELLING` picked a troop (zone `preferred_troops[]` first, else
  `dwelling_range`), derived the dwelling kind from the troop's `dwelling`
  field, placed `INTERACT_DWELLING_*`, and registered a pinned `DwellingState`.
  `SALT_FRIENDLY` created a `FoeState` with `friendly = true` and a placeholder
  garrison (re-rolled fresh on accept, §15.5).

### 10.4 Salt of villains

- **REQ-233.** `salt_villains` (`engine/game.c`) has run after
  `salt_continent`. For each villain in catalog order, a retry loop (guarded by
  `min(ncastles * 20, max_attempts)`) picks a random castle in the villain's
  declared `zone` that is not excluded from contracts and not already
  villain-owned; the chosen castle gets `owner_kind = CASTLE_OWNER_VILLAIN`,
  `villain_id`, and a garrison populated from the villain's pre-built army
  (5 stacks).

### 10.5 Salt of spells

- **REQ-234.** `salt_spells` (`engine/game.c`) has assigned exactly one spell
  to each town's `spell_for_sale` in three phases: (1) **pinned**: each town
  with a non-empty `pinned_spell` receives it and the spell is marked claimed;
  (2) **random**: each unclaimed spell is placed in a random spell-less town;
  (3) **fallback**: any still-empty town receives a random spell from the
  fully-claimed pool.

### 10.6 Scepter burial

- **REQ-235.** `bury_scepter` (`engine/game.c`) has chosen one of the four
  zones uniformly at random, loaded its map, counted all tiles whose terrain is
  `TERRAIN_GRASS`, interactive is `INTERACT_NONE`, and `blocks_foot` is false;
  picked the Nth such tile (N uniform in `[0, count-1]`); and stored the tile's
  zone id, x, and y in `Game.scepter`. The scepter is not visible on the map;
  searching (key `S`) on the buried tile triggers the win flow (§26).

---

## 11. Movement, mounts, and travel

### 11.1 Eight-directional movement

- **REQ-240.** Movement has supported four cardinals (arrows, numpad 2/4/6/8)
  and four diagonals (numpad 7/9/1/3 or Home/PgUp/End/PgDn), as single-step
  `(±1, ±1)` deltas. The one-tile step entry point is `step_try`
  (`engine/step.c`); walkability is decided by `engine/adventure.c`.
- **REQ-241.** A blocked move has played the bump sound and left position,
  facing, and travel mode unchanged. A successful move has updated
  `position.x/y`, set `facing_left = (dx < 0)`, fired `FogReveal` at the new
  position with radius `world.fog_sight` (3), and called `GameOnStep`.

### 11.2 Walking, sailing, flying

- **REQ-242.** While walking, walkability follows §9.3. In a boat, the hero
  can enter water, bridge, and grass/desert tiles (the last triggering
  disembark). While flying, every terrain is walkable and interactive tiles do
  not fire.
- **REQ-243.** Stepping from walk mode onto the parked boat sets `travel_mode =
  TRAVEL_BOAT`. Stepping in boat mode from water/bridge onto land parks the
  boat at the previous water tile and sets `travel_mode = TRAVEL_WALK`. Boat-mode
  movement on land is only ever a single disembark step.

### 11.3 Boat rental

- **REQ-244.** A boat has been rented at any town menu (`B`) for `GameBoatCost`
  (`economy.boat_cost_normal = 500`, or `boat_cost_cheap = 100` with the Anchor
  of Admirability) and parked at the town's `boat_x/boat_y`. Rental is
  cancelled at the same menu when the boat is on land; mid-sail cancellation is
  refused. A rented boat costs one weekly rental at each week boundary; on
  bankruptcy it is repossessed.

### 11.4 Sail navigation

- **REQ-245.** In boat mode, key `N` has opened a 1..5 prompt listing up to
  five `neighbors` of the current zone; selecting a digit calls
  `GameSwitchZone` and `GameSpendWeek` (one week passes during the journey).
  Outside boat mode, `N` produces a "must be sailing" dialog and consumes no
  time.

### 11.5 Bounce-back

- **REQ-246.** When the hero has stepped onto a tile that opens a bouncing
  interactive flow (towns, castles, dwellings, alcove, hostile foes) and the
  player dismisses it without committing, position/travel-mode/boat coords
  revert to the pre-step values. Non-bouncing interactives (chest, navmap, orb,
  telecave, sign, friendly-foe accept, artifact) leave the hero on the
  destination tile.

---

## 12. Adventure-mode actions

### 12.1 Action key bindings

- **REQ-250.** Adventure-mode keys (one action per press; no autorepeat within
  a turn), dispatched through `src/shell_actions.c` / `src/input.c`:
  `A` view army, `C` view controls, `D` dismiss army, `F` fly, `I` view
  contract, `L` land, `M` worldmap, `N` navigate (sail), `O` options, `P` view
  puzzle, `Q` save-and-quit, `Ctrl+Q` fast quit, `S` search, `U` cast spell,
  `V` view character, `W` end week, `Numpad 5` rest one day, `F10` cheat menu,
  `Esc` close overlay, `Tab` game menu.

### 12.2 Gamepad mapping

- **REQ-251.** D-pad / left stick → 8-direction movement; A → search; X → cast
  spell; Y → end week; LB → army; RB → character; LT → fly; RT → land; Start →
  worldmap; Back → options; B → cancel.

### 12.3 Search / dismiss / end week

- **REQ-252.** `S` has opened a yes/no "It will take 10 days to do a search…"
  prompt (`tuning.search_cost_days = 10`); Yes compares zone/x/y to
  `Game.scepter`, on match, the win flow fires; otherwise `GameSpendDays(10)`
  runs and the "revealed nothing" banner shows.
- **REQ-253.** `D` has opened a numeric (1..5) prompt of non-empty slots.
  Choosing any but the last zeros that stack and runs `GameCompactArmy`.
  Choosing the last remaining stack chains into a yes/no prompt; Yes runs temp
  death (clear army, zero siege weapons, grant 20 peasants, teleport home,
  dismount, drop boat).
- **REQ-254.** `W` has called `GameSpendWeek` and scheduled the post-week
  dialog sequence (§24.5); running out of days fires the lose flow.

---

## 13. Troops and the army

### 13.1 Catalog

- **REQ-260.** The troop catalog has held exactly 25 entries, indexed 0..24
  (`game.json:troops[]`; `TroopDef` in `engine/include/tables.h`). The full
  catalog appears in §Appendix A. Each troop carries: `skill_level`,
  `hit_points`, `move_rate`, `recruit_cost`, `spoils`, an ability mask,
  `dwelling` kind, `max_population`, `growth_per_week`, a morale group A..E,
  and a `tier_counts[4]` row (foe-garrison stack sizes per continent tier, used
  by `roll_creature`, §15.4).

### 13.2 Ability flags

- **REQ-261.** The 8-bit ability mask (§3.5): `FLY` (flies over impassable
  terrain; required for hero flight), `REGEN` (regenerates HP between rounds),
  `MAGIC` (casts combat spells / fixed ranged), `IMMUNE` (immune to magic
  damage), `ABSORB` (converted to peasants on a Peasants astrology week, and
  grows on kills in combat), `LEECH` (heals from melee dealt), `SCYTHE`
  (special doubled-damage rule), `UNDEAD` (eligible for Turn Undead, immune to
  morale).

### 13.3 Army stacks

- **REQ-262.** The player army has had five slots (`GAME_ARMY_SLOTS = 5`); each
  stack holds a troop id and count. Adding a troop matching an existing stack
  increments it; otherwise the first empty slot is used; with no empty slot and
  no match, the add fails and the source flow shows a banner.
  `GameCompactArmy` keeps non-empty stacks contiguous, preserving order.

### 13.4 Recruitment cap and upkeep

- **REQ-263.** `GameMaxRecruitable(troop_id)`: `same_troop_consumed =
  sum(stack.count * troop.hp)` over stacks of that troop; `free_leadership =
  leadership_current - same_troop_consumed`; result is `free_leadership /
  troop.hp` (0 / "n/a" when free leadership is negative). Buying costs
  `recruit_cost * count`; the gold check uses strict `<` (insufficient when
  `gold < cost`).
- **REQ-264.** At each week boundary, each non-empty stack pays `upkeep =
  count * (recruit_cost / 10)` gold (integer division), deducted after
  commission is credited.

---

## 14. Morale and out-of-control

### 14.1 Morale groups and chart

- **REQ-270.** Each troop belongs to one of five groups A..E (§Appendix A). The
  5×5 morale chart (`game.json:combat.morale_chart`):

  ```
           A   B   C   D   E
      A    N   N   N   N   N
      B    N   N   N   N   N
      C    N   N   H   N   N
      D    L   N   L   H   N
      E    L   L   L   N   N
  ```

  Row = the stack whose morale is computed; column = another stack present.
- **REQ-271.** Per-stack morale: a single non-empty stack is High; otherwise
  each other non-empty stack is consulted on the chart and results counted, any `L` → Low; all `H` (≥1) → High; else Normal.

### 14.2 Out-of-control

- **REQ-272.** A stack is out of control (OOC) when `(troop.hp * stack.count) >
  leadership_current`. In the army view this shows as "Out of Control" in red
  (mutually exclusive with the Low/Normal/High labels). In combat an OOC unit
  still takes turns but attacks its own side (§25.4).

---

## 15. Foes, encounters, and recruiting

### 15.1 Foe state and sources

- **REQ-280.** A `FoeState` (§4.3) holds zone, `(x,y)`, `placement_id`,
  `alive`, `friendly`, and a 5-stack garrison. Up to 160 foes are tracked
  (`GAME_MAX_FOES` = 4 continents × 40), and static zone armies and salt-placed
  friendly foes share the one flat array. Because the array is shared and salted
  in zone order, the cap must cover every continent's allocation at once — a
  smaller cap let the early continents exhaust the table and left later ones
  (Archipelia, Saharia) with no foes. To keep OpenKB's per-continent 40-slot
  split (`foe_coords[4][40]`), `salt_continent` bounds each continent to
  `GAME_MAX_HOSTILE_PER_ZONE` (35) hostiles plus its `friendly_foes` (5)
  friendlies. Static hostile foes come from `zones[].armies[]` with garrisons
  pre-rolled at salt time (`roll_hostile_garrison`); friendly foes are
  salt-placed with placeholder garrisons re-rolled on join (§15.5).

### 15.2 Spawn pool

- **REQ-281.** `tier_chance_curve[4][4]` (`game.json:spawn.tier_chance_curve`):

  ```
  Tier 0 (Continentia): [60, 90, 98, 101]
  Tier 1 (Forestria):   [20, 70, 95, 99]
  Tier 2 (Archipelia):  [10, 20, 50, 90]
  Tier 3 (Saharia):     [3,  6,  10, 40]
  ```

- **REQ-282.** `tier_troop_pool[4][5]` (`game.json:spawn.tier_troop_pool`):

  ```
  Kind 0 (plains):  peasants, wolves, nomads, barbarians, archmages
  Kind 1 (forest):  sprites, gnomes, elves, trolls, druids
  Kind 2 (hill):    orcs, dwarves, ogres, giants, dragons
  Kind 3 (dungeon): skeletons, zombies, ghosts, vampires, demons
  ```

### 15.3 Roll

- **REQ-283.** `roll_creature(continent_tier)` (`engine/game.c`): `kind =
  rng(0,3)`; `chance = rng(1,100)`; walk `chance_curve[tier]` for the smallest
  slot where `chance <= curve[slot]` (else slot 4); `troop_id =
  troop_pool[kind][slot]`; `count = troop.tier_counts[tier] + rng(0,
  tier_counts[tier]/2)`, clamped to ≥ 2. `roll_hostile_garrison` fills 1..3
  stacks (`rng(0,2)+1`) via `roll_creature`.

### 15.4 Encounter flows

- **REQ-284.** Stepping onto a hostile foe shows the garrison banner and a
  yes/no fight prompt: Yes enters combat, No bounces back. Stepping onto a
  friendly foe re-rolls a fresh troop offer; Yes with a free slot runs
  `GameAddTroop` (no gold cost) and consumes the foe; Yes with no slot, or No,
  consumes the foe with the "flee in terror" banner.

### 15.5 Hero-on-foe

- **REQ-285.** Hostile (and friendly) foes proactively walk toward the hero
  each turn via `GameFoesFollow` (`engine/game.c`): for each foe within
  Chebyshev distance 2 of the hero's *previous* position, all 9 cells of the
  foe's 3×3 neighborhood are scored by Euclidean distance to that position;
  unwalkable/occupied non-center cells get a sentinel max score; the hero's
  current tile is **not** excluded (landing on it is the combat trigger). The
  foe moves to the lowest-score cell; if that is the hero's tile,
  `GameFoesFollow` returns the foe index so the caller fires the
  attack/recruit flow. Foe motion does not consume the hero's day budget and
  stops at impassable terrain.

---

## 16. Towns

- **REQ-290.** The town catalog has held 26 towns (`game.json:towns[]`), one
  per letter A..Z. Each town carries id, name, zone, `(x,y)`, gate coords, boat
  coords, an intel castle, and an optional pinned spell (full table in
  §Appendix A). Each `TownRecord` tracks `visited` and `spell_for_sale`.
- **REQ-291.** The town view (`src/screens/` / `src/views_render.c`) has shown
  `Town of <NAME>` + `GP=<gold>K` and five rows:
  - **A) Get New Contract**: `GameTakeNextContract`; shows the villain id,
    reward, and last-known zone, or "no contracts" when none remain.
  - **B) Rent / Cancel boat**: toggles `boat.has_boat` at `GameBoatCost`;
    refuses mid-sail; strict `<` gold check.
  - **C) Gather information**: intel on the town's `intel_castle`: name,
    owner, and garrison list with `GameNumberName` bucket labels; unavailable
    when the castle is excluded from intel.
  - **D) `<Spell>` spell (`<cost>`)**: buys `spell_for_sale` if
    `GameKnownSpells < max_spells` and `gold > cost` (strict, OpenKB-faithful);
    deducts gold, increments the spell count.
  - **E) Buy siege weapons (3000) / owned**: sets `siege_weapons = 1`; the
    flag is not enforced anywhere in combat (§38).
  Every action is at-most-once per visit; the dialog persists until `Esc`.

---

## 17. Castles

### 17.1 Catalog

- **REQ-300.** The castle catalog has held **27** castles: 26 villain/monster
  castles (A..Z) plus King Maximus's castle (full table + difficulty tiers in
  §Appendix A). Each `CastleRecord` tracks `visited`, `known`, `owner_kind`,
  `villain_id` (when applicable), and a 5-stack garrison.

### 17.2 Owner kinds

- **REQ-301.** `CASTLE_OWNER_VILLAIN`, populated by `salt_villains`; garrison
  is the villain's pre-built army. `CASTLE_OWNER_MONSTERS`, populated by
  `repopulate_castle` using the castle's `difficulty_tier` (5×
  `roll_creature`). `CASTLE_OWNER_PLAYER`, after capture; supports
  garrison/ungarrison via the Own Castle view. `CASTLE_OWNER_SPECIAL`, only
  the King's castle; never siegeable, never repopulated.

### 17.3 Repopulation

- **REQ-302.** At game init, every monster castle is seeded by
  `repopulate_castle`. At each week boundary, each player-owned castle whose
  garrison is *completely* empty is auto-repopulated by the same call; a
  partially-populated player castle is preserved. Astrology growth (§24.3)
  applies weekly to non-player castle stacks matching the astrology creature.

### 17.4 Visit / siege / own / audience

- **REQ-303.** Stepping onto a castle gate: player-owned → **Own Castle** view;
  monsters → siege-monster flow; villain → siege-villain flow; the King's
  castle → audience. A siege flow shows a yes/no prompt with the garrison; Yes
  enters combat, No bounces back. Combat win sets `owner_kind =
  CASTLE_OWNER_PLAYER`; a villain-castle win additionally fulfils the contract
  (§21.3).
- **REQ-304.** **Own Castle** (`src/screens/own_castle.c`) shows the 5-slot
  garrison with letter selectors A..E plus Space to toggle direction (army ↔
  garrison). Each transfer merges into a matching slot or fills the first empty
  one; the player army is never allowed to become entirely empty.
- **REQ-305.** **King's audience** (`src/shell_audience.c`,
  `src/screens/home_castle.c`) shows the intro, a rank-up report when the next
  rank's `villains_needed` is met, a "capture N more villains" message
  otherwise, and a final-rank "recover my Scepter" line. The King's castle
  doubles as the **Home Castle** view, which also offers (`A`) Recruit Soldiers
  for castle-class troops (`src/screens/recruit_soldiers.c`).

---

## 18. Dwellings

- **REQ-310.** Dwelling kinds: `plains`, `forest`, `hills` (singular `hill` in
  the troop catalog), `dungeon`; each `TroopDef` names exactly one via its
  `dwelling` field. Castle-kind troops (militia, archers, pikemen, knights,
  cavalry) have `max_population = 0` and are recruitable only at the home
  castle's recruit screen.
- **REQ-311.** Each `DwellingState` tracks `(zone,x,y)`, `troop_id`,
  `max_population`, `count` (cap 64, `GAME_MAX_DWELLINGS`). A dwelling is
  created lazily on first visit if not salt-placed; the troop is picked
  deterministically by `(seed, x, y)` (`GameDwellingTroopAt`); initial `count`
  is the troop's `max_population`. Salt-placed dwellings are pinned to their
  troop.
- **REQ-312.** Visiting a dwelling bounces the hero and opens the dwelling
  screen (`src/screens/dwelling.c`) showing kind, count, cost per unit, gold,
  and the `recruit_cost * count <= gold && count <= GameMaxRecruitable` clamp.
  Yes + a numeric input runs `GameBuyTroop` (deduct gold, add to army,
  decrement dwelling count).
- **REQ-313.** **Astrology growth** (§24.2): only dwellings whose `troop_id`
  matches the astrology creature refill to `max_population`; non-matching
  dwellings keep their current count.
- **REQ-314.** **Alcove** (Aurange): each zone declares one `magic_alcove_x/y`
  (`INTERACT_ALCOVE`). When the class sets `knows_magic` at start (Sorceress),
  every alcove is marked consumed at init. Visiting an unconsumed alcove
  (`src/screens/alcove.c`) bounces the hero and offers "Visit Archmage
  Aurange's chamber? cost = `economy.alcove_cost = 5000`g"; Yes + sufficient
  gold (strict `<`) sets `knows_magic`, deducts 5000, consumes the tile, and
  adds a permanent leadership bonus.

---

## 19. Spells: catalog and adventure casts

### 19.1 Catalog

- **REQ-320.** The spell catalog has held 14 spells (`game.json:spells[]`),
  indexed 0..13: combat (0..6), clone(2000), teleport(500), fireball(1500),
  lightning(500), freeze(300), resurrect(5000), turn_undead(2000); adventure
  (7..13), bridge(100), time_stop(200), find_villain(1000), castle_gate(1000),
  town_gate(500), instant_army(1000), raise_control(500). (Full table in
  §Appendix A.)

### 19.2 Counts, storage, buying

- **REQ-321.** `Game.spells.counts[14]` holds one count per spell.
  `GameKnownSpells` sums them; `max_spells` caps purchases; casting decrements,
  buying increments. Spells are bought at the town menu (§16) for the spell's
  `cost`. Buying does not require `knows_magic` (OpenKB-faithful); casting does.
  The gold check is strict `<`.

### 19.3 Adventure spell effects

- **REQ-322.** Adventure spells are implemented in
  `engine/spells_adventure.c` (dispatched via `dispatch_adventure_spell`),
  with `GameCastTimeStop` / `GameCastFindVillain` exposed directly on
  `engine/include/game.h`. Modal continuations route through
  `engine/pending.h`.
  - **Bridge** (100): on cast, awaits a direction key; then walks up to 5 water
    tiles in `(dx,dy)`, converting up to 2 consecutive water tiles to
    `is_bridge` grass with the appropriate art. The count decrements only on
    success.
  - **Time Stop** (200): adds `max(spell_power * 10, 10)` to `time_stop`. Steps
    in the time-stop window do not advance the day.
  - **Find Villain** (1000): scans castles for the active contract's villain
    and sets that castle `known = true`. With no active contract, no effect and
    no decrement.
  - **Castle Gate** (1000) / **Town Gate** (500): await a letter A..Z, match on
    the destination's first letter; a visited castle/town teleports the hero
    (via `GameSwitchZone` + setting position). Town Gate's default landing is
    `(town.boat_x, boat_y)` when no gate is declared.
  - **Instant Army** (1000): resolves the troop via
    `class.ranks[rank].instant_army` and count `(spell_power + 1) *
    [3,2,1,1][rank]` (§8.5), added via `GameAddTroop` (free). No empty slot →
    no-room banner, no consume.
  - **Raise Control** (500): adds `spell_power * 100` to `leadership_current`.

### 19.4 Casting gate

- **REQ-323.** `U` in adventure mode with `knows_magic` opens `VIEW_SPELLS`
  (combat spells 0..6 left column, adventure 7..13 right); without it, a "no
  magic" dialog hints at the alcove. Only adventure spells are castable from
  adventure mode; combat spells fire from the in-combat menu (§25.8).

---

## 20. Artifacts

### 20.1 Catalog and placement

- **REQ-330.** The artifact catalog has held 8 artifacts
  (`game.json:artifacts[]`; full table in §Appendix A). Each zone holds two
  (`local_idx` 0 and 1); `salt_continent` places artifact tiles by matching
  `(zone, local_idx)`.

### 20.2 Pickup and powers

- **REQ-331.** Stepping onto an artifact tile calls `GameClaimArtifact(idx)`:
  sets `artifacts.found[idx]`, applies the instant power, consumes the tile,
  and shows the banner.
- **REQ-332.** Pickup-time (instant) effects: `DOUBLE_LEADERSHIP` (Crown), `leadership_base *= 2; leadership_current = leadership_base`;
  `INCREASE_COMMISSION` (Articles), `commission_weekly += 2000`;
  `DOUBLE_SPELL_POWER` (Amulet), `spell_power *= 2`; `DOUBLE_MAX_SPELLS`
  (Ring), `max_spells *= 2`.
- **REQ-333.** Live-checked (query-time) effects via `GameHasPower`:
  `INCREASED_DAMAGE` (Sword), combat damage +50%; `QUARTER_PROTECTION`
  (Shield) (combat damage taken ×0.75; `CHEAPER_BOATS` (Anchor)) boat rental
  100 instead of 500; `UNKNOWN` (Book of Necros), no effect, faithful to the
  unimplemented original.

---

## 21. Villains and contracts

### 21.1 Catalog

- **REQ-340.** The villain catalog has held 17 villains
  (`game.json:villains[]`; full table + descriptions in §Appendix A). Per-zone
  counts: `[6, 4, 4, 3]` (Continentia, Forestria, Archipelia, Saharia). Each
  villain has a fixed five-stack army copied verbatim into its host castle at
  salt time.

### 21.2 Contract cycle

- **REQ-341.** `Contract` holds `active_id`, `cycle[5]` (rotating villain ids),
  `last_contract` (index 0..4 of the most recently issued slot), `max_contract`
  (next villain to rotate in), and `villains_caught[17]`. At init, `cycle` is
  seeded with the first five villain ids, `last_contract = 4`, `max_contract =
  5`. `GameTakeNextContract` increments `last_contract` (wrapping 0..4) and
  copies `cycle[last_contract]` to `active_id`.

### 21.3 Fulfilment

- **REQ-342.** A combat win on a villain castle calls
  `GameFulfillContract(villain_id)`: credits `gold += villain.reward`, sets
  the caught flag, clears `active_id`, and rotates the next uncaught villain
  (from `max_contract`) into `cycle[last_contract]`, incrementing
  `max_contract`. Capture also sets the castle's `owner_kind =
  CASTLE_OWNER_PLAYER` and triggers the rank-up check (§8.4). The Find Villain
  spell marks the active contract's castle `known = true` (§19.3).

---

## 22. Chests and rewards

### 22.1 Chest table and roll

- **REQ-350.** `economy.chest` holds chance/value tables indexed by zone
  (0..3):

  ```
  chance_gold        = [61, 66, 76, 71]
  chance_commission  = [81, 86, 86, 81]
  chance_spell_power = [86, 92, 93, 91]
  chance_max_spells  = [86, 92, 93, 91]
  chance_new_spell   = [101, 101, 101, 101]
  gold_min           = [0, 4, 9, 19]
  gold_max           = [5, 16, 21, 31]
  commission_min     = [9, 49, 99, 199]
  commission_max     = [41, 51, 101, 301]
  max_spells_base    = [1, 1, 2, 2]
  ```

- **REQ-351.** `GameRollChest(zone, x, y)` (`engine/game.c`) rolls `chance =
  (chest_rand(g,x,y,1) % 100) + 1` and walks cumulative thresholds in order (gold → commission → spell_power → max_spells → new_spell → empty) taking the
  first whose threshold exceeds `chance`. Because `chance_spell_power ==
  chance_max_spells` element-wise, the **max_spells** outcome is unreachable in
  the shipped tables (OpenKB-faithful collision, preserved).
- **REQ-352.** `GamePeekChest` is a read-only equivalent (no mutation) used by
  the autoplay planner (§36) to pre-plan chest choices.

### 22.2 Outcomes

- **REQ-353.** **Gold**: `points ∈ [gold_min, gold_max]`; `gold = points *
  100`; an A/B prompt offers take-gold vs distribute (`+leadership = gold/50`,
  doubled with `DOUBLE_LEADERSHIP`). Resolved via `GameAcceptChestGold` /
  `GameAcceptChestLeadership`. **Commission**: `commission_weekly += points ∈
  [commission_min, commission_max]`. **Spell power**: `+1`. **Max spells**
  (unreachable): `+= max_spells_base[zi]`, doubled by `DOUBLE_MAX_SPELLS`.
  **New spell**: a random spell, count `(chest_rand % (zi+1)) + 1`. **Empty**:
  no change. Every outcome consumes the chest tile (adds to `consumed[]`).

### 22.3 Other chest types

- **REQ-354.** **Navmap chest** reveals the next undiscovered zone
  (`world.zones_discovered[zi+1] = true`) and consumes. **Orb chest** reveals
  the entire current zone's fog (`world.orbs_found[zi] = true`); the worldmap
  then supports a Space-toggle between fog-gated and fully-revealed views.
  **Telecave**: tiles paired by index within a zone (0↔1, 2↔3); stepping on one
  teleports to its pair (an odd telecave is a one-way dead-end). **Signpost**:
  each sign tile carries per-tile `sign_title` / `sign_body` shown in a dialog;
  there is no global sign index.

---

## 23. Economy: gold, commission, upkeep

### 23.1 Sources and sinks

- **REQ-360.** Gold sources: starting gold (`class.starting_gold`), weekly
  commission (`gold += commission_weekly`), chest gold, villain reward. Gold
  sinks: troop recruitment (`recruit_cost * count`), boat rental (per week),
  spell purchase (`cost`), siege weapons (3000, one-time), alcove (5000,
  one-time).

### 23.2 Week-end ordering

- **REQ-361.** End-of-week processing has run in this order (`engine/game.c`
  day/week rollover): (1) `time_stop = 0`; (2) `leadership_current =
  leadership_base`; (3) `astrology = GamePickAstrologyCreature(week_id)`;
  (4) `gold += commission_weekly`, `last_commission = commission_weekly`;
  (5) `gold -= sum(stack.count * (recruit_cost / 10))`; (6) if `boat.has_boat`,
  `gold -= GameBoatCost`, repossessing the boat on shortfall; (7) `gold =
  max(0, gold)`; (8) astrology effects (§24).

---

## 24. Astrology and weekly events

### 24.1 Pick

- **REQ-370.** `GamePickAstrologyCreature(week_id)` (`engine/game.c`) returns
  troop 0 (peasants) when `(week_id & 3) == 0` (every 4th week from week 0);
  otherwise `1 + ((seed XOR week_id) * 1664525 + 1013904223) mod (troops_count
  - 1)` (deterministic, so a reload shows the same creature).

### 24.2 Effects

- **REQ-371.** `GameApplyAstrology(troop_idx)`: dwellings whose `troop_id`
  matches refill to `max_population` (non-matching keep their count). For each
  non-player castle and each hostile foe, every stack matching the astrology
  creature grows by `troop.growth_per_week`. When the week is Peasants, every
  player-army stack with `TROOP_ABIL_ABSORB` (ghosts) has its id rewritten to
  `peasants` (count preserved). When no astrology event applies,
  `GameGrowDwellings` grows all dwellings by their `growth_per_week` instead
  (capped at `max_population`).

### 24.3 Week-end dialog

- **REQ-372.** After processing, a two-phase dialog sequence is queued
  (`src/shell_weekend.c`): **Phase 1 (Astrology)** shows the new week's
  creature; **Phase 2 (Budget)** shows gold on hand, commission paid, boat cost
  (if any), per-stack upkeep, and the final balance. Dismissed by any key.

---

## 25. Combat

Combat has been **fully implemented** (engine state machine + rendered shell
loop + golden-digest regression tests). The engine half lives in
`engine/combat.c` (state, AI, headless turn loop, damage formula, combat
spells); the rendered loop (`RunCombat`, modal input, target picker, per-frame
present) lives in `src/combat_loop.c`; the battlefield renderer is
`src/combat_render.c`. Both halves share the `Combat` struct in
`engine/include/combat.h`.

### 25.1 Arena

- **REQ-380.** The grid has been 6 columns × 5 rows (`COMBAT_W = 6`,
  `COMBAT_H = 5`), each cell 48×34 design pixels holding at most one unit. Each
  side has up to 5 slots (`COMBAT_SLOTS = 5`); player = side 0
  (`COMBAT_SIDE_PLAYER`), AI = side 1. Two modes: **field**
  (`COMBAT_MODE_FOE`, open field, scattered obstacles) and **castle**
  (`COMBAT_MODE_CASTLE`, siege layout with walls). The player starts one stack
  per row (slot i → column 0, row i). The obstacle map (`omap`) uses codes
  1..3 for field obstacles and 5..10 for castle walls (0 = open); the unit map
  (`umap`) holds packed unit ids (1-based; 0 = empty). Both maps are sized
  `[H+1][W+1]` for off-by-one guards.

### 25.2 Combat state

- **REQ-381.** The `Combat` struct holds `units[2][5]`, `omap`, `umap`,
  `spoils[2]`, `powers[2]` (per-side artifact bits), `heroes[2]` (`g` for
  player, `NULL` for AI), `turn`, `phase`, `spells_this_round`, `side`,
  `unit_id`, `castle` (siege flag), `first_kill_seen`, `stacks_destroyed`,
  `rng_state`, `banner[80]`, `log_lines[8][80]`, `log_count`, `cursor_x/y`,
  `target_filter`, `picker_active`, `cursor_frame`, a pick+cast state machine
  (`cast_phase`, `cast_spell_idx`, `pick_reason`, `pick_filter`, first-target
  and destination scratch), `result` (0 running / 1 player win / 2 AI win),
  `villain_id`, `mode`, `target_name`.
- **REQ-382.** A `CombatUnit` holds `troop_idx` (-1 = empty), `count`,
  `turn_count` (snapshot at turn start, for the damage formula), `max_count`
  (snapshot at combat start, the ABSORB/LEECH cap), `dead`, `frame`, `injury`
  (sub-HP residual), `acted`, `retaliated`, `moves`, `shots`, `flights`,
  `frozen`, `out_of_control`, `(x,y)`, `hit_flash`.

### 25.3 Turn order

- **REQ-383.** The player side acts first each round; within a side, lowest
  slot index first. `combat_next_unit` scans `[unit_id+1..4]` for the first
  stack with `count > 0 && !acted`, then wraps via `phase++` to scan
  `[0..unit_id]`. `combat_reset_turn` resets `acted`/`retaliated`, refreshes
  `moves = move_rate` and `flights = 2` for FLY units, and (REGEN/Trolls)
  resets `injury = 0` at the start of the player side. A `frozen` unit skips
  its turn (`acted = true`), then has `frozen` cleared by the next reset.

### 25.4 Move and fly

- **REQ-384.** `combat_move_unit` moves one tile: out-of-bounds or obstacle →
  blocked; friendly under-control unit in target → blocked; hostile (or
  own-side OOC) unit in target → melee attack (`acted = true`); empty cell →
  relocate, `moves -= 1`, `acted` when `moves == 0`. `combat_fly_unit` ignores
  obstacles/units and lands only in empty cells; `flights -= 1`, `acted` when
  `flights == 0`. OOC units keep taking turns but attack their own side.

### 25.5 Damage formula

- **REQ-385.** `combat_deal_damage` (`engine/combat.c`) computes final damage:
  1. **External path** (spell): `final_damage = external_damage`.
  2. **Internal path**: `dmg = is_ranged ? (MAGIC ? ranged_min :
     rand(ranged_min, ranged_max)) : rand(melee_min, melee_max)`; `total = dmg
     * turn_count`; `skill_diff = attacker.skill + 5 - target.skill`;
     `final_damage = (total * skill_diff) / 10`. **SCYTHE** (Demon): 10% chance
     adds `target.hp * ceil(target.count / 2)` after the morale/artifact passes.
  3. **Morale** (attacker has hero and is in control): Low → `/2`; High → `×1.5`;
     Normal → unchanged.
  4. **Artifact attacker** `INCREASED_DAMAGE` → `×1.5`.
  5. **Artifact target** `QUARTER_PROTECTION` → `×0.75`.
  6. Accumulate `+= target.injury`; add the SCYTHE bonus.
  7. `kills = final_damage / target.hp`; `injury = final_damage % target.hp`;
     stack outcome updates count or marks dead.
- **REQ-386.** A MAGIC ranged attack against an `IMMUNE` defender returns -1
  (fizzle, no state change); non-MAGIC attacks ignore IMMUNE. Ranged attacks
  decrement `shots` regardless of outcome. When the target side has the hero,
  `kills` are added to `g->stats.followers_killed`.

### 25.6 Retaliation

- **REQ-387.** Retaliation fires iff: not external; not already a retaliation
  pass; the defender has not retaliated this round; the defender survived; the
  attack was melee (ranged sets `retaliated` early to skip). It is a recursive
  `combat_deal_damage` from defender to attacker with `retaliation = true`; the
  flag is set first to prevent infinite recursion, and reset at the unit's next
  turn.

### 25.7 Special-ability post-effects

- **REQ-388.** **ABSORB** (Ghosts): `count += kills`. **LEECH** (Vampires):
  `count += final_damage / hp`, clamped to `max_count`. **REGEN** (Trolls):
  `injury = 0` at the start of the player side. **MAGIC** (Druids, Archmages):
  ranged uses fixed `ranged_min`, cancelled by IMMUNE. **IMMUNE** (Dragons):
  blocks MAGIC attacks and Fireball/Lightning/Freeze/Turn Undead. **SCYTHE**
  (Demons): see REQ-385. **UNDEAD**: only Turn Undead targets them; immune to
  morale.

### 25.8 Combat spells

- **REQ-389.** One spell per round (`spells_this_round < 1`); requires the
  hero's class+rank `knows_magic`. SP comes from `g->stats.spell_power` (the
  Amulet doubles it at pickup).
  - **Clone** (2000): `clones = (sp*10 + injury) / hp`; `count += clones`,
    `max_count += clones`.
  - **Teleport** (500): two-pick workflow (unit, then empty destination).
  - **Fireball** (1500): external `25 * sp`; blocked by IMMUNE.
  - **Lightning** (500): external `10 * sp`; blocked by IMMUNE.
  - **Freeze** (300): sets `frozen`; blocked by IMMUNE.
  - **Resurrect** (5000): `revived = max(1, sp)`, clamped so `count + revived <=
    max_count`.
  - **Turn Undead** (2000): external `50 * sp` vs UNDEAD only; blocked by
    IMMUNE.
  The target picker (`combat_cell_passes_filter`, shell-side
  `combat_pick_target`) supports filters `ANY`, `EMPTY`, `ANY_UNIT`,
  `FRIENDLY`, `ENEMY`, `UNDEAD`.

### 25.9 Player input

- **REQ-390.** Movement via arrows / numpad 8/4/6/2 (cardinals) and
  Home/PgUp/End/PgDn or numpad 7/9/1/3 (diagonals); numpad 5 = wait. `S` shoots
  (rejected when `shots == 0` or surrounded). `U` opens the spell menu (A..G
  for spells 0..6). `G` (give up) and `Esc` set `result = 2`. `C` shows the
  controls overlay (modal, does not consume the turn).

### 25.10 AI behaviour

- **REQ-391.** `combat_ai_action` (`engine/combat.c`) selects, in priority:
  frozen → skip; close (1-tile) target → melee; ranged when shots > 0 and not
  surrounded → far target; fly when FLY and flights > 0 → land adjacent to a
  far target; walk toward closest/far target; else pass. `ai_pick_target`
  scores far ranged enemies = 10000, others = `1000 - hp` (lower-HP preferred);
  OOC attackers treat their own side as targetable. Movement tie-breaks iterate
  `dy ∈ {+1,0,-1}` outer, `dx ∈ {-1,0,+1}` inner.

### 25.11 Log, banner, result, spoils

- **REQ-392.** Up to 8 log lines (`COMBAT_LOG_LINES`) ring-buffer from
  `game.json:combat_log` templates (`melee_hit`, `retaliate`, `ranged_hit`,
  `frozen`, `immune`, `cloned`, `resurrected`, `teleported`, etc.). The banner
  shows the actor's name + M/S counters before the first kill, then "<actor>
  vs <target> killing N".
- **REQ-393.** **Win** (`result = 1`): all defenders dead; spoils =
  `sum(troop.spoils * 5 * count)` over killed enemies credited to gold;
  survivors written back to `g->army` with `GameCompactArmy`. **Loss / flee**
  (`result = 2`): all attackers dead, or Esc/G; triggers temp death (clear
  army, grant 20 peasants, dismount, drop boat, teleport home).

### 25.12 Siege weapons

- **REQ-394.** The `siege_weapons` flag is purchased and persisted but is not
  checked anywhere in combat or the siege flow (OpenKB-faithful, §38).

### 25.13 Combat RNG

- **REQ-395.** Combat uses an independent LCG (`Combat.rng_state`) seeded by
  `combat_seed_rng` as a pure function of (world seed, the encounter's stable
  identity (a foe's `placement_id` or castle id) and the combat mode), so a
  fight's RNG does not depend on casualty history and the autoplay planner's
  prediction matches the live outcome. `combat_rand(c, min, max)` advances
  `state = state * 25214903917 + 11` and returns `min + (state >> 32) % (max -
  min + 1)`.

### 25.14 Headless loop and test harness

- **REQ-396.** `combat_run_headless(g, mode, target, cap_rounds)` drives both
  sides with `combat_ai_action` (no raylib, no input, no animation), mirroring
  `RunCombat`'s setup + writeback. `combat_test_digest(seed, attacker, count_a,
  defender, count_b, rounds)` runs a deterministic two-stack fight and returns a
  64-bit digest; the 25 golden cases in
  `tests/regression/test_combat_digests.c` run under `make test` to catch
  formula regressions. `combat_run_headless` / `combat_test_digest` are also
  exercised by `tests/unit/test_combat_ai.c`.

### 25.15 Render

- **REQ-397.** `src/combat_render.c` draws the arena at `(CL_COMBAT_X = 16,
  CL_COMBAT_Y = 22)`, size 288×170, cell 48×34. Render order: grass field,
  obstacles, units (with count badge), damage-flash overlay, picker cursor
  (when `picker_active`), then chrome and title bar. Only the active unit
  animates (frames 0..3 at ~150 ms); on frame wrap the AI takes its action.
  While `RunCombat` is active, `combat_current_rendered` points at its local
  `Combat` so gameplay-test scenarios can introspect state via the frame-host
  callback.

---

## 26. Scoring, victory, and defeat

### 26.1 Score

- **REQ-400.** `GameComputeScore` (`engine/game.c`): `score = 500 *
  villains_caught + 250 * artifacts_found + 100 * castles_owned -
  followers_killed`. When difficulty is Easy and `score.easy_halves` is true,
  the score is halved; otherwise it is multiplied by
  `score.difficulty_multiplier` `[1, 2, 4, 8]` for Normal/Hard/Impossible
  (the 5-slot table reserves a ×8 for an unused tier). Clamped at ≥ 0. The
  score is recomputed at every state mutation and stored in `Game.stats.score`.

### 26.2 Win and lose

- **REQ-401.** The **win** fires when the player searches (key `S`) on the
  buried scepter tile (matching zone, x, y), running `show_win_game`
  (`engine/flows.c`). The **lose** fires when `days_left` reaches 0 (at the
  next day rollover), running `show_lose_game`.
- **REQ-402.** Both end-game screens (`src/screens/end_game.c`,
  `engine/include/end_screen.h`) show a left half (DBLUE background) with
  header/body/footer from `strings.win` or `strings.lose`, and a right half
  with an end-game cartoon (`src/end_cartoon.c`; animated throne approach for
  win, lose-art for loss). Substitution tokens: `%NAME%`, `%RANK%`, `%SCORE%`.
  Render-side concerns (the win cartoon) are invoked by the host before
  `show_win_game`; the flow itself is render-free.

---

# Part III, Engine, save, UI, platform

## 27. Save format and slots

### 27.1 Format

- **REQ-410.** Saves have been **JSON, version 8** (`SAVE_VERSION` in
  `engine/include/savegame.h`). Catalog references use string ids
  (`troop_id: "knights"`), so saves are human-readable and pack-portable.
  Read/write is `engine/savegame.c`; the full-state snapshot builder is
  `engine/state_serialize.c`.
- **REQ-411.** Save slots: 10 (`SAVE_SLOT_COUNT` in
  `engine/include/savepath.h`). Filenames: `save_0.dat` … `save_9.dat` under
  the platform save directory.
- **REQ-412.** Save directory (`engine/savepath.c`):
  - **Linux**: `$XDG_DATA_HOME/openbounty/` if set, else
    `~/.local/share/openbounty/`. Created on first save.
  - **Windows**: `%APPDATA%\OpenBounty\`.
- **REQ-413.** Fog of war is encoded compactly in the save (per-tile bits).
  The scepter location is stored in the save (with the XOR key kept for parity,
  §4.3).

### 27.2 Schema and load

- **REQ-414.** The save schema mirrors the `Game` struct field-for-field (§4),
  so serialization round-trips without loss. The one deliberate exception is the
  seed: a catalog game writes `seed_from_catalog: true` and `seed_index`, and
  re-derives `Game.seed` on load (REQ-166, REQ-186); a raw-seed game writes
  `seed_from_catalog: false` and the `seed` itself. Writing the index rather
  than the expanded seed is what makes the round-trip exact, cJSON numbers are
  doubles, so a full-width seed would not survive above 2^53. A save-format
  change bumps `SAVE_VERSION` and updates/replaces the golden fixture
  (`tests/fixtures/save_v1.dat`) and the round-trip regression test.
- **REQ-415.** On load, the caller re-applies `consumed[]` tile mutations to
  the loaded map (`GameApplyTileMutations`) and restores per-continent fog, so
  consumed tiles render and behave as plain terrain.

---

## 28. Resource system

### 28.1 `Resources`

- **REQ-420.** `Resources` (`engine/include/resources.h`) has been loaded once
  from `game.json` at startup (`engine/resources.c`) and treated as read-only
  thereafter. It holds: world/time/economy/tuning/contract/combat blocks; the
  troop/spell/artifact/villain/class catalogs; castle/town/zone tables; the
  128-entry tile-code table; sprite paths; colors; audio metadata; controls
  rows; and all `strings.*` (banners, UI labels, villain descriptions, win/lose
  text, count buckets). Array sizes are compile-time bounded (§3.2).
- **REQ-421.** Where a required numeric value is absent from `game.json`, the
  engine substitutes a documented default; where a required string is absent,
  it substitutes a built-in English fallback (so a minimal pack can omit
  `strings` entirely).

### 28.2 Asset embedding

- **REQ-422.** In embedded-asset mode (`-DEMBED_ASSETS`, used for the Linux
  release and all Windows builds), `scripts/embed_assets.sh` walks `assets/`
  and emits `build/embedded.c` + `build/embedded.h` containing every file as a
  byte array plus a path→data lookup; the shell asset loader (`src/assets.c`)
  reads from these tables instead of the filesystem. Engine-side byte reads go
  through `LoadAssetBytes` (`engine/assets_bytes.c`); shell-side texture loads
  go through `LoadAssetTexture` (`src/assets.c`). The engine never touches GPU
  textures.

---

## 29. UI: views, HUD, dialogs, prompts

### 29.1 Adventure HUD and chrome

- **REQ-430.** The adventure layout (`src/chrome.c`, `src/hud.c`,
  `src/map_render.c`, constants in `src/layout.h`): a **top status bar** (yellow
  border) showing Options / Controls / Days Left; a **map viewport** (5×5 tiles,
  240×170, hero centred, camera clamped at zone edges); a **right sidebar**
  (48px: portrait, contract/siege/magic/puzzle icons, gold); and a **bottom**
  region that drops out for dialogs and prompts.

### 29.2 Views

- **REQ-431.** Full-screen overlays (`ViewKind`, §3.4), managed by
  `src/views.c` and drawn by `src/views_render.c`, with per-location screens in
  `src/screens/`. Toggle views: Army (`A`), Character (`V`), Contract (`I`),
  Puzzle (`P`, 5×5 grid derived from villains_caught + artifacts_found),
  Worldmap (`M`), Controls (`C`), Options (`O`). Location views: Town, Home
  Castle, Own Castle, Dwelling, Alcove, Recruit Soldiers. End views: Win, Lose.

### 29.3 Dialogs and prompts

- **REQ-432.** Two dialog flavors: `open_dialog(header, body)` (`src/ui.c`), a bottom-frame box dismissed by any key, supporting paginated body (split on
  form-feed `\f`); and the blocking prompts (`src/prompt.c`) (`prompt_yes_no_open`, `prompt_numeric_open`, `prompt_text_input_open`) whose
  results are dispatched through a pending-flow state machine
  (`src/shell_promptdispatch.c`, `engine/pending.h`). The engine never renders;
  it requests prompts/dialogs through the host callbacks in
  `engine/include/ui_host.h`, which the shell implements.

---

## 30. Input and controls

- **REQ-440.** Keyboard bindings (adventure §12; combat §25.9) are owned by
  `src/input.c`, which also maps the gamepad. The render exit key is disabled
  so `Escape` dismisses overlays rather than closing the window.
- **REQ-441.** The Controls menu (`VIEW_CONTROLS`) exposes per-game settings
  persisted in `Game.stats.options[7]` (parallel to `res->controls.items[]`):
  animation delay, sounds, walk-beep, animation toggle, CGA, music, volume.
  Meta keys: Alt+Enter fullscreen, backtick screenshot
  (`screenshots/shot_NNNN.png`, `src/screenshot.c`), `Q` save-and-quit,
  `Ctrl+Q` fast quit (`src/shell_fastquit.c`).

---

## 31. Cheats and debug

- **REQ-450.** `F10` opens a debug cheat menu (`src/shell_cheats.c`) for
  development: granting gold, spells, leadership, revealing the map, jumping
  zones, etc. It is gated behind the F10 toggle and is not part of normal play.

---

## 32. Audio

- **REQ-460.** Audio (`src/audio.c`) plays OGG music tracks and WAV sound
  effects through raylib's audio device, driven by abstract engine events
  (`audio_play_tune` host callback). Track and SFX paths come from
  `game.json:audio`. Volume, ducking, and the sound on/off option are handled
  shell-side; the engine only emits tune/sfx events.

---

## 33. Rendering

- **REQ-470.** The internal render target is **320×200** (`CL_SCREEN_W/H`,
  the original VGA mode), integer-scaled to fit the window preserving aspect
  ratio (minimum 2×). The base window is 640×400 (§5.3). A 256-color VGA
  palette is loaded from `assets/kings-bounty/data/palette.bin` (768 bytes;
  `src/palette.c`). The font is an 8×8 bitmap (`kb-font.png`, `src/bfont.c`).
  Map tiles are 48×34; sprites are 48×34 except hero/troop frame cycles
  (`<name>_00..03.png`). Tiles are cached as textures by `src/tile_cache.c`;
  sprite sheets load via `src/sprites.c`. The end cartoon is `src/end_cartoon.c`.

---

## 34. CLI, packs, and platform

### 34.1 CLI flags (`build/openbounty`)

- **REQ-480.** Parsed in `src/main.c` (early-exit modes in
  `src/shell_earlyexit.c`): `--version`/`-v`, `--help`/`-h`, `--fullscreen`,
  `--pack <name|path>`, `--save-dir <dir>`, `--seed N` (catalog world `0`–`255`,
  REQ-166), `--movie [<path>]`,
  `--demo` (the human-like agent, `DEMO-SPEC.md`), `--autoplay` (the
  winnability oracle, `AUTOPLAY-SPECS.md`) with its modifiers
  `--autoplay-hero=<class>`, `--autoplay-level=<easy|normal|hard|impossible>`
  and `--autoplay-speed=<slow|normal|fast>`, `--validate-pack [LO [HI]]` (the
  pack-author winnability report), `--headless` (modifier for the agent
  modes), `--verbose` (agent diagnostics), `--extract`, `--out-dir <dir>`
  (modifier for `--extract`), `--pack-dir <src> <dst>`. Normal play takes no
  flags.
  Parsing has been STRICT: a flag needing a value with none, a value outside
  its range, an unknown flag, or a stray token has printed the reason to
  stderr and exited `2` without running anything.

### 34.2 Packs

- **REQ-481.** A pack is a self-contained tree (`game.json` + `art/` + `audio/`
  + `maps/` + `palettes/`) or a zipped `.openbounty` archive; the engine treats
  loose dirs and archives interchangeably (`engine/pack.c`, miniz). Pack
  discovery (`engine/pack.c`): with no `--pack`, search the cwd then the user
  packs directory; if more than one is found, the pack picker
  (`src/pack_select.c`) runs before character creation. Pack schema version is
  1 (`PACK-FORMAT.md`); the engine refuses incompatible versions. The shipped
  game ships **without** game data, the user supplies a pack via `--extract`
  (§37). The full pack format is documented in `docs/PACK-FORMAT.md`.

### 34.3 Platform

- **REQ-482.** Four release targets (`docs/RELEASE-PROCESS.md`): Linux
  x86_64 (tar.gz), Windows x86_64 + i686 (zip, single static .exe, no DLLs),
  macOS universal (zip, arm64 + x86_64, ad-hoc signed), and Web/WebAssembly
  (zip; `.html`/`.js`/`.wasm`/`.data`, the only target that embeds the asset
  pack). Releases are sequential build numbers under `release-N` tags; the
  build number is embedded at compile time and exposed via `--version`.

---

## 35. Recorder, encoder, and harness

- **REQ-490.** `--movie [<path>]` records gameplay to an MP4
  (`src/recorder.c`, `src/encode_mp4*.c`, `src/encode_dialog.c`; vendored
  minih264 + minimp4). Intermediate per-tick frames live in a hidden temp dir;
  at shutdown an "Encoding…" dialog runs the muxer and the temp frames are
  deleted. With no path argument, output goes to
  `<user-data>/openbounty/movie-<timestamp>.mp4`.
- **REQ-491.** A frame-host harness (`src/frame_host.c`, `src/input_host.c`)
  provides a per-frame hook used by gameplay tests and the recorder to script
  input and capture state; the engine exposes a JSON state snapshot
  (`engine/state_serialize.c`) for these consumers.

---

## 36. Autoplay planner

- **REQ-500.** The engine exposes a determinism-preserving introspection
  surface for an autoplay planner: `GameRngSnapshot` / `GameRngRestore`
  (`engine/game.c`) snapshot and restore the process-global world RNG so the
  planner's plan-time engine replays do not perturb the live game's RNG
  sequence; `GamePeekChest` (§22.1) returns what a chest *would* yield without
  mutating state; and combat's stable per-encounter RNG seeding (§25.13) makes
  a fight's outcome a pure function of (seed, encounter identity, mode) so a
  predicted result matches the live one. These exist so an automated player can
  plan ahead deterministically.

---

## 37. Tools, asset extraction

- **REQ-510.** Asset extraction is pure C compiled into the `openbounty`
  binary (`tools/extract*.c`); the invocation is `./build/openbounty --extract`.
  The extractor reads a user's DOS King's Bounty distribution (`KB.EXE`, etc.;
  input `legacy/bin/KB.EXE` if present, else `./KB.EXE`) and writes a complete
  `.openbounty` pack (palette, font, sprites, tiles, chrome, audio metadata,
  `game.json`) to `<user-data>/openbounty/<pack_id>.openbounty`. It is split one
  translation unit per pipeline stage: `extract_unpack.c`, `extract_lzw.c`,
  `extract_vga.c`, `extract_png.c`, `extract_chrome.c`, `extract_gamejson.c`,
  `extract_io.c`, plus the dispatcher `extract.c`. `--out-dir` emits a loose
  tree instead of a zip; `--pack-dir <src> <dst>` zips a pre-extracted tree
  into a `.openbounty` archive. The shipped pack is **not** distributed (the
  DOS-extracted assets are copyright-restricted). There is no Python.

---

# Part IV, Deviations & data

## 38. Known deviations from OpenKB

These are the standing differences between OpenBounty and OpenKB: where the
architecture diverges without changing gameplay (§38.1), where gameplay itself
differs from OpenKB or DOS (§38.2), where an OpenKB behaviour is preserved
even though DOS differs (§38.3), and what is deliberately out of scope
(§38.4).

### 38.1 Architectural divergences (no gameplay change)

- **REQ-520.** raylib instead of SDL 1.2; JSON saves instead of the OpenKB
  20,421-byte binary (saves are not interchangeable with OpenKB or DOS); a
  single `assets/kings-bounty/` tree + `.openbounty` packs instead of DOS `.CC`
  packs / module dirs; Java-style LCG seeded from `g->seed` instead of libc
  `rand()`; per-tile terrain+interact struct instead of a 128-byte tile-id
  space; per-tile sign text instead of a global indexed list. See §1.10.

### 38.2 Gameplay deviations from OpenKB / DOS

- **REQ-521.** **Pikemen cost**: OpenBounty uses 300 (DOS-original); OpenKB
  inherited 800. This is the only catalog-data deviation between OpenBounty and
  OpenKB.
- **REQ-522.** **Time Stop floor**: OpenBounty imposes a 10-step floor that
  OpenKB does not (the bonus is `max(spell_power * 10, 10)`; §19.3).
- **REQ-523.** **Astrology dwelling refresh**: matches OpenKB exactly: only
  the matching dwelling refills to `max_population`; non-matching dwellings keep
  their current count (§24.2).

### 38.3 OpenKB-faithful behaviours (preserved even where DOS differs)

- **REQ-524.** **No siege-weapons gate**: the `siege_weapons` purchase flag
  exists and persists but `lay_siege` never checks it (§25.12).
- **REQ-525.** **Max-spells chest outcome unreachable**: the shipped chest
  chance tables make the max_spells outcome unreachable (`chance_spell_power ==
  chance_max_spells`); preserved deliberately (§22.1).
- **REQ-526.** **Strict gold checks**: spell/boat/alcove purchases use strict
  `<` comparisons matching OpenKB.

### 38.4 Non-goals

- **REQ-527.** **Multiplayer**: OpenKB carried SDL_net combat; OpenBounty has
  none, by design. **Module system**: OpenKB's discovery + chain-of-
  responsibility loader is replaced by a single active pack. **DOS binary save
  compatibility**, not a goal; saves are JSON.

---

## Appendix A, Complete data tables (from `game.json`)

These tables were regenerated from `assets/kings-bounty/game.json` (the
shipped `kings-bounty` pack). Values are the authoritative source; the spec
body names JSON paths rather than copying values, so this appendix is the one
place the full tables are reproduced.

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

Per-zone counts: `[6, 4, 4, 3]`. Each villain has a fixed 5-stack army
(`game.json:villains[].army_*`) copied into its host castle at salt time.
Per-villain `features` / `crimes` flavor text lives in
`game.json:strings.villain_descriptions` (17 entries).

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

Each town additionally carries gate coords, boat coords, an intel castle, and
an optional pinned spell (`game.json:towns[]`). Hunterville pins the Bridge
spell.

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
- **strings.banners:** 111 entries (`game.json:strings.banners`).
- **economy / spawn / score / combat blocks:** values reproduced inline in
  §22.1 (chest), §15.2 (spawn), §26.1 (score), §14.1 (morale chart).

---

## Document history

This document merges the former `GAMEPLAY-SPEC.md` (game-design/rules) and the
former `OPENBOUNTY-SPEC.md` (implementation) into one source. The merge
corrected the earlier implementation spec's stale combat description (combat is
fully implemented, §25), updated the save version (7), save slot count (10),
and storage caps (`GAME_MAX_MUTATIONS` = 1024) to current values, repointed all
code citations from the pre-engine-split `src/` paths to `engine/` +
`engine/include/`, and regenerated the data tables from the current
`game.json`.
