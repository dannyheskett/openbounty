# OpenBounty Gameplay &amp; Game Design Specification

**Status:** living document. Snapshot of the implementation as of 2026-05-04.

This specification has described every gameplay rule, data table, screen, and state transition that has constituted the OpenBounty game as built. It has been written in the present perfect tense ("the game has done X"), as a factual reproduction-grade record. A complete reimplementation of OpenBounty has been possible from this document alone, paired with the asset pack at `assets/kings-bounty/` (sprites, palette, maps, audio).

Each requirement has carried a stable identifier of the form `REQ-NNN`. Where a numeric value or table has appeared in the asset pack, the requirement has named the JSON path (e.g. `game.json:economy.chest.chance_gold`) rather than copying the value into prose, so that the data and the spec have remained synchronised. The full data tables have been reproduced where their content has had no canonical home outside the JSON.

**Scope:** the entire game, including adventure mode, combat, UI, audio, save format, and tooling.

**Conventions:**
- "The game" has referred to the running OpenBounty binary.
- "The player" has referred to the human user controlling the hero.
- "The hero" has referred to the in-world avatar (a character of one of four classes).
- File:line citations have followed the form `src/file.c:NNN`. Paths have been rooted at the project root.
- Coordinates have been written `(x, y)` with `x` increasing east and `y` increasing south; origin `(0, 0)` has sat at the top-left of every zone map.

**Out of scope of the requirements (but documented for context):**
- Pixel-exact UI layout has not been mandated; the spec has named the panels, regions, and metrics, but small visual deltas have been considered conformant.
- The prompt-to-prompt input timing (debounce, key-repeat) has been left to the implementation.

---

## Table of Contents

1. [Game model and lifecycle](#1-game-model-and-lifecycle)
2. [Resources and the asset pack](#2-resources-and-the-asset-pack)
3. [RNG and determinism](#3-rng-and-determinism)
4. [Time, days, and weeks](#4-time-days-and-weeks)
5. [Character, classes, and ranks](#5-character-classes-and-ranks)
6. [World, zones, and tiles](#6-world-zones-and-tiles)
7. [Salt: per-zone object placement](#7-salt-per-zone-object-placement)
8. [Movement, mounts, and travel](#8-movement-mounts-and-travel)
9. [Adventure-mode actions](#9-adventure-mode-actions)
10. [Troops and the army](#10-troops-and-the-army)
11. [Morale and out-of-control](#11-morale-and-out-of-control)
12. [Foes, encounters, and recruiting](#12-foes-encounters-and-recruiting)
13. [Towns](#13-towns)
14. [Castles](#14-castles)
15. [Dwellings](#15-dwellings)
16. [Spells: catalog and adventure casts](#16-spells-catalog-and-adventure-casts)
17. [Artifacts](#17-artifacts)
18. [Villains and contracts](#18-villains-and-contracts)
19. [Chests and rewards](#19-chests-and-rewards)
20. [Economy: gold, commission, upkeep](#20-economy-gold-commission-upkeep)
21. [Astrology and weekly events](#21-astrology-and-weekly-events)
22. [Combat](#22-combat)
23. [Scoring, victory, and defeat](#23-scoring-victory-and-defeat)
24. [Save format and slots](#24-save-format-and-slots)
25. [UI: views, HUD, dialogs, prompts](#25-ui-views-hud-dialogs-prompts)
26. [Input and controls](#26-input-and-controls)
27. [Cheats and debug](#27-cheats-and-debug)
28. [Audio](#28-audio)
29. [Rendering](#29-rendering)
30. [CLI, packs, and platform](#30-cli-packs-and-platform)
31. [Recorder, encoder, and harness](#31-recorder-encoder-and-harness)

---

## 1. Game model and lifecycle

### 1.1 Top-level structure

- **REQ-001.** The game has held a single root `Game` struct in memory (`src/game.h:217-252`); no persistent global state outside that struct, the loaded `Resources` (read-only), and platform handles (window, audio device, render target) has existed.
- **REQ-002.** The `Game` struct has owned: `version`, `seed (uint64_t)`, `Character`, `Stats`, `Position`, `TravelMode`, `anim_frame`, `hud_visible`, `army[5]`, `Spellbook`, `Contract`, `Artifacts`, `WorldProgress`, `BoatState`, `TownRecord towns[26]`, `CastleRecord castles[26]`, `ScepterLocation scepter`, `TileMutation consumed[64]`, `DwellingState dwellings[64]`, `SaltedPlacement placements[128]`, `FoeState foes[64]`, plus counters for the variable-length arrays.
- **REQ-003.** The `Game` has held a `const Resources *res` pointer to the loaded asset pack; the engine has never mutated `*res` after startup.

### 1.2 Lifecycle states

- **REQ-010.** The process has progressed through these macro-states in order: **STARTUP** → **CHARACTER CREATION** (or **LOAD**) → **ADVENTURE** ↔ **COMBAT** → **END GAME** → **EXIT**.
- **REQ-011.** **STARTUP** has shown publisher splash (≈2.5 s or any key), title splash (≈2.5 s or any key), then optional credits screen (≈2.5 s or any key, skipped when the pack has supplied no credits).
- **REQ-012.** After the splashes, the **CLASS SELECT** screen has presented four class options (`A`/`B`/`C`/`D` for Knight/Paladin/Sorceress/Barbarian) plus `L` for Load and `Esc` to quit.
- **REQ-013.** Pressing `L` has opened the **SAVE PICKER** (10 slots, 0..9, plus a "New" row); selecting an existing slot has loaded the slot, selecting an empty slot or "New" has fallen through to character creation.
- **REQ-014.** **CHARACTER CREATION** has consisted of: name entry (max 10 chars, first letter capitalised), difficulty selection (Easy/Normal/Hard/Impossible) shown in a table together with starting days and score multiplier, and an intro banner ("`<Name>` the `<Class>`, new game being created…").
- **REQ-015.** The seed for a new game has been computed as `time(NULL) XOR hash(name) XOR class_index`, except when `--seed N` has been supplied on the command line, in which case `N` has been used verbatim.
- **REQ-016.** **ADVENTURE** has been the main loop. The game has remained in adventure until: the player has triggered combat (via stepping onto a hostile foe, monster castle, or villain castle), or the game has ended (won via search-on-scepter, or lost via `days_left == 0`).
- **REQ-017.** **COMBAT** has run a separate state machine (see §22) on a tactical grid; on completion, control has returned to adventure with the post-combat results applied.
- **REQ-018.** **END GAME** has shown either a win cartoon (§23.7) or a lose dialog (§23.8) with the final score; pressing any key has returned to the title and ultimately to **EXIT**.

### 1.3 Main loop

- **REQ-020.** Each frame has performed, in order: harness tick (§31.5), audio tick (§28.4), `Alt+Enter` fullscreen toggle, `F10` cheat-menu toggle (§27), input dispatch, animation update, render to a 320×200 offscreen target, blit to the window with integer scaling.
- **REQ-021.** The window has been initialised at 640×400 with `FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT`, minimum size 320×200, target FPS 60, and `KEY_NULL` set as the raylib exit key (so that `Escape` has not closed the window).
- **REQ-022.** Input dispatch has followed a strict overlay hierarchy (highest priority first): fast-quit prompt → active prompt → active view → active dialog → adventure-mode actions → movement.
- **REQ-023.** The HUD visibility has been remembered when an overlay (view, dialog, or prompt) has opened: `hud_visible` has been forced to `false`, and on overlay dismissal `hud_visible` has been restored to the value the player has set.
- **REQ-024.** Animation frames (`Game.anim_frame`) have advanced at intervals of `0.05 + options[0] * 0.05` seconds (so 0.05 s..0.30 s); when the animation toggle (`options[3]`) has been off, `anim_frame` has remained at 0.

---

## 2. Resources and the asset pack

### 2.1 Asset pack layout

- **REQ-030.** A pack has been a directory containing `game.json` at its root. All other files (sprites under `art/`, maps under `maps/`, audio under `audio/`, palette under `palettes/`) have been resolved relative to the pack root.
- **REQ-031.** The default pack id has been `kings-bounty`; the bundled pack has lived at `assets/kings-bounty/`.
- **REQ-032.** The pack format has been documented separately at `docs/PACK-FORMAT.md`; a pack has had no required structure beyond `game.json` and the files it has named.
- **REQ-033.** Each pack has carried a `pack_id` (string) and an FNV1a-64 hash of its `game.json`; the hash has been computed once at pack open.

### 2.2 Pack discovery

- **REQ-040.** At startup, when no `--pack` flag has been given, the engine has searched in this order: the current working directory, then the user packs directory (`<save-dir>/packs`).
- **REQ-041.** When zero packs have been found, the engine has looked for `assets/kings-bounty/` (developer build) and for a DOS `KB.EXE` in the cwd; finding `KB.EXE` has triggered a one-time first-run extraction into `<save-dir>/packs/kings-bounty/`.
- **REQ-042.** When more than one pack has been discovered, the **PACK PICKER** UI has run before character creation.
- **REQ-043.** The pack stack has supported push/pop/clear, but only one pack at a time has been active for the running game.

### 2.3 Resource parsing

- **REQ-050.** `Resources` has been loaded once from `game.json` at startup (`src/resources.c`); the structure has been read-only thereafter.
- **REQ-051.** All array and string sizes within `Resources` have been bounded at compile time by named constants (`RES_MAX_TOWNS=32`, `RES_MAX_CASTLES=32`, `RES_MAX_ZONES=8`, `RES_TILE_CODE_COUNT=128`, `RES_ID_LEN=32`, `RES_NAME_LEN=48`, `RES_PATH_LEN=128`, `RES_BANNER_LEN=320`, etc., per `src/resources.h`).
- **REQ-052.** Where a numeric value has been required by spec but absent from `game.json`, the engine has substituted a documented default (logged on first read); where a string has been required but absent, the engine has substituted an empty string and rendered an empty UI region.

---

## 3. RNG and determinism

### 3.1 PRNG

- **REQ-060.** A single deterministic Java-style LCG has driven all randomness (`src/game.c:22-25`): `state = (state * 25214903917 + 11) & 0xFFFFFFFFFFFFFFFF`; `result = state >> 32`.
- **REQ-061.** The PRNG has been seeded from `Game.seed` XORed with `0x5DEECE66D` at game start.
- **REQ-062.** The function `game_rng_next(min, max)` has returned a value uniformly in the inclusive range `[min, max]`.
- **REQ-063.** `chest_rand(game, x, y, salt)` has produced a deterministic per-tile value by hashing `(seed, x, y, salt)`, so that re-rolling the same chest has yielded the same outcome until the chest has been consumed.

### 3.2 Determinism guarantees

- **REQ-070.** Given the same seed and the same sequence of player inputs, the entire game has been reproduced bit-for-bit (subject to floating-point only being used for animation and rendering, not gameplay).
- **REQ-071.** All randomised game-start placements (zones, villains, scepter, dwellings, foes, telecaves, navmaps, orbs, artifacts, town spells) have been derived from `seed` so that loading a save has restored identical placements.
- **REQ-072.** Real-time animation, audio, and rendering have not affected gameplay state.

---

## 4. Time, days, and weeks

### 4.1 Time units

- **REQ-080.** A **day** has consisted of `time.day_steps = 40` step opportunities (`game.json:economy/time.day_steps`).
- **REQ-081.** A **week** has consisted of `time.week_days = 5` days (`game.json:time.week_days`).
- **REQ-082.** The starting `days_left` has been read from `time.days_per_difficulty[]`: Easy=900, Normal=600, Hard=400, Impossible=200.

### 4.2 Step processing

- **REQ-090.** Each completed move on the overworld has called `GameOnStep` (`src/game.c`), which has:
  1. If `time_stop > 0`, decremented `time_stop` and returned without touching day state.
  2. Else if the destination terrain has been desert, set `steps_left_today = 0` immediately.
  3. Else decremented `steps_left_today` by 1.
  4. If `steps_left_today <= 0` after the above, fired `end_day`.
- **REQ-091.** `end_day` has decremented `days_left` by 1, reset `steps_left_today` to `time.day_steps`, and set `game_over = true` when `days_left` has reached 0.
- **REQ-092.** A week boundary has been detected when `days_left % week_days == 0`; on a week boundary, `end_day` has performed week-end processing (§21).

### 4.3 Time spend helpers

- **REQ-100.** `GameSpendDays(game, n, &paid)` has spent `n` days, firing one `end_day` per day, accumulating `*paid` with the commission paid (0 if no week boundary crossed).
- **REQ-101.** `GameSpendWeek(game, &paid)` has spent enough days to cross exactly one week boundary; it has been called by **End Week** and by zone-switch sailing.
- **REQ-102.** `Search` (key `S`) has cost 10 days regardless of the result, except when the search has revealed the buried scepter (in which case the game has ended immediately as a win).
- **REQ-103.** `time_stop` has been reset to 0 at every `end_day` call, except that the spell `time_stop` has applied its bonus before `end_day` has been processed and so has carried over until consumed.

---

## 5. Character, classes, and ranks

### 5.1 Classes

- **REQ-110.** The game has provided exactly four classes: Knight (index 0), Paladin (index 1), Sorceress (index 2), Barbarian (index 3); these have been declared in `game.json:classes[]`.
- **REQ-111.** Each class has had a `starting_gold` and a `starting_troops` array (troop id + count). Initial values have been: Knight 7,500g (20 militia, 2 archers); Paladin 10,000g (20 peasants, 20 militia); Sorceress 10,000g (30 peasants, 10 sprites); Barbarian 7,500g (20 wolves).
- **REQ-112.** Each class has had four ranks (index 0..3) with these per-rank fields: `id`, `name`, `villains_needed`, `leadership`, `max_spells`, `spell_power`, `commission`, `knows_magic`, `instant_army` (troop catalog index for Instant Army summoning).

### 5.2 Per-rank tables

The four classes have followed these per-rank tables (delta values; cumulative stats accumulate by summing rank 0..n).

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

### 5.3 Stats and leadership

- **REQ-120.** `leadership_base` has been the sum of `leadership` deltas through the current rank; `leadership_current` has been the live value, modified by alcove, Raise Control spell, chest leadership, and combat losses.
- **REQ-121.** `commission_weekly` has been the cumulative sum of `commission` deltas through the current rank, optionally augmented by the `INCREASE_COMMISSION` artifact (§17.4).
- **REQ-122.** `max_spells` and `spell_power` have similarly accumulated; the `DOUBLE_MAX_SPELLS` and `DOUBLE_SPELL_POWER` artifacts have applied multiplicative doublings on pickup (§17.4).
- **REQ-123.** `knows_magic` has been initialised from the rank-0 `knows_magic` flag of the chosen class; among the four classes only the Sorceress (rank 0) has set it true at character creation. Any class has been able to learn magic by visiting the Archmage Aurange's alcove (§15.5).

### 5.4 Rank-up

- **REQ-130.** After each villain capture, the game has checked the next rank's `villains_needed`; while `villains_caught >= ranks[rank+1].villains_needed`, the rank has been advanced and stats recomputed (`src/game.c:1221-1253`).
- **REQ-131.** On rank-up: `leadership_base`, `commission_weekly`, `max_spells`, `spell_power` have been recomputed cumulatively; `leadership_current` has been reset to the new `leadership_base`.
- **REQ-132.** Rank advancement has not opened a popup of its own; the King's audience screen (§14.7) has reflected the new rank on the next visit.

### 5.5 Difficulty

- **REQ-140.** Difficulty has been an enum: `DIFFICULTY_EASY` (0), `DIFFICULTY_NORMAL` (1), `DIFFICULTY_HARD` (2), `DIFFICULTY_IMPOSSIBLE` (3).
- **REQ-141.** Difficulty has affected `days_left` (per §4.1) and the final score multiplier (per §23.2).
- **REQ-142.** Difficulty has not affected starting gold, starting army, garrison strengths, foe spawn rates, or chest contents.

### 5.6 Mount

- **REQ-150.** The character mount has been one of `MOUNT_RIDE` (default, walking), `MOUNT_SAIL` (boat), `MOUNT_FLY` (flying).
- **REQ-151.** `Mount` has been distinct from `TravelMode` (`TRAVEL_WALK` / `TRAVEL_BOAT`); the travel mode has been set automatically when the hero has stepped onto a boat tile or off it.
- **REQ-152.** Flying has been triggered by key `F` and required `GamePlayerCanFly` (every non-empty stack with `TROOP_ABIL_FLY` and `skill_level >= 2`); landing has been triggered by key `L` and required a destination tile that has been grass, non-interactive, and not blocking on foot.

---

## 6. World, zones, and tiles

### 6.1 Zones

- **REQ-160.** The world has consisted of exactly four zones: `continentia`, `forestria`, `archipelia`, `saharia` (`game.json:zones[]`).
- **REQ-161.** Each zone has been 64×64 tiles. The dimensions have been declared per zone but have matched on every zone shipped in `kings-bounty`.
- **REQ-162.** Each zone has declared: `id`, display `name`, `map_path`, `hero_spawn_x/y`, optional `home_spawn_x/y` and `is_home` flag, optional `magic_alcove_x/y`, `neighbors[]` (zone ids reachable by sailing), `salt` config (§7), and per-feature lists (towns, castles, signs, chests, dwellings, armies).
- **REQ-163.** Exactly one zone has had `is_home: true`. Continentia has been the home zone in `kings-bounty`.

### 6.2 Coordinates

- **REQ-170.** The coordinate origin has been the top-left of each zone; `x` has increased east, `y` has increased south.
- **REQ-171.** All gameplay coordinates have been integer tile indices. There has been no sub-tile position.

### 6.3 Terrain enum

- **REQ-180.** Terrain has been one of: `TERRAIN_GRASS`, `TERRAIN_FOREST`, `TERRAIN_MOUNTAIN`, `TERRAIN_WATER`, `TERRAIN_DESERT`.
- **REQ-181.** Walkability on foot (`TerrainWalkable`): grass and desert have been walkable; forest, mountain, and water have not been walkable.
- **REQ-182.** A grass tile flagged `is_bridge` has been walkable on foot and traversable in a boat.
- **REQ-183.** Stepping onto a desert tile has set `steps_left_today = 0` immediately, ending the current day on the next step opportunity.

### 6.4 Interactive overlay enum

- **REQ-190.** The interactive overlay has been one of: `INTERACT_NONE`, `INTERACT_CASTLE_GATE`, `INTERACT_TOWN`, `INTERACT_TREASURE_CHEST`, `INTERACT_SIGN`, `INTERACT_ARTIFACT`, `INTERACT_DWELLING_PLAINS`, `INTERACT_DWELLING_FOREST`, `INTERACT_DWELLING_HILLS`, `INTERACT_DWELLING_DUNGEON`, `INTERACT_ALCOVE`, `INTERACT_ORB`, `INTERACT_TELECAVE`, `INTERACT_NAVMAP`, `INTERACT_FOE`.
- **REQ-191.** A tile has carried at most one interactive overlay; `INTERACT_NONE` has indicated none.
- **REQ-192.** Castle wall tiles have been non-interactive but have set `blocks_foot = true`.

### 6.5 Tile data

- **REQ-200.** Each tile has stored: `art` (sprite filename root), `terrain` (derived from art at load time), `interactive`, optional `id` (named instance, e.g. "kings_castle"), `blocks_foot`, `is_bridge`, optional `sign_title` and `sign_body`, and `boat_spawn_x/y` (for town tiles).

### 6.6 Map file format

- **REQ-210.** Maps have been plain text files with one ASCII byte per tile, organised row-major. Lines beginning with `#` and blank lines have been ignored.
- **REQ-211.** Each byte has been looked up in `Resources.tile_codes[256]` to produce an art name plus terrain/blocking flags. Short rows have been padded with grass.
- **REQ-212.** The tile-code table has supplied 128 distinct codes shipping in `kings-bounty` (e.g. `.` → grass, `~` → water, `F` → forest, `^` → mountain). Codes 0..127 have been valid; 128..255 have been reserved.

### 6.7 Castle and town tile placement

- **REQ-220.** A castle has been stamped as a 3×2 footprint centred on its `gate_x/gate_y`: the gate tile (interactive `CASTLE_GATE`, walkable) has sat at `(x, y)`; the surrounding five tiles `(x±1, y-1)` and `(x±1, y)` have been wall pieces (non-interactive, `blocks_foot = true`).
- **REQ-221.** Each castle has optionally declared additional decorative wall pieces (relative offsets); the King's castle has carried 24 such pieces.
- **REQ-222.** A town has been a single tile with `interactive = INTERACT_TOWN`; the town's `boat_spawn_x/y` has been used when a boat has been rented.
- **REQ-223.** When a castle's `gate` object has been absent in JSON, the gate landing tile has been computed as `(x, y+1)` (one tile south of the castle centre).

---

## 7. Salt: per-zone object placement

### 7.1 Salt budget

- **REQ-230.** Each zone has declared a `salt` block: `artifacts` (count of artifact chests to place), `navmaps`, `orbs`, `telecaves`, `dwellings`, `friendly_foes`, plus `preferred_troops[]` and `dwelling_range[lo, hi]` for dwelling troop selection.
- **REQ-231.** The default `kings-bounty` salt budget per zone has been: artifacts=2, navmaps=1, orbs=1, telecaves=2, dwellings=10, friendly_foes=5.

### 7.2 Algorithm

- **REQ-240.** `salt_continent` (`src/game.c:521-678`) has run once per zone at game init. It has:
  1. Registered every static foe army declared on the zone as a hostile `FoeState`.
  2. Created a barrel of `chest_count` slots (one per `treasure_chest_*` placeholder on the zone), each tagged `SALT_NONE`.
  3. For each kind in order — artifacts, navmaps, orbs, telecaves, dwellings, friendly foes — repeatedly picked a random unclaimed barrel slot via `game_rng_next` and tagged it with the kind, until the kind's quota has been met or a guard counter (`barrel_len * 20`) has been exhausted.
  4. Walked the barrel and emitted a `SaltedPlacement` for each tagged slot.
- **REQ-241.** When the guard has been exhausted before the quota has been met, the missing items have been silently skipped (no error).

### 7.3 Slot semantics

- **REQ-250.** `SALT_ARTIFACT`: looked up the artifact catalog for the entry whose `zone == zone_id` and `local_idx == artifact_counter`, placed it as `INTERACT_ARTIFACT` with `id = artifact_id`.
- **REQ-251.** `SALT_NAVMAP`: placed `INTERACT_NAVMAP` with id `navmap_<n>`.
- **REQ-252.** `SALT_ORB`: placed `INTERACT_ORB` with id `orb_<n>`.
- **REQ-253.** `SALT_TELECAVE`: placed `INTERACT_TELECAVE` with id `telecave_<n>`.
- **REQ-254.** `SALT_DWELLING`: picked a troop using `salt_pick_dwelling_troop` (consult zone `preferred_troops[]` first, otherwise `dwelling_range`), derived the dwelling kind from the troop's `dwelling` field (singular `hill` mapped to plural `hills`), placed `INTERACT_DWELLING_*`, and registered a pinned `DwellingState` row.
- **REQ-255.** `SALT_FRIENDLY`: created a `FoeState` with `friendly = true` and id `salt_foe_friendly_<n>`; a placeholder garrison has been pre-rolled at salt time but the friendly recruit flow has rolled fresh on accept (§12.5).

### 7.4 Salt of villains

- **REQ-260.** `salt_villains` has run after `salt_continent`. For each villain in catalog order:
  - The villain's declared `zone` has determined the candidate castles.
  - A retry loop (guarded by `min(ncastles * 20, max_attempts)`) has picked a random castle in that zone that has not been excluded from contracts and has not already been owned by a villain.
  - The chosen castle has had `owner_kind = CASTLE_OWNER_VILLAIN`, `villain_id = <id>`, and its garrison populated from the villain's pre-built army (5 stacks).

### 7.5 Salt of spells

- **REQ-270.** `salt_spells` (`src/game.c:362-417`) has assigned exactly one spell to each town's `spell_for_sale`:
  1. **Pinned phase**: each town with a non-empty `pinned_spell` has received that spell, and the spell has been marked claimed.
  2. **Random phase**: each unclaimed spell has been placed in a random town that has not already had a spell, with retries until placement.
  3. **Fallback phase**: any town still empty has received a random spell from the (already-fully-claimed) pool.

### 7.6 Scepter burial

- **REQ-280.** `bury_scepter` (`src/game.c`) has chosen one of the four zones uniformly at random, loaded its map, counted all tiles whose terrain is `TERRAIN_GRASS`, interactive is `INTERACT_NONE`, and `blocks_foot` is false; picked the Nth such tile (N rolled uniformly in `[0, count-1]`); and stored the tile's true zone id, x, and y in `Game.scepter`.
- **REQ-281.** The scepter has not been visible on the map. Searching (key `S`) on the buried tile has triggered the win flow (§23).

---

## 8. Movement, mounts, and travel

### 8.1 Eight-directional movement

- **REQ-290.** Movement has supported four cardinal directions (arrow keys, numpad 2/4/6/8) and four diagonals (numpad 7/9/1/3 or Home/PgUp/End/PgDn). Diagonals have been single-step `(dx, dy)` deltas of `(±1, ±1)`.
- **REQ-291.** A blocked move has played the `AUDIO_TUNE_BUMP` sound and emitted a `step:blocked` recorder tag (containing direction, terrain, and zone-edge flag). The hero has not changed position, facing, or travel mode.
- **REQ-292.** A successful move has updated `position.x/y`, `position.facing_left = (dx < 0)`, fired `FogReveal` at the new position with radius `world.fog_sight`, and called `GameOnStep`.

### 8.2 Walking, sailing, flying

- **REQ-300.** While `travel_mode == TRAVEL_WALK` and `mount == MOUNT_RIDE`, walkability has followed §6.3.
- **REQ-301.** While `travel_mode == TRAVEL_BOAT`, the hero (in the boat) has been able to enter water tiles, bridge tiles, and grass/desert tiles (the latter triggering disembark).
- **REQ-302.** While `mount == MOUNT_FLY`, every terrain has been walkable; interactive tiles have not fired during flight (`adventure.c`).
- **REQ-303.** Stepping from walk mode onto the parked boat has set `travel_mode = TRAVEL_BOAT`. Stepping in boat mode from water/bridge onto land has parked the boat at the previous water tile (`boat.x/y = prev_x/prev_y`) and set `travel_mode = TRAVEL_WALK`.
- **REQ-304.** Boat-mode movement on land has not been allowed except as a single disembark step.

### 8.3 Boat rental

- **REQ-310.** A boat has been rented at any town menu (`B`), at cost `GameBoatCost(g)` returning `economy.boat_cost_normal = 500` (or `economy.boat_cost_cheap = 100` when the Anchor of Admirability has been held). The boat has been parked at the town's `boat_x/boat_y`.
- **REQ-311.** A boat rental has been cancelled at the same town menu when the boat has been on land. While the hero has been sailing, cancellation has been refused with the `town_boat_vacate_first` banner.
- **REQ-312.** A rented boat has consumed one weekly cost at every week boundary; on bankruptcy (insufficient gold to pay) the boat has been repossessed (`has_boat = false`, position cleared).

### 8.4 Sail navigation

- **REQ-320.** While `travel_mode == TRAVEL_BOAT`, key `N` has opened a 1..5 prompt listing up to five `neighbors` of the current zone. Selecting a digit has called `GameSwitchZone(target_zone)` and `GameSpendWeek` (one week passes during the journey).
- **REQ-321.** Outside boat mode, key `N` has produced a "must be sailing" dialog and consumed no time.

### 8.5 Bounce-back

- **REQ-330.** When the hero has stepped onto a tile that has opened an interactive flow with `bounce_back = true` (towns, castles, dwellings, alcove, hostile foes), and the player has dismissed the flow without committing, the hero's position, travel mode, and boat coordinates have been reverted to the pre-step values.
- **REQ-331.** When the hero has stepped onto an interactive that has not bounced (chest, navmap, orb, telecave, signpost, friendly foe accept, artifact), the hero has remained on the destination tile after the flow has resolved.

---

## 9. Adventure-mode actions

### 9.1 Action key bindings

- **REQ-340.** The following adventure-mode keys have triggered actions on press (one action per press; held keys have not autorepeated within a turn):
  - `A` — view army
  - `C` — view controls
  - `D` — dismiss army (numeric prompt)
  - `F` — fly (when allowed)
  - `I` — view contract
  - `L` — land (when in flight on grass)
  - `M` — view map (worldmap)
  - `N` — navigate (sail to neighbour zone)
  - `O` — options menu
  - `P` — view puzzle
  - `Q` — save and prompt-to-quit
  - `Ctrl+Q` — fast quit (status-bar y/n)
  - `S` — search (10-day prompt)
  - `U` — cast spell (or "no magic" message + alcove hint when `knows_magic == false`)
  - `V` — view character
  - `W` — end week
  - `Numpad 5` — rest one day in place
  - `F10` — open cheat menu (§27)
  - `Esc` — close active overlay (view, dialog, prompt)
  - `Tab` — open game menu (`VIEW_MENU`)

### 9.2 Gamepad mapping

- **REQ-350.** The gamepad has been mapped: D-pad / left stick to 8-direction movement; A → search; X → cast spell; Y → end week; LB → view army; RB → view character; LT → fly; RT → land; Start → world map; Back → options; B → cancel.

### 9.3 Search

- **REQ-360.** Pressing `S` has opened a yes/no prompt with body "It will take 10 days to do a search of this area. Search (y/n)?" (template `tuning.search_cost_days = 10`).
- **REQ-361.** A `Yes` answer has compared the hero's current zone, x, y to `Game.scepter`. On match, the win flow has fired (§23); otherwise, `GameSpendDays(10)` has run and the "revealed nothing" banner has shown.
- **REQ-362.** A `No` answer has cancelled with no time spent.

### 9.4 Dismiss army

- **REQ-370.** Pressing `D` has opened a numeric (1..5) prompt listing the slots that have held a non-empty stack.
- **REQ-371.** Choosing any slot but the last has zeroed that stack and run `GameCompactArmy`.
- **REQ-372.** Choosing the last remaining stack has chained into a yes/no `body_dismiss_last` prompt; a `Yes` has run `perform_temp_death` (clear army, zero siege weapons, grant 20 peasants, teleport home, dismount, drop boat).

### 9.5 End week

- **REQ-380.** Pressing `W` has called `GameSpendWeek` and scheduled the post-week dialog sequence (§21.5); when the hero has run out of days the lose flow has fired.

---

## 10. Troops and the army

### 10.1 Catalog

- **REQ-390.** The troop catalog has held exactly 25 entries, indexed 0..24, declared in `game.json:troops[]`. The full catalog has appeared in the table below; all numeric values have come from `game.json`.

| # | id | Name | SL | HP | MV | Recruit | Spoils | Abilities | Dwelling | MaxPop | Growth/Wk | Morale |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | peasants | Peasants | 1 | 1 | 1 | 10 | 1 | — | plains | 250 | 6 | A |
| 1 | sprites | Sprites | 1 | 1 | 1 | 15 | 1 | FLY | forest | 200 | 6 | C |
| 2 | militia | Militia | 2 | 2 | 2 | 50 | 5 | — | castle | 0 | 5 | A |
| 3 | wolves | Wolves | 2 | 3 | 3 | 40 | 4 | — | plains | 150 | 5 | D |
| 4 | skeletons | Skeletons | 2 | 3 | 2 | 40 | 4 | UNDEAD | dungeon | 150 | 5 | E |
| 5 | zombies | Zombies | 2 | 5 | 1 | 50 | 5 | UNDEAD | dungeon | 100 | 5 | E |
| 6 | gnomes | Gnomes | 2 | 5 | 1 | 60 | 6 | — | forest | 250 | 5 | C |
| 7 | orcs | Orcs | 2 | 5 | 2 | 75 | 7 | — | hill | 200 | 5 | D |
| 8 | archers | Archers | 2 | 10 | 2 | 250 | 25 | — | castle | 0 | 5 | B |
| 9 | elves | Elves | 3 | 10 | 3 | 200 | 20 | — | forest | 100 | 4 | C |
| 10 | pikemen | Pikemen | 3 | 10 | 2 | 300 | 30 | — | castle | 0 | 4 | B |
| 11 | nomads | Nomads | 3 | 15 | 2 | 300 | 30 | — | plains | 150 | 4 | C |
| 12 | dwarves | Dwarves | 3 | 20 | 1 | 350 | 30 | — | hill | 100 | 4 | C |
| 13 | ghosts | Ghosts | 4 | 10 | 3 | 400 | 40 | ABSORB, UNDEAD | dungeon | 25 | 3 | E |
| 14 | knights | Knights | 5 | 35 | 1 | 1000 | 100 | — | castle | 250 | 3 | B |
| 15 | ogres | Ogres | 4 | 40 | 1 | 750 | 75 | — | hill | 200 | 3 | D |
| 16 | barbarians | Barbarians | 4 | 40 | 3 | 750 | 75 | — | plains | 100 | 3 | C |
| 17 | trolls | Trolls | 4 | 50 | 1 | 1000 | 100 | REGEN | forest | 25 | 3 | D |
| 18 | cavalry | Cavalry | 4 | 20 | 4 | 800 | 80 | — | castle | 0 | 2 | B |
| 19 | druids | Druids | 5 | 25 | 2 | 700 | 70 | MAGIC | forest | 25 | 2 | C |
| 20 | archmages | Archmages | 5 | 25 | 1 | 1200 | 120 | FLY, MAGIC | plains | 25 | 2 | C |
| 21 | vampires | Vampires | 5 | 30 | 1 | 1500 | 150 | FLY, LEECH, UNDEAD | dungeon | 50 | 2 | E |
| 22 | giants | Giants | 5 | 60 | 3 | 2000 | 200 | — | hill | 50 | 2 | C |
| 23 | demons | Demons | 6 | 50 | 1 | 3000 | 300 | FLY, SCYTHE | dungeon | 25 | 1 | E |
| 24 | dragons | Dragons | 6 | 200 | 1 | 5000 | 500 | FLY, IMMUNE | hill | 25 | 1 | D |

- **REQ-391.** Each troop has additionally carried a `tier_counts[4]` row giving foe-garrison stack sizes per continent tier; the values have matched `game.json:troops[].tier_counts` and have been used by `roll_creature` (§12.4).

### 10.2 Ability flags

- **REQ-400.** The ability mask has been an 8-bit field with these bits (`src/tables.h`):
  - `TROOP_ABIL_FLY` (0x01) — has flown over impassable terrain; required for hero flight (§5.6).
  - `TROOP_ABIL_REGEN` (0x02) — has regenerated HP between combat rounds.
  - `TROOP_ABIL_MAGIC` (0x04) — has cast combat spells.
  - `TROOP_ABIL_IMMUNE` (0x08) — has been immune to magic damage.
  - `TROOP_ABIL_ABSORB` (0x10) — has been converted to peasants on a Peasants astrology week.
  - `TROOP_ABIL_LEECH` (0x20) — has healed from melee damage dealt.
  - `TROOP_ABIL_SCYTHE` (0x40) — has applied special doubled-damage rule in combat.
  - `TROOP_ABIL_UNDEAD` (0x80) — has been eligible for `Turn Undead` and immune to morale effects.

### 10.3 Army stacks

- **REQ-410.** The player army has had exactly five stack slots (`GAME_ARMY_SLOTS = 5`); each stack has held `id[32]` and `count`. An empty stack has been encoded as `id[0] == '\0'` and/or `count == 0`.
- **REQ-411.** When a troop has been added that has matched an existing stack's id, the count has been incremented; otherwise, the first empty slot has been used.
- **REQ-412.** When the player has had no empty slot and no matching stack at the time of an add, the add has failed; the source flow (recruit, friendly foe, instant army) has shown an appropriate banner ("flee in terror", "no room", etc.).
- **REQ-413.** `GameCompactArmy` has moved non-empty stacks to the front, preserving order.

### 10.4 Recruitment cap

- **REQ-420.** `GameMaxRecruitable(troop_id)` has computed the maximum count the player has been able to recruit of a given troop:
  - `same_troop_consumed = sum(stack.count * troop.hp)` over stacks holding the same troop.
  - `free_leadership = leadership_current - same_troop_consumed`.
  - When `free_leadership < 0`, the result has been 0 ("n/a" in UI).
  - Otherwise, the result has been `free_leadership / troop.hp`.
- **REQ-421.** Buying troops has cost `recruit_cost * count` gold; the gold check has used strict `<` (insufficient when `gold < cost`).
- **REQ-422.** Out-of-control stacks (§11.3) have not been excluded from recruitment cap calculations; only the same-troop-id consumption has reduced free leadership for a given troop.

### 10.5 Upkeep

- **REQ-430.** At every week boundary, each non-empty stack has paid `upkeep = count * (recruit_cost / 10)` gold (integer division). The total has been deducted after commission has been credited.

---

## 11. Morale and out-of-control

### 11.1 Morale groups

- **REQ-440.** Each troop has belonged to one of five groups A..E. The full mapping has appeared in §10.1 column "Morale".

### 11.2 Morale chart

- **REQ-450.** The 5×5 morale chart (`combat.morale_chart` in `game.json`) has been:

```
         A   B   C   D   E
    A    N   N   N   N   N
    B    N   N   N   N   N
    C    N   N   H   N   N
    D    L   N   L   H   N
    E    L   L   L   N   N
```

Row = the stack whose morale is being computed; column = another stack present in the army.

- **REQ-451.** Per-stack morale (`army_slot_morale`) has been computed by:
  1. With a single non-empty stack: morale `High`.
  2. Otherwise, for each other non-empty stack, the chart has been consulted; results have been counted.
  3. Any `L` result → morale `Low`. All `H` (and at least one) → morale `High`. Otherwise → morale `Normal`.

### 11.3 Out-of-control

- **REQ-460.** A stack has been out of control (OOC) when `(troop.hp * stack.count) > leadership_current`.
- **REQ-461.** OOC has been mutually exclusive with the morale labels in the UI: when OOC, the army view has shown "Out of Control" in red; otherwise, "Low"/"Normal"/"High" in white.
- **REQ-462.** In combat, an OOC unit has been unable to move, attack, or retaliate; it has counted as dead weight (§22).

---

## 12. Foes, encounters, and recruiting

### 12.1 Foe state

- **REQ-470.** A `FoeState` has held: `zone`, `x`, `y`, `placement_id`, `alive`, `friendly`, and a 5-stack `garrison`.
- **REQ-471.** Up to 64 foes have been tracked per game (`GAME_MAX_FOES = 64`); both the static zone armies and the salt-placed friendly foes have shared the array.

### 12.2 Foe sources

- **REQ-480.** Static hostile foes have been declared per zone (`zones[].armies[]`); their garrisons have been pre-rolled by `roll_hostile_garrison` at `salt_continent` time.
- **REQ-481.** Friendly foes have been placed by `salt_continent` (§7.3) with placeholder garrisons; the troop accepted on join has been re-rolled fresh on yes (§12.5).

### 12.3 Spawn pool

- **REQ-490.** `tier_chance_curve[4][4]` (`game.json:spawn.tier_chance_curve`) has been:

```
Tier 0 (Continentia): [60, 90, 98, 101]
Tier 1 (Forestria):   [20, 70, 95, 99]
Tier 2 (Archipelia):  [10, 20, 50, 90]
Tier 3 (Saharia):     [3,  6,  10, 40]
```

- **REQ-491.** `tier_troop_pool[4][5]` (`game.json:spawn.tier_troop_pool`) has been:

```
Kind 0 (plains):  peasants, wolves, nomads, barbarians, archmages
Kind 1 (forest):  sprites, gnomes, elves, trolls, druids
Kind 2 (hill):    orcs, dwarves, ogres, giants, dragons
Kind 3 (dungeon): skeletons, zombies, ghosts, vampires, demons
```

### 12.4 Roll

- **REQ-500.** `roll_creature(continent_tier)` has produced `(troop_id, count)`:
  1. `kind = game_rng_next(0, 3)` (independent of tier).
  2. `chance = game_rng_next(1, 100)`.
  3. Walked `chance_curve[tier]` to find the smallest `slot ∈ [0..3]` where `chance <= curve[slot]`; if no slot has matched, slot has been 4 (fallthrough).
  4. `troop_id = troop_pool[kind][slot]`.
  5. `count = troop.tier_counts[tier] + game_rng_next(0, troop.tier_counts[tier] / 2)`, clamped to a minimum of 2.
- **REQ-501.** `roll_hostile_garrison(continent_tier)` has filled 1..3 stacks (count rolled `game_rng_next(0, 2) + 1`) of the foe's garrison via `roll_creature`.

### 12.5 Encounter flows

- **REQ-510.** When the hero has stepped onto a hostile-foe tile, the `encounter_hostile_*` banner has displayed the foe garrison; a yes/no fight prompt has followed. `Yes` has entered combat. `No` has bounced the hero back.
- **REQ-511.** When the hero has stepped onto a friendly-foe tile, a fresh troop roll has produced the offered troop and count; the `encounter_join_named`/`_numeric` banner has named them; a yes/no accept prompt has followed.
  - `Yes` and a free army slot has run `GameAddTroop` with no gold cost; the foe has been consumed.
  - `Yes` and no slot has shown `encounter_wanderers` ("flee in terror at the sight of your vast army"); the foe has been consumed.
  - `No` has consumed the foe with `encounter_wanderers`.

### 12.6 Hero-on-foe

- **REQ-520.** Hostile foes have proactively walked toward the hero each turn (`GameFoesFollow`): a foe within Chebyshev distance 2 of the hero's last position has stepped one tile toward the hero. On collision, the encounter flow has fired.
- **REQ-521.** Foe motion has not consumed the hero's day budget; foe motion has stopped at impassable terrain.

---

## 13. Towns

### 13.1 Catalog

- **REQ-530.** The town catalog has held exactly 26 towns (`game.json:towns[]`), one per letter A..Z by display name. The full table has appeared below (id, name, zone, x, y, gate, boat, intel castle, pinned spell):

| # | id | Name | Zone | x | y | Gate | Boat | Intel Castle | Pinned |
|---|---|---|---|---|---|---|---|---|---|
| 0 | riverton | Riverton | continentia | 29 | 51 | (29,52) | (30,52) | azram | — |
| 1 | underfoot | Underfoot | forestria | 58 | 59 | (57,59) | (59,58) | basefit | — |
| 2 | paths_end | Path's End | continentia | 38 | 13 | (38,14) | (39,13) | cancomar | — |
| 3 | anomaly | Anomaly | forestria | 34 | 40 | (35,40) | (34,41) | duvock | — |
| 4 | topshore | Topshore | archipelia | 5 | 13 | (5,14) | (4,14) | endryx | — |
| 5 | lakeview | Lakeview | continentia | 17 | 19 | (16,19) | (18,19) | faxis | — |
| 6 | simpleton | Simpleton | archipelia | 13 | 3 | (12,3) | (14,3) | goobare | — |
| 7 | centrapf | Centrapf | archipelia | 9 | 24 | (9,25) | (10,25) | hyppus | — |
| 8 | quiln_point | Quiln Point | continentia | 14 | 36 | (13,36) | (14,37) | irok | — |
| 9 | midland | Midland | forestria | 58 | 30 | (57,30) | (58,31) | jhan | — |
| 10 | xoctan | Xoctan | continentia | 51 | 35 | (51,34) | (51,36) | kookamunga | — |
| 11 | overthere | Overthere | archipelia | 57 | 6 | (57,7) | (58,7) | lorsche | — |
| 12 | elans_landing | Elan's Landing | forestria | 3 | 26 | (3,27) | (2,27) | mooseweigh | — |
| 13 | kings_haven | King's Haven | continentia | 17 | 42 | (16,42) | (18,42) | nilslag | — |
| 14 | bayside | Bayside | continentia | 41 | 5 | (40,5) | (41,6) | ophiraund | — |
| 15 | nyre | Nyre | continentia | 50 | 50 | (50,49) | (49,51) | portalis | — |
| 16 | dark_corner | Dark Corner | forestria | 58 | 3 | (58,4) | (59,3) | quinderwitch | — |
| 17 | isla_vista | Isla Vista | continentia | 57 | 58 | (56,58) | (56,59) | rythacon | — |
| 18 | grimwold | Grimwold | saharia | 9 | 3 | (9,4) | (10,3) | spockana | — |
| 19 | japper | Japper | archipelia | 13 | 56 | (13,55) | (12,56) | tylitch | — |
| 20 | vengeance | Vengeance | saharia | 7 | 60 | (6,60) | (8,60) | uzare | — |
| 21 | hunterville | Hunterville | continentia | 12 | 60 | (12,59) | (11,60) | vutar | **bridge** |
| 22 | fjord | Fjord | continentia | 46 | 28 | (47,28) | (47,27) | wankelforte | — |
| 23 | yakonia | Yakonia | archipelia | 49 | 55 | (50,55) | (50,54) | xelox | — |
| 24 | woods_end | Woods End | forestria | 3 | 55 | (3,54) | (2,54) | yeneverre | — |
| 25 | zaezoizu | Zaezoizu | saharia | 58 | 15 | (58,16) | (59,15) | zyzzarzaz | — |

- **REQ-531.** Each town has tracked `visited` (set true on first entry), `spell_for_sale` (set by §7.5), and a denormalised copy of resource fields for save/replay.

### 13.2 Town menu

- **REQ-540.** The town view has shown a header `Town of <NAME>` and `GP=<gold>K` (gold ÷ 1000), and five rows:
  - **A) Get New Contract** — called `GameTakeNextContract`. The new villain id, reward, and last-known zone have been displayed; if no contracts have remained, `town_contract_none` has shown.
  - **B) Rent boat (<cost> week) / Cancel boat rental** — toggled `boat.has_boat`. The `town_boat_vacate_first` banner has fired when the player has been mid-sail. Cost has been `GameBoatCost(g)`. The gold check has used strict `<` (must have strictly more than cost; refused at exactly cost).
  - **C) Gather information** — fetched intel on the town's `intel_castle`: castle name, owner (player/monsters/villain/king), and garrison list with bucketed labels via `NumberName`. When the castle has set `excluded_from_intel`, the row has shown unavailable.
  - **D) <Spell> spell (<cost>)** — bought `spell_for_sale` if `GameKnownSpells(g) < max_spells` and `gold > cost` (strict inequality, OpenKB-faithful). Gold has been deducted, `spells.counts[spell]++`, `town_spell_can_learn` displayed.
  - **E) Buy siege weapons (<cost>) / Siege weapons (owned)** — set `siege_weapons = 1` and deducted `economy.siege_cost = 3000`. The flag has not been enforced anywhere in combat (§22.13).
- **REQ-541.** Every town menu action has been at-most-once per visit; the dialog has remained until `Esc` has dismissed it.

---

## 14. Castles

### 14.1 Catalog

- **REQ-550.** The castle catalog has held exactly 27 castles: 26 villain/monster castles (one per letter A..Z) plus King Maximus's castle.

| # | id | Name | Zone | x | y | Tier | King |
|---|---|---|---|---|---|---|---|
| 0 | azram | Azram | continentia | 30 | 36 | 0 | — |
| 1 | basefit | Basefit | forestria | 47 | 57 | 1 | — |
| 2 | cancomar | Cancomar | continentia | 36 | 14 | 0 | — |
| 3 | duvock | Duvock | forestria | 30 | 45 | 1 | — |
| 4 | endryx | Endryx | archipelia | 11 | 17 | 2 | — |
| 5 | faxis | Faxis | continentia | 22 | 14 | 0 | — |
| 6 | goobare | Goobare | archipelia | 41 | 27 | 2 | — |
| 7 | hyppus | Hyppus | archipelia | 43 | 36 | 2 | — |
| 8 | irok | Irok | continentia | 11 | 33 | 0 | — |
| 9 | jhan | Jhan | forestria | 41 | 29 | 1 | — |
| 10 | kookamunga | Kookamunga | continentia | 57 | 5 | 0 | — |
| 11 | lorsche | Lorsche | archipelia | 52 | 6 | 2 | — |
| 12 | mooseweigh | Mooseweigh | forestria | 25 | 24 | 1 | — |
| 13 | nilslag | Nilslag | continentia | 22 | 39 | 0 | — |
| 14 | ophiraund | Ophiraund | continentia | 6 | 6 | 0 | — |
| 15 | portalis | Portalis | continentia | 58 | 40 | 0 | — |
| 16 | quinderwitch | Quinderwitch | forestria | 42 | 7 | 1 | — |
| 17 | rythacon | Rythacon | continentia | 54 | 57 | 0 | — |
| 18 | spockana | Spockana | saharia | 17 | 24 | 3 | — |
| 19 | tylitch | Tylitch | archipelia | 9 | 45 | 2 | — |
| 20 | uzare | Uzare | saharia | 41 | 51 | 3 | — |
| 21 | vutar | Vutar | continentia | 40 | 58 | 0 | — |
| 22 | wankelforte | Wankelforte | continentia | 40 | 22 | 0 | — |
| 23 | xelox | Xelox | archipelia | 45 | 57 | 2 | — |
| 24 | yeneverre | Yeneverre | forestria | 19 | 44 | 1 | — |
| 25 | zyzzarzaz | Zyzzarzaz | saharia | 46 | 20 | 3 | — |
| 26 | king_maximus | of King Maximus | continentia | 11 | 56 | 0 | **YES** |

- **REQ-551.** Each `CastleRecord` has tracked: `visited`, `known`, `owner_kind` (`CASTLE_OWNER_PLAYER` / `CASTLE_OWNER_MONSTERS` / `CASTLE_OWNER_VILLAIN` / `CASTLE_OWNER_SPECIAL`), `villain_id` when applicable, and a 5-stack `garrison`.

### 14.2 Owner kinds

- **REQ-560.** **CASTLE_OWNER_VILLAIN** — populated by `salt_villains`; garrison has been the villain's pre-built army.
- **REQ-561.** **CASTLE_OWNER_MONSTERS** — populated by `repopulate_castle` using the castle's `difficulty_tier`; `roll_creature(tier)` has filled all five stacks.
- **REQ-562.** **CASTLE_OWNER_PLAYER** — set after the player has captured the castle in combat; player has been able to garrison and ungarrison troops via the **Own Castle** view.
- **REQ-563.** **CASTLE_OWNER_SPECIAL** — used only by the King's castle; never siegeable, never repopulated.

### 14.3 Difficulty tiers

- **REQ-570.** Each castle has carried a `difficulty_tier` 0..3 used by `repopulate_castle`. The shipped values have been: Saharia (Spockana, Uzare, Zyzzarzaz) = 3; mid-game (Endryx, Goobare, Hyppus, Lorsche, Mooseweigh) = 2; lower mid-game (Basefit, Duvock, Jhan, Quinderwitch, Tylitch, Xelox, Yeneverre) = 1; Continentia and the King's castle = 0.

### 14.4 Repopulation

- **REQ-580.** At game init, every castle with `owner_kind == CASTLE_OWNER_MONSTERS` has been seeded by `repopulate_castle` (5 calls to `roll_creature(tier)`).
- **REQ-581.** At every week boundary, each player-owned castle whose garrison has been completely empty (all five stacks empty) has been auto-repopulated by the same call.
- **REQ-582.** A partially-populated player castle has not been overwritten; the player's stored troops have been preserved.
- **REQ-583.** Astrology growth (§21.3) has applied weekly to non-player castle stacks whose troop id has matched the astrology creature.

### 14.5 Visit flow

- **REQ-590.** Stepping onto a castle gate tile owned by the player has opened the **Own Castle** view (§14.6). Owned by monsters → `FLOW_SIEGE_MONSTER`. Owned by villain → `FLOW_SIEGE_VILLAIN`. The King's castle (special) → `FLOW_AUDIENCE`.
- **REQ-591.** A siege flow has shown a yes/no prompt with the garrison; `Yes` has entered combat, `No` has bounced the hero back.
- **REQ-592.** Combat win on a siege has set `owner_kind = CASTLE_OWNER_PLAYER`. Combat win on a villain castle has additionally fulfilled the contract (§18.3).

### 14.6 Own castle (garrison swap)

- **REQ-600.** The Own Castle view has shown the castle's garrison (5 slots) and supported letter selectors A..E plus Space to toggle direction (army → garrison or garrison → army). Each transfer has moved a stack into the first matching slot (merge) or first empty slot (new).
- **REQ-601.** The view has not allowed the player army to become entirely empty; at least one stack has remained.

### 14.7 King's audience

- **REQ-610.** The King's castle has been entered at any time via `audience` flow. The audience has shown:
  - Intro: "Trumpets announce…" (the `audience.intro` text).
  - When `villains_caught` has equalled the next rank's `villains_needed`, the rank-up message: "%NAME%, I now promote you to %RANK%." The rank advancement has occurred at the moment the next villain has been caught (§5.4); the audience has merely reported it.
  - When more villains have remained: "My dear %NAME%, I can aid you better after you've captured %NEEDED% more villains."
  - At final rank: "%NAME% the %RANK%, Hurry and recover my Scepter of Order or all will be lost!"
- **REQ-611.** The audience has doubled as the **Home Castle** view, which has also offered (`A`) **Recruit Soldiers** for castle-class troops (militia, archers, pikemen, knights, cavalry).

---

## 15. Dwellings

### 15.1 Kinds

- **REQ-620.** Dwelling kinds have been: `plains`, `forest`, `hills` (singular `hill` in troop catalog), `dungeon`. Each `TroopDef` has named exactly one kind via the `dwelling` field.
- **REQ-621.** Castle-kind troops (militia, archers, pikemen, knights, cavalry) have had `max_population = 0` and have been recruitable only at the home castle's recruit-soldiers screen.

### 15.2 Dwelling state

- **REQ-630.** Each `DwellingState` has tracked `(zone, x, y)`, `troop_id`, `max_population`, `count`. The array has been bounded at 64 (`GAME_MAX_DWELLINGS`).
- **REQ-631.** A dwelling has been created lazily on first visit if not already placed by `salt_continent` (§7.3). The troop has been picked deterministically by `(seed, x, y)`. Initial `count` has been the troop's `max_population`.
- **REQ-632.** A salt-placed dwelling has been pinned to its troop via `enforce_dwelling_pinned`, ignoring the lazy-pick path.

### 15.3 Visit and recruit

- **REQ-640.** Visiting a dwelling tile has bounced the hero (§8.5) and opened the dwelling screen showing: dwelling kind name, count available, cost per unit, gold available, and `recruit_cost * count <= gold && count <= GameMaxRecruitable(troop_id)` clamp.
- **REQ-641.** Yes plus a numeric input has called `GameBuyTroop` (deduct gold, add to army, decrement dwelling count). No has cancelled with no effect.

### 15.4 Astrology growth

- **REQ-650.** At every week boundary, only dwellings whose `troop_id` has matched the astrology creature have refilled to `max_population` (§21.3); non-matching dwellings have retained their current count.

### 15.5 Alcove (Aurange)

- **REQ-660.** Each zone has declared a single `magic_alcove_x/y`. The alcove has been an interactive tile (`INTERACT_ALCOVE`) until consumed.
- **REQ-661.** When the player's class has set `knows_magic = true` at game start (Sorceress), every zone's alcove has been marked consumed at game init.
- **REQ-662.** Visiting an unconsumed alcove has bounced the hero and shown an offer prompt: "Visit Archmage Aurange's chamber? cost = `economy.alcove_cost = 5000`g".
  - `Yes` plus sufficient gold (strict `<`) has set `knows_magic = true`, deducted `5000g`, marked the tile consumed, and (per implementation) added a permanent leadership bonus.
  - `No` has cancelled.

---

## 16. Spells: catalog and adventure casts

### 16.1 Catalog

- **REQ-670.** The spell catalog has held exactly 14 spells, indexed 0..13:

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

### 16.2 Spell counts and storage

- **REQ-680.** `Game.spells.counts[14]` has held one count per spell (parallel to the catalog).
- **REQ-681.** `GameKnownSpells(g)` has been the sum of all 14 counts. `max_spells` has capped purchases.
- **REQ-682.** Casting has decremented the count; buying has incremented it.

### 16.3 Buying

- **REQ-690.** Spells have been bought at the town menu (§13.2 row D). The price has been the spell's `cost`.
- **REQ-691.** Buying has not required `knows_magic` (OpenKB-faithful); casting has required it.
- **REQ-692.** The gold check has used strict `<` (insufficient when `gold <= cost` per the OpenKB-faithful preserved behaviour).

### 16.4 Adventure spell effects

- **REQ-700.** **Bridge** (cost 100, count consumed only on success): on cast, has set the bridge state to await a direction key. After the next direction press, the engine has walked up to 5 water tiles in `(dx, dy)`; up to 2 consecutive water tiles have been converted to `is_bridge` grass with the appropriate art (`bridge_h` for horizontal, `bridge_v` for vertical). The count has been decremented inside `try_build_bridge` only on success.
- **REQ-701.** **Time Stop** (cost 200, immediate): has added `max(spell_power * 10, 10)` to `time_stop`. Steps in the time-stop window have not advanced the day.
- **REQ-702.** **Find Villain** (cost 1000): has scanned all castles for one whose `owner_kind == CASTLE_OWNER_VILLAIN` and `villain_id == contract.active_id`; the matching castle has had `known = true` set, revealing it on the worldmap. With no active contract, the spell has fired no effect and has not decremented.
- **REQ-703.** **Castle Gate** (cost 1000): has set `gate_state = GATE_STATE_SELECT, gate_mode = 0` and waited for a letter A..Z. Mapping has matched on the destination's first letter (lowercased). Selection of a never-visited castle has shown a no-op message; selection of a visited castle has resolved the destination's `zone`, `gate_x/y`, called `GameSwitchZone`, set `position` to the gate coords, and decremented the count.
- **REQ-704.** **Town Gate** (cost 500): identical to Castle Gate but `gate_mode = 1` and the catalog has been towns. Default landing has been `(town.boat_x, town.boat_y)` when no `gate` has been declared.
- **REQ-705.** **Instant Army** (cost 1000): has resolved the troop via `class.ranks[rank].instant_army`; the count has been `(spell_power + 1) * instant_army_multiplier[rank]` with multiplier `[3, 2, 1, 1]` for ranks 0..3 (minimum 1). The troops have been added via `GameAddTroop` (free, no leadership refusal). No empty slot has shown the no-room banner and not consumed the spell.
- **REQ-706.** **Raise Control** (cost 500): has added `spell_power * 100` to `leadership_current` and decremented the count.

### 16.5 Casting gate

- **REQ-710.** Pressing `U` in adventure mode with `knows_magic == true` has opened `VIEW_SPELLS`; with `knows_magic == false`, it has shown a "no magic" dialog hinting at the alcove location.
- **REQ-711.** The spells view has presented two columns: combat spells (0..6) on the left, adventure spells (7..13) on the right; pressing the row letter A..G has cast the corresponding adventure spell when `count > 0` (combat spells have not been castable from adventure mode).

---

## 17. Artifacts

### 17.1 Catalog

- **REQ-720.** The artifact catalog has held exactly 8 artifacts:

| # | id | Name | Zone | local_idx | Power |
|---|---|---|---|---|---|
| 0 | sword | The Sword of Prowess | saharia | 1 | INCREASED_DAMAGE |
| 1 | shield | The Shield of Protection | forestria | 0 | QUARTER_PROTECTION |
| 2 | crown | The Crown of Command | archipelia | 0 | DOUBLE_LEADERSHIP |
| 3 | articles | The Articles of Nobility | continentia | 1 | INCREASE_COMMISSION |
| 4 | amulet | The Amulet of Augmentation | saharia | 0 | DOUBLE_SPELL_POWER |
| 5 | ring | The Ring of Heroism | continentia | 0 | DOUBLE_MAX_SPELLS |
| 6 | book | The Book of Necros | archipelia | 1 | UNKNOWN |
| 7 | anchor | The Anchor of Admirability | forestria | 1 | CHEAPER_BOATS |

### 17.2 Placement

- **REQ-730.** Each zone has held two artifacts (`local_idx` 0 and 1). `salt_continent` (§7.3) has placed artifact tiles by matching `(zone, local_idx)` against the catalog.
- **REQ-731.** `artifact_local_from_id(tile_id)` has resolved the artifact id to its catalog index, falling back to a trailing-digit parse on legacy `artifact_<n>` ids.

### 17.3 Pickup

- **REQ-740.** Stepping onto an artifact tile has called `GameClaimArtifact(idx)`: set `artifacts.found[idx] = true`, applied the instant power (per §17.4), marked the tile consumed, and shown the artifact's banner.

### 17.4 Powers

- **REQ-750.** Pickup-time effects:
  - `DOUBLE_LEADERSHIP` (Crown): `leadership_base *= 2; leadership_current = leadership_base`.
  - `INCREASE_COMMISSION` (Articles): `commission_weekly += 2000`.
  - `DOUBLE_SPELL_POWER` (Amulet): `spell_power *= 2`.
  - `DOUBLE_MAX_SPELLS` (Ring): `max_spells *= 2`.
- **REQ-751.** Live-checked effects (queried at use time):
  - `INCREASED_DAMAGE` (Sword): combat damage +50%.
  - `QUARTER_PROTECTION` (Shield): combat damage taken ×0.75.
  - `CHEAPER_BOATS` (Anchor): boat rental drops to `economy.boat_cost_cheap = 100` from `economy.boat_cost_normal = 500`.
  - `UNKNOWN` (Book): no effect (faithful to the unimplemented original).
- **REQ-752.** `GameHasPower(power)` has returned `true` when at least one owned artifact has matched the requested power id.

---

## 18. Villains and contracts

### 18.1 Catalog

- **REQ-760.** The villain catalog has held exactly 17 villains. The full catalog (id, name, zone, reward, puzzle cell) has been:

| # | id | Name | Zone | Reward | Puzzle |
|---|---|---|---|---|---|
| 0 | murray | Murray the Miser | continentia | 5000 | 0 |
| 1 | hack | Hack the Rogue | continentia | 6000 | 1 |
| 2 | aimola | Princess Aimola | continentia | 7000 | 2 |
| 3 | baron_makahl | Baron Johnno Makahl | continentia | 8000 | 3 |
| 4 | dread_rob | Dread Pirate Rob | continentia | 9000 | 4 |
| 5 | caneghor | Canegor the Mystic | continentia | 10000 | 5 |
| 6 | moradon | Sir Moradon the Cruel | forestria | 12000 | 6 |
| 7 | barrowpine | Prince Barrowpine | forestria | 14000 | 7 |
| 8 | bargash | Bargash Eyesore | forestria | 16000 | 8 |
| 9 | rinaldus | Rinaldus Drybone | forestria | 18000 | 9 |
| 10 | ragface | Ragface | archipelia | 20000 | 10 |
| 11 | mahk | Mahk Bellowspeak | archipelia | 25000 | 11 |
| 12 | auric | Auric Whiteskin | archipelia | 30000 | 12 |
| 13 | czar_nickolai | Czar Nickolai the Mad | archipelia | 35000 | 13 |
| 14 | magus | Magus Deathspell | saharia | 40000 | 14 |
| 15 | urthrax | Urthrax Killspite | saharia | 45000 | 15 |
| 16 | arech | Arech Dragonbreath | saharia | 50000 | 16 |

- **REQ-761.** Per-zone counts have followed `villains_per_continent = [6, 4, 4, 3]` (Continentia, Forestria, Archipelia, Saharia).
- **REQ-762.** Each villain has had a fixed five-stack army (game.json), copied verbatim into its host castle's `garrison` at salt time.

### 18.2 Contract cycle

- **REQ-770.** The contract has held: `active_id` (current contract, or empty), `cycle[5]` (rotating buffer of villain ids), `last_contract` (index 0..4 of the most recently issued slot), `max_contract` (index of the next-to-rotate-in villain).
- **REQ-771.** At game init, `cycle` has been seeded with the first five villain ids, `last_contract = 4`, `max_contract = 5`.
- **REQ-772.** `GameTakeNextContract` has incremented `last_contract` (wrapping `0..4`) and copied `cycle[last_contract]` to `active_id`. The contract banner has shown name, reward, last-known zone.

### 18.3 Fulfilment

- **REQ-780.** A combat win on a villain castle has called `GameFulfillContract(villain_id)`: credited `gold += villain.reward`, set `villains_caught[villain.index] = true`, cleared `active_id`, and rotated the next uncaught villain (starting from `max_contract`) into `cycle[last_contract]`, incrementing `max_contract`.
- **REQ-781.** Capture has additionally set the castle's `owner_kind = CASTLE_OWNER_PLAYER` and triggered the rank-up check (§5.4).

### 18.4 Find Villain

- **REQ-790.** The Find Villain spell (§16.4) has marked the active contract's castle `known = true`, making it visible on the worldmap.

### 18.5 Villain descriptions

- **REQ-800.** `strings.villain_descriptions[]` has held a `features` and a `crimes` text per villain id; the contract view has rendered both blocks below the portrait.

---

## 19. Chests and rewards

### 19.1 Chest table

- **REQ-810.** `economy.chest` has held the chance/value tables, indexed by zone (0..3 for Continentia/Forestria/Archipelia/Saharia):

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

### 19.2 Roll algorithm

- **REQ-820.** `GameRollChest(zone, x, y)` has rolled `chance = (chest_rand(g, x, y, 1) % 100) + 1` and walked the cumulative thresholds in this order, taking the first outcome whose threshold has exceeded `chance`: gold → commission → spell_power → max_spells → new_spell → empty.
- **REQ-821.** Because `chance_spell_power == chance_max_spells` element-wise, the **max_spells** outcome has been unreachable in the shipped tables; the spec preserves the OpenKB-faithful collision.

### 19.3 Outcomes

- **REQ-830.** **Gold**: `points ∈ [gold_min[zi], gold_max[zi]]`; `gold = points * 100`; an A/B prompt has offered `[A] take gold` vs `[B] distribute (+leadership = gold/50)`. With `DOUBLE_LEADERSHIP` artifact, the leadership amount has doubled.
- **REQ-831.** **Commission**: `points ∈ [commission_min[zi], commission_max[zi]]`; `commission_weekly += points`.
- **REQ-832.** **Spell power**: `spell_power += 1`.
- **REQ-833.** **Max spells** (unreachable in shipped data): `points = max_spells_base[zi]`, doubled by `DOUBLE_MAX_SPELLS`; `max_spells += points`.
- **REQ-834.** **New spell**: `spell_index = chest_rand(...) % spells_count()`; `spell_count_added = (chest_rand(...) % (zi + 1)) + 1`; `spells.counts[spell] += spell_count_added`. With no spells available, the empty outcome has fired.
- **REQ-835.** **Empty**: `chest_empty` banner displayed; no state change.
- **REQ-836.** Every outcome has consumed the chest tile (added to `consumed[]`).

### 19.4 Other chest types

- **REQ-840.** **Navmap chest**: revealed the next undiscovered zone via `world.zones_discovered[zi+1] = true` and consumed.
- **REQ-841.** **Orb chest**: revealed the entire current zone's fog (`world.orbs_found[zi] = true`) and consumed; the worldmap has supported a Space-toggle between fog-gated and fully revealed views thereafter.
- **REQ-842.** **Telecave**: telecave tiles have been paired by index within a zone (0↔1, 2↔3); stepping on one has teleported the hero to its pair. With an odd telecave count, the unpaired one has been a one-way dead-end.
- **REQ-843.** **Signpost**: each sign tile has carried a per-tile `sign_title` and `sign_body`; stepping on it has shown a dialog. There has been no global sign index.

---

## 20. Economy: gold, commission, upkeep

### 20.1 Gold sources

- **REQ-850.** **Starting gold** has been `class.starting_gold` (Knight 7,500; Paladin 10,000; Sorceress 10,000; Barbarian 7,500).
- **REQ-851.** **Commission** has been credited at every week boundary: `gold += commission_weekly`. `last_commission` has been set for the budget dialog.
- **REQ-852.** **Chest gold** has come from the chest table (§19.3).
- **REQ-853.** **Villain reward** has been credited on contract fulfilment (§18.3).

### 20.2 Gold sinks

- **REQ-860.** **Troop recruitment**: `recruit_cost * count`.
- **REQ-861.** **Boat rental**: `GameBoatCost(g)` per week.
- **REQ-862.** **Spell purchase**: spell `cost` per buy.
- **REQ-863.** **Siege weapons**: 3,000g (one-time, flag persists).
- **REQ-864.** **Alcove**: 5,000g (one-time, sets `knows_magic`).

### 20.3 Week-end ordering

- **REQ-870.** End-of-week processing has run in this order (`src/game.c:881-979`):
  1. `time_stop = 0`.
  2. `leadership_current = leadership_base`.
  3. `astrology = GamePickAstrologyCreature(week_id)`.
  4. `gold += commission_weekly`; `last_commission = commission_weekly`.
  5. `gold -= sum(stack.count * (recruit_cost / 10))`.
  6. If `boat.has_boat`: `gold -= GameBoatCost(g)`; on insufficient gold, the boat has been repossessed.
  7. `gold = max(0, gold)`.
  8. Astrology effects (§21).

### 20.4 Score

- **REQ-880.** `score = per_villain * villains_caught + per_artifact * artifacts_found + per_castle * castles_owned - kill_penalty * followers_killed`. Shipped values: 500 / 250 / 100 / 1.
- **REQ-881.** When `easy_halves == true` and difficulty == EASY, `score /= 2`. Otherwise, when `1 <= difficulty < 5`, `score *= difficulty_multiplier[difficulty]` with `[1, 2, 4, 8]`. The result has been clamped to `>= 0`.

---

## 21. Astrology and weekly events

### 21.1 Pick

- **REQ-890.** `GamePickAstrologyCreature(week_id)` has returned:
  - Index 0 (peasants) when `(week_id & 3) == 0` (every 4th week from week 0).
  - Otherwise, `1 + ((seed XOR week_id) * 1664525 + 1013904223) mod (troops_count - 1)` (deterministic LCG).

### 21.2 Dwellings

- **REQ-900.** Dwellings whose `troop_id` has matched the astrology creature have refilled to `max_population`.
- **REQ-901.** Non-matching dwellings have retained their current count (no growth).

### 21.3 Foes and castles

- **REQ-910.** For each non-player-owned castle and each hostile foe, every stack whose troop id has matched the astrology creature has grown by `troop.growth_per_week`, capped at no specified maximum (the original cap has been saturated by recruit cost).

### 21.4 Ghost conversion

- **REQ-920.** When the astrology week has been Peasants, every player-army stack whose troop has had `TROOP_ABIL_ABSORB` (ghosts only) has had its `id` rewritten to `peasants` (count preserved).

### 21.5 Week-end dialog

- **REQ-930.** After week-end processing, a two-phase dialog sequence has been queued:
  - **Phase 1 (Astrology)**: shown the astrology creature for the new week.
  - **Phase 2 (Budget)**: shown gold on hand, commission paid, boat cost (if any), army upkeep total per stack, and final balance.
- **REQ-931.** Dialogs have been dismissed by any key.

---

## 22. Combat

### 22.1 Arena

- **REQ-940.** The combat grid has been 6 columns × 5 rows (`COMBAT_W = 6`, `COMBAT_H = 5`). Each cell has been 48×34 design pixels and has held at most one unit.
- **REQ-941.** Each side has had up to 5 unit slots (`COMBAT_SLOTS = 5`); the player side index has been 0 (`COMBAT_SIDE_PLAYER`), the AI side index 1 (`COMBAT_SIDE_AI`).
- **REQ-942.** Two arena modes have been supported: **field** (open-field foes; defender stacked in column 5, rows 0..2) and **castle** (siege; defender placed at fixed positions per the 5×6 `castle_umap`/`castle_omap` tables in `src/combat.c:308-321`).
- **REQ-943.** The player has always started with one stack per row (slot i → column 0, row i).
- **REQ-944.** The obstacle map (`omap`) has used codes 1..3 for field obstacles and 5..10 for castle wall pieces; `omap == 0` has meant open. The unit map (`umap`) has held packed unit ids (1-based; 0 = empty).

### 22.2 Combat state

- **REQ-950.** The `Combat` struct (`src/combat.h:89-130`) has held: `units[2][5]`, `omap[6][7]`, `umap[6][7]`, `spoils[2]`, `powers[2]` (artifact bits), `heroes[2]` (`g` for player, `NULL` for AI), `turn` (round counter), `phase`, `spells_this_round`, `side`, `unit_id` (current actor), `castle` (siege flag), `first_kill_seen`, `stacks_destroyed`, `rng_state`, `banner[80]`, `log_lines[8][80]`, `log_count`, `cursor_x/y`, `target_filter`, `picker_active`, `cursor_frame`, `result` (0 running / 1 win / 2 loss/flee), `villain_id`, `mode`, `target_name`.
- **REQ-951.** A `CombatUnit` (`src/combat.h:63-83`) has held: `troop_idx` (-1 if empty), `count`, `turn_count` (snapshot at turn start, used by damage formula), `max_count` (snapshot at combat start, used by ABSORB/LEECH cap), `dead`, `frame`, `injury` (sub-HP residual), `acted`, `retaliated`, `moves`, `shots`, `flights`, `frozen`, `out_of_control`, `(x, y)`, `hit_flash`.

### 22.3 Turn order

- **REQ-960.** The player side has always acted first each round. Within a side, the unit with the lowest slot index has gone first; ties have been broken by slot index.
- **REQ-961.** `combat_next_unit` (`src/combat.c:705-726`) has scanned slots `[unit_id+1..4]` for the first stack with `count > 0 && !acted`, then wrapped via `phase++` to scan `[0..unit_id]`. With no actable units, the side has handed over.
- **REQ-962.** `combat_reset_turn` (`src/combat.c:678-744`) has reset `acted=false` and `retaliated=false` for every unit, refreshed `moves = troop.move_rate`, refreshed `flights = 2` for FLY units, and (Trolls / `TROOP_ABIL_REGEN`) reset `injury = 0` at the start of the player side.
- **REQ-963.** A `frozen` unit has skipped its turn (`acted = true` immediately), then had `frozen` cleared by the next `reset_turn`.

### 22.4 Move and fly

- **REQ-970.** `combat_move_unit` (`src/combat.c:597-620`) has moved a unit one tile per call:
  - Out-of-bounds → blocked (return 0).
  - Obstacle in target cell → blocked (return 0).
  - Friendly under-control unit in target cell → blocked (return 0).
  - Hostile (or own-side OOC) unit in target cell → melee attack, `acted = true` (return 2).
  - Empty cell → relocate, `moves -= 1`; `acted = true` when `moves == 0` (return 1).
- **REQ-971.** `combat_fly_unit` (`src/combat.c:624-635`) has ignored obstacles and units; it has landed only in empty cells. `flights -= 1`; `acted = true` when `flights == 0`.
- **REQ-972.** OOC units (`u->out_of_control == true`) have continued to take turns but have attacked their own side (the AI's `ai_pick_target` has treated them as targetable, and their move logic has chosen friendly targets).

### 22.5 Damage formula

- **REQ-980.** `combat_deal_damage` (`src/combat.c:395-526`) has computed final damage according to this contract:
  1. **External path** (spell damage): `final_damage = external_damage`.
  2. **Internal path**:
     - `dmg = is_ranged ? (MAGIC ? ut->ranged_min : kb_rand(ut->ranged_min, ut->ranged_max)) : kb_rand(ut->melee_min, ut->melee_max)`.
     - `total = dmg * u->turn_count`.
     - `skill_diff = ut->skill_level + 5 - tt->skill_level`.
     - `final_damage = (total * skill_diff) / 10`.
     - **SCYTHE** (Demon): with 10% chance (`kb_rand(1,100) > 89`), `demon_kills = ceil(t->count / 2)` and adds `tt->hit_points * demon_kills` after the morale and artifact passes.
  3. **Morale** (attacker side has hero AND attacker is in control):
     - `LOW` → `final_damage /= 2`.
     - `HIGH` → `final_damage += final_damage / 2` (×1.5).
     - `NORMAL` → no change.
  4. **Artifact attacker**: `INCREASED_DAMAGE` → `final_damage += final_damage / 2` (×1.5).
  5. **Artifact target**: `QUARTER_PROTECTION` → `final_damage = (final_damage / 4) * 3` (×0.75).
  6. **Accumulate**: `final_damage += t->injury`.
  7. **Add SCYTHE bonus**: `final_damage += tt->hit_points * demon_kills`.
  8. **Kills**: `kills = (tt->hp > 0) ? final_damage / tt->hp : t->count`.
  9. **Residual**: `t->injury = (tt->hp > 0) ? final_damage % tt->hp : 0`.
  10. **Stack outcome**: if `kills < t->count` → `t->count -= kills`; else → `t->dead = true; t->count = 0; kills = t->turn_count; final_damage = kills * tt->hp`.
- **REQ-981.** A successful MAGIC ranged attack against an `IMMUNE` defender has returned -1 with no state change (the spell has fizzled); for non-MAGIC attacks the IMMUNE flag has been ignored.
- **REQ-982.** Ranged attacks have decremented `u->shots` regardless of the outcome.
- **REQ-983.** When the target side has had a hero attached (i.e. `t_side` was the player), `kills` have been added to `g->stats.followers_killed`.

### 22.6 Retaliation

- **REQ-990.** Retaliation has fired iff: not external; not already a retaliation pass; defender has not yet retaliated this round; defender has survived (`t->count > 0`); attack has been melee (ranged sets `retaliation = true` early to skip).
- **REQ-991.** Retaliation has been a recursive `combat_deal_damage` call from defender to attacker with `retaliation = true`. The retaliating unit's flag has been set first to prevent infinite recursion.
- **REQ-992.** The `retaliated` flag has been reset at the start of the unit's next turn.

### 22.7 Special-ability post-effects

- **REQ-1000.** **ABSORB** (Ghosts): `u->count += kills` (no max-cap inflation; bounded by `max_count` in subsequent operations).
- **REQ-1001.** **LEECH** (Vampires): `u->count += final_damage / ut->hp`, clamped to `max_count` with `injury = 0` on cap.
- **REQ-1002.** **REGEN** (Trolls): `injury = 0` at the start of the player side's turn.
- **REQ-1003.** **MAGIC** (Druids, Archmages): ranged attacks have used the fixed `ranged_min` value rather than a roll, and have been cancelled by `IMMUNE`.
- **REQ-1004.** **IMMUNE** (Dragons): MAGIC attacks and the spells Fireball, Lightning, Freeze, and Turn Undead have all been blocked.
- **REQ-1005.** **SCYTHE** (Demons): see REQ-980 step 2.
- **REQ-1006.** **UNDEAD**: only Turn Undead has targeted them; they have been immune to morale modifiers.

### 22.8 Combat spells

- **REQ-1010.** Casting has been one spell per round (`spells_this_round < 1`); the hero's class+rank `knows_magic` has been required.
- **REQ-1011.** **Clone** (id 0, cost 2000): `clones = (sp * 10 + u->injury) / t->hp`; `u->count += clones; u->max_count += clones; u->injury = (sp*10 + u->injury) % t->hp`.
- **REQ-1012.** **Teleport** (id 1, cost 500): two-picker workflow — pick a unit, then pick an empty destination; relocates.
- **REQ-1013.** **Fireball** (id 2, cost 1500): external damage `25 * sp`; blocked by `IMMUNE`.
- **REQ-1014.** **Lightning** (id 3, cost 500): external damage `10 * sp`; blocked by `IMMUNE`.
- **REQ-1015.** **Freeze** (id 4, cost 300): sets `t->frozen = true`; blocked by `IMMUNE`.
- **REQ-1016.** **Resurrect** (id 5, cost 5000): `revived = max(1, sp)`; clamped so `count + revived <= max_count`; `u->count += revived; u->injury = 0`.
- **REQ-1017.** **Turn Undead** (id 6, cost 2000): external damage `50 * sp` against UNDEAD targets only; blocked by `IMMUNE`.
- **REQ-1018.** Picker (`combat_pick_target`, `src/combat.c:844-850`) has supported filters `PICK_FILTER_ANY`, `_HOSTILE`, `_FRIENDLY`, `_UNDEAD`, `_EMPTY`, etc.
- **REQ-1019.** SP (spell power) has come from `g->stats.spell_power`, which the Amulet of Augmentation has doubled at pickup (§17.4).

### 22.9 Player input

- **REQ-1030.** Movement has used arrow keys / numpad 8/4/6/2 for cardinals and Home/PgUp/End/PgDn (or numpad 7/9/1/3) for diagonals. KP_5 has been "wait" (acted = true).
- **REQ-1031.** `S` has fired ranged (rejected when `shots == 0` or surrounded by hostiles).
- **REQ-1032.** `U` has opened the spell menu (A..G for spells 0..6).
- **REQ-1033.** `G` (give up) and `Esc` have set `result = 2` (loss/flee).
- **REQ-1034.** `C` has shown the controls overlay; `V` has been reserved for view-character (not implemented in the combat layer).

### 22.10 AI behaviour

- **REQ-1040.** `combat_ai_action` (`src/combat.c:1472-1553`) has selected an action in this priority order:
  1. **Frozen** → skip turn.
  2. **Close target** (1-tile) → melee attack.
  3. **Ranged** when shots > 0 and not surrounded → far target.
  4. **Fly** when FLY and flights > 0 → land adjacent to a far target.
  5. **Walk** toward closest or far target.
  6. **Pass** (skip).
- **REQ-1041.** `ai_pick_target` (`src/combat.c:1390-1423`) has scored: far ranged enemies = 10000, others = `1000 - hp`. Lower-HP targets have been preferred among non-shooters. Out-of-control attackers have considered their own side as targetable.
- **REQ-1042.** Tie-breaks for movement direction have used iteration order: `dy ∈ {+1, 0, -1}` outer, `dx ∈ {-1, 0, +1}` inner.

### 22.11 Combat log and banner

- **REQ-1050.** Up to 8 log lines have been kept in a ring buffer (`COMBAT_LOG_LINES = 8`, line length `COMBAT_LOG_LINE_LEN = 80`).
- **REQ-1051.** Lines have come from `game.json:combat_log` templates: `melee_hit`, `retaliate`, `ranged_hit`, `no_effect_msg`, `frozen`, `immune`, `cloned`, `resurrected`, `teleported`, `only_one_spell`, `cannot_cast`, `cast_fireball`, `cast_lightning`, `cast_turn_undead`, `no_ammo`, `cant_shoot`, with `%TROOP%`, `%COUNT%`, etc. tokens.
- **REQ-1052.** The top banner has shown the active actor's name plus M/S counters before the first kill, and has switched to "<actor> vs <target> killing N" after.

### 22.12 Result and spoils

- **REQ-1060.** **Win** (`result = 1`): all defenders dead. Spoils = `sum(troop.spoils_factor * 5 * count)` over killed enemies have been credited to `gold`. Surviving player units have been written back to `game.army[5]` with `GameCompactArmy`.
- **REQ-1061.** **Loss** (`result = 2`): all attackers dead. The hero has triggered "temp death" (clear army, grant 20 peasants, dismount, drop boat, teleport home).
- **REQ-1062.** **Flee** (Esc/G, `result = 2`): treated as loss for adventure-state purposes; player troop losses have already accumulated in `followers_killed`.

### 22.13 Siege weapons

- **REQ-1070.** The `siege_weapons` flag has been purchased and persisted but has not been checked anywhere in the combat or castle siege flow (OpenKB-faithful).

### 22.14 Combat RNG

- **REQ-1080.** Combat has used an independent LCG state (`Combat.rng_state`) seeded by:
  - `s = g->seed XOR 0x5DEECE66D`.
  - XOR with `g->stats.followers_killed * 0x9E3779B97F4A7C15`.
  - XOR with `mode * 0xBF58476D1CE4E5B9`.
  - For each char of the target name: `s = s * 131 + ch`.
  - Final `rng_state = s != 0 ? s : 1`.
- **REQ-1081.** `kb_rand(c, min, max)` has used `state = state * 25214903917 + 11`; `r = state >> 32`; result `min + r % (max - min + 1)`.
- **REQ-1082.** Independent state has prevented combat rolls from advancing the world RNG.

### 22.15 Combat input wrappers

- **REQ-1090.** `combat_player_action_full` (`src/combat.c:1271-1373`) has dispatched all player input. Movement and attacks have consumed the turn; `C` (controls overlay) has been modal but has not consumed the turn.

### 22.16 Combat render

- **REQ-1100.** The arena has been drawn at `(CL_COMBAT_X = 16, CL_COMBAT_Y = 22)`, size `288×170`, cell `48×34`.
- **REQ-1101.** Render order has been: grass field, obstacles (omap), units (with count badge), damage flash overlay, picker cursor (when `picker_active`), then chrome and title bar.
- **REQ-1102.** Only the active unit has animated (frame cycle 0..3 at ~150 ms per frame); on frame wrap the AI has taken its action.

### 22.17 Combat-test harness

- **REQ-1110.** `combat_test_digest(seed, attacker, count_a, defender, count_b, rounds)` has run a deterministic two-stack fight and returned a 64-bit FNV digest, used by `make combat-test` to detect formula regressions.

---

## 23. Scoring, victory, and defeat

### 23.1 Score

- **REQ-1080.** See §20.4. Score has been recomputed at every state mutation and stored in `Game.stats.score` for display.

### 23.2 Difficulty multipliers

- **REQ-1090.** `scoring.difficulty_multiplier[]` has been `[1, 2, 4, 8]` (Normal, Hard, Impossible, reserved). `easy_halves` has been `true` so that the Easy score has been halved.

### 23.3 Win

- **REQ-1100.** The win has fired when the player has searched (key `S`) on the buried scepter tile (matching zone, x, and y). The win flow has run `show_win_game(g, res)` (§23.7).

### 23.4 Lose

- **REQ-1110.** The lose has fired when `days_left` has reached 0 (computed at the next `end_day`). The lose flow has run `show_lose_game(g, res)` (§23.8).

### 23.5 Both screens

- **REQ-1120.** Both end-game screens have shown a left half (DBLUE background) with the header / body / footer text from `strings.win` or `strings.lose`, and a right half with an end-game cartoon (animated approach to throne for win, lose-art for loss).
- **REQ-1130.** Substitution tokens supported: `%NAME%`, `%RANK%`, `%SCORE%`.

### 23.6 Cartoon (win)

- **REQ-1140.** The win cartoon has used the tiles named in `ending` (`grass_tile`, `carpet_tile`, `hero_tile`, `throne_backdrop`), arranged in a `grid_width × grid_height` grid, with a carpet column and an animation that has stepped the hero forward over `frame_count` frames at `ticks_per_step` per step.

---

## 24. Save format and slots

### 24.1 Format

- **REQ-1150.** Saves have been JSON files (uncompressed UTF-8). The current `SAVE_VERSION` has been 7. Loading a save with a different version has failed gracefully with an error.
- **REQ-1151.** Save slot files have been named `save_<slot>.dat` for slots 0..9.
- **REQ-1152.** A save has carried `pack_id` and `pack_hash`; a save from a different pack has been refused (cross-pack guard).

### 24.2 Save directory

- **REQ-1160.** Linux: `$XDG_DATA_HOME/openbounty` (default `~/.local/share/openbounty`).
- **REQ-1161.** macOS: `~/Library/Application Support/OpenBounty`.
- **REQ-1162.** Windows: `%APPDATA%\OpenBounty`.
- **REQ-1163.** `--save-dir <path>` has overridden the platform default.

### 24.3 Schema

- **REQ-1170.** The save has held: `version`, `seed`, `pack_id`, `pack_hash`, `mode`, `character` (name, class, rank_index, rank_title, difficulty, mount), `stats` (gold, commission_weekly, leadership_base/current, followers_killed, score, spell_power, max_spells, knows_magic, siege_weapons, time_stop, steps_left_today, days_left, game_over, last_commission, options[8]), `position`, `army[]` (sparse), `spells` (sparse), `contract`, `artifacts.found[]`, `world` (zones_discovered[], orbs_found[], puzzle_revealed[]), `boat`, `towns[]`, `castles[]`, `scepter`, `consumed[]`, `placements[]`, `foes[]`, and `map_state.<zone>.fog` (hex-encoded per-row, 4 tiles per nibble, MSB first).
- **REQ-1171.** Empty slots have been omitted from the army and spells arrays; loading has filled missing entries with empty/zero values.

### 24.4 Header preview

- **REQ-1180.** `SaveGameReadHeader` has parsed only the metadata needed by the save picker UI (name, class, rank, zone, days_left, gold) without loading the rest.

---

## 25. UI: views, HUD, dialogs, prompts

### 25.1 View enum

- **REQ-1190.** Views have been: `VIEW_NONE`, `VIEW_MENU`, `VIEW_CHARACTER`, `VIEW_ARMY`, `VIEW_SPELLS`, `VIEW_CONTRACT`, `VIEW_PUZZLE`, `VIEW_WORLDMAP`, `VIEW_OPTIONS`, `VIEW_CONTROLS`, `VIEW_TOWN`, `VIEW_HOME_CASTLE`, `VIEW_OWN_CASTLE`, `VIEW_DWELLING`, `VIEW_ALCOVE`, `VIEW_RECRUIT_SOLDIERS`, `VIEW_WIN`, `VIEW_LOSE`.
- **REQ-1191.** A view stack has held up to four views (`VIEWS_STACK_MAX = 4`); only the top has rendered. `views_set(v)` has replaced the stack; `views_push(v)` has stacked; `views_dismiss()` has popped.

### 25.2 HUD

- **REQ-1200.** The HUD sidebar has been a 48×170-pixel column at `(256, 22)` (4 panels of 48×34 plus the gold purse), persisting over the map view.
- **REQ-1201.** The four upper panels have shown: contract portrait/silhouette (animated 4-frame, 2 Hz when active), siege cart silhouette/animation (animated when `siege_weapons == 1`), magic star silhouette/animation (animated when `knows_magic == true`), puzzle 5×5 grid (covers vanish on capture/find).
- **REQ-1202.** The gold purse panel (bottom) has shown the gold icon and `gold` numeric label (yellow, right-aligned).

### 25.3 Chrome

- **REQ-1210.** The chrome has been a 320×200 frame with a transparent interior, a top status bar (9 px), a bar strip (5 px), the map area (240×170), a sidebar (48×170), and bottom frame (8 px). Pixel coords have followed `src/layout.h`.
- **REQ-1211.** The status-bar background colour has matched the difficulty: Easy=Cyan, Normal=Red, Hard=Blue, Impossible=Magenta.
- **REQ-1212.** The status text has been (in priority): fast-quit prompt, exit hint, day/time-stop counter.

### 25.4 Dialog and prompt

- **REQ-1220.** `open_dialog(header, body)` has shown a 240×68 panel at `(16, 124)` with a yellow header line and word-wrapped white body (29-char width, 7 rows max per page); pagination has stepped on any key.
- **REQ-1221.** Prompts have been one of: `PK_YES_NO` (`y/n/Esc`), `PK_NUMERIC` (digits 1..N + `Esc`), `PK_AB_CHOICE` (`A`/`B`), `PK_TEXT_INPUT` (digits + Backspace + `Enter`/`Esc`).
- **REQ-1222.** Prompt results have been one of `PROMPT_RESULT_NONE` (still open), `PROMPT_RESULT_YES`, `PROMPT_RESULT_NO`, `PROMPT_RESULT_CANCEL`, `PROMPT_RESULT_1..5`.

### 25.5 Specific views

- **REQ-1230.** **VIEW_CHARACTER**: portrait + name + rank, then stat rows (leadership current, commission weekly, gold, spell power, max spells, villains caught, artifacts found, castles owned, followers killed, current score), then an artifact belt (4×2 icons) and a zone-map mini-strip (2×2).
- **REQ-1231.** **VIEW_ARMY**: 5 troop rows with id, count, HP, melee min/max, ranged min/max/ammo, recruit cost, current morale (Low/Normal/High in white, "Out of Control" in red).
- **REQ-1232.** **VIEW_SPELLS**: two columns; pressing A..G has cast the corresponding adventure spell.
- **REQ-1233.** **VIEW_CONTRACT**: villain portrait + name/alias/reward/last seen/castle + features + crimes blocks; renders "no contract" placeholder when none.
- **REQ-1234.** **VIEW_PUZZLE**: 5×5 grid; revealed cells have shown the underlying scepter-zone terrain; covered cells have shown villain face or artifact icon.
- **REQ-1235.** **VIEW_WORLDMAP**: minimap with terrain colours (`grass = #55FF55`, `forest = #00AA00`, `mountain = #AA5500`, `water = #5555FF`, `desert = #FFFF55`, `fog = #000000`); markers for known castles and visited towns; hero blink; Space toggles fog gating when an orb has been found in the current zone.
- **REQ-1236.** **VIEW_TOWN**: header `Town of <NAME>` + `GP=<gold>K`, five rows A..E.
- **REQ-1237.** **VIEW_HOME_CASTLE**: castle backdrop, two rows (`A) Recruit Soldiers`, `B) Audience with the King`).
- **REQ-1238.** **VIEW_OWN_CASTLE**: 5 garrison rows; Space toggles direction; A..E selects slot.
- **REQ-1239.** **VIEW_DWELLING**: location backdrop, dwelling kind name, count, cost, gold, recruit prompt.
- **REQ-1240.** **VIEW_ALCOVE**: hillcave backdrop with offer prompt.
- **REQ-1241.** **VIEW_RECRUIT_SOLDIERS**: 5 castle-class troops A..E with cost or "n/a" gating (`leadership_current < hp * 6`); right column shows count input.
- **REQ-1242.** **VIEW_OPTIONS**: keybind reference grid.
- **REQ-1243.** **VIEW_CONTROLS**: 8 settings (`delay`, `sounds`, `walk_beep`, `animation`, `army_size`, `cga` [hidden], `music`, `volume`); defaults `[4, 1, 1, 1, 1, 0, 0, 5]`. Audio rows (`sounds`, `music`, `volume`) have grayed out when no audio device has been available.
- **REQ-1244.** **VIEW_MENU**: nested menu (Game Menu → Views/Options/Back/Exit; Views → Army/Spells/Character/Contract/Puzzle/View Map/Back; Options → Save/Load/New Game/Back).
- **REQ-1245.** **VIEW_WIN** / **VIEW_LOSE**: end-game screens (§23.5).

### 25.6 Banners

- **REQ-1250.** Every UI string outside of view-internal layout has come from `strings.banners` in `game.json` (e.g. `town_no_gold`, `chest_gold`, `encounter_join_named`, `spell_time_stop`, `audience.intro`). Substitution tokens (`%NAME%`, `%GOLD%`, `%TROOP%`, `%COUNT%`, `%LABEL%`, `%STEPS%`, etc.) have been replaced at format time.
- **REQ-1251.** Number-name buckets (`number_name_thresholds[]` and `number_name_labels[]`) have produced fuzzy size labels ("Few", "Several", "Many", etc.) when `controls.army_size = 1`.

---

## 26. Input and controls

### 26.1 Keyboard

- **REQ-1260.** Movement, action, and view keys have followed the table in §9.1.

### 26.2 Gamepad

- **REQ-1270.** Mappings have followed §9.2.

### 26.3 Settings persistence

- **REQ-1280.** `Game.stats.options[8]` has held the controls-menu values (delay, sounds, walk beep, animation, army size, cga, music, volume); these have been persisted in saves and re-applied on load.

---

## 27. Cheats and debug

### 27.1 F10 cheat menu

- **REQ-1290.** Pressing `F10` in adventure mode (when no overlay has been active) has opened a modal cheat dialog with the body:

```
G  Gold        N  Zone
V  Leadership  O  Fog
M  Magic       W  Win
U  Spells      L  Lose
S  Siege
F  Flight
F10/ESC  close
```

- **REQ-1291.** The dispatched letters have done:
  - `G` — `gold += 50000`.
  - `V` — `leadership_base += 100; leadership_current += 100`.
  - `M` — `spell_power += 1; max_spells += 1`.
  - `U` — `spells.counts[i] += 1` for `i = 0..13`.
  - `S` — `siege_weapons = 1`.
  - `F` — `mount = MOUNT_FLY`.
  - `N` — first undiscovered zone marked `world.zones_discovered[zi] = true`.
  - `O` — every tile in the current zone marked `fog.seen = true`.
  - `W` — close menu, run `show_win_game`.
  - `L` — close menu, run `show_lose_game`.
  - `F10` / `Esc` — close menu without action.
  - any other letter — "Unknown cheat: X" dialog, menu remains modal.

---

## 28. Audio

### 28.1 Device

- **REQ-1300.** `audio_init(res)` (`src/audio.c:83`) has initialised the raylib audio device, redirecting stderr around the call to swallow ALSA noise on WSL. With initialisation failure, the game has continued silently and `audio_is_available()` has returned false; the controls-menu rows for `sounds`, `music`, and `volume` have been disabled.
- **REQ-1301.** `audio_shutdown` has been called at process exit; the `audio_*` functions have been idempotent across init/shutdown cycles.

### 28.2 Tracks

- **REQ-1310.** Two music tracks have been supported: `AUDIO_TRACK_OPENWORLD` (overworld OGG) and `AUDIO_TRACK_COMBAT` (combat OGG), both loaded from paths in `Resources.audio`.
- **REQ-1311.** `audio_set_track(t)` has hard-cut between tracks; the previous stream has been stopped before the new one has played. The active track has been switched at zone load and at combat enter/exit.

### 28.3 Tunes

- **REQ-1320.** Four sound effects have been loaded from `Resources.audio`: `AUDIO_TUNE_WALK`, `AUDIO_TUNE_BUMP`, `AUDIO_TUNE_CHEST`, `AUDIO_TUNE_DEFEAT` (all WAV).
- **REQ-1321.** `audio_play_tune(t)` has played one of the four; in-flight tunes have been pre-empted (voice stealing, single SFX channel).

### 28.4 Volume and ducking

- **REQ-1330.** Master volume has been an integer 0..9 mapped to a float `0..1` via `v / 9`. The `options[7]` slider has driven the master.
- **REQ-1331.** Constants in `src/audio.c:32-38` have been: `MUSIC_HEADROOM = 0.50`, `SFX_HEADROOM = 0.80`, `DUCK_FACTOR = 0.20` (target while a tune has played), `DUCK_ATTACK = 0.40` (per-frame down-step), `DUCK_RELEASE = 0.10` (per-frame up-step).
- **REQ-1332.** Effective music volume has been: `master * MUSIC_HEADROOM * duck_level * (music_enabled ? 1 : 0)`.
- **REQ-1333.** `audio_tick` has been called once per frame to update the duck level and to drive the streaming buffer.

### 28.5 Options

- **REQ-1340.** `options[1]` (sounds) has gated SFX globally; `options[6]` (music) has paused/resumed the active track; `options[7]` (volume) has set master; `options[2]` (walk beep) has gated the per-step `AUDIO_TUNE_WALK`.
- **REQ-1341.** Toggling music off has paused the stream rather than rewound it.

---

## 29. Rendering

### 29.1 Render target and scaling

- **REQ-1350.** All rendering has gone first into a 320×200 offscreen `RenderTexture2D` with `TEXTURE_FILTER_POINT`. The target has then been blitted to the window centred, letterboxed, at integer scale `max(min(win_w / 320, win_h / 200), 2)` (minimum 2×).

### 29.2 Map

- **REQ-1360.** The map viewport has shown a 5×5 tile region (`CL_MAP_W = 240` × `CL_MAP_H = 170`) at `(CL_MAP_X = 16, CL_MAP_Y = 22)`. Camera anchor: `cam_x = position.x - 2`, `cam_y = position.y - 2`, clamped at zone edges so the hero stays visible.
- **REQ-1361.** Fog edge gradients have been drawn as 3 alpha bands (128, 64, 32) along edges between seen and unseen tiles, stepping outward by 0, 2, 4 pixels.
- **REQ-1362.** The hero sprite has been drawn centred (with horizontal mirror when `facing_left`); the boat sprite has been drawn at `boat.x/y` when `boat.has_boat == true && travel_mode == TRAVEL_WALK` and the boat's tile has been visible and fog-seen.

### 29.3 Animation

- **REQ-1370.** Hero animation has cycled `anim_frame ∈ {0..3}` at intervals of `0.05 + options[0] * 0.05` seconds. With animation toggled off (`options[3] == 0`) `anim_frame` has stayed at 0.

### 29.4 Font

- **REQ-1380.** The bitmap font has been loaded by `bfont_init(path)` (`src/bfont.c:35-62`) from a 256-glyph 8×8 sheet at `art/font/kb-font.png`. After load, the engine has copied glyphs from `'|'`, `'/'`, `'-'`, `'\\'` into control-char slots `0x1D, 0x05, 0x1F, 0x1C` for the DOS twirl spinner.
- **REQ-1381.** Text rendering has been left-aligned by default (`bfont_draw`); centred (`bfont_draw_centered`) and width-measured (`bfont_measure`) variants have been provided. Newlines have wrapped to the original x.

### 29.5 Palette

- **REQ-1390.** The 256-color VGA palette has been loaded from `palettes/palette.bin` (768 raw RGB bytes). The first 16 indices have been: BLACK, DBLUE, DGREEN, DCYAN, DRED, MAGENTA, BROWN, GREY, DGREY, BLUE, GREEN, CYAN, RED, VIOLET, YELLOW, WHITE.
- **REQ-1391.** A built-in EGA fallback (`src/palette.c:8-17`) has been installed before file load; on missing or short file the fallback has remained.
- **REQ-1392.** Code has indexed via `PAL_CLR(NAME)`; UI rendering has used these names rather than raw raylib colors.

### 29.6 Tile cache

- **REQ-1395.** Map tiles have been lazy-loaded by name from `art/tiles/<art>.png` into a 96-entry cache (`src/tile_cache.c`). Each texture has been set to `TEXTURE_FILTER_POINT` and `TEXTURE_WRAP_CLAMP`. A cache miss with the cache full has fallen back to "grass".

### 29.7 Sprites

- **REQ-1396.** Sprite groups loaded from `Resources.sprites` have been: `hero_walk[4]`, `hero_boat[4]`, `class_portrait[4]`, `villain_portrait[17]`, `villain_anim[17][4]` (built by stripping `.png` and appending `_<00..03>.png`), `view_icon[14]` (8 artifacts + 6 extras), `troop_sprite[25]`, `troop_anim[25][4]`, six location backdrops, four end-cartoon tiles, HUD silhouettes and 4-frame animations, splashes, chrome, and a 15-tile combat tileset.

### 29.8 End cartoon

- **REQ-1400.** The win cartoon has been driven by `Resources.ending`: `grass_tile`, `carpet_tile`, `hero_tile`, `throne_backdrop`, `grid_width`, `grid_height`, `carpet_column`, `carpet_length`, `frame_count`, `ticks_per_step`, `troop_border`. With `troop_border == true`, troops have animated around the non-carpet edge cells with a 4-frame walk cycle (left columns unflipped, right columns flipped horizontally).
- **REQ-1401.** The cartoon has advanced one frame every `ticks_per_step` ticks at ~12 ticks/sec; a non-modifier keypress has skipped to the end. The cartoon has rendered into the same 320×200 target and has been blitted with the same letterbox.

---

## 30. CLI, packs, and platform

### 30.1 CLI flags

- **REQ-1410.** Recognised flags: `--version` / `-v` (print and exit), `--help` / `-h` (print usage and exit), `--fullscreen` (toggle fullscreen at startup), `--pack <name|path>`, `--save-dir <path>`, `--seed <N>`, `--asset-dir <path>`, `--extract <KB.EXE>` (one-time first-run), `--combat-test <seed:attacker:N:defender:N:rounds>` (headless combat test), `--record <dir>` (snapshot recording), `--encode-movie` (post-record MP4 encode).

### 30.2 Packs

- **REQ-1420.** Pack discovery and switching have followed §2.2.

### 30.3 Platform

- **REQ-1430.** Linux (X11/Wayland), macOS, and Windows have been supported via raylib. Cross-compilation has produced platform-native binaries.

---

## 31. Recorder, encoder, and harness

### 31.1 Recorder

- **REQ-1440.** `recorder_init(cap_entries, cap_bytes)` has held a ring of (state JSON + framebuffer PNG) pairs in memory, with defaults of 16 entries and 8 MB respectively when args have been zero/negative. `recorder_active()` has reported whether the recorder has been live.
- **REQ-1441.** State, combat, map, fog, and render-target pointers have been registered via `recorder_attach_*`. With a render target attached, every capture has exported the framebuffer to PNG.
- **REQ-1442.** `recorder_capture(trigger)` has pushed one record with monotonic `seq`, wall-clock `ms`, the trigger string ("step:right", "rank:lord", "prompt:open:yes_no", etc.), the state-snapshot JSON, and the optional PNG. Old entries have been evicted when caps have been exceeded.
- **REQ-1443.** With `recorder_set_record_dir(dir)`, every capture has additionally written `tick_<NNNNNN>.json`, `tick_<NNNNNN>.png`, and a manifest line to `<dir>/manifest.ndjson`.
- **REQ-1444.** `recorder_dump_ring(dir)` has performed an on-demand dump and returned the count of entries written.

### 31.2 Encoder

- **REQ-1450.** `mp4_encode_dir(dir, cb, user, err, errcap)` (`src/encode_mp4.c`) has read the manifest, loaded each PNG, and produced `<dir>/movie.mp4` (H.264 baseline + AAC). Frames have been padded from 320×200 to 320×208 (a multiple of 16), with the bottom 8 rows in YUV black (Y=16, U=V=128). The progress callback has reported `current/total` and a status string ("Reading manifest", "Encoding", "Muxing", "Done").
- **REQ-1451.** Encoding has used `minih264` (encoder) and `minimp4` (muxer) compiled in separate translation units to avoid symbol collisions.

### 31.3 Screenshots

- **REQ-1460.** Pressing the backtick (`` ` ``) key has captured the current frame to `screenshots/<prefix>_<NNNN>.png` (`src/screenshot.c:23-26`). The sequence number has been the next free 0..9999.
- **REQ-1461.** Opening `VIEW_CHARACTER` from a closed state has additionally auto-captured a screenshot.

### 31.4 Harness

- **REQ-1470.** `--harness <socket-path>` has bound a Unix domain socket. Commands have been one line each (`\n`-terminated), with replies of `OK [payload]` or `ERR [reason]`.
- **REQ-1471.** The protocol has supported: `key:<NAME>` (queue an `IsKeyPressed` edge), `hold:<NAME>` / `release:<NAME>` (sustained key down), `char:<C>` / `text:<STRING>` (`GetCharPressed` codepoints), `frames:<N>` (advance N frames with no input), `snap:<PATH>` (write rt PNG, associate with next state capture), `dump[:PATH]` (dump trace), `state` (one-line JSON of game state), `quit` (clean shutdown).
- **REQ-1472.** `harness_tick` (called once per frame) has accepted at most one client per frame, processed one command, closed the connection, and cleared one-shot input queues.

### 31.5 Harness input shim

- **REQ-1480.** Game code has called `harness_key_pressed`, `harness_key_down`, `harness_get_key_pressed`, `harness_get_char_pressed` rather than raylib equivalents directly; these have routed through the harness queues so injected input has matched real-key paths exactly.

### 31.6 State snapshot format

- **REQ-1490.** The state JSON (used by both recorder and `state` harness command) has been a single-line object containing: hero `(x, y, facing, anim_frame)`, difficulty, character (name/class/rank/mount/stats), army, artifacts, spells, fog (per-row hex nibble encoding, 4 tiles/nibble, MSB-first), and a 5×5 tile window centred on the hero (terrain, interactive type, walkability for the hero's current travel mode).

---

## Appendix A — Glossary

- **Adventure mode** — overworld view with hero movement.
- **Alcove** — Aurange's chamber, one per zone, that has granted `knows_magic` for 5,000g.
- **Astrology creature** — the troop type promoted at the start of each in-game week.
- **Bounce-back** — return of the hero to the prior tile when an interactive flow has been cancelled.
- **Continent / Zone** — used interchangeably to mean one of the four 64×64 maps.
- **End week** — manual "skip to next week boundary" action.
- **OOC** — Out of Control; a stack whose `troop.hp * count > leadership_current`.
- **Salt** — the per-zone randomised placement of artifacts, navmaps, orbs, telecaves, dwellings, friendly foes.
- **Salted placement** — a single `(zone, x, y, kind, id)` record produced by `salt_continent`.
- **Scepter** — the buried object the player has searched for to win the game.

## Appendix B — Files referenced

- Source: `src/*.c`, `src/*.h`, `src/screens/*.c`.
- Data: `assets/kings-bounty/game.json`, `assets/kings-bounty/maps/*.dat`, `assets/kings-bounty/palettes/palette.bin`.
- Companion docs: `docs/OPENBOUNTY-SPEC.md` (engine spec), `docs/OPENKB-SPEC.md` (reference implementation reverse-engineering), `docs/COMBAT-PLAN.md` (combat design notes), `docs/PACK-FORMAT.md` (mod pack format), `docs/SPEC-COMPARISON.md` (this game vs OpenKB).
