# Pack format

A *pack* is a self-contained directory (or zipped `.openbounty` archive)
that supplies everything the engine needs to run one specific game:
gameplay rules, world data, art, audio, fonts, and palettes. The base
King's Bounty pack at `assets/kings-bounty/` is the canonical reference.

Packs are how OpenBounty supports re-themed games, total conversions,
and community content without recompiling the engine.

---

## 1. Directory layout

```
my-pack/
├── game.json          # All gameplay data + asset path manifest (required)
├── art/               # Sprites, tiles, fonts, UI chrome (PNG)
├── audio/             # Music + SFX (WAV / OGG)
├── maps/              # Zone tile-grid files (*.dat, ASCII)
└── palettes/          # 256-color palette binaries (768-byte raw RGB)
```

The directory name has no special meaning. The pack identifies itself
via `pack_id` inside `game.json`.

A packaged distribution is a ZIP file with the `.openbounty` extension,
containing the same tree at the archive root. The engine treats loose
directories and `.openbounty` archives interchangeably.

---

## 2. `game.json` top-level keys

All paths are relative to the pack root. Required fields are marked.

| Key | Type | Purpose |
|---|---|---|
| `pack_id`     | string ✱ | Stable identifier (e.g. `"kings-bounty"`). Used as save-file partition key. |
| `pack_name`   | string ✱ | Display name shown in the pack picker. |
| `pack_kind`   | string   | `"base"` or `"mod"`. Informational. |
| `title`       | string   | Window title. |
| `version`     | int      | Pack schema version. Current: `1`. |
| `world`       | object ✱ | Global world flags (see §3). |
| `time`        | object ✱ | Day/week/difficulty constants. |
| `economy`     | object ✱ | Costs, chest tables, scoring. |
| `tuning`      | object   | Spell multipliers, search cost, temp-death army (`temp_death`: `{"troop": id, "count": n}`; defaults: the cheapest-recruit-cost troop, 20). |
| `combat`      | object ✱ | Morale chart, number-name labels. |
| `controls`    | object   | Settings-menu rows. |
| `colors`      | object   | Difficulty-bar colors, minimap palette. |
| `audio`       | object   | Music track list, SFX paths. |
| `sprites`     | object ✱ | Texture-atlas paths (see §4). |
| `tile_codes`  | object ✱ | Map-character → terrain mapping. |
| `troops`      | array  ✱ | Troop catalog. |
| `spells`      | array  ✱ | Spell catalog. |
| `artifacts`   | array  ✱ | Artifact catalog. |
| `villains`    | array  ✱ | Villain catalog. |
| `classes`     | array  ✱ | Player-class catalog. |
| `castles`     | array  ✱ | Castle catalog. |
| `towns`       | array  ✱ | Town catalog. |
| `zones`       | array  ✱ | Continent / map definitions. |
| `spawn`       | object   | Per-continent monster-spawn tables. |
| `contract`    | object   | Contract cycle parameters. |
| `strings`     | object   | All user-visible text (see §5). |
| `credits`     | object   | Credits-screen lines. |
| `ending`      | object   | Victory cartoon parameters. |

---

## 3. Conventions

**IDs.** Catalog entries (troops, spells, castles, etc.) are referenced
by string id rather than array index. Ids are lowercase, snake_case, and
must be stable across pack versions if you want save compatibility.

**Coordinates.** Tile coordinates are `(x, y)` integers in zone-local
space. `(0, 0)` is the top-left tile of each zone's map. Maps are 64×64
in the base pack but each zone declares its own `width` and `height`.

**Asset paths.** All paths are relative to the pack root. Forward
slashes only.

**Optional fields.** Anything not marked `required` is optional. The
engine has a built-in fallback for every UI string (see §5) so a
minimal pack can omit `strings` entirely.

**Numbers.** All numeric fields are integers unless context indicates
otherwise.

---

## 4. Sprites and tiles

The `sprites` block points at PNG files. Each entry is either:

- A single path (`"path": "art/foo.png"`).
- A path + frame count for animated sprites (`{"path": "...", "frames": 4}`).

Tile images live under `art/tiles/` by convention. Each `tile_codes`
entry maps an ASCII character (used in `.dat` map files) to a tile
record:

```json
"tile_codes": {
  "G": { "name": "grass", "terrain": "grass", "blocks_foot": false, "art": "tiles/grass.png" },
  "F": { "name": "forest", "terrain": "forest", "blocks_foot": true, "art": "tiles/forest.png" }
}
```

`terrain` must be one of: `grass`, `forest`, `mountain`, `water`,
`desert`. `blocks_foot` and `is_bridge` are booleans that interact with
walkability (a non-blocking terrain or `is_bridge` lets the hero walk).

---

## 5. Strings and localization

User-visible text lives under `strings.<group>.<key>`. Examples:

- `strings.banners.*`: chest, town, dwelling, encounter dialogs.
- `strings.ui.*`: labels, prompts, exit hints.
- `strings.contract_view.*`: contract screen.
- `strings.win.*` / `strings.lose.*`, endings.

Most strings support `%TOKEN%` substitution (e.g. `%NAME%`, `%GOLD%`,
`%COUNT%`). Tokens are documented per-string in
`engine/include/resources.h` next to each field.

Translating a pack means replacing the string values; the keys, tokens,
and grammatical positions of substitutions stay the same.

If a `strings.*` key is missing, the engine falls back to a built-in
English default.

---

## 6. Maps

Map files under `maps/` are plain text:

```
# optional comment lines start with #
GGGGGGGGGFFFFFFGGGG
GGGGGGGGFFFFFFGGGGG
...
```

One character per tile, one row per line. Characters resolve through
`tile_codes` in `game.json`. Width/height come from the zone's
`width`/`height` fields (the engine validates the map matches).

Interactive objects (towns, castles, chests, signs, dwellings,
artifacts, foes, telecaves, navmaps, orbs) are **not** placed via map
characters. They live in the zone's JSON arrays and are stamped onto
the map at load time.

---

## 7. Palettes

A pack ships a 256-color VGA-style palette at
`palettes/<name>.bin`, exactly 768 bytes (256 × RGB). The first 16
entries are reserved for the standard named indices (black, dblue,
yellow, etc.); the rest are free for art.

---

## 8. Distribution

To distribute a pack:

1. Verify it loads in a development build by pointing the engine at the
   loose directory: `./openbounty --pack /path/to/my-pack`.
2. Zip the pack root into `<pack_id>.openbounty` (the file extension is
   what tells the engine to treat it as a pack).
3. Drop the `.openbounty` file into the user data directory (below).

**Discovery.** At startup the engine scans three roots in order, taking
the first match on a duplicate name (`engine/pack.c pack_discover`):

1. the current working directory,
2. the user data directory, flat, with no `packs/` subdirectory:
   - Linux: `$XDG_DATA_HOME/openbounty`, else `~/.local/share/openbounty`
   - macOS: `~/Library/Application Support/OpenBounty`
   - Windows: `%APPDATA%\OpenBounty`
3. `<directory containing the binary>/assets`.

Discovery matches `*.openbounty` archives **only**. A loose directory is
a perfectly valid pack and the engine loads it happily, but it is never
found by scanning, pass it explicitly with `--pack <path>`. If more than
one pack is discovered, the pack picker runs before character creation.

## 9. Validating a pack

`--validate-pack` runs the headless autoplay oracle over a range of
catalog worlds and reports, per seed, whether the pack is winnable at
all:

```sh
./openbounty --validate-pack            # the whole catalog, seeds 0..255
./openbounty --validate-pack 0 9        # seeds 0..9
./openbounty --validate-pack 7          # seed 7 only
./openbounty --validate-pack 0 9 --pack /path/to/my-pack
```

It prints one row per seed, verdict, objectives cleared, days, score,
moves, elapsed time, and, on a seed the oracle could not clear, the
first objective that blocked it and why. The closing row totals the run:
`PASS` only when every seed in the range solved. Exit status is 0 on
PASS, 1 on FAIL, 2 if the run could not be set up (an unreadable pack or
an unknown `--autoplay-hero` class).

The oracle plays a knight at Normal by default; `--autoplay-hero=<class>`
and `--autoplay-level=<easy|normal|hard|impossible>` change the class and
the day budget it validates against. A NOT-SOLVED row is not a proof that
the seed is unwinnable, see `AUTOPLAY-SPECS.md` (AP-016) for exactly
what the two verdicts claim.

---

## 10. Versioning

Pack schema version is declared by the top-level `version` field.
Current: `1`. The engine checks this on load. Breaking changes will
bump the version and the engine will refuse to load older packs until
they're migrated.

Within a single schema version, the engine guarantees backward
compatibility: new optional fields can be added to packs without
breaking older engine builds (older engines ignore unknown keys).
