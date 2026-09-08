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
at the size in the table below and scaled by `ui_scale` at draw time. The design
sizes are the original 320 x 200 layout's, listed so nobody has to derive them.
The generated pieces (title, picker, portraits, backdrops) all come from one
engine, RD Pro, which caps a side at 256 (ART-PIPELINE, "Engines and their size
caps"), so they are authored at the design size, an exact 2x on screen; the
portraits are the one generated piece small enough to be made at x2.

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
| terrain and map objects | 67 | `art/tiles/` (a zone with `tile_set` draws its terrain from `art/tiles/<set>/` instead; objects stay shared) |
| troop sprites and their animation frames | 100 | `art/troops/` |
| villain portraits and frames | 68 | `art/villains/` |
| combat field, obstacles, castle walls, cursors | 15 | `art/combat/` |
| hero walk / idle / boat frames | 8 | `art/sprites/` |
| HUD sidebar panels, artifact and map inventory icons | 28 | `art/ui/` |

These are one class on purpose: the same troop PNG is drawn into a combat cell,
an army-roster row, a location screen and the victory cartoon. One square size
means it is correct in all of them.

### Screen-shaped — design size x 2, except the generated pieces (RD Pro cap 256)

| asset | design | authored | path |
|---|---|---|---|
| chrome frame | 320 x 200 | none: drawn in code (section 3) | -- |
| splash title | 320 x 200 | **256 x 164** (RD Pro cap, stretched to its rect) | `art/ui/` |
| splash logo | 320 x 84 | **640 x 168** | `art/ui/` |
| status bar strip | 320 x 5 | none: drawn in code (section 3) | -- |
| class picker | 288 x 184 | **256 x 164** (RD Pro cap, stretched to its rect) | `art/ui/` |
| location backdrops | 240 x 102 | **240 x 102** (design size; RD Pro cap) | `art/ui/` (6) |
| ending win / lose | 144 x 170 | **144 x 170** (design size; RD Pro cap) | `art/ui/` (2) |
| class portraits | 96 x 102 | **192 x 204** | `art/classes/` (4) |
| class-select highlight | 42 x 44 | **84 x 88** | `art/ui/` |
| puzzle cover chip | 9 x 6 | **18 x 12** | `art/ui/` |

### Font

The bitmap font is a single horizontal strip of 128 glyphs, ASCII order, no
padding. At `ui_scale` 2 a glyph is **16 x 16**, so the strip is **2048 x 16**.

Rome currently ships `1024 x 8` (an 8 x 8 glyph), which the engine blows up 2x.
The engine reads the glyph size off the strip, so dropping a 2048 x 16 file in
is all that is needed.

## 3. The chrome is drawn in code; a bitmap frame is a nine-slice

Rome ships no chrome bitmap. Its frame bands, the bar under the status line,
the HUD panel borders and every window border are the gold lattice drawn by
`src/lattice.c`: a cross-hatch of two gold strands on dark wood, bright where
they cross, one repeat every 8 units (16 px at `ui_scale` 2), railed in gold
with a dark line inside the rail. The buffer is fixed at 832 x 540
(`render.native_w/native_h`) with a 7 x 5 viewport, the minimum that holds
it, so the side bands are 32 px and the top and bottom 16 px; the window opens at 1x and the Scale control
steps 1x, 2x, 3x. Screen art (splash, title, picker) draws at the largest
whole scale that fits: the 256 x 164 picker at 3x, 768 x 492.

A pack that does ship `chrome_overworld.png` is drawn as before: the bitmap is
not stretched to the screen. It is cut into nine
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
- **Sprites need real transparent backgrounds.** Troops, the hero and map
  objects draw *over* terrain. Villains are not sprites: they are opaque
  bust portraits with a painted background, drawn as faces in the contract
  view, the HUD chip and the puzzle grid. No baked-in ground, no drop shadow onto
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

### Rendering state, measured 2026-09-07

What the shell does today in modern mode, read from `src/layout.c`,
`src/present.c`, `src/chrome.c`, `src/bfont.c` and `src/main.c`:

- **Buffer and scale.** The buffer is the window divided by the presentation
  scale; default 1, so one buffer pixel is one screen pixel. A bigger window
  shows more tiles, not bigger ones. Legacy is the opposite: a fixed 320 x 200
  buffer at the largest integer scale (2..5) that fits, centred and
  letterboxed. Scales above 1 are a menu choice, clamped to the window.
- **Viewport.** The map pane is the interior minus the chrome bands and the
  one-tile sidebar. It holds whole tiles only, an odd count each way, floor 5,
  ceiling 63. Space left over stays black inside the frame, on the right and
  at the bottom: the tile field is not centred in the pane, so the hero sits
  left of and above the window centre by up to a tile.
- **Two pixel densities on one screen.** Tile-shaped art draws at 1x with
  one-pixel detail. Everything sized by `ui_scale` (frame bands, status, bar,
  font, dialogs) is the legacy design doubled, two-pixel detail. Rome still
  ships the legacy pieces for all of it: `chrome_overworld.png` 320 x 200,
  `hud_bar_strip.png` 320 x 5, `rome-font.png` 1024 x 8. The chrome is a
  nine-slice (section 3) cut at `16 * ui_scale` by `8 * ui_scale`, so from a
  320 x 200 source the corners take half transparent interior and the frame
  draws thinner than its slot; the bar strip is tiled at native width and
  doubled in height only; the font is the 8-pixel glyph doubled.
- **Screen art.** The picker and title are 256 x 164 drawn at their width
  times `ui_scale`, 512 x 328, centred in a black window of any size.
- **Combat.** Cells equal the tile; the 6 x 5 board is 576 x 480 at 1x,
  centred in the map pane, so on a 1920-wide window it is under a third of the
  width unless the player raises the scale.
- **DPI.** No high-DPI window flag and no DPI query. Harmless on X. On a
  Windows or macOS display at 150 or 200 percent the OS scales the window
  bilinearly and the pixels blur.
- **Filtering.** Every texture and the render target use point sampling and
  the present is an integer scale, so nothing blurs inside the game's own path.
- **Minimum window** for Rome: 640 x 540, from the 6 x 5 board and the 5-tile
  viewport floor.

Superseded 2026-09-08: the frame and bar are now drawn in code (section 3),
the buffer is fixed at 832 x 540 with the map centred, and the zoom is 1x, 2x
or 3x of that buffer. The font is still the 8-pixel glyph doubled.

## 6. Verifying

`tools/capture.sh` and `tools/walkthrough.sh` drive a running game and pull
frames out. Check new art in place, at the size it ships, over real terrain --
not in isolation at 8x zoom, where everything looks fine.

Two rules learned by getting them wrong:

- **An automated check only tests what someone thought to encode.** "Feet on
  the bottom row" passes for a foot standing on it and for a leg sliced off by
  it. Metrics are a floor, never a verdict.
- **Look at the image beside the reference before reporting anything.**
