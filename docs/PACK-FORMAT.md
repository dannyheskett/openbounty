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
| `render`      | object ✱ | Screen geometry: `mode`, tile size, viewport, `ui_scale`, optional fixed buffer (see §2.1). |
| `font`        | object   | A TrueType/OpenType font rasterised at load into the glyph cell (see §2.2). Absent: the bitmap strip in `sprites.font`. |
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

### 2.1 `render`

```json
"render": { "mode": "modern", "tile_w": 96, "tile_h": 96, "tiles_w": 7, "tiles_h": 5,
            "ui_scale": 2, "native_w": 832, "native_h": 540 }
```

`mode` is required: `"legacy"` is the 320 x 200 layout (48 x 34 tiles, 5 x 5
viewport, `ui_scale` 1, the other keys ignored); `"modern"` takes the tile
size, the viewport in tiles (odd on both axes) and `ui_scale`, which
multiplies the font and the chrome bands.

`native_w` / `native_h` (modern only, optional) fix the buffer size. Without
them the buffer follows the window and the viewport grows to fill it. With
them the screen is exactly that size, the viewport is exactly `tiles_w` x
`tiles_h`, and the space the viewport, the one-tile sidebar and the thinnest
chrome bands do not use is split between the left and right bands and between
the top and bottom bands, so the map stays centred. The window opens at 1x and
the Scale control cycles 1x, 2x, 3x, resizing the window to the buffer times
the scale; a window of any other size shows the buffer at the largest of those
that fits, letterboxed. The buffer must hold the viewport (the loader rejects
one that cannot). Rome: 832 x 540 with 7 x 5 tiles of 96 gives 32-pixel side
bands and 16-pixel top and bottom bands, the minimum for that viewport.

A modern pack that ships no `sprites.ui.chrome_overworld` gets its chrome
drawn in code: the gold lattice (`src/lattice.c`) fills the frame bands and
the bar under the status line, borders every HUD panel
(`sprites.ui.panel_frame` still has to name a colour to turn panel borders
on) and rings every window (prompts, dialogs, views, location menus).
`sprites.hud.bar_strip` is then unused too. A legacy pack, or one that ships
the bitmap, draws it as before.

### 2.2 `font`

```json
"font": { "file": "art/font/Cinzel-Bold.ttf", "size": 15, "caps": true,
          "license": "art/font/OFL-Cinzel.txt" }
```

Modern packs only. `file` is a `.ttf` or `.otf` inside the pack; `size` is
the requested pixel size (6..64; omit it for the largest that fits); `caps`
true draws every string in capitals; `license` is the licence text shipped
beside the font. The file and the licence are both in the art manifest, so
the archive carries them.

The shell rasterises the face at load with anti-aliasing and fits it to the
layout's glyph cell, `8 * ui_scale` square: the size steps down from the
requested one until the tallest and widest glyph ink fit the cell. The
advance is then the widest ink plus one pixel, capped at the cell, so a
narrow face packs tighter than the cell and lines only get shorter. Glyphs
are centred in their advance and share one baseline. The start-up log
reports the fitted size and advance. If the file fails to load the strip in
`sprites.font` is used instead. Legacy packs never read this block.

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

### 4.1 Animations

An animation is a JSON array of frame paths, and **the length of that array is
the cycle**. A pack ships as many frames as it has, up to 16; nothing is fixed
at four. This applies to `sprites.hero.*`, `sprites.hud.*_animation`,
`troops[].anim` and `villains[].anim`.

The hero's `walk`, `idle` and `boat` may instead be authored per facing:

```json
"hero": {
  "walk": { "south": ["..."], "east": ["..."], "west": ["..."], "north": ["..."] },
  "idle": ["art/sprites/hero_idle_00.png", "art/sprites/hero_idle_01.png"],
  "boat": ["art/sprites/boat_00.png", "art/sprites/boat_01.png"]
}
```

The two forms mean different things to the renderer:

- **Flat array** — one strip, mirrored horizontally when the hero faces west.
  Walking north or south shows the side-on view. This is how `kings-bounty` is
  authored.
- **Per-facing object** — the authored facing is drawn and the sprite is
  **never mirrored**, so an asymmetric figure keeps its shield on the correct
  arm walking west. Facings may be omitted individually; a missing one falls
  back to `south`.

Each facing carries its own frame count, so a six-frame walk east alongside a
four-frame walk north is legal.

`idle` is optional. With it, the hero animates while standing still. Without
it, he holds frame 0 between steps, which is what every pack did before `idle`
existed.

**Per-class hero art.** A class entry may carry its own `hero` block with the
same `walk` / `idle` / `boat` keys, plus `tile`, the win-cartoon hero tile:

```json
{ "id": "knight", "name": "Legatus", "portrait": "art/classes/legatus.png",
  "hero": { "tile": "art/classes/legatus_hero.png",
            "walk": ["art/classes/legatus_walk_00.png", "..."] }, ... }
```

The map and the win cartoon draw the chosen class's art when it is declared
and fall back to `sprites.hero` and `ending.hero_tile` for anything the class
leaves out, so packs that declare nothing are unchanged. A pack whose classes
all declare a hero tile may leave `ending.hero_tile` out, and a pack may leave
`ending.grass_tile` out: the cartoon then draws the map's `grass` tile
(`glory-of-rome` does both; `kings-bounty` declares both tiles).

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

A castle is stamped as a 3×2 block by default: its gate tile at `x, y`
plus five blocking wall tiles above and beside it, drawn with the
`castle_tl/br/tr/ml/mr` and `castle_gate` tile art. A catalog entry that
declares `"footprint": "1x1"` is stamped as the gate tile alone, drawn
with `art/tiles/castle.png`, the way a town is:

```json
{ "id": "capua", "name": "Capua", "x": 41, "y": 53, "zone": "italia",
  "difficulty_tier": 0, "footprint": "1x1" }
```

A pack only needs the castle art for the footprints it uses.

**Panel frame.** `sprites.ui.panel_frame` names a palette colour
(`YELLOW`, `GREY`, ... or a raw index) and the shell then draws a
one-design-pixel frame in that colour, with a darker inner line, round every
panel slot: the HUD panels, the inventory belt cells and the contract face.
Art for those slots is authored edge to edge with no frame of its own.
Absent, the shell draws nothing and the art carries its own frame, which is
how `kings-bounty` ships.

**Siege back wall.** `sprites.ui.siege_back_wall` names a cell-sized tile the
shell repeats across the band above the siege board, with
`siege_back_wall_left` / `_right` for the band's end cells; field tiles are
drawn beneath. Decorative, outside the grid, siege only; absent, nothing is
drawn.

**Combat ground.** `sprites.ui.combat_ground` is `"field"` (default) or
`"terrain"`. With `"terrain"` the shell draws the map tile the hero stands on
under every combat cell (grass, desert, ...; water falls back to grass) and the
pack ships no field tile: `sprites.combat[0]` is left out of the manifest.
`kings-bounty` declares nothing and draws its field tile as before.

**Siege grid.** `sprites.ui.siege_grid` names a path prefix for a full grid of
siege tiles, one file per cell: `<prefix>_<x>_<y>.png` for `x` in `0..5` and
`y` in `0..5`, row 0 the band above the board and rows 1..5 the board's rows
0..4 (36 files). In a siege the shell draws each cell's own tile as the ground
and nothing for the wall codes, since the walls are painted in the tiles; the
tiles may be any size and are scaled to the cell. When it is set the
`siege_back_wall*` keys are ignored and the per-code wall pieces,
`sprites.combat[5..10]`, leave the manifest, so the pack need not ship them. Draw-only: the castle layout still
blocks the wall cells. Absent, the per-code wall pieces draw as above
(`kings-bounty` declares none; `glory-of-rome` ships 36 cells at 64).

**Per-town art.** A town catalog entry may declare `"art": "<stem>"`, a
tile under `art/tiles/`, and the engine stamps that tile at the town's
position instead of the shared `art/tiles/town.png`. Absent means `town`, so
older packs are unchanged; the shared tile is only required while some town
still uses it, and the art manifest lists each declared stem once.

```json
{ "id": "massilia", "name": "Massilia", "art": "town_galliae", "x": 28, "y": 21,
  "zone": "galliae", ... }
```

**Per-zone wandering-army art.** A zone may declare
`"army_art": "<stem>"`, a tile under `art/tiles/`; every wandering foe in
that zone, declared or salted, then draws that tile instead of the shared
`art/tiles/wandering_army.png`. Absent means the shared tile, which is only
required while some zone still uses it.

**Per-zone terrain art.** A zone may declare `"tile_set": "<folder>"`. Every
`tile_codes` art name for that zone then resolves under
`art/tiles/<folder>/` instead of `art/tiles/`, so one `.dat` and one
`tile_codes` table serve every continent while each draws its own grass,
forest, water, edges and bridges. The folder must hold a file for every
`tile_codes` art the pack declares; the art manifest lists them, so
validation catches a missing one. Object tiles (towns, castles, chests,
signs, dwellings) are never affected. A zone without the key draws the
shared `art/tiles/` set, so packs that predate the key load unchanged, and
the shared set is only required while some zone still uses it.

```json
{ "id": "galliae", "name": "Galliae", "map": "maps/galliae.dat",
  "tile_set": "galliae", ... }
```

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
