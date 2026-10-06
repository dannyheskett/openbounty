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
├── intro.json         # The Introduction's script, named by game.json (optional, §2.4)
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
invalid index (§2.9), a bad `tuning.temp_death`, missing string keys (§5,
counting the four required `world` strings of §2.5) and a malformed
Introduction script (§2.4); any other absent block has parsed as empty or as
its defaults.

| Key | Type | Purpose |
|---|---|---|
| `pack_id`     | string ✱ | Stable identifier (e.g. `"kings-bounty"`). Used as save-file partition key. |
| `pack_name`   | string ✱ | Display name shown in the pack picker. |
| `pack_kind`   | string   | `"base"` or `"mod"`. Informational. |
| `title`       | string   | Window title. |
| `version`     | int      | Pack schema version (`1`). |
| `world`       | object ✱ | Global world keys: four required strings, the base locale, the option defaults and two flags (§2.5). |
| `time`        | object ✱ | Day/week/difficulty constants (§2.6). |
| `economy`     | object ✱ | Costs, chest tables, scoring (§2.6). |
| `tuning`      | object   | `instant_army_multiplier` (up to four ints, per rank; default `[3, 2, 1, 1]`), `search_cost_days` (default 10), and the temp-death army (`temp_death`: `{"troop": id, "count": n}`; defaults: the cheapest-recruit-cost troop, 20). |
| `combat`      | object ✱ | Morale chart, number-name labels, `morale_as_army_view`, `field_obstacle_chance`, `guardian_full_band` (§2.7). |
| `controls`    | object   | Settings-menu rows (§2.3). |
| `colors`      | object   | Difficulty-bar colors, minimap palette (§2.7). |
| `audio`       | object   | Music tracks and the four short tunes (§2.8). |
| `render`      | object ✱ | Screen geometry: `mode`, tile size, viewport, `ui_scale`, optional fixed buffer (see §2.1). |
| `font`        | object   | A TrueType/OpenType font rasterised at load into the glyph cell (see §2.2). Absent: the bitmap strip in `sprites.font`. |
| `sprites`     | object ✱ | Texture-atlas paths (see §4). |
| `tile_codes`  | object ✱ | Map-character → terrain mapping (§4.2). |
| `troops`      | array  ✱ | Troop catalog. Each: `index` (§2.9), `id`, `name`, `sprite`, `portrait` (modern still, optional), `anim` (frames, §4.1), `skill_level`, `hit_points`, `move_rate`, `melee` `[min, max]`, `ranged` `[min, max, ammo]`, `recruit_cost`, `spoils_factor`, `abilities` (a `\|`-joined mask of `FLY`, `REGEN`, `MAGIC`, `IMMUNE`, `ABSORB`, `LEECH`, `SCYTHE`, `UNDEAD`), `dwelling` (singular: `plains`, `forest`, `hill`, `dungeon` or `castle`; a `castle` troop has been recruited only at the home castle and never hosted a salted dwelling), `max_population`, `growth_per_week`, `morale_group` (`A`..`E`, default `A`), `tier_counts` (up to four ints, one foe-garrison count per continent tier). OPENBOUNTY-SPEC §13. |
| `spells`      | array  ✱ | Spell catalog. Each: `index` (§2.9), `id`, `name`, `cost`, `kind` (`adventure`; any other value or none has meant `combat`), optional `description` (the modern spell pages' fallback when `spell_lore` / `spell_brief` lack the id). Combat spells have been bound by position and adventure spells by id (§2.9). OPENBOUNTY-SPEC §19. |
| `artifacts`   | array  ✱ | Artifact catalog. Each: `index` (§2.9), `id`, `name`, `icon`, `power` (`increased_damage`, `quarter_protection`, `double_leadership`, `increase_commission`, `double_spell_power`, `double_max_spells`, `cheaper_boat_rental`; any other string has had no effect), `effect` (its one-line description), `puzzle_cell` (default -1), and its placement `zone` and `local_idx` (0 or 1, default 0). OPENBOUNTY-SPEC §20. |
| `villains`    | array  ✱ | Villain catalog. Each: `index` (required, §2.9), `id`, `name`, `portrait`, optional `anim` (frames; absent, `<portrait-stem>_00..03`), `zone`, `reward`, `puzzle_cell`, `army` (entries of `troop` and `count`, the castle garrison). OPENBOUNTY-SPEC §21. |
| `classes`     | array  ✱ | Player-class catalog. Each: `index` (§2.9), `id`, `name`, `portrait`, `starting_gold`, `starting_troops` (`id`, `count`), `ranks` (four, each `id`, `name`, `villains_needed`, `leadership`, `max_spells`, `spell_power`, `commission`, `knows_magic`, `instant_army`), and an optional `hero` block (`walk`, `idle`, `boat`, `tile`, `disgraced`; §4.1). OPENBOUNTY-SPEC §8. |
| `castles`     | array  ✱ | Castle catalog. Each: `id`, `name`, `zone`, `x`, `y`, `difficulty_tier` (0..3, default 0), optional `gate` `{x, y}` (where the Castle Gate spell lands; default the tile below `x, y`), optional `footprint` (`3x2` or `1x1`, §6) and `art`; the King's castle carries `special` (`flow`, `dialog`, `audience`, `win_condition`, the `excluded_from_contract` / `_intel` / `_siege` flags and, for the modern screens, `portrait`, `figure`, `promotion`, `barracks_portrait`, `barracks_figure`, `greeter_figure` naming `portraits` ids). OPENBOUNTY-SPEC §17. |
| `towns`       | array  ✱ | Town catalog. Each: `id`, `name`, `zone`, `x`, `y`, `gate` `{x, y}` (where the Town Gate spell lands; absent, the town's own tile), `boat` `{x, y}` (where a rented boat appears), `intel_castle`, optional `intel_artifact` (the informant has reported an artifact instead), optional `pinned_spell`, optional `art` and `backdrop` (§6), and the modern screen's `headman`, `informant`, `townhead` (`portraits` ids) and `invitations` (a `strings.town_invitations` block). OPENBOUNTY-SPEC §16. |
| `zones`       | array  ✱ | Continent / map definitions. Each: `id`, `name`, `map`, `width` and `height` (default 64), `hero_spawn` `{x, y}` (default 0, 0), optional `home_spawn` and `is_home` (exactly one zone), optional `magic_alcove` `{x, y}`, `neighbors` (zone ids reachable by sea), `salt` (§6), and the object lists `towns` (`id`s of `towns` entries; an unknown id has been skipped and logged), `castles` (`id`, `x`, `y`, optional `decorations`, §6), `signs` (`id`, `x`, `y`, `title`, `body`), `chests` (`id`, `x`, `y`, §6), `artifacts` (`id`, `x`, `y`), `dwellings` (`id`, `x`, `y`, `kind`, optional `troop`, §6), `wandering_armies` (`id`, `x`, `y`, `static`, §6); a modern pack has added `events`, `arrivals`, `alcove_art`, `alcove_cost`, `army_art`, `field_grid`, `town_backdrop`, `treasure_scene`, `tile_set`, `tile_set_arts` and the `boatmaster`, `pontifex`, `siegemaster` portrait ids (§6). OPENBOUNTY-SPEC §9–§10. |
| `spawn`       | object   | Per-continent monster-spawn tables: `tier_chance_curve` (one threshold list per continent tier), `tier_troop_pool` (one troop list per dwelling kind, any length), the calm start `calm_radius` / `calm_max_slot` / `calm_max_stacks` (hostile foes within the radius of the zone's `hero_spawn` have rolled no pool slot above the max and no more stacks than the cap; radius 0 or absent has disabled it, REQ-283), and an optional `kind_chance_curve` (per kind, a curve set of its own or `null` to keep the tier curve; `glory-of-rome` has used it for its six-troop plains kind). |
| `contract`    | object   | Contract cycle parameters (§2.6). |
| `audiences`   | object   | Modern home-castle audience pages (Promotion, Blessing, Tribute) (§2.6). |
| `magic`       | object   | `rites_per_zone`: each zone's temple has taught spells only once that zone's rites are known (OPENBOUNTY-SPEC REQ-314a). `max_per_spell`: `max_spells` has capped each spell's charges, not all of them together, and no discard has been offered (REQ-321, REQ-540). `weekly_renewal`: each week end one spell learned at a temple has been filled to the limit (REQ-540). |
| `foes`        | object   | `evade_needs_free_square`: Evade has been offered only with a free square beside the hero, the hero's own parked boat counting as one (REQ-430o). |
| `portraits`   | array    | The people of the modern place screens: each an `id` and an `anim` list of frames, named by a town's `headman` / `informant` / `townhead`, a zone's `boatmaster` / `pontifex` / `siegemaster` and a castle's `special` block. |
| `credits`     | object   | Credits-screen lines (§2.8). |
| `ending`      | object   | Victory cartoon parameters (§2.8). |
| `intro`       | string   | The Introduction's script file (§2.4). Absent: the pack has no Introduction and the title menu no row for it. |

### 2.1 `render`

```json
"render": { "mode": "modern", "tile_w": 96, "tile_h": 96, "tiles_w": 5, "tiles_h": 5,
            "ui_scale": 1, "dim": 30, "native_w": 800, "native_h": 504 }
```

`mode` has been required: `"legacy"` has been the 320 x 200 layout (48 x 34
tiles, 5 x 5 viewport, `ui_scale` 1, the other keys ignored); `"modern"` has
taken the tile size, the viewport in tiles (odd on both axes) and `ui_scale`,
which has multiplied the chrome bands and the bitmap font (modern defaults:
`tile_w` / `tile_h` 96, `tiles_w` / `tiles_h` 7, `ui_scale` 1). The loader
has refused a viewport that is even or under 3 on either axis, a tile under
8 px or a `ui_scale` outside 1..8. The viewport has also
been the fog cleared round the hero at every step (`FogRevealFor`).

`dim` (modern only, optional, default 55) has been the percent of black laid
once over the screen behind a floating page (OPENBOUNTY-SPEC REQ-430g,
DESIGN-SPEC DSGN-0036); 0 has turned it off.

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
2P has been crisp at whole multiples of its 8 px grid, 16 or 24); `caps` true has
drawn every string in capitals; `license` has been the licence text shipped
beside the font. The file and the licence have both been in the art
manifest, so the archive has carried them.

With this block the shell has drawn text from the face at `size`,
anti-aliased, in a FIXED cell: every glyph has advanced by the face's widest
advance and been centred in it, so the screens' column layouts have held. Declare
a monospaced face; a proportional one has been letter-spaced to its widest
glyph. Lines have been the face's line height. The layout has followed the
font rather than the other way round: a title strip has held one line and its
padding, and list rows have been at least a line plus padding high. Word wrap has been by
pixel width and every authored newline has been kept, so menus and tables in
the strings have held their shape. At 2x and 3x the atlas has been rebuilt at that
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

Each setting has been a row of the Controls page, in order, at most eight:
`label` its words, `type` `"bool"` (On and Off; the default) or `"numeric"`
(0 to `range` − 1; `range` default 2). `hidden` true has kept the setting and
left it off the page. `audio` true has marked a sound setting, greyed when the
machine has had no audio device.

A row has been bound to the option at its position, not by `id`: row 0 has
stepped option 0 (delay), row 1 option 1 (sounds), and so on through walk
beep, animation, CGA, music and volume. Its first value has been
`world.default_options` at that position (§2.5); `id` and `default` have been
read and have had no effect.

### 2.4 `intro`

The Introduction has been an animated opening, played only from the modern
title menu's Introduction row (OPENBOUNTY-SPEC REQ-430u). `game.json`'s
`"intro"` has named its script, a file of its own because it is long and
edited apart from the gameplay data:

```json
{
  "frame": [240, 102], "type_cps": 28, "read_cps": 14, "min_hold": 2.0,
  "scenes": [
    { "id": "forum", "fade_in": 1.5, "fade_out": 1.0, "beats": [
      { "backdrop": "art/ui/backdrop_town_italia.png", "say": "forum_1", "face": "informant_market",
        "actors": [ { "portrait": "palace_usher", "at": [130, 6] } ] },
      { "for_each": "villain", "count": 4, "backdrop": "%ZONE_SCENE%",
        "actors": [ { "villain": "*", "at": [72, 3] } ], "say": "wanted", "face": "informant_market" }
    ]}
  ]
}
```

- **Top level.** `frame` has been the picture window in art pixels (1..256 a
  side; default 240×102, a backdrop's size). `type_cps` (default 28) has been
  the caption's typing speed in characters a second and `read_cps` (default
  14) the reading speed that sizes its hold, never shorter than `min_hold`
  seconds (default 2.0). Beyond these and `scenes`, no top-level key has
  been read.
- **Scenes** have played in order, each faded in from black over `fade_in`
  seconds and out over `fade_out` (defaults 1.0), the fades inside the
  scene's length. `id` has named the scene in logs and captures.
- **Beats** have played end to end on one timeline. Each has had:
  - `backdrop`: the picture behind the beat, or none for black;
  - `pan`: `[[x0, y0], [x1, y1]]`, the frame window's top-left in the
    backdrop moving across the beat (a backdrop larger than `frame` has been
    needed to move);
  - `ease`: `"linear"` (the default) or `"smooth"`, for the pan and the moves;
  - `dissolve`: seconds of cross-fade from the previous beat (default 0, a cut);
  - `actors`: sprites drawn in order, each with exactly one of `frames` (a
    list of paths), `portrait` (a `portraits` id) or `villain` (a villain id,
    or `"*"` on a `for_each` beat), plus `fps` (default 6.67), `at` and an optional `to`
    (the sprite's top-left in backdrop pixels), `mirror`, `start` and `end`
    (seconds into the beat the sprite is on screen, default the whole beat;
    the move from `at` to `to` has run across them) and `loop` (default true;
    false has played the frames once and held the last, for a single action),
    and `fade_in` / `fade_out` (seconds the sprite fades up after its
    `start` and away before its `end`), and `crop` (`[x, y, w, h]`, the part
    of each frame shown, for a portrait's head and shoulders);
  - `weather`: `"rain"`, streaks drawn over the picture, and `flashes`: the
    seconds into the beat of each lightning flash;
  - `sounds`: sound effects, each a `file` (a .wav in the pack: iOS has decoded
    only WAV), `at` (seconds into the beat it starts, default 0) and `gain`
    (0..1, default 1). Each has started as the timeline passed it, played to
    its end across later beats, and stopped when the intro ended;
  - `say`: a key in the strings' `intro` group, the caption typed on under
    the picture, and `face`: a `portraits` id, the speaker's loop beside it;
  - `card`: a key in the same group, text set in the middle of the picture;
  - `duration`: seconds. Absent, a beat with a caption has lasted its typing
    time plus its reading time; a beat with neither has been an error.
- **`for_each: "villain"`** has repeated the beat once per villain in catalog
  order, from catalog position `from` (default 0) for `count` villains
  (default all). Its text and `backdrop` have taken `%NAME%`, `%ALIAS%` (from
  `villain_descriptions`), `%REWARD%`, `%ZONE%` (the zone's name) and
  `%ZONE_SCENE%` (the zone's `treasure_scene`). Every beat has taken
  `%DAYS%`, the normal difficulty's day budget.

The music has had no cues: `audio.tracks.intro` has started with the first
beat and faded with the last scene. Sound effects have been cued per beat
(`sounds`); like the theme, they have ignored the Sounds option.

The loader has resolved the script at load, and refused the pack when a
caption or card key has been missing from `intro`, when `ui.title_intro` has
been missing, or when a beat has named an unknown portrait or villain, given an
actor no source or two, an empty `frames` list, a crop without a positive size
or a time on screen outside the beat, had neither `duration` nor `say` or a
`duration` not above 0, named a weather other than rain or a `for_each` other
than villain, given `sounds` that is not a list, a sound no file, a start
outside its beat or a gain outside 0..1; or when the script has not been
readable, has had no scenes or no beats, a scene with no beats or fades longer
than the scene, a `type_cps` or `read_cps` not above 0, or a frame out of
range. Its
art has been listed in the art manifest (§9) like every other path.

### 2.5 `world`

| Key | Type | Default | Effect |
|---|---|---|---|
| `starting_zone`    | string | required | The zone the hero has started in, at its `hero_spawn`, when no zone is `is_home`; under `magic.rites_per_zone` its rites have been known from the start. |
| `zone_noun`        | string | required | Read and stored; no screen has drawn it. |
| `zone_noun_plural` | string | required | Read and stored; no screen has drawn it. |
| `default_name`     | string | required | The hero's name when the player leaves it empty. |
| `language`         | string | `en`     | The base locale, `strings/<language>.json` (§5). |
| `default_options`  | int array | `[4, 1, 1, 1, 1, 0, 5]` | The first values of the seven options, in order delay, sounds, walk beep, animation, CGA, music, volume; a shorter list has kept the default for the rest. The Controls rows have stepped them by position (§2.3). |
| `max_army_slots`   | int    | 5        | Read and stored; the army has held five stacks whatever it says. |
| `clear_keeps_ground` | bool | false    | A cleared object has restored walkable desert ground instead of plain grass (REQ-229f). |
| `castle_gate_report` | bool | false    | A castle gate has shown the town informant's report on its castle, in a message box without siege weapons and above the siege question (string `castle_siege_ask`) with them (REQ-303). |

A missing required string has been counted with the missing string keys of
§5, and the pack refused.

### 2.6 `time`, `economy`, `contract`, `audiences`

Every key has been optional; the defaults have been King's Bounty's.

| Key | Type | Default | Effect |
|---|---|---|---|
| `time.day_steps`   | int | 40 | Steps in a day. |
| `time.week_days`   | int | 5  | Days in a week. |
| `time.days_per_difficulty` | object | `easy` 900, `normal` 600, `hard` 400, `impossible` 200 | The day budget per difficulty. |
| `economy.alcove_cost`      | int | 5000 | The temple's fee, unless the zone sets `alcove_cost` (§6). |
| `economy.boat_cost_normal` | int | 500  | A boat's rent, paid on hiring and each week after. |
| `economy.boat_cost_cheap`  | int | 100  | The rent with a `cheaper_boat_rental` artifact. |
| `economy.siege_cost`       | int | 3000 | The price of siege weapons. |
| `economy.unpaid_troops_leave` | bool | false | Stacks the week's gold cannot pay have left (string `banners.week_troops_left`). |
| `economy.chest` | object | below | The treasure chest's roll, each key a list of four ints, one per zone (by catalog position; a fifth zone onward has used the first entry). A roll of 1..100 below `chance_gold` has given gold, (a roll of 1..`gold_max`, plus `gold_min`) × 100; below `chance_commission` weekly commission, a roll of 1..`commission_max` plus `commission_min`; below `chance_spell_power` one spell power; below `chance_max_spells` `max_spells_base` more spell capacity; below `chance_new_spell` a spell; otherwise nothing. Defaults: `chance_gold` 61, 66, 76, 71; `chance_commission` 81, 86, 86, 81; `chance_spell_power` 83, 89, 89, 86; `chance_max_spells` 86, 92, 93, 91; `chance_new_spell` 101 each; `gold_min` 0, 4, 9, 19; `gold_max` 5, 16, 21, 31; `commission_min` 9, 49, 99, 199; `commission_max` 41, 51, 101, 301; `max_spells_base` 1, 1, 2, 2. |
| `economy.scoring` | object | below | The score: `per_villain` (500) × villains caught + `per_artifact` (250) × artifacts found + `per_castle` (100) × castles held − `kill_penalty` (1) × followers killed, floored at 0. `easy_halves` (true) has halved it on Easy; otherwise `difficulty_multiplier` (five ints, default `[0, 1, 2, 4, 8]`) entry 1, 2 or 3 has multiplied it on Normal, Hard or Impossible, and Easy has kept it unchanged. |
| `contract.cycle_length` | int | 5 | Villains in the contract cycle (at least 1), seeded with the first ones in catalog order. |
| `contract.initial_last_contract` | int | 4 | The cycle position counted as last offered at the start; each later contract has taken the next position, wrapping to 0. |
| `audiences` | object | absent | Present, the modern home castle has offered its Blessing and Tribute audiences. |
| `audiences.blessing_leadership_pct` | int | 50 | The Blessing's leadership gain, percent of base leadership. |
| `audiences.tribute_cost` | int | 50000 | The Tribute's price in gold. |
| `audiences.tribute_leadership_pct` | int | 25 | The Tribute's leadership gain, percent. |
| `audiences.tribute_magic_pct` | int | 25 | The Tribute's spell power and spell capacity gain, percent. |

### 2.7 `combat` and `colors`

| Key | Type | Default | Effect |
|---|---|---|---|
| `combat.morale_chart` | 5 × 5 array of strings | all `"N"` | Row the unit's `morale_group`, column the other group (`A`..`E`); each cell `N` (normal), `L` (low) or `H` (high). Any other cell has kept `N`. |
| `combat.number_names` | array | empty | `{"min": n, "label": "..."}` entries (`min` default 1), highest first: a stack's size has taken the first label whose `min` it reaches. |
| `combat.morale_as_army_view` | bool | false | A unit's combat morale has followed the army view's rule (REQ-271) instead of the behaviour ported from King's Bounty (REQ-385). Glory of Rome has set it. |
| `combat.field_obstacle_chance` | int 0..100 | 10 | The percent chance that each cell of an open-field battle's middle three columns holds an obstacle; a value outside 0..100 has kept 10. Glory of Rome has set 12. |
| `combat.guardian_full_band` | bool | false | A fixed guardian (a zone army with `static`) has fielded all five of its stacks on the open field instead of the first three. Glory of Rome has set it. |
| `colors.minimap_terrain` | object | `grass` `#00AA00`, `forest` `#55FF55`, `mountain` `#AA5500`, `water` `#5555FF`, `desert` `#FFFF55`, `fog` `#000000` | The minimap's colour per terrain. |
| `colors.difficulty_bar` | object | `easy` `#00AAAA`, `normal` `#AA0000`, `hard` `#5555FF`, `impossible` `#AA00AA` | The difficulty bar's colours. |

A colour has been `"#RRGGBB"` or `"#AARRGGBB"`; a malformed one has kept the
default.

### 2.8 `audio`, `ending`, `credits`

| Key | Type | Default | Effect |
|---|---|---|---|
| `audio.tracks.openworld`, `.combat`, `.intro` | path | none | The map's music, the fight's, and the Introduction's theme (§2.4). |
| `audio.tunes.walk`, `.bump`, `.chest`, `.defeat` | path (WAV) | none | The short tunes: a step, a blocked step, a chest, a defeat. An absent or unreadable one has stayed silent. |
| `ending.grass_tile`, `carpet_tile`, `hero_tile` | path | none | The win cartoon's tiles (§4.1 for the grass and hero fallbacks). |
| `ending.throne_backdrop` | path | none | Loaded; no screen has drawn it. |
| `ending.grid_width` / `grid_height` | int | 6 / 5 | The cartoon's grid in tiles. |
| `ending.carpet_column` / `carpet_length` | int | 4 / 5 | The carpet's column and its length in tiles. |
| `ending.frame_count` / `ticks_per_step` | int | 10 / 2 | The cartoon's steps and the ticks between them. |
| `ending.troop_border` | bool | true | Every catalog troop has been drawn round the cartoon's edge. |
| `credits.image` | path | none | The picture on the credits screen. |
| `credits.groups` | array | empty | `{"label": "...", "names": ["..."]}`: each heading and its names. |
| `credits.copyright` | array of strings | empty | The copyright lines at the foot. |

### 2.9 Catalog positions and `index`

Every troop, spell, artifact, villain and class has carried an `index`, and
the engine has used it as the entry's position in its catalog (combat units,
spell charges, artifacts found, class art). It has had to equal the entry's
zero-based position. The loader has checked only the villains': each has had
to be unique and in 0..n−1, or the pack has been refused; the others have
been read unchecked. Towns and castles have also been read with an `index`,
which no rule has consulted.

**Combat spells by position.** A combat spell's effect has been bound to its
catalog position, not its id: 0 Clone, 1 Teleport, 2 Fireball, 3 Lightning,
4 Freeze, 5 Resurrect, 6 Turn Undead. A spell at position 7 or beyond has not
been castable in a fight. **Adventure spells by id.** An adventure spell
(`kind: "adventure"`) has been bound by its id, at any position: `bridge`,
`time_stop`, `find_villain`, `castle_gate`, `town_gate`, `instant_army`,
`raise_control`; any other id has had no effect. Both packs have listed the
seven combat spells first, then the seven adventure spells.

---

## 3. Conventions

**IDs.** Catalog entries (troops, spells, castles, etc.) have been
referenced by string id rather than array index, apart from the positions of
§2.9 and the salt's `dwelling_range`. Ids have been lowercase,
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
(DESIGN-SPEC DSGN-0022, DSGN-0023): Menu, Map, Goto and Army, then the
puzzle, which the shell has drawn from `sprites.hud.puzzle_grid` and
`sprites.ui.puzzle_cover`. `goto` has been optional: without it the Goto tile
has been the `rail_goto` string on a dark card. `cast` has been the Cast
command's tile in a fight:

```json
"rail": { "menu": "art/ui/rail_menu.png", "map": "art/ui/rail_map.png",
          "goto": "art/ui/rail_goto.png", "army": "art/ui/rail_army.png",
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
  (all three or none; otherwise the title has been `splash_title`, still).
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
- `orb`: loaded and listed in the art manifest; no screen has drawn it.
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

### 4.2 `tile_codes`

Each `tile_codes` entry has mapped one byte of a `.dat` map file to a tile
record. The key has been that byte itself for a printable ASCII character,
or a four-character escape `\xNN` (two hex digits; written `"\\xNN"` in the
JSON) for any of the 256 codes, so that `game.json` has stayed plain ASCII; a
map file that uses one has been latin-1. Any other key has been ignored.

```json
"tile_codes": {
  "G":      { "art": "grass", "terrain": "grass" },
  "F":      { "art": "forest", "terrain": "forest", "blocks_foot": true },
  "\\xd0":  { "art": "pharos", "ground": "grass", "terrain": "grass" }
}
```

`art` has been a bare tile name, resolved to `art/tiles/<art>.png` (or the
zone's `tile_set` folder, §6). `ground` (optional) has named a tile drawn
under `art`, the way an object stands on its terrain (a landmark such as a
lighthouse); absent, the code has had no ground of its own.
`terrain` has been one of `grass` (also the default, and what an unknown name
has meant), `forest`, `mountain`, `water`, `desert`, `river`. A map byte with
no entry has failed that zone's map load. River has been inland water: it has blocked walking and
boats alike, taken the bridge spell (the `bridge_river_*` pieces) and been
flown over. Several codes have been able to share a terrain with different
art: `glory-of-rome` has had a grass variant and twenty-four road pieces
(`road_*`) that are plain grass to the engine (OPENBOUNTY-SPEC REQ-229c). An
optional `variants` list (any number of art names, repeats allowed to weight
them) has given a code cosmetic alternates the shell picks per cell at draw
time (OPENBOUNTY-SPEC REQ-229d). `blocks_foot` and `is_bridge` (default
false) have been booleans that interact with walkability (a non-blocking
terrain or `is_bridge` has let the hero walk).

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

The groups of required keys (`banners`, `combat_log`, `ui`, `menu` with its
`items`, `stats`, `army_view`, `morale`, `startup`, `controls`, `prompts`,
`toasts`, `contract_view`, `spells_view`, `dialog_titles`) have been required
too: a pack missing one has been refused, the group named.

These tables have been optional; an absent one, or an absent entry, has
left that text empty or let the screen fall back:

| Table | Shape | Use |
|---|---|---|
| `win`, `lose` | `header`, `body`, `footer` | The ending texts. |
| `villain_descriptions` | villain id → `alias`, `features`, `crimes` | The contract page; `%ALIAS%` in the Introduction. |
| `town_invitations` | block id → `contracts`, `boat`, `information`, `temple`, `siege` | A modern town's rows, named by the town's `invitations`. |
| `town_docks` | town id → string | A line under the modern town's Boat row. |
| `spell_lore`, `spell_brief` | spell id → string | The modern spell pages (else the spell's `description`). |
| `count_buckets` | `army_view`, `instant_army`: lists of `{"max": n, "label": "..."}` (`max` default unbounded) | The words for a troop count. |
| `difficulty` | `easy` .. `impossible` → `label`, `score_mult` | The difficulty table's words. |
| `keybinds` | list of `{"key": "...", "label": "..."}` | The key-help list. |

A few keys have been optional, each read by modern screens only:

- `banners.chest_gold_title`, `chest_gold_found`, `chest_gold_take`,
  `chest_gold_share`: the treasure chest's title, its words and its two
  answers as rows (`chest_gold`'s A and B lines, apart). Absent, the chest has
  had no title and its rows have been A and B.
- `banners.gmr_spell_in_fight`, `gmr_spell_on_map`: why a spell cannot be
  cast here, on the spells page.
- `banners.goto_title`, `goto_to`, `goto_today`, `goto_days`,
  `goto_no_route`, `goto_go`, `goto_cancel`, `rail_goto`, `gm_goto` and
  `gmd_goto`: Goto's page, its rows, the rail's label without an icon and the
  game menu's row (OPENBOUNTY-SPEC REQ-541). Without `gm_goto` the game menu
  has had no Goto row.
- `ui.title_intro` and the `intro` group: the Introduction's title-menu row
  and its captions (§2.4), required of a pack that names an `intro`.

These more have been optional in every mode, each for a pack rule or picture
that King's Bounty does not set:

- `banners.signpost_header`: a sign's title as its message's header.
- `banners.castle_gate_owner` and `banners.castle_siege_ask`: the castle
  gate's report and the siege question under it (`world.castle_gate_report`).
- `banners.foe_requires_troop`: the refusal of a foe with `requires_troop` (§6).
- `banners.body_navigate_confirm`: the sailing question (§6, the sailing scene).
- `combat_log.melee_no_kill` (`%ATK%`, `%TGT%`): the log line for a melee
  blow that kills none. With it set, every attack has been logged: a ranged
  blow that kills none has logged `ranged_no_effect`.

- `banners.week_troops_left` (`%TROOPS%`): the stacks that have left unpaid
  (`economy.unpaid_troops_leave`).
- `banners.week_spell_renewed` (`%SPELL%`): the week's renewed spell, under
  the week's creature (`magic.weekly_renewal`).
- `banners.spell_combat_only` (`%SPELL%`): a combat spell chosen on the map
  (`magic.max_per_spell`); absent, `spell_unavailable`.

A class's description on the class picker has been
`banners.class_desc_<the class's id>`.

---

## 6. Maps

Map files under `maps/` have been plain text:

```
# optional comment lines start with #, before the first row
GGGGGGGGGFFFFFFGGGG
GGGGGGGGFFFFFFGGGGG
...
```

One byte per tile, one row per line. Bytes have resolved through
`tile_codes` in `game.json` (§4.2); a byte with no entry has failed the
zone's map load. Width/height have come from the zone's `width`/`height`
fields: short rows and missing rows have padded with grass, and bytes past
the width have been ignored.

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

**Castle decorations.** A zone's `castles` entry has been able to carry
`decorations`, a list of `{"dx": n, "dy": n, "art": "<tile name>"}`: extra
wall tiles stamped at those offsets from the castle's `x, y`, blocking and
never interactive. An entry without `art` has been skipped. `kings-bounty`
has ringed its home castle this way.

**Dwellings.** A zone's `dwellings` entry has stamped a dwelling at `x, y`
by its `kind`, in the plural: `plains`, `forest`, `hills` or `dungeon`, drawn
with `dwelling_<kind>`. Any other kind, the troop catalog's singular `hill`
included, has stamped no dwelling. This has differed from a troop's `dwelling`,
which has been singular (`hill`).

**Salt.** A zone's `salt` has turned some of its `chests` into other objects
at the start of each game. Each count has been a number of chest positions
(default 0):

| Key | Default | Effect |
|---|---|---|
| `artifacts`     | 0 | Artifacts, the catalog's for this `zone` by `local_idx`; a chest pinning an artifact has counted against it. |
| `navmaps`       | 0 | Navigation maps. |
| `orbs`          | 0 | Crystal orbs. |
| `telecaves`     | 0 | Teleport caves. |
| `dwellings`     | 0 | Dwellings, each kind following its troop's `dwelling`. |
| `friendly_foes` | 0 | Friendly foes. |
| `preferred_troops` | none | Troop ids for the salted dwellings, in order, one each. |
| `dwelling_range` | none | `[min, max]` catalog positions: the dwellings past `preferred_troops` have rolled a troop in that range, never a `castle` troop. Absent, they have not been placed. |

Fixed chests and chests that pin an artifact have stayed out of the draw. A
zone whose remaining chests are fewer than the counts' total has been left
unsalted, with a log line.

**Wandering armies.** A `wandering_armies` entry has become a hostile foe at
`x, y`. `static` (bool, default false) has made it a guardian that never moves
and whose fight cannot be declined.

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
stands on under every combat cell (grass, desert, ...; water has fallen back to
grass) and the pack has shipped no field tile: `sprites.combat[0]` has been
left out of the manifest. `kings-bounty` has declared nothing and drawn its
field tile.

**Siege grid.** `sprites.ui.siege_grid` has named a path prefix for a full
grid of siege tiles, one file per cell: `<prefix>_<x>_<y>.png` for `x` in
`0..5` and `y` in `0..5`, row 0 the band above the board and rows 1..5 the
board's rows 0..4 (36 files). In a siege the shell has drawn each cell's own
tile as the ground and nothing for the wall codes, since the walls have been
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
6 × 5 picture cut into 96 px cells (`romeart.py siegeslice --field` has taken the
largest 6:5 rectangle of content centred in a picture, so a meadow painted on
white has kept its white out, scaled it to 576 × 480 and cut it). A zone has
been able to declare its
own `field_grid` prefix, which has won for fights on that zone, so each
continent can have its own ground. The shell has drawn each cell's own
picture under the obstacles and troops, in place of `combat_ground`, when
every cell of the zone's grid has loaded; a siege has kept the siege grid.
Absent, `combat_ground` has applied. `glory-of-rome` has shipped a grid per
zone (`art/combat/field/<zone>_<x>_<y>.png`, each zone's own `field_grid`);
`kings-bounty` has declared none.

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

**Treasure vistas.** A zone has been able to declare `"treasure_scene"`, a
240x102 vista the chest's gold-or-leadership choice has stood under while the
hero is in that zone (#140); a zone without one has kept the menu page.

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

**A pinned artifact.** A zone chest has been able to carry `"artifact": "<id>"`:
it has then been that artifact, placed before the salt draws and counted
against the zone's artifact quota, so the salt has scattered only the rest
(OPENBOUNTY-SPEC REQ-231). It has stayed out of the barrel like a fixed chest.
Glory of Rome's guarded trail on Sardinia has ended in one.

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
scene has been drawn full width (the image has been 240x102, like every backdrop)
with a single Continue, and the effects have changed the map for good -- they
have survived a zone switch and a save. `requires` has taken `spell`, `troop`,
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
name, which has also found `<name>/game.json` under each root. If more than one
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
in both shipped packs. A breaking change has bumped it.

Within a single schema version, optional fields have been addable to
packs without breaking older engine builds (older engines have ignored unknown
keys).
