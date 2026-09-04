# OpenBounty Art Spec

Authoring dimensions for a complete pack of game graphics.

Nothing here is compiled into the renderer any more. Every size below is
derived from three numbers a pack declares in `game.json`, so this document is
a worked example, not a set of constants:

```json
"render": { "mode": "modern", "tile_w": 96, "tile_h": 96, "ui_scale": 2 }
```

Glory of Rome is the target this spec is written for: **96 x 96 tiles, authored
at native resolution.** King's Bounty stays exactly as it is -- `mode: legacy`,
48 x 34, `ui_scale` 1 -- and every formula below evaluates to its historic
literal at those settings, so the legacy pack is unaffected by anything here.

---

## 1. The model

Two multipliers, each with one job:

| | what it sizes | Rome |
|---|---|---|
| `tile_w` / `tile_h` | one map or combat cell | 96 x 96 |
| `ui_scale` | the font and all fixed UI furniture | 2 |

**`ui_scale` is not free: it must be `tile_w / 48`.** The original sized its
glyph at one sixth of its tile (8 against 48), and the HUD is built on that
ratio -- the gold counter is drawn inside a one-tile sidebar panel, so a font
that grows faster than the tile overflows it. A 96px tile therefore takes
`ui_scale` 2, giving a 16px glyph and the same 1:6 ratio. This was measured, not
assumed: at `ui_scale` 4 a four-digit gold total is 128px wide against a 96px
panel and spills out of the sidebar.

If everything looks small on a large monitor, that is what the presentation
scale is for -- see below. It enlarges the whole picture uniformly and keeps
every proportion intact.

Everything an artist delivers falls into one of two classes:

**Tile-shaped art** — anything that occupies a map cell, a combat cell, or a
sidebar panel. Authored at exactly `tile_w x tile_h`: **96 x 96**.

**Screen-shaped art** — chrome, portraits, backdrops, splash screens. Authored
at its design size times `ui_scale`. The design sizes are the original 320 x 200
layout's, and they are listed in the table below so nobody has to derive them.

A third multiplier, the **presentation scale**, blows the whole finished buffer
up (1x, 2x, 3x, ...). It is a per-machine viewing preference and changes nothing
about what you author. 1x is one buffer pixel to one screen pixel -- the default
and the resolution this art is authored for. A player on a 4K panel picks 2x or
3x, which enlarges everything uniformly and shows fewer tiles.

## 2. Authoring table

Rome: `tile 96 x 96`, `ui_scale 2`. Every asset is delivered at exactly the
size in the right-hand column.

### Tile-shaped — all 96 x 96

| category | count | path |
|---|---|---|
| terrain and map objects | 67 | `art/tiles/` |
| troop sprites and their animation frames | 100 | `art/troops/` |
| villain portraits and frames | 68 | `art/villains/` |
| combat field, obstacles, castle walls, cursors | 15 | `art/combat/` |
| hero walk / idle / boat frames | 8 | `art/sprites/` |
| HUD sidebar panels, artifact and map inventory icons | 28 | `art/ui/` |

These are one class on purpose: the same troop PNG is drawn into a combat cell,
an army-roster row, a location screen and the victory cartoon. One square size
means it is correct in all of them.

### Screen-shaped — design size x 2

| asset | design | authored | path |
|---|---|---|---|
| chrome frame | 320 x 200 | **640 x 400** | `art/ui/chrome_overworld.png` |
| splash title | 320 x 200 | **640 x 400** | `art/ui/` |
| splash logo | 320 x 84 | **640 x 168** | `art/ui/` |
| status bar strip | 320 x 5 | **640 x 10** | `art/ui/hud_bar_strip.png` |
| class picker | 288 x 184 | **576 x 368** | `art/ui/` |
| location backdrops | 240 x 102 | **480 x 204** | `art/ui/` (6) |
| ending win / lose | 144 x 170 | **288 x 340** | `art/ui/` (2) |
| class portraits | 96 x 102 | **192 x 204** | `art/classes/` (4) |
| class-select highlight | 42 x 44 | **84 x 88** | `art/ui/` |
| puzzle cover chip | 9 x 6 | **18 x 12** | `art/ui/` |

### Font

The bitmap font is a single horizontal strip of 128 glyphs, ASCII order, no
padding. At `ui_scale` 2 a glyph is **16 x 16**, so the strip is **2048 x 16**.

Rome currently ships `1024 x 8` (an 8 x 8 glyph), which the engine blows up 2x.
The engine reads the glyph size off the strip, so dropping a 2048 x 16 file in
is all that is needed.

## 3. The chrome frame is a nine-slice

`chrome_overworld.png` is not stretched to the screen. It is cut into nine
pieces: four corners drawn 1:1, four edge bands repeated along their length,
and a transparent middle.

The renderer takes a corner of exactly `16 * ui_scale` wide by `8 * ui_scale`
tall from the source. **The decorative band in the source must be exactly that
thick** -- 32 x 16 at `ui_scale` 2 -- or the slice grabs transparent interior
and the frame renders thinner than the space reserved for it.

This is measured, not theoretical: Rome's current 320 x 200 chrome draws a 16px
band into a 32px slot, leaving a black gap between the frame and the playfield
in every screenshot.

So: left and right bands 32px wide, top and bottom bands 16px tall, interior
fully transparent, and the pattern must repeat cleanly along each band because
the middle span is tiled, not stretched.

## 4. Format, transparency, palette

- **PNG, 32-bit RGBA.**
- **Alpha is binary.** Fully opaque or fully transparent. Soft edges read as
  dirt, and the QA counts partial-alpha pixels as a failure.
- **Sprites need real transparent backgrounds.** Troops, villains, the hero and
  map objects draw *over* terrain. No baked-in ground, no drop shadow onto
  transparency.
- **Terrain tiles are fully opaque** and must tile seamlessly against their own
  kind on all four edges.
- The pack ships a 256-colour palette (`palettes/palette.bin`, 768 bytes) that
  the engine uses for named UI colours only. It does **not** quantise art --
  PNGs may carry any RGBA. Even so, work from a restricted palette so the pack
  reads as one thing.

## 5. Engine state

Both prerequisites this spec depended on are done:

- Slots holding tile-shaped art read `CL_TILE_W/H` rather than the legacy
  48 x 34 design units, so a square tile no longer stretches sprites in the
  army roster, the contract panel, the puzzle grid or the inventory belt.
- The font glyph size is measured off the strip, so a pack can ship a
  higher-resolution font and have it drawn 1:1.

The manifest flip is done: Rome declares `tile_h` 96 (2026-09-04), so the
cell is square and 96 x 96 art draws 1:1. Every tile or sprite still at
48 x 34 stretches to a square until it is replaced; `ART-WORKLIST.md` is
the list of what replaces them.

## 6. Verifying

`tools/capture.sh` and `tools/walkthrough.sh` drive a running game and pull
frames out. Check new art in place, at the size it ships, over real terrain --
not in isolation at 8x zoom, where everything looks fine.

Two rules learned by getting them wrong:

- **An automated check only tests what someone thought to encode.** "Feet on
  the bottom row" passes for a foot standing on it and for a leg sliced off by
  it. Metrics are a floor, never a verdict.
- **Look at the image beside the reference before reporting anything.**
