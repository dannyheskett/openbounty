# Modern resolution

How a `modern` pack fills a modern display, and what is still outstanding.

## The problem

Modern mode originally scaled one thing: the map tile. Everything else is fixed
pixel furniture authored for a 320x200 buffer with an 8px font -- the chrome
bands, the status strip, the dialog panel, the view rects, the combat cell. On a
1920x1040 window Rome's buffer became 957x520, the tiles grew, and nothing else
did: a hairline frame, an 8px status font, a 30-column dialog stretched across
900px, and a combat field pinned to the top-left at its original 288x170.

Two independent causes:

**Furniture did not scale.** `BFONT_GLYPH_W/H` and the `CL_FRAME_*`,
`CL_STATUS_H`, `CL_BAR_H`, `CL_PANEL_*` constants were literals. A pack that
doubled its tile had no way to say "double my furniture too."

**Fixed content was sized from the map pane.** `VIEW_W` was `CL_MAP_W` and
`CL_PANEL_W` was `CL_MAP_W + 5`. In legacy those coincide. Once the pane grew
with the window it dragged fixed-column content along with it.

## The model

Three quantities, each with one job.

| | what it does | who sets it |
|---|---|---|
| `tile_w` / `tile_h` | the size of one map tile in buffer pixels | the pack |
| `ui_scale` | multiplies the font and the chrome bands | the pack |
| present scale | how many screen pixels one buffer pixel becomes | the player |

The buffer is the window divided by the present scale. The chrome frame therefore
reaches the window edge instead of a small image being centred in black.

The map pane is whatever is left inside the chrome. It holds as many whole tiles
as fit; the sub-tile remainder stays black inside the frame, and the tile grid is
centred so the remainder splits evenly. A partial tile is never drawn.

Fixed-size screens -- the character sheet, the army list, the dialog panel, the
combat field -- do not use the pane. They use the **content rect**: 240x170
design units times `ui_scale`, centred in the pane. Combat likewise uses a
6x5 grid of one-tile cells, centred.

Wide screens (character, army, gate, end game) use the content rect plus a
sidebar's width, centred in the chrome interior -- the same construction
`CL_COMBAT_X` uses. Centring those in the *pane* would put legacy at x=-8,
because the view is wider than the pane by exactly the sidebar.

### Art scales to its slot, never to itself

Both packs author every asset in the legacy 320x200 design space: 48x34 tiles
and troops, 240x102 location backdrops, 96x102 class portraits, a 288x184 class
picker. Drawing at `tex.width`/`tex.height` therefore means drawing at 1x inside
a buffer that has been scaled up -- which is what left combat as small sprites in
a field of black gutters and the location backdrop as a postage stamp in the
corner of the pane.

The rule is that art fills the slot that holds it: `CL_TILE_W/H` for anything in
a map or combat cell, its design size times `CL_UI` for everything else. Use
`ui_blit` (`src/ui.h`) rather than hand-rolling the `DrawTexturePro` call.

### Every render-target loop must re-fit

The buffer is derived from the window, so it is stale the moment the window is
resized. `present_refit` (`src/present.h`) re-fits and reallocates; all seven
loops that own a target call it at the top of their frame. The startup loop not
calling it is why a resized window used to clip the class-select screen top and
bottom: `main.c` fits once at launch, and nothing refit again until the main loop
took over.

The pack's `tiles_w`/`tiles_h` are the viewport it opens with, not a fixed size.

### The scale is always a number, and 1x is 1:1

There is no Auto. The two jobs are split cleanly:

**The window decides how much you see.** Resize it and the viewport gains or
loses whole tiles. Nothing about the image gets bigger.

**The scale decides how big a pixel is.** At 1x -- the default -- one buffer
pixel is one screen pixel and the pack renders at the resolution it was authored
for. 2x, 3x and up are for a 4K or 8K panel where 1:1 is too small; they never
resize the window, so a higher scale means bigger pixels and fewer tiles.

Auto used to pick the scale by measuring what fit. It existed because `ui_scale`
did not: when the chrome could not grow, blowing up the whole buffer was the only
way to make anything bigger. Now that a pack sizes its own furniture, an
auto-scale on top of that enlarged everything twice -- Rome rendered a 96px tile
at 192px on screen, from a 48x34 source.

The menu cycles 1x up to `present_max_scale()`, the largest scale at which the
window still holds the minimum viewport (`CL_TILES_MIN`, 5x5, matching what
`FogReveal` uncovers), then wraps. Wrapping at the measured maximum rather than a
constant is what stops the label reading `4x` while the renderer clamps to `2x`.

### Legacy is unchanged by construction

`ui_scale` is 1 for a legacy pack, so every converted constant evaluates to the
literal it replaced. The content rect is 240x170 and the legacy pane is 240x170,
so both centring offsets are zero and it lands on the historic 16,22. The combat
grid is 288x170 in a 288-wide interior and a 170-tall map band, so both of its
offsets are zero and it lands on 16,22 as well. `layout_fit_window` returns
immediately for legacy.

## Worked example

Rome on a 1920x1080 panel, maximised to a 1920x1040 client area.
Tile 96x68, `ui_scale` 4, scale 1x.

```
buffer                   1920 x 1040            (the window itself)
chrome                   frame 64, status 36, bar 20
pane                     1920 - 64 - 96 - 64 = 1696
                         1040 - 32 - 36 - 20 - 32 = 920
viewport                 1696/96 = 17 odd       920/68 = 13 odd
on screen                tiles 96x68, font 32px, frame 64px
```

At 2x the same window gives a 960x520 buffer and a 7x5 viewport: the same
picture, twice the size, less of the map.

## Files

- `engine/include/resources.h`, `engine/resources.c` -- `render.ui_scale`, parsed
  and validated with the rest of the render block
- `src/layout.h` -- bands as multiples of `CL_UI`; `CL_CONTENT_*`; `CL_PANEL_*`
- `src/layout.c` -- `layout_fit_window`
- `src/ui.h`, `src/ui.c` -- `ui_blit`, `ui_blit_mirrored`
- `src/present.h`, `src/present.c` -- `present_refit`
- `src/bfont.h`, `src/bfont.c` -- `BFONT_SRC_GLYPH_*` is measured off the strip;
  `BFONT_GLYPH_*` is the on-screen size
- `src/present.c` -- `present_scale`, `present_max_scale`
- `src/map_render.c` -- centred tile grid, black remainder
- `src/combat_render.h` -- cell is one tile, field centred
- `src/views_render.c` -- views draw into the content rect

## Verifying

`tools/capture.sh` drives a running window and pulls frames out with `import`.
`tools/walkthrough.sh` plays a fresh game from character select to the overworld
and captures every view a key can reach.

Three things must be right or captures lie:

- Keys must be sent with `xdotool key --window <wid>`, not to the focused
  window. This X server has no window manager, so `windowactivate` fails and
  every key is dropped in silence -- the capture then returns the same frame
  repeatedly and the screens read as verified when nothing was.
- `--delay 200`, or the press and release land inside one 60fps frame and
  `IsKeyPressed` never fires.
- The window must sit at the origin, or `import` silently returns a clipped
  image when part of it hangs off the display.

Byte-comparing captures is NOT a valid regression gate: the army roster, puzzle,
world map, spells and controls screens all animate, so two runs of the *same*
build differ on five screens. Compare layout by eye.

Legacy gate: `make test` (281 tests) and
`./build/debug/openbounty --validate-pack 0 4` on kings-bounty, which must stay
PASS 5/5, 368 days, 8853.

## Not defects

- **The world map's tall narrow rectangle.** It is drawing the map it was given.
  Every King's Bounty zone is 64x64, so its minimap is square; Rome's `italia` is
  genuinely 64x128, so a square-pixel minimap is correctly twice as tall as it is
  wide. (The `# Italia -- 40x64` header comment in `italia.dat` is stale and is
  what originally made this look like a bug.)
- **`src/pack_select.c`.** Its own 640x400 `DrawText` screen, drawn before a pack
  is chosen and therefore before any layout exists. Outside this system.
- **The black column under the sidebar.** Five stacked panels do not fill a tall
  pane. Accepted.

## Verified by capture

Rome at 1920x1040, and again at 900x620 to exercise the minimum-viewport
fallback: character, army, contract, puzzle, spells, controls, options, world
map, overworld, class select, dwelling/recruit, town, own castle, combat
(including a castle siege), the end cartoon and the end-game win screen.

Legacy is byte-identical to its pre-change baseline on every screen that does
not animate.

## Outstanding

- Three screens still unseen: the alcove, the home-castle throne room, and the
  end-game lose screen. Each is built from two pieces that ARE verified --
  `draw_location_backdrop` (seen in the dwelling, town and own-castle captures)
  and the `CL_PANEL_*` rect (seen in every dialog) -- so they are correct by
  construction, but nobody has looked at them.
