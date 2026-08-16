# OpenBounty Art Spec

Authoring dimensions for a complete pack of game graphics.

Nothing here is compiled into the renderer any more. Every size below is
derived from three numbers a pack declares in `game.json`, so this document is
a worked example, not a set of constants:

```json
"render": { "mode": "modern", "tile_w": 96, "tile_h": 96, "ui_scale": 4 }
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
| `ui_scale` | the font and all fixed UI furniture | 4 |

Everything an artist delivers falls into one of two classes:

**Tile-shaped art** — anything that occupies a map cell, a combat cell, or a
sidebar panel. Authored at exactly `tile_w x tile_h`: **96 x 96**.

**Screen-shaped art** — chrome, portraits, backdrops, splash screens. Authored
at its design size times `ui_scale`. The design sizes are the original 320 x 200
layout's, and they are listed in the table below so nobody has to derive them.

A third multiplier, the **presentation scale**, blows the whole finished buffer
up on a very high-resolution display (Auto / 2x / 3x). It is a viewing
preference and changes nothing about what you author. At 1080p it is 1, which
is why Rome's art is authored at native resolution.

## 2. Authoring table

Rome: `tile 96 x 96`, `ui_scale 4`. Every asset is delivered at exactly the
size in the right-hand column.

### Tile-shaped — all 96 x 96

| category | count | path |
|---|---|---|
| terrain and map objects | 72 | `art/tiles/` |
| troop sprites and their animation frames | 100 | `art/troops/` |
| villain portraits and frames | 68 | `art/villains/` |
| combat field, obstacles, castle walls, cursors | 15 | `art/combat/` |
| hero walk / idle / boat frames | 8 | `art/sprites/` |
| HUD sidebar panels, artifact and map inventory icons | 28 | `art/ui/` |

These are one class on purpose: the same troop PNG is drawn into a combat cell,
an army-roster row, a location screen and the victory cartoon. One square size
means it is correct in all of them.

### Screen-shaped — design size x 4

| asset | design | authored | path |
|---|---|---|---|
| chrome frame | 320 x 200 | **1280 x 800** | `art/ui/chrome_overworld.png` |
| splash title | 320 x 200 | **1280 x 800** | `art/ui/` |
| splash logo | 320 x 84 | **1280 x 336** | `art/ui/` |
| status bar strip | 320 x 5 | **1280 x 20** | `art/ui/hud_bar_strip.png` |
| class picker | 288 x 184 | **1152 x 736** | `art/ui/` |
| location backdrops | 240 x 102 | **960 x 408** | `art/ui/` (6) |
| ending win / lose | 144 x 170 | **576 x 680** | `art/ui/` (2) |
| class portraits | 96 x 102 | **384 x 408** | `art/classes/` (4) |
| class-select highlight | 42 x 44 | **168 x 176** | `art/ui/` |
| puzzle cover chip | 9 x 6 | **36 x 24** | `art/ui/` |

### Font

The bitmap font is a single horizontal strip of 128 glyphs, ASCII order, no
padding. At `ui_scale` 4 a glyph is **32 x 32**, so the strip is **4096 x 32**.

Rome currently ships `1024 x 8` (an 8 x 8 glyph), which the engine blows up 4x.
See §5 -- the engine must learn the glyph size before a higher-resolution font
can be delivered.

## 3. The chrome frame is a nine-slice

`chrome_overworld.png` is not stretched to the screen. It is cut into nine
pieces: four corners drawn 1:1, four edge bands repeated along their length,
and a transparent middle.

The renderer takes a corner of exactly `16 * ui_scale` wide by `8 * ui_scale`
tall from the source. **The decorative band in the source must be exactly that
thick** -- 64 x 32 at `ui_scale` 4 -- or the slice grabs transparent interior
and the frame renders thinner than the space reserved for it.

This is measured, not theoretical: Rome's current 320 x 200 chrome at
`ui_scale` 2 draws a 16px band into a 32px slot, leaving a black gap between
the frame and the playfield in every screenshot.

So: left and right bands 64px wide, top and bottom bands 32px tall, interior
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

## 5. Engine changes this spec depends on

Two things must change before a 96 x 96 pack is fully correct. Both are small
and neither affects legacy.

**Tile-shaped slots must derive from the tile.** Several slots that hold
tile-shaped art are still sized from the legacy 48 x 34 design units times
`ui_scale`, which was right when the tile *was* 48 x 34 and is wrong for a
square tile. At `tile 96 x 96, ui_scale 4` the army-roster row, the contract
villain face, the full-screen puzzle cell and the inventory belt would each
draw a 96 x 96 sprite into a 192 x 136 slot and stretch it. They need to read
`CL_TILE_W/H`, like the combat cell already does.

**The font glyph size must come from the pack.** `BFONT_SRC_GLYPH_W/H` are
hardcoded to 8 in `src/bfont.h`, so every glyph is an upscale of an 8 x 8
source no matter what `ui_scale` says. Reading the glyph size from the strip
(width / 128) or from the manifest lets a pack ship a crisp 32 x 32 font.

## 6. Verifying

`tools/capture.sh` and `tools/walkthrough.sh` drive a running game and pull
frames out. Check new art in place, at the size it ships, over real terrain --
not in isolation at 8x zoom, where everything looks fine.

Two rules learned by getting them wrong:

- **An automated check only tests what someone thought to encode.** "Feet on
  the bottom row" passes for a foot standing on it and for a leg sliced off by
  it. Metrics are a floor, never a verdict.
- **Look at the image beside the reference before reporting anything.**
