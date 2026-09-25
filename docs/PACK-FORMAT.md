# Pack format

A *pack* has been a self-contained directory (or zipped `.openbounty`
archive) that supplies everything the engine needs to run one specific game:
gameplay rules, world data, text, art, audio, fonts, and palettes. The base
King's Bounty pack at `assets/kings-bounty/` has been the canonical
reference, and `assets/glory-of-rome/` the modern one.

Packs have been how OpenBounty supports re-themed games, total conversions,
and community content without recompiling the engine.

---

## 1. Directory layout

```
my-pack/
├── game.json          # All gameplay data + asset path manifest (required)
├── art/               # Sprites, tiles, fonts, UI chrome (PNG)
├── audio/             # Music + SFX (WAV / OGG)
├── maps/              # Zone tile-grid files (*.dat, ASCII)
├── palettes/          # 256-color palette binaries (768-byte raw RGB)
└── strings/           # All user-visible text, one file per language (en.json, ...)
```

The directory name has had no special meaning. The pack has identified
itself via `pack_id` inside `game.json`.

A packaged distribution has been a ZIP file with the `.openbounty` extension,
containing the same tree at the archive root. The engine has treated loose
directories and `.openbounty` archives interchangeably.

---

## 2. `game.json` top-level keys

All paths have been relative to the pack root. ✱ marks what a playable pack
has supplied. The loader itself has refused only a manifest it cannot open or
parse, a missing `render.mode`, a `font` block without a file or with a size
outside 6..64, a tile size or buffer it cannot lay out, a villain with an
invalid index, a bad `tuning.temp_death`, and missing string keys (§5); any
other absent block has parsed as empty or as its defaults.

| Key | Type | Purpose |
|---|---|---|
| `pack_id`     | string ✱ | Stable identifier (e.g. `"kings-bounty"`). Used as save-file partition key. |
| `pack_name`   | string ✱ | Display name shown in the pack picker. |
| `pack_kind`   | string   | `"base"` or `"mod"`. Informational. |
| `title`       | string   | Window title. |
| `version`     | int      | Pack schema version. Current: `1`. |
| `world`       | object ✱ | Global world flags, including `language`, the base locale in `strings/` (default `en`). |
| `time`        | object ✱ | Day/week/difficulty constants. |
| `economy`     | object ✱ | Costs, chest tables, scoring. |
| `tuning`      | object   | `instant_army_multiplier` (per rank), `search_cost_days`, and the temp-death army (`temp_death`: `{"troop": id, "count": n}`; defaults: the cheapest-recruit-cost troop, 20). |
| `combat`      | object ✱ | Morale chart, number-name labels. |
| `controls`    | object   | Settings-menu rows. |
| `colors`      | object   | Difficulty-bar colors, minimap palette. |
| `audio`       | object   | Music track list, SFX paths. |
| `render`      | object ✱ | Screen geometry: `mode`, tile size, viewport, `ui_scale`, optional fixed buffer (see §2.1). |
| `font`        | object   | A TrueType/OpenType font rasterised at load into the glyph cell (see §2.2). Absent: the bitmap strip in `sprites.font`. |
| `sprites`     | object ✱ | Texture-atlas paths (see §4). |
| `tile_codes`  | object ✱ | Map-character → terrain mapping. |
| `troops`      | array  ✱ | Troop catalog. Each: `id`, `name`, `sprite`, `portrait` (modern still, optional), `anim` (frames, §4.1), `skill_level`, `hit_points`, `move_rate`, `melee` `[min, max]`, `ranged` `[min, max, ammo]`, `recruit_cost`, `spoils_factor`, `abilities` (a `\|`-joined mask of `FLY`, `REGEN`, `MAGIC`, `IMMUNE`, `ABSORB`, `LEECH`, `SCYTHE`, `UNDEAD`), `dwelling` (`plains`, `forest`, `hill`, `dungeon` or `castle`), `max_population`, `growth_per_week`, `morale_group` (`A`..`E`), `tier_counts` (one foe-garrison count per continent tier). OPENBOUNTY-SPEC §13. |
| `spells`      | array  ✱ | Spell catalog, in engine order: seven combat spells then seven adventure spells. Each: `id`, `name`, `cost`, `kind` (`combat` or `adventure`). OPENBOUNTY-SPEC §19. |
| `artifacts`   | array  ✱ | Artifact catalog. Each: `id`, `name`, `icon`, `power` (`increased_damage`, `quarter_protection`, `double_leadership`, `increase_commission`, `double_spell_power`, `double_max_spells`, `cheaper_boat_rental`; any other string has no effect), `effect` (its one-line description), `puzzle_cell`, and its placement `zone` and `local_idx` (0 or 1). OPENBOUNTY-SPEC §20. |
| `villains`    | array  ✱ | Villain catalog. Each: `id`, `name`, `portrait`, optional `anim` (frames; absent, `<portrait-stem>_00..03`), `zone`, `reward`, `puzzle_cell`, `army` (entries of `troop` and `count`, the castle garrison). OPENBOUNTY-SPEC §21. |
| `classes`     | array  ✱ | Player-class catalog. Each: `id`, `name`, `portrait`, `starting_gold`, `starting_troops` (`id`, `count`), `ranks` (four, each `id`, `name`, `villains_needed`, `leadership`, `max_spells`, `spell_power`, `commission`, `knows_magic`, `instant_army`), and an optional `hero` block (`walk`, `idle`, `boat`, `tile`, `disgraced`; §4.1). OPENBOUNTY-SPEC §8. |
| `castles`     | array  ✱ | Castle catalog. Each: `id`, `name`, `zone`, `x`, `y`, `difficulty_tier` (0..3), optional `footprint` (`3x2` or `1x1`, §6) and `art`; the King's castle carries `special` (`flow`, `dialog`, `audience`, `win_condition`, the `excluded_from_contract` / `_intel` / `_siege` flags and, for the modern screens, `portrait`, `figure`, `promotion`, `barracks_portrait`, `barracks_figure`, `greeter_figure` naming `portraits` ids). OPENBOUNTY-SPEC §17. |
| `towns`       | array  ✱ | Town catalog. Each: `id`, `name`, `zone`, `x`, `y`, `gate` `{x, y}`, `boat` `{x, y}`, `intel_castle`, optional `intel_artifact` (the informant reports an artifact instead), optional `pinned_spell`, optional `art` and `backdrop` (§6), and the modern screen's `headman`, `informant`, `townhead` (`portraits` ids) and `invitations` (a `strings.town_invitations` block). OPENBOUNTY-SPEC §16. |
| `zones`       | array  ✱ | Continent / map definitions. Each: `id`, `name`, `map`, `width`, `height`, `hero_spawn` `{x, y}`, optional `home_spawn` and `is_home` (exactly one zone), optional `magic_alcove`, `neighbors` (zone ids reachable by sea), `salt` (§6), and the object lists `towns`, `castles`, `signs`, `chests`, `artifacts`, `dwellings`, `wandering_armies`; a modern pack has added `events`, `arrivals`, `alcove_art`, `alcove_cost`, `army_art`, `field_grid`, `town_backdrop`, `tile_set`, `tile_set_arts` and the `boatmaster`, `pontifex`, `siegemaster` portrait ids (§6). OPENBOUNTY-SPEC §9–§10. |
| `spawn`       | object   | Per-continent monster-spawn tables: `tier_chance_curve` (one threshold list per continent tier), `tier_troop_pool` (one troop list per dwelling kind, any length), and an optional `kind_chance_curve` (per kind, a curve set of its own or `null` to keep the tier curve; `glory-of-rome` uses it for its six-troop plains kind). |
| `contract`    | object   | Contract cycle parameters. |
| `audiences`   | object   | Modern home-castle audience pages (Promotion, Blessing, Tribute). |
| `magic`       | object   | `rites_per_zone`: each zone's temple has taught spells only once that zone's rites are known (OPENBOUNTY-SPEC REQ-314a). |
| `foes`        | object   | `evade_needs_free_square`: Evade has been offered only with a free square beside the hero, the hero's own parked boat counting as one (REQ-430o). |
| `portraits`   | array    | The people of the modern place screens: each an `id` and an `anim` list of frames, named by a town's `headman` / `informant` / `townhead`, a zone's `boatmaster` / `pontifex` / `siegemaster` and a castle's `special` block. |
| `credits`     | object   | Credits-screen lines. |
| `ending`      | object   | Victory cartoon parameters. |

### 2.1 `render`

```json
"render": { "mode": "modern", "tile_w": 96, "tile_h": 96, "tiles_w": 5, "tiles_h": 5,
            "ui_scale": 1, "dim": 30, "native_w": 800, "native_h": 504 }
```

`mode` has been required: `"legacy"` has been the 320 x 200 layout (48 x 34
tiles, 5 x 5 viewport, `ui_scale` 1, the other keys ignored); `"modern"` has
taken the tile size, the viewport in tiles (odd on both axes) and `ui_scale`,
which multiplies the chrome bands and the bitmap font. The viewport has also
been the fog cleared round the hero at every step (`FogRevealFor`).

`dim` (modern only, optional, default 55) has been the percent of black laid
once over the screen behind a floating page (OPENBOUNTY-SPEC REQ-430g,
DESIGN-SPEC DSGN-0036); 0 turns it off.

`native_w` / `native_h` (modern only, optional) have declared the buffer.
Without them the buffer has followed the window and the viewport has grown
to fill it. With them the declared size has been the smallest screen: across
it the frame, a one-tile column, a band, the map, a band, a one-tile column
and the frame; down it the frame, the map and the frame, with no status band
(`layout_init`, `src/layout.c`). The screen has been shown at the largest
whole scale the surface allows (3 at most), and what the surface has left
over at that scale has gone to the map; every page has kept the size it has
at the declared screen (DESIGN-SPEC DSGN-0003 to DSGN-0006, DSGN-0040). The
buffer has had to hold the viewport and the battlefield (the loader has
rejected one that cannot). Rome: 800 x 504 with 5 x 5 tiles of 96 is
12 + 96 + 4 + 576 + 4 + 96 + 12 across and 12 + 480 + 12 down.

A modern pack that ships no `sprites.ui.chrome_overworld` has had its chrome
drawn in code: the gold lattice (`src/lattice.c`) has filled the frame and the
bands beside the map, joined the column tiles and ringed every floating page.
`sprites.hud.bar_strip` has then been unused. A legacy pack, or one that
ships the bitmap, has drawn the bitmap as a nine-slice (ART-SPEC §3).

### 2.2 `font`

```json
"font": { "file": "art/font/PressStart2P-Regular.ttf", "size": 16, "caps": false,
          "license": "art/font/OFL-PressStart2P.txt" }
```

Modern packs only. `file` has been a `.ttf` or `.otf` inside the pack;
`size` the height of the line box in pixels, ascent plus descent, the meaning
raylib gives a font size (6..64, default 16; a pixel face such as Press Start
2P is crisp at whole multiples of its 8 px grid, 16 or 24); `caps` true has
drawn every string in capitals; `license` has been the licence text shipped
beside the font. The file and the licence have both been in the art
manifest, so the archive has carried them.

With this block the shell has drawn text from the face at `size`,
anti-aliased, in a FIXED cell: every glyph has advanced by the face's widest
advance and been centred in it, so the screens' column layouts hold. Declare
a monospaced face; a proportional one has been letter-spaced to its widest
glyph. Lines have been the face's line height. The layout has followed the
font rather than the other way round: a title strip holds one line and its
padding, and list rows are at least a line plus padding high. Word wrap has been by
pixel width and every authored newline has been kept, so menus and tables in
the strings hold their shape. At 2x and 3x the atlas has been rebuilt at that
zoom, so text has been sharp while art has stayed pixel-identical. If the file
fails to load, the strip in `sprites.font` has been used instead, in its
8 x 8 cell. Legacy packs have never read this block: they have kept the
strip, the cell and their character wrap exactly.

### 2.3 `controls`

```json
"controls": { "settings": [
  { "id": "delay", "label": "Delay", "type": "numeric", "range": 10, "default": 5 },
  { "id": "sounds", "label": "Sounds", "type": "bool", "default": 1, "audio": true },
  { "id": "cga", "label": "CGA", "type": "numeric", "range": 8, "default": 0, "hidden": true } ] }
```

Each setting has been a row of the Controls page, in order: `label` its
words, `type` `"bool"` (On and Off) or `"numeric"` (0 to `range` − 1),
`default` its first value. `hidden` true has kept the setting and left it off
the page. `audio` true has marked a sound setting, greyed when the machine has
had no audio device.

---

## 3. Conventions

**IDs.** Catalog entries (troops, spells, castles, etc.) have been
referenced by string id rather than array index. Ids have been lowercase,
snake_case, and stable across pack versions where save compatibility
matters.

**Coordinates.** Tile coordinates have been `(x, y)` integers in zone-local
space. `(0, 0)` has been the top-left tile of each zone's map. Maps have been
64×64 in the base pack, and each zone has declared its own `width` and
`height`.

**Asset paths.** All paths have been relative to the pack root. Forward
slashes only.

**Optional fields.** Anything not marked ✱ has been optional, except text:
the engine has carried no text of its own, so every string key has had to be
supplied (see §5).

**Numbers.** All numeric fields have been integers unless context indicates
otherwise.

---

## 4. Sprites and tiles

The `sprites` block has pointed at PNG files. Each entry has been either:

- A single path (`"path": "art/foo.png"`).
- An array of frame paths for an animated sprite (§4.1).

### 4.0a The columns and the combat commands

`sprites.rail` has named the tiles of the left column, each one map tile
square, edge to edge, because the shell has drawn the joins between them
(DESIGN-SPEC DSGN-0022, DSGN-0023): Menu, Map, Army and Search, then the
puzzle, which the shell has drawn from `sprites.hud.puzzle_grid` and
`sprites.ui.puzzle_cover`. `cast` has been the Cast command's tile in a
fight:

```json
"rail": { "menu": "art/ui/rail_menu.png", "map": "art/ui/rail_map.png",
          "army": "art/ui/rail_army.png", "search": "art/ui/rail_search.png",
          "cast": "art/ui/rail_cast.png" }
```

`sprites.hud.days` has been the right column's last tile, under the days
figure. `sprites.combat_panel` has named the Shoot, Wait and Fly tiles of the
command grid in a fight (DSGN-0113); Menu there has been `rail.menu`:

```json
"combat_panel": { "shoot": "art/ui/combat_shoot.png", "wait": "art/ui/combat_wait.png",
                  "fly": "art/ui/combat_fly.png" }
```

A tile the pack has not named has been left dark; the column has stood all
the same.

### 4.0b The `ui`, `hud` and `font` keys

`sprites.ui` has held the screens and their dressing, each a path unless
noted; every key has been optional and an absent one has drawn nothing or
borrowed as described:

- `splash_logo`, `splash_title`: the publisher and title splashes;
  `title_battle`, `title_eagle`, `title_words`: the modern title sequence
  (all three or none; otherwise the title is `splash_title`, still).
- `class_picker`, `class_highlight`, `class_picker_selected` (one per class,
  in catalog order): the class painting, its cursor glow and the painting
  with each figure picked out.
- `chrome_overworld`: a bitmap frame (absent, the modern lattice);
  `panel_frame`: a palette colour name for legacy's panel frames (§6).
- `puzzle_cover`: the chip over each puzzle piece still to be won;
  `view_icons_extra`: extra view icons, a list.
- `town_backdrop`, `castle_backdrop`, `plains_backdrop`, `forest_backdrop`,
  `hillcave_backdrop`, `dungeon_backdrop`: the place screens' pictures;
  `palace_welcome`, `palace_barracks`, `palace_throne`: the King's castle's
  three scenes (absent, the shared castle backdrop); `sail_backdrop`: the
  sail-to scene; `scene_column_capital`, `scene_column_shaft`,
  `scene_column_base`: the column pieces in the bars beside a place
  backdrop (absent, the lattice).
- `alcove_backdrop`, `alcove_figure`, `alcove_figure_animation` (frames),
  `alcove_figure_frame_ms` (a number; 0 = the screen's tick),
  `alcove_figure_place` (`x`, `y`, `w`, `h` in the backdrop's 240x102
  units), `alcove_portrait`: the temple and its keeper (absent, the hill
  cave's backdrop and the `gnomes` troop in the troop slot).
- `ending_win`, `ending_lose`: the ending pictures.
- `siege_back_wall`, `siege_back_wall_left`, `siege_back_wall_right`,
  `siege_grid`, `field_grid`, `combat_ground` (`field` or `terrain`): the
  fight's ground and walls (§6).

`sprites.hud` has held the right column and its readouts:
`contract_silhouette`, `siege_silhouette` with `siege_animation` (frames),
`magic_silhouette` with `magic_animation` (frames), `boat_silhouette` (the
town's Boat screen with no boat master), `gold_purse`, `days`,
`puzzle_grid` and legacy's `bar_strip`.

`sprites.font` has been the bitmap font strip (default
`art/font/kb-font.png`), `sprites.palette` the VGA palette binary (default
`palettes/palette.bin`), `sprites.combat` a list of the fight's fifteen
tile roles in fixed order (field, three obstacles, castle item, six walls,
four cursor frames; default the reference `art/combat/` names) and
`sprites.hero` the hero's sets (§4.1). Neither pack has declared `combat`
or `palette`.

### 4.1 Animations

An animation has been a JSON array of frame paths, and **the length of that
array has been the cycle**. A pack has shipped as many frames as it lists,
with no ceiling; nothing has been fixed at four. This has applied to
`sprites.hero.*`, `sprites.hud.*_animation`, `troops[].anim` and
`villains[].anim`. A villain without `anim` has fallen back to
`<stem>_00..03`.

The hero's `walk`, `idle` and `boat` have also been authorable per facing:

```json
"hero": {
  "walk": { "south": ["..."], "east": ["..."], "west": ["..."], "north": ["..."] },
  "idle": ["art/sprites/hero_idle_00.png", "art/sprites/hero_idle_01.png"],
  "boat": ["art/sprites/boat_00.png", "art/sprites/boat_01.png"]
}
```

The two forms have meant different things to the renderer:

- **Flat array** — one strip, mirrored horizontally when the hero faces west.
  Walking north or south has shown the side-on view. `kings-bounty` has been
  authored this way.
- **Per-facing object** — the authored facing has been drawn and the sprite
  has **never been mirrored**, so an asymmetric figure has kept its shield on
  the correct arm walking west. Facings have been individually optional; a
  missing one has fallen back to `south`.

Each facing has carried its own frame count, so a six-frame walk east
alongside a four-frame walk north has been legal.

`idle` has been optional. With it, the hero has animated while standing
still. Without it, he has held frame 0 between steps.

**Per-class hero art.** A class entry has been able to carry its own `hero`
block with the same `walk` / `idle` / `boat` keys, plus `tile`, the
win-cartoon hero tile:

```json
{ "id": "knight", "name": "Legatus", "portrait": "art/classes/legatus.png",
  "hero": { "tile": "art/classes/legatus_hero.png",
            "walk": ["art/classes/legatus_walk_00.png", "..."] }, ... }
```

The map and the win cartoon have drawn the chosen class's art when it is
declared and fallen back to `sprites.hero` and `ending.hero_tile` for
anything the class leaves out. A pack whose classes all declare a hero tile
has been able to leave `ending.hero_tile` out, and a pack has been able to
leave `ending.grass_tile` out: the cartoon has then drawn the map's `grass`
tile (`glory-of-rome` has done both; `kings-bounty` has declared both
tiles).

Tile images have lived under `art/tiles/` by convention. Each `tile_codes`
entry has mapped an ASCII character (used in `.dat` map files) to a tile
record:

```json
"tile_codes": {
  "G": { "name": "grass", "terrain": "grass", "blocks_foot": false, "art": "tiles/grass.png" },
  "F": { "name": "forest", "terrain": "forest", "blocks_foot": true, "art": "tiles/forest.png" }
}
```

`terrain` has had to be one of: `grass`, `forest`, `mountain`, `water`,
`desert`, `river`. River has been inland water: it has blocked walking and
boats alike, taken the bridge spell (the `bridge_river_*` pieces) and been
flown over. Several codes have been able to share a terrain with different
art: `glory-of-rome` has had a grass variant and twenty-four road pieces
(`road_*`) that are plain grass to the engine (OPENBOUNTY-SPEC REQ-229c). An
optional `variants` list (any number of art names, repeats allowed to weight
them) has given a code cosmetic alternates the shell picks per cell at draw
time (OPENBOUNTY-SPEC REQ-229d). `blocks_foot` and `is_bridge` have been
booleans that interact with walkability (a non-blocking terrain or
`is_bridge` lets the hero walk).

---

## 5. Strings and localization

User-visible text has lived in `strings/<lang>.json`, one file per language,
grouped by `<group>.<key>`. Examples:

- `banners.*`: chest, town, dwelling, encounter dialogs.
- `ui.*`: labels, prompts, exit hints.
- `contract_view.*`: contract screen.
- `win.*` / `lose.*`: endings.

The base language has been `world.language` (default `en`); `--lang <code>`
has loaded `strings/<code>.json` instead, falling back to the base file when
that one is absent.

Most strings have supported `%TOKEN%` substitution (e.g. `%NAME%`, `%GOLD%`,
`%COUNT%`). Tokens have been documented per-string in
`engine/include/resources.h` next to each field.

Translating a pack has meant adding a `strings/<code>.json` with the same
keys, tokens and grammatical positions of substitutions.

The engine has carried no text of its own: a pack missing any required key
has been refused at load, with every missing key printed.

A few keys have been optional, each read by modern screens only:

- `banners.chest_gold_title`, `chest_gold_found`, `chest_gold_take`,
  `chest_gold_share`: the treasure chest's title, its words and its two
  answers as rows (`chest_gold`'s A and B lines, apart). Absent, the chest has
  had no title and its rows have been A and B.
- `banners.gmr_spell_in_fight`, `gmr_spell_on_map`: why a spell cannot be
  cast here, on the spells page.

A class's description on the class picker has been
`banners.class_desc_<the class's id>`.

---

## 6. Maps

Map files under `maps/` are plain text:

```
# optional comment lines start with #
GGGGGGGGGFFFFFFGGGG
GGGGGGGGFFFFFFGGGGG
...
```

One character per tile, one row per line. Characters have resolved through
`tile_codes` in `game.json`. Width/height have come from the zone's
`width`/`height` fields (short rows pad with grass).

Interactive objects (towns, castles, chests, signs, dwellings, artifacts,
foes, telecaves, navmaps, orbs) have **not** been placed via map characters.
They have lived in the zone's JSON arrays and been stamped onto the map at
load time.

A castle has been stamped as a 3×2 block by default: its gate tile at `x, y`
plus five blocking wall tiles above and beside it, drawn with the
`castle_tl/br/tr/ml/mr` and `castle_gate` tile art. A catalog entry that
declares `"footprint": "1x1"` has been stamped as the gate tile alone, drawn
with `art/tiles/castle.png`, the way a town is:

```json
{ "id": "capua", "name": "Capua", "x": 41, "y": 53, "zone": "italia",
  "difficulty_tier": 0, "footprint": "1x1" }
```

A pack has needed castle art only for the footprints it uses.

**Panel frame.** `sprites.ui.panel_frame` has named a palette colour
(`YELLOW`, `GREY`, ... or a raw index) and a legacy screen has then drawn a
one-design-pixel frame in that colour, with a darker inner line, round every
panel slot: the HUD panels, the inventory belt cells and the contract face.
Art for those slots has been authored edge to edge with no frame of its own.
Absent, the shell has drawn nothing and the art has carried its own frame,
which is how `kings-bounty` has shipped. A modern screen has drawn no panel
frames: its columns have been joined by the lattice.

**Siege back wall.** `sprites.ui.siege_back_wall` has named a cell-sized
tile the shell repeats across the band above the siege board, with
`siege_back_wall_left` / `_right` for the band's end cells; field tiles have
been drawn beneath. Decorative, outside the grid, siege only; absent, nothing
has been drawn.

**Combat ground.** `sprites.ui.combat_ground` has been `"field"` (default)
or `"terrain"`. With `"terrain"` the shell has drawn the map tile the hero
stands on under every combat cell (grass, desert, ...; water falls back to
grass) and the pack has shipped no field tile: `sprites.combat[0]` has been
left out of the manifest. `kings-bounty` has declared nothing and drawn its
field tile.

**Siege grid.** `sprites.ui.siege_grid` has named a path prefix for a full
grid of siege tiles, one file per cell: `<prefix>_<x>_<y>.png` for `x` in
`0..5` and `y` in `0..5`, row 0 the band above the board and rows 1..5 the
board's rows 0..4 (36 files). In a siege the shell has drawn each cell's own
tile as the ground and nothing for the wall codes, since the walls are
painted in the tiles; the tiles have been free to be any size and have been
scaled to the cell. When it is set the `siege_back_wall*` keys have been
ignored and the per-code wall pieces, `sprites.combat[5..10]`, have left the
manifest, so the pack has not needed to ship them. Draw-only: the castle
layout has still blocked the wall cells. Absent, the per-code wall pieces
have drawn as above (`kings-bounty` has declared none; `glory-of-rome` has
shipped 36 cells at 32).

**Field grid.** `sprites.ui.field_grid` has named a path prefix for the
ground of an open-field fight, one file per board cell:
`<prefix>_<x>_<y>.png` for `x` in `0..5` and `y` in `0..4` (30 files), a
6 × 5 picture cut into 96 px cells (`tools/siegeslice.py --field` takes the
largest 6:5 rectangle of content centred in a picture, so a meadow painted on
white keeps its white out, scales it to 576 × 480 and cuts it). A zone has
been able to declare its
own `field_grid` prefix, which has won for fights on that zone, so each
continent can have its own ground. The shell has drawn each cell's own
picture under the obstacles and troops, in place of `combat_ground`, when
every cell of the zone's grid loaded; a siege has kept the siege grid.
Absent, `combat_ground` has applied. `glory-of-rome` has shipped Italia's
grid (`art/combat/field/italia_<x>_<y>.png`); `kings-bounty` has declared
none.

**Per-town art.** A town catalog entry has been able to declare
`"art": "<stem>"`, a tile under `art/tiles/`, and the engine has stamped that
tile at the town's position instead of the shared `art/tiles/town.png`.
Absent has meant `town`; the shared tile has been required only while some
town uses it, and the art manifest has listed each declared stem once.

```json
{ "id": "massilia", "name": "Massilia", "art": "town_galliae", "x": 28, "y": 21,
  "zone": "galliae", ... }
```

**Per-zone wandering-army art.** A zone has been able to declare
`"army_art": "<stem>"`, a tile under `art/tiles/`; every wandering foe in
that zone, declared or salted, has then drawn that tile instead of the shared
`art/tiles/wandering_army.png`. Absent has meant the shared tile, which has
been required only while some zone uses it.

**Per-zone terrain art.** A zone has been able to declare
`"tile_set": "<folder>"`. Every `tile_codes` art name for that zone has then
resolved under `art/tiles/<folder>/` instead of `art/tiles/`, so one `.dat`
and one `tile_codes` table have served every continent while each draws its
own grass, forest, water, edges and bridges. The folder has had to hold a
file for every `tile_codes` art the pack declares; the art manifest has
listed them, so validation has caught a missing one. Object tiles (towns,
castles, chests, signs, dwellings) have never been affected. A zone without
the key has drawn the shared `art/tiles/` set, which has been required only
while some zone uses it.

```json
{ "id": "galliae", "name": "Galliae", "map": "maps/galliae.dat",
  "tile_set": "galliae", ... }
```

**Overriding single tiles.** A zone has also been able to fork only some of
the master set: `"tile_set_arts"` has listed the art names its folder
overrides, one by one. Those names have drawn from `art/tiles/<folder>/`;
every other name has drawn from the master `art/tiles/` set, so the folder
has held only what differs. A cosmetic variant has been a name of its own and
has been forked by listing it. The art manifest has asked the folder for
exactly the listed files, and kept the master set for everything else.

```json
{ "id": "galliae", "tile_set": "galliae",
  "tile_set_arts": ["grass", "grass_variant", "grass_01", "forest", "forest_edge_01"] }
```

**Town backdrops.** A zone has been able to declare `"town_backdrop"`, and a
town its own `"backdrop"`, both 240x102. A town's own has won, then its
zone's, then the pack's `sprites.ui.town_backdrop`.

**A gate that demands one arm.** A static `wandering_armies` entry has been
able to carry `"requires_troop"` (a troop id), `"scene"` (240x102) and
`"title"`: the fight has been refused until that troop is in the army, and
the refusal has been drawn over the picture under that heading. A
`dwellings` entry has been able to carry `"troop"`, pinning what it breeds.

**A pinned purse.** A zone chest has been able to carry `"gold": N`: it has
then always held exactly that, instead of rolling.

**A fixed chest.** A zone chest has been able to carry `"fixed": true`: it
has stayed out of the salt barrel (OPENBOUNTY-SPEC REQ-231) and always been
a chest.

**An explicit garrison.** A `wandering_armies` entry has been able to carry
`army`, a list of `{"troop": id, "count": n}`, fielded verbatim instead of a
rolled garrison.

**The alcove per zone.** A zone has been able to declare `alcove_art`, the
tile stem its alcove is drawn with, and `alcove_cost`, its own fee in place
of `economy.alcove_cost` (`glory-of-rome` has charged 2500, 5000, 7500 and
10000 by zone).

**Telecaves** have been drawn with the dungeon dwelling's tile.

**The sailing scene.** A pack has been able to ship `sprites.ui.sail_backdrop`
(240x102, as every backdrop) and the string `body_navigate_confirm` ("Sail
for %ZONE%?"). With both, sailing to another zone has been drawn over that
picture: the provinces, then a confirmation. With neither, the plain list has
been used.

**One-time vistas.** A zone has been able to declare `events`: moments that
play once, when the hero steps onto their tile holding what they ask for. The
scene has been drawn full width (the image is 240x102, like every backdrop)
with a single Continue, and the effects have changed the map for good -- they
survive a zone switch and a save. `requires` has taken `spell`, `troop`,
`gold` or `artifact`, each with an optional `count` and `consume`; `effects`
have named a tile by its `tile_codes` key, or `{"reveal": true}` to uncover
the whole zone's fog. An optional `hint` has been what the place says, as a
note under the vista's title, each time the hero steps onto its tile while
something is still missing; without one the tile has stayed silent until the
vista fires.

```json
{ "id": "rubicon", "x": 31, "y": 31,
  "scene": "art/scenes/rubicon.png",
  "title": "The Rubicon", "body": "Your pontifex speaks the rite ...",
  "requires": [ { "spell": "bridge", "count": 1, "consume": true } ],
  "effects":  [ { "x": 30, "y": 30, "tile": "\\xcc" } ] }
```

**Arrivals.** A zone has been able to declare where a hero sailing in lands,
by the zone sailed from. A zone left out, or no `arrivals` at all, has landed
at `hero_spawn`. A water tile has arrived in the boat.

```json
{ "id": "africa", "hero_spawn": {"x": 24, "y": 2},
  "arrivals": { "italia": {"x": 26, "y": 3}, "oriens": {"x": 57, "y": 21} } }
```

---

## 7. Palettes

A pack has shipped a 256-color VGA-style palette at `palettes/<name>.bin`,
exactly 768 bytes (256 × RGB). The first 16 entries have been reserved for
the standard named indices (black, dblue, yellow, etc.); the rest have been
free for art.

---

## 8. Distribution

To distribute a pack:

1. Verify it loads in a development build by pointing the engine at the
   loose directory: `./openbounty --pack /path/to/my-pack`.
2. Zip the pack root into `<pack_id>.openbounty` (the file extension is
   what tells the engine to treat it as a pack).
3. Drop the `.openbounty` file into the user data directory (below).

**Discovery.** At startup the engine has scanned three roots in order,
taking the first match on a duplicate name (`engine/pack.c pack_discover`):

1. the current working directory,
2. the user data directory, flat, with no `packs/` subdirectory:
   - Linux: `$XDG_DATA_HOME/openbounty`, else `~/.local/share/openbounty`
   - macOS: `~/Library/Application Support/OpenBounty`
   - Windows: `%APPDATA%\OpenBounty`
3. `<directory containing the binary>/assets`.

Discovery has matched `*.openbounty` archives **only**. A loose directory has
been a perfectly valid pack and the engine has loaded it, but it has never
been found by scanning: pass it explicitly with `--pack <path>`, or by bare
name, which also finds `<name>/game.json` under each root. If more than one
pack is discovered, the pack picker has run before character creation; with
exactly one it has opened directly.

## 9. Validating a pack

`--validate-pack` has run the headless autoplay oracle over a range of
catalog worlds and reported, per seed, whether the pack is winnable at all:

```sh
./openbounty --validate-pack            # the whole catalog, seeds 0..255
./openbounty --validate-pack 0 9        # seeds 0..9
./openbounty --validate-pack 7          # seed 7 only
./openbounty --validate-pack 0 9 --pack /path/to/my-pack
```

`--pack` has named the pack; without it these modes have opened the loose
`assets/kings-bounty` tree of a source checkout, not a discovered pack.

It has printed one row per seed, verdict, objectives cleared, days, score,
moves, elapsed time, and, on a seed the oracle has not cleared, the first
objective that blocked it and why. The closing row has totalled the run:
`PASS` only when every seed in the range has solved. Exit status has been 0
on PASS, 1 on FAIL, 2 if the run has not been set up (an unreadable pack or
an unknown `--autoplay-hero` class).

The oracle has played a knight at Normal by default; `--autoplay-hero=<class>`
and `--autoplay-level=<easy|normal|hard|impossible>` have changed the class
and the day budget it validates against. A NOT-SOLVED row has not been a
proof that the seed is unwinnable; see `AUTOPLAY-SPECS.md` (AP-016) for
exactly what the two verdicts claim.

---

## 10. Versioning

Pack schema version has been declared by the top-level `version` field, `1`
in both shipped packs. A breaking change bumps it.

Within a single schema version, new optional fields have been addable to
packs without breaking older engine builds (older engines ignore unknown
keys).
