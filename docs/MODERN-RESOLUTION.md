# Modern resolution

How a `modern` pack has filled a display. The requirement has been REQ-528
(`OPENBOUNTY-SPEC.md`); this page has been the working detail behind it.

## The model

Three quantities, each with one job.

| | what it has done | who has set it |
|---|---|---|
| `tile_w` / `tile_h` | the size of one map tile in buffer pixels | the pack |
| `ui_scale` | multiplied the font and the chrome bands | the pack |
| present scale | how many screen pixels one buffer pixel has become | the surface |

There has been no zoom setting and no scale menu. `present_scale`
(`src/present.c`) has returned the largest whole number of times the pack's
buffer fits the surface, so every pack pixel has stayed square and the art
has stayed hard-edged.

A modern pack has laid out in one of two ways.

**A declared buffer** (`render.native_w` / `native_h`; Glory of Rome has
declared 800 x 532). The declared size has been a floor, not a fixed size.
The scale has been measured against it, and what that scale leaves over has
gone to the map viewport in whole tiles, an odd count so the hero has kept the
centre cell (`layout_grow_native`, `src/layout.c`). The chrome bands, the
sidebar and its gap, the status and bar heights and every panel have kept the
size the pack declared and re-centred.

Only the world map has grown. `present_allow_growth` has been off by default
and set for one frame by the world frame alone (`shell_present_frame` in
`src/shell_frame.c`, and the main loop's draw). A town, a castle, the
battlefield, the title and every dialog have refitted to the declared buffer
and been letterboxed. Panels that sit in the map pane have laid out against
`CL_PANE_BASE_*` (`src/layout.h`), a clamp that has pinned a grown pane back
to its declared size, so a panel has kept the exact size it was drawn for.

On a touch session the menu band has grown to a touch unit, but only out of
the slack the whole tiles leave: the tile count has been odd, so taking a row
would cost two rows of world, and the band has never done that.

The desktop window has opened at the declared buffer times the largest whole
scale the monitor can show, so it has always been an exact multiple
(`src/main.c`). Phones and the web canvas have taken whatever surface they
are given.

**No declared buffer.** The buffer has been the window divided by the scale
(`layout_fit_window`, `src/layout.c`), so the chrome frame has reached the
window edge. The map pane has been whatever is left inside the chrome; it has
held as many whole tiles as fit, the sub-tile remainder has stayed black
inside the frame, and the tile grid has been centred so the remainder splits
evenly. A partial tile has never been drawn.

In both layouts, fixed-size screens -- the character sheet, the army list, the
dialog panel -- have used the **content rect**: 240 x 170 design units times
`ui_scale`, centred in the pane. Combat has been a 6 x 5 grid of one-tile
cells, centred. Wide screens (character, army, gate, end game) have used the
content rect plus a sidebar's width, centred in the chrome interior -- the
construction `CL_COMBAT_X` has used. Centring those in the pane would put
legacy at x=-8, because the view has been wider than the pane by exactly the
sidebar.

### Art has scaled to its slot, never to itself

Art has filled the slot that holds it: `CL_TILE_W/H` for anything in a map or
combat cell, its design size times `CL_UI` for everything else. Drawing at
`tex.width` / `tex.height` would draw at 1x inside a buffer the pack has sized
up, which would leave combat as small sprites in black gutters and a location
backdrop as a postage stamp in the corner of the pane. Use `ui_blit`
(`src/ui.h`) rather than hand-rolling the draw call.

### Every render-target loop has re-fitted

The buffer has been derived from the surface, so it has gone stale the moment
the window is resized or a phone rotates. `present_refit` (`src/present.h`)
has re-fitted and reallocated the target; every loop that owns a target has
called it at the top of its frame.

The pack's `tiles_w` / `tiles_h` have been the viewport it opens with, not a
fixed size.

### Legacy has been unchanged by construction

Legacy mode has kept its fixed 320 x 200 buffer, auto-fit with a 2x floor
(`CL_SCALE_MIN`), and none of the above has applied. `ui_scale` has been 1
for a legacy pack, so every constant expressed in `CL_UI` has evaluated to
its legacy value. The content rect has been 240 x 170 and the legacy pane
240 x 170, so both centring offsets have been zero and it has landed at 16,22.
The combat grid has been 288 x 170 in a 288-wide interior and a 170-tall map
band, so it has landed at 16,22 as well. `layout_fit_window` and
`layout_grow_native` have returned at once for legacy.

## Worked example

Glory of Rome: tile 96 x 96, `ui_scale` 1, declared buffer 800 x 532 with a
7 x 5 viewport, so the chrome around the pane has been 800 - 7x96 = 128 wide
and 532 - 5x96 = 52 tall.

```
1920 x 1080 display     scale min(1920/800, 1080/532) = 2
                        room 960 x 540: (960-128)/96 = 8 -> 7 odd, (540-52)/96 = 5
                        buffer 800 x 532, shown at 1600 x 1064

2532 x 1170 surface     scale min(2532/800, 1170/532) = 2
                        room 1266 x 585: (1266-128)/96 = 11, (585-52)/96 = 5
                        buffer 1184 x 532: the world map shows 11 x 5 tiles;
                        every other screen is the 800 x 532 buffer, centred
```

## Files

- `engine/include/resources.h`, `engine/resources.c` -- the `render` block:
  `ui_scale`, `native_w` / `native_h`, parsed and validated together
- `src/layout.h` -- bands as multiples of `CL_UI`; `CL_CONTENT_*`,
  `CL_PANEL_*`, `CL_PANE_BASE_*`, `CL_SCREEN_BASE_*`
- `src/layout.c` -- `layout_grow_native`, `layout_fit_window`,
  `layout_min_window`
- `src/present.h`, `src/present.c` -- `present_scale`, `present_max_scale`,
  `present_refit`, `present_allow_growth`
- `src/ui.h`, `src/ui.c` -- `ui_blit`, `ui_blit_mirrored`
- `src/bfont.h`, `src/bfont.c` -- `BFONT_SRC_GLYPH_*` measured off the strip;
  `BFONT_GLYPH_*` the on-screen size
- `src/map_render.c` -- centred tile grid, black remainder
- `src/combat_render.h` -- a cell one tile, the field centred
- `src/views_render.c` -- views drawn into the content rect

## Verifying

`./build/debug/openbounty --pack glory-of-rome --gallery <dir>` has drawn
every modern screen into `<dir>` as a PNG with no input, and checked that a
tap reaches the rows each screen drew. Inspect every affected PNG.

`tools/capture.sh` has driven a running window and pulled frames out with
`import`; `tools/walkthrough.sh` has played a fresh game from character select
to the overworld and captured every view a key can reach. Three things have
had to be right or those captures lie:

- Keys have been sent with `xdotool key --window <wid>`, not to the focused
  window. This X server has had no window manager, so `windowactivate` has
  failed and every key has been dropped in silence; the capture has then
  returned the same frame repeatedly.
- `--delay 200`, or the press and release have landed inside one 60fps frame
  and `IsKeyPressed` has never fired.
- The window has sat at the origin, or `import` has returned a clipped image
  when part of it hangs off the display.

Byte-comparing captures has not been a regression gate: the army roster,
puzzle, world map, spells and controls screens have animated, so two runs of
the same build have differed on five screens. Compare layout by eye. Legacy's
gate has been `make test`.

## Not defects

- **The world map's tall narrow rectangle.** It has drawn the map it is
  given. Every King's Bounty zone has been 64 x 64, so its minimap has been
  square; Rome's `italia` has been 64 x 128, so a square-pixel minimap has
  been twice as tall as it is wide.
- **`src/pack_select.c`.** Its own 640 x 400 screen, drawn before a pack has
  been chosen and therefore before any layout has existed. Outside this
  system.
- **The black column under the sidebar.** Five panels have not filled a tall
  pane.
