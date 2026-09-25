# OpenBounty Art Spec

Authoring dimensions for a complete pack of game graphics.

No size here has been compiled into the renderer. Every size below has been
derived from numbers a pack declares in `game.json`, so this document has been
a worked example, not a set of constants. Glory of Rome's block:

```json
"render": { "mode": "modern", "tile_w": 96, "tile_h": 96, "tiles_w": 5, "tiles_h": 5,
            "ui_scale": 1, "dim": 30, "native_w": 800, "native_h": 504 }
```

Glory of Rome has been the target this spec is written for: **96 x 96 tiles,
authored at native resolution.** King's Bounty has been `mode: legacy`,
48 x 34, `ui_scale` 1, and every formula below has evaluated to its legacy
value at those settings, so the legacy pack has been unaffected by anything
here.

---

## 1. The model

Two numbers, each with one job:

| | what it has sized | Rome |
|---|---|---|
| `tile_w` / `tile_h` | one map or combat cell | 96 x 96 |
| `ui_scale` | the chrome bands and fixed UI furniture | 1 |

**The text has been one sixth of a tile.** The original sized its glyph at one
sixth of its tile (8 against 48), and the HUD has been built on that ratio:
the gold counter has been drawn inside a one-tile sidebar panel, so a font
that grows faster than the tile has overflowed it. At `ui_scale` 4 a
four-digit gold total has been 128 px wide against a 96 px panel and has
spilled out of the sidebar. A pack on the bitmap strip font has had an
`8 * ui_scale` glyph, so it has kept the ratio with `ui_scale` =
`tile_w / 48`. Rome has declared a TrueType face at 16 px instead (section 2,
Font), the same one-sixth of its 96 px tile, so it has declared `ui_scale` 1.

The whole picture has been shown at the largest whole scale the screen allows
(`DESIGN-SPEC.md`), which has changed nothing about what you author.

Everything an artist delivers has fallen into one of two classes:

**Tile-shaped art** — anything that occupies a map cell, a combat cell, or a
sidebar panel. Authored at exactly `tile_w x tile_h`: **96 x 96**.

**Screen-shaped art** — portraits, backdrops, splash and title screens.
Authored at the size in the table below and drawn at the largest whole
multiple that fits its slot (`ui_fit_scale`, `src/ui.c`); a location backdrop
has been scaled up to the width of the pane. The generated pieces (title,
picker, backdrops, endings) have all come from one engine, RD Pro, which has
capped a side at 256 (ART-PIPELINE, "Engines and their size caps"), so they
have been authored at the original 320 x 200 layout's design size or under the
cap.

## 2. Authoring table

Rome: `tile 96 x 96`, `ui_scale 1`. Every asset has been delivered at exactly
the size in the "authored" column.

### Tile-shaped — all 96 x 96

| category | path |
|---|---|
| terrain and map objects | `art/tiles/` (a zone with `tile_set` draws its terrain from `art/tiles/<set>/` instead; objects stay shared) |
| zone terrain sets | `art/tiles/africa/`, `galliae/`, `oriens/` |
| troop sprites and their animation frames | `art/troops/` |
| villain portraits and frames | `art/villains/` |
| character portraits and frames | `art/portraits/` |
| hero figures and walk frames | `art/classes/` |
| boat frames | `art/sprites/` |
| obstacles, castle spike, cursors | `art/combat/` |
| HUD panels, inventory icons, location figures | `art/ui/` |

The castle battlefield has been the one exception: 36 cells at 32 x 32 in
`art/combat/siege/`, scaled to the cell (PACK-FORMAT, "Siege grid"). An
open field has been a picture of its own per continent, the centre
576 x 480 of it cut into 30 cells of 96 x 96 at 1:1 in `art/combat/field/`
(PACK-FORMAT, "Field grid"); Italia's has shipped, from
`art/fields/italia.png`.

These have been one class on purpose: the same troop PNG has been drawn into a
combat cell, an army-roster row, a location screen and the victory cartoon.
One square size has meant it is correct in all of them.

### Screen-shaped

| asset | design | authored | path |
|---|---|---|---|
| chrome frame | 320 x 200 | none: drawn in code (section 3) | -- |
| splash title | 320 x 200 | **256 x 164** (RD Pro cap) | `art/ui/` |
| title battle and title words | 320 x 200 | **256 x 164** (RD Pro cap) | `art/ui/` |
| title eagle | -- | **96 x 164** | `art/ui/` |
| splash logo | 320 x 84 | **320 x 84** | `art/ui/` |
| status bar strip | 320 x 5 | none: a modern screen has no status bar | -- |
| class picker | 288 x 184 | **256 x 164** (RD Pro cap) | `art/ui/` |
| class picker, one frame per figure picked | 288 x 184 | **256 x 164** (4) | `art/ui/` |
| location backdrops | 240 x 102 | **240 x 102** | `art/ui/` |
| disgraced scenes, one per class | 240 x 102 | **240 x 102** (4) | `art/ui/` |
| one-time vistas | 240 x 102 | **240 x 102** | `art/scenes/` |
| scene column capital, shaft, base | -- | **28 x 9**, **28 x 45**, **28 x 8** | `art/ui/` |
| ending win / lose, promotion | 144 x 170 | **144 x 170** | `art/ui/` |
| class portraits | 96 x 102 | **192 x 204** | `art/classes/` (4) |
| class-select highlight | 42 x 44 | **42 x 44** | `art/ui/` |
| puzzle cover chip | 18 x 18 | **18 x 18** | `art/ui/` |

### Font

Rome has declared a TrueType face in `game.json` (`font` block, PACK-FORMAT
§2.2): Press Start 2P, SIL OFL, drawn at 16 (a 16 px cell) in a fixed cell
with anti-aliasing; the title strips, the message box and the text in rows
have taken their height from the face. The 8 x 8 strip `rome-font.png` has
been in the pack as the fallback.

The bitmap strip route, which legacy has used, has been a single horizontal
strip of 128 glyphs, ASCII order, no padding, read at whatever glyph size it
has been authored at and drawn into the `8 * ui_scale` cell.

## 3. The chrome has been drawn in code; a bitmap frame has been a nine-slice

Rome has shipped no chrome bitmap. Its frame, the bands beside the map, the
joins between column tiles and every page's ring have been the gold lattice
drawn by `src/lattice.c`: a cross-hatch of two gold strands on dark wood,
bright where they cross, one repeat every 8 units (8 px at `ui_scale` 1),
railed in gold with a dark line inside the rail. The declared buffer has been
800 x 504 (`render.native_w/native_h`) with a 5 x 5 viewport; across it have
run a 12 px edge, the 96 px left column, a 4 px band, the 576 px map, a 4 px
band, the 96 px right column and a 12 px edge, and down it a 12 px edge, the
480 px map and a 12 px edge (`layout_init`, `src/layout.c`; DESIGN-SPEC
DSGN-0003).

A pack that ships `chrome_overworld.png` has had it cut into nine pieces
rather than stretched to the screen: four corners drawn 1:1, four edge bands
repeated along their length, and a transparent middle.

The renderer has taken a corner of exactly `16 * ui_scale` wide by
`8 * ui_scale` tall from the source. **The decorative band in the source has
had to be exactly that thick**, or the slice has grabbed transparent interior
and the frame has rendered thinner than the space reserved for it. So: left
and right bands `16 * ui_scale` wide, top and bottom bands `8 * ui_scale`
tall, interior fully transparent, and a pattern that repeats cleanly along
each band, because the middle span has been tiled, not stretched.

## 4. Format, transparency, palette

- **PNG, 32-bit RGBA.**
- **Alpha has been binary.** Fully opaque or fully transparent. Soft edges
  have read as dirt, and the QA has counted partial-alpha pixels as a
  failure.
- **Sprites have needed real transparent backgrounds.** Troops, the hero and
  map objects have drawn *over* terrain. Villains have not been sprites: they
  have been opaque bust portraits with a painted background, drawn as faces
  in the contract view, the HUD chip and the puzzle grid. No baked-in ground,
  no drop shadow onto transparency.
- **Terrain tiles have been fully opaque** and have had to tile seamlessly
  against their own kind on all four edges.
- The pack has shipped a 256-colour palette (`palettes/palette.bin`, 768
  bytes) that the engine has used for named UI colours only. It has **not**
  quantised art -- PNGs have been free to carry any RGBA. Even so, work from a
  restricted palette so the pack reads as one thing.

## 5. Engine behaviour an artist has relied on

- Slots holding tile-shaped art have read `CL_TILE_W/H`, so a square tile has
  drawn square in the army roster, the contract panel, the puzzle grid and
  the inventory belt, not only on the map.
- The font glyph size has been measured off the strip, so a pack has been
  able to ship a higher-resolution strip and have it drawn 1:1.
- Every texture and the render target have used point sampling and the
  present has been a whole-number scale, so nothing has blurred inside the
  game's own path.

## 6. Verifying

`tools/capture.sh` and `tools/walkthrough.sh` have driven a running game and
pulled frames out. Check new art in place, at the size it ships, over real
terrain -- not in isolation at 8x zoom, where everything looks fine.

Two rules:

- **An automated check has only tested what someone thought to encode.**
  "Feet on the bottom row" has passed for a foot standing on it and for a leg
  sliced off by it. Metrics have been a floor, never a verdict.
- **Look at the image beside the reference before reporting anything.**
