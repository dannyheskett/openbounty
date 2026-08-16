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
| present scale | multiplies the whole buffer onto the window | Auto, or the player |

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

The pack's `tiles_w`/`tiles_h` are the viewport it opens with and a floor for the
Auto scale to respect, not a fixed size.

### Auto

Auto is the largest integer scale at which the *minimum* viewport (`CL_TILES_MIN`,
5x5, matching what `FogReveal` uncovers) still fits the window. It measures
against the minimum rather than the pack's declared viewport on purpose: measuring
against the declared one makes `ui_scale` self-cancelling, because doubling the
chrome grows the space the declared viewport needs, which drops the scale back to
1 and leaves the tiles exactly as small as they started.

2x and 3x are manual overrides for a 4K or 8K panel. They never resize the window.

### Legacy is unchanged by construction

`ui_scale` is 1 for a legacy pack, so every converted constant evaluates to the
literal it replaced. The content rect is 240x170 and the legacy pane is 240x170,
so both centring offsets are zero and it lands on the historic 16,22. The combat
grid is 288x170 in a 288-wide interior and a 170-tall map band, so both of its
offsets are zero and it lands on 16,22 as well. `layout_fit_window` returns
immediately for legacy.

## Worked example

Rome on a 1920x1080 panel, maximised to a 1920x1040 client area.
Tile 96x68, `ui_scale` 2.

```
minimum viewport needs   32 + 96*5 + 96 + 32   =  640 wide
                         60 + 68*5             =  400 tall
auto scale               min(1920/640, 1040/400) = 2
buffer                   960 x 520
pane                     960 - 64 - 96 = 800    520 - 60 = 460
viewport                 800/96 = 8 -> 7 odd    460/68 = 6 -> 5 odd
on screen                tiles 192x136, font 32px, frame 64px
```

## Files

- `engine/include/resources.h`, `engine/resources.c` -- `render.ui_scale`, parsed
  and validated with the rest of the render block
- `src/layout.h` -- bands as multiples of `CL_UI`; `CL_CONTENT_*`; `CL_PANEL_*`
- `src/layout.c` -- `layout_fit_window`, `layout_auto_scale`
- `src/ui.h`, `src/ui.c` -- `ui_blit`, `ui_blit_mirrored`
- `src/present.h`, `src/present.c` -- `present_refit`
- `src/bfont.h`, `src/bfont.c` -- `BFONT_SRC_GLYPH_*` is the 8x8 strip;
  `BFONT_GLYPH_*` is the on-screen size
- `src/present.c` -- Auto and the 2x/3x override
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

## Outstanding

- Screens still not captured because autoplay does not reliably reach them:
  town, home castle, own castle, alcove, end game win/lose, end cartoon. Their
  code went through the same conversion as the dwelling screen, which is
  verified, but they have not been seen.
