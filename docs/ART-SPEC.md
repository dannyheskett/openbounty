# OpenBounty Art Spec

A commissioning sheet for a complete pack of game graphics. Everything here is
what the engine actually reads; the reference pack at `assets/kings-bounty/`
is the working example of every item below.

**Deliverable:** 311 PNG files in the directory layout of §7, plus one 768-byte
palette binary.

---

## 1. The one hard constraint: 48 × 34

Nearly every sprite and every map tile is **48 × 34 pixels**. It is not
negotiable and not a pack setting — it is compiled into the renderer
(`src/layout.h`, `CL_TILE_W 48`).

The odd aspect comes from the original VGA 320×200 mode, whose pixels were
**not square**: they were displayed on a 4:3 screen, so each pixel is about
20% taller than it is wide. A 48×34 tile therefore reads on screen as roughly
square. **Draw for the display, not the pixel grid** — a circle you want to
look round should be drawn as a slightly wide ellipse. If you work in a
square-pixel program, set a 1.2 : 1 horizontal stretch on your preview.

The whole game renders into a 320 × 200 buffer, integer-scaled to the window.
Nothing is ever drawn at a fractional scale, so there is no antialiasing to
hide behind: every pixel you place is a pixel the player sees.

## 2. Format and transparency

- **PNG, 32-bit RGBA.** Indexed-color PNGs are not required.
- **Alpha is binary in practice** — fully opaque or fully transparent. Avoid
  soft/feathered edges; they read as dirt at this size.
- **Sprites must have real transparent backgrounds.** Troops, villains,
  the hero, the boat, and map objects are drawn *over* the terrain tile
  beneath them. A sprite delivered with an opaque background will show as a
  rectangle of the wrong ground.
- **Terrain tiles are fully opaque** and must tile seamlessly against their
  own kind on all four edges.
- No drop shadows onto transparency, no baked-in ground beneath a unit.

## 3. Palette and color

The pack ships a 256-color VGA palette (`palettes/palette.bin`, exactly
768 bytes, 256 × RGB triplets, 8-bit each). The engine uses it for the UI
chrome's named colors, **not** to quantize your art — your PNGs can carry any
RGBA values.

That said, the game should look like one thing. Work from a **restricted
palette of roughly 32–64 colors** across the whole pack, hold to it, and
deliver the final table as the 768-byte binary (first 16 entries are the
standard EGA/VGA named colors: black, blue, green, cyan, red, magenta, brown,
light grey, dark grey, and the six bright variants, then white). Everything
after index 15 is yours.

Style target: late-80s / early-90s DOS VGA. Chunky, high-contrast, readable at
100%. Dithering is welcome and period-correct.

## 4. Animation

Animated things are **4 frames**, named `<root>_00.png` … `<root>_03.png`,
played as a straight 0→1→2→3 loop at a player-configurable 0.05–0.30 s per
frame. There is no ping-pong and no per-frame timing.

Every troop, every villain, the hero, and the boat animate. These are idle
animations — a breathing bob, a weapon shift, a flicker of cloth — **not**
walk cycles. The sprite does not turn to face a direction; the engine mirrors
the hero horizontally when he moves left, so **draw the hero facing right**.

## 5. Asset inventory

### 5.1 Troops — 100 files, 48 × 34

25 creature types × 4 frames, at `art/troops/<id>_00..03.png`:

```
peasants  sprites   militia   wolves    skeletons  zombies   gnomes
orcs      archers   elves     pikemen   nomads     dwarves   ghosts
knights   ogres     barbarians trolls   cavalry    druids    archmages
vampires  giants    demons    dragons
```

These appear both on the overworld and in combat, so they must read at a
glance and be distinguishable from one another in silhouette. They span the
full power curve — Peasants are the trash tier, Dragons the apex — and the
art should telegraph that ranking. Several carry mechanics the player must be
able to see: **Sprites, Archmages, Vampires, Demons and Dragons fly**;
**Skeletons, Zombies, Ghosts and Vampires are undead**; **Orcs, Archers,
Elves, Druids, Archmages and Giants are ranged attackers**.

### 5.2 Villains — 68 files, 48 × 34

17 named antagonists × 4 frames, at `art/villains/<id>_00..03.png`:

```
murray  hack  aimola  baron_makahl  dread_rob  caneghor  moradon
barrowpine  bargash  rinaldus  ragface  mahk  auric  czar_nickolai
magus  urthrax  arech
```

These are individuals, not troop types — each is a named character with flavor
text in `game.json` (`strings.villain_descriptions`) worth reading before you
draw them. They escalate in menace across the list, roughly in the order
above. Frame `_00` doubles as the villain's portrait in the contract view.

### 5.3 Map tiles — 72 files, 48 × 34, opaque

At `art/tiles/`.

**Base terrain (5) + variants:** `grass`, `grass_variant`, `forest`,
`mountain`, `water`, `desert`.

**Edge transitions (48):** `water_edge_00..11`, `forest_edge_01..12`,
`mountain_edge_01..12`, `desert_edge_01..12` — the corner-and-side pieces that
blend each terrain into grass. Twelve orientations per terrain: four sides,
four outer corners, four inner corners.

**Structures and objects (18):** `castle_gate`, `castle_tl`, `castle_tr`,
`castle_ml`, `castle_mr`, `castle_br` (a castle is a 3×2 footprint: a walkable
gate tile plus five wall pieces), `town`, `dwelling_plains`,
`dwelling_forest`, `dwelling_hills`, `dwelling_dungeon`, `chest`,
`artifact_chest`, `artifact_ring`, `sign`, `bridge_h`, `bridge_v`,
`wandering_army`.

Object tiles (chest, sign, dwellings, town, castle gate) sit on grass, so give
them transparent surrounds and let the ground show through.

### 5.4 Hero and boat — 8 files, 48 × 34

`art/sprites/hero_walk_00..03.png` and `art/sprites/boat_00..03.png`. The hero
is the player's avatar on the overworld, drawn over terrain and mirrored when
walking left. The boat replaces him entirely while sailing.

### 5.5 Combat arena — 15 files, 48 × 34

At `art/combat/`. `field_grass` (the open-field battle floor, tiled across a
6×5 grid), `obstacle_01..03` (scattered field cover), `castle_wall_01..06`
and `castle_spike` (siege-battle layout), and `cursor_01..04` (the animated
target-picker cursor — this one overlays a unit, so keep it a thin frame, not
a fill).

### 5.6 Class portraits — 4 files, 96 × 102

`art/classes/{knight,paladin,sorceress,barbarian}.png`. Shown at character
creation and in the character view. Bust or three-quarter figure; these are the
only place the player sees the hero rendered large.

### 5.7 UI and chrome — 43 files, mixed sizes

At `art/ui/`.

| Item | Size | Notes |
|---|---|---|
| `chrome_overworld` | 320 × 200 | The full-screen frame: top status bar, map viewport border, right sidebar. The map and HUD draw *into* its cutouts. |
| `splash_title` | 320 × 200 | Title screen. |
| `splash_logo` | 320 × 84 | Publisher splash. |
| `class_select_picker` | 288 × 184 | Character-creation panel. |
| `class_select_highlight` | 42 × 44 | Selection cursor for the above. |
| `backdrop_{town,castle,plains,forest,hillcave,dungeon}` | 240 × 102 | Location scene behind each visit screen. Six moods: civic, martial, open country, woodland, cave, crypt. |
| `end_win_screen`, `end_lose_screen` | 144 × 170 | Endgame illustrations. |
| `end_hero`, `end_carpet`, `end_grass` | 48 × 34 | Pieces of the animated victory scene (the hero approaching the throne). |
| `hud_bar_strip` | 320 × 5 | Thin separator. |
| `hud_gold_purse`, `hud_puzzle_grid` | 48 × 34 | Sidebar icons. |
| `hud_contract_silhouette`, `hud_siege_silhouette`, `hud_magic_silhouette` | 48 × 34 | The "unavailable" state of three sidebar icons. |
| `hud_siege_00..03`, `hud_magic_00..03` | 48 × 34 | Their animated "available" state. |
| `inventory_artifact_{sword,shield,crown,articles,amulet,ring,book,anchor}` | 48 × 34 | The eight collectible artifacts. |
| `inventory_zone_{continentia,forestria,archipelia,saharia}` | 48 × 34 | Continent emblems. |
| `puzzle_cover` | 9 × 6 | One tile of the 5×5 puzzle grid's concealing cover. |

### 5.8 Font — 1 file, 1024 × 8

`art/font/kb-font.png`. A single horizontal strip of **128 glyphs, each
8 × 8 pixels**, laid out in ASCII order: glyph *n* occupies x = n × 8. Codes
0–31 are unused by ASCII and the engine repurposes four of them for a spinner
(`|`, `/`, `-`, `\`); supply printable glyphs for codes 32–126 and leave the
rest blank. White-on-transparent — the engine tints it at draw time.

## 6. Audio (if in scope)

Not graphics, listed for completeness: 2 OGG music tracks
(`audio/openworld.ogg`, `audio/combat.ogg`) and 4 WAV effects
(`tune_walk`, `tune_bump`, `tune_chest`, `tune_defeat`).

## 7. Directory layout

```
art/
├── troops/     100 files   48 × 34
├── villains/    68 files   48 × 34
├── tiles/       72 files   48 × 34
├── ui/          43 files   mixed
├── combat/      15 files   48 × 34
├── sprites/      8 files   48 × 34
├── classes/      4 files   96 × 102
└── font/         1 file    1024 × 8
palettes/
└── palette.bin              768 bytes
```

Filenames are referenced from `game.json` and are **case-sensitive**. Match
the reference pack's names exactly unless you are also editing that manifest.

## 8. Acceptance

An asset is done when it (a) is the exact pixel dimensions listed, (b) is RGBA
PNG with clean binary alpha, (c) holds the shared palette, and (d) reads
correctly at 1× on a 320×200 screen — not just zoomed in. Drop the tree into a
pack directory and run `./openbounty --pack /path/to/pack` to see it live.
