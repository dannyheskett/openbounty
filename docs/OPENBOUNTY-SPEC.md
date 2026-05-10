# OpenBounty — Implementation Specification

This document is the **as-built specification** for OpenBounty as it
currently exists in the source tree. It mirrors the structure of
`docs/OPENKB-SPEC.md` (which describes the OpenKB DOS-faithful reference
implementation) and serves as the dual reference: where OpenBounty
matches OpenKB, this spec is brief and points at OPENKB-SPEC for detail;
where OpenBounty deliberately deviates (engine choices: raylib vs SDL,
JSON saves vs binary, single asset pack vs module system), the deviation
is documented inline as the authoritative behavior.

**Tiebreaker:** when this spec disagrees with the source code, the
source code wins and this spec is updated to match. When the source
code disagrees with OpenKB, `docs/OPENKB-SPEC.md` and the OpenKB
sources are the intended target -- but the OpenBounty code as it
currently stands is what this document describes.

**Scope:** the code in `src/`, `assets/kings-bounty/`, and `docs/`.

---

## Table of contents

1. Top-level architecture
2. Data types and conventions
3. Global constants and limits
4. The `Game` struct — complete game state
5. Troop catalog — all 25 units
6. Character classes and ranks
7. Villains — armies and rewards
8. Dwellings, morale, and the morale chart
9. Artifacts and their powers
10. Spells
11. The world — zones, castles, towns
12. Tiles and map conventions
13. Random generation — seeded RNG, chance tables, salting
14. Game creation — `GameInit` step by step
15. Player actions
16. Day/week cycle
17. Combat engine (stub status)
18. Spell effects in detail
19. AI behavior
20. Chests, recruits, and rewards
21. Victory, defeat, scoring
22. Save file (JSON) — schema
23. UI — screens, menus, layout
24. Verbatim strings (data-driven, `strings.banners`)
25. Strings sourced from KB.EXE — N/A (data-driven)
26. Colors — VGA palette, color schemes
27. Resource system (`Resources` struct)
28. Module system — N/A (single asset pack)
29. DOS asset formats — N/A
30. Font — bitmap glyph format
31. Keybindings
32. Configuration
33. Multiplayer combat — N/A
34. Known bugs and incomplete features
35. Appendices
36. Tools (`tools/`)

Appendices:

- A. Verbatim source extracts (RNG, GameInit, salt_*, bury_scepter, …)
- B. Save schema (verbatim JSON example)
- C. Sprite assets (file inventory)
- D. game.json schema reference
- E. Tile code reference
- F. Complete control flow per frame
- G. Bug-fix history
- H. Missing / pending
- I. File line counts
- J. Complete data tables (troops, spells, classes, artifacts,
     villains, towns, castles, tile codes, banners, economy,
     tuning, time, spawn, score, world, morale chart, zones,
     villain descriptions, UI, win/lose)
- K. Resource struct catalog (verbatim from `src/resources.h`)
- L. Complete `strings.banners` inventory
- M. Classic layout constants + diagram
- N. Input handler keymap
- O. Combat call-sites
- P. Auto-screenshot system
- Q. Boat coords map
- R. Save-path resolver
- S. Engine deviations from OpenKB
- T. Future work / non-goals
- U. Public function index
- V. Prompt system
- W. Chrome (status, sidebar, frame)
- X. Fog of war
- Y. Sprite catalog
- Z. Test infrastructure

---

## 1. Top-level architecture

### 1.1 Process flow

`main()` (src/main.c:1646) parses CLI args, loads `Resources` from the
asset pack, initializes raylib, opens the title window, runs the
startup picker (new game / continue / quit), then enters the
adventure loop.

Per-frame loop (src/main.c:1815+):

1. Alt-Enter toggles fullscreen.
2. F10 enters debug cheat-menu mode (consumes the next letter key).
3. `pump_week_end_dialog` pops any pending astrology / budget dialog.
4. Input dispatch is a layered if/else chain prioritizing:
   - Fast-quit prompt (Ctrl+Q in adventure)
   - Active modal prompt (`prompt_*` from `src/classic/prompt.c`)
   - View-specific keys (VIEW_HOME_CASTLE, VIEW_OWN_CASTLE,
     VIEW_RECRUIT_SOLDIERS, VIEW_DWELLING, VIEW_ALCOVE,
     VIEW_SPELLS, VIEW_CONTROLS, etc.)
   - Catchall view-dismiss (any key dismisses overlay views) —
     gated on `!dialog_is_active()` so dialogs over persistent
     views don't tear down the underlying view.
   - Active dialog (any key advances/dismisses, Ctrl+Q quits)
   - Game-over (any key returns to menu)
   - Default classic-input (`classic_input_poll` from
     `src/classic/input.c`) for adventure movement, view keys,
     spells, search, end-week, etc.
5. Every-frame draw: chrome (top status, side bar, map frame),
   map (`classic_map_render`), HUD overlay if visible, view panel
   if any view is active, dialog panel if active, prompt panel if
   active.

### 1.2 Run-game lifecycle

```
main()
  ├─ resources_load("assets/kings-bounty/game.json")
  ├─ InitWindow / palette_init / bfont_init / sprites_load
  ├─ classic_run_startup_picker → STARTUP_NEW | STARTUP_LOAD | STARTUP_QUIT
  ├─ if NEW:    GameInit(game, res, name, class_id, difficulty, seed)
  │             MapLoadZoneWithPlacements(map, res, position.zone, game)
  │             FogInit(fog) + FogReveal(fog, map, x, y, sight)
  ├─ if LOAD:   SaveGameRead(slot, &game) → restores everything
  │             MapLoadZoneWithPlacements + GameApplyTileMutations
  │             FogReveal around starting position only
  ├─ adventure loop (frame-driven, see §1.1)
  └─ on quit: CloseWindow + resources_free
```

### 1.3 Build system

Plain Makefile (`Makefile`). Targets:

- `make` → `build/openbounty` (dev build, asset files referenced from disk).
- `make release` → `build/openbounty-release` (asset files embedded
  via `scripts/embed_assets.sh` → `build/embedded.{c,h}`, single binary).
- `make win64` / `make win32` — cross-compile to Windows.
- `make clean` — removes `build/`.

Compiler: `gcc -std=c99 -Wall -Wextra -O2`. Sources listed
explicitly in the Makefile's `SRC` variable (no wildcard). cJSON
and raylib live in `third_party/`.

### 1.4 Vendor code

- `third_party/raylib-install/` — raylib 5.x headers and static lib.
- `third_party/cjson/cJSON.{c,h}` — JSON parser, vendored.

No SDL. No SDL_net. No SDL_mixer. raylib's audio module is used for
sound effects, but openbounty does not currently load any sounds.

### 1.5 Memory model

- `Resources` is loaded once at startup, owned by `main()`, never
  freed except on shutdown. Pointer is stored on `Game.res` for code
  paths that need it without a separate parameter.
- `Game` is a single fixed-size struct (~600KB) allocated on the
  stack of `main()`. No dynamic allocation per game.
- `Map` and `Fog` are stack-allocated alongside `Game`. `Map.tiles`
  is `[64][64]Tile` (~800KB); `Fog` is a bitset.
- `Sprites` (loaded textures) and `Resources` (catalog data) are
  the only long-lived heap allocations beyond raylib internals.

### 1.6 Coordinate system

- Map tiles: 0,0 top-left; +x right, +y down. Zone width/height
  are read from `ResZone.width`/`height` (typically 64×64).
- Tile dimensions in pixels: `CL_TILE_W = 32`, `CL_TILE_H = 28`
  (`src/classic/layout.h`).
- The viewport shows a 5×5 tile region centered on the hero.
- Origin convention matches OpenKB.

### 1.7 Naming conventions

- `Game` struct: `g` (or `game`) in arguments.
- `Resources` struct: `res` or `g->res`.
- Function naming: PascalCase for public game-state mutators
  (`GameInit`, `GameBuyTroop`, `GameSwitchZone`); `snake_case` for
  static helpers (`enforce_dwelling`, `roll_creature`).
- Resources: typed C structs (`ResTroop`, `ResZone`, `ResUI`,
  `ResBanners`) parsed from JSON once at startup.
- Tile interaction: `Interact` enum (`INTERACT_NONE`,
  `INTERACT_CASTLE_GATE`, `INTERACT_TOWN`, …) in `src/tile.h`.

### 1.8 Important global state

Static singletons (one per process):

- `g_resources` (src/resources.c:14) — the loaded `Resources`,
  consulted by `troop_by_id`, `class_by_id`, etc.
- `g_screenshot_seq` (src/screenshot.c) — auto-screenshot counter.
- `dialog_active`, `dialog_header`, `dialog_body`, `dialog_page`
  (src/ui.c) — current modal dialog.
- Prompt state in `src/classic/prompt.c`.
- Various `pending_*` statics in `src/main.c` that survive between
  the moment a flow is opened (e.g. `start_foe_friendly_flow`) and
  the dispatch in the prompt-resolved branch.

### 1.9 Source layout

```
src/
  main.c              — entry point, frame loop, input dispatch
  game.{c,h}          — Game struct, GameInit, salt_continent,
                        bury_scepter, repopulate_castle, end_day,
                        end_week, GameSpendWeek, GameAddTroop,
                        GameBuyTroop, GameGarrisonTroop, dwellings,
                        astrology, foes_follow
  map.{c,h}           — Map struct, MapLoadZone, tile stamping
  fog.{c,h}           — Fog struct, FogInit, FogReveal
  tile.{c,h}          — Terrain enum, Interact enum, Tile struct
  tile_cache.{c,h}    — pre-rendered tile textures
  resources.{c,h}     — Resources struct, JSON parser
  tables.{c,h}        — TroopDef/SpellDef/ClassDef/RankDef catalog API
  savegame.{c,h}      — SaveGameRead/Write (JSON)
  savepath.{c,h}      — XDG-compliant save path resolver
  adventure.{c,h}     — adventure_walkable_*, adventure_handle_interact
  combat.{c,h}        — combat stub (returns WIN; real engine pending)
  bfont.{c,h}         — 8×8 bitmap font loader + draw
  sprites.{c,h}       — sprite catalog loader (textures keyed by id)
  views.{c,h}         — view stack (VIEW_*), shared view rendering
  ui.{c,h}            — dialog state, ui_any_key_pressed
  screenshot.{c,h}    — auto-screenshot helper
  assets.{c,h}        — embedded-asset accessor (release builds)

src/classic/
  layout.h            — pixel-coord constants (CL_*)
  palette.{c,h}       — 256-color VGA palette + PAL_CLR macro
  chrome.{c,h}        — top status bar, side bar, map frame
  hud.{c,h}           — floating HUD overlay (army snapshot)
  map_render.{c,h}    — viewport renderer (tile sprites, hero, foes)
  overlay.{c,h}       — view panels, dialog, town menu
  views.{c,h}         — VIEW_ARMY/CHARACTER/CONTRACT/SPELLS/PUZZLE/
                        WORLDMAP/OPTIONS/CONTROLS rendering
  input.{c,h}         — classic-input keymap → action enum
  prompt.{c,h}        — modal yes/no, numeric, A/B, text-input prompts
  startup.{c,h}       — title screen + new/continue/quit picker
  end_cartoon.{c,h}   — win/lose intermezzo
  screens/            — one .c per persistent screen
    home_castle.c     — King Maximus throne-room (A=Recruit, B=Audience)
    recruit_soldiers.c— direct port of OpenKB recruit_soldiers
    own_castle.c      — Garrison/Remove A..E flow
    dwelling.c        — outdoor dwelling backdrop + recruit prompt
    alcove.c          — magic alcove (60g lesson)
    end_game.c        — VIEW_WIN / VIEW_LOSE
```

### 1.10 Engine choices vs OpenKB

| Concern | OpenKB | OpenBounty |
| --- | --- | --- |
| Window/render | SDL 2 | raylib 5 |
| Audio | SDL_mixer | raylib (currently unused) |
| Net | SDL_net (combat.c) | none |
| Saves | 20,421-byte binary | JSON |
| Asset bundling | DOS .CC packs / module dirs | single `assets/kings-bounty/` tree |
| Modules | discovery + chain-of-resp loader | N/A |
| Resolution | 320×200, scaled | 640×400 native (2× DOS) |
| Palette | EGA / CGA / Hercules build-time | VGA only at runtime |
| RNG | libc `rand()` | Java-style LCG seeded from `g->seed` |
| Tile data | 128-byte tile-id space | per-tile struct (terrain + interact) |
| Sign text | global `STRL_SIGNS` indexed | per-tile `sign_title`/`sign_body` |

These are not bugs; they are intended architectural choices. The
gameplay-significant constants and formulas all match OpenKB or are
flagged as known deviations in §34.

---

## 2. Data types and conventions

### 2.1 Primitive types

- `int`, `unsigned`, `bool` (C99 `<stdbool.h>`), `char`, `unsigned long`.
- No fixed-width types in user code — gameplay state is plain `int`
  unless a struct field documents otherwise (e.g. `g->seed` is
  `unsigned long` for LCG arithmetic).
- `float` and `double` are used only for raylib API calls
  (rectangles, sprite coords) and `GetTime()` cadence checks.

### 2.2 Endian

- Save format is JSON (text). No endian concerns.
- Tile maps are text `.dat` files (one char per tile + `# header`
  comment). No endian concerns.
- raylib textures are loaded via raylib's internal codecs.

### 2.3 String identifiers

All gameplay entities are keyed by string ids parsed from
`game.json` (e.g. `"peasants"`, `"sorceress"`, `"continentia"`).
Internal lookup tables (`troop_by_id`, `class_by_id`,
`resources_zone_by_id`) walk the parsed catalog linearly.

### 2.4 Memory ownership

- The `Resources *` pointer stored in `Game.res` is borrowed; the
  `Game` lifecycle never outlives the `Resources` lifecycle.
- Strings inside `Resources` are owned by the `Resources` struct
  (fixed-size arrays inside, not allocated separately).
- Strings inside `Game` are fixed-size arrays (e.g. `char id[24]`)
  copied via `copy_id` / `copy_str` helpers in `src/game.c`.

### 2.5 Numeric ranges

- Gold: `int`, signed, can technically go negative; in practice
  always non-negative (every deduction gates on a `>=` check).
- Counts (army sizes, damages): `int`, signed.
- Coordinates: `int`, range 0 .. zone width-1 / height-1, with
  `-1` used as "unset" sentinel for boat coords, gate coords,
  alcove coords, etc.

---

## 3. Global constants and limits

Defined in `src/game.h`:

```c
#define GAME_NAME_LEN        16
#define GAME_ARMY_SLOTS       5
#define GAME_CONTINENTS       4
#define GAME_TOWNS           26
#define GAME_CASTLES         26
#define GAME_MAX_MUTATIONS   64
#define GAME_MAX_DWELLINGS   64
#define GAME_MAX_PLACEMENTS 128
#define GAME_MAX_FOES        64
#define CONTRACT_CYCLE_MAX    8
```

Defined in `src/resources.h`:

```c
RES_MAX_ZONES         = 8
RES_MAX_ZONE_OBJECTS  = 64
RES_MAX_TOWNS         = 32
RES_MAX_CASTLES       = 32
RES_MAX_NEIGHBORS     = 4
RES_ID_LEN            = 32
RES_NAME_LEN          = 48
RES_PATH_LEN          = 96
RES_BANNER_LEN        = 256
RES_SPAWN_TIERS       = 4    // continent tier (0..3)
RES_SPAWN_POOL_N      = 5    // 5 troop slots per pool (chance curve has 5 thresholds)
RES_MAX_CASTLE_DECOR  = 32
RES_END_BODY_LEN      = 512
```

Defined in `src/tables.h`:

```c
CAT_TROOPS_MAX     = 25
CAT_SPELLS_MAX     = 14
CAT_CLASSES_MAX     = 4
CAT_VILLAINS_MAX   = 17
CAT_ARTIFACTS_MAX  =  8
CAT_ID_LEN         = 24
CAT_NAME_LEN       = 32
CAT_PATH_LEN       = 96
CLASS_MAX_RANKS               = 4
CLASS_MAX_STARTING_TROOPS     = 8
```

Defined in `src/classic/layout.h`:

```c
CL_WINDOW_W   = 640
CL_WINDOW_H   = 400
CL_TILE_W     = 32
CL_TILE_H     = 28
CL_MAP_X      = 16    // CL_FRAME_LEFT_W
CL_MAP_Y      = 22
CL_MAP_W      = 160   // 5 × 32
CL_MAP_H      = 140   // 5 × 28
CL_PANEL_X    = CL_MAP_X
CL_PANEL_Y    = CL_MAP_Y + CL_MAP_H - CL_PANEL_H
CL_PANEL_W    = CL_MAP_W
CL_PANEL_H    = 80
```

(OpenBounty renders at native 640×400 — 2× the DOS resolution. Not
scaled at runtime; assets ship at this size.)

### 3.1 Mount enum (`src/game.h`)

```c
typedef enum {
    MOUNT_RIDE = 0,
    MOUNT_SAIL,
    MOUNT_FLY,
} Mount;
```

### 3.2 TravelMode enum

```c
typedef enum {
    TRAVEL_WALK = 0,
    TRAVEL_BOAT,
} TravelMode;
```

`Mount` is the long-term possession ("can the hero ride/sail/fly?");
`TravelMode` is the moment-to-moment state on the adventure screen.
Not in OpenKB (which conflates them via `KBgame.mount = KBMOUNT_SAIL`
when on the boat); a deliberate engine clarification.

### 3.3 Difficulty enum

```c
typedef enum {
    DIFFICULTY_EASY = 0,
    DIFFICULTY_NORMAL,
    DIFFICULTY_HARD,
    DIFFICULTY_IMPOSSIBLE,
} Difficulty;
```

### 3.4 CastleOwnerKind enum

```c
typedef enum {
    CASTLE_OWNER_PLAYER   = 0,
    CASTLE_OWNER_MONSTERS,
    CASTLE_OWNER_VILLAIN,
    CASTLE_OWNER_SPECIAL,   // king's castle, quest castles
} CastleOwnerKind;
```

OpenKB uses a single `castle_owner[i]` byte where `0xFF` means
player, `0x7F` means monsters, `0..16` is a villain id, and special
castles are out of band. OpenBounty uses an explicit enum plus a
separate `villain_id[24]` string field.

### 3.5 Morale group enum

Per-troop `morale_group` is a single character in 'A'..'E', stored
as `char`. Used by the morale chart (§8.3).

### 3.6 ABIL_* ability flags (`src/tables.h`)

```c
TROOP_ABIL_FLY      = 1 << 0
TROOP_ABIL_RANGE    = 1 << 1
TROOP_ABIL_MAGIC    = 1 << 2
TROOP_ABIL_UNDEAD   = 1 << 3
TROOP_ABIL_LEECH    = 1 << 4
TROOP_ABIL_ABSORB   = 1 << 5
TROOP_ABIL_REGEN    = 1 << 6
TROOP_ABIL_IMMUNE   = 1 << 7
TROOP_ABIL_SCYTHE   = 1 << 8
```

Stored in `TroopDef.abilities` as a bitmask. JSON encodes them as a
space-separated string (e.g. `"FLY|RANGE"`); parser walks the
delimiters.

### 3.7 POWER_* artifact flags

Defined in `src/tables.h`:

```c
ARTIFACT_POWER_DOUBLE_LEADERSHIP    = 1 << 0
ARTIFACT_POWER_DOUBLE_SPELL_POWER   = 1 << 1
ARTIFACT_POWER_INCREASE_COMMISSION  = 1 << 2
ARTIFACT_POWER_DOUBLE_MAX_SPELLS    = 1 << 3
ARTIFACT_POWER_INCREASED_DAMAGE     = 1 << 4
ARTIFACT_POWER_QUARTER_PROTECTION   = 1 << 5
ARTIFACT_POWER_CHEAPER_BOAT         = 1 << 6
ARTIFACT_POWER_NAVIGATION           = 1 << 7
```

The first four are "instant" — applied at pickup. The last four are
"live" — checked at runtime by `GameHasPower`.

### 3.8 Cost constants

Stored in `Resources.economy` (parsed from `game.json` `economy`):

```c
typedef struct {
    int boat_rent_cost;
    int boat_rent_cost_cheap;        // when CHEAPER_BOAT power held
    int siege_weapons_cost;
    int alcove_cost;
    int spell_cost_default;
    ...
} ResEconomy;
```

Default values (game.json `economy`):

```
boat_rent_cost          = 250
boat_rent_cost_cheap    = 100
siege_weapons_cost      = 3000
alcove_cost             = 60
```

### 3.9 Time constants

```c
typedef struct {
    int days_per_difficulty[4];   // 900, 600, 400, 200 (game.json)
    int day_steps;                // 40
    int week_days;                // 5
} ResTime;
```

### 3.10 Combat-grid constants

Not yet defined — combat module is stubbed. When the combat engine
is implemented, the grid will be 6×5 (matching OpenKB CLEVEL_W=6,
CLEVEL_H=5).

---

## 4. The `Game` struct — complete game state

Defined in `src/game.h:175-242`. Single struct holding every field
that survives a save/load round trip.

### 4.1 Field declaration order

```c
typedef struct Game {
    const Resources *res;             // borrowed pointer
    int              version;
    unsigned long    seed;

    Character        character;
    Stats            stats;
    Position         position;
    TravelMode       travel_mode;
    int              anim_frame;
    bool             hud_visible;

    ArmyStack        army[GAME_ARMY_SLOTS];
    Spellbook        spells;
    Contract         contract;
    Artifacts        artifacts;
    WorldProgress    world;
    BoatState        boat;
    TownRecord       towns[GAME_TOWNS];
    CastleRecord     castles[GAME_CASTLES];
    ScepterLocation  scepter;

    TileMutation     consumed[GAME_MAX_MUTATIONS];
    int              consumed_count;

    DwellingState    dwellings[GAME_MAX_DWELLINGS];
    int              dwelling_count;

    SaltedPlacement  placements[GAME_MAX_PLACEMENTS];
    int              placement_count;

    FoeState         foes[GAME_MAX_FOES];
    int              foe_count;
} Game;
```

### 4.2 Field semantics

#### 4.2.1 Top-level

- `res` — borrowed pointer to the loaded `Resources`. Never serialized.
- `version` — schema version. Always equals `SAVE_VERSION`
  (currently 1). Emitted by `SaveGameWrite`; checked by `SaveGameRead`.
- `seed` — `unsigned long`. RNG seed used by `salt_continent`,
  `bury_scepter`, `salt_villains`, `salt_spells`, `roll_creature`.
  Re-seeded into `game_rng_state` at the start of `GameInit` and
  again on save load.

#### 4.2.2 Character

```c
typedef struct {
    char        name[GAME_NAME_LEN];      // null-terminated, capitalized
    ClassState  cls;                      // id, rank_index, denormalized rank id/title
    Difficulty  difficulty;
    Mount       mount;                    // RIDE / SAIL / FLY
} Character;
```

The `cls.id` ("knight", "paladin", "sorceress", "barbarian") indexes
into `res->classes[]`. `cls.rank_index` is 0..3. `cls.rank_id` and
`cls.rank_title` are denormalized at rank-up time so the UI can
read them without a table lookup.

#### 4.2.3 Stats

```c
typedef struct {
    int  gold;
    int  commission_weekly;
    int  leadership_base;
    int  leadership_current;
    int  followers_killed;
    int  score;
    int  spell_power;
    int  max_spells;
    bool knows_magic;
    int  siege_weapons;
    int  time_stop;
    int  steps_left_today;
    int  days_left;
    bool game_over;
    int  last_commission;
    int  last_astrology_troop;
    int  options[6];          // controls-menu settings
} Stats;
```

- `leadership_base` is the rank-derived value; `leadership_current`
  starts equal but increases by Raise Control spell + chest events.
- `time_stop` decrements per step instead of `steps_left_today`
  while non-zero (Time Stop spell).
- `last_commission` and `last_astrology_troop` are display state for
  the week-end dialog.
- `options[6]` mirrors OpenKB's `controls_menu` settings: delay,
  sounds, walk-beep, animation, army-size display, CGA (last is
  always 0 in openbounty, no CGA mode).

Initial: `leadership_base = leadership_current = class.ranks[0].leadership`,
`gold = class.starting_gold`, `commission_weekly = class.ranks[0].commission`,
`spell_power = class.ranks[0].spell_power`,
`max_spells = class.ranks[0].max_spells`,
`knows_magic = class.ranks[0].knows_magic` (Sorceress only),
`siege_weapons = 0`, `time_stop = 0`,
`steps_left_today = res.time.day_steps` (40),
`days_left = res.time.days_per_difficulty[difficulty]`,
`followers_killed = 0`, `score = 0`,
`options = res.world.default_options` (typically `[4, 1, 1, 1, 1, 0]`).

#### 4.2.4 Position / TravelMode

```c
typedef struct {
    char zone[24];
    int  x, y;
    int  last_x, last_y;
    bool facing_left;
} Position;
```

- `zone` is the current zone id (string, parses from
  `world.starting_zone` or `is_home: true` zone in JSON).
- `last_x/y` is the previous tile (used for bump-back when the hero
  walks onto an interactive tile, and as the target for
  `foes_follow`).
- `facing_left` flips the hero sprite horizontally.

`TravelMode` is `TRAVEL_WALK` or `TRAVEL_BOAT`. The hero is on the
boat sprite when `TRAVEL_BOAT`; the boat tile flips to the hero's
former tile when stepping off.

#### 4.2.5 Army

```c
typedef struct {
    char id[32];        // troop id; empty = slot empty
    int  count;
} ArmyStack;
```

Five slots, fixed. Empty slot = `id[0] == '\0'` and `count == 0`.

#### 4.2.6 Spellbook

```c
typedef struct {
    int counts[14];     // parallel to res->spells[]
} Spellbook;
```

Slot order matches `game.json` `spells[]` order (the order they
appear). `spell_index_by_id("bridge")` walks the catalog.

#### 4.2.7 Contract

```c
typedef struct {
    char active_id[24];                    // current contract villain id
    char cycle[CONTRACT_CYCLE_MAX][24];    // bounded; real length in res.contract.cycle_length
    int  last_contract;                    // last villain rotated in
    int  max_contract;                     // next villain to add
    bool villains_caught[17];
} Contract;
```

OpenKB stores the contract cycle as a fixed 5-byte array of villain
ids. OpenBounty keeps a length-bounded array up to
`CONTRACT_CYCLE_MAX = 8` to allow a mod with more than 5 active
contracts. Default cycle length is 5 (matches OpenKB).

`last_contract` is the index of the most-recently issued contract
in `cycle[]`; `max_contract` is the next villain id to rotate in
when a contract is fulfilled.

#### 4.2.8 Artifacts

```c
typedef struct {
    bool found[8];      // parallel to res->artifacts[]
} Artifacts;
```

Slot order matches the `artifacts[]` array in game.json. `Artifact[i].zone`
and `local_idx` map each catalog entry to its continent + slot
(see §9).

#### 4.2.9 WorldProgress

```c
typedef struct {
    bool zones_discovered[GAME_CONTINENTS];   // 4
    bool orbs_found[GAME_CONTINENTS];
    bool puzzle_revealed[25];                 // 17 villains + 8 artifacts
} WorldProgress;
```

Initial: only `zones_discovered[home_zone_index] = true`. Set by
navmap chests, orb chests, contract fulfillment.

#### 4.2.10 Boat

```c
typedef struct {
    bool has_boat;
    int  x, y;            // tile coords on the boat's zone
    char zone[24];
} BoatState;
```

`has_boat = false` initially. Set by town's `B) Rent boat` choice;
unset by `B) Cancel boat rental` (only allowed when not currently
sailing).

#### 4.2.11 TownRecord / CastleRecord

```c
typedef struct {
    char id[24];
    bool visited;
    char spell_for_sale[24];      // randomized at GameInit by salt_spells
} TownRecord;

typedef struct {
    char            id[24];
    bool            visited;
    bool            known;             // visible in worldmap
    CastleOwnerKind owner_kind;
    char            villain_id[24];    // empty unless owner_kind==CASTLE_OWNER_VILLAIN
    Unit            garrison[GAME_ARMY_SLOTS];
} CastleRecord;
```

Indexed parallel to `res->towns[]` and `res->castles[]`.
`spell_for_sale` is set by `salt_spells` (§14 step 8). `known` is
set when the player gathers info on this castle's intel target,
when Find Villain is cast, or by the worldmap-reveal chest.

#### 4.2.12 ScepterLocation

```c
typedef struct {
    char     zone[24];
    int      x, y;
    unsigned key;       // OpenKB's XOR key, kept for save parity (unused at runtime)
} ScepterLocation;
```

Set by `bury_scepter` at GameInit (§14 step 2). The win check
compares hero position against `scepter.zone/x/y` after all 17
villains are caught and all 8 artifacts found.

#### 4.2.13 TileMutation array

```c
typedef struct {
    char zone[24];
    int  x, y;
} TileMutation;
```

Permanently consumed tiles: artifact pickup, opened chest, alcove
visit, friendly foe consumed (yes or no), telecave used.
`GameApplyTileMutations` walks this array on zone load and clears
the interactive overlay on each matching tile.

#### 4.2.14 DwellingState array

```c
typedef struct {
    char zone[24];
    int  x, y;
    char troop_id[32];
    int  count;
    int  max_population;
} DwellingState;
```

Created by `enforce_dwelling` at GameInit (matching OpenKB's
eager-populate model). Both JSON-declared and salt-placed dwellings
get a row each. `count` is decremented on recruit, refilled on
matching astrology week.

#### 4.2.15 SaltedPlacement array

```c
typedef struct {
    char zone[24];
    int  x, y;
    int  kind;          // Interact enum value
    char id[32];        // payload id (artifact id, telecave_n, etc.)
} SaltedPlacement;
```

Produced by `salt_continent` per zone at GameInit. Replayed by
`MapLoadZoneWithPlacements` whenever the zone is loaded; the tile's
`interactive` becomes `kind` and `t->id` becomes `id`.

#### 4.2.16 FoeState array

```c
typedef struct {
    char zone[24];
    int  x, y;
    char placement_id[32];
    Unit garrison[GAME_ARMY_SLOTS];
    bool alive;
    bool friendly;
} FoeState;
```

Hostile foes are added from `ResZone.armies[]` (static placements
in JSON, OpenKB's 0x91 tile equivalent). Friendly foes are salted
onto chest slots by `salt_continent`. Garrisons are pre-rolled at
spawn (`roll_hostile_garrison`).

### 4.3 Allocation lifecycle

`GameInit` zero-fills the `Game` struct and populates fields per the
spawn sequence (§14). The struct lives on `main()`'s stack for the
entire process lifetime; `STARTUP_NEW` overwrites it via `GameInit`,
`STARTUP_LOAD` overwrites it via `SaveGameRead`.

### 4.4 Field constraints / invariants

- `army[i].id[0] == '\0'` ⇔ `army[i].count == 0` (slot is empty).
- `castles[i].owner_kind == CASTLE_OWNER_VILLAIN` ⇔ `castles[i].villain_id[0] != '\0'`.
- `boat.has_boat == false` ⇒ `boat.x == -1 && boat.y == -1`.
- `placements[]` is append-only during GameInit, never modified at
  runtime (placements are static once the world is salted).
- `consumed[]` is append-only at runtime.
- `dwellings[]` rows are created in `enforce_dwelling` but never
  removed (OpenKB also keeps them across the game).

### 4.5 Save schema parity

Every field in `Game` except `res`, `version`, `anim_frame`, and
`scepter.key` is serialized to JSON by `SaveGameWrite` (see §22).
The omissions:

- `res` — always re-resolved at load time.
- `version` — written separately at the top of the JSON object.
- `anim_frame` — pure visual state.
- `scepter.key` — XOR key from OpenKB's binary save format,
  meaningful only for DOS save compatibility.

---

## 5. Troop catalog — all 25 units

Defined in `assets/kings-bounty/game.json` `troops[]` (parsed into
`TroopDef troops[CAT_TROOPS_MAX]` by `src/resources.c`).

### 5.1 `TroopDef` struct (`src/tables.h:38-60`)

```c
typedef struct {
    int  index;
    char id[CAT_ID_LEN];
    char name[CAT_NAME_LEN];
    char sprite[CAT_PATH_LEN];
    char anim[4][CAT_PATH_LEN];   // 4-frame idle
    int  skill_level;
    int  hit_points;
    int  move_rate;
    int  melee_min, melee_max;
    int  ranged_min, ranged_max, ranged_ammo;
    int  recruit_cost;
    int  spoils_factor;
    int  abilities;               // ABIL_* bitmask
    char dwelling[CAT_ID_LEN];    // "plains" | "forest" | "hill" | "dungeon" | "castle"
    int  max_population;
    int  growth_per_week;
    char morale_group;            // 'A'..'E'
    int  tier_counts[4];          // per-tier monster spawn counts
} TroopDef;
```

### 5.2 Master troop table

The 25 entries in catalog order (matches OpenKB indices 0x00..0x18):

```
idx  id           SL  HP  MV  MELEE   RANGED  AMMO   COST  SPOIL  ABIL                          DWELL    MAXPOP  GROW  MOR  TIER_COUNTS
00   peasants     1   1   1   1-1     0-0     0      10    1      —                             plains   250     6     A    50,50,50,50
01   sprites      1   1   1   1-2     0-0     0      15    1      FLY                           forest   200     6     C    50,50,50,50
02   militia      2   2   2   1-2     0-0     0      50    5      —                             castle   0       5     A    20,20,20,20
03   wolves       2   3   3   1-3     0-0     0      40    4      —                             plains   150     5     D    20,20,20,20
04   skeletons    2   3   2   1-2     0-0     0      40    4      UNDEAD                        dungeon  150     5     E    20,20,20,20
05   zombies      2   5   1   2-2     0-0     0      50    5      UNDEAD                        dungeon  100     5     E    10,10,10,10
06   gnomes       2   5   1   1-3     0-0     0      60    6      —                             forest   250     5     C    10,10,10,10
07   orcs         2   5   2   2-3     1-2     10     75    7      —                             hill     200     5     D    10,10,10,10
08   archers      2   10  2   1-2     1-3     12     250   25     RANGE                         castle   0       5     B    5,5,5,5
09   elves        3   10  3   1-2     2-4     24     200   20     —                             forest   100     4     C    10,10,10,10
0A   pikemen      3   10  2   2-4     0-0     0      300   30     —                             castle   0       4     B    5,5,5,5
0B   nomads       3   15  2   2-4     0-0     0      300   30     —                             plains   150     4     C    5,5,5,5
0C   dwarves      3   20  1   2-4     0-0     0      350   30     —                             hill     100     4     C    5,5,5,5
0D   ghosts       4   10  3   3-4     0-0     0      400   40     ABSORB | UNDEAD               dungeon  50      4     E    5,5,5,5
0E   knights      5   35  2   6-10    0-0     0      1000  100    —                             castle   0       3     B    2,2,2,2
0F   ogres        4   40  1   3-5     0-0     0      750   75     —                             hill     50      3     D    2,2,2,2
10   barbarians   4   40  3   1-6     0-0     0      750   75     —                             hill     50      3     D    2,2,2,2
11   trolls       4   50  2   7-10    0-0     0      1000  100    REGEN                         hill     30      2     D    2,2,2,2
12   cavalry      4   20  4   3-5     0-0     0      800   80     —                             castle   0       2     B    2,2,2,2
13   druids       5   25  2   2-3     10-25   8      700   70     RANGE | MAGIC                 forest   30      2     C    2,2,2,2
14   archmages    5   25  1   2-3     25-50   4      1200  120    RANGE | MAGIC                 dungeon  25      2     C    2,2,2,2
15   vampires     5   30  1   3-6     0-0     0      1500  150    LEECH | UNDEAD                dungeon  20      2     E    1,1,1,1
16   giants       5   60  3   10-20   5-10    2      2000  200    —                             dungeon  20      1     D    1,1,1,1
17   demons       6   200 1   5-7     0-0     0      3000  300    SCYTHE                        dungeon  10      1     E    1,1,1,1
18   dragons      6   200 1   25-50   0-0     0      5000  500    FLY | IMMUNE                  dungeon  10      1     E    1,1,1,1
```

(Rows mirror OpenKB `troops[]` in `bounty.c:46-220` exactly except
for Pikemen cost, which openbounty sets to 300 (DOS-original) where
OpenKB inherited 800.)

### 5.3 Sprite paths

Each troop has a base sprite (`troops/<id>.png`) and a 4-frame idle
animation at `troops/<id>_NN.png`. Sprites are 48×34 pixels (rendered
into the 5×5 viewport at the same size).

### 5.4 Per-troop abilities

- **FLY (0x01)** — sprite renders mid-air; in combat will use 2
  flight points/turn (combat unimplemented).
- **RANGE (0x02)** — has nonzero `ranged_ammo`; can shoot.
- **MAGIC (0x04)** — Druids, Archmages. Magic-typed ranged attacks.
- **UNDEAD (0x08)** — affected by Turn Undead spell.
- **LEECH (0x10)** — Vampires. Self-healing on melee kills.
- **ABSORB (0x20)** — Ghosts. Stack grows on each kill.
- **REGEN (0x40)** — Trolls. Heal full each round.
- **IMMUNE (0x80)** — Dragons. Magic ranged attacks cancelled.
- **SCYTHE (0x100)** — Demons. 10% chance to instantly halve target.

### 5.5 Skill-level distribution

```
SL 1:  Peasants, Sprites
SL 2:  Militia, Wolves, Skeletons, Zombies, Gnomes, Orcs, Archers
SL 3:  Elves, Pikemen, Nomads, Dwarves
SL 4:  Ghosts, Ogres, Barbarians, Trolls, Cavalry
SL 5:  Knights, Druids, Archmages, Vampires, Giants
SL 6:  Demons, Dragons
```

### 5.6 Dwelling-kind distribution

```
plains   (kind 0):  Peasants, Wolves, Nomads
forest   (kind 1):  Sprites, Gnomes, Elves, Druids
hill     (kind 2):  Orcs, Dwarves, Ogres, Barbarians, Trolls
dungeon  (kind 3):  Skeletons, Zombies, Ghosts, Archmages, Vampires, Giants, Demons, Dragons
castle:             Militia, Archers, Pikemen, Knights, Cavalry
```

### 5.7 Cost tier distribution (gp/unit)

```
1-100:    Peasants(10), Sprites(15), Wolves(40), Skeletons(40),
          Militia(50), Zombies(50), Gnomes(60), Orcs(75)
100-500:  Elves(200), Archers(250), Nomads(300), Pikemen(300),
          Dwarves(350), Ghosts(400)
500-1000: Druids(700), Ogres(750), Barbarians(750),
          Cavalry(800), Knights(1000), Trolls(1000)
1000+:    Archmages(1200), Vampires(1500), Giants(2000),
          Demons(3000), Dragons(5000)
```

---

## 6. Character classes and ranks

Defined in `assets/kings-bounty/game.json` `classes[]`. Each class
has up to 4 ranks. Stored in `ClassDef classes[CAT_CLASSES_MAX]`.

### 6.1 `ClassDef` and `RankDef` (`src/tables.h:91-115`)

```c
typedef struct {
    int  rank_index;
    char id[CAT_ID_LEN];
    char name[CAT_NAME_LEN];
    int  villains_needed;
    int  leadership;
    int  max_spells;
    int  spell_power;
    int  commission;
    bool knows_magic;
    int  instant_army;     // troop catalog index
} RankDef;

typedef struct {
    int  index;
    char id[CAT_ID_LEN];
    char name[CAT_NAME_LEN];
    char portrait[CAT_PATH_LEN];
    int  starting_gold;
    char starting_troops[CLASS_MAX_STARTING_TROOPS][CAT_ID_LEN];
    int  starting_counts[CLASS_MAX_STARTING_TROOPS];
    int  rank_count;
    RankDef ranks[CLASS_MAX_RANKS];
} ClassDef;
```

### 6.2 Knight (class 0)

```
starting_gold:    7500
starting_troops:  Militia × 20, Archers × 2

rank 0  Knight       villains_needed=0   lead=100   max_spells=2  spell_power=1  commission=1000  knows_magic=false  instant_army=Peasants(0)
rank 1  General      villains_needed=2   lead=200   max_spells=3  spell_power=2  commission=2000  knows_magic=false  instant_army=Militia(2)
rank 2  Marshal      villains_needed=8   lead=500   max_spells=4  spell_power=3  commission=4000  knows_magic=false  instant_army=Archers(8)
rank 3  Lord         villains_needed=14  lead=1000  max_spells=5  spell_power=5  commission=8000  knows_magic=false  instant_army=Knights(14)
```

### 6.3 Paladin (class 1)

```
starting_gold:    7500
starting_troops:  Militia × 20, Archers × 2

rank 0  Paladin           villains_needed=0   lead=80    max_spells=3  spell_power=1  commission=1000  knows_magic=false  instant_army=Peasants(0)
rank 1  Crusader          villains_needed=2   lead=160   max_spells=4  spell_power=2  commission=2000  knows_magic=false  instant_army=Militia(2)
rank 2  Avenger           villains_needed=8   lead=400   max_spells=5  spell_power=3  commission=4000  knows_magic=false  instant_army=Archers(8)
rank 3  Champion          villains_needed=14  lead=800   max_spells=6  spell_power=5  commission=8000  knows_magic=false  instant_army=Cavalry(18)
```

### 6.4 Sorceress (class 2)

```
starting_gold:    10000
starting_troops:  Sprites × 20, Gnomes × 4

rank 0  Sorceress         villains_needed=0   lead=60    max_spells=5  spell_power=1  commission=3000  knows_magic=true   instant_army=Sprites(1)
rank 1  Magician          villains_needed=3   lead=120   max_spells=8  spell_power=2  commission=4000  knows_magic=true   instant_army=Gnomes(6)
rank 2  Mage              villains_needed=8   lead=300   max_spells=11 spell_power=3  commission=5000  knows_magic=true   instant_army=Elves(9)
rank 3  Archmage          villains_needed=14  lead=600   max_spells=14 spell_power=5  commission=8000  knows_magic=true   instant_army=Druids(19)
```

### 6.5 Barbarian (class 3)

```
starting_gold:    7500
starting_troops:  Wolves × 20, Orcs × 2

rank 0  Barbarian         villains_needed=0   lead=100   max_spells=1  spell_power=1  commission=1000  knows_magic=false  instant_army=Peasants(0)
rank 1  Chieftain         villains_needed=2   lead=200   max_spells=2  spell_power=1  commission=2000  knows_magic=false  instant_army=Wolves(3)
rank 2  Warlord           villains_needed=8   lead=500   max_spells=3  spell_power=2  commission=4000  knows_magic=false  instant_army=Orcs(7)
rank 3  Overlord          villains_needed=14  lead=1000  max_spells=4  spell_power=3  commission=8000  knows_magic=false  instant_army=Knights(15)
```

### 6.6 Instant Army count formula

`(spell_power + 1) × tuning.instant_army_multiplier[rank]`
where `instant_army_multiplier = [3, 2, 1, 1]` (game.json `tuning`).

So Knight rank 0 with spell_power=1: `(1+1) × 3 = 6` Peasants.
Knight rank 3 with spell_power=5: `(5+1) × 1 = 6` Knights.

### 6.7 Promotion mechanics

`GameMaybeRankUp` (src/game.c) is called after each villain capture
from `FLOW_SIEGE_VILLAIN`. If `villains_caught_count >=
class.ranks[rank+1].villains_needed`, advance `rank_index`,
re-denormalize `rank_id`/`rank_title`, and apply the new rank's
leadership/max_spells/spell_power/commission to `Stats`.
`leadership_current` is bumped by the same delta as
`leadership_base`.

---

## 7. Villains — armies and rewards

Defined in `assets/kings-bounty/game.json` `villains[]`. 17 entries.

### 7.1 `VillainDef` struct (`src/tables.h`)

```c
typedef struct {
    int  index;                           // 0..16, OpenKB's villain id byte
    char id[CAT_ID_LEN];                  // "murray", "hack", ...
    char name[CAT_NAME_LEN];              // "Murray", ...
    char portrait[CAT_PATH_LEN];          // "villains/murr.png"
    char zone[RES_ID_LEN];                // "continentia" | "forestria" | "archipelia" | "saharia"
    int  reward;                          // gold reward when caught with the matching contract
    int  puzzle_cell;                     // 0..16, position in the puzzle map
    int  army_slots;
    char army_troop[GAME_ARMY_SLOTS][CAT_ID_LEN];
    int  army_count[GAME_ARMY_SLOTS];
} VillainDef;
```

### 7.2 Reward schedule

```
v00  Murray             5000
v01  Hack               6500
v02  Aimola             8500
v03  Baron Makahl       10000
v04  Dread Rob          11500
v05  Caneghor           13000
v06  Moradon            14500
v07  Barrowpine         16000
v08  Bargash            17500
v09  Rinaldus Drybone   19000
v0A  Ragface            20500
v0B  Mahk               22000
v0C  Auric Whiteskin    23500
v0D  Czar Nickolai      25000
v0E  Magus              26500
v0F  Urthrax            28000
v10  Arech              50000
```

(Reward = bountyland-modified; OpenKB has 5000..50000 in 5000
increments. OpenBounty keeps OpenKB's exact OpenKB values.)

### 7.3 Continent assignment

```
Continentia (6 villains): v00..v05
Forestria   (4 villains): v06, v07, v08, v09
Archipelia  (4 villains): v0A, v0B, v0C, v0D
Saharia     (3 villains): v0E, v0F, v10
```

This is set per-villain via the `zone` field in JSON. Counts must
equal `[6, 4, 4, 3]` (matches OpenKB `villains_per_continent`).

### 7.4 Villain placement

`salt_villains` (src/game.c) per OpenKB `salt_villains`:

```
For each zone z (in order):
  Count villains v whose v.zone == z.id.
  If z has fewer monster castles than that, log error and skip.
  For each villain in z (in id order):
    Pick a random monster-owned castle on z.
    Set its owner_kind = CASTLE_OWNER_VILLAIN, villain_id = v.id.
    Copy v.army_troop[]/army_count[] into castles[i].garrison[].
```

### 7.5 Contract flow

1. Player visits a town, picks `A) Get New Contract`.
2. `last_contract = (last_contract + 1) % cycle_length`.
3. `contract.active_id = cycle[last_contract]` (the villain id).
4. The contract view (`V` key) shows the villain's portrait, name,
   reward, and (if `castles[i].known` for that villain) "in
   <continent>" or "Unknown".
5. To fulfill: defeat the villain's castle. If
   `contract.active_id == captured_villain`, `GameFulfillContract`
   credits the reward, marks `villains_caught[i] = true`, and
   rotates `cycle[last_contract] = villain_id_of_max_contract`,
   then `max_contract = (max_contract + 1) % 17`.

### 7.6 Contract cycle rotation

Initial state: `last_contract = 4`, `max_contract = 5`,
`cycle = [v00, v01, v02, v03, v04]`. After fulfilling `v01`:

```
cycle[1] = v05    (the next villain)
last_contract still = 4
max_contract = 6
```

The next contract pick rotates: `last_contract = (4+1) % 5 = 0`,
issues `cycle[0] = v00` (assuming v00 still exists). And so on.

---

## 8. Dwellings, morale, and the morale chart

### 8.1 Dwelling kinds

Five kinds, four outdoor + one indoor:

```
plains  (TROOP_DWELL_PLAINS)   tile art "dwelling_plains"
forest  (TROOP_DWELL_FOREST)   tile art "dwelling_forest"
hill    (TROOP_DWELL_HILL)     tile art "dwelling_hills" (note plural in tiles)
dungeon (TROOP_DWELL_DUNGEON)  tile art "dwelling_dungeon"
castle  (special)              not placed as a dwelling tile
```

Tile interactive kinds:
`INTERACT_DWELLING_PLAINS`, `INTERACT_DWELLING_FOREST`,
`INTERACT_DWELLING_HILLS`, `INTERACT_DWELLING_DUNGEON`.

### 8.2 Continent-specific preferred troops

Each zone declares a `salt.preferred_troops[]` list and a
`salt.dwelling_range[2]` fallback. `salt_continent` consults the
list first by slot index; once exhausted, picks uniformly in
`[range_min, range_max]` (catalog indices).

```
Continentia: ["peasants","sprites","orcs","skeletons","wolves","gnomes"]
             range [0, 14]   (peasants .. knights)

Forestria:   ["dwarves","zombies","nomads","elves","ogres","elves"]
             range [1, 14]   (sprites .. knights)

Archipelia:  ["ghosts","barbarians","trolls","druids"]
             range [2, 14]   (militia .. knights)

Saharia:     ["giants","vampires","archmages","dragons","demons"]
             range [20, 24]  (archmages .. dragons)
```

These mirror OpenKB `continent_dwellings[]` and `dwelling_ranges[]`.

### 8.3 Morale chart

`combat.morale_chart[5][5]` (game.json `combat.morale_chart`).
Rows = host group, cols = candidate group:

```
       A  B  C  D  E
A      N  N  N  N  N
B      N  N  N  N  N
C      N  N  H  N  N
D      L  N  L  H  N
E      L  L  L  N  N
```

Letters: 'L' = low morale, 'N' = normal, 'H' = high. Matches
OpenKB `morale_chart[5][5]` (bounty.c:150).

Morale groups:
```
A  Smallfolk    Peasants, Militia
B  Lords        Archers, Pikemen, Knights, Cavalry
C  Creatures    Sprites, Gnomes, Elves, Nomads, Dwarves, Druids, Archmages
D  Predators    Wolves, Orcs, Ogres, Barbarians, Trolls, Giants
E  Undead       Skeletons, Zombies, Ghosts, Vampires, Demons, Dragons
```

### 8.4 Morale display

`army_slot_morale` (src/classic/views.c:213) computes per-slot
morale at view-army time. For each occupied slot, walk every other
occupied slot and look up `morale_chart[my_group][other_group]`. If
any 'L', return Low; if all 'H', return High; otherwise Normal.
Single-stack armies report High.

### 8.5 Salt-time dwelling placement

`salt_continent` SALT_DWELLING branch:

```
1. tid = salt_pick_dwelling_troop(g, continent, dwelling_counter)
   — preferred list first; then random in dwelling_range.
2. Look up troop, derive kind = troop->dwelling.
3. ik = dwelling_kind_to_interact(kind).
4. id = "sd_<troop>_<n>"
5. GameAddPlacement(zone, x, y, ik, id).
6. enforce_dwelling_pinned(zone, x, y, troop_id) — pre-create the
   DwellingState row with that troop.
```

### 8.6 Friendly foe placement

SALT_FRIENDLY branch: friendlies are pre-rolled, registered as
`FoeState` rows with `friendly = true`. Garrison is also pre-rolled
at salt time for save consistency, but the friendly accept flow
re-rolls a fresh creature on encounter (matching OpenKB).

### 8.7 Dwelling refresh

`GameApplyAstrology` (src/game.c:1517): each week, iterate
`dwellings[]`. If `troop_id == astrology_troop`, `count =
max_population`. Otherwise, **count is unchanged** (matches OpenKB).

### 8.8 Recruiting at dwellings

`screen_dwelling_open` opens VIEW_DWELLING (location backdrop). The
recruit prompt (`prompt_text_input_open`) shows `Cost=X each.
GP=Y. You may recruit up to N.` On commit, `GameBuyTroop` deducts
gold and adds to the army. The dwelling's `count` is decremented by
the purchased amount.

---

## 9. Artifacts and their powers

8 artifacts, defined in `assets/kings-bounty/game.json` `artifacts[]`.

### 9.1 `ArtifactDef` struct

```c
typedef struct {
    int  index;                       // 0..7
    char id[CAT_ID_LEN];              // "sword_of_prowess", ...
    char name[CAT_NAME_LEN];
    char effect[256];                 // flavor text shown at pickup
    int  power;                       // ARTIFACT_POWER_* bitmask
    char zone[RES_ID_LEN];            // continent
    int  local_idx;                   // 0 or 1 within the zone
} ArtifactDef;
```

### 9.2 Catalog

```
idx  id                       name              power                       zone        local_idx
00   sword_of_prowess         Sword of Prowess  INCREASED_DAMAGE            saharia     1
01   shield_of_protection     Shield of Protec. QUARTER_PROTECTION          forestria   0
02   crown_of_command         Crown of Command  DOUBLE_LEADERSHIP           archipelia  0
03   articles_of_nobility     Articles of Nobi. INCREASE_COMMISSION         continentia 1
04   amulet_of_augmentation   Amulet of Augm.   DOUBLE_SPELL_POWER          saharia     0
05   ring_of_heroism          Ring of Heroism   DOUBLE_LEADERSHIP           continentia 0
06   anchor_of_admiralty      Anchor of Admir.  CHEAPER_BOAT | NAVIGATION   forestria   1
07   book_of_necros           Book of Necros    DOUBLE_MAX_SPELLS           archipelia  1
```

### 9.3 Pickup logic

`GameClaimArtifact` (src/game.c) — applied at pickup:

- DOUBLE_LEADERSHIP: `leadership_base *= 2`, `leadership_current *= 2`.
- DOUBLE_SPELL_POWER: `spell_power *= 2`.
- INCREASE_COMMISSION: `commission_weekly *= 2`.
- DOUBLE_MAX_SPELLS: `max_spells *= 2`.

The other four (INCREASED_DAMAGE, QUARTER_PROTECTION, CHEAPER_BOAT,
NAVIGATION) are checked at runtime via `GameHasPower`.

### 9.4 Placement

`salt_continent` SALT_ARTIFACT branch: scan the artifact catalog
for entries matching `(zone, local_idx)`; for each artifact in
order, place the matching artifact id into the next available
chest slot tagged SALT_ARTIFACT.

### 9.5 Pickup tile id

Placement writes the artifact's catalog id (e.g.
`"ring_of_heroism"`) into the tile's `id` field. The pickup handler
(`adventure_handle_interact` for `INTERACT_ARTIFACT`) calls
`artifact_local_from_id` which first looks up the id in the artifact
catalog (returning `local_idx`) and falls back to trailing-digit
parsing for legacy `"artifact_N"` ids.

---

## 10. Spells

14 spells split exactly 7+7 between combat and adventure. Defined
in `assets/kings-bounty/game.json` `spells[]`.

### 10.1 Spell catalog

```
idx  id              name           kind        cost
00   clone           Clone          combat      2000
01   teleport        Teleport       combat      500
02   fireball        Fireball       combat      1500
03   lightning       Lightning      combat      500
04   freeze          Freeze         combat      300
05   resurrect       Resurrect      combat      2000
06   turn_undead     Turn Undead    combat      400
07   bridge          Bridge         adventure   100
08   time_stop       Time Stop      adventure   200
09   find_villain    Find Villain   adventure   1000
0A   castle_gate     Castle Gate    adventure   400
0B   town_gate       Town Gate      adventure   200
0C   instant_army    Instant Army   adventure   500
0D   raise_control   Raise Control  adventure   1000
```

(matches OpenKB `spell_costs[]` and `spell_kinds[]` in `bounty.c`.)

### 10.2 Spell purchase

In a town, `D) <Spell name> spell (<cost>)`. On confirm:

- If `known_spells >= max_spells` → "You have learned your maximum
  number of spells."
- Else if `gold <= cost` → "You don't have enough gold!" (`<=`,
  matching OpenKB; exact-match also fails).
- Else: `spells.counts[id]++`, `gold -= cost`, show "You can learn
  N more spells."

### 10.3 Spell distribution (`salt_spells`)

`salt_spells` (src/game.c) at GameInit:

1. **Pinned spells**: each town's `pinned_spell` JSON field forces a
   pre-assigned spell. Hunterville pins `bridge` (matches OpenKB
   `town_spell[0x15] = 0x07`).
2. **Eager fill**: walk towns; for each town not yet pinned, pick a
   random spell. If already used, retry.
3. **Random fallback**: if retries exceed budget, accept duplicates.
4. **Result**: stored per-town in `TownRecord.spell_for_sale`.

### 10.4 Spell casting workflow

`U` key opens VIEW_SPELLS. Player picks a spell (A..G). Combat
spells require an active battle (currently always fail because
combat is a stub); adventure spells dispatch to their handler:

- `bridge` → `cast_bridge_spell`: prompts for direction, builds up
  to 2 water-tile bridges in that direction.
- `time_stop` → `GameCastTimeStop`: `time_stop += spell_power × 10`,
  with a 10-step floor.
- `find_villain` → `cast_find_villain`: marks the contracted
  villain's castle as `known`, opens worldmap.
- `castle_gate` → `cast_castle_gate`: opens letter prompt; on letter,
  scans visited castles, switches zone, places hero at gate coords.
- `town_gate` → `cast_town_gate`: same but for towns.
- `instant_army` → `cast_instant_army`: picks the rank's
  `instant_army` troop, computes count per §6.6, calls
  `GameAddTroop` (free).
- `raise_control` → `cast_raise_control`: `leadership_current +=
  spell_power × 100`.

### 10.5 In-combat spell limit

`combat.spells` is incremented per cast in OpenKB (max 1/turn).
OpenBounty tracks this in the eventual combat state; currently
N/A.

---

## 11. The world — zones, castles, towns

### 11.1 Zones

Defined in `assets/kings-bounty/game.json` `zones[]`. OpenBounty
ships 4 zones (matches OpenKB).

```
id          name        map                                       size  is_home
continentia Continentia assets/kings-bounty/maps/continentia.dat 64×64 true
forestria   Forestria   assets/kings-bounty/maps/forestria.dat   64×64 false
archipelia  Archipelia  assets/kings-bounty/maps/archipelia.dat  64×64 false
saharia     Saharia     assets/kings-bounty/maps/saharia.dat     64×64 false
```

Each zone defines:

```c
typedef struct {
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char map_path[RES_PATH_LEN];
    int  width, height;
    int  hero_spawn_x, hero_spawn_y;
    int  neighbor_count;
    char neighbors[RES_MAX_NEIGHBORS][RES_ID_LEN];
    int  sign_count;     ResSign          signs[];
    int  town_count;     ResZoneTown      towns[];
    int  castle_count;   ResZoneCastle    castles[];
    int  chest_count;    ResZoneChest     chests[];
    int  artifact_count; ResZoneArtifact  artifacts[];
    int  dwelling_count; ResZoneDwelling  dwellings[];
    int  army_count;     ResZoneArmy      armies[];
    ResZoneSalt salt;             // budget + preferred troops + range
    bool is_home;
    int  home_spawn_x, home_spawn_y;
    int  magic_alcove_x, magic_alcove_y;   // -1 if none
} ResZone;
```

### 11.2 Castles

26 catalog entries (`game.json` `castles[]`). Each has:

```c
typedef struct {
    int  index;
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char zone[RES_ID_LEN];
    int  x, y;                    // tile coords
    int  gate_x, gate_y;          // tile player lands on (Castle Gate spell)
    int  difficulty_tier;         // 0..3
    ResCastleSpecial special;     // king's castle, audience flow
} ResCastle;
```

26 castles, one per A..Z letter (catalog ids start with that letter
for Castle Gate spell letter-matching). The 27th entry is King
Maximus, which is special-flow (audience).

Difficulty tiers (game.json):
```
0  Continentia  + king_maximus
1  forestria    Basefit, Duvock, Jhan, Quinderwitch, Yeneverre
1  archipelia   Tylitch, Xelox
2  forestria    Mooseweigh
2  archipelia   Endryx, Goobare, Hyppus, Lorsche
3  saharia      Spockana, Uzare, Zyzzarzaz
```

### 11.3 Towns

26 catalog entries (`game.json` `towns[]`). Each has:

```c
typedef struct {
    int  index;
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char zone[RES_ID_LEN];
    int  x, y;                    // tile coords
    int  boat_x, boat_y;          // -1 if no water adjacency
    int  gate_x, gate_y;          // -1 if not set
    char intel_castle[RES_ID_LEN];
    char pinned_spell[RES_ID_LEN]; // empty = no pin
} ResTown;
```

Hunterville pins `bridge`. No other towns pin in the default asset
pack.

### 11.4 Sailing

The sail neighbor list is per-zone (`neighbors[]` field, JSON).
When the player rents a boat at a town, the boat is placed at
`town.boat_x/y`. To sail to another zone, walk the boat to the
zone's edge; openbounty resolves the next zone via the
neighbor list.

### 11.5 Castle Gate spell

`cast_castle_gate` opens a letter prompt. On A..Z keypress:

```
For each visited castle ci:
  If castle.name[0] (lowercased) == letter:
    dest_zone = castle.zone
    dest_x = castle.gate_x (or castle.x if -1)
    dest_y = castle.gate_y (or castle.y+1)
    GameSwitchZone(dest_zone), set position.x/y, decrement spell.
```

### 11.6 Town Gate spell

Same as Castle Gate but matches against `res->towns[]` and uses
`town.gate_x/y` coords.

---

## 12. Tiles and map conventions

### 12.1 `Tile` struct (`src/map.h:15-26`)

```c
typedef struct {
    char     art[TILE_ART_NAME_LEN];      // sprite id ("water", "castle_roof")
    Terrain  terrain;                      // GRASS | FOREST | MOUNTAIN | WATER | DESERT
    Interact interactive;                  // INTERACT_NONE | INTERACT_TOWN | ...
    char     id[TILE_ID_LEN];              // overlay id (castle id, foe id, etc.)
    bool     blocks_foot;                  // castle walls etc.
    bool     is_bridge;                    // bridge tile
    char     sign_title[TILE_SIGN_TITLE_LEN];
    char     sign_body[TILE_SIGN_BODY_LEN];
    int      boat_spawn_x, boat_spawn_y;   // for towns
} Tile;
```

### 12.2 Terrain enum

```c
typedef enum {
    TERRAIN_GRASS,
    TERRAIN_FOREST,
    TERRAIN_MOUNTAIN,
    TERRAIN_WATER,
    TERRAIN_DESERT,
} Terrain;
```

`TerrainWalkable`: only GRASS and DESERT.

### 12.3 Interact enum (`src/tile.h`)

```c
INTERACT_NONE
INTERACT_CASTLE_GATE
INTERACT_TOWN
INTERACT_CHEST
INTERACT_SIGN
INTERACT_DWELLING_PLAINS
INTERACT_DWELLING_FOREST
INTERACT_DWELLING_HILLS
INTERACT_DWELLING_DUNGEON
INTERACT_FOE
INTERACT_ARTIFACT
INTERACT_NAVMAP
INTERACT_ORB
INTERACT_TELECAVE
INTERACT_ALCOVE
```

### 12.4 .dat file format

Plain text. First line is a `# header` comment (zone name, sometimes
metadata). Subsequent lines are one character per tile, one row per
line, top-to-bottom. Each character maps to a `tile_codes` entry
in `game.json` `tile_codes[]`.

### 12.5 Tile codes

`tile_codes` is a JSON array of objects, each with:

```c
typedef struct {
    bool present;
    char art[RES_TILE_ART_LEN];
    int  terrain;        // Terrain enum value
    bool blocks_foot;
    bool is_bridge;
} ResTileCode;
```

The character at index `c` in the array is what the `.dat` file uses.
Default mapping (game.json):

```
'.' grass         (terrain=GRASS, walkable)
'~' water         (terrain=WATER)
'*' forest        (terrain=FOREST, blocks_foot=true)
'^' mountain      (terrain=MOUNTAIN, blocks_foot=true)
'-' desert        (terrain=DESERT)
'=' bridge        (is_bridge=true)
'#' castle_wall   (blocks_foot=true)
```

(plus dozens of decoration tiles like `b` for water-edge variants.)

### 12.6 Walkability rules

`adventure_walkable_on_foot`:

- Interactive tile (any non-NONE) → walkable (steps onto trigger
  the interact handler).
- Bridge → walkable.
- Otherwise: `blocks_foot==false && TerrainWalkable(terrain)`.

`adventure_walkable_in_boat`:

- Bridge → walkable.
- Water → walkable.
- Otherwise: same as on-foot (counts as disembark).

`adventure_walkable_in_flight`:

- All terrain (water/grass/desert/forest/mountain) is walkable.
- Interactive overlays still aren't triggered mid-flight (per
  OpenKB).

---

## 13. Random generation — seeded RNG, chance tables, salting

### 13.1 RNG

`game_rng_state` (src/game.c:14) is an `unsigned long`. Java-style
LCG:

```c
static int game_rng_next(int min, int max) {
    game_rng_state = (game_rng_state * 25214903917 + 11) & 0xFFFFFFFFFFFFFFFFu;
    int r = (int)((game_rng_state >> 32) % (unsigned)(max - min + 1));
    return min + r;
}
```

Inclusive on both ends, matching `KB_rand` in OpenKB. Seeded from
`g->seed` at the start of `GameInit` and on save load (so the world
state is reproducible from the seed alone).

OpenBounty deliberately uses this LCG instead of libc `rand()` so
the world state is fully determined by `g->seed` regardless of
platform.

### 13.2 Chance curves (continent tier)

`res->spawn.chance_curve[tier][slot]` (parsed from
`game.json` `spawn.chance_curve`). 4 tiers × 5 slots:

```
tier 0 (cont 0):  20  50  70  90  100   // common low-tier
tier 1 (cont 1):  10  30  55  80  100
tier 2 (cont 2):   5  20  40  65  100
tier 3 (cont 3):   3  10  25  50  100   // rare-skewed
```

Walk: `chance = game_rng_next(1,100); slot = 0; while (chance >
curve[tier][slot]) slot++; troop = pool[kind][slot]`.

### 13.3 Spawn pool (kind → troop)

`res->spawn.troop_pool[kind][slot]` maps a kind (0..3) and pool
slot (0..4) to a troop id. Default (game.json):

```
plains  pool: peasants, wolves, nomads, ogres, knights
forest  pool: sprites, gnomes, elves, druids, knights
hill    pool: orcs, dwarves, ogres, barbarians, trolls
dungeon pool: skeletons, zombies, ghosts, archmages, vampires
```

`roll_creature` separates kind (random 0..3) from continent tier
(used for chance curve). See §17.5 in OPENKB-SPEC.

### 13.4 Chest event tables

`res->economy.chest` (game.json `economy.chest`):

```
chance_gold          50
chance_commission    65
chance_spell_power   80
chance_max_spells    80     (collision intentional, OpenKB §34.7)
chance_new_spell     95
gold_min             50
gold_max             1000
commission_points    100
spell_power_points   1
max_spells_points    1
```

Cumulative roll: `r = rng(1,100)`. r ≤ 50 → gold; ≤ 65 →
commission; ≤ 80 → spell_power; ≤ 80 → max_spells (the equal-
threshold makes max_spells the "effective never" outcome, matching
OpenKB §34.7); ≤ 95 → new_spell; else → empty.

### 13.5 Salting

`salt_continent` (src/game.c:483) per OpenKB `salt_continent`
(play.c:185-333). For each zone:

1. Build a barrel of N chest slots (zone's `chests[]`).
2. Tag the barrel with budget counts: `min_artifacts`, `min_navmaps`,
   `min_orbs`, `min_telecaves`, `min_dwellings`, `min_friendly`.
3. For each tagged slot, emit the appropriate placement.

Budgets per zone are read from `zone.salt`:

```
artifacts: 2
navmaps: 1
orbs: 1
telecaves: 2
dwellings: 10
friendly_foes: 5
```

(matches OpenKB `salt_continent(game, i, 2, 1, 1, 2, 10, 5)` in
spawn_game.)

---

## 14. Game creation — `GameInit` step by step

`GameInit(g, res, name, class_id, difficulty, seed)` (src/game.c).

```
1. Zero-fill *g; g->res = res; g->seed = seed; g->version = SAVE_VERSION.
2. Seed RNG: game_rng_seed(g->seed). Set g->scepter.key = rng(0,255);
   pick scepter_continent = rng(0,3); call bury_scepter(g, scepter_continent).
3. Character: copy name (uppercase first letter), copy class_id,
   set rank_index=0, denormalize rank_id/rank_title.
   stats.gold = class.starting_gold.
   stats.commission_weekly = class.ranks[0].commission.
   stats.leadership_base = stats.leadership_current = class.ranks[0].leadership.
   stats.spell_power = class.ranks[0].spell_power.
   stats.max_spells = class.ranks[0].max_spells.
   stats.knows_magic = class.ranks[0].knows_magic.
   stats.siege_weapons = 0.
   stats.days_left = res.time.days_per_difficulty[difficulty].
   stats.steps_left_today = res.time.day_steps.
   stats.last_commission = 0.
4. Position: scan zones for `is_home: true`; set position.zone, x, y
   from `home_spawn_x/y`. Fallback: use res.world.starting_zone +
   that zone's hero_spawn_x/y. Mark world.zones_discovered for the
   resolved home zone.
5. Mount, boat, last_x/y: mount = RIDE; boat.has_boat = false;
   boat.x/y = -1; position.last_x = position.x.
6. Rank init (no-op; class_stats_at_rank above already covered it).
7. Starting army: copy class.starting_troops[]/starting_counts[]
   into army[].
8. Options: stats.options[] = res.world.default_options[].
9. salt_spells(g) — assign per-town spells (pinned + random).
10. Alcove removal: if alcove and class already knows magic, mark
    the alcove tile as consumed.
11. salt_continent(g, zi, salt.artifacts, salt.navmaps, salt.orbs,
    salt.telecaves, salt.dwellings, salt.friendly_foes) per zone.
12. Castle init: copy res.castles[] ids into game.castles[].id;
    owner_kind = MONSTERS; clear garrison.
13. salt_villains(g) — assign 17 villains to monster castles per
    zone.
14. Repopulate non-villain monster castles (`repopulate_castle`).
15. Eager-populate dwelling state rows: for each declared dwelling
    in res.zones[].dwellings[] and for each placed dwelling in
    g->placements[] kind in DWELLING_*, call enforce_dwelling.
16. clear_fog around starting position.
```

### 14.1 Boat coords

OpenKB stores `boat_coords[town_id]` separately. OpenBounty stores
`boat_x/y` per town in `ResTown`. Initial state has no boat;
`boat.has_boat = false` until rented.

### 14.2 Initial worldmap state

Only the home zone's `zones_discovered[i]` is true. Other zones are
fog of war until a navmap chest reveals them or the player arrives
via boat.

### 14.3 Bury scepter

```c
void bury_scepter(Game *g, int continent) {
    Map *m = calloc(1, sizeof(Map));
    if (!MapLoadZone(m, g->res, zone_id)) { free(m); return; }
    Count grass tiles (TERRAIN_GRASS && interactive == NONE && !blocks_foot).
    target = rng(0, total_grass - 1).
    Walk the grid row-major; bury on the Nth grass tile.
    g->scepter.zone = zone.id; g->scepter.x = x; g->scepter.y = y.
    free(m).
}
```

OpenKB has a known bug here (`scepter_y = i`); openbounty does it
correctly (`scepter_y = j`).

---

## 15. Player actions

Adventure-screen actions, dispatched by `classic_input_poll`
(src/classic/input.c) → `ClassicInput.action`.

### 15.1 Movement

Arrow keys + numpad: 8 cardinal/diagonal directions.

```
KP_8 / Up           → (0, -1)
KP_9                → (1, -1)
KP_6 / Right        → (1, 0)
KP_3                → (1, 1)
KP_2 / Down         → (0, 1)
KP_1                → (-1, 1)
KP_4 / Left         → (-1, 0)
KP_7                → (-1, -1)
KP_5                → CL_ACTION_REST (rest 1 step in place, openbounty extension)
```

On move:
1. Compute `(nx, ny) = (x + dx, y + dy)`.
2. Walkability check via `adventure_walkable_*`.
3. If unwalkable: ignore.
4. If walkable: `last_x = x; last_y = y; x = nx; y = ny; facing_left = (dx < 0)`.
5. If destination is interactive: dispatch via
   `adventure_handle_interact`.
6. If interact sets `bounce_back`: `x = last_x; y = last_y` (hero
   stays in place after the dialog).
7. If not bounced: `GameOnStep` (decrement steps_left_today,
   handle desert, foes_follow).
8. `foes_follow`: each on-screen foe within 2 tiles tries to step
   one tile toward `last_x/last_y`. Land foes can't reach hero
   while in boat (water tile).

### 15.2 View keys

```
A      VIEW_ARMY
V      VIEW_CHARACTER
I      VIEW_CONTRACT
P      VIEW_PUZZLE
M      VIEW_WORLDMAP
U      VIEW_SPELLS  (spell cast)
C      VIEW_CONTROLS
O      VIEW_OPTIONS
N      navigate-continent prompt (when adjacent to ocean)
```

### 15.3 Action keys

```
S      Search Area: rng(1, 10) days, "Nothing to search" or "You found a chest"
W      End Week: skip to next week boundary (`GameSpendWeek`)
F      Cast Fly (mount->FLY) — if class can fly + mount permits
L      Land — if mount==FLY
D      Dismiss army (pick slot 1..5)
Q      Save+Quit (writes save, exits to menu)
Ctrl+Q Fast Quit (status-bar "Quit to DOS without saving (y/n)")
F10    Debug cheat menu (G/V/M/U/S/F/N/W/L)
```

### 15.4 Interact handlers

`adventure_handle_interact` (src/adventure.c):

```
INTERACT_CASTLE_GATE → opened_castle = true; bounce_back = true.
INTERACT_TOWN        → opened_town   = true; bounce_back = true.
INTERACT_CHEST       → take_chest;          bounce_back = true.
INTERACT_SIGN        → read sign;           bounce_back = true.
INTERACT_DWELLING_*  → opened_dwelling = true; bounce_back = true (most cases).
INTERACT_FOE         → opened_foe = true;
                        bounce_back = true if hostile, false if friendly.
INTERACT_ARTIFACT    → claim artifact;     bounce_back = true.
INTERACT_NAVMAP      → reveal next zone;   bounce_back = true.
INTERACT_ORB         → reveal full zone;   bounce_back = true.
INTERACT_TELECAVE    → teleport to paired telecave; bounce_back = false.
INTERACT_ALCOVE      → magic alcove;       bounce_back = true.
```

### 15.5 Search Area

Costs `tuning.search_cost_days` (default 10 days). Outcomes:

- 80%: nothing found.
- 20%: chest discovered (a chest tile is materialized at the hero's
  position); the chest opens automatically.

(matches OpenKB `ask_search`.)

### 15.6 End Week

`GameSpendWeek` advances `days_left` to the next multiple of
`week_days`, paying commission and applying astrology refresh.

### 15.7 Debug cheat menu (F10)

```
A    auto-battle toggle (no-op until combat lands)
G    +5000 gold
V    +100 leadership (base + current)
M    +1 spell_power, +1 max_spells
U    +1 of every spell
S    siege weapons granted
F    flight granted (mount = FLY)
N    reveal next undiscovered zone
W    instant win (calls show_win_game)
L    instant loss (calls show_lose_game)
```

---

## 16. Day/week cycle

### 16.1 `end_day`

`end_day` (src/game.c) — called once per step:

1. `days_left -= 1`. If 0: `game_over = true`.
2. `steps_left_today = res.time.day_steps`.
3. `time_stop = 0`.
4. If `days_left % week_days == 0`: trigger week-end logic.

### 16.2 Week boundary

```
1. astrology = GamePickAstrologyCreature(g, week_id):
     If week_id % 4 == 0: return 0 (peasants).
     Else: rng(0, 24).
2. last_astrology_troop = astrology.
3. GameApplyAstrology(g, astrology):
     For each dwelling: if troop_id == astrology.id, count =
     max_population. Otherwise unchanged.
4. Commission: stats.gold += commission_weekly. last_commission = amount.
5. Upkeep: deduct sum of (troop.recruit_cost / 10) × count over
   army stacks.
6. If gold < 0: gold = 0; player loses boat (boat.has_boat = false).
7. Castle-army astrology: for each player-castle garrison and each
   monster castle, if matches astrology, grow by growth_per_week.
8. Foe astrology: for each FoeState garrison, ditto.
9. Player castle re-populate: if all 5 garrison slots empty AND
   castle is player-owned, repopulate from res-defined troops.
   (OpenKB only checks slot 0; openbounty checks all 5 — minor
   deviation, more sensible.)
```

Display: `pump_week_end_dialog` opens a dialog showing
"Week of <troop>" + commission + budget summary, then dispatches
each line.

---

## 17. Combat engine

**Status:** stub. `RunCombatStub` (`src/combat.c`) opens a dialog
showing the defender garrison, then unconditionally returns
`COMBAT_RESULT_WIN`. The full combat engine is the next major work
item; the call sites (FLOW_SIEGE_MONSTER, FLOW_SIEGE_VILLAIN,
FLOW_ATTACK_FOE) and win-loss bookkeeping (castle ownership
transfer, contract fulfillment, rank-up, foe removal,
`perform_temp_death`) are wired and tested via the stub.

When the engine is implemented, this section should mirror
OPENKB-SPEC §17.1-§17.16 (battlefield, setup, turn structure,
damage formula, movement/flight/attack drivers, ranged shot,
compact_units, combat loop, victory/defeat, distance helpers,
unit_closest_offset, unit_fly_offset, random obstacles, castle
layout, spoils computation).

---

## 18. Spell effects in detail

### 18.1-18.7 Combat spells — N/A (combat stub)

Will be implemented with the combat engine. Costs and limits
already defined in `res->spells[]`.

### 18.8 Bridge

`cast_bridge_spell` (src/main.c). Opens a direction prompt. On
direction press, walks from the hero in that direction; for each
WATER tile, replaces it with a bridge tile (terrain stays WATER,
`is_bridge = true`). Cap: 2 water tiles per cast (matches OpenKB).
Spell count decremented only if at least 1 tile was bridged.

### 18.9 Time Stop

`GameCastTimeStop`: `time_stop += spell_power × 10`. OpenBounty
imposes a 10-step floor (minor deviation; OpenKB has no floor).

### 18.10 Find Villain

`cast_find_villain`: marks `castles[i].known = true` for the
contracted villain's castle. Opens VIEW_WORLDMAP showing the
revealed location.

### 18.11 Castle Gate

`cast_castle_gate`: opens letter prompt (header "Castle Gate").
A..Z keypress → letter-match against visited castles. On match,
`GameSwitchZone(zone)` then `position.x = gate_x; position.y =
gate_y`. Spell count decremented on success.

### 18.12 Town Gate

`cast_town_gate`: same as Castle Gate but for towns.

### 18.13 Instant Army

`cast_instant_army`: troop_id = `class.ranks[rank].instant_army`
(catalog index). count = `(spell_power + 1) ×
tuning.instant_army_multiplier[rank]`. `GameAddTroop(troop_id,
count)` (free, no leadership refusal). Refused only if no army
slot is available.

### 18.14 Raise Control

`cast_raise_control`: `leadership_current += spell_power × 100`.

### 18.15 Pick target

For combat spells (when implemented): a target picker that walks
the grid and applies a filter (enemy unit, ally unit, any unit,
empty cell). Currently N/A.

---

## 19. AI behavior

### 19.1 Foe follow (`GameFoesFollow`)

Implemented in `src/game.c:1811`. For each on-screen foe within 2
tiles of `(last_x, last_y)`:

1. Evaluate 9 cells in foe's 3×3 neighborhood (including stay-put).
2. Each cell's distance² is computed; non-center cells must be
   walkable (no obstacle, no other interactive overlay, walkable
   terrain).
3. Pick the cell with minimum distance to `(last_x, last_y)`.
4. If the foe steps onto the hero's tile (combat trigger), the
   hero must be reachable (not in boat on water).
5. Otherwise stamp the new tile as INTERACT_FOE.

### 19.2 Foe-vs-hero collision

If a foe steps onto the hero's tile:

- Hostile foe → hostile-attack flow (`start_foe_hostile_flow`).
- Friendly foe → friendly-accept flow (`start_foe_friendly_flow`).

### 19.3 Combat AI

N/A until combat engine implementation.

---

## 20. Chests, recruits, and rewards

### 20.1 Chest dispatch

`take_chest` (src/main.c) walks the cumulative-chance curve in
§13.4. On each outcome:

- **Gold**: `gold += rng(min, max)` (doubled if `DOUBLE_LEADERSHIP`
  power). Banner: "After scouring the area, you fall upon a hidden
  treasure cache. You may: A) Take the N gold. B) Distribute the
  gold to the peasants, increasing your leadership by L."
- **Commission**: `commission_weekly += commission_points` (default
  100). Banner: "After surveying the area, you discover that it is
  rich in mineral deposits."
- **Spell Power**: `spell_power += 1`. Banner: "Traversing the
  area, you stumble upon a time worn cannister."
- **Max Spells**: `max_spells += max_spells_points` (doubled if
  `DOUBLE_MAX_SPELLS`). Banner: "A tribe of nomads greet you and
  your army warmly."
- **New Spell**: pick a random spell, increment its count. Banner:
  "You have captured a mischevious imp..."
- **Empty**: "The chest was empty!"

### 20.2 Navmap chest

Sets `world.zones_discovered[next_zi] = true`. Banner: "You found
a navigation chart!" then names the next zone.

### 20.3 Orb chest

Reveals the entire current zone's fog. Sets
`world.orbs_found[zi] = true`. Banner: "Looking up, you see the
glistening of a magical orb..."

### 20.4 Alcove

Hill-cave tile with magic teaching. Costs `economy.alcove_cost` (60
gp). On confirm: deduct gold, set `knows_magic = true`, consume
the tile.

### 20.5 Telecave

Pairs of consecutive placements within a zone (0↔1, 2↔3). Stepping
on one teleports the hero to the paired one.

### 20.6 Friendly foe recruit

`start_foe_friendly_flow` rolls a fresh creature (random kind,
chance from continent tier), shows a yes/no accept prompt. On yes:
`GameAddTroop` (free, no gold cost, no leadership refusal). No-slot
case shows "They flee in terror at the sight of your vast army."

### 20.7 Spoils

After a combat win (when implemented): `gold += combat.spoils[1]`.
Spoils accumulate per side as `troops[id].spoils_factor × 5 ×
count` for every kill on the AI side.

---

## 21. Victory, defeat, scoring

### 21.1 Victory check

After all 17 villains caught and all 8 artifacts found, the puzzle
map is fully revealed. The win condition fires when the hero
stands on the scepter tile:

```c
on_scepter = (strcmp(scepter.zone, position.zone) == 0
              && position.x == scepter.x
              && position.y == scepter.y);
```

If `on_scepter`: `show_win_game(g, res, &classic_target,
&sprites)`.

### 21.2 Victory dialog

`show_win_game` calls `classic_run_end_cartoon` (the bridge-walk
cinematic), then pushes `VIEW_WIN`. The view shows:

- Header (formatted from `res.win_text.header` with `%NAME%`).
- Body (`res.win_text.body` with `%NAME%`, `%CLASS%`, `%RANK%`
  substitutions).
- Footer (`res.win_text.footer` with `%SCORE%`).
- Status bar: "Press 'ESC' to exit".

### 21.3 Defeat

`show_lose_game(g, res)`:

- If days run out: lose immediately.
- If a combat is lost: `perform_temp_death` (teleport home, wipe
  army, lose siege weapons, gain 20 peasants).

`show_lose_game` displays VIEW_LOSE with `res.lose_text.*`
substitutions.

### 21.4 Score

`GameComputeScore`:

```c
score = villains_caught × score.villain_coef       (default 500)
     + artifacts_found × score.artifact_coef      (default 250)
     + player_castles  × score.castle_coef        (default 100)
     - followers_killed × score.kill_penalty       (default 1)
score = max(score, 0)
if difficulty == EASY: score /= 2
else: score *= score.difficulty_multiplier[difficulty]
                                              (default {1,2,4,8})
```

---

## 22. Save file (JSON)

### 22.1 Format

JSON, written by `SaveGameWrite` (src/savegame.c). Save path
resolved by `savepath_get_dir`:

- Linux: `$XDG_DATA_HOME/openbounty/save_<slot>.dat` or
  `~/.local/share/openbounty/save_<slot>.dat`.
- Windows: `%APPDATA%\OpenBounty\save_<slot>.dat`.

(`.dat` extension despite the JSON content is intentional — matches
OpenKB save naming.)

### 22.2 Schema

Top-level keys:

```json
{
  "version": 1,
  "seed": <unsigned long>,
  "character": { name, class, rank, difficulty, mount },
  "stats": { gold, commission, leadership, ... },
  "position": { zone, x, y, last_x, last_y, travel_mode, hud_visible },
  "army": [ { id, count }, × 5 ],
  "spells": [ count, × 14 ],
  "contract": { active, cycle, last, max, villains_caught },
  "artifacts": { found: [bool, × 8] },
  "world": { zones_discovered, orbs_found, puzzle_revealed },
  "boat": { has_boat, x, y, zone },
  "towns":   [ { id, visited, spell_for_sale }, × 26 ],
  "castles": [ { id, visited, known, owner_kind, villain_id, garrison }, × 26 ],
  "scepter": { zone, x, y },
  "consumed": [ { zone, x, y }, × N ],
  "dwellings": [ { zone, x, y, troop_id, count, max_population }, × N ],
  "placements": [ { zone, x, y, kind, id }, × N ],
  "foes": [ { zone, x, y, placement_id, garrison, alive, friendly }, × N ]
}
```

(`scepter.key` from OpenKB binary saves is computed but not
serialized.)

### 22.3 Load behavior

`SaveGameRead` zero-fills the `Game`, walks the JSON, populates
every field. After loading, `MapLoadZoneWithPlacements` is called
on `position.zone` and `GameApplyTileMutations` is run.
`FogReveal` runs around the saved position.

---

## 23. UI — screens, menus, layout

### 23.1 Adventure layout

```
┌───────────────────────────────────────────────┐
│ TopBox (status bar)                           │  CL_STATUS_Y, CL_STATUS_H
├──────────────────────────────────┬────────────┤
│                                  │            │
│   Map viewport (5×5 tiles)       │  Sidebar   │  CL_MAP/SIDEBAR
│   CL_MAP_X..CL_MAP_X+CL_MAP_W    │            │
│                                  │            │
├──────────────────────────────────┴────────────┤
│ Bottom panel (CL_PANEL_*)                      │  Active during view+dialog+prompt
└───────────────────────────────────────────────┘
```

Top status: "Days Left: N / Score: N / Press 'ESC' to exit" depending
on context.

Sidebar shows: gold, commission, current zone name, artifacts grid,
selected hero portrait.

### 23.2 Bottom panel (CL_PANEL_*)

Fixed rect: `CL_PANEL_X, CL_PANEL_Y, CL_PANEL_W, CL_PANEL_H` (160
× 80 px at 2× DOS resolution). Yellow border, DBLUE interior.
Used by every persistent menu screen (home castle, recruit
soldiers, town, dwelling, alcove, dialog box, prompts).

### 23.3 Persistent screens

```
VIEW_HOME_CASTLE    Home castle: A=Recruit, B=Audience
VIEW_OWN_CASTLE     Player-owned castle: Garrison/Remove
VIEW_RECRUIT_SOLDIERS  King Maximus barracks: A..E + count input
VIEW_DWELLING       Outdoor dwelling: yes/no recruit
VIEW_ALCOVE         Hill-cave magic alcove
VIEW_TOWN           Town menu: A..E (Contract / Boat / Info / Spell / Siege)
VIEW_WIN            Victory screen
VIEW_LOSE           Defeat screen
VIEW_ARMY           View army (5 rows)
VIEW_CHARACTER      View character (gold, leadership, spells, etc.)
VIEW_CONTRACT       View contract (active villain portrait + bounty)
VIEW_PUZZLE         Puzzle map (5×5 grid)
VIEW_WORLDMAP       Full continent map
VIEW_OPTIONS        Adventure key bindings
VIEW_CONTROLS       Controls menu (delay, sounds, walk-beep, etc.)
VIEW_SPELLS         Spell-book column-picker
VIEW_MENU           Esc/save/load/quit hub (openbounty extension)
```

### 23.4 Modal dialogs

`open_dialog(header, body)`. Yellow header, white body. Spinning
cursor in bottom-right. Pages content if body exceeds 7 visible
lines. Dismissed by any key (advance or final).

### 23.5 Modal prompts

`prompt_yes_no_open(header, body)`, `prompt_numeric_open`,
`prompt_ab_open`, `prompt_text_input_open`. Replace the bottom
panel; cursor input handled per-prompt-type.

### 23.6 HUD overlay

Floating top-of-map overlay showing army snapshot, days left,
gold. Toggled by Tab (openbounty extension). Persists in save
(`hud_visible`).

### 23.7 Town screen layout

Per OpenKB `visit_town` (game.c:2585):

- Header row 1: `Town of <name>` (yellow, slightly above panel top).
- Header row 2: `GP=<gold/1000>K` (right-aligned).
- Row A: `A) Get New Contract`
- Row B: `B) Rent boat (<N> week)` or `B) Cancel boat rental`
- Row C: `C) Gather information`
- Row D: `D) <Spell name> spell (<cost>)`
- Row E: `E) Buy siege weapons (<cost>)`

Popups (insufficient gold, vacate-first, gather-info text) replace
the menu in-place using the same DBLUE/YELLOW panel rect — match
OpenKB `KB_BottomBox` semantics.

### 23.8 Recruit Soldiers screen

Per OpenKB `recruit_soldiers` (game.c:2121):

- Backdrop: random castle-class troop animated at 4-frame idle.
- Bottom panel left column: `Recruit Soldiers` header (slightly
  above panel top), then 5 rows `A) <Troop>     <cost>`. Rows
  whose troop's HP×6 exceeds total leadership show "n/a" in place
  of cost — and pressing that letter is rejected (matches DOS
  binary behavior, beyond OpenKB which silently allows over-
  selection).
- Right column: `GP=<gold/1000>K`, then `(A-C) <letter>` (or twirl
  cursor), `Max=<count>`, `How Many`, count input cursor.

### 23.9 Audience-with-king flow

OpenKB issues two `KB_BottomBox` calls:

1. Fanfare: header empty, body "Trumpets announce your arrival
   with regal fanfare. King Maximus rises from his throne to
   greet you and proclaims:           (space)".
2. King's message (after space): header is the actual message
   (with `%NAME%` / `%RANK%` / `%NEEDED%` substitutions), body
   empty.

OpenBounty replicates this via two-stage dialog: opens fanfare
first, stashes the king's message, the generic dialog-dismiss
path opens the second when the stash is non-empty.

---

## 24. Verbatim strings

All gameplay strings live in `assets/kings-bounty/game.json`
under `strings.banners` (~120 entries) and `strings.ui`. Loaded
into `Resources.banners` and `Resources.ui` at startup.

### 24.1 Notable strings (verbatim)

```
town_no_gold:           "\n\n\nYou don't have enough gold!"  (3 leading newlines = MSG_PADDED)
town_boat_vacate_first: "\n\nPlease vacate the boat first"   (2 leading newlines)
no_troop_slots:         "No troop slots left!"
chest_empty:            "The chest was empty!"
spell_unavailable:      "You don't know any of these spells."
encounter_join_named:   "%LABEL% %TROOP%,\nwith desires of greater\nglory, wish to join you."
encounter_join_numeric: "%COUNT% %TROOP%,\nwith desires of greater\nglory, wish to join you."
encounter_wanderers:    "They flee in terror at the\nsight of your vast army."
spell_bridge_built:     "%COUNT% bridge tile(s) appeared!"
spell_gate_teleported:  "You have been teleported."
spell_gate_invalid:     "There is no such destination."
spell_raise_control_success: "Your leadership is increased by %AMOUNT%."
spell_instant_army_success: "%QTY% %TROOP%\nhave joined your army."
combat_placeholder:     "(combat is not yet implemented)"
```

(Full list: ~120 entries; see `game.json` `strings.banners`.)

### 24.2 Templates and substitution

Banners use `%TOKEN%` placeholders. `resources_format_template`
walks the source, expands tokens via the `vars[]` array. Recognized
tokens vary per banner — common ones: `%NAME%`, `%CLASS%`,
`%RANK%`, `%GOLD%`, `%COST%`, `%COUNT%`, `%TROOP%`, `%LABEL%`,
`%SCORE%`, `%AMOUNT%`, `%NEEDED%`, `%S%` (plural suffix).

### 24.3 Count buckets

For "fuzzy" army-size display (when `options[4] == 1`),
`resources_count_bucket_label` walks a sorted `[threshold, label]`
list. Default `count_buckets_army_view`:

```
threshold=1     "One"
threshold=2     "A few"
threshold=9     "Several"
threshold=∞     "Many"
```

(Per-flow bucket lists for instant-army, encounter, dismiss-army,
etc., separately defined in `game.json` `count_buckets`.)

---

## 25. Strings sourced from KB.EXE

**N/A.** OpenBounty does not load KB.EXE. The categories OpenKB
sources from KB.EXE are present in `game.json` instead:

- Villain names: `villains[].name`.
- Villain descriptions: `strings.villain_descriptions[<id>]`
  (alias / features / crimes).
- Artifact names + effects: `artifacts[].name`, `artifacts[].effect`.
- Sign text: per-tile `sign_title`/`sign_body` in zones JSON.
- Win/lose text: `strings.win` / `strings.lose`.
- Credits: `credits[]` (minimal; user preference).

---

## 26. Colors — VGA palette, color schemes

### 26.1 Palette

OpenBounty ships a single 768-byte VGA palette at
`assets/kings-bounty/data/palette.bin`. 256 entries × 3 bytes
(R,G,B). Loaded once at startup (`palette_init`).

The first 16 entries match OpenKB's EGA palette indices:

```
0  BLACK     #000000     8  GREY      #555555
1  BLUE      #0000AA     9  LBLUE     #5555FF
2  GREEN     #00AA00     A  LGREEN    #55FF55
3  CYAN      #00AAAA     B  LCYAN     #55FFFF
4  RED       #AA0000     C  LRED      #FF5555
5  MAGENTA   #AA00AA     D  LMAGENTA  #FF55FF
6  BROWN     #AA5500     E  YELLOW    #FFFF55
7  LGREY     #AAAAAA     F  WHITE     #FFFFFF
```

Aliases: `DGREEN = GREEN (#00AA00)`, `DBLUE = BLUE (#0000AA)`.

`PAL_CLR(NAME)` (src/classic/palette.h) returns a raylib `Color`
struct for the named entry.

### 26.2 Color schemes

OpenBounty does not separate "color schemes" the way OpenKB does
(VIEWCHAR / DWELLING / etc.). Each renderer hard-codes the colors
it uses. Common patterns:

- Headers: YELLOW.
- Body text: WHITE.
- Highlighted/cursor: YELLOW (otherwise WHITE).
- Panel borders: YELLOW.
- Panel interiors: DBLUE.
- "Out of Control" warning: RED.

### 26.3 Minimap terrain colors

`res->colors.minimap_terrain` (game.json `colors.minimap_terrain`):

```
grass       #55FF55  (LGREEN, bright)
forest      #00AA00  (DGREEN, dark)
mountain    #AA5500  (BROWN)
water       #5555FF  (LBLUE)
desert      #FFFF55  (YELLOW)
fog         #000000  (BLACK)
```

(Matches OpenKB `COL_MINIMAP` with grass=GREEN bright and
tree=DGREEN dark.)

---

## 27. Resource system

### 27.1 `Resources` struct

Defined in `src/resources.h`. Single struct holding every parsed
catalog and string, loaded once from `game.json` at startup.

```c
typedef struct Resources {
    char title[64];
    int  troops_count;          TroopDef troops[];
    int  spells_count;          SpellDef spells[];
    int  classes_count;         ClassDef classes[];
    int  artifacts_count;       ArtifactDef artifacts[];
    int  villain_count;         VillainDef villains[];
    int  villain_desc_count;    ResVillainDesc villain_descs[];
    int  zone_count;            ResZone zones[];
    int  town_count;            ResTown towns[];
    int  castle_count;          ResCastle castles[];
    int  tile_code_count;       ResTileCode tile_codes[256];
    ResWindow window;
    ResUI ui;
    ResBanners banners;
    ResEconomy economy;
    ResTime time;
    ResWorld world;
    ResScore score;
    ResTuning tuning;
    ResColors colors;
    ResSpawn spawn;
    ResEnd win_text;
    ResEnd lose_text;
    ResControlsTable controls;
    ResCountBucketTable count_buckets;
    ...
} Resources;
```

### 27.2 Loading

`resources_load(res, path)` (src/resources.c) opens the file with
`fopen`, reads the entire content, parses with cJSON, then walks
the parse tree populating each catalog. Failures emit to stderr
and return false.

### 27.3 Asset embedding

Release builds (`make release`) embed every file under `assets/`
as a C array via `scripts/embed_assets.sh` → `build/embedded.c`.
At runtime, `assets_open(path)` returns the embedded blob (release)
or opens the disk file (dev).

---

## 28. Module system

**N/A.** OpenBounty ships a single asset pack (`assets/kings-bounty/`).
There is no module discovery, no `KB_Resolve` indirection, no
chain-of-responsibility loader. If multi-pack support becomes
desirable, the `Resources` struct would need to be augmented with
a chain of catalog pointers.

---

## 29. DOS asset formats

**N/A.** OpenBounty ships PNG sprites, JSON tile maps (text), and a
raw 768-byte VGA palette. No `.CC` packs, no `.4`/`.16`/`.256`
sprite sheets, no KB.EXE, no PC-speaker tunes. The `legacy/scripts/`
directory contains Python helpers used to extract original DOS
assets for reference at port time, but no runtime parser exists.

---

## 30. Font

`src/bfont.c` ships an 8×8 bitmap font loaded from
`assets/kings-bounty/art/font/kb-font.png`. The PNG sheet is
16 columns × 6 rows (96 glyphs covering printable ASCII 0x20-0x7F).

Special codepoints used by OpenKB-style status indicators:
- `\x05` — `/` (forward slash twirl)
- `\x1C` — `\` (backslash twirl)
- `\x1D` — `|` (pipe twirl)
- `\x1F` — `-` (hyphen twirl)

`bfont_init` copies the printable glyphs into those slots so OpenKB
literal twirl strings render identically.

API:
```c
void bfont_init(const char *path);
void bfont_draw(const char *text, int x, int y, Color c);
Vector2 bfont_measure(const char *text);
```

---

## 31. Keybindings

### 31.1 Adventure bindings

Hardcoded in `classic_input_poll` (src/classic/input.c):

```
Arrows / Numpad   → 8-direction movement (KP_5 = REST)
A                 → CL_ACTION_VIEW_ARMY
V                 → CL_ACTION_VIEW_CHARACTER
I                 → CL_ACTION_VIEW_CONTRACT
P                 → CL_ACTION_VIEW_PUZZLE
M                 → CL_ACTION_VIEW_MAP
U                 → CL_ACTION_CAST_SPELL
C                 → CL_ACTION_VIEW_CONTROLS
O                 → CL_ACTION_VIEW_OPTIONS
N                 → CL_ACTION_NAVIGATE (sail-to prompt)
S                 → CL_ACTION_SEARCH
W                 → CL_ACTION_END_WEEK
F                 → CL_ACTION_FLY
L                 → CL_ACTION_LAND
D                 → CL_ACTION_DISMISS_ARMY
Q                 → CL_ACTION_SAVE_QUIT
Ctrl+Q            → fast-quit prompt (handled inline in main.c)
F10               → debug cheat menu (handled inline in main.c)
Tab               → toggle HUD (openbounty extension)
```

### 31.2 Combat bindings

N/A until combat engine implementation. Intended:

```
Arrows / Numpad  → move/attack
S                → shoot
F                → fly
W                → wait
G                → give up
U                → cast spell
Space            → pass
A                → view army
V                → view character
C                → controls
F10              → cheat menu
Esc              → ESC to give up confirmation
```

### 31.3 View bindings

Per-view: ESC dismisses; A-Z / digits select within the view.

---

## 32. Configuration

### 32.1 CLI args

`main()` parses:

```
--version  / -v       print "openbounty" and exit
--help     / -h       print usage and exit
--fullscreen          start fullscreen
--asset <path>        override default game.json path
```

No INI file. No `--datadir` / `--savedir` / `--rootdir` —
asset path is hardcoded relative to CWD (or override via
`--asset`); save path is platform-derived.

### 32.2 Runtime options

Stored in `Stats.options[6]`, persisted in saves:

```
options[0]  delay         (animation speed)
options[1]  sounds        (0/1)
options[2]  walk_beep     (0/1)
options[3]  animation     (0/1)
options[4]  army_size     (0=exact, 1=fuzzy)
options[5]  cga           (always 0; openbounty has no CGA)
```

Default: `[4, 1, 1, 1, 1, 0]`. Set in `world.default_options`.

### 32.3 Controls menu

VIEW_CONTROLS shows 6 rows (delay/sounds/walk_beep/animation/
army_size/cga). Player can cycle each option. CGA is hidden by
default via `controls.settings.cga.hidden = true`.

---

## 33. Multiplayer combat — N/A

OpenBounty does not include OpenKB's `combat.c` multiplayer
combat emulator. There is no networking, no SDL_net dependency,
no packet protocol.

---

## 34. Known bugs and incomplete features

### 34.1 Combat engine — stub

`RunCombatStub` returns WIN. Castle siege, villain siege, and foe
attack flows all complete cleanly via the stub but no actual
battle plays out.

### 34.2 Combat spells — N/A

The 7 combat spells (Clone, Teleport, Fireball, Lightning, Freeze,
Resurrect, Turn Undead) cannot be cast because there is no combat
state to target. They appear in the spellbook and can be
purchased; casting in adventure mode opens the spell view but
combat-typed spells cannot be selected.

### 34.3 Time Stop floor

OpenBounty imposes a 10-step floor on the Time Stop spell that
OpenKB does not. Minor deviation.

### 34.4 Astrology dwelling refresh

OpenKB had non-matching dwellings *grow* by `growth_per_week` each
week. OpenBounty now matches OpenKB exactly: only the matching
dwelling refills to `max_population`; non-matching dwellings keep
their current count.

### 34.5 Out-of-control display

`view_army` correctly flags per-stack OOC using OpenKB's per-troop
`army_leadership` formula (Bug fixed earlier this session).

### 34.6 Recruit "n/a" gating

Recruit screen now refuses pressing letters whose troop is
unreachable (DOS binary behavior, fixed earlier this session;
spec §20.3).

### 34.7 Pikemen cost

OpenBounty uses 300 (DOS-original); OpenKB inherited 800. See
spec §5.7. This is the only catalog-data deviation between
openbounty and OpenKB.

### 34.8 No siege-weapons gate

OpenKB-faithful: `siege_weapons` purchase doesn't gate castle
attacks. The flag exists, can be bought, but `lay_siege` doesn't
check it.

### 34.9 Save format binary parity

OpenBounty uses JSON; OpenKB uses 20,421-byte binary. JSON saves
are not interchangeable with OpenKB or DOS saves.

---

## 35. Appendices

### 35.1 Difficulty multipliers

`score.difficulty_multiplier = [1, 2, 4, 8]` for [EASY, NORMAL,
HARD, IMPOSSIBLE]. EASY also halves the score.

### 35.2 Days per difficulty

`time.days_per_difficulty = [900, 600, 400, 200]`.

### 35.3 Starting armies

```
Knight    7500 gp,  Militia × 20, Archers × 2
Paladin   7500 gp,  Militia × 20, Archers × 2
Sorceress 10000 gp, Sprites × 20, Gnomes × 4
Barbarian 7500 gp,  Wolves × 20,  Orcs × 2
```

### 35.4 Per-class instant_army troops

Per OpenKB (matches openbounty `class.ranks[].instant_army`):

```
Knight     Peasants(0),   Militia(2),   Archers(8),  Knights(0xE)
Paladin    Peasants(0),   Militia(2),   Archers(8),  Cavalry(0x12)
Sorceress  Sprites(1),    Gnomes(6),    Elves(9),    Druids(0x13)
Barbarian  Peasants(0),   Wolves(3),    Orcs(7),     Knights(0xF)
```

### 35.5 Castle difficulty tiers

```
0   Continentia + king_maximus
1   forestria/Basefit, Duvock, Jhan, Quinderwitch, Yeneverre
1   archipelia/Tylitch, Xelox
2   forestria/Mooseweigh
2   archipelia/Endryx, Goobare, Hyppus, Lorsche
3   saharia/Spockana, Uzare, Zyzzarzaz
```

### 35.6 Astrology week mapping

```
week % 4 == 0: Peasants (always)
otherwise:     rng(0, 24)  uniform random troop
```

### 35.7 Save slot layout

Slots 0..2 (3 save slots). Each slot is one JSON file
(`save_<slot>.dat`) under the platform save directory.

---

## 36. Tools

`tools/` contains:

- `extract.c` (+ `extract_*.c`, `extract_output_map.inc`) — pure-C
  asset extractor. Reads a user's KB.EXE / 256.CC / 416.CC and
  produces the full `assets/kings-bounty/` tree (320 PNGs, 4 maps,
  4 WAVs, palette, font). Built by `make extract`.
- `playtest.c` (+ `playtest_lib.{c,h}`, `scenario.{c,h}`,
  `scenarios/*.json`) — scenario harness driver. Boots the engine
  via socket, replays JSON-described input sequences, asserts on
  state. Built into `build/openbounty-playtest` and run by
  `make test`.

---

## End

This spec describes OpenBounty as of commit `HEAD` on
2026-04-26. Update this document whenever code changes
significantly.

---

## Appendix A — Verbatim source extracts

This appendix reproduces key implementation functions verbatim from
`src/`. Each block is annotated with `file:line` so the spec can be
cross-checked against the source. When the source changes, this
appendix is updated to match.

### A.1 RNG (`src/game.c:14-29`)

```c
static unsigned long game_rng_state = 0;

static void game_rng_seed(unsigned long seed) {
    game_rng_state = seed ^ 0x5DEECE66DUL;
}

static int game_rng_next(int min, int max) {
    if (min > max) return min;
    if (min == max) return min;
    game_rng_state = (game_rng_state * 25214903917UL + 11UL)
                     & 0xFFFFFFFFFFFFFFFFUL;
    unsigned int result = (unsigned int)(game_rng_state >> 32);
    return min + (result % (max - min + 1));
}
```

This is a 64-bit Java-style LCG. Inclusive on both endpoints,
matching OpenKB's `KB_rand`. The seed XOR with `0x5DEECE66DUL` is the
Java LCG initial-state constant. Results derived from the upper 32
bits avoid LCG low-bit periodicity.

### A.2 GameInit (full sequence)

`src/game.c:50-298`. Complete 16-step spawn:

```c
void GameInit(Game *g, const char *name, int pclass, int difficulty,
              const unsigned char *land) {
    int i;
    const Resources *res_saved = g->res;
    memset(g, 0, sizeof(*g));
    g->res = res_saved;
    (void)land;

    // Step 2 (play.c:385-388): Hide scepter.
    game_rng_seed(g->seed);
    g->scepter.key = game_rng_next(0, 255);
    int scepter_continent = game_rng_next(0, 3);
    bury_scepter(g, scepter_continent);

    // Step 3 (play.c:390-400): Character name, class, difficulty,
    // days, gold.
    if (name && name[0]) {
        strncpy(g->character.name, name, sizeof(g->character.name) - 1);
        g->character.name[sizeof(g->character.name) - 1] = '\0';
        if (g->character.name[0] >= 'a' && g->character.name[0] <= 'z')
            g->character.name[0] = (char)(g->character.name[0] - 'a' + 'A');
    } else {
        const char *dn = (g->res && g->res->world.default_name[0])
            ? g->res->world.default_name : "Hero";
        strncpy(g->character.name, dn, sizeof(g->character.name) - 1);
        g->character.name[sizeof(g->character.name) - 1] = '\0';
    }

    g->character.difficulty = difficulty;

    const ClassDef *cls = (pclass >= 0 && pclass < 4)
                          ? class_by_index(pclass)
                          : class_by_index(0);
    if (!cls) cls = class_by_index(0);
    copy_id(g->character.cls.id, sizeof(g->character.cls.id), cls->id);
    g->character.cls.rank_index = 0;
    copy_id(g->character.cls.rank_id,    sizeof(g->character.cls.rank_id),
            cls->ranks[0].id);
    copy_id(g->character.cls.rank_title, sizeof(g->character.cls.rank_title),
            cls->ranks[0].name);

    int lead = 0, maxsp = 0, spp = 0, comm = 0;
    class_stats_at_rank(cls, 0, &lead, &maxsp, &spp, &comm);
    g->stats.gold               = cls->starting_gold;
    g->stats.commission_weekly  = comm;
    g->stats.leadership_base    = lead;
    g->stats.leadership_current = lead;
    g->stats.spell_power        = spp;
    g->stats.max_spells         = maxsp;
    g->stats.knows_magic        = cls->ranks[0].knows_magic;
    g->stats.siege_weapons      = 0;

    int di = (difficulty >= 0 && difficulty < 4) ? difficulty : 0;
    g->stats.days_left          = g->res ? g->res->time.days_per_difficulty[di] : 900;
    g->stats.steps_left_today   = g->res ? g->res->time.day_steps : 40;
    g->stats.last_commission    = 0;

    // Step 4 (play.c:402-405): Starting position.
    g->position.zone[0] = '\0';
    g->position.x = g->position.y = 0;
    int home_zone_index = -1;
    if (g->res) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            if (!z->is_home) continue;
            copy_id(g->position.zone, sizeof(g->position.zone), z->id);
            g->position.x = z->home_spawn_x;
            g->position.y = z->home_spawn_y;
            home_zone_index = zi;
            break;
        }
        if (!g->position.zone[0]) {
            copy_id(g->position.zone, sizeof(g->position.zone),
                    g->res->world.starting_zone);
            for (int zi = 0; zi < g->res->zone_count; zi++) {
                const ResZone *z = &g->res->zones[zi];
                if (strcmp(z->id, g->res->world.starting_zone) == 0) {
                    g->position.x = z->hero_spawn_x;
                    g->position.y = z->hero_spawn_y;
                    home_zone_index = zi;
                    break;
                }
            }
        }
        if (home_zone_index >= 0)
            g->world.zones_discovered[home_zone_index] = true;
    }

    // Step 5 (play.c:407-410): Mount, boat, last position.
    g->character.mount = MOUNT_RIDE;
    g->boat.has_boat = false;
    g->boat.x = -1; g->boat.y = -1;
    g->position.last_x = g->position.x;
    g->position.last_y = g->position.y;

    // Step 6 (play.c:412-415): Rank init.
    g->character.cls.rank_index = 0;
    player_accept_rank(g);
    g->stats.time_stop = 0;

    // Step 7 (play.c:417-424): Contract cycle.
    g->contract.active_id[0] = '\0';
    int cycle_len = g->res ? g->res->contract.cycle_length : 5;
    if (cycle_len < 1) cycle_len = 1;
    if (cycle_len > CONTRACT_CYCLE_MAX) cycle_len = CONTRACT_CYCLE_MAX;
    g->contract.last_contract = g->res
                              ? g->res->contract.initial_last_contract
                              : cycle_len - 1;
    g->contract.max_contract  = cycle_len;
    for (i = 0; i < cycle_len; i++) {
        const VillainDef *v = villain_by_index(i);
        if (v) copy_id(g->contract.cycle[i],
                       sizeof(g->contract.cycle[i]), v->id);
        else   g->contract.cycle[i][0] = '\0';
    }

    // Step 8 (play.c:426-433): Starting army.
    for (i = 0; i < 2; i++) {
        const char *troop = cls->starting_troops[i];
        int count = cls->starting_counts[i];
        if (!troop || count <= 0) continue;
        copy_id(g->army[i].id, sizeof(g->army[i].id), troop);
        g->army[i].count = count;
    }

    // Step 9 (play.c:435-441): Default options.
    for (int oi = 0; oi < 6; oi++)
        g->stats.options[oi] = g->res ? g->res->world.default_options[oi] : 1;

    // Step 10 (play.c:444): Salt spells.
    salt_spells(g);

    // Step 11 (play.c:447-449): Remove alcove if class already knows magic.
    if (g->stats.knows_magic && g->res) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            if (z->magic_alcove_x < 0 || z->magic_alcove_y < 0) continue;
            GameAddConsumed(g, z->id, z->magic_alcove_x, z->magic_alcove_y);
        }
    }

    // Step 12 (play.c:452-458): salt_continent per zone.
    if (g->res) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            salt_continent(g, zi,
                           z->salt.artifacts, z->salt.navmaps,
                           z->salt.orbs,      z->salt.telecaves,
                           z->salt.dwellings, z->salt.friendly_foes);
        }
    }

    // Step 13 (play.c:461-464): Castle init (all monster-owned).
    {
        int ncastles = g->res ? g->res->castle_count : 0;
        if (ncastles > GAME_CASTLES) ncastles = GAME_CASTLES;
        for (i = 0; i < ncastles; i++) {
            const ResCastle *rc = &g->res->castles[i];
            CastleRecord *cr = &g->castles[i];
            copy_id(cr->id, sizeof(cr->id), rc->id);
            cr->visited = false;
            cr->known = false;
            cr->owner_kind = CASTLE_OWNER_MONSTERS;
            cr->villain_id[0] = '\0';
            for (int sl = 0; sl < GAME_ARMY_SLOTS; sl++) {
                cr->garrison[sl].id[0] = '\0';
                cr->garrison[sl].count = 0;
            }
        }
    }

    // Step 14 (play.c:467-470): salt_villains.
    salt_villains(g);

    // Step 15 (play.c:473-477): Repopulate non-villain monster castles.
    for (i = 0; i < GAME_CASTLES; i++) {
        if (!g->castles[i].id[0]) continue;
        if (g->res && i < g->res->castle_count &&
            g->res->castles[i].special.excluded_from_contract) {
            g->castles[i].owner_kind = CASTLE_OWNER_SPECIAL;
            continue;
        }
        if (g->castles[i].owner_kind == CASTLE_OWNER_MONSTERS)
            repopulate_castle(g, i);
    }

    // Step 16: Eager-populate dwellings from declared + salted sources.
    if (g->res) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            for (int di = 0; di < z->dwelling_count; di++) {
                const ResZoneDwelling *rd = &z->dwellings[di];
                enforce_dwelling(g, z->id, rd->x, rd->y, rd->kind);
            }
        }
    }
    for (int pi = 0; pi < g->placement_count; pi++) {
        const SaltedPlacement *p = &g->placements[pi];
        const char *kind = NULL;
        switch ((Interact)p->kind) {
            case INTERACT_DWELLING_PLAINS:  kind = "plains";  break;
            case INTERACT_DWELLING_FOREST:  kind = "forest";  break;
            case INTERACT_DWELLING_HILLS:   kind = "hills";   break;
            case INTERACT_DWELLING_DUNGEON: kind = "dungeon"; break;
            default: continue;
        }
        enforce_dwelling(g, p->zone, p->x, p->y, kind);
    }

    clear_fog(g);   // marker only; fog reveal happens in main.c
}
```

### A.3 salt_spells (`src/game.c:339-394`)

```c
void salt_spells(Game *g) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    int nspells = spells_count();
    int ntowns  = res->town_count;
    if (ntowns > GAME_TOWNS) ntowns = GAME_TOWNS;
    if (nspells <= 0 || ntowns <= 0) return;

    // Eagerly create TownRecord entries.
    for (int i = 0; i < ntowns; i++) {
        const ResTown *rt = &res->towns[i];
        TownRecord *tr = &g->towns[i];
        copy_id(tr->id, sizeof(tr->id), rt->id);
        tr->visited = false;
        tr->spell_for_sale[0] = '\0';
    }

    // Step 1: pinned spells.
    bool spell_claimed[CAT_SPELLS_MAX] = {0};
    for (int i = 0; i < ntowns; i++) {
        const char *pin = res->towns[i].pinned_spell;
        if (!pin[0]) continue;
        const SpellDef *sp = spell_by_id(pin);
        if (!sp) continue;
        copy_id(g->towns[i].spell_for_sale,
                sizeof(g->towns[i].spell_for_sale), sp->id);
        if (sp->index >= 0 && sp->index < CAT_SPELLS_MAX)
            spell_claimed[sp->index] = true;
    }

    // Step 2: random fill of unclaimed spells.
    for (int s = 0; s < nspells; ) {
        if (s < CAT_SPELLS_MAX && spell_claimed[s]) { s++; continue; }
        int t = game_rng_next(0, ntowns - 1);
        if (g->towns[t].spell_for_sale[0] == '\0') {
            const SpellDef *sp = spell_by_index(s);
            if (sp) copy_id(g->towns[t].spell_for_sale,
                            sizeof(g->towns[t].spell_for_sale), sp->id);
            s++;
        }
    }

    // Step 3: any still-empty town gets a random spell.
    for (int i = 0; i < ntowns; i++) {
        if (g->towns[i].spell_for_sale[0]) continue;
        int s = game_rng_next(0, nspells - 1);
        const SpellDef *sp = spell_by_index(s);
        if (sp) copy_id(g->towns[i].spell_for_sale,
                        sizeof(g->towns[i].spell_for_sale), sp->id);
    }
}
```

### A.4 salt_continent (`src/game.c:502-660`)

```c
typedef enum {
    SALT_NONE = 0,
    SALT_ARTIFACT,
    SALT_NAVMAP,
    SALT_ORB,
    SALT_TELECAVE,
    SALT_DWELLING,
    SALT_FRIENDLY,
} SaltKind;

void salt_continent(Game *g, int continent, int min_artifacts,
                    int min_navmaps, int min_orbs, int min_telecaves,
                    int min_dwellings, int min_friendly) {
    if (!g || !g->res) return;
    if (continent < 0 || continent >= g->res->zone_count) return;

    const ResZone *z = &g->res->zones[continent];

    // Hostile foes from static armies[].
    for (int i = 0; i < z->army_count; i++) {
        const ResZoneArmy *a = &z->armies[i];
        const char *aid = (a->id[0]) ? a->id : NULL;
        char fallback[32];
        if (!aid) {
            snprintf(fallback, sizeof(fallback), "static_foe_%d", i);
            aid = fallback;
        }
        add_foe(g, continent, z->id, a->x, a->y, aid, /*friendly=*/false);
    }

    int barrel_len = z->chest_count;
    int min_len = min_artifacts + min_navmaps + min_orbs +
                  min_telecaves + min_dwellings + min_friendly;
    if (min_len == 0) return;
    if (barrel_len < min_len) {
        fprintf(stderr,
                "salt_continent: zone '%s' has %d chests, need %d. Skipping.\n",
                z->id, barrel_len, min_len);
        return;
    }

    SaltKind *barrel = (SaltKind *)calloc((size_t)barrel_len, sizeof(SaltKind));
    if (!barrel) return;

    #define TAG_N(count, kind) do {                                     \
        int _placed = 0;                                                \
        int _guard = 0;                                                 \
        while (_placed < (count) && _guard < barrel_len * 20) {         \
            int _bi = game_rng_next(0, barrel_len - 1);                 \
            if (barrel[_bi] == SALT_NONE) {                             \
                barrel[_bi] = (kind);                                   \
                _placed++;                                              \
            }                                                           \
            _guard++;                                                   \
        }                                                               \
    } while (0)

    TAG_N(min_artifacts, SALT_ARTIFACT);
    TAG_N(min_navmaps,   SALT_NAVMAP);
    TAG_N(min_orbs,      SALT_ORB);
    TAG_N(min_telecaves, SALT_TELECAVE);
    TAG_N(min_dwellings, SALT_DWELLING);
    TAG_N(min_friendly,  SALT_FRIENDLY);

    #undef TAG_N

    int artifact_counter = 0;
    int telecave_counter = 0;
    int dwelling_counter = 0;
    int foe_counter      = 0;
    int orb_counter      = 0;
    int navmap_counter   = 0;

    for (int i = 0; i < barrel_len; i++) {
        const ResZoneChest *slot = &z->chests[i];
        char id[32];
        switch (barrel[i]) {
            case SALT_ARTIFACT: {
                int ac = g->res->artifacts_count;
                int aidx = -1;
                for (int j = 0; j < ac; j++) {
                    const ArtifactDef *cand = artifact_by_index(j);
                    if (cand &&
                        strcmp(cand->zone, z->id) == 0 &&
                        cand->local_idx == artifact_counter) {
                        aidx = j; break;
                    }
                }
                if (aidx < 0) { artifact_counter++; break; }
                const ArtifactDef *a = artifact_by_index(aidx);
                const char *aid = (a && a->id[0]) ? a->id : "";
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_ARTIFACT, aid);
                artifact_counter++;
                break;
            }
            case SALT_NAVMAP:
                snprintf(id, sizeof(id), "navmap_%d", navmap_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_NAVMAP, id);
                navmap_counter++;
                break;
            case SALT_ORB:
                snprintf(id, sizeof(id), "orb_%d", orb_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_ORB, id);
                orb_counter++;
                break;
            case SALT_TELECAVE:
                snprintf(id, sizeof(id), "telecave_%d", telecave_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y,
                                 INTERACT_TELECAVE, id);
                telecave_counter++;
                break;
            case SALT_DWELLING: {
                const char *tid = salt_pick_dwelling_troop(g, continent,
                                                           dwelling_counter);
                if (!tid) { dwelling_counter++; break; }
                const TroopDef *td = troop_by_id(tid);
                if (!td) { dwelling_counter++; break; }
                Interact ik = dwelling_kind_to_interact(td->dwelling);
                snprintf(id, sizeof(id), "sd_%.20s_%d", tid, dwelling_counter);
                GameAddPlacement(g, z->id, slot->x, slot->y, ik, id);
                enforce_dwelling_pinned(g, z->id, slot->x, slot->y, tid);
                dwelling_counter++;
                break;
            }
            case SALT_FRIENDLY:
                snprintf(id, sizeof(id), "salt_foe_friendly_%d", foe_counter);
                add_foe(g, continent, z->id, slot->x, slot->y, id, true);
                foe_counter++;
                break;
            case SALT_NONE:
            default:
                break;
        }
    }

    free(barrel);
}
```

### A.5 bury_scepter (`src/game.c:682-732`)

```c
void bury_scepter(Game *g, int continent) {
    if (!g || !g->res) return;
    if (continent < 0 || continent >= g->res->zone_count) return;
    const ResZone *z = &g->res->zones[continent];
    copy_id(g->scepter.zone, sizeof(g->scepter.zone), z->id);
    g->scepter.x = -1; g->scepter.y = -1;

    Map *m = (Map *)calloc(1, sizeof(Map));
    if (!m) return;
    if (!MapLoadZone(m, g->res, z->id)) { free(m); return; }

    int total = 0;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (t->terrain == TERRAIN_GRASS &&
                t->interactive == INTERACT_NONE && !t->blocks_foot)
                total++;
        }
    }
    if (total <= 0) { free(m); return; }
    int target = game_rng_next(0, total - 1);
    int count = 0;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (t->terrain != TERRAIN_GRASS) continue;
            if (t->interactive != INTERACT_NONE) continue;
            if (t->blocks_foot) continue;
            if (count == target) {
                g->scepter.x = x; g->scepter.y = y;
                free(m); return;
            }
            count++;
        }
    }
    free(m);
}
```

### A.6 roll_creature (`src/game.c`)

```c
static void roll_creature(Game *g, int tier,
                          char *out_id, size_t out_id_sz, int *out_count) {
    out_id[0] = '\0';
    if (out_count) *out_count = 0;
    if (!g || !g->res) return;
    if (tier < 0 || tier >= RES_SPAWN_TIERS) tier = 0;

    const ResSpawn *sp = &g->res->spawn;

    int kind = game_rng_next(0, 3);
    int chance = game_rng_next(1, 100);
    int slot = 0;
    while (slot < RES_SPAWN_POOL_N - 1 &&
           chance > sp->chance_curve[tier][slot]) {
        slot++;
    }
    const char *troop_id = sp->troop_pool[kind][slot];
    if (!troop_id[0]) return;

    const TroopDef *t = troop_by_id(troop_id);
    copy_id(out_id, out_id_sz, troop_id);

    int count = (t ? t->tier_counts[tier] : 0);
    if (count <= 1) count = 2;
    if (out_count) *out_count = count;
}
```

### A.7 roll_hostile_garrison (`src/game.c:457-484`)

```c
static void roll_hostile_garrison(const Game *g, int continent, Unit *out) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        out[i].id[0] = '\0'; out[i].count = 0;
    }
    if (!g || !g->res) return;
    int continent_tier = continent & 3;
    int stacks = 1 + game_rng_next(0, 2);
    if (stacks > GAME_ARMY_SLOTS) stacks = GAME_ARMY_SLOTS;
    for (int s = 0; s < stacks; s++) {
        int kind = game_rng_next(0, 3);
        int chance = game_rng_next(1, 100);
        int pool_slot = 0;
        while (pool_slot < RES_SPAWN_POOL_N - 1 &&
               chance > g->res->spawn.chance_curve[continent_tier][pool_slot])
            pool_slot++;
        const char *tid = g->res->spawn.troop_pool[kind][pool_slot];
        if (!tid || !tid[0]) continue;
        const TroopDef *td = troop_by_id(tid);
        if (!td) continue;
        int base = td->tier_counts[continent_tier];
        if (base < 2) base = 2;
        int jitter = game_rng_next(0, base / 2);
        copy_id(out[s].id, sizeof(out[s].id), tid);
        out[s].count = base + jitter;
    }
}
```

### A.8 GameFoesFollow (`src/game.c:1811`)

```c
int GameFoesFollow(Game *g, Map *map) {
    if (!g || !map) return -1;
    int tx = g->position.last_x;
    int ty = g->position.last_y;
    int collided = -1;
    for (int i = 0; i < g->foe_count; i++) {
        FoeState *f = &g->foes[i];
        if (!f->alive) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        int diff_x = f->x - tx; if (diff_x < 0) diff_x = -diff_x;
        int diff_y = f->y - ty; if (diff_y < 0) diff_y = -diff_y;
        if (diff_x > 2 || diff_y > 2) continue;

        const unsigned SENTINEL = 0xFFFFFFFFu;
        unsigned best_dist = SENTINEL;
        int best_x = f->x, best_y = f->y;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = f->x + dx, ny = f->y + dy;
                if (!MapInBounds(map, nx, ny)) continue;
                bool is_center = (dx == 0 && dy == 0);
                bool hero_reachable = (nx == g->position.x &&
                                       ny == g->position.y &&
                                       g->travel_mode != TRAVEL_BOAT);
                if (!is_center && !hero_reachable &&
                    !foe_can_stand(map, nx, ny))
                    continue;
                unsigned d = foe_dist_sq(nx, ny, tx, ty);
                if (d < best_dist) {
                    best_dist = d; best_x = nx; best_y = ny;
                }
            }
        }
        if (best_x == f->x && best_y == f->y) continue;

        MapClearInteractive(map, f->x, f->y);
        f->x = best_x; f->y = best_y;

        if (best_x == g->position.x && best_y == g->position.y) {
            collided = i;
            continue;
        }

        Tile *dst = &map->tiles[best_y][best_x];
        dst->interactive = INTERACT_FOE;
        size_t k = 0;
        while (k + 1 < sizeof(dst->id) && f->placement_id[k]) {
            dst->id[k] = f->placement_id[k]; k++;
        }
        dst->id[k] = '\0';
    }
    return collided;
}
```

### A.9 GameApplyAstrology (`src/game.c:1517`)

```c
const char *GameApplyAstrology(Game *g, int troop_idx) {
    if (!g) return "";
    const TroopDef *at = troop_by_index(troop_idx);
    if (!at) return "";

    // OpenKB play.c:1024-1026: only refill matching dwellings.
    // Non-matching keep their current population.
    for (int i = 0; i < g->dwelling_count; i++) {
        DwellingState *d = &g->dwellings[i];
        if (!d->troop_id[0]) continue;
        if (strcmp(d->troop_id, at->id) == 0)
            d->count = d->max_population;
    }

    // Week of Peasants: ABSORB-ability stacks convert to peasants.
    if (troop_idx == 0) {
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!g->army[s].id[0] || g->army[s].count == 0) continue;
            const TroopDef *t = troop_by_id(g->army[s].id);
            if (!t) continue;
            if (t->abilities & TROOP_ABIL_ABSORB)
                copy_id(g->army[s].id, sizeof(g->army[s].id), at->id);
        }
    }
    return at->id;
}
```

### A.10 GameMaxRecruitable (`src/game.c:1431`)

```c
int GameMaxRecruitable(const Game *g, const char *troop_id) {
    if (!g || !troop_id) return 0;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t || t->hit_points <= 0) return 0;
    int free_leadership = g->stats.leadership_current;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0) {
            free_leadership -= t->hit_points * g->army[i].count;
            break;
        }
    }
    if (free_leadership < 0) return 0;
    return free_leadership / t->hit_points;
}
```

Mirrors OpenKB `army_leadership` exactly: only the slot already
holding `troop_id` is subtracted (not all other slots). This means
recruiting a brand-new troop ignores the leadership consumed by
other stacks — the "you can over-recruit" OpenKB-faithful quirk.

### A.11 GameBuyTroop (`src/game.c:1603`)

```c
int GameBuyTroop(Game *g, const char *troop_id, int count) {
    if (!g || !troop_id || count <= 0) return 2;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t) return 2;
    int total_cost = t->recruit_cost * count;
    if (g->stats.gold < total_cost) return 1;
    if (count > GameMaxRecruitable(g, troop_id)) return 3;

    int slot = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0]) { slot = i; break; }
        }
    }
    if (slot < 0) return 2;
    copy_id(g->army[slot].id, sizeof(g->army[slot].id), troop_id);
    g->army[slot].count += count;
    g->stats.gold -= total_cost;
    return 0;
}
```

Returns: 0 success, 1 not enough gold, 2 no slot, 3 leadership exceeded.

### A.12 GameAddTroop (`src/game.c:1628`)

```c
int GameAddTroop(Game *g, const char *troop_id, int count) {
    if (!g || !troop_id || count <= 0) return 1;
    int slot = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0 &&
            g->army[i].count > 0) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0] || g->army[i].count == 0) {
                slot = i; break;
            }
        }
    }
    if (slot < 0) return 1;
    copy_id(g->army[slot].id, sizeof(g->army[slot].id), troop_id);
    g->army[slot].count += count;
    return 0;
}
```

Free-add (no gold deduction, no leadership refusal). Used by Instant
Army spell, friendly-foe acceptance, and `temp_death`'s 20-peasant
restoration. Returns: 0 success, 1 no slot.

### A.13 enforce_dwelling (`src/game.c:1376`)

```c
static DwellingState *enforce_dwelling(Game *g, const char *zone,
                                       int x, int y,
                                       const char *dwelling_kind) {
    if (!g || !zone || !zone[0]) return NULL;
    for (int i = 0; i < g->dwelling_count; i++) {
        if (g->dwellings[i].x == x && g->dwellings[i].y == y &&
            strcmp(g->dwellings[i].zone, zone) == 0)
            return &g->dwellings[i];
    }
    if (g->dwelling_count >= GAME_MAX_DWELLINGS) return NULL;
    DwellingState *d = &g->dwellings[g->dwelling_count++];
    memset(d, 0, sizeof(*d));
    copy_id(d->zone, sizeof(d->zone), zone);
    d->x = x; d->y = y;
    const TroopDef *t = GameDwellingTroopAt(g,
                            dwelling_kind_normalize(dwelling_kind), x, y);
    if (t) {
        copy_id(d->troop_id, sizeof(d->troop_id), t->id);
        d->max_population = t->max_population;
        d->count          = t->max_population;
    }
    return d;
}
```

Idempotent: returns existing row on re-touch without overwriting.

### A.14 GameDwellingTroopAt (`src/game.c:1308`)

```c
const TroopDef *GameDwellingTroopAt(const Game *g, const char *dwelling_kind,
                                    int x, int y) {
    if (!g || !dwelling_kind || !dwelling_kind[0]) return NULL;
    int cand[CAT_TROOPS_MAX]; int n = 0;
    int total = troops_count();
    for (int i = 0; i < total; i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t) continue;
        if (strcmp(t->dwelling, dwelling_kind) == 0) {
            cand[n++] = i;
            if (n >= (int)(sizeof(cand)/sizeof(cand[0]))) break;
        }
    }
    if (n == 0) return NULL;
    unsigned h = (unsigned)g->seed;
    h ^= (unsigned)(x * 131u);
    h ^= (unsigned)(y * 97u);
    h = h * 1664525u + 1013904223u;
    int pick = (int)(h % (unsigned)n);
    return troop_by_index(cand[pick]);
}
```

Deterministic pick keyed by `(seed, x, y)`. Used by
`enforce_dwelling` for visit-time dwellings (where the troop wasn't
pre-pinned at salt time).

### A.15 cast_instant_army (`src/main.c:1520`)

```c
static void cast_instant_army(Game *g) {
    int idx = spell_index_by_id("instant_army");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;

    const ResBanners *bn = &g->res->banners;
    char msg[RES_BANNER_LEN];

    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_fizzle, NULL, 0);
        open_dialog(spell_header("instant_army", "Instant Army"), msg);
        return;
    }
    int rank = g->character.cls.rank_index;
    if (rank < 0 || rank >= cls->rank_count) rank = 0;
    int troop_idx = cls->ranks[rank].instant_army;
    const TroopDef *troop = troop_by_index(troop_idx);
    if (!troop || !troop->id[0]) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_fizzle, NULL, 0);
        open_dialog(spell_header("instant_army", "Instant Army"), msg);
        return;
    }

    const ResTuning *tn = &g->res->tuning;
    int mult_rank = (rank >= 0 && rank < 4) ? rank : 0;
    int multiplier = tn->instant_army_multiplier[mult_rank];
    if (multiplier < 1) multiplier = 1;
    int count = (g->stats.spell_power + 1) * multiplier;
    if (count < 1) count = 1;

    int rc = GameAddTroop(g, troop->id, count);
    if (rc != 0) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_no_room, NULL, 0);
        open_dialog(spell_header("instant_army", "Instant Army"), msg);
        return;
    }

    g->spells.counts[idx]--;
    const ResUI *ui = &g->res->ui;
    const char *qty = resources_count_bucket_label(
        ui->count_buckets_instant_army,
        ui->count_buckets_instant_army_n,
        count, "");
    ResTemplateVar vars[] = {
        { "QTY",   qty },
        { "TROOP", troop->name },
    };
    resources_format_template(msg, sizeof msg,
                              bn->spell_instant_army_success, vars, 2);
    open_dialog(spell_header("instant_army", "Instant Army"), msg);
}
```

### A.16 cast_raise_control (`src/main.c:1601`)

```c
static void cast_raise_control(Game *g) {
    int idx = spell_index_by_id("raise_control");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    g->spells.counts[idx]--;
    int amount = g->stats.spell_power * 100;
    g->stats.leadership_current += amount;
    char msg[RES_BANNER_LEN], abuf[16];
    snprintf(abuf, sizeof abuf, "%d", amount);
    ResTemplateVar vars[] = { { "AMOUNT", abuf } };
    resources_format_template(msg, sizeof msg,
                              g->res->banners.spell_raise_control_success,
                              vars, 1);
    open_dialog(spell_header("raise_control", "Raise Control"), msg);
}
```

### A.17 GameCastTimeStop (`src/game.c:301`)

```c
void GameCastTimeStop(Game *g) {
    if (!g) return;
    int idx = spell_index_by_id("time_stop");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    g->spells.counts[idx]--;
    int bonus = g->stats.spell_power * 10;
    if (bonus < 10) bonus = 10;
    g->stats.time_stop += bonus;
}
```

### A.18 GameCastFindVillain (`src/game.c:314`)

```c
void GameCastFindVillain(Game *g) {
    if (!g) return;
    int idx = spell_index_by_id("find_villain");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    if (!g->contract.active_id[0]) return;
    g->spells.counts[idx]--;
    for (int i = 0; i < GAME_CASTLES; i++) {
        if (!g->castles[i].id[0]) continue;
        if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
        if (strcmp(g->castles[i].villain_id, g->contract.active_id) != 0)
            continue;
        g->castles[i].known = true;
        return;
    }
}
```

### A.19 cast_castle_gate (`src/main.c`)

The Castle Gate flow has three parts: opening the prompt, polling
the letter, dispatching the teleport.

Opening:
```c
static void cast_castle_gate(Game *g) {
    int idx = spell_index_by_id("castle_gate");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    char msg[RES_BANNER_LEN];
    resources_format_template(msg, sizeof msg,
                              g->res->banners.spell_castle_gate_choose,
                              NULL, 0);
    open_dialog(spell_header("castle_gate", "Castle Gate"), msg);
    gate_state = GATE_STATE_SELECT;
    gate_mode  = 0;   // castle gate
}
```

Letter dispatch (in main-loop frame handler):
```c
} else if (gate_state == GATE_STATE_SELECT) {
    int key = GetKeyPressed();
    if (key == 0) { /* no input */ }
    else if (key == KEY_ESCAPE) {
        gate_state = GATE_STATE_NONE;
        dialog_dismiss();
    } else if (key >= KEY_A && key <= KEY_Z) {
        char letter = (char)('a' + (key - KEY_A));
        bool valid = false;
        const char *dest_zone = NULL;
        int dest_x = -1, dest_y = -1;
        int spell_idx = spell_index_by_id(
            gate_mode == 0 ? "castle_gate" : "town_gate");

        if (gate_mode == 0) {
            for (int ci = 0; ci < GAME_CASTLES; ci++) {
                if (!game.castles[ci].visited) continue;
                if (ci >= game.res->castle_count) continue;
                const ResCastle *rc = &game.res->castles[ci];
                char first = (char)tolower((unsigned char)
                    (rc->name[0] ? rc->name[0] : rc->id[0]));
                if (first != letter) continue;
                if (!rc->zone[0]) continue;
                valid = true;
                dest_zone = rc->zone;
                dest_x = rc->gate_x >= 0 ? rc->gate_x : rc->x;
                dest_y = rc->gate_y >= 0 ? rc->gate_y : rc->y;
                break;
            }
        } else {
            int tn = game.res->town_count;
            if (tn > GAME_TOWNS) tn = GAME_TOWNS;
            for (int ti = 0; ti < tn; ti++) {
                if (!game.towns[ti].visited) continue;
                const ResTown *rt = &game.res->towns[ti];
                char first = (char)tolower((unsigned char)
                    (rt->name[0] ? rt->name[0] : rt->id[0]));
                if (first != letter) continue;
                if (!rt->zone[0]) continue;
                valid = true;
                dest_zone = rt->zone;
                dest_x = rt->gate_x >= 0 ? rt->gate_x : rt->x;
                dest_y = rt->gate_y >= 0 ? rt->gate_y : rt->y;
                break;
            }
        }

        const ResBanners *bn = &game.res->banners;
        const char *header = spell_header(
            gate_mode == 0 ? "castle_gate" : "town_gate",
            gate_mode == 0 ? "Castle Gate" : "Town Gate");
        char msg[RES_BANNER_LEN];
        if (valid && dest_zone &&
            GameSwitchZone(&game, &map, &fog, dest_zone)) {
            if (spell_idx >= 0) game.spells.counts[spell_idx]--;
            if (dest_x >= 0 && dest_y >= 0) {
                game.position.x = dest_x; game.position.y = dest_y;
                game.position.last_x = dest_x; game.position.last_y = dest_y;
            }
            resources_format_template(msg, sizeof msg,
                                      bn->spell_gate_teleported, NULL, 0);
            dialog_dismiss();
            gate_state = GATE_STATE_NONE;
            open_dialog(header, msg);
        } else {
            resources_format_template(msg, sizeof msg,
                                      bn->spell_gate_invalid, NULL, 0);
            dialog_dismiss();
            gate_state = GATE_STATE_NONE;
            open_dialog(header, msg);
        }
    }
}
```

### A.20 Audience-with-king (`src/main.c`)

`audience_substitute` (template expansion):

```c
static void audience_substitute(const Game *game, int needed,
                                const char *src, char *out, size_t out_sz) {
    if (out_sz == 0) return;
    char *dst = out;
    char *end = out + out_sz - 1;
    while (*src && dst < end) {
        if (*src == '%') {
            const char *tok = src + 1;
            const char *close = strchr(tok, '%');
            if (close) {
                int len = (int)(close - tok);
                const char *sub = NULL;
                char nbuf[16];
                if (len == 4 && !strncmp(tok, "NAME", 4)) {
                    sub = game->character.name;
                } else if (len == 4 && !strncmp(tok, "RANK", 4)) {
                    sub = game->character.cls.rank_title;
                } else if (len == 6 && !strncmp(tok, "NEEDED", 6)) {
                    snprintf(nbuf, sizeof(nbuf), "%d",
                             needed > 0 ? needed : 0);
                    sub = nbuf;
                } else if (len == 1 && tok[0] == 'S') {
                    sub = (needed == 1) ? "" : "s";
                }
                if (sub) {
                    while (*sub && dst < end) *dst++ = *sub++;
                    src = close + 1;
                    continue;
                }
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}
```

`run_audience_dialog` (two-stage flow):

```c
static void run_audience_dialog(Game *game, const ResCastle *rc) {
    if (!rc) return;
    const ClassDef *cls = class_by_id(game->character.cls.id);
    int caught = GameVillainsCaught(game);
    int rank   = game->character.cls.rank_index;
    int needed = 0;
    if (cls && rank + 1 < cls->rank_count)
        needed = cls->ranks[rank + 1].villains_needed - caught;
    bool at_final_rank = !cls || rank + 1 >= cls->rank_count;

    const char *branch;
    if (needed > 0) {
        branch = rc->special.audience_more_needed;
    } else if (!at_final_rank) {
        GameMaybeRankUp(game);
        branch = rc->special.audience_rank_up;
    } else {
        branch = rc->special.audience_final_rank;
    }

    audience_substitute(game, needed, branch ? branch : "",
                        pending_audience_message,
                        sizeof(pending_audience_message));
    snprintf(pending_audience_header, sizeof(pending_audience_header),
             "%s",
             rc->special.dialog_header[0]
             ? rc->special.dialog_header
             : "Audience");

    char fanfare[400];
    audience_substitute(game, needed,
                        rc->special.audience_intro[0]
                            ? rc->special.audience_intro
                            : "Trumpets announce your\n"
                              "arrival with regal fanfare.\n\n"
                              "King Maximus rises from his\n"
                              "throne to greet you and\n"
                              "proclaims:           (space)",
                        fanfare, sizeof(fanfare));
    open_dialog(NULL, fanfare);
}
```

Page-2 dispatch (in dialog-dismiss handler):

```c
} else if (ui_any_key_pressed()) {
    if (!dialog_advance()) {
        dialog_dismiss();
        if (pending_audience_message[0]) {
            char hdr[64];
            snprintf(hdr, sizeof(hdr), "%s", pending_audience_header);
            char body[700];
            snprintf(body, sizeof(body), "%s", pending_audience_message);
            pending_audience_header[0]  = '\0';
            pending_audience_message[0] = '\0';
            open_dialog(hdr, body);
        }
    }
}
```

### A.21 town_do_boat (`src/views.c:447`)

```c
static void town_do_boat(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char buf[96];
    if (g->boat.has_boat) {
        // OpenKB game.c:2725-2734: refuses cancellation while sailing.
        if (g->travel_mode == TRAVEL_BOAT) {
            resources_format_template(buf, sizeof buf,
                                      bn->town_boat_vacate_first, NULL, 0);
            town_show_info(buf);
            return;
        }
        g->boat.has_boat = false;
        g->boat.x = -1; g->boat.y = -1;
        return;     // silent menu redraw
    }
    int cost = GameBoatCost(g);
    if (g->stats.gold <= cost) {     // OpenKB: <= (exact-match fails)
        resources_format_template(buf, sizeof buf, bn->town_no_gold, NULL, 0);
        town_show_info(buf);
        return;
    }
    g->stats.gold -= cost;
    g->boat.has_boat = true;
    g->boat.x = town.boat_x;
    g->boat.y = town.boat_y;
    size_t m = 0;
    while (m + 1 < sizeof(g->boat.zone) && g->position.zone[m]) {
        g->boat.zone[m] = g->position.zone[m]; m++;
    }
    g->boat.zone[m] = '\0';
    // Silent menu redraw — row B label flips to "Cancel boat rental".
}
```

### A.22 GameBoatCost (`src/game.c`)

```c
int GameBoatCost(const Game *g) {
    if (!g || !g->res) return 0;
    if (GameHasPower(g, ARTIFACT_POWER_CHEAPER_BOAT))
        return g->res->economy.boat_rent_cost_cheap;
    return g->res->economy.boat_rent_cost;
}
```

Default cheap = 100, normal = 250.

### A.23 GameHasPower (`src/game.c`)

```c
bool GameHasPower(const Game *g, int power) {
    if (!g) return false;
    int mask = 0;
    for (int i = 0; i < g->res->artifacts_count && i < 8; i++) {
        if (!g->artifacts.found[i]) continue;
        const ArtifactDef *a = artifact_by_index(i);
        if (a) mask |= a->power;
    }
    return (mask & power) != 0;
}
```

OR's together every found artifact's power flags, then masks
against the queried bit.

### A.24 GameClaimArtifact (`src/game.c`)

```c
bool GameClaimArtifact(Game *g, int idx) {
    if (!g || idx < 0 || idx >= 8) return false;
    if (g->artifacts.found[idx]) return false;
    g->artifacts.found[idx] = true;
    const ArtifactDef *a = artifact_by_index(idx);
    if (!a) return true;
    if (a->power & ARTIFACT_POWER_DOUBLE_LEADERSHIP) {
        g->stats.leadership_base    *= 2;
        g->stats.leadership_current *= 2;
    }
    if (a->power & ARTIFACT_POWER_DOUBLE_SPELL_POWER)
        g->stats.spell_power *= 2;
    if (a->power & ARTIFACT_POWER_INCREASE_COMMISSION)
        g->stats.commission_weekly *= 2;
    if (a->power & ARTIFACT_POWER_DOUBLE_MAX_SPELLS)
        g->stats.max_spells *= 2;
    return true;
}
```

### A.25 GameComputeScore (`src/game.c`)

```c
int GameComputeScore(const Game *g) {
    if (!g || !g->res) return 0;
    const ResScore *sc = &g->res->score;
    int caught = GameVillainsCaught(g);
    int found = 0;
    for (int i = 0; i < 8; i++) if (g->artifacts.found[i]) found++;
    int castles_player = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        if (g->castles[i].id[0] && g->castles[i].owner_kind == CASTLE_OWNER_PLAYER)
            castles_player++;
    }
    int score = caught       * sc->villain_coef
              + found        * sc->artifact_coef
              + castles_player * sc->castle_coef
              - g->stats.followers_killed * sc->kill_penalty;
    if (score < 0) score = 0;
    if (sc->easy_halves && g->character.difficulty == DIFFICULTY_EASY)
        score /= 2;
    else if (g->character.difficulty >= 0 && g->character.difficulty < 4)
        score *= sc->difficulty_multiplier[g->character.difficulty];
    return score;
}
```

### A.26 GameMaybeRankUp (`src/game.c`)

```c
bool GameMaybeRankUp(Game *g) {
    if (!g) return false;
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls) return false;
    int rank = g->character.cls.rank_index;
    if (rank + 1 >= cls->rank_count) return false;
    int caught = GameVillainsCaught(g);
    if (caught < cls->ranks[rank + 1].villains_needed) return false;

    int prev_lead = cls->ranks[rank].leadership;
    int prev_max  = cls->ranks[rank].max_spells;
    int prev_spp  = cls->ranks[rank].spell_power;
    int prev_comm = cls->ranks[rank].commission;

    rank++;
    g->character.cls.rank_index = rank;
    copy_id(g->character.cls.rank_id,    sizeof(g->character.cls.rank_id),
            cls->ranks[rank].id);
    copy_id(g->character.cls.rank_title, sizeof(g->character.cls.rank_title),
            cls->ranks[rank].name);

    int dlead = cls->ranks[rank].leadership - prev_lead;
    int dmax  = cls->ranks[rank].max_spells - prev_max;
    int dspp  = cls->ranks[rank].spell_power - prev_spp;
    int dcomm = cls->ranks[rank].commission - prev_comm;
    g->stats.leadership_base    += dlead;
    g->stats.leadership_current += dlead;
    g->stats.max_spells         += dmax;
    g->stats.spell_power        += dspp;
    g->stats.commission_weekly  += dcomm;
    return true;
}
```

### A.27 end_day / week boundary (`src/game.c`)

```c
static void end_day(Game *g, bool *week_ended, int *commission_paid) {
    if (week_ended) *week_ended = false;
    if (commission_paid) *commission_paid = 0;

    if (g->stats.days_left > 0) g->stats.days_left--;
    g->stats.steps_left_today = g->res->time.day_steps;
    g->stats.time_stop = 0;

    if (g->stats.days_left == 0) {
        g->stats.game_over = true;
        return;
    }

    int wk = g->res->time.week_days;
    if (wk > 0 && g->stats.days_left % wk == 0) {
        // Week boundary: compute astrology, refresh dwellings, pay
        // commission, deduct upkeep, grow castles/foes.
        if (week_ended) *week_ended = true;
        int week_id = (g->res->time.days_per_difficulty[g->character.difficulty]
                       - g->stats.days_left) / wk;
        int astrology = GamePickAstrologyCreature(g, week_id);
        g->stats.last_astrology_troop = astrology;
        GameApplyAstrology(g, astrology);

        // Commission.
        g->stats.gold += g->stats.commission_weekly;
        g->stats.last_commission = g->stats.commission_weekly;
        if (commission_paid) *commission_paid = g->stats.commission_weekly;

        // Upkeep (sum recruit_cost / 10 × count).
        int upkeep = 0;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!g->army[s].id[0] || g->army[s].count == 0) continue;
            const TroopDef *t = troop_by_id(g->army[s].id);
            if (!t) continue;
            upkeep += (t->recruit_cost / 10) * g->army[s].count;
        }
        g->stats.gold -= upkeep;
        if (g->stats.gold < 0) {
            g->stats.gold = 0;
            // Lose the boat (OpenKB end_week behavior).
            g->boat.has_boat = false;
            g->boat.x = -1; g->boat.y = -1;
        }

        // Astrology growth: castles + foes.
        const TroopDef *astro_troop = troop_by_index(astrology);
        if (astro_troop) {
            for (int ci = 0; ci < GAME_CASTLES; ci++) {
                CastleRecord *cr = &g->castles[ci];
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    if (!cr->garrison[s].id[0]) continue;
                    if (strcmp(cr->garrison[s].id, astro_troop->id) != 0)
                        continue;
                    cr->garrison[s].count += astro_troop->growth_per_week;
                }
            }
            for (int fi = 0; fi < g->foe_count; fi++) {
                FoeState *f = &g->foes[fi];
                if (!f->alive) continue;
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    if (!f->garrison[s].id[0]) continue;
                    if (strcmp(f->garrison[s].id, astro_troop->id) != 0)
                        continue;
                    f->garrison[s].count += astro_troop->growth_per_week;
                }
            }
        }
    }
}
```

### A.28 GameSwitchZone (`src/game.c`)

```c
bool GameSwitchZone(Game *g, Map *map, Fog *fog, const char *zone_id) {
    if (!g || !map || !fog || !zone_id) return false;
    const ResZone *z = resources_zone_by_id(g->res, zone_id);
    if (!z) return false;
    if (!MapLoadZoneWithPlacements(map, g->res, zone_id, g)) return false;

    GameApplyTileMutations(g, map, zone_id);
    copy_id(g->position.zone, sizeof(g->position.zone), zone_id);
    g->position.x = map->hero_spawn_x;
    g->position.y = map->hero_spawn_y;
    g->position.last_x = g->position.x;
    g->position.last_y = g->position.y;

    FogInit(fog);
    FogReveal(fog, map, g->position.x, g->position.y, g->res->world.fog_sight);

    int zi = (int)(z - g->res->zones);
    if (zi >= 0 && zi < GAME_CONTINENTS)
        g->world.zones_discovered[zi] = true;
    return true;
}
```

### A.29 adventure_handle_interact (`src/adventure.c:65-...`)

```c
InteractResult adventure_handle_interact(const Tile *t, const char *zone) {
    InteractResult r = { 0 };
    r.artifact_idx = -1;
    r.town_boat_x = -1; r.town_boat_y = -1;
    if (!t || t->interactive == INTERACT_NONE) return r;

    if (t->interactive == INTERACT_SIGN) {
        // Read sign body+title from per-tile fields.
        ... assemble dialog body, set r.dialog_* fields.
        r.bounce_back = true;
        return r;
    }

    if (t->interactive == INTERACT_CASTLE_GATE) {
        r.opened_castle = true;
        copy_string(r.castle_id, sizeof(r.castle_id), t->id);
        r.bounce_back = true;
        return r;
    }

    if (t->interactive == INTERACT_TOWN) {
        r.opened_town = true;
        copy_string(r.town_id, sizeof(r.town_id), t->id);
        // Town tile may carry boat-spawn coords for rentals.
        r.town_boat_x = t->boat_spawn_x;
        r.town_boat_y = t->boat_spawn_y;
        r.bounce_back = true;
        return r;
    }

    if (t->interactive == INTERACT_ARTIFACT) {
        int local = artifact_local_from_id(t->id);
        int idx   = artifact_index_for_tile(zone, local);
        if (idx >= 0) r.artifact_idx = idx;
        // bounce_back set by caller after applying the pickup.
        return r;
    }

    if (t->interactive == INTERACT_FOE) {
        r.opened_foe = true;
        copy_string(r.foe_id, sizeof(r.foe_id), t->id);
        // bounce_back depends on hostile/friendly — caller decides.
        return r;
    }

    // Other interactives: chest, sign, dwelling*, navmap, orb, telecave,
    // alcove. Each sets the appropriate r.opened_* and r.bounce_back.
    ...
}
```

(Full body in `src/adventure.c:65-300`. The handler is a dispatcher
that produces an `InteractResult` struct; the main loop reads
`r.opened_*`/`r.artifact_idx`/`r.bounce_back` and dispatches the
actual flow.)

### A.30 adventure_walkable_*

```c
bool adventure_walkable_on_foot(const Tile *t) {
    if (!t) return false;
    if (t->interactive != INTERACT_NONE) return true;
    if (t->is_bridge) return true;
    if (t->blocks_foot) return false;
    return TerrainWalkable(t->terrain);
}

bool adventure_walkable_in_boat(const Tile *t) {
    if (!t) return false;
    if (t->is_bridge) return true;
    if (t->terrain == TERRAIN_WATER) return true;
    return adventure_walkable_on_foot(t);   // disembark
}

bool adventure_walkable_in_flight(const Tile *t) {
    if (!t) return false;
    return true;   // fly over everything
}
```

### A.31 TerrainWalkable (`src/tile.c:76`)

```c
bool TerrainWalkable(Terrain t) {
    return t == TERRAIN_GRASS || t == TERRAIN_DESERT;
}

int TerrainMoveCost(Terrain t) {
    switch (t) {
        case TERRAIN_GRASS:  return 1;
        case TERRAIN_DESERT: return 40;     // ~1 tile per day
        default:             return 0;       // unreachable
    }
}
```

### A.32 RunCombatStub (`src/combat.c`)

Current stub — returns WIN unconditionally:

```c
CombatResult RunCombatStub(Game *g, CombatMode mode, const CombatTarget *target) {
    const ResBanners *bn = &g->res->banners;
    char body[512], frag[160];
    size_t off = 0;
    resources_format_template(frag, sizeof frag,
                              bn->combat_scouts_header, NULL, 0);
    append_frag(body, sizeof body, &off, frag);

    int shown = 0;
    if (target && target->garrison && target->garrison_slots > 0) {
        for (int i = 0; i < target->garrison_slots &&
             off + 1 < sizeof(body); i++) {
            const Unit *u = &target->garrison[i];
            if (!u->id[0] || u->count == 0) continue;
            const TroopDef *t = troop_by_id(u->id);
            const char *tname = (t && t->name[0]) ? t->name : u->id;
            char cbuf[16];
            snprintf(cbuf, sizeof cbuf, "%d", u->count);
            ResTemplateVar vars[] = { { "COUNT", cbuf }, { "TROOP", tname } };
            resources_format_template(frag, sizeof frag,
                                      bn->combat_scouts_count, vars, 2);
            append_frag(body, sizeof body, &off, frag);
            shown++;
        }
    }
    if (shown == 0) {
        resources_format_template(frag, sizeof frag,
                                  bn->combat_scouts_small_band, NULL, 0);
        append_frag(body, sizeof body, &off, frag);
    }
    resources_format_template(frag, sizeof frag,
                              bn->combat_placeholder, NULL, 0);
    append_frag(body, sizeof body, &off, frag);

    const char *header = (mode == COMBAT_MODE_CASTLE)
                       ? bn->combat_header_siege
                       : bn->combat_header_default;
    if (target && target->name && target->name[0]) header = target->name;
    open_dialog(header, body);
    return COMBAT_RESULT_WIN;
}
```

To be replaced by the full combat engine port.

---

## Appendix B — Save schema (verbatim JSON)

A representative save file:

```json
{
  "version": 1,
  "seed": 1234567890,
  "character": {
    "name": "Dan",
    "class": "knight",
    "rank_index": 1,
    "rank_id": "general",
    "rank_title": "General",
    "difficulty": 1,
    "mount": 0
  },
  "stats": {
    "gold": 5500,
    "commission_weekly": 2000,
    "leadership_base": 200,
    "leadership_current": 200,
    "followers_killed": 12,
    "score": 0,
    "spell_power": 2,
    "max_spells": 3,
    "knows_magic": false,
    "siege_weapons": 0,
    "time_stop": 0,
    "steps_left_today": 28,
    "days_left": 542,
    "game_over": false,
    "last_commission": 2000,
    "last_astrology_troop": 0,
    "options": [4, 1, 1, 1, 1, 0]
  },
  "position": {
    "zone": "continentia",
    "x": 18, "y": 24,
    "last_x": 18, "last_y": 23,
    "facing_left": false,
    "travel_mode": 0,
    "hud_visible": true
  },
  "anim_frame": 2,
  "army": [
    { "id": "militia",  "count": 24 },
    { "id": "archers",  "count": 4  },
    { "id": "",         "count": 0  },
    { "id": "",         "count": 0  },
    { "id": "",         "count": 0  }
  ],
  "spells": [0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0],
  "contract": {
    "active_id": "hack",
    "cycle": ["murray", "hack", "aimola", "baron_makahl", "dread_rob"],
    "last_contract": 4,
    "max_contract": 5,
    "villains_caught": [true, false, false, false, false, false, false,
                        false, false, false, false, false, false,
                        false, false, false, false]
  },
  "artifacts": { "found": [false, false, false, false, false, false, false, false] },
  "world": {
    "zones_discovered": [true, false, false, false],
    "orbs_found": [false, false, false, false],
    "puzzle_revealed": [/* 25 booleans */ false, false, ...]
  },
  "boat": { "has_boat": true, "x": 11, "y": 60, "zone": "continentia" },
  "towns":   [ /* 26 entries */ ],
  "castles": [ /* 26 entries */ ],
  "scepter": { "zone": "saharia", "x": 12, "y": 39 },
  "consumed":   [ /* tile mutations */ ],
  "dwellings":  [ /* dwelling state rows */ ],
  "placements": [ /* salted placements */ ],
  "foes":       [ /* foe state rows */ ]
}
```

`SaveGameWrite` writes pretty-printed (cJSON `Print`); `SaveGameRead`
parses with cJSON and walks every key.

---

## Appendix C — Sprite assets (file inventory)

`assets/kings-bounty/`:

```
art/
  font/kb-font.png                 8×8 bitmap font (96 glyphs)
  hero/hero_<frame>.png            Adventure-screen hero sprite
  boat/boat_<frame>.png            Boat sprite when sailing
  troops/<id>.png                  Static troop sprite (24 entries)
  troops/<id>_<frame>.png          4-frame idle animation per troop
  troops/<id>_combat.png           Combat sprite (when engine lands)
  villains/<short>.png             Villain portraits (17 × 4 chars: murr, hack, ...)
  artifacts/<id>.png               Artifact icons
  ui/
    plains_backdrop.png            Outdoor dwelling backdrop
    forest_backdrop.png
    hillcave_backdrop.png
    dungeon_backdrop.png
    castle_backdrop.png            Home + own castle backdrop
    ending_win.png                 VIEW_WIN background
    ending_lose.png                VIEW_LOSE background
    chrome.png                     Top status + side bar frame
    cartoon_*.png                  End-game cartoon frames

data/
  palette.bin                      768-byte VGA palette (256 RGB triplets)

maps/
  continentia.dat                  64×64 text tilemap
  forestria.dat
  archipelia.dat
  saharia.dat

game.json                          All gameplay data + strings
```

---

## Appendix D — game.json schema reference

Top-level keys (parsed by `src/resources.c:resources_load`):

```
title              string                  Window title
version            string                  Asset pack version (informational)
window             { base_w, base_h }      Render-target size hint
world              { starting_zone, default_name, default_options[6],
                     fog_sight }
time               { days_per_difficulty[4], day_steps, week_days }
economy            { boat_rent_cost, boat_rent_cost_cheap,
                     siege_weapons_cost, alcove_cost,
                     spell_cost_default,
                     chest: { chance_*, *_min, *_max, *_points } }
score              { villain_coef, artifact_coef, castle_coef,
                     kill_penalty, easy_halves, difficulty_multiplier[4] }
tuning             { instant_army_multiplier[4], search_cost_days }
spawn              { chance_curve[4][5], troop_pool[4][5] }
contract           { cycle_length, initial_last_contract }
colors             { minimap_terrain { grass, forest, mountain, water, desert, fog },
                     difficulty_bar { easy, normal, hard, impossible } }
combat             { morale_chart[5][5] }       (5×5 char matrix)

troops[]           per §5
spells[]           per §10
classes[]          per §6
artifacts[]        per §9
villains[]         per §7
zones[]            per §11
towns[]            per §11
castles[]          per §11
tile_codes[]       per §12

strings.banners    ~120 templates with %TOKEN% placeholders
strings.ui         labels + count_buckets (per-flow)
strings.win        { header, body, footer }
strings.lose       { header, body, footer }
strings.dialog_titles  per-spell title overrides
strings.villain_descriptions  17 entries { alias, features, crimes }

credits[]          List of credit lines (minimal in default pack)
controls           Controls-menu definitions (delay, sounds, ...)
end                Reserved for future end-game content
```

---

## Appendix E — Tile code reference (game.json)

The default `tile_codes[]` (256 entries; printable ASCII fills the
first ~96 indices, the rest are unused/reserved):

```
'.'  grass            (TERRAIN_GRASS)
'~'  water            (TERRAIN_WATER)
','  water_edge_*     (decoration variants by surrounding tile)
'*'  forest           (TERRAIN_FOREST, blocks_foot=true)
'^'  mountain         (TERRAIN_MOUNTAIN, blocks_foot=true)
'-'  desert           (TERRAIN_DESERT)
'='  bridge_h         (is_bridge=true)
'|'  bridge_v
'#'  castle_wall      (blocks_foot=true)
'C'  castle_gate      (interactive=INTERACT_CASTLE_GATE)
'T'  town_gate        (interactive=INTERACT_TOWN)
'$'  chest            (saltable; interactive=INTERACT_CHEST initially)
'D'  dwelling_plains
'F'  dwelling_forest
'H'  dwelling_hills
'M'  dwelling_dungeon
'?'  signpost
```

(Plus dozens of decoration variants: water-edge tiles, road tiles,
rock cluster, beach, etc. Each is a distinct entry in `tile_codes`
with its own art name; only the terrain category and blocks_foot
flag affect gameplay.)

---

## Appendix F — Complete control flow per frame

```
WHILE running:
  ALT+ENTER       toggle fullscreen
  F10             enter cheat-menu mode (next letter dispatches)
  if cheat menu active:
    consume letter; apply effect; close menu

  pump_week_end_dialog(g)         pop pending astrology dialog

  overlay = (views_active || dialog || prompt)
  if overlay just opened:  hide HUD
  if overlay just closed:  restore HUD
  prev_overlay = overlay

  if fast_quit_active:
    Y → quit; N/Esc → cancel

  else if prompt_active:
    prompt_update() → resolve → dispatch FLOW_*

  else if VIEW_HOME_CASTLE && !dialog:
    Esc → views_dismiss
    A → screen_recruit_soldiers_open
    B → run_audience_dialog

  else if VIEW_RECRUIT_SOLDIERS && !dialog:
    screen_recruit_soldiers_update → handles letter + count

  else if VIEW_OWN_CASTLE && !dialog:
    Esc / Space / A..E → garrison transfer

  else if VIEW_DWELLING && !dialog:
    Esc → views_dismiss
    A..C → recruit prompt

  else if VIEW_ALCOVE && !dialog:
    Esc → views_dismiss
    Y → buy lesson; N/Esc → leave

  else if VIEW_TOWN && !dialog && !prompt:
    A..E → town_do_*

  else if VIEW_CONTROLS:
    cycle settings, Esc

  else if VIEW_SPELLS:
    pick column + letter → cast

  else if any view && !dialog:
    any-key-dismiss (Worldmap: Space toggles fog reveal if orb)

  else if dialog_active:
    Ctrl+Q → quit
    any key → dialog_advance / dialog_dismiss
    if dismiss && pending_audience_message: open page 2

  else if game_over:
    any key → quit to menu

  else (default classic input):
    arrows / numpad → move
    A V I P M U C O N S W F L D Q → action
    Ctrl+Q → fast-quit prompt
    Tab → toggle HUD

  draw frame:
    chrome (top status, sidebar, map frame)
    map render (5×5 viewport, hero, foes)
    HUD overlay (if visible)
    view panel (if any view active)
    dialog panel (if dialog active)
    prompt panel (if prompt active)
```

---

## Appendix G — Bug-fix history (this codebase)

All previously logged spec discrepancies (24 bugs catalogued
during the initial spec audit) have been resolved. The following
items have been addressed in subsequent sessions:

- foes_follow water-tile bypass (land foes no longer reach the
  hero on the boat).
- view_army OOC display — now per-stack via OpenKB
  army_leadership formula.
- audience-with-king two-step fanfare → king's message dialogs.
- Pikemen recruit cost 800 → 300 (DOS-original).
- Recruit-soldiers screen rejects letters whose troop's HP×6
  exceeds total leadership (matches DOS binary "n/a" gating).
- Town header GP=NK format with two-row layout.
- Town boat popups dropped (rental-success / cancellation /
  no-dock invented popups removed; vacate/no-gold popups gain
  OpenKB MSG_PADDED leading newlines).
- Town info-overlay popup uses the same DBLUE/YELLOW panel rect
  as the menu beneath, matching `KB_BottomBox` semantics.

Subsequent updates to this spec should record code changes
inline in the appropriate section.

---

## Appendix H — Missing / pending

These OpenKB features are not yet implemented in openbounty; this
spec describes what exists.

### H.1 Combat engine

`src/combat.c` is a 60-line stub. The full battlefield, turn
system, damage formula, retaliation, abilities, AI, spells, and
victory/defeat flow are all pending. Once implemented, sections
17, 18.1-18.7, and 19.2-19.x of this spec must be expanded with
the same depth as the corresponding sections in OPENKB-SPEC.

### H.2 Combat spells

Clone, Teleport, Fireball, Lightning, Freeze, Resurrect, Turn
Undead — purchasable but not castable (no combat state to target).

### H.3 Auto-battle

Cheat 'A' toggles `auto_battle` in OpenKB. OpenBounty has no
combat, so the toggle has no effect.

### H.4 Audio

raylib's audio module is initialized at startup but no sound
effects are currently loaded or played.

### H.5 Dragons sprite

Some troop sprite assets are missing. The fallback renders a colored
placeholder rectangle.

### H.6 King's audience flavor

Default audience strings are placeholder fallbacks; the king's
castle JSON declares `audience_intro / rank_up / more_needed /
final_rank` strings but they may need polish to match OpenKB's
exact wording.

---

## Appendix I — File line counts (snapshot)

```
src/main.c                         ~3000
src/game.c                         ~2000
src/map.c                           ~400
src/fog.c                           ~150
src/tile.c                          ~150
src/resources.c                    ~2400
src/savegame.c                      ~700
src/views.c                         ~900
src/adventure.c                     ~400
src/ui.c                            ~120
src/combat.c                         ~60   (stub)
src/classic/overlay.c               ~700
src/classic/views.c                ~1300
src/classic/chrome.c                ~400
src/classic/map_render.c            ~300
src/classic/hud.c                   ~250
src/classic/input.c                 ~200
src/classic/prompt.c                ~400
src/classic/startup.c               ~400
src/classic/screens/*.c              ~1000 (combined)
```

Approximate totals: ~12,000 LOC engine + ~6,000 LOC view layer.

---

## Document history

- **2026-04-26** — Initial creation. Snapshot of openbounty at
  commit `a9544a7` (post boat-popup parity fixes).

---

---

## Appendix J — Complete data tables (from game.json)

### J.1 troops[] — complete table

```
idx id             name           SL  HP MV M- M+  R-  R+   A  Cost  Sp dwell     pop gw mor tier
  0 peasants       Peasants        1   1  1  1  1   0   0   0    10   1 plains    250  6   A [10, 20, 50, 100]
  1 sprites        Sprites         1   1  1  1  2   0   0   0    15   1 forest    200  6   C [20, 50, 100, 127]
  2 militia        Militia         2   2  2  1  2   0   0   0    50   5 castle      0  5   A [10, 20, 50, 100]
  3 wolves         Wolves          2   3  3  1  3   0   0   0    40   4 plains    150  5   D [5, 15, 30, 80]
  4 skeletons      Skeletons       2   3  2  1  2   0   0   0    40   4 dungeon   150  5   E [5, 10, 25, 50]
  5 zombies        Zombies         2   5  1  2  2   0   0   0    50   5 dungeon   100  5   E [5, 10, 25, 75]
  6 gnomes         Gnomes          2   5  1  1  3   0   0   0    60   6 forest    250  5   C [10, 25, 50, 100]
  7 orcs           Orcs            2   5  2  2  3   1   2  10    75   7 hill      200  5   D [5, 15, 30, 80]
  8 archers        Archers         2  10  2  1  2   1   3  12   250  25 castle      0  5   B [0, 0, 0, 0]
  9 elves          Elves           3  10  3  1  2   2   4  24   200  20 forest    100  4   C [5, 10, 25, 50]
 10 pikemen        Pikemen         3  10  2  2  4   0   0   0   300  30 castle      0  4   B [0, 0, 0, 0]
 11 nomads         Nomads          3  15  2  2  4   0   0   0   300  30 plains    150  4   C [4, 8, 15, 30]
 12 dwarves        Dwarves         3  20  1  2  4   0   0   0   350  30 hill      100  4   C [4, 10, 20, 50]
 13 ghosts         Ghosts          4  10  3  3  4   0   0   0   400  40 dungeon    25  3   E [2, 4, 10, 20]
 14 knights        Knights         5  35  1  6 10   0   0   0  1000 100 castle    250  3   B [0, 0, 0, 0]
 15 ogres          Ogres           4  40  1  3  5   0   0   0   750  75 hill      200  3   D [2, 4, 8, 15]
 16 barbarians     Barbarians      4  40  3  1  6   0   0   0   750  75 plains    100  3   C [2, 4, 10, 20]
 17 trolls         Trolls          4  50  1  2  5   0   0   0  1000 100 forest     25  3   D [2, 4, 8, 15]
 18 cavalry        Cavalry         4  20  4  3  5   0   0   0   800  80 castle      0  2   B [0, 0, 0, 0]
 19 druids         Druids          5  25  2  2  3  10   0   3   700  70 forest     25  2   C [1, 3, 6, 10]
 20 archmages      Archmages       5  25  1  2  3  25   0   2  1200 120 plains     25  2   C [1, 2, 4, 8]
 21 vampires       Vampires        5  30  1  3  6   0   0   0  1500 150 dungeon    50  2   E [2, 4, 10, 25]
 22 giants         Giants          5  60  3 10 20   5  10   6  2000 200 hill       50  2   C [1, 2, 5, 10]
 23 demons         Demons          6  50  1  5  7   0   0   0  3000 300 dungeon    25  1   E [1, 2, 4, 8]
 24 dragons        Dragons         6 200  1 25 50   0   0   0  5000 500 hill       25  1   D [1, 1, 1, 2]
```

### J.2 spells[] — complete table

```
idx id               name             kind        cost
  0 clone            Clone            combat      2000
  1 teleport         Teleport         combat       500
  2 fireball         Fireball         combat      1500
  3 lightning        Lightning        combat       500
  4 freeze           Freeze           combat       300
  5 resurrect        Resurrect        combat      5000
  6 turn_undead      Turn Undead      combat      2000
  7 bridge           Bridge           adventure    100
  8 time_stop        Time Stop        adventure    200
  9 find_villain     Find Villain     adventure   1000
 10 castle_gate      Castle Gate      adventure   1000
 11 town_gate        Town Gate        adventure    500
 12 instant_army     Instant Army     adventure   1000
 13 raise_control    Raise Control    adventure    500
```

### J.3 classes[] — complete tables


#### Knight (`knight`)

- starting_gold: 7500
- starting_troops: 
- ranks:
  - rank 0 `knight` (Knight): villains_needed=0 lead=100 max_spells=2 spell_power=1 commission=1000 knows_magic=False instant_army=0
  - rank 1 `general` (General): villains_needed=2 lead=100 max_spells=3 spell_power=1 commission=1000 knows_magic=False instant_army=2
  - rank 2 `marshal` (Marshal): villains_needed=8 lead=300 max_spells=4 spell_power=1 commission=2000 knows_magic=False instant_army=8
  - rank 3 `lord` (Lord): villains_needed=14 lead=500 max_spells=5 spell_power=2 commission=4000 knows_magic=False instant_army=14

#### Paladin (`paladin`)

- starting_gold: 10000
- starting_troops: 
- ranks:
  - rank 0 `paladin` (Paladin): villains_needed=0 lead=80 max_spells=3 spell_power=1 commission=1000 knows_magic=False instant_army=0
  - rank 1 `crusader` (Crusader): villains_needed=2 lead=80 max_spells=3 spell_power=1 commission=1000 knows_magic=False instant_army=2
  - rank 2 `avenger` (Avenger): villains_needed=7 lead=240 max_spells=6 spell_power=2 commission=2000 knows_magic=False instant_army=8
  - rank 3 `champion` (Champion): villains_needed=13 lead=400 max_spells=5 spell_power=2 commission=4000 knows_magic=False instant_army=18

#### Sorceress (`sorceress`)

- starting_gold: 10000
- starting_troops: 
- ranks:
  - rank 0 `sorceress` (Sorceress): villains_needed=0 lead=60 max_spells=5 spell_power=2 commission=3000 knows_magic=True instant_army=1
  - rank 1 `magician` (Magician): villains_needed=3 lead=60 max_spells=8 spell_power=3 commission=1000 knows_magic=False instant_army=6
  - rank 2 `mage` (Mage): villains_needed=6 lead=180 max_spells=10 spell_power=5 commission=1000 knows_magic=False instant_army=9
  - rank 3 `archmage` (Archmage): villains_needed=12 lead=300 max_spells=12 spell_power=5 commission=1000 knows_magic=False instant_army=19

#### Barbarian (`barbarian`)

- starting_gold: 7500
- starting_troops: 
- ranks:
  - rank 0 `barbarian` (Barbarian): villains_needed=0 lead=100 max_spells=2 spell_power=0 commission=2000 knows_magic=False instant_army=0
  - rank 1 `chieftain` (Chieftain): villains_needed=1 lead=100 max_spells=2 spell_power=1 commission=2000 knows_magic=False instant_army=3
  - rank 2 `warlord` (Warlord): villains_needed=5 lead=300 max_spells=3 spell_power=1 commission=2000 knows_magic=False instant_army=7
  - rank 3 `overlord` (Overlord): villains_needed=10 lead=500 max_spells=3 spell_power=1 commission=2000 knows_magic=False instant_army=15

### J.4 artifacts[] — complete table

```
idx id                       name                       power                    zone         local
  0 sword_of_prowess         The Sword of Prowess       increased_damage         saharia      1
  1 shield_of_protection     The Shield of Protection   quarter_protection       forestria    0
  2 crown_of_command         The Crown of Command       double_leadership        archipelia   0
  3 articles_of_nobility     The Articles of Nobility   increase_commission      continentia  1
  4 amulet_of_augmentation   The Amulet of Augmentation double_spell_power       saharia      0
  5 ring_of_heroism          The Ring of Heroism        double_max_spells        continentia  0
  6 book_of_necros           The Book of Necros         unknown                  archipelia   1
  7 anchor_of_admirability   The Anchor of Admirability cheaper_boats            forestria    1
```

### J.5 villains[] — complete table


#### v00 Murray the Miser (`murray`)

- portrait: `villains/mury.png`
- zone: continentia
- reward: 5000
- puzzle_cell: 0
- army:
  - peasants × 50
  - wolves × 20
  - militia × 25
  - peasants × 30
  - peasants × 25

#### v01 Hack the Rogue (`hack`)

- portrait: `villains/hack.png`
- zone: continentia
- reward: 6000
- puzzle_cell: 1
- army:
  - nomads × 10
  - militia × 30
  - militia × 20
  - peasants × 60
  - peasants × 40

#### v02 Princess Aimola (`aimola`)

- portrait: `villains/ammi.png`
- zone: continentia
- reward: 7000
- puzzle_cell: 2
- army:
  - sprites × 70
  - sprites × 50
  - skeletons × 20
  - zombies × 20
  - ogres × 4

#### v03 Baron Johnno Makahl (`baron_makahl`)

- portrait: `villains/baro.png`
- zone: continentia
- reward: 8000
- puzzle_cell: 3
- army:
  - wolves × 30
  - orcs × 20
  - archers × 10
  - trolls × 2
  - dwarves × 6

#### v04 Dread Pirate Rob (`dread_rob`)

- portrait: `villains/drea.png`
- zone: continentia
- reward: 9000
- puzzle_cell: 4
- army:
  - militia × 50
  - militia × 50
  - archers × 10
  - elves × 10
  - barbarians × 5

#### v05 Caneghor the Mystic (`caneghor`)

- portrait: `villains/cane.png`
- zone: continentia
- reward: 10000
- puzzle_cell: 5
- army:
  - sprites × 250
  - ghosts × 10
  - knights × 10
  - archmages × 4
  - archmages × 4

#### v06 Sir Moradon the Cruel (`moradon`)

- portrait: `villains/mora.png`
- zone: forestria
- reward: 12000
- puzzle_cell: 6
- army:
  - militia × 100
  - archers × 20
  - pikemen × 20
  - cavalry × 15
  - knights × 15

#### v07 Prince Barrowpine (`barrowpine`)

- portrait: `villains/barr.png`
- zone: forestria
- reward: 14000
- puzzle_cell: 7
- army:
  - elves × 30
  - archmages × 30
  - druids × 10
  - pikemen × 30
  - sprites × 300

#### v08 Bargash Eyesore (`bargash`)

- portrait: `villains/barg.png`
- zone: forestria
- reward: 16000
- puzzle_cell: 8
- army:
  - orcs × 150
  - ogres × 20
  - trolls × 10
  - giants × 5
  - wolves × 80

#### v09 Rinaldus Drybone (`rinaldus`)

- portrait: `villains/rina.png`
- zone: forestria
- reward: 18000
- puzzle_cell: 9
- army:
  - skeletons × 500
  - zombies × 100
  - ghosts × 30
  - vampires × 10
  - demons × 6

#### v0A Ragface (`ragface`)

- portrait: `villains/ragf.png`
- zone: archipelia
- reward: 20000
- puzzle_cell: 10
- army:
  - skeletons × 600
  - zombies × 200
  - ghosts × 50
  - vampires × 25
  - demons × 10

#### v0B Mahk Bellowspeak (`mahk`)

- portrait: `villains/mahk.png`
- zone: archipelia
- reward: 25000
- puzzle_cell: 11
- army:
  - giants × 30
  - dragons × 5
  - ogres × 30
  - orcs × 200
  - gnomes × 200

#### v0C Auric Whiteskin (`auric`)

- portrait: `villains/auri.png`
- zone: archipelia
- reward: 30000
- puzzle_cell: 12
- army:
  - gnomes × 300
  - barbarians × 40
  - giants × 20
  - nomads × 100
  - peasants × 700

#### v0D Czar Nickolai the Mad (`czar_nickolai`)

- portrait: `villains/czar.png`
- zone: archipelia
- reward: 35000
- puzzle_cell: 13
- army:
  - archers × 35
  - pikemen × 100
  - cavalry × 80
  - knights × 60
  - dragons × 5

#### v0E Magus Deathspell (`magus`)

- portrait: `villains/magu.png`
- zone: saharia
- reward: 40000
- puzzle_cell: 14
- army:
  - demons × 30
  - vampires × 50
  - archmages × 100
  - gnomes × 500
  - peasants × 5000

#### v0F Urthrax Killspite (`urthrax`)

- portrait: `villains/urth.png`
- zone: saharia
- reward: 45000
- puzzle_cell: 15
- army:
  - demons × 50
  - dragons × 10
  - cavalry × 200
  - knights × 250
  - archmages × 60

#### v10 Arech Dragonbreath (`arech`)

- portrait: `villains/arec.png`
- zone: saharia
- reward: 50000
- puzzle_cell: 16
- army:
  - dragons × 100
  - dragons × 25
  - dragons × 25
  - demons × 100
  - vampires × 100

### J.6 towns[] — complete table

```
idx id               name             zone           x   y       boat       gate intel          pinned
  0 riverton         Riverton         continentia   29  51    (30,52)    (29,52) azram          
  1 underfoot        Underfoot        forestria     58  59    (59,58)    (57,59) basefit        
  2 paths_end        Path's End       continentia   38  13    (39,13)    (38,14) cancomar       
  3 anomaly          Anomaly          forestria     34  40    (34,41)    (35,40) duvock         
  4 topshore         Topshore         archipelia     5  13     (4,14)     (5,14) endryx         
  5 lakeview         Lakeview         continentia   17  19    (18,19)    (16,19) faxis          
  6 simpleton        Simpleton        archipelia    13   3     (14,3)     (12,3) goobare        
  7 centrapf         Centrapf         archipelia     9  24    (10,25)     (9,25) hyppus         
  8 quiln_point      Quiln Point      continentia   14  36    (14,37)    (13,36) irok           
  9 midland          Midland          forestria     58  30    (58,31)    (57,30) jhan           
 10 xoctan           Xoctan           continentia   51  35    (51,36)    (51,34) kookamunga     
 11 overthere        Overthere        archipelia    57   6     (58,7)     (57,7) lorsche        
 12 elans_landing    Elan's Landing   forestria      3  26     (2,27)     (3,27) mooseweigh     
 13 kings_haven      King's Haven     continentia   17  42    (18,42)    (16,42) nilslag        
 14 bayside          Bayside          continentia   41   5     (41,6)     (40,5) ophiraund      
 15 nyre             Nyre             continentia   50  50    (49,51)    (50,49) portalis       
 16 dark_corner      Dark Corner      forestria     58   3     (59,3)     (58,4) quinderwitch   
 17 isla_vista       Isla Vista       continentia   57  58    (56,59)    (56,58) rythacon       
 18 grimwold         Grimwold         saharia        9   3     (10,3)      (9,4) spockana       
 19 japper           Japper           archipelia    13  56    (12,56)    (13,55) tylitch        
 20 vengeance        Vengeance        saharia        7  60     (8,60)     (6,60) uzare          
 21 hunterville      Hunterville      continentia   12  60    (11,60)    (12,59) hyppus         bridge
 22 fjord            Fjord            continentia   46  28    (47,27)    (47,28) wankelforte    
 23 yakonia          Yakonia          archipelia    49  55    (50,54)    (50,55) xelox          
 24 woods_end        Woods End        forestria      3  55     (2,54)     (3,54) yeneverre      
 25 zaezoizu         Zaezoizu         saharia       58  15    (59,15)    (58,16) zyzzarzaz      
```

### J.7 castles[] — complete table

```
idx id             name           zone           x   y       gate tier special
  0 azram          Azram          continentia   30  36          —    0 
  1 basefit        Basefit        forestria     47  57          —    1 
  2 cancomar       Cancomar       continentia   36  14          —    0 
  3 duvock         Duvock         forestria     30  45          —    1 
  4 endryx         Endryx         archipelia    11  17          —    2 
  5 faxis          Faxis          continentia   22  14          —    0 
  6 goobare        Goobare        archipelia    41  27          —    2 
  7 hyppus         Hyppus         archipelia    43  36          —    2 
  8 irok           Irok           continentia   11  33          —    0 
  9 jhan           Jhan           forestria     41  29          —    1 
 10 kookamunga     Kookamunga     continentia   57   5          —    0 
 11 lorsche        Lorsche        archipelia    52   6          —    2 
 12 mooseweigh     Mooseweigh     forestria     25  24          —    2 
 13 nilslag        Nilslag        continentia   22  39          —    0 
 14 ophiraund      Ophiraund      continentia    6   6          —    0 
 15 portalis       Portalis       continentia   58  40          —    0 
 16 quinderwitch   Quinderwitch   forestria     42   7          —    1 
 17 rythacon       Rythacon       continentia   54  57          —    0 
 18 spockana       Spockana       saharia       17  24          —    3 
 19 tylitch        Tylitch        archipelia     9  45          —    1 
 20 uzare          Uzare          saharia       41  51          —    3 
 21 vutar          Vutar          continentia   40  58          —    0 
 22 wankelforte    Wankelforte    continentia   40  22          —    0 
 23 xelox          Xelox          archipelia    45  57          —    1 
 24 yeneverre      Yeneverre      forestria     19  44          —    1 
 25 zyzzarzaz      Zyzzarzaz      saharia       46  20          —    3 
 26 king_maximus   of King Maximus continentia   11  56          —    0 audience
```

### J.8 tile_codes — 54 entries

```
char     art                          terrain    blocks  bridge
'*'      desert_edge_01               desert     False   False
'+'      desert_edge_02               desert     False   False
','      grass_variant                grass      False   False
'-'      desert_edge_03               desert     False   False
'.'      grass                        grass      False   False
'/'      desert_edge_04               desert     False   False
'0'      desert_edge_05               desert     False   False
'1'      desert_edge_06               desert     False   False
'2'      desert_edge_07               desert     False   False
'3'      desert_edge_08               desert     False   False
'4'      desert_edge_09               desert     False   False
'5'      desert_edge_10               desert     False   False
'6'      desert_edge_11               desert     False   False
'7'      desert_edge_12               desert     False   False
'<'      forest_edge_01               forest     True    False
'='      forest_edge_02               forest     True    False
'>'      forest_edge_03               forest     True    False
'?'      forest_edge_04               forest     True    False
'@'      forest_edge_05               forest     True    False
'A'      forest_edge_06               forest     True    False
'B'      forest_edge_07               forest     True    False
'C'      forest_edge_08               forest     True    False
'D'      desert                       desert     False   False
'E'      forest_edge_09               forest     True    False
'F'      forest                       forest     True    False
'G'      forest_edge_10               forest     True    False
'H'      forest_edge_11               forest     True    False
'I'      forest_edge_12               forest     True    False
'J'      mountain_edge_01             mountain   True    False
'K'      mountain_edge_02             mountain   True    False
'L'      mountain_edge_03             mountain   True    False
'M'      mountain_edge_04             mountain   True    False
'N'      mountain_edge_05             mountain   True    False
'O'      mountain_edge_06             mountain   True    False
'P'      mountain_edge_07             mountain   True    False
'Q'      mountain_edge_08             mountain   True    False
'R'      mountain_edge_09             mountain   True    False
'S'      mountain_edge_10             mountain   True    False
'T'      mountain_edge_11             mountain   True    False
'U'      mountain_edge_12             mountain   True    False
'Y'      water_edge_00                water      False   False
'Z'      water_edge_01                water      False   False
'['      water_edge_02                water      False   False
'\\'     water_edge_03                water      False   False
']'      water_edge_04                water      False   False
'^'      mountain                     mountain   True    False
'_'      water_edge_05                water      False   False
'`'      water_edge_06                water      False   False
'a'      water_edge_07                water      False   False
'b'      water_edge_08                water      False   False
'c'      water_edge_09                water      False   False
'd'      water_edge_10                water      False   False
'e'      water_edge_11                water      False   False
'~'      water                        water      False   False
```

### J.9 strings.banners — 117 entries

Categories (key prefix counts):

- `town_*`: 29 entries
- `spell_*`: 20 entries
- `body_*`: 11 entries
- `encounter_*`: 7 entries
- `chest_*`: 6 entries
- `combat_*`: 6 entries
- `budget_*`: 6 entries
- `alcove_*`: 4 entries
- `no_*`: 4 entries
- `dwelling_*`: 3 entries
- `tile_*`: 3 entries
- `telecave_*`: 2 entries
- `navmap_*`: 2 entries
- `astrology_*`: 2 entries
- `signpost_*`: 2 entries
- `relic_*`: 2 entries
- `status_*`: 2 entries
- `crystal_*`: 1 entries
- `temp_*`: 1 entries
- `recruit_*`: 1 entries
- `cannot_*`: 1 entries
- `army_*`: 1 entries
- `castle_*`: 1 entries

First 30 entries:

```
  chest_gold: "After scouring the area,\nyou fall upon a hidden\ntreasure cache. You may:\nA) T"
  chest_commission: "After surveying the area,\nyou discover that it is\nrich in mineral deposits.\n\"
  chest_spell_power: "Traversing the area, you\nstumble upon a time worn\ncannister. Curious, you un-\"
  chest_max_spells: "A tribe of nomads greet you\nand your army warmly. Their\nshaman, in awe of your"
  chest_new_spell: "You have captured a\nmischevious imp which has\nbeen terrorizing the\nregion. In"
  chest_empty: "The chest was empty!"
  town_header: "Town of %NAME%"
  town_gold_label: "GP=%GOLD%K"
  town_row_contract: "A) Get New Contract"
  town_row_boat_rent: "B) Rent boat (%COST% week)"
  town_row_boat_cancel: "B) Cancel boat rental"
  town_row_info: "C) Gather information"
  town_row_spell: "D) %SPELL% spell (%SPELL_COST%)"
  town_row_spell_none: "D) (no spell available)"
  town_row_siege_buy: "E) Buy seige weapons (%SIEGE_COST%)"
  town_row_siege_owned: "E) Siege weapons (owned)"
  town_contract_new: "New contract: %VILLAIN%.\nReward: %REWARD% gold.\nLast seen on %ZONE%."
  town_contract_none: "No contracts are available right now."
  town_boat_vacate_first: "\n\nPlease vacate the boat first"
  town_no_gold: "\n\n\nYou don't have enough gold!"
  town_intel_unavailable: "No intelligence is available here."
  town_intel_castle_under: "Castle %NAME% is under\n"
  town_intel_owner_rule: "%OWNER%'s rule.\n\n"
  town_intel_owner_none: "no one"
  town_intel_owner_player: "your"
  town_intel_owner_king: "the King"
  town_intel_count_named: "  %LABEL% %TROOP%\n"
  town_intel_count_numeric: "  %COUNT% %TROOP%\n"
  town_intel_monsters_generic: "  Various groups of monsters\n  occupy the castle."
  town_intel_no_garrison: "  (no garrison)"
  ...
```

### J.10 economy block

```
boat_cost_normal: 500
boat_cost_cheap: 100
siege_cost: 3000
alcove_cost: 5000
chest:
  chance_gold: [61, 66, 76, 71]
  chance_commission: [81, 86, 86, 81]
  chance_spell_power: [86, 92, 93, 91]
  chance_max_spells: [86, 92, 93, 91]
  chance_new_spell: [101, 101, 101, 101]
  gold_min: [0, 4, 9, 19]
  gold_max: [5, 16, 21, 31]
  commission_min: [9, 49, 99, 199]
  commission_max: [41, 51, 101, 301]
  max_spells_base: [1, 1, 2, 2]
scoring:
  per_villain: 500
  per_artifact: 250
  per_castle: 100
  kill_penalty: 1
  difficulty_multiplier: [0, 1, 2, 4, 8]
  easy_halves: True
```

### J.11 tuning block

```
instant_army_multiplier: [3, 2, 1, 1]
search_cost_days: 10
```

### J.12 time block

```
day_steps: 40
week_days: 5
days_per_difficulty: {'easy': 900, 'normal': 600, 'hard': 400, 'impossible': 200}
```

### J.13 spawn block

```
chance_curve (continent tier × pool slot):
troop_pool (kind × pool slot):
```

### J.14 score block

```
```

### J.15 world block

```
max_army_slots: 5
fog_sight: 3
starting_zone: continentia
zone_noun: continent
zone_noun_plural: continents
default_name: Hero
default_options: [4, 1, 1, 1, 1, 1]
```

### J.16 combat.morale_chart

```
        A  B  C  D  E
  A     N  N  N  N  N
  B     N  N  N  N  N
  C     N  N  H  N  N
  D     L  N  L  H  N
  E     L  L  L  N  N
```

### J.17 zones[] — summary


#### Continentia (`continentia`)

- map: `assets/kings-bounty/maps/continentia.dat`
- size: 64×64
- is_home: True
- neighbors: ['forestria', 'archipelia', 'saharia']
- objects: 56 chests, 22 signs, 11 towns, 12 castles, 0 artifacts, 0 dwellings, 40 static foes
- salt budget: artifacts=2 navmaps=1 orbs=1 telecaves=2 dwellings=10 friendly_foes=5
- preferred_troops: ['peasants', 'sprites', 'orcs', 'skeletons', 'wolves', 'gnomes']
- dwelling_range: [0, 14]

#### Forestria (`forestria`)

- map: `assets/kings-bounty/maps/forestria.dat`
- size: 64×64
- is_home: False
- neighbors: ['continentia', 'archipelia']
- objects: 35 chests, 0 signs, 6 towns, 6 castles, 0 artifacts, 0 dwellings, 0 static foes
- salt budget: artifacts=2 navmaps=1 orbs=1 telecaves=2 dwellings=10 friendly_foes=5
- preferred_troops: ['dwarves', 'zombies', 'nomads', 'elves', 'ogres', 'elves']
- dwelling_range: [1, 14]

#### Archipelia (`archipelia`)

- map: `assets/kings-bounty/maps/archipelia.dat`
- size: 64×64
- is_home: False
- neighbors: ['continentia', 'forestria', 'saharia']
- objects: 35 chests, 0 signs, 6 towns, 6 castles, 0 artifacts, 0 dwellings, 0 static foes
- salt budget: artifacts=2 navmaps=1 orbs=1 telecaves=2 dwellings=10 friendly_foes=5
- preferred_troops: ['ghosts', 'barbarians', 'trolls', 'druids']
- dwelling_range: [2, 14]

#### Saharia (`saharia`)

- map: `assets/kings-bounty/maps/saharia.dat`
- size: 64×64
- is_home: False
- neighbors: ['continentia', 'archipelia']
- objects: 35 chests, 0 signs, 3 towns, 3 castles, 0 artifacts, 0 dwellings, 0 static foes
- salt budget: artifacts=2 navmaps=1 orbs=1 telecaves=2 dwellings=10 friendly_foes=5
- preferred_troops: ['giants', 'vampires', 'archmages', 'dragons', 'demons']
- dwelling_range: [20, 24]

### J.18 strings.villain_descriptions — 17 entries


#### murray
- alias: "None"
- features: "Threadbare clothes, bald patch with hair combed to cover it, incessant cough."
- crimes: "Murray is wanted for various petty crimes as well as for treason. He allowed a group of pirates to enter the castle and free criminals."

#### hack
- alias: "The Spitter"
- features: "Bushy ebon beard stained with tobacco juice, numerous battle scars, brash, arrogant behavior."
- crimes: "Along with many minor infractions, Hack is wanted for conspiracy against the Crown and grave-robbing."

#### aimola
- alias: "Lady Deceit"
- features: "Excessive use of make-up to hide aging features, ever- present lace handkerchief."
- crimes: "The Princess violated her status as a visiting dignitary by encouraging a murder and joining the conspiracy against the Crown."

#### baron_makahl
- alias: "Johnno"
- features: "Expensive and gaudy clothes, overweight, and a scruffy beard."
- crimes: "Johnno is wanted for various crimes against the Kingdom, including leading a direct assault against the Crown and conspiracy."

#### dread_rob
- alias: "Terror of the Sea"
- features: "Pencil thin moustache and elegantly trimmed beard, never without a rapier."
- crimes: "Rob is wanted for piracy as well a conspiracy and for breaking out five traitors sentenced to death in the Royal Dungeons."

#### caneghor
- alias: "The Majestic Sage"
- features: "Voluminous robes, bald head, magic symbols engraved on body, levitating ability."
- crimes: "Canegor is wanted for grave robbing, conspiracy against the Crown, and plundering the Royal Library."

#### moradon
- alias: "None"
- features: "Always wearing armor and concealed weapons, has two prominent front teeth and an unkept beard."
- crimes: "Sir Moradon, an emissary from another land, is wanted for joining a conspiracy to topple the kingdom."

#### barrowpine
- alias: "The Elf Lord"
- features: "Pointed ears, sharp elfin features, pale blue eyes with no whites, glimmering enchanted coin."
- crimes: "The prince is one of the leaders of the conspiracy against the Crown, he also traffics stolen artifacts."

#### bargash
- alias: "Old One Eye"
- features: "Single eye centered in middle of forehead, over ten feet tall, only hair on body is in beard."
- crimes: "Bargash is wanted for conspiracy against the Crown and for leading an outright attack against the King."

#### rinaldus
- alias: "The Death Lord"
- features: "Rinaldus is a magically animated skeleton, an undead, he is easily identifiable by the ancient crown he wears."
- crimes: "Rinaldus is wanted for conspiracy against the Crown and leading a rebellion on the continent of Saharia."

#### ragface
- alias: "None"
- features: "Ragface is an undead, he is covered from head to foot in moldering green strips of cloth. A rotting smell follows him."
- crimes: "Conspiracy against the Crown and leading an insurrection in Saharia."

#### mahk
- alias: "Bruiser"
- features: "Bright orange body hair on a fluorescent green body, a tendency to shout for no apparent reason."
- crimes: "Mahk is wanted for the conspiracy against the Crown, leading a jail break, and for piracy on the open seas."

#### auric
- alias: "The Barbarian"
- features: "Auric is heavily muscled and wears a protective skin made from the hides of baby lambs."
- crimes: "Auric is wanted for conspiracy and for leading the rebellion of the continent Saharia."

#### czar_nickolai
- alias: "The Mad Czar"
- features: "The Czar has eyes which change color constantly, also a sulphuric smell emanates from his body."
- crimes: "The Czar is wanted for conspiracy against the Crown, violating diplomatic status, and murder."

#### magus
- alias: "None"
- features: "Pupil-less eyes, flowing white beard, always wears crimson robes and a matching skull cap."
- crimes: "Magus is wanted for conspiracy against the Crown and for practicing forbidden magics."

#### urthrax
- alias: "The Demon King"
- features: "Green, scaly skin, glowing red eyes, horns protruding from sides of head, over 7 feet tall."
- crimes: "Urthrax is wanted for conspiracy against the Crown."

#### arech
- alias: "Mastermind"
- features: "Arech is an immense dragon with a green body and blue wings, he breathes fire."
- crimes: "Arech is wanted for leading the conspiracy against the Crown, arranging jail- breaks, fomenting rebellion, stealing the Sceptre of Order."

### J.19 strings.ui — 6 entries (top-level keys)

```
  press_esc_to_exit: "Press 'ESC' to exit"
  quit_to_dos_prompt: " Quit to DOS without saving (y/n) "
  out_of_control: "OUT OF CONTROL"
  worldmap_hint_your_map: "'ESC' to exit / 'SPC' your map"
  worldmap_hint_whole_map: "'ESC' to exit / 'SPC' whole map"
  empty_slot: "Empty"
```


### J.20 strings.win

```
header: "Congratulations, %NAME%"
body: "You have recovered the\nSceptre of Order from\nthe clutches of the evil\nMaster Villains.\n\nAs a reward for saving himself\nand the four continents from\nruin, King Maximus and his\nsubjects reward you with a\nlarge parcel of land, a rank\nof nobility, and a medal\nannouncing your victory."
footer: "Final Score: %SCORE%"
```

### J.21 strings.lose

```
header: "Sorry, %NAME% the %RANK%"
body: "you have failed to\nrecover the Sceptre of Order\nin time to save the land!\n\nBeloved King Maximus has died\nand the Demon King Urthrax\nKillspite rules in his place.\n\nThe Four Continents lay in ruin\nabout you, its people doomed\nto a life of misery and\noppression because you could\nnot find the Sceptre."
footer: "Final Score: %SCORE%"
```

---

## Appendix K — Resource struct catalog (verbatim from `src/resources.h`)

This appendix mirrors every public typedef in `src/resources.h` so
the document can serve as a one-stop reference for the data
schema. Field ordering matches the source.


### K.1 Time / tuning / score / chest / economy

```c
typedef struct {
    int day_steps;
    int week_days;
    int days_per_difficulty[4];   // [easy, normal, hard, impossible]
} ResTime;
typedef struct {
    // Instant Army count = (spell_power + 1) * multiplier[rank].
    // OpenKB bounty.c:213 instant_army_multiplier[MAX_RANKS] = {3,2,1,1}.
    int instant_army_multiplier[4];
    // Search Area cost in days (OpenKB ask_search, game.c:4580). Used both
    // for the day deduction and as %DAYS% in body_search.
    int search_cost_days;
} ResTuning;
typedef struct {
    int per_villain;
    int per_artifact;
    int per_castle;
    int kill_penalty;
    int difficulty_multiplier[5];
    bool easy_halves;
} ResScoring;
typedef struct {
    int chance_gold[4];
    int chance_commission[4];
    int chance_spell_power[4];
    int chance_max_spells[4];
    int chance_new_spell[4];
    int gold_min[4];
    int gold_max[4];
    int commission_min[4];
    int commission_max[4];
    int max_spells_base[4];
} ResChest;
typedef struct {
    int alcove_cost;
    int boat_cost_normal;
    int boat_cost_cheap;
    int siege_cost;
    ResChest chest;
    ResScoring scoring;
} ResEconomy;
```

### K.2 Contract / spawn / world / window

```c
typedef struct {
    int cycle_length;
    int initial_last_contract;
} ResContract;
typedef struct {
    int  chance_curve[RES_SPAWN_TIERS][RES_SPAWN_POOL_N - 1];
    char troop_pool  [RES_SPAWN_TIERS][RES_SPAWN_POOL_N][RES_ID_LEN];
} ResSpawn;
typedef struct {
    char starting_zone[RES_ID_LEN];
    char zone_noun[RES_ID_LEN];
    char zone_noun_plural[RES_ID_LEN];
    int  max_army_slots;
    int  fog_sight;
    // Initial player state defaults, used by GameInit when no override exists.
    char default_name[RES_NAME_LEN];   // fallback when player enters no name
    int  default_options[6];           // delay, sounds, walk_beep, anim, army_size, cga
} ResWorld;
typedef struct {
    int base_w;
    int base_h;
} ResWindow;
```

### K.3 Town / castle / tile-code

```c
typedef struct {
    int  index;
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char zone[RES_ID_LEN];        // was "continent" in KB; engine calls it zone
    int  x, y;
    int  boat_x, boat_y;          // -1 if no water adjacency
    int  gate_x, gate_y;          // -1 if not set
    char intel_castle[RES_ID_LEN];// castle id the "gather info" command in
                                  // this town reports on. Empty = none.
    char pinned_spell[RES_ID_LEN];// spell id pre-placed here by salt_spells.
                                  // Empty = no pin (OpenKB: only hunterville
                                  // pins "bridge"; any town may pin in mods).
} ResTown;
typedef struct {
    char flow[RES_ID_LEN];           // "" / "audience" / ...
    bool excluded_from_siege;
    bool excluded_from_intel;
    bool excluded_from_contract;     // villain assignment skips this castle
    char dialog_header[RES_NAME_LEN];
    char dialog_body[320];
    char win_condition[RES_ID_LEN];  // "scepter" triggers end-game on visit
    // Audience-flow text variants (templates with %NAME% / %RANK% /
    // %NEEDED% / %S% substitutions). Only populated when flow == "audience".
    char audience_intro[320];
    char audience_rank_up[320];
    char audience_more_needed[320];
    char audience_final_rank[320];
} ResCastleSpecial;
typedef struct {
    int  index;
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char zone[RES_ID_LEN];
    int  x, y;                    // -1 for entries without coords (e.g. King Maximus)
    int  gate_x, gate_y;          // tile player lands on from Castle Gate
                                  // spell. Defaults to (x, y+1) if absent.
    int  difficulty_tier;         // 0-3 (plains/forest/hill/dungeon), used by
                                  // roll_creature-style monster generation.
                                  // OpenKB bounty.c:558 castle_difficulty[].
    ResCastleSpecial special;
} ResCastle;
typedef struct {
    bool present;                 // false = unused code
    char art[RES_TILE_ART_LEN];
    int  terrain;                 // Terrain enum value (see tile.h)
    bool blocks_foot;
    bool is_bridge;
} ResTileCode;
```

### K.4 Per-zone object structs

```c
typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    char title[RES_SIGN_TITLE_LEN];
    char body[RES_SIGN_BODY_LEN];
} ResSign;
typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    int  boat_x, boat_y;
} ResZoneTown;
typedef struct {
    int  dx, dy;
    char art[RES_ID_LEN];
} ResCastleDecor;
typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    int  decor_count;
    ResCastleDecor decorations[RES_MAX_CASTLE_DECOR];
} ResZoneCastle;
typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
} ResZoneChest;
typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
} ResZoneArtifact;
typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    char kind[RES_ID_LEN];        // "plains" / "forest" / "hills" / "dungeon"
} ResZoneDwelling;
typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
} ResZoneArmy;
typedef struct {
    int artifacts;
    int navmaps;
    int orbs;
    int telecaves;
    int dwellings;
    int friendly_foes;
    // OpenKB has no hostile_foes salt budget — hostiles come from static
    // armies[] placements in the zone definition.

    // OpenKB continent_dwellings[c][]: ordered list of preferred troop ids
    // for the first N salt dwelling slots on this zone. After the list is
    // exhausted, salt_continent falls back to a uniform random pick from
    // dwelling_range_min..dwelling_range_max (catalog indices, OpenKB
    // dwelling_ranges[c][]).
    int  preferred_troop_count;
    char preferred_troops[16][RES_ID_LEN];
    int  dwelling_range_min;   // troop catalog index (inclusive)
    int  dwelling_range_max;   // troop catalog index (inclusive)
} ResZoneSalt;
typedef struct {
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char map_path[RES_PATH_LEN];
    int  width, height;
    int  hero_spawn_x, hero_spawn_y;
    int  neighbor_count;
    char neighbors[RES_MAX_NEIGHBORS][RES_ID_LEN];

    // Per-zone object placements. Interactive overlays come from here —
    // the .dat only carries terrain + edge art.
    int  sign_count;     ResSign          signs[RES_MAX_ZONE_OBJECTS];
    int  town_count;     ResZoneTown      towns[RES_MAX_ZONE_OBJECTS];
    int  castle_count;   ResZoneCastle    castles[RES_MAX_ZONE_OBJECTS];
    int  chest_count;    ResZoneChest     chests[RES_MAX_ZONE_OBJECTS];
    int  artifact_count; ResZoneArtifact  artifacts[RES_MAX_ZONE_OBJECTS];
    int  dwelling_count; ResZoneDwelling  dwellings[RES_MAX_ZONE_OBJECTS];
    int  army_count;     ResZoneArmy      armies[RES_MAX_ZONE_OBJECTS];

    // Salt budget — randomized objects placed by salt_continent at GameInit.
    ResZoneSalt salt;

    // Zone is the starting continent. Exactly one zone should set this.
    bool is_home;
    // Starting tile on a home zone. Only meaningful when is_home == true.
    int  home_spawn_x, home_spawn_y;

    // Magic alcove tile (optional). If set and the player class already
    // knows magic, GameInit removes this overlay so it cannot be claimed.
    // -1,-1 = no alcove in this zone.
    int  magic_alcove_x, magic_alcove_y;
} ResZone;
```

### K.5 Villain / end-text / colors

```c
typedef struct {
    char id[RES_ID_LEN];
    char alias[RES_NAME_LEN];
    char features[RES_VDESC_TEXT_LEN];
    char crimes[RES_VDESC_TEXT_LEN];
} ResVillainDesc;
typedef struct {
    char header[RES_NAME_LEN];
    char body[RES_END_BODY_LEN];
    char footer[RES_NAME_LEN];
} ResEndText;
typedef struct {
    // Minimap tile colors by terrain (classic/views.c terrain_minimap_color).
    unsigned int minimap_grass;
    unsigned int minimap_forest;
    unsigned int minimap_mountain;
    unsigned int minimap_water;
    unsigned int minimap_desert;
    unsigned int minimap_fog;
    // Status bar background color by difficulty (classic/chrome.c).
    unsigned int difficulty_easy;
    unsigned int difficulty_normal;
    unsigned int difficulty_hard;
    unsigned int difficulty_impossible;
} ResColors;
```

### K.6 Banners / UI labels / count-buckets

```c
typedef struct {
    // Treasure-chest outcomes (OpenKB take_chest, game.c:3094-3263).
    // Substitutions: %GOLD%, %LEADERSHIP%, %POINTS%, %COUNT%, %SPELL%.
    char chest_gold[RES_BANNER_LEN];
    char chest_commission[RES_BANNER_LEN];
    char chest_spell_power[RES_BANNER_LEN];
    char chest_max_spells[RES_BANNER_LEN];
    char chest_new_spell[RES_BANNER_LEN];
    char chest_empty[RES_BANNER_LEN];

    // Town overlay header + sticky widgets (OpenKB visit_town, game.c:2585).
    // Substitutions: %NAME%, %GOLD%.
    char town_header[RES_BANNER_LEN];
    char town_gold_label[RES_BANNER_LEN];

    // Town menu rows. The boat/spell/siege rows toggle between two
    // strings depending on game state. Substitutions: %COST%, %SPELL%,
    // %SPELL_COST%, %SIEGE_COST%.
    char town_row_contract[RES_BANNER_LEN];
    char town_row_boat_rent[RES_BANNER_LEN];     // %COST%
    char town_row_boat_cancel[RES_BANNER_LEN];
    char town_row_info[RES_BANNER_LEN];
    char town_row_spell[RES_BANNER_LEN];         // %SPELL% %SPELL_COST%
    char town_row_spell_none[RES_BANNER_LEN];
    char town_row_siege_buy[RES_BANNER_LEN];     // %SIEGE_COST%
    char town_row_siege_owned[RES_BANNER_LEN];

    // Town action toasts. Substitutions: %COST%, %LEFT%, %S%, %VILLAIN%,
    // %REWARD%, %ZONE%.
    char town_contract_new[RES_BANNER_LEN];
    char town_contract_none[RES_BANNER_LEN];
    char town_boat_vacate_first[RES_BANNER_LEN];
    char town_no_gold[RES_BANNER_LEN];
    char town_intel_unavailable[RES_BANNER_LEN];
    char town_intel_castle_under[RES_BANNER_LEN];// %NAME%
    char town_intel_owner_rule[RES_BANNER_LEN];  // %OWNER%
    char town_intel_owner_none[RES_BANNER_LEN];
    char town_intel_owner_player[RES_BANNER_LEN];
    char town_intel_owner_king[RES_BANNER_LEN];
    char town_intel_count_named[RES_BANNER_LEN]; // %LABEL% %TROOP%
    char town_intel_count_numeric[RES_BANNER_LEN];// %COUNT% %TROOP%
    char town_intel_monsters_generic[RES_BANNER_LEN];
    char town_intel_no_garrison[RES_BANNER_LEN];
    char town_spell_unavailable[RES_BANNER_LEN];
    char town_spell_at_cap[RES_BANNER_LEN];
    char town_spell_can_learn[RES_BANNER_LEN];   // %LEFT% %S% (s/empty)
    char town_siege_already[RES_BANNER_LEN];
    char town_siege_purchased[RES_BANNER_LEN];

    // Spell-effect dialog bodies (OpenKB choose_spell, game.c:4130).
    // Substitutions: %STEPS%, %CASTLE%, %COUNT%, %QTY%, %TROOP%, %AMOUNT%.
    char spell_time_stop[RES_BANNER_LEN];                 // %STEPS%
    char spell_find_villain_no_contract[RES_BANNER_LEN];
    char spell_find_villain_success[RES_BANNER_LEN];      // %CASTLE%
    char spell_find_villain_none[RES_BANNER_LEN];
    char spell_bridge_prompt[RES_BANNER_LEN];
    char spell_bridge_built[RES_BANNER_LEN];              // %COUNT%
    char spell_bridge_invalid[RES_BANNER_LEN];
    char spell_castle_gate_none[RES_BANNER_LEN];
    char spell_castle_gate_choose[RES_BANNER_LEN];
    char spell_town_gate_none[RES_BANNER_LEN];
    char spell_town_gate_choose[RES_BANNER_LEN];
    char spell_instant_army_fizzle[RES_BANNER_LEN];
    char spell_instant_army_no_room[RES_BANNER_LEN];
    char spell_instant_army_success[RES_BANNER_LEN];      // %QTY% %TROOP%
    char spell_raise_control_success[RES_BANNER_LEN];     // %AMOUNT%
    char spell_gate_teleported[RES_BANNER_LEN];
    char spell_gate_invalid[RES_BANNER_LEN];

    // Foe encounters (OpenKB attack_foe / accept_foe, game.c:3574-3673).
    // Substitutions: %LABEL%, %COUNT%, %TROOP%, %COST%.
    char encounter_join_named[RES_BANNER_LEN];     // friendly w/ word count
    char encounter_join_numeric[RES_BANNER_LEN];   // friendly w/ numeric
    char encounter_wanderers[RES_BANNER_LEN];      // friendly refused
    char encounter_hostile_header[RES_BANNER_LEN]; // composite prefix
    char encounter_hostile_unknown[RES_BANNER_LEN];// fallback line
    char encounter_hostile_count_named[RES_BANNER_LEN];   // %LABEL% %TROOP%
    char encounter_hostile_count_numeric[RES_BANNER_LEN]; // %COUNT% %TROOP%

    // Archmage Aurange alcove (OpenKB visit_alcove, game.c:2831).
    // Substitutions: %COST%.
    char alcove_offer[RES_BANNER_LEN];
    char alcove_already[RES_BANNER_LEN];
    char alcove_taught[RES_BANNER_LEN];
    char alcove_no_gold[RES_BANNER_LEN];
    char no_spell_banner[RES_BANNER_LEN];

    // Dwelling recruitment (OpenKB visit_dwelling, game.c:2918).
    // Substitutions: %COUNT%, %TROOP%, %COST%, %GOLD%, %CAP%.
    char dwelling_recruit_prompt[RES_BANNER_LEN];
    char dwelling_none_this_week[RES_BANNER_LEN];
    char dwelling_empty[RES_BANNER_LEN];

    // Adventure-tile interactions (OpenKB visit_telecave / take_chest variants
    // for navmap / orb). Substitutions: %ZONE%.
    char telecave_teleport[RES_BANNER_LEN];
    char telecave_inert[RES_BANNER_LEN];
    char navmap_pickup[RES_BANNER_LEN];
    char navmap_torn[RES_BANNER_LEN];
    char crystal_ball_pickup[RES_BANNER_LEN];

    // End-of-week / temp_death (OpenKB end_of_week game.c:3673, temp_death
    // play.c:950). Substitutions: %WEEK%, %TROOP%.
    char astrology_header[RES_BANNER_LEN];
    char astrology_body[RES_BANNER_LEN];
    char temp_death[RES_BANNER_LEN];

    // Pre-combat scout report (combat.c stub). Substitutions: %COUNT% %TROOP%.
    char combat_scouts_header[RES_BANNER_LEN];
    char combat_scouts_count[RES_BANNER_LEN];
    char combat_scouts_small_band[RES_BANNER_LEN];
    char combat_placeholder[RES_BANNER_LEN];
    char combat_header_siege[RES_BANNER_LEN];      // dialog title
    char combat_header_default[RES_BANNER_LEN];    // dialog title

    // Signposts and unrecognized tiles (OpenKB read_signpost game.c:3022,
    // adventure.c fallback dialog). Substitutions: %TITLE%, %BODY%, %ID%, %KIND%.
    char signpost_with_body[RES_BANNER_LEN];
    char signpost_title_only[RES_BANNER_LEN];
    char relic_unrecognized[RES_BANNER_LEN];
    char relic_unrecognized_header[RES_BANNER_LEN];
    char tile_fallback_header[RES_BANNER_LEN];
    char tile_fallback_named[RES_BANNER_LEN];
    char tile_fallback_unnamed[RES_BANNER_LEN];

    // End-of-week budget panel (main.c WK_PHASE_BUDGET, OpenKB end_of_week
    // game.c:3707). Substitutions: %WEEK%.
    char budget_header[RES_BANNER_LEN];
    char budget_on_hand[RES_BANNER_LEN];
    char budget_payment[RES_BANNER_LEN];
    char budget_boat[RES_BANNER_LEN];
    char budget_army[RES_BANNER_LEN];
    char budget_balance[RES_BANNER_LEN];

    // Status bar (chrome.c). Substitutions: %DAYS%, %STEPS%.
    char status_days_left[RES_BANNER_LEN];
    char status_time_stop[RES_BANNER_LEN];

    // Composite prompt bodies (system flows that have %TOKEN%-style values).
    // Substitutions: %NAME%, %COST%, %GOLD%, %CAP%, %INDEX%, %TROOP%,
    // %COUNT%, %ZONE%, %REASON%.
    char body_save_confirm[RES_BANNER_LEN];
    char body_search[RES_BANNER_LEN];
    char body_dismiss_pick[RES_BANNER_LEN];
    char body_dismiss_last[RES_BANNER_LEN];
    char body_home_castle[RES_BANNER_LEN];
    char body_own_castle[RES_BANNER_LEN];           // %NAME%
    char body_garrison_row_named[RES_BANNER_LEN];   // %INDEX% %TROOP% %COUNT%
    char body_garrison_row_empty[RES_BANNER_LEN];   // %INDEX%
    char body_navigate_row[RES_BANNER_LEN];         // %INDEX% %ZONE%
    char body_no_continents[RES_BANNER_LEN];
    char body_must_be_sailing[RES_BANNER_LEN];
    char recruit_home_prompt[RES_BANNER_LEN];       // %COST% %GOLD% %CAP%

    // Short error / outcome banners reused by recruit and home-castle paths.
    char no_castle_troops[RES_BANNER_LEN];
    char cannot_garrison_last[RES_BANNER_LEN];
    char no_troop_slots[RES_BANNER_LEN];
    char army_cannot_handle[RES_BANNER_LEN];
    char no_troops_to_garrison[RES_BANNER_LEN];
    char castle_garrison_empty[RES_BANNER_LEN];
    char spell_unavailable[RES_BANNER_LEN];
    char spell_not_known[RES_BANNER_LEN];
    char spell_unknown[RES_BANNER_LEN];
} ResBanners;
typedef struct {
    int  threshold;                       // count <= threshold => use label
    char label[RES_UI_LABEL_LEN];
} ResCountBucket;
typedef struct {
    // Generic UI strings.
    char press_esc_to_exit[RES_UI_LABEL_LEN];
    // Status-bar fast-quit prompt (spec §24.38). Rendered into the
    // top status bar via KB_TopBox, not a bottom dialog.
    char quit_to_dos_prompt[RES_UI_LABEL_LEN * 2];
    char out_of_control[RES_UI_LABEL_LEN];
    char worldmap_hint_your_map[RES_UI_LABEL_LEN];
    char worldmap_hint_whole_map[RES_UI_LABEL_LEN];

    // Menu labels.
    char menu_root_title[RES_UI_LABEL_LEN];
    char menu_views_title[RES_UI_LABEL_LEN];
    char menu_options_title[RES_UI_LABEL_LEN];
    char menu_back[RES_UI_LABEL_LEN];
    char menu_exit[RES_UI_LABEL_LEN];
    char menu_save[RES_UI_LABEL_LEN];
    char menu_load[RES_UI_LABEL_LEN];
    char menu_new_game[RES_UI_LABEL_LEN];
    char menu_views[RES_UI_LABEL_LEN];
    char menu_options[RES_UI_LABEL_LEN];
    char menu_army[RES_UI_LABEL_LEN];
    char menu_spells[RES_UI_LABEL_LEN];
    char menu_character[RES_UI_LABEL_LEN];
    char menu_contract[RES_UI_LABEL_LEN];
    char menu_puzzle[RES_UI_LABEL_LEN];
    char menu_view_map[RES_UI_LABEL_LEN];

    // Character-view stat labels.
    char stat_leadership[RES_UI_LABEL_LEN];
    char stat_commission[RES_UI_LABEL_LEN];
    char stat_gold[RES_UI_LABEL_LEN];
    char stat_spell_power[RES_UI_LABEL_LEN];
    char stat_max_spells[RES_UI_LABEL_LEN];
    char stat_villains_caught[RES_UI_LABEL_LEN];
    char stat_artifacts_found[RES_UI_LABEL_LEN];
    char stat_castles_garrisoned[RES_UI_LABEL_LEN];
    char stat_followers_killed[RES_UI_LABEL_LEN];
    char stat_current_score[RES_UI_LABEL_LEN];

    // Army-view inline label prefixes.
    char army_skill[RES_UI_LABEL_LEN];        // "SL:"
    char army_move[RES_UI_LABEL_LEN];         // "MV:"
    char army_morale[RES_UI_LABEL_LEN];       // "Morale:"
    char army_hit_points[RES_UI_LABEL_LEN];   // "HitPts:"
    char army_damage[RES_UI_LABEL_LEN];       // "Damage:"
    char army_g_cost[RES_UI_LABEL_LEN];       // "G-Cost:"

    // Morale enum labels.
    char morale_normal[RES_UI_LABEL_LEN];
    char morale_low[RES_UI_LABEL_LEN];
    char morale_high[RES_UI_LABEL_LEN];

    // Count buckets (sorted ascending by threshold).
    int            count_buckets_army_view_n;
    ResCountBucket count_buckets_army_view[RES_MAX_COUNT_BUCKETS];
    int            count_buckets_instant_army_n;
    ResCountBucket count_buckets_instant_army[RES_MAX_COUNT_BUCKETS];

    // Difficulty labels (indexed by Difficulty enum 0..3).
    ResDifficultyLabel difficulty[4];

    // Keybind labels.
    int        keybind_count;
    ResKeybind keybinds[RES_MAX_KEYBINDS];

    // Startup strings.
    char startup_controls_hint[RES_UI_LABEL_LEN * 2];
    char startup_class_select_hint[RES_UI_LABEL_LEN * 2];
    char startup_class_picker_missing[RES_UI_LABEL_LEN * 2];
    char startup_save_picker_title[RES_UI_LABEL_LEN];
    char startup_save_picker_empty[RES_UI_LABEL_LEN];
    char startup_save_picker_new_game[RES_UI_LABEL_LEN];
    char startup_new_game_table_header[RES_UI_LABEL_LEN * 2];
    char startup_new_game_select_hint[RES_UI_LABEL_LEN * 2];

    // Controls panel labels.
    char controls_title[RES_UI_LABEL_LEN];
    char controls_on[RES_UI_LABEL_LEN];
    char controls_off[RES_UI_LABEL_LEN];

    // Prompt hint lines (classic/prompt.c). Substitutions: %COUNT%.
    char prompt_text_hint[RES_UI_LABEL_LEN * 2];
    char prompt_numeric_range_hint[RES_UI_LABEL_LEN];
    char prompt_yes_no_hint[RES_UI_LABEL_LEN];
    char prompt_numeric_5_hint[RES_UI_LABEL_LEN];

    // Dialog/prompt header titles (game.json strings.dialog_titles).
    // Spell-flow headers come from spell_by_id()->name and are not stored
    // here; everything else (artifact pickups, system flows, fallbacks)
    // gets a slot.
    char dt_treasure[RES_UI_LABEL_LEN];
    char dt_teleport_cave[RES_UI_LABEL_LEN];
    char dt_crystal_ball[RES_UI_LABEL_LEN];
    char dt_foes[RES_UI_LABEL_LEN];
    char dt_alcove_offer[RES_UI_LABEL_LEN];   // Archmage Aurange
    char dt_alcove_result[RES_UI_LABEL_LEN];  // Aurange
    char dt_strange_relic[RES_UI_LABEL_LEN];
    char dt_castle_default[RES_UI_LABEL_LEN]; // home-castle picker fallback
    char dt_own_castle[RES_UI_LABEL_LEN];
    char dt_search[RES_UI_LABEL_LEN];
    char dt_dismiss_army[RES_UI_LABEL_LEN];
    char dt_dismiss_last[RES_UI_LABEL_LEN];
    char dt_navigate[RES_UI_LABEL_LEN];
    char dt_garrison_pick[RES_UI_LABEL_LEN];
    char dt_remove_pick[RES_UI_LABEL_LEN];
    char dt_save_confirm[RES_UI_LABEL_LEN];
    char dt_lose_fallback[RES_UI_LABEL_LEN];   // header when win_text.header empty
    char dt_win_fallback[RES_UI_LABEL_LEN];

    // Misc fallbacks.
    char empty_slot[RES_UI_LABEL_LEN];   // garrison-row "Empty" placeholder

    // Save/load + game-state toasts (game.json strings.toasts).
    // Substitutions: %REASON%.
    char toast_save_cancelled[RES_UI_LABEL_LEN];
    char toast_save_ok[RES_UI_LABEL_LEN];
    char toast_save_failed[RES_UI_LABEL_LEN];
    char toast_load_cancelled[RES_UI_LABEL_LEN];
    char toast_load_ok[RES_UI_LABEL_LEN];
    char toast_load_failed[RES_UI_LABEL_LEN];
    char toast_new_game[RES_UI_LABEL_LEN];

    // Contract / villain-detail view labels (game.json strings.contract_view).
    // Substitutions: %VALUE%.
    char cv_title_no_contract[RES_UI_LABEL_LEN];
    char cv_label_name[RES_UI_LABEL_LEN];
    char cv_label_alias[RES_UI_LABEL_LEN];
    char cv_label_reward[RES_UI_LABEL_LEN];
    char cv_label_last_seen[RES_UI_LABEL_LEN];
    char cv_label_castle[RES_UI_LABEL_LEN];
    char cv_alias_none[RES_UI_LABEL_LEN];
    char cv_castle_unknown[RES_UI_LABEL_LEN];
    char cv_features_header[RES_UI_LABEL_LEN];
    char cv_crimes_header[RES_UI_LABEL_LEN];

    // Spells view labels (game.json strings.spells_view).
    char sv_title[RES_UI_LABEL_LEN];
    char sv_combat_col[RES_UI_LABEL_LEN];
    char sv_adventure_col[RES_UI_LABEL_LEN];
} ResUI;
```

### K.7 Top-level Resources struct

```c
typedef struct {
    char game_id[RES_ID_LEN];
    char title[RES_NAME_LEN];
    int  version;

    ResTime     time;
    ResEconomy  economy;
    ResContract contract;
    ResSpawn    spawn;
    ResWorld    world;
    ResWindow   window;
    ResTuning   tuning;
    ResColors   colors;
    ResUI       ui;

    int         town_count;
    ResTown     towns[RES_MAX_TOWNS];

    int         castle_count;
    ResCastle   castles[RES_MAX_CASTLES];

    int         zone_count;
    ResZone     zones[RES_MAX_ZONES];

    // Indexed by raw byte from the .dat. `present == false` means unused.
    ResTileCode tile_codes[RES_TILE_CODE_COUNT];

    // Catalogs (loaded from game.json top-level arrays).
    int         troops_count;
    TroopDef    troops[CAT_TROOPS_MAX];

    int         spells_count;
    SpellDef    spells[CAT_SPELLS_MAX];

    int         classes_count;
    ClassDef    classes[CAT_CLASSES_MAX];

    int         villains_count;
    VillainDef  villains[CAT_VILLAINS_MAX];

    int         artifacts_count;
    ArtifactDef artifacts[CAT_ARTIFACTS_MAX];

    // Combat rules — cross-group morale chart (OpenKB morale_chart).
    // Indexed by (my_group - 'A', their_group - 'A'); values are 'N'/'L'/'H'.
    char morale_chart[5][5];

    // Fuzzy-number labels for intelligence / enemy-sight text
    // (OpenKB number_names/number_mins at src/bounty.c:458).
    // Entries are ordered high-to-low by threshold: the first entry
    // whose threshold is <= count wins. Up to 6 buckets.
    int  number_name_count;
    int  number_name_thresholds[8];
    char number_name_labels[8][24];

    // Controls settings panel (OpenKB controls_menu, game.c:5276).
    // Each entry describes one row: a label, a type ("bool" or "numeric"),
    // a default value, and (for numeric) the range cap.
    struct {
        int count;
        struct {
            char id[RES_ID_LEN];
            char label[24];
            char type[16];       // "bool" or "numeric"
            int  range;          // numeric upper bound (exclusive)
            int  def;            // default value
            bool hidden;         // not shown unless CGA mode active
        } items[6];
    } controls;

    // End-game text blocks (strings.win, strings.lose in game.json).
    ResEndText win_text;
    ResEndText lose_text;

    // Player-facing banner templates (strings.banners in game.json).
    ResBanners banners;

    // Credits screen lines (OpenKB show_credits, game.c:678). Shown once
    // between the title splash and the class picker. %VERSION% is
    // substituted with res->version at render time. Modpacks override
    // per game pack.
    int  credits_count;
    char credits[16][96];

    // Victory cartoon tiles + parameters (OpenKB display_cartoon,
    // game.c:4281-4396). Tile art is sized to CL_TILE_W x CL_TILE_H so
    // each cell lines up with the map viewport.
    struct {
        char grass_tile[RES_PATH_LEN];   // GR_ENDTILE sub 0
        char carpet_tile[RES_PATH_LEN];  // GR_ENDTILE sub 1
        char hero_tile[RES_PATH_LEN];    // GR_ENDTILE sub 2
        char throne_backdrop[RES_PATH_LEN]; // optional post-cartoon image
        int  grid_width;                 // default 6
        int  grid_height;                // default 5
        int  carpet_column;              // x-column the carpet/hero use
        int  carpet_length;              // max carpet tiles (5 in OpenKB)
        int  frame_count;                // animation frame cap (10)
        int  ticks_per_step;             // advance frame every N ticks (2)
        bool troop_border;               // animate troop walk frames around edges
    } ending;

    // Per-villain description blocks (strings.villain_descriptions).
    int             villain_desc_count;
    ResVillainDesc  villain_descs[CAT_VILLAINS_MAX];

    // Role-fixed sprite manifest (assets that aren't per-catalog-entry).
    struct {
        char font[RES_PATH_LEN];
        // Hero.
        char hero_walk[RES_ANIM_FRAMES][RES_PATH_LEN];
        char hero_boat[RES_ANIM_FRAMES][RES_PATH_LEN];
        // UI.
        char puzzle_cover[RES_PATH_LEN];
        // Location backdrops (OpenKB GR_LOCATION sub_ids 0..5).
        char town_backdrop[RES_PATH_LEN];
        char castle_backdrop[RES_PATH_LEN];
        char plains_backdrop[RES_PATH_LEN];
        char forest_backdrop[RES_PATH_LEN];
        char hillcave_backdrop[RES_PATH_LEN];
        char dungeon_backdrop[RES_PATH_LEN];
        // End-game images (OpenKB GR_ENDING sub_id 0=win, 1=lose).
        char ending_win[RES_PATH_LEN];
        char ending_lose[RES_PATH_LEN];
        int  view_icons_extra_count;
        char view_icons_extra[RES_EXTRA_ICONS][RES_PATH_LEN];
        // HUD panels.
        char hud_contract_silhouette[RES_PATH_LEN];
        char hud_siege_silhouette[RES_PATH_LEN];
        char hud_siege_animation[RES_ANIM_FRAMES][RES_PATH_LEN];
        char hud_magic_silhouette[RES_PATH_LEN];
        char hud_magic_animation[RES_ANIM_FRAMES][RES_PATH_LEN];
        char hud_puzzle_grid[RES_PATH_LEN];
        char hud_gold_purse[RES_PATH_LEN];
        char hud_bar_strip[RES_PATH_LEN];   // 320x5 middle bar (GR_SELECT, 1)
        char hud_coin[3][RES_PATH_LEN];     // gold / silver / copper
        char chrome_overworld[RES_PATH_LEN]; // 320x200 frame bitmap,
                                             // transparent interior; pixel-
                                             // exact copy of reference chrome.
        char splash_logo[RES_PATH_LEN];      // publisher logo (first splash)
        char splash_title[RES_PATH_LEN];     // game title (second splash)
        char class_picker[RES_PATH_LEN];     // 288x184 A-D class portrait image
        char class_highlight[RES_PATH_LEN];  // 42x44 cursor glow for class picker
        char orb[RES_PATH_LEN];              // orb of power tile overlay
    } sprites;
} Resources;
```

---

## Appendix L — Complete `strings.banners` inventory

Full dump of every banner string (117 entries). Newlines
rendered as `\n` for readability.

```
alcove_already: "The archmage nods.\nYou already know magic."
alcove_no_gold: "You have not enough gold!\n%COST% is required.\nBegone until you do!"
alcove_offer: "The venerable Archmage,\nAurange, will teach you the\nsecrets of spell casting for\n%COST% gold.\n\nAccept?"
alcove_taught: "Aurange teaches you the\narcane arts. You now know\nthe ways of magic."
army_cannot_handle: "Your army cannot handle\nthat many troops."
astrology_body: "Astrologers proclaim:\nWeek of the %TROOP%\n\nAll %TROOP% dwellings are\nrepopulated.         (space)"
astrology_header: "Week #%WEEK%"
body_dismiss_last: "If you dismiss your last\narmy, you will be sent back\nto the King in disgrace.\nDismiss last army?"
body_dismiss_pick: "Dismiss which troop?"
body_garrison_row_empty: "%INDEX%. Empty\n"
body_garrison_row_named: "%INDEX%. %TROOP% (%COUNT%)\n"
body_home_castle: "Castle of King Maximus\n\nA) Recruit Soldiers\nB) Audience with the King"
body_must_be_sailing: "You must be sailing to\nnavigate to another\ncontinent."
body_navigate_row: "%INDEX%. %ZONE%\n"
body_no_continents: "No known continents from\nthis zone."
body_own_castle: "Castle %NAME%:\n1. Garrison troops\n2. Remove troops"
body_save_confirm: "Press Control-Q to Quit or\nany other key to continue."
body_search: "It will take %DAYS% days to\nsearch this area.\nSearch?"
budget_army: "Army   "
budget_balance: "Balance"
budget_boat: "Boat   "
budget_header: "Week #%WEEK%             Budget"
budget_on_hand: "On Hand"
budget_payment: "Payment"
cannot_garrison_last: "You cannot garrison your\n        last army!"
castle_garrison_empty: "Castle garrison is empty."
chest_commission: "After surveying the area,\nyou discover that it is\nrich in mineral deposits.\n\nThe King rewards you for\nyour find by increasing\nyour weekly income by %POINTS%"
chest_empty: "The chest was empty!"
chest_gold: "After scouring the area,\nyou fall upon a hidden\ntreasure cache. You may:\nA) Take the %GOLD% gold.\nB) Distribute the gold to\nthe peasants, increasing\nyour leadership by %LEADERSHIP%."
chest_max_spells: "A tribe of nomads greet you\nand your army warmly. Their\nshaman, in awe of your\nprowess, teaches you the\nsecret of his tribe's magic.\nYour maximum spell capacity\nis increased by %POINTS%"
chest_new_spell: "You have captured a\nmischevious imp which has\nbeen terrorizing the\nregion. In exchange for\nits release, you receive:\n\n   %COUNT% %SPELL% spell."
chest_spell_power: "Traversing the area, you\nstumble upon a time worn\ncannister. Curious, you un-\nstop the bottle, releasing\na powerful genie who raises\nyour Spell Power by %POINTS% and\nvanishes."
combat_header_default: "Combat"
combat_header_siege: "Siege"
combat_placeholder: "\n(Battle resolves off-screen until\ncombat is implemented. Victory\ngranted.)"
combat_scouts_count: "  %COUNT% %TROOP%\n"
combat_scouts_header: "Your scouts have sighted:\n"
combat_scouts_small_band: "  (a small band)\n"
crystal_ball_pickup: "You gaze into a crystal ball\nand see all of %ZONE%!"
dwelling_empty: "This dwelling is empty."
dwelling_none_this_week: "No troops are available here\nthis week."
dwelling_recruit_prompt: "%COUNT% %TROOP% are available\nCost=%COST% each.  GP=%GOLD%\nYou may recruit up to %CAP%."
encounter_hostile_count_named: "  %LABEL% %TROOP%\n"
encounter_hostile_count_numeric: "  %COUNT% %TROOP%\n"
encounter_hostile_header: "You encounter:\n"
encounter_hostile_unknown: "  a hostile band\n"
encounter_join_named: "%LABEL% %TROOP%,\nwith desires of greater\nglory, wish to join you."
encounter_join_numeric: "%COUNT% %TROOP%,\nwith desires of greater\nglory, wish to join you."
encounter_wanderers: "They flee in terror at the\nsight of your vast army."
navmap_pickup: "A navigation map of %ZONE%!"
navmap_torn: "A tattered map of uncertain\nprovenance. You keep it anyway."
no_castle_troops: "No castle troops are available."
no_spell_banner: "You have not been trained in\nthe art of spellcasting yet.\nVisit the Archmage Aurange\nin Continentia at 11,19 for\nthis ability."
no_troop_slots: "No troop slots left!"
no_troops_to_garrison: "No troops to garrison."
recruit_home_prompt: "Cost=%COST% each.  GP=%GOLD%\nYou may recruit up to %CAP%."
relic_unrecognized: "It resists your touch."
relic_unrecognized_header: "A strange relic"
signpost_title_only: "A sign reads:\n\n"%TITLE%""
signpost_with_body: "A sign reads:\n\n"%TITLE%\n%BODY%""
spell_bridge_built: "Bridge constructed with\n%COUNT% tiles."
spell_bridge_invalid: "Not a suitable location\nfor a bridge."
spell_bridge_prompt: "Build bridge in which\ndirection? Use arrows."
spell_castle_gate_choose: "Which castle? Press A-K."
spell_castle_gate_none: "You have not visited\nany castles yet."
spell_find_villain_no_contract: "You have no contract."
spell_find_villain_none: "Your search reveals nothing."
spell_find_villain_success: "You have found %CASTLE%!"
spell_gate_invalid: "Invalid selection."
spell_gate_teleported: "Teleported!"
spell_instant_army_fizzle: "Spell fizzles."
spell_instant_army_no_room: "Your army has no room!"
spell_instant_army_success: "%QTY% %TROOP%\nhave joined your army."
spell_not_known: "You don't have that spell."
spell_raise_control_success: "Your leadership has\nincreased by %AMOUNT%."
spell_time_stop: "Time has stopped for %STEPS% steps."
spell_town_gate_choose: "Which town? Press A-K."
spell_town_gate_none: "You have not visited\nany towns yet."
spell_unavailable: "That spell is not available."
spell_unknown: "Unknown spell."
status_days_left: " Options / Controls / Days Left:%DAYS% "
status_time_stop: " Options / Controls / Time Stop:%STEPS% "
telecave_inert: "This cave hums with magic,\nbut nothing happens."
telecave_teleport: "You step into the cave and\nemerge elsewhere on this\ncontinent."
temp_death: "The King dismisses you in\ndisgrace. Return with a\nproper army."
tile_fallback_header: "You reach:"
tile_fallback_named: "%ID% (%KIND%)"
tile_fallback_unnamed: "a %KIND%"
town_boat_vacate_first: "\n\nPlease vacate the boat first"
town_contract_new: "New contract: %VILLAIN%.\nReward: %REWARD% gold.\nLast seen on %ZONE%."
town_contract_none: "No contracts are available right now."
town_gold_label: "GP=%GOLD%K"
town_header: "Town of %NAME%"
town_intel_castle_under: "Castle %NAME% is under\n"
town_intel_count_named: "  %LABEL% %TROOP%\n"
town_intel_count_numeric: "  %COUNT% %TROOP%\n"
town_intel_monsters_generic: "  Various groups of monsters\n  occupy the castle."
town_intel_no_garrison: "  (no garrison)"
town_intel_owner_king: "the King"
town_intel_owner_none: "no one"
town_intel_owner_player: "your"
town_intel_owner_rule: "%OWNER%'s rule.\n\n"
town_intel_unavailable: "No intelligence is available here."
town_no_gold: "\n\n\nYou don't have enough gold!"
town_row_boat_cancel: "B) Cancel boat rental"
town_row_boat_rent: "B) Rent boat (%COST% week)"
town_row_contract: "A) Get New Contract"
town_row_info: "C) Gather information"
town_row_siege_buy: "E) Buy seige weapons (%SIEGE_COST%)"
town_row_siege_owned: "E) Siege weapons (owned)"
town_row_spell: "D) %SPELL% spell (%SPELL_COST%)"
town_row_spell_none: "D) (no spell available)"
town_siege_already: "You have siege weapons!"
town_siege_purchased: "Siege weapons purchased."
town_spell_at_cap: "You have learned your maximum number of spells."
town_spell_can_learn: "You can learn %LEFT% more spell%S%."
town_spell_unavailable: "No spell is available here."
```

---

## Appendix M — Classic layout constants (verbatim)

`src/classic/layout.h` defines the pixel rects used by every renderer.
Reproduced verbatim:

```c
#define CL_SCREEN_W   320
#define CL_SCREEN_H   200
#define CL_SCALE        2
#define CL_WINDOW_W  (CL_SCREEN_W * CL_SCALE)
#define CL_WINDOW_H  (CL_SCREEN_H * CL_SCALE)

#define CL_TILE_W 48
#define CL_TILE_H 34

// Chrome strips
#define CL_FRAME_TOP_H    8
#define CL_FRAME_BOTTOM_H 8
#define CL_FRAME_LEFT_W  16
#define CL_FRAME_RIGHT_W 16
#define CL_BAR_H          5
#define CL_BAR_Y         17

// Status strip
#define CL_STATUS_X       CL_FRAME_LEFT_W
#define CL_STATUS_Y       CL_FRAME_TOP_H
#define CL_STATUS_W       (CL_SCREEN_W - CL_FRAME_LEFT_W - CL_FRAME_RIGHT_W)
#define CL_STATUS_H       9

// Map viewport
#define CL_MAP_X          CL_FRAME_LEFT_W
#define CL_MAP_Y          (CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H)  // 22
#define CL_MAP_W          (CL_TILE_W * 5)                            // 240
#define CL_MAP_H          (CL_TILE_H * 5)                            // 170
#define CL_MAP_TILES_W    5
#define CL_MAP_TILES_H    5

// Sidebar column
#define CL_SIDEBAR_X      (CL_MAP_X + CL_MAP_W)                      // 256
#define CL_SIDEBAR_Y      CL_MAP_Y
#define CL_SIDEBAR_W      (CL_SCREEN_W - CL_SIDEBAR_X - CL_FRAME_RIGHT_W)  // 48
#define CL_SIDEBAR_H      CL_MAP_H

// Bottom chrome
#define CL_BOTTOM_Y       (CL_MAP_Y + CL_MAP_H)   // 192
#define CL_BOTTOM_H       CL_FRAME_BOTTOM_H        // 8

// Dialog / menu panel (KB_BottomBox)
#define CL_PANEL_X        CL_MAP_X
#define CL_PANEL_W        CL_MAP_W
#define CL_PANEL_H        80
#define CL_PANEL_Y        (CL_MAP_Y + CL_MAP_H - CL_PANEL_H)
```

### Layout diagram

```
┌────────────────────────────────────────┐ y=0
│ TopBox (status bar)        8px tall    │
├────────────────────────────────────────┤ y=8
│ Status strip (Days/Score)  9px tall    │
├────────────────────────────────────────┤ y=17
│ Decorative bar             5px tall    │
├──────────────────────────────┬─────────┤ y=22
│                              │         │
│  Map viewport               │ Sidebar │
│   240w × 170h                │  48w    │
│   (5 tiles × 5 tiles)        │  170h   │
│                              │         │
│   ─────────────────────────  │         │
│   │   CL_PANEL bottom 80px│  │         │
│   │   (menu / dialog)     │  │         │
│   └───────────────────────┘  │         │
├──────────────────────────────┴─────────┤ y=192
│ Bottom chrome              8px tall    │
└────────────────────────────────────────┘ y=200
                              x=0..16..256..304..320
```

The render target is 320×200 internal; `BeginTextureMode` /
`EndTextureMode` paints into a `RenderTexture2D` and the final
`DrawTexture` upscales to 640×400 (CL_SCALE=2).

---

## Appendix N — Input handler reference (`src/classic/input.c`)

Adventure-state keymap. `classic_input_poll` returns a
`ClassicInput` struct with `dx`, `dy` (delta for movement) and
`action` (CL_ACTION_*).

### N.1 Movement

```
KEY_UP    | KP_8        → (dx=0, dy=-1)
KEY_RIGHT | KP_6        → (dx=+1, dy=0)
KEY_DOWN  | KP_2        → (dx=0, dy=+1)
KEY_LEFT  | KP_4        → (dx=-1, dy=0)
KP_7 | Home             → (dx=-1, dy=-1)  diagonal NW
KP_9 | PageUp           → (dx=+1, dy=-1)  diagonal NE
KP_1 | End              → (dx=-1, dy=+1)  diagonal SW
KP_3 | PageDown         → (dx=+1, dy=+1)  diagonal SE
KP_5                    → CL_ACTION_REST  (rest in place)
```

Diagonal entries handle the two key labels (numpad and the
Home/End/PgUp/PgDn block). Holding a movement key produces a single
press (raylib `IsKeyPressed`).

### N.2 Action keys

```
A     → CL_ACTION_VIEW_ARMY        push VIEW_ARMY
V     → CL_ACTION_VIEW_CHARACTER   push VIEW_CHARACTER
I     → CL_ACTION_VIEW_CONTRACT    push VIEW_CONTRACT
P     → CL_ACTION_VIEW_PUZZLE      push VIEW_PUZZLE
M     → CL_ACTION_VIEW_MAP         push VIEW_WORLDMAP
U     → CL_ACTION_CAST_SPELL       push VIEW_SPELLS
C     → CL_ACTION_VIEW_CONTROLS    push VIEW_CONTROLS
O     → CL_ACTION_VIEW_OPTIONS     push VIEW_OPTIONS
N     → CL_ACTION_NAVIGATE         open navigate prompt
S     → CL_ACTION_SEARCH           open search-area prompt
W     → CL_ACTION_END_WEEK         GameSpendWeek
F     → CL_ACTION_FLY              mount = MOUNT_FLY (if class allows)
L     → CL_ACTION_LAND             mount = MOUNT_RIDE (from FLY)
D     → CL_ACTION_DISMISS_ARMY     pick slot 1..5
Q     → CL_ACTION_SAVE_QUIT        write save, exit to menu
ESC   → CL_ACTION_NONE (or dismiss view)
```

### N.3 Inline-handled keys (not in classic_input_poll)

These are handled in `main()` directly:

```
F10              debug cheat menu (one-letter follow-up)
Ctrl+Q           fast-quit prompt (status-bar overlay)
Alt+Enter        fullscreen toggle
Tab              HUD overlay toggle (openbounty extension)
```

### N.4 View-state input

When a view is on the stack (and dialog/prompt aren't active),
view-specific handlers fire instead of the default classic-input.
See §1.1 step 4 for the dispatch order.

---

## Appendix O — Combat call sites (current stub wiring)

Three entry points fire `RunCombatStub`:

### O.1 FLOW_SIEGE_MONSTER (`src/main.c:2055`)

```c
case FLOW_SIEGE_MONSTER: {
    if (r == PROMPT_RESULT_YES) {
        CastleRecord *cr = GameFindCastle(&game, pending_castle_id);
        const ResCastle *rc = resources_castle_by_id(&res, pending_castle_id);
        CombatTarget tgt = { 0 };
        tgt.name = rc && rc->name[0] ? rc->name : pending_castle_id;
        if (cr) {
            tgt.garrison = cr->garrison;
            tgt.garrison_slots = GAME_ARMY_SLOTS;
        }
        CombatResult outcome =
            RunCombatStub(&game, COMBAT_MODE_CASTLE, &tgt);
        if (outcome == COMBAT_RESULT_WIN && cr) {
            cr->owner_kind = CASTLE_OWNER_PLAYER;
            cr->visited = true;
            for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                cr->garrison[s].id[0] = '\0';
                cr->garrison[s].count = 0;
            }
        } else if (outcome == COMBAT_RESULT_LOSS) {
            perform_temp_death(&game, &map, &fog, &res);
        }
    }
    pending_castle_id[0] = '\0';
    break;
}
```

### O.2 FLOW_SIEGE_VILLAIN (`src/main.c:2071`)

```c
case FLOW_SIEGE_VILLAIN: {
    if (r == PROMPT_RESULT_YES) {
        CastleRecord *cr = GameFindCastle(&game, pending_castle_id);
        const ResCastle *rc = resources_castle_by_id(&res, pending_castle_id);
        const VillainDef *v = (cr && cr->villain_id[0])
                              ? villain_by_id(cr->villain_id) : NULL;
        CombatTarget tgt = { 0 };
        tgt.name = v && v->name[0] ? v->name
                 : (rc && rc->name[0] ? rc->name : pending_castle_id);
        if (cr) {
            tgt.garrison = cr->garrison;
            tgt.garrison_slots = GAME_ARMY_SLOTS;
        }
        CombatResult outcome =
            RunCombatStub(&game, COMBAT_MODE_CASTLE, &tgt);
        if (outcome == COMBAT_RESULT_WIN && cr) {
            char caught_vid[24];
            size_t k = 0;
            while (k + 1 < sizeof(caught_vid) && cr->villain_id[k]) {
                caught_vid[k] = cr->villain_id[k]; k++;
            }
            caught_vid[k] = '\0';
            cr->owner_kind = CASTLE_OWNER_PLAYER;
            cr->visited = true;
            cr->villain_id[0] = '\0';
            for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                cr->garrison[s].id[0] = '\0';
                cr->garrison[s].count = 0;
            }
            if (caught_vid[0]) GameFulfillContract(&game, caught_vid);
            GameMaybeRankUp(&game);
        } else if (outcome == COMBAT_RESULT_LOSS) {
            perform_temp_death(&game, &map, &fog, &res);
        }
    }
    pending_castle_id[0] = '\0';
    break;
}
```

### O.3 FLOW_ATTACK_FOE (`src/main.c:2122`)

```c
case FLOW_ATTACK_FOE: {
    if (r == PROMPT_RESULT_YES && pending_foe_id[0]) {
        FoeState *f = GameFindFoe(&game, pending_foe_id);
        CombatTarget tgt = { 0 };
        tgt.name = "Hostile band";
        if (f) {
            tgt.garrison = f->garrison;
            tgt.garrison_slots = GAME_ARMY_SLOTS;
        }
        CombatResult outcome =
            RunCombatStub(&game, COMBAT_MODE_FOE, &tgt);
        if (outcome == COMBAT_RESULT_WIN) {
            MapClearInteractive(&map, pending_foe_x, pending_foe_y);
            if (f) f->alive = false;
        } else if (outcome == COMBAT_RESULT_LOSS) {
            perform_temp_death(&game, &map, &fog, &res);
        }
    }
    pending_foe_id[0] = '\0';
    pending_foe_x = pending_foe_y = -1;
    break;
}
```

When the combat engine is implemented, `RunCombatStub` is replaced
by `RunCombat` returning the same `CombatResult` values; the
surrounding bookkeeping at each call site stays unchanged.

---

## Appendix P — Auto-screenshot system

`src/screenshot.c` provides a side-channel screenshot helper used
during testing. Files saved to `screenshots/shot_NNNN.png` with
auto-incrementing index per process. Triggered by:

- `screenshot_capture(rt)` — explicit call from main loop on
  certain events (currently disabled by default).
- raylib's built-in F12 → `TakeScreenshot()` (always available;
  raylib default).

Counter starts at 1 and persists during the process; `find` of
existing `screenshots/shot_*.png` files at startup determines the
first index used.

---

## Appendix Q — Boat coords map

For each town that has water adjacency, `boat_x/y` (where the
rented boat appears) and `gate_x/y` (the tile the player lands on
from Town Gate spell) are stored per town. Both default to `-1`
if not declared.

This is a per-town JSON object:

```json
{
  "id": "hunterville",
  "name": "Hunterville",
  "x": 12, "y": 60,
  "boat": { "x": 11, "y": 60 },
  "gate": { "x": 12, "y": 59 },
  "zone": "continentia",
  "intel_castle": "hyppus",
  "pinned_spell": "bridge"
}
```

OpenKB stored these as parallel global arrays (`boat_coords[26]`,
`towngate_coords[26]`); openbounty stores them per-town for
clarity.

---

## Appendix R — Save-path resolver (`src/savepath.c`)

```c
char *savepath_get_dir(void) {
    static char path[1024];

#if defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        snprintf(path, sizeof(path), "%s\\OpenBounty", appdata);
    } else {
        snprintf(path, sizeof(path), ".");
    }
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        snprintf(path, sizeof(path), "%s/openbounty", xdg);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(path, sizeof(path),
                     "%s/.local/share/openbounty", home);
        } else {
            snprintf(path, sizeof(path), ".");
        }
    }
#endif
    mkdir_p(path);
    return path;
}

void savepath_for_slot(int slot, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s/save_%d.dat",
             savepath_get_dir(), slot);
}
```

`mkdir_p` creates the directory tree if it doesn't exist.

---

## Appendix S — Engine deviations from OpenKB (consolidated)

This appendix lists every place where openbounty deliberately
deviates from OpenKB. Each entry includes rationale.

### S.1 Architectural (no behavior change)

| Topic | OpenKB | OpenBounty | Why |
| --- | --- | --- | --- |
| Window/render | SDL 2 | raylib 5 | simpler dependency tree |
| Audio backend | SDL_mixer | raylib audio | unified with render |
| Network | SDL_net | none | no multiplayer scope |
| Save format | 20,421-byte binary | JSON | human-readable, mod-friendly |
| Asset pack | DOS .CC + module dirs | single `assets/` dir | simpler |
| Module discovery | chain-of-responsibility loader | none | single asset pack |
| Resolution | 320×200, scaled | 320×200 internal × 2 = 640×400 | larger native |
| Color depth | 4 (CGA), 16 (EGA), 256 (VGA) | 256 (VGA) only | one palette |
| Tile data | 128-byte tile-id space | per-tile struct | richer |
| Sign text | global `STRL_SIGNS` indexed | per-tile fields | simpler |
| Resource lookup | `KB_Resolve(id, sub_id)` | typed C struct (`Resources`) | direct |
| RNG | libc `rand()` | Java-style 48-bit LCG | reproducible, cross-platform |

### S.2 Gameplay clarifications (sensible behavior)

| Topic | OpenKB | OpenBounty | Why |
| --- | --- | --- | --- |
| Player castle auto-repop | check slot 0 only | check all 5 slots | sensible |
| Boat seizure on negative gold | silent keep | seized | spec wording matches DOS |
| Time Stop spell floor | none | 10-step minimum | ensures usefulness |
| Pikemen recruit cost | 800 (OpenKB inherited) | 300 (DOS-original) | matches DOS binary |
| Recruit "n/a" gating | display-only | input-rejected | matches DOS binary |
| Boat mount/travel split | `mount = SAIL` | `Mount + TravelMode` | distinct concepts |
| `bury_scepter` y-coord bug | inherited | fixed | enables win condition |
| Boat over-water foe-walk | allowed | refused | sane behavior |

### S.3 Engine choices that mod-friendly

| Topic | OpenKB | OpenBounty | Why |
| --- | --- | --- | --- |
| Castle/town gate coords | global arrays | per-town/castle JSON | mod-friendly |
| Pinned town spells | hardcoded (Bridge → 0x15) | per-town JSON | mod-friendly |
| Salt budgets | hardcoded `(2,1,1,2,10,5)` | per-zone JSON | mod-friendly |
| Continent dwellings | global table | per-zone JSON | mod-friendly |
| Castle difficulty tiers | hardcoded `castle_difficulty[]` | per-castle JSON | mod-friendly |
| Sign content | `STRL_SIGNS` global | per-tile JSON | mod-friendly |
| Banner strings | hardcoded source | `strings.banners` JSON | mod-friendly |

### S.4 OpenKB-faithful (matching even where DOS differs)

| Topic | OpenKB | OpenBounty | DOS |
| --- | --- | --- | --- |
| Spell purchase comparison | `gold <= cost` | `gold <= cost` | likely `<` |
| Siege weapons gate on attack | not enforced | not enforced | enforced |
| Chest max_spells outcome | unreachable (curve collision) | unreachable | reachable |

---

## Appendix T — Future work / non-goals

### T.1 Combat engine

The biggest remaining piece. Plan:

1. Phase 1: data structures + setup (`KBcombat`, `KBunit`,
   `prepare_units_*`, `reset_match`, `reset_turn`, `next_unit`,
   `next_turn`).
2. Phase 2: `deal_damage` (full §17.5 OPENKB-SPEC).
3. Phase 3: `move_unit`, `fly_unit`, `hit_unit`,
   `unit_ranged_shot`, `compact_units`, distance helpers.
4. Phase 4: AI (`ai_unit_think`, `unit_closest_offset`,
   `unit_fly_offset`).
5. Phase 5: combat loop, `run_combat`, victory/defeat,
   animated grid render, status bar, log, keyboard target picker,
   auto-battle, give-up flow, combat keybindings. (OpenBounty is
   keyboard-only — no mouse / click-to-target path.)
6. Phase 6: 7 combat spells.

When each phase lands, the corresponding section in this spec
must be expanded to the same depth as OPENKB-SPEC §17/§18.

### T.2 Audio

raylib audio module is initialized but no sound effects load. The
OpenKB sound library is PC-speaker tunes (`TUNE_*`); to faithfully
reproduce, one would need to convert to WAV/OGG and trigger via
raylib `PlaySound`.

### T.3 Multiplayer

OpenKB's `combat.c` standalone multiplayer combat emulator is
explicitly out of scope.

### T.4 Module system

Multi-pack support is not planned. If desired, the `Resources`
struct would need a chain-of-responsibility lookup and asset path
resolver.

### T.5 Original DOS asset compatibility

OpenBounty ships its own asset pack and does not load original
DOS `.CC` files, KB.EXE strings, or PC-speaker tunes.

---

## End of spec


---

## Appendix U — Public function index

Every non-static function declared in headers under `src/`, with
its file:line. Generated from `grep` over `src/*.h` and
`src/classic/*.h`.

### U.1 `game.h` — game state mutators

```c
void GameInit(Game *g, const char *name, int pclass, int difficulty, const unsigned char *land);
void refill_rules(void);
void refill_names(void);
void player_accept_rank(Game *g);
void GameCastTimeStop(Game *g);
void GameCastFindVillain(Game *g);
void GameAddPlacement(Game *g, const char *zone, int x, int y, int kind, const char *id);
void salt_spells(Game *g);
void salt_continent(Game *g, int continent, int artifacts, int navmaps, int orbs, int telecaves, int dwellings, int friendly_foes);
void furnish_map(Game *g);
void clear_fog(Game *g);
void bury_scepter(Game *g, int continent);
void salt_villains(Game *g);
void repopulate_castle(Game *g, int castle_id);
bool GameIsOver(const Game *g);
int  GameSpendDays(Game *g, int days, int *total_commission);
int  GameSpendWeek(Game *g, int *total_commission);
void GameAcceptChestGold(Game *g, int gold);
void GameAcceptChestLeadership(Game *g, int leadership);
int GameBuyTroop(Game *g, const char *troop_id, int count);
int GameAddTroop(Game *g, const char *troop_id, int count);
int GameGarrisonTroop(Game *g, const char *castle_id, int slot);
int GameUngarrisonTroop(Game *g, const char *castle_id, int slot);
int GameMaxRecruitable(const Game *g, const char *troop_id);
void GameGrowDwellings(Game *g);
const char *GameApplyAstrology(Game *g, int troop_idx);
int GamePickAstrologyCreature(const Game *g, int week_id);
bool GameSwitchZone(Game *g, Map *map, Fog *fog, const char *zone_id);
bool GameFulfillContract(Game *g, const char *villain_id);
const char *GameNumberName(const Game *g, int count);
bool GamePlayerCanFly(const Game *g);
CastleRecord *GameFindCastle(Game *g, const char *castle_id);
const CastleRecord *GameFindCastleConst(const Game *g, const char *castle_id);
bool GameClaimArtifact(Game *g, int idx);
bool GameHasPower(const Game *g, ArtifactPower power);
void GameAddConsumed(Game *g, const char *zone, int x, int y);
void GameApplyTileMutations(const Game *g, Map *map, const char *zone);
int  GameKnownSpells(const Game *g);
int  GameBoatCost(const Game *g);
TownRecord *GameTouchTown(Game *g, const char *town_id);
const char *GameTakeNextContract(Game *g);
bool GameMaybeRankUp(Game *g);
int  GameArmyTotalLeadership(const Game *g);     // sum of troop->hp * count for all stacks
int  GameArmyStackCount(const Game *g);          // number of non-empty stacks
int  GameVillainsCaught(const Game *g);
int  GameArtifactsFound(const Game *g);
int  GameCastlesOwned(const Game *g);
int  GameComputeScore(const Game *g);
FoeState *GameFindFoe(Game *g, const char *placement_id);
const FoeState *GameFindFoeConst(const Game *g, const char *placement_id);
int GameFoesFollow(Game *g, Map *map);
```

### U.2 `map.h` — map management

```c
bool MapLoadZone(Map *map, const Resources *res, const char *zone_id);
const Tile *MapGetTile(const Map *map, int x, int y);
bool MapInBounds(const Map *map, int x, int y);
bool MapWalkable(const Map *map, int x, int y);
void MapClearInteractive(Map *map, int x, int y);
```

### U.3 `resources.h` — resource catalog

```c
bool resources_load(Resources *res, const char *manifest_path);
void resources_free(Resources *res);
const Resources *resources_current(void);
const ResTown   *resources_town_by_id(const Resources *r, const char *id);
const ResTown   *resources_town_by_index(const Resources *r, int index);
const ResCastle *resources_castle_by_id(const Resources *r, const char *id);
const ResZone   *resources_zone_by_id(const Resources *r, const char *id);
```

### U.4 `tables.h` — catalog lookup

```c
const TroopDef *troop_by_id(const char *id);
const TroopDef *troop_by_index(int idx);
int             troops_count(void);
const SpellDef *spell_by_id(const char *id);
const SpellDef *spell_by_index(int idx);
int             spells_count(void);
int             spell_index_by_id(const char *id);
const ClassDef *class_by_id(const char *id);
const ClassDef *class_by_index(int idx);
int             classes_count(void);
const VillainDef *villain_by_id(const char *id);
const VillainDef *villain_by_index(int idx);
int               villains_count(void);
const ArtifactDef *artifact_by_id(const char *id);
const ArtifactDef *artifact_by_index(int idx);
int                artifacts_count(void);
int artifact_index_for_tile(const char *zone, int local_idx);
char morale_result(char my_group, char their_group);
```

### U.5 `adventure.h` — adventure-screen interactions

```c
bool adventure_walkable_on_foot(const Tile *t);
bool adventure_walkable_in_boat(const Tile *t);
bool adventure_walkable_in_flight(const Tile *t);
InteractResult adventure_handle_interact(const Tile *t, const char *zone);
```

### U.6 `combat.h` — combat module (stub)

```c
CombatResult RunCombatStub(Game *g, CombatMode mode, const CombatTarget *target);
```

### U.7 `views.h` — view stack and view rendering

```c
ViewKind views_active(void);
void     views_set(ViewKind v);   // Replace stack with [v] (or empty if VIEW_NONE).
void     views_dismiss(void);     // Pop top; if stack empty, do nothing.
void     views_push(ViewKind v);
bool     views_wants_exit_hint(void);
bool     views_menu_update(const MenuCallbacks *cbs, void *userdata);
bool     views_town_update(Game *g);
const char *views_menu_title(void);
int  views_menu_entry_count(void);
const char *views_menu_entry_label(int i);
bool views_menu_entry_is_submenu(int i);
int  views_menu_cursor(void);
const char *views_town_display_name(void);
int  views_town_row_count(void);
const char *views_town_info_text(void);
int  views_town_cursor(void);
int  views_controls_cursor(void);
void views_controls_set_cursor(int r);
void views_controls_advance(struct Game *g, int row);
void views_spells_set_mode(bool cast_mode);
int  views_spells_chosen(void);
bool views_spells_update(void);
```

### U.8 `ui.h` — dialog system

```c
void open_dialog(const char *header, const char *body);
void open_dialog_flags(const char *header, const char *body, int flags);
bool dialog_is_active(void);
void dialog_dismiss(void);
const char *dialog_header_text(void);
const char *dialog_body_text(void);
int  dialog_page_current(void);
void dialog_page_next(void);
bool dialog_advance(void);  // Advance to next page if available; returns true if advanced
const char *toast_text_current(void);   // NULL if no active toast
void toast_show(const char *msg);
bool ui_any_key_pressed(void);
```

### U.9 `savegame.h` / `savepath.h`

```c
const char *SaveResultText(SaveResult r);
SaveResult SaveGameReadHeader(const char *path, SaveHeader *out);
bool SavePathGetSlot(int slot, char *out, size_t out_size);
bool SavePathGet(char *out, size_t out_size);
bool SavePathGetDir(char *out, size_t out_size);
```

### U.10 `bfont.h` — bitmap font

```c
bool    bfont_init(const char *png_path);
void    bfont_shutdown(void);
bool    bfont_ready(void);
void    bfont_draw(const char *text, int x, int y, Color c);
void    bfont_draw_centered(const char *text, int cx, int y, Color c);
Vector2 bfont_measure(const char *text);
int     bfont_line_height(void);
```

### U.11 `sprites.h` — sprite catalog

```c
void sprites_load(Sprites *s, const Resources *res);
void sprites_unload(Sprites *s);
```

### U.12 `fog.h` — fog of war

```c
void FogInit(Fog *fog);
void FogReveal(Fog *fog, const Map *map, int cx, int cy, int radius);
bool FogSeen(const Fog *fog, int x, int y);
```

### U.13 `tile.h` / `tile_cache.h`

```c
Terrain TerrainFromArt(const char *art);
bool ArtBlocksFoot(const char *art);
bool ArtIsBridge(const char *art);
Interact InteractFromString(const char *s);
const char *InteractToString(Interact i);
bool TerrainWalkable(Terrain t);
int  TerrainMoveCost(Terrain t);
const char *TerrainName(Terrain t);
void      tile_cache_shutdown(void);
Texture2D tile_cache_get(const char *art);
```

### U.14 `classic/*.h` — view layer

```c
void classic_chrome_draw(const Game *g, const Sprites *s);
ClassicInput classic_input_poll(void);
void classic_views_worldmap_toggle_hero_only(void);
void prompt_yes_no_open(const char *header, const char *body);
void prompt_numeric_open(const char *header, const char *body, int max_choice);
void prompt_ab_open(const char *header, const char *body);
int  prompt_text_input_value(void);
bool prompt_is_active(void);
void prompt_dismiss(void);
PromptResult prompt_update(void);
void prompt_draw(void);
void screen_own_castle_open(Game *g, const char *castle_id);
void screen_own_castle_draw(const Game *g, const Sprites *s);
bool screen_own_castle_is_garrison_mode(void);
void screen_own_castle_toggle_mode(void);
const char *screen_own_castle_castle_id(void);
void classic_hud_draw(const Game *g, const Sprites *s);
bool palette_init(const char *path);
void screen_recruit_soldiers_open(Game *g);
bool screen_recruit_soldiers_update(Game *g);
void screen_recruit_soldiers_draw(const Game *g, const Sprites *s);
void screen_dwelling_draw(const Game *g, const Sprites *s);
void screen_dwelling_refresh(int dwelling_pop, int gold, int cap);
void screen_alcove_open(Game *g);
void screen_alcove_draw(const Game *g, const Sprites *s);
void screen_end_game_open(bool won, const char *body);
void screen_end_game_draw(const Game *g, const Sprites *s);
void screen_home_castle_open(Game *g);
void screen_home_castle_draw(const Game *g, const Sprites *s);
```

---

## Appendix V — Prompt system (`src/classic/prompt.c`)

Modal prompts replace the bottom panel for one-shot input.

### V.1 Prompt API (`prompt.h` verbatim)

```c
#ifndef CLASSIC_PROMPT_H
#define CLASSIC_PROMPT_H

#include <stdbool.h>
#include "../game.h"

// Bottom-frame modal prompts used by classic-mode flows (ask_quit,
// ask_search, dismiss_army, navigate_continent, ...). One prompt active
// at a time. While a prompt is active, input and drawing are redirected
// to it; the overworld keeps rendering underneath.
//
// Flow:
//   prompt_yes_no_open(...) or prompt_numeric_open(...) → state active
//   main loop calls prompt_update() every frame until it returns non-NONE
//   caller switches on the returned result and clears state

typedef enum {
    PROMPT_RESULT_NONE = 0,
    PROMPT_RESULT_YES,
    PROMPT_RESULT_NO,
    PROMPT_RESULT_CANCEL,   // ESC
    PROMPT_RESULT_1,        // numeric picker: 1..5 → PROMPT_RESULT_1..5
    PROMPT_RESULT_2,
    PROMPT_RESULT_3,
    PROMPT_RESULT_4,
    PROMPT_RESULT_5,
} PromptResult;

// Open a yes/no prompt. `header` is bold/yellow; `body` is the question
// text (word-wrapped). The hint line is " <question> (y/n)?" appended
// automatically.
void prompt_yes_no_open(const char *header, const char *body);

// Open a 1..5 numeric picker with the given `header` and `body`.
// Useful for dismiss_army (pick a slot), instant_army targeting, etc.
void prompt_numeric_open(const char *header, const char *body, int max_choice);

// Open an A/B letter picker. Returns PROMPT_RESULT_1 for A, PROMPT_RESULT_2
// for B (so callers can share the numeric-picker dispatch path). Used by
// the OpenKB gold_or_leadership chest prompt (spec §20.2 / §24.24 /
// §31.4 two_choices), where the body labels its options A) and B).
void prompt_ab_open(const char *header, const char *body);

// Open a multi-digit numeric entry prompt (0-9, Backspace, Enter, Esc).
// Accepts numbers up to `max_digits`. Max accepted value is `max_value`;
// typing beyond it is rejected.
void prompt_text_input_open(const char *header, const char *body,
                            int max_digits, int max_value);

// Read the typed value after prompt_update() returned PROMPT_RESULT_YES
// from a text-input prompt. Returns 0 if no input was available.
int  prompt_text_input_value(void);

bool prompt_is_active(void);
void prompt_dismiss(void);

// Poll for input. Returns PROMPT_RESULT_NONE if still waiting; any other
// value means the prompt dispatched and state was cleared.
PromptResult prompt_update(void);

// Render the prompt (if active) in the bottom frame. Called from the
// classic overlay after dialogs.
void prompt_draw(void);

#endif
```

### V.2 Prompt state (selected internals from `prompt.c`)

Single-active-prompt model: a static `active_kind` enum and a
static body buffer. `prompt_update` polls input and returns a
`PromptResult` value (1..5, YES, NO, CANCEL, NONE) when the
user resolves the prompt; the caller then dispatches based on
the value.

Prompt kinds:

- **yes_no**: header + body + auto-appended " (y/n)?". Y → YES,
  N → NO, ESC → CANCEL.
- **numeric**: header + body + auto-appended " (1..N)?". 1..5 →
  RESULT_1..RESULT_5, ESC → CANCEL.
- **ab**: header + body. A → RESULT_1, B → RESULT_2, ESC →
  CANCEL. Used for chest gold/leadership choice.
- **text_input**: header + body + cursor row. 0-9 type,
  Backspace deletes, Enter commits (RESULT_YES with
  `prompt_text_input_value()` returning the typed integer),
  ESC cancels.

All four kinds clear on resolution; the caller can immediately
open another prompt.

---

## Appendix W — Chrome (status bar, sidebar, frame)

### W.1 Top status (`classic_chrome_draw_status`)

Renders the 9px-tall status strip at `CL_STATUS_*`. Content
depends on game state:

- **Adventure**: `Days Left:N` (left), zone name (center),
  `Score:N` (right). When `fast_quit_active`: replaces with
  "Quit to DOS without saving (y/n)".
- **Recruit / town / castle / dwelling / alcove / view-army /
  view-character / view-spells / view-puzzle / view-worldmap /
  view-controls / view-options**: `Press 'ESC' to exit`
  centered.
- **Combat (planned)**: `<Troop> Move|Shoot|...` per current
  unit.

### W.2 Sidebar (`classic_chrome_draw_sidebar`)

Right-side 48px column showing:

- Hero portrait (top, ~36×40).
- Gold: `GP`+5-digit count.
- Commission: `COMM`+amount.
- Zone shorthand.
- Artifacts grid: 2×4 cells, each shows the artifact icon if
  found, dim placeholder otherwise.
- Days-left fragment (small).

### W.3 Frame (`classic_chrome_draw_frame`)

Decorative borders: top 8px, left 16px, right 16px, bottom 8px.
Sourced from a single `chrome.png` asset.

---

## Appendix X — Fog of war

### X.1 `Fog` struct

```c
```

### X.2 `FogReveal` (`src/fog.c`)

```c
void FogReveal(Fog *fog, const Map *map, int cx, int cy, int radius) {
    // OpenKB `clear_fog` reveals a 5×5 square (−2..+2 on both axes)
    // regardless of the radius argument. We respect that for authenticity,
    // clamping to the map edges.
    (void)radius;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || y < 0 || x >= map->width || y >= map->height) continue;
            fog->seen[y][x] = true;
        }
    }
}
```

### X.3 `FogIsLit` / `FogIsExplored`

```c

```

Fog has two states: "lit" (currently visible, around hero) and
"explored" (previously seen). Lit tiles render at full color,
explored tiles render dimmed, unexplored tiles render as the
fog-color minimap entry.

---

## Appendix Y — Sprite catalog

`src/sprites.{c,h}`. Sprites struct holds raylib `Texture2D`
values keyed by id. Loaded once at startup via `sprites_load`,
freed via `sprites_free`.

### Y.1 Sprite categories

- **troop_sprite[id_idx]** — base troop sprite, from `troops/<id>.png`.
- **troop_anim[id_idx][frame]** — 4-frame idle, from `troops/<id>_NN.png`.
- **villain_portrait[v_idx]** — from `villains/<short>.png`.
- **artifact[a_idx]** — from `artifacts/<id>.png`.
- **hero_anim[frame]** — adventure hero, from `hero/hero_NN.png`.
- **boat_anim[frame]** — boat, from `boat/boat_NN.png`.
- **tile sprites** — keyed by tile.art string, loaded by
  `tile_cache_*` from referenced PNGs.
- **backdrop** — UI screen backgrounds (castle, plains, forest,
  hillcave, dungeon).
- **end-cartoon frames** — pre-game and end-game cinematics.

### Y.2 Sprite resolution

All sprites at native openbounty resolution (typically 48×34
for troops to fit one tile; 16×16 for artifact icons; etc.).        
Drawing uses `DrawTexturePro` for arbitrary-size positioning.

---

## Appendix Z — Test infrastructure

OpenBounty has no automated test suite. Verification is via:

- Manual play (build, run, observe behavior).
- Auto-screenshot to `screenshots/` for visual diffs.
- `make` cleanly building (compile-time correctness).
- F10 cheat menu for quick state setup.
- Save/load round-trip (save state → exit → reload → continue).

Adding a test harness is a possible future direction; not
currently scoped.

---

