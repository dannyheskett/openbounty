# Design specification (modern mode)

Reproduction-grade record of the modern-mode interface as built: every
screen, size, colour, rule and input behaviour a `modern` pack (Glory of
Rome) has been drawn and driven with. Legacy mode (King's Bounty) has been
outside this document; `OPENBOUNTY-SPEC.md` and
`tests/unit/test_legacy_freeze.c` have held it.

**Conventions.**
- Each requirement has carried a stable identifier of the form `DSGN-NNNN`.
- Requirements have been written in the present perfect tense ("the frame has
  been 12 px thick"), as a factual record of the code.
- Code citations have named a file and a function, never a line number.
- Where a value has come from the pack, the requirement has named its JSON
  path (`game.json:render.tile_w`, `strings/en.json:menu.items.gm_back`)
  rather than copying the value. A path written without a file has been a key
  of `strings/en.json`; every `game.json` path has carried its file.
- Sizes have been in game pixels: the pixels the screen has been laid out in,
  before the whole-number zoom of DSGN-0006.

**Symbols.**
- `TW`, `TH`: the tile, `game.json:render.tile_w` and `game.json:render.tile_h`
  (`CL_TILE_W`, `CL_TILE_H`).
- `UI`: `game.json:render.ui_scale` (`CL_UI`).
- `F`: the frame, `RES_MODERN_FRAME` (12) × `UI`.
- `G`: the band between a column and the map, `RES_MODERN_GAP` (4) × `UI`.
- `NW` × `NH`: the smallest screen, `game.json:render.native_w` ×
  `game.json:render.native_h`.
- `W` × `H`: the screen (DSGN-0005).
- `GH`: the text line height, `BFONT_GLYPH_H` (DSGN-0092). `GW`: the text
  advance, `BFONT_GLYPH_W`. Both have been measured from the face when it has
  loaded; `--gallery` has written them into `pages.txt` (see Verifying).
- `L`: the line pitch, `uk_line_h()` = `GH` + 2.
- `R`: the row height, `ml_row_h()` (DSGN-0041). `P` = `R` + 2, the row
  pitch.
- `ring`: `page_ring()` = `F`, a floating page's ring and the gap outside it.
- Colour names (`YELLOW`, `WHITE`, `DGREY`, `RED`, `GREEN`, `CYAN`, `BLACK`)
  have been palette indices (DSGN-0100).

---

## 1. Screen and scale

**DSGN-0001. The render block.** A modern pack has declared its geometry in
`game.json:render`: `mode`, `tile_w`/`tile_h` (default 96), `tiles_w`/`tiles_h`
(default 7), `ui_scale` (default 1), `native_w`/`native_h` (default 0: no
declared screen) and `dim` (default 55, clamped to 0..100). The loader has
rejected a render block whose `tiles_w` or `tiles_h` has been even or below 3,
whose tile has been under 8 px, or whose `ui_scale` has been outside 1..8.
`tiles_w` × `tiles_h` has also been the fog reveal round the hero
(`FogRevealFor`).
Source: `engine/resources.c` `resources_load`; `engine/include/resources.h`
`ResRender`; `engine/fog.c` `FogRevealFor`.

**DSGN-0002. The smallest screen's floor.** The loader has rejected a declared
screen smaller than the frame, both one-tile columns with their bands, and the
wider of the viewport and the battlefield:
`need_w` = max(`tiles_w`, `COMBAT_W` 6) × `TW` + 2 × (`TW` + `G`) + 2 × `F`;
`need_h` = max(`tiles_h`, `COMBAT_H` 5) × `TH` + 2 × `F`.
Source: `engine/resources.c` `resources_load`; `engine/include/combat.h`.

**DSGN-0003. Screen composition.** Across the screen the layout has been
frame, left column, band, map, band, right column, frame; down it, frame,
map, frame. There has been no status band and no bar (`no_band`, so
`CL_STATUS_H` = `CL_BAR_H` = 0).
- Frame `F` on all four sides; each column `TW` wide; each band `G`.
- `CL_RAIL_X` = `F`; `CL_MAP_X` = `F` + `TW` + `G`;
  `CL_MAP_W` = `W` − 2 × (`F` + `TW` + `G`);
  `CL_SIDEBAR_X` = `CL_MAP_X` + `CL_MAP_W` + `G`.
- `CL_MAP_Y` = `CL_RAIL_Y` = `CL_SIDEBAR_Y` = `F`;
  `CL_MAP_H` = `CL_RAIL_H` = `CL_SIDEBAR_H` = `H` − 2 × `F`.
Source: `src/layout.c` `layout_init`, `native_fit`; `src/layout.h`.

**DSGN-0004. Whole map tiles.** The layout has counted an odd number of whole
tiles on each axis, never fewer than the pack's `tiles_w` × `tiles_h`:
`tw` = odd_clamp(`CL_MAP_W` / `TW`), `th` = odd_clamp(`CL_MAP_H` / `TH`), where
odd_clamp has turned an even count into the odd one below it and clamped to
`CL_TILES_MIN` 5 .. `CL_TILES_MAX` 63. At 63 tiles the map area has been capped
at 64 tiles and the rest of the surface has been letterboxed.
Source: `src/layout.c` `native_fit`, `odd_clamp`.

**DSGN-0005. Screen size.** The screen has been the safe surface divided by
the zoom (integer division), floored at the smallest screen:
`W` = max(`NW`, `safe_w` / `z`), `H` = max(`NH`, `safe_h` / `z`). All growth has
gone to the map; frame, columns and bands have kept their size.
Source: `src/layout.c` `layout_grow_native`; `src/present.c` `present_refit`.

**DSGN-0006. Zoom.** The zoom has been the largest whole number of times the
smallest screen has fitted the safe surface, from 1 to `CL_SCALE_MAX_NATIVE`
3: `fit` = clamp(min(`safe_w` / `NW`, `safe_h` / `NH`), 1, 3).
- iOS, Android and the web build have taken `fit` on every frame.
- A desktop window has held its zoom while an edge has been dragged: the zoom
  has risen to `fit` only on the first frame, while the window has been the
  size the game gave it (DSGN-0010), or on the frame its maximised or
  full-screen state has changed; it has fallen to `fit` whenever `fit` has
  been below it.
- The render target has been `W·z` × `H·z`, point-filtered, with every draw
  inside a zoom camera, so design coordinates have not changed with `z`. The
  font atlas has been re-baked at `game.json:font.size` × `z`. There has been
  no player scale control.
- Every loop that has owned the render target (the main frame, combat,
  start-up, the end cartoon, the autoplay and encoding screens) has called
  `present_refit` at the top of its frame, so a resized window or a turned
  phone has been re-fitted on the next frame; `present_layout_changed` has
  reported a frame whose screen changed.
Source: `src/present.c` `held_zoom`, `present_scale`, `present_refit`,
`present_layout_changed`, `present_begin`; `src/text.c` `text_set_zoom`;
`src/shell_frame.c` `shell_present_frame`; `src/combat_loop.c`
`combat_present`; `src/startup.c` `frame_begin`; `src/end_cartoon.c`
`run_end_cartoon`; `src/shell_autoplay.c` `draw_processing`;
`src/encode_dialog.c` `frame_begin`.

**DSGN-0007. Blit.** Each frame the window has been cleared to black and the
target blitted 1:1, centred in the safe rect. On a surface smaller than the
smallest screen (a browser window can be), `present_fit_down` has shrunk the
target to fit with its shape kept, and the target has been filtered smoothly
for as long as it has been fitted down, point-filtered again after. On mobile
the blit has been multiplied by `present_fit_multiple`, the largest whole
number that has fitted.
Source: `src/present.c` `present_scaled`, `present_fit_down`,
`present_fit_multiple`; `src/gfx_raylib.c` `gfx_texture_smooth`.

**DSGN-0008. Safe area.** The surface has been the window less the Android
cutout and gesture insets; an axis whose remainder has been 0 or less has
ignored its insets. Desktop, web and iOS have had zero insets (the iOS view
has been the safe rect).
Source: `src/present.c` `safe_rect`; `src/safe_area.c` `safe_area_get`;
`ios/ios_main.mm` `updateDrawableSize`.

**DSGN-0009. Window to game pixels.** A window position has been mapped
through the last blit rect: `sx` = (`wx` − `dst_x`) × `W` / `dst_w`, and `sy`
likewise; a length has converted as `len` × `W` / `dst_w`. A tap in the
letterbox has been ignored.
Source: `src/present.c` `present_window_to_screen`,
`present_window_len_to_design`.

**DSGN-0010. The desktop window.** The window has opened resizable at the
smallest screen. Once it has existed, `frame_host_window_room` has measured
the monitor's work area less the window's decorations; when that room has
held a larger whole multiple of the smallest screen, the window has been
resized to exactly that multiple and centred. The size the game has last
given the window has been remembered (`frame_host_window_at_set_size`).
`--window WxH` and `--fullscreen` have skipped the resize. The minimum window
size has been the smallest screen. Alt+Enter has toggled fullscreen. The
window has been created with a 4x MSAA hint (not on Android), a hidden
cursor, 60 fps and no exit key. Android has opened at the display; iOS has
reported its view.
Source: `src/main.c` `main`; `src/frame_host.c` `frame_host_window_open`,
`frame_host_window_room`, `frame_host_window_place`,
`frame_host_window_at_set_size`, `frame_host_window_min_size`;
`src/layout.c` `layout_min_window`.

---

## 2. Frame, bands and lattice

**DSGN-0011. Draw order of the base screen.** Each frame on the map has drawn,
in order: a black clear; the chrome (a black fill, the frame and the two
bands); the map, clipped to its area; the right column; the left column; then
the overlays in one order: the view or menu page, the question, the message,
and the toast last. A battle has drawn its own base screen (DSGN-0109).
Source: `src/shell_frame.c` `draw_frame`; `src/overlay.c` `overlay_draw`.

**DSGN-0012. Chrome.** `chrome_draw` has filled the screen black and drawn the
frame as a lattice ring `F` thick on every side (a pack bitmap in
`game.json:sprites.ui.chrome_overworld` in its place when declared), then two
vertical lattice bands `G` wide and `CL_MAP_H` tall at
x = `CL_RAIL_X` + `TW` and x = `CL_MAP_X` + `CL_MAP_W`. No status text has been
drawn.
Source: `src/chrome.c` `chrome_draw`, `draw_base_chrome`, `draw_frame`.

**DSGN-0013. Ring anatomy.** A lattice ring has been, from the outside in: a
1-unit rail (232,186,60), a 1-unit ink line (14,10,6), the pattern, a 1-unit
ink line and a 1-unit rail against the content. The ink lines have been drawn
only when every side has been at least 6 units.
Source: `src/lattice.c` `lattice_ring`.

**DSGN-0014. Bands.** A band narrower than 6 units has been two rails with the
pattern between them and no ink.
Source: `src/lattice.c` `lattice_band_v`, `lattice_band_h`.

**DSGN-0015. Lattice pattern.** The pattern has been an 8-unit cross-hatch
(`LATTICE_PITCH` 8 × `UI`), built once and tiled from the screen origin, so
every band has shared one grid. For cell (`ux`, `uy`), with
`a` = (`ux` − `uy`) mod 8 and `b` = (`ux` + `uy`) mod 8: both 0, bright
(250,222,104); either 0, gold (206,160,42); either 1, shade (118,82,22);
otherwise wood (46,30,16). `lattice_ground` has filled plain wood.
Source: `src/lattice.c` `cell_colour`, `build`, `tile`, `lattice_ground`.

---

## 3. The map

**DSGN-0016. Map area.** The map has filled the whole space between the bands
(`CL_MAP_X`, `CL_MAP_Y`, `CL_MAP_W`, `CL_MAP_H`), cleared to palette index 0 and
scissored (the scissor multiplied by the zoom), so no tile has spilled out.
Source: `src/map_render.c` `map_render_draw`.

**DSGN-0017. Whole and part tiles.** The hero's cell has been centred across
the map, at `hx` = `CL_MAP_X` + (`CL_MAP_W` − `TW`) / 2, on row
`hr` = (`CL_MAP_H` / 2) / `TH` counted from the map's top, so the rows have
stood flush with the columns' tiles. Every cell the area has shown has been
drawn: part tiles at the left and right edges and a part row at the foot.
Every cell has been drawn at `TW` × `TH`.
Source: `src/map_render.c` `map_view`, `map_render_draw`.

**DSGN-0018. Camera.** The camera has never clamped: the hero has always
stood on that centre cell, and cells past the world's edge have not been
drawn (dark).
Source: `src/map_render.c` `map_view`, `map_render_hero_cell`.

**DSGN-0019. Map contents.** Each seen cell has drawn its ground under any
object or landmark, then its art (a cosmetic variant where the pack has
declared variants, chosen per cell from the game's seed); then the idle
boat, then the hero (the boat when sailing, the lead troop mirrored west when
flying, rocking between frames 0 and 1 when still), then fog-edge strips:
three strips black at alpha 128, 64 and 32, `TW` / 24 px wide on west and east
edges and `TH` / 17 px on north and south. `map_render_cell` has been the one
way a map cell has been drawn: the map, the gate's preview and the puzzle.
Source: `src/map_render.c` `map_render_draw`, `map_render_cell`;
`src/tilevar.c` `tilevar_art`, `tilevar_seed`; `src/shell_frame.c`
`draw_frame`.

**DSGN-0020. Map taps.** The whole map has been one tap region: a tap has
stepped the hero one cell toward the finger, the direction taken per axis
from the hero's cell; a tap on the hero's own cell has done nothing. Holding
has repeated the step after 0.35 s and then every 0.15 s, each repeat
re-resolved at the finger. The map has taken no part in the near-miss pass.
Source: `src/main.c` `map_tap_cell`; `src/uitouch.c` `ui_map`; `src/touch.c`
`map_region_key`, `touch_frame`.

---

## 4. The columns

**DSGN-0021. Column geometry.** Each column has been five rows `TW` × `TH`
stacked from `CL_MAP_Y`, each row's art stretched to its cell. Below the
fifth row the column has been lattice ground, `CL_MAP_H` − 5 × `TH` tall.
Source: `src/modern/rail.c` `rail_draw`; `src/hud.c` `hud_draw`, `blit_tile`.

**DSGN-0022. Joins.** A 2 × `UI` px band has crossed every edge of every row
(at y + i × `TH` − `UI` for i = 0..5), half on each side, the column's top and
the fifth row's foot included, so every row has given the same share of its
art. No row has had a frame of its own.
Source: `src/hud.c` `hud_column_finish`.

**DSGN-0023. Left column.** The left column has held, top to bottom: Menu
(`game.json:sprites.rail.menu`, the game menu), Map
(`game.json:sprites.rail.map`, the world map), Army
(`game.json:sprites.rail.army`, the army sheet), Search
(`game.json:sprites.rail.search`, a search) and Puzzle (drawn by
`hud_draw_puzzle_tile`, the puzzle). Each row has fired
exactly the action its key has fired.
Source: `src/modern/rail.c` `ROWS`, `rail_draw`, `rail_tapped`;
`src/shell_actions.c` `shell_dispatch_action`.

**DSGN-0024. Right column.** The right column has held, top to bottom:
Contract (the silhouette, overlaid by the active villain's animation or
portrait; the contract), Siege (the silhouette, or its animation once siege
weapons have been owned; the character sheet), Magic (the silhouette, or its
animation once the zone's rites have been known; cast a spell), Gold (the
purse and the gold figure; the character sheet) and Days
(`game.json:sprites.hud.days`, the sundial and the days figure; the character
sheet). Animations have run at 2 frames a second of `ui_anim_time`, held at
frame 0 under `--gallery`.
Source: `src/hud.c` `hud_draw`, `contract_tile`, `siege_tile`, `magic_tile`,
`days_tile`, `HUD_ACTIONS`, `hud_tapped`.

**DSGN-0025. Figures on tiles.** The gold and the days have been drawn alike,
with no box, right-aligned 2 × `UI` from the tile's right and 2 × `UI` above
its foot (y = tile top + `TH` − `GH` − 2 × `UI`). A figure wider than
`TW` − 4 × `UI` has been shortened to "Nk" (thousands) or, from a million,
"Nm".
Source: `src/hud.c` `tile_number`, `gold_tile`, `days_tile`.

**DSGN-0026. Days and Time Stop.** The days left have been `YELLOW`; while
Time Stop has been running, the steps it has left have been drawn in `CYAN`
in their place. The days have started at
`game.json:time.days_per_difficulty`.
Source: `src/hud.c` `days_tile`.

**DSGN-0027. Puzzle tile.** The puzzle tile has been the grid art with a
cover chip (`game.json:sprites.ui.puzzle_cover`) over every piece not yet won,
laid out 5 × 5 by the engine's `puzzle_grid_entity` (the layout the puzzle
sheet has used). Each cell has been exactly one chip at the chip's own size
times the largest whole multiple `k` at which five chips fit the tile both
ways, `k` = max(1, min(`TW` / (5 × chip width), `TH` / (5 × chip height))),
the grid centred on the tile, so the covers tile edge to edge. Glory of Rome
has authored its chip 18 × 18 (ART-SPEC), five to a 90 px grid on the 96 px
tile.
Source: `src/hud.c` `hud_draw_puzzle_tile`; `engine/tables.c`
`puzzle_grid_entity`.

**DSGN-0028. Tile key hints.** While the keyboard has been the last thing used
(DSGN-0077) and no page has been open, every column row has shown its key in
its top-left corner: a `uk_hint_bg` (0,0,0,190) box at (x + 2 × `UI`,
y + 2 × `UI`), text width + 4 × `UI` wide and `GH` + 4 × `UI` tall, with the
key in `YELLOW` at (x + 4 × `UI`, y + 4 × `UI`). The left column's keys have
been `strings/en.json:ui.key_esc`, M, A, S, P; the right column's I, V, U,
V, V.
Source: `src/hud.c` `hud_key_hint`, `HUD_KEYS`; `src/modern/rail.c` `ROWS`.

**DSGN-0029. Column taps.** Each row has been a tiled tap target exactly its
drawn rect, registered only while no view, message or question has been up.
The right column has registered before the left, the order the frame has
drawn them in.
Source: `src/modern/rail.c` `a_page_is_open`, `rail_draw`; `src/hud.c`
`hud_draw`; `src/uitouch.c` `ui_tile_row`.

---

## 5. Pages: the engine

**DSGN-0030. The space inside the frame.** Every page has been measured
against the space inside the frame, `in` = (`F`, `F`, `W` − 2 × `F`,
`H` − 2 × `F`). On bare frames (the pre-game screens) it has been the whole
screen.
Source: `src/modern/page.c` `page_interior`, `page_bare`.

**DSGN-0031. Per-frame reset.** Every frame has started with no page open,
not bare and no battlefield (`page_frame_begin`, called from
`present_begin`).
Source: `src/modern/page.c` `page_frame_begin`; `src/present.c`
`present_begin`.

**DSGN-0032. Three kinds of page.** Every page has been one of three kinds,
each with one size on a screen, taken from the pack's declared screen and
frame (DSGN-0040):
- A full page (a place, a sheet, the world map, the spells) has been the
  space inside the smallest screen's frame. It has floated when `in` has held
  it with its ring and a gap on every side, `in.w` ≥ `w` + 4 × `ring` and
  `in.h` ≥ `h` + 4 × `ring`, and filled `in` otherwise; every full page has
  done the same on the same screen (`page_full_floats`).
- A menu page (a menu, a list, a count, a question with more than two
  answers) has been sized to float on every screen.
- A message (a message, a question with two answers) has stood on the foot
  of the map, or of the battlefield in a fight, and has floated on every
  screen.
Source: `src/modern/page.c` `fits`, `page_full_floats`, `open_page`.

**DSGN-0033. A floating page.** A floating page has sat at its size, centred
in its area or on its foot, its content filled with `uk_fill` (12,14,30)
inside a lattice ring `ring` thick (outer rect x − `ring`, y − `ring`,
w + 2 × `ring`, h + 2 × `ring`).
Source: `src/modern/page.c` `open_page`; `src/modern/uikit.c` `uk_fill`.

**DSGN-0034. A filling page.** A full page that has not floated has painted
all of `in` with `uk_fill` and has had no ring of its own (the frame has been
its border; on bare frames it has had none). Its content has sat centred in
`in` at its size, cut to `in` when larger.
Source: `src/modern/page.c` `open_page`.

**DSGN-0035. Pages on a foot.** A page anchored `PAGE_MAP_FOOT` (area `in`,
across which the map has been centred; on bare frames the whole screen) or
`PAGE_FIELD_FOOT` (area the battlefield of DSGN-0110; without a battlefield
it has behaved as `PAGE_CENTER`) has always floated: centred across its area,
its content's foot 2 × `ring` above the area's foot. When larger than the
area less 2 × `ring` on each side it has shrunk to that size and its rows
have scrolled.
Source: `src/modern/page.c` `open_page`.

**DSGN-0036. Dim.** A floating page has first drawn (0,0,0,`a`) over what has
been behind it: the outer rect of the page opened just before it in the same
frame, or the whole screen when none, so the screen behind has been dimmed
once. `a` = clamp(`game.json:render.dim`, 0, 100) × 255 / 100; 0 has dimmed
nothing. A filling page, the class caption (DSGN-0075) and the toast
(DSGN-0068) have dimmed nothing.
Source: `src/modern/page.c` `dim`, `open_page`; `src/overlay.c`
`overlay_dim_alpha`.

**DSGN-0037. Layering.** Pages opened in one frame have been layered in call
order; at most `PAGE_DEPTH` 4 have been recorded, and a fifth page has been
drawn and has set its tap rule all the same.
Source: `src/modern/page.c` `open_page`, `page_stack`.

**DSGN-0038. A page's taps.** The last page opened in a frame has owned the
tap rule, and has been modal: no region registered before it has taken a tap.
- A page with a single action (`tap_key` set: its one row is Continue, or it
  is read and closed) has pressed that key for a tap anywhere on the screen.
- A page with choices (`tap_key` 0) has done nothing for a tap inside it, and
  has pressed its exit key for a tap outside it: on the dimmed screen round a
  floating page, on the frame round a filling one.
- The title menu has had no exit key: a tap off it has done nothing.
- The bridge's message has been the one page that has not been modal: the map
  round it has taken taps, and a tap elsewhere off it has done nothing.
Source: `src/modern/page.c` `open_page`; `src/touch.c` `touch_page`,
`under_page`, `page_key`.

**DSGN-0039. The world under a page.** While a view, a message or a question
has been up, both columns have been drawn but have shown no key hints and
registered no taps.
Source: `src/modern/rail.c` `a_page_is_open`, `rail_draw`; `src/hud.c`
`hud_draw`.

**DSGN-0040. Page sizes.**
- `ring` = `F`.
- A full page: `NW` − 2 × `F` wide, `NH` − 2 × `F` tall
  (`page_full_w`, `page_full_h`).
- A menu page: a full page less 4 × `ring` across (`page_menu_w`); tall
  enough for `PAGE_MENU_ROWS` 7 rows and the exit at the device's row height
  under a title strip and a two-line description, never shorter than a full
  page less 4 × `ring` and never taller than `in.h` − 4 × `ring`
  (`page_menu_h`).
- A message: the smallest screen's map width less 4 × `ring` (`page_msg_w`),
  as tall as what it has held.
- `PAGE_MSG_LINES` 6, `PAGE_FOE_CARD_LINES` 5.
Source: `src/modern/page.h`; `src/modern/page.c` `page_ring`, `page_full_w`,
`page_full_h`, `page_menu_w`, `page_menu_h`, `page_msg_w`.

---

## 6. The pieces of a page

**DSGN-0041. Row height.** A row and its rule have been half a tile, two rows
to a tile: `R` = `TH` / 2 − `ML_ROW_RULE`, never less than `GH` + `ML_PAD`. On
a touch device (DSGN-0078) a row has been at least two thirds of a tile:
`R` ≥ 2 × `TH` / 3 − `ML_ROW_RULE`. The device has been fixed for the session,
so `R` has been one height from the first frame to the last.
Source: `src/modern/mlayout.c` `ml_row_h`; `src/input_host.c`
`input_touch_device`.

**DSGN-0042. Row stacking.** Rows have stacked from the top of their space
with a 2 px lattice rule (`ML_ROW_RULE`) under every row, the last included,
and have never stretched to fill the space. `ml_list_height(n)` = `n` × `P`;
`ml_list_fit(h)` = max(1, (`h` + 2) / `P`).
Source: `src/modern/mlist.c` `ml_list_draw_ex`, `ml_list_height`,
`ml_list_fit`.

**DSGN-0043. Row text.** A label has sat `UK_INSET` in from the row's left,
centred down the row (y + (`R` − `GH`) / 2); an optional right-hand text has
ended `UK_INSET` from the right edge. A label too long for the room left of
the right-hand text has been cut with "..". A label holding a newline has
drawn two lines `L` apart, centred down the row as a pair.
Source: `src/modern/mlist.c` `draw_row`.

**DSGN-0044. Row colours.** An enabled row has been `WHITE`, a greyed row
`DGREY`. The cursor on an enabled row has been a `YELLOW` bar with its text in
`uk_ink` (16,18,36); on a greyed row, a `uk_ink` fill with a 1 px `YELLOW`
outline at (x + 1, y + 1, w − 2, h − 2) and `DGREY` text. The right-hand text
has followed the label's colour.
Source: `src/modern/mlist.c` `draw_row`.

**DSGN-0045. Greyed rows.** A greyed row has still registered its tap, which
has moved the cursor there and done nothing else, so a finger that has missed
it has never landed on its neighbour. The keyboard cursor has been able to
land on it, where Enter has done nothing.
Source: `src/modern/mlist.c` `ml_list_draw_ex`, `ml_list_input`.

**DSGN-0046. Exit rows' key.** The row Escape has pressed has been marked by
its own row function (`ml_exit_hint`, or `UkRows.esc`), never found by its
words: while keys have been shown (DSGN-0087) it has shown
`strings/en.json:ui.key_esc` at its right.
Source: `src/modern/mlist.c` `ml_exit_hint`; `src/modern/uikit.c`
`uk_rows_fn`.

**DSGN-0047. Scrolling position.** A list longer than its space has shown its
first rows until the cursor has passed the last visible row, and from then on
the cursor row has been the last visible one:
first = clamp(cursor − visible + 1, 0, count − visible). A cursor of −1 (the
cursor in another column) has counted as 0. Nothing has been remembered
between frames.
Source: `src/modern/mlist.c` `ml_list_first`.

**DSGN-0048. Scroll arrows.** When rows have been hidden above or below, the
edge row has shown a 10 × 8 triangle at x + w − `UK_INSET` / 2 − 6, in `YELLOW`
(`uk_ink` on the lit cursor row). In a touch list each arrow's area has been a
button 3 × `UK_INSET` wide and `R` tall pressing Up or Down, registered before
the rows.
Source: `src/modern/mlist.c` `ml_list_draw_ex`.

**DSGN-0049. Drag scrolling.** A list longer than its space has registered a
scroll region over its visible rows. A drag has started after `DRAG_SLOP` 10
px, and each `P` of travel has injected one Down (finger moving up) or Up, at
most one a frame. A press that has not moved has been resolved as a tap at
release, against the regions of the release frame.
Source: `src/touch.c` `touch_frame`, `scroll_region_at`;
`src/modern/mlist.c` `ml_list_draw_ex`.

**DSGN-0050. Lists in parts.** A list drawn in parts has numbered its rows
from a base (`touch_base`) and has shown no cursor outside the part holding
it: the spells page (bases 0 and 7, the exit 14), a list's foot rows
(`ml_rows_draw`), the world map's Close, and the town's Information page.
Source: `src/modern/mlist.c` `ml_list_draw_ex`, `ml_rows_draw`;
`src/modern/views_render.c` `modern_spells_draw`, `draw_worldmap`;
`src/modern/overlay.c` `modern_overlay_draw_town`.

**DSGN-0051. The title strip.** Every page's title strip has been
`uk_title`, `GH` + 14 tall: the title in `YELLOW` at (x + `UK_INSET`, y + 7), an
optional right-hand text in `YELLOW` ending `UK_INSET` from the right, and a
`UK_BAND` (4 px) lattice band under it. Parts have stood 2 × `UK_INSET` apart.
Source: `src/modern/uikit.c` `uk_title`, `uk_title_h`.

**DSGN-0052. Close in the strip.** A page to read has carried
`strings/en.json:menu.items.gm_close` in `WHITE` at its strip's right, after
the right-hand text, followed by the key for the device last used
(DSGN-0087); the strip's height round it has been a button pressing Escape.
When the strip has been too short, Close has first lost its key, then the
right-hand text has gone, and only then has the title been cut with "..".
Source: `src/modern/uikit.c` `uk_title`; `src/modern/mlist.c`
`ml_hint_text`.

**DSGN-0053. Count buttons.** A count has had seven parts: the least
(`banners.count_min`), −10, −1, the count, +1, +10 and the most
(`banners.count_max`), a button with an empty label skipped.
- Height `R`. The count box: black, a 1 px `WHITE` outline and a `WHITE`
  number.
- Buttons `ML_PAD` apart, a double 1 px `uk_button` (200,160,60) outline and
  `YELLOW` labels, pressing Home, Down, Left, Right, Up and End. The count
  box, or the last button when the count has had a row of its own, has taken
  what the division has left, so the row has ended flush with the bar.
- In one row when every label has cleared its button's edges with the count
  box measure("00000") + 4 × `ML_PAD` wide; otherwise the count has taken a
  full-width row of its own above the six buttons.
- Under them, `ML_PAD` below, an 8 px black bar filled `uk_button` as far as
  count / most, outlined `uk_bar_edge` (90,72,30).
Source: `src/modern/mlist.c` `ml_count_buttons`, `count_value`.

**DSGN-0054. Stepping a count.** Left and Right have stepped a count by 1,
Down and Up by 10, Home to the least and End to the most, clamped to the
range; keys pressed in the same frame have added. A castle count has ranged
1..most and started at the most; a question's count has ranged
(most > 0 ? 1 : 0)..most and started at the most.
Source: `src/modern/mlist.c` `ml_stepper_keys`; `src/modern/castle.c`
`open_stepper`; `src/prompt.c` `prompt_text_input_open`.

**DSGN-0055. How many.** Every count has been one block, `uk_count`: a
`YELLOW` heading (`banners.count_heading`), a `WHITE` line of how many
(`banners.count_of_lead`, `count_of_army` or `count_of_garrison`), an optional
`YELLOW` cost (`banners.count_cost`), `UK_INSET`, then the count buttons
(DSGN-0053). Its answers have been two rows on the foot of the rows column:
the action with the count ("Recruit N", `banners.count_recruit`,
`count_garrison`, `count_withdraw`) and Cancel (`banners.count_cancel`), the
row Escape has pressed.
Source: `src/modern/uikit.c` `uk_count`; `src/modern/overlay.c`
`count_rows`, `modern_overlay_draw_castle`, `modern_overlay_draw_dwelling`;
`src/modern/prompt.c` `modern_prompt_draw`.

**DSGN-0056. Pictures.** Art has never been drawn at a fractional scale.
- `uk_picture`: a black square with the texture drawn into it at a whole
  multiple, and a 1 px `uk_edge` (150,118,48) edge just outside it.
- `uk_picture_cut`: the texture at its own size, cut to its place from its
  top-left, the same edge.
- `uk_figure`: a figure at a whole multiple with its foot on a line; where it
  would pass a given top, its top has been cut in whole pixels of the art,
  never squashed.
Source: `src/modern/uikit.c` `uk_picture`, `uk_picture_cut`, `uk_figure`.

**DSGN-0057. Words.** Every line of words has stood `L` apart, and every cut
has been marked:
- `uk_line`: one line cut to its width with "..".
- `uk_flow`: words from a point, beside a picture while above its foot, then
  the full width, stopping before a floor; the last line that has fitted with
  words still to come has ended "..".
- `uk_lines_draw`, `uk_words_centred`: wrapped to a width (centred on a point
  for the second), at most a given number of lines, a cut marked on the last.
- `uk_foot_rows`: full-width rows along a body's foot with a `UK_BAND` band
  above them.
Source: `src/modern/uikit.c` `uk_mark_cut`, `uk_line`, `uk_flow`,
`uk_lines_draw`, `uk_words_centred`, `uk_foot_rows`, `uk_line_h`.

**DSGN-0058. Formatted words.** Formatted words beside a picture have stayed
in one column, starting `UK_INSET` right of the picture, or under it when
fewer than 16 × `GW` have been left. A paragraph gap has been `L` / 2 + 2; at
most `UK_DOC_PARAS` 32 paragraphs and 4096 bytes; a paragraph's label
(`uk_doc_labeled`) has been `YELLOW`. With a pager, one line has been kept
free at the foot for "n/m" in `YELLOW` and two `GH`-wide arrows, `YELLOW` or
`DGREY` when there has been no page that way, each a button (`GH` + 16) ×
(`GH` + 16) pressing Up or Down. Without a pager the words have been cut on
their last line with "..".
Source: `src/modern/uikit.c` `uk_doc_add`, `uk_doc_gap`, `uk_doc_labeled`,
`uk_doc_draw`.

**DSGN-0059. Backdrop band.** A place's backdrop has been drawn at the largest
whole scale its page's width has allowed, at most 3:
scale = clamp(width / `ML_BACKDROP_W` 240, 1, 3), 720 × 306 on a full page.
- The side bars ((width − 720) / 2) have held a column: the capital, a
  repeated shaft and the base from `game.json:sprites.ui.scene_column_capital`,
  `scene_column_shaft` and `scene_column_base`, at 1× and at the art's own
  width (28 px), standing against the picture's edge, mirrored on the right,
  over `uk_fill`, which has filled whatever a wider bar has left (a scene
  note at 2×, DSGN-0127); without them, the lattice.
- When the band has been shorter than the backdrop, the backdrop's top has
  been trimmed by trim = ceil((306 − band) / 3) × 3, in whole source pixels.
- Black has been drawn behind the art, and a `UK_BAND` divider under it.
Source: `src/modern/uikit.c` `uk_scene_band`, `draw_column`,
`column_piece`.

**DSGN-0060. Figures in a band.** A figure has stood at 2× with its foot on
the band's foot, `x` pixels in from the backdrop's left, its top cut where it
has passed the band's top (DSGN-0056).
Source: `src/modern/uikit.c` `uk_scene_figure`.

**DSGN-0061. Shared measures.** `ML_PAD` 8 (a gap that is not a text inset),
`ML_ROW_RULE` 2, `ML_BACKDROP_W` × `ML_BACKDROP_H` 240 × 102, `UK_BAND` 4,
`UK_INSET` 12 (every text inset), `UK_IDLE_FPS` 1000 / 150 (every figure and
troop standing still, on the map, in a place and in a fight), `UK_FACE_FPS` 2
(a face that talks).
Source: `src/modern/mlayout.h`; `src/modern/uikit.h`.

---

## 7. Page templates

**DSGN-0062. The message box.** Messages and questions with two answers have
shared one box, `page_msg_w` wide:
- An optional picture at the left at 1× (`TW` × `TH`), inset `UK_INSET`; the
  words have started `TW` + `UK_INSET` further right.
- `YELLOW` title lines (at most 3, wrapped), then `WHITE` words.
- A `UK_BAND` band, then the answers as full-width rows along the foot.
- Height 2 × `UK_INSET` + `T` × `L` + `Rw`: `T` the title and word lines (at
  least 1; at least `TH` tall with a picture), `Rw` = `UK_BAND` +
  `ml_list_height`(rows) when there have been rows.
Source: `src/modern/page.c` `ask`, `title_lines`.

**DSGN-0063. `page_message`.** A message's words have been paged
`PAGE_MSG_LINES` lines at a time, the last line of a page with more to come
ending "..". It has had one Continue row (`banners.castle_continue`), the row
Escape has pressed; a tap anywhere has pressed Enter, and any key has turned
its page or closed it. Without a row (the bridge, DSGN-0125) it has had no
action of its own.
Source: `src/modern/page.c` `page_message`, `page_message_pages`,
`page_message_text_w`; `src/main.c` `main`.

**DSGN-0064. `page_question`.** A question with two answers has shown at most
12 lines of words under an optional title, the last marked when cut; a
question with only a title has shown the title as `WHITE` words. Its answers
have been Yes and No, No the row Escape has pressed, and a tap outside it has
pressed Escape.
Source: `src/modern/page.c` `page_question`.

**DSGN-0065. The anchors of messages.** A message or two-answer question has
stood on the map's foot (`PAGE_MAP_FOOT`) wherever it has been raised, whatever
has been open, and on the battlefield's foot (`PAGE_FIELD_FOOT`) when raised
over the field.
Source: `src/modern/overlay.c` `draw_message`; `src/modern/prompt.c`
`modern_prompt_draw`.

**DSGN-0066. Places: two pages.** A place has been two full pages, and each
step of it has been the one its kind has called for. Both have had the title
strip, a rows column 16 × `GW` + 2 × `UK_INSET` wide at the left with a
`UK_BAND` band beside it, and the words beside that, inset `UK_INSET`. One row
has been a single action (a tap anywhere presses Enter); more rows a choice
with Escape as the exit.
Source: `src/modern/page.c` `page_place`, `page_person`.

**DSGN-0067. The room (`page_place`).** The place itself: the backdrop band
(DSGN-0059) under the strip, two tiles tall (2 × `TH`), trimmed from its top
where the rows have needed the room so `PAGE_PLACE_ROWS` 3 rows and the exit
have fitted under it (on a touch device); its keeper standing in it
(DSGN-0060); then the rows column and the words. Used for a place's front, a
roll of troops, How many, and sailing; a note drawn as a scene has had a room
of its own (DSGN-0127).
Source: `src/modern/page.c` `page_place`.

**DSGN-0068. The toast.** A toast has not been a page: one `YELLOW` line, cut
with "..", in a box text width + 2 × `UK_INSET` wide (at most the area's width
− 4 × `ring`) and `GH` + 2 × `UK_INSET` tall, centred across the area
2 × `ring` below its top, filled `uk_fill` inside a `ring` ring. The area has
been the battlefield in a fight and the map otherwise. It has dimmed nothing,
taken no taps, and been drawn last.
Source: `src/modern/page.c` `page_toast`.

**DSGN-0069. The person (`page_person`).** Someone speaking: no band; the
rows column has run the page's full height under the strip, its rows from the
top and its exit or answers on its foot, and the speaker's face at 2×
(2 × `TW` square) has stood at the top-left of the words with what they have
said beside it in one column. Used for a town's services and each service, a
question asked in a place, and every outcome.
Source: `src/modern/page.c` `page_person`; `src/modern/overlay.c`
`person_says`, `place_outcome`.

**DSGN-0070. The foe (`page_foe`).** The foe's page has had the strip, the
plains as a band min(`TH` + 2 × `UK_INSET`, the room left) tall and never
under `TH`, a card per troop under it, a `UK_BAND` band, and Fight and Evade
as two full-width rows on the foot. A card has held the troop's face at 1×
and `PAGE_FOE_CARD_LINES` lines; its three gaps (over the face, under it, at
the foot) have shared what the lines have left, at most `UK_INSET` and at
least 2.
Source: `src/modern/page.c` `page_foe`; `src/modern/overlay.c`
`modern_overlay_draw_foe`.

**DSGN-0071. The menu page (`page_menu`).** Every menu page has had one shape
and one size: the path as its title (a page's title joined with " > ") with a
right-hand text, two `WHITE` lines saying what the row under the cursor does
or why it has been greyed (`2 × UK_INSET` + 2 × `L` + `UK_BAND` tall), then
the rows: from the top, and the last `foot` of them on the foot with a
`UK_BAND` band above them (DSGN-0050); rows that have not fitted have
scrolled. A row that has opened a page has ended in " >"; its shortcut has
shown at its right while keys have been shown. One row has been a single
action; more rows a choice with Escape as the exit.
Source: `src/modern/page.c` `page_menu`, `page_menu_body`;
`src/modern/gamemenu.c` `gm_row`; `src/modern/mlist.c` `ml_rows_draw`.

**DSGN-0072. Menu pages with bodies of their own.** Controls, the gate picker,
a count question, difficulty and the name have been menu pages
(`page_menu_body`) that have drawn their own body under the strip, with their
rows along the foot.
Source: `src/modern/page.c` `page_menu_body`.

**DSGN-0073. Full pages with bodies of their own.** The world map and the
spells have been full pages with choices (`page_full_body`): the strip, their
own body, and Escape as the exit.
Source: `src/modern/page.c` `page_full_body`.

**DSGN-0074. Pages to read.** A sheet has been a full page (`page_sheet`)
whose single action has been its close: Close in the strip (DSGN-0052) and a
tap anywhere pressing Escape, or, where its one row has been Continue (the
ending), a tap anywhere pressing Enter and no Close. `page_sheet_beside` has
put a picture as tall as the page at its left and the strip over the words
beside it (the puzzle); `page_sheet_small` has been the same at a menu page's
size (the credits).
Source: `src/modern/page.c` `page_sheet`, `page_sheet_beside`,
`page_sheet_small`.

**DSGN-0075. `page_caption`.** A caption on full-bleed art has been a
message's width on the screen's foot inside a ring, dimming nothing, its
title strip carrying the way back (Back, a button pressing Escape), with the
caller's single action or none.
Source: `src/modern/page.c` `page_caption`.

---

## 8. Input

**DSGN-0076. Input devices.** The game has read the keyboard, touch and pad 0.
Source: `src/input_host.c` `input_key_pressed`, `input_touch_sample`;
`src/input.c` `input_poll`, `poll_gamepad`.

**DSGN-0077. The device last used.** `input_last_device()` has been the
keyboard, a finger or the pad, whichever the player has used last: a real key
a screen has read (not a key injected by a tap, not Android's Back), a sampled
touch contact, or a pad button or direction. Before anything has been used it
has been the keyboard, or a finger on a touch device. iOS has always reported
a finger.
Source: `src/input_host.c` `note_key`, `input_host_note_gamepad`,
`input_touch_sample`, `input_last_device`; `ios/host_ios.c`.

**DSGN-0078. A touch device.** A touch device has been an iOS or Android
build, or a session started with `--touch`; it has been fixed for the session
(`input_touch_device`).
Source: `src/input_host.c` `input_touch_device`, `input_host_force_touch`;
`ios/host_ios.c`; `src/main.c` `main`.

**DSGN-0079. The pad.** Pad 0 has been noted when the map has read any button,
or when a direction, A or B has been read elsewhere. The map, the letter grid,
Controls and the bridge have read the pad; on the map, A has searched, X
cast, Y ended the week, LB the army, RB the character sheet, LT flown, RT
landed, Start the world map and Back the game menu; a keyboard action in the
same frame has won.
Source: `src/input.c` `poll_gamepad`, `input_gamepad_dir`,
`input_gamepad_confirm`, `gamepad_pressed_cancel`.

**DSGN-0080. The touch unit.** In modern, the touch unit in game pixels has
been the row height, `touch_unit_design()` = `R` (DSGN-0041), fixed for the
session. `touch_unit()`, in window pixels, has been 11% of the window's
shorter side (the side capped at 1284 in modern), never below 44.
Source: `src/touch.c` `touch_unit`, `touch_unit_design`.

**DSGN-0081. The order a tap has been resolved in.** Every tap has been
resolved in one order, at the end of its frame:
1. legacy window buttons (none in modern, DSGN-0085);
2. a tap in the letterbox has done nothing;
3. priority regions hit squarely;
4. the first region registered that has held the tap: a map (a direction), a
   row (its list and row), a grid (its cell), a button or tile (its key);
5. the nearest button or row within the slack (DSGN-0082);
6. the page on top (DSGN-0038), which has ended the search;
7. the any-key fallback, only when no page has been open.
While a page has been open, steps 3 to 5 have skipped every region registered
before it (under the bridge's message, only those inside it).
Source: `src/touch.c` `resolve`, `resolve_tap`, `under_page`.

**DSGN-0082. Near misses.** A tap that has hit nothing squarely has gone to
the nearest button or row within the slack, `R` / 2, measured as the larger of
the across and down distances from the rect's edge; the strictly nearest has
won, ties to the first registered. With a page open, only the page's own
regions have been in reach, and only for a tap inside the page. Maps, grids,
scroll areas and pages have had no slack.
Source: `src/touch.c` `touch_slack`, `rect_distance`, `resolve`.

**DSGN-0083. Tap timing.** A tap has been resolved at the end of the frame it
has landed in, against that frame's regions, and its row or key has been
visible to the next frame's input for one frame. A press on a frame whose
screen has changed (DSGN-0006) has been dropped. At most `REGION_MAX` 96
regions and `INJECT_MAX` 32 injected keys a frame have been kept.
Source: `src/touch.c` `touch_frame`, `add_region`; `src/present.c`
`present_layout_changed`; `src/input_host.c` `input_host_inject_key`,
`input_host_inject_key_next_frame`.

**DSGN-0084. Widgets.** A target aimed at on its own (`ui_button`) has been
grown to at least the touch unit on each axis around its centre; a target in a
run of neighbours (`ui_tile`, `ui_tile_row`, `ui_map`, `ui_grid`, `ui_scroll`)
has been registered exactly as drawn. `ui_bar` has been grown and given
priority. Every region has been registered through `src/uitouch.c`, except
the page tap rule, which `src/modern/page.c` has set through `touch_page`.
Source: `src/uitouch.c`; `src/modern/page.c` `open_page`.

**DSGN-0085. No window buttons.** Modern has drawn no window-pixel buttons: a
request for one has been dropped when made (`touch_request` and the prompt bar
requests) and again at draw time.
Source: `src/touch.c` `touch_request`, `touch_request_prompt_yesno`,
`touch_request_prompt_ab`, `touch_request_prompt_numeric`,
`touch_draw_chrome`.

**DSGN-0086. Input guards and Android Back.** Each start-up screen has
discarded queued keys and ignored key presses for 0.25 s, and the game for
0.3 s after loading; row taps have not been guarded. On Android the system
Back has answered every Escape read, without counting as the keyboard.
Source: `src/input_host.c` `input_host_flush`, `input_key_pressed`;
`src/startup.c` `screen_open`; `src/main.c` `main`.

---

## 9. Keys, hints and words

**DSGN-0087. Key hints.** Keys have been shown only while the keyboard has
been the device last used (DSGN-0077), in three forms:
- a bare key in a box in a column tile's corner (DSGN-0028, DSGN-0116), only
  while nothing has been open over it;
- a bare key at a row's right: its shortcut, its digit or letter, and
  `ui.key_esc` at the exit row's right (DSGN-0046);
- " [key]" after Close in a title strip, or " (pad key)" (`ui.pad_back`) while
  the pad has been the device last used.
Source: `src/hud.c` `hud_key_hint`; `src/modern/mlist.c` `ml_keys_shown`,
`ml_exit_hint`, `ml_hint_text`; `src/modern/gamemenu.c` `gm_row`.

**DSGN-0088. The one list reader.** Every modern list has read its keys and
taps through `ml_list_input`: Up, Down and keypad 8 and 2 have moved the
cursor, wrapping; Enter, keypad Enter or Space has acted on the cursor's row;
a tap on a row has put the cursor there and acted on it; a row's own key
(its shortcut letter, or its digit, the keypad's digit too) has acted on it;
Escape has been Back. A row that cannot be chosen has taken the cursor but has
not acted. The game menu, the combat menu, the prompts' rows, the title menu,
the Load pages, difficulty, Controls, the world map, the town and the places
have all read through it.
Source: `src/modern/mlist.c` `ml_list_input`, `ml_key_code`;
`src/modern/gamemenu.c` `gm_page_input`; `src/select.c` `sel_input`.

**DSGN-0089. The words on controls.**
- Back: `menu.items.gm_back` (menu sub-pages, the combat menu's sub-pages and
  its spells, Controls opened from a menu, the Load pages, difficulty and
  name, the class caption), and `banners.town_back` (the town's services and
  sections, the castle rolls and the audience).
- Close: `menu.items.gm_close` (the top page of the game and combat menus,
  every page to read, the world map, the spells on the map, Controls opened
  from the map).
- Leave: `banners.location_leave` (town, castle, temple, dwelling).
- Cancel: `banners.count_cancel` (counts, numbered questions, sailing, the
  gate).
- Yes and No: `prompts.yes`, `prompts.no`.
- Continue: `banners.castle_continue`, and a literal "Continue" in the class
  caption.
- Exit: `menu.items.exit` (the title menu) and `menu.items.gm_exit` (the game
  menu), on desktop and web only.
- A row that has opened a page of rows has ended in " >".
Source: `src/modern/gamemenu.c` `modern_gamemenu_page`, `gm_load_page`;
`src/combat_loop.c` `combat_menu_page`; `src/modern/overlay.c`
`modern_overlay_draw_town`, `controls_row`; `src/modern/castle.c`
`modern_castle_row`; `src/startup.c` `class_confirm_row`,
`title_menu_labels`; `src/views.c` `views_town_list_row`.

**DSGN-0090. Map keys.** On the map, A has opened the army sheet, C Controls,
F flown, L landed, I the contract, M the world map, P the puzzle, S searched,
U cast, V the character sheet, W ended the week, D dismissed, N set sail,
5 and keypad 5 rested, O and Escape opened the game menu, Q opened it on the
Save page, and Ctrl+Q quit. Arrows, the keypad and Home, End, PgUp and PgDn
have stepped once a press; Enter and Space have done nothing.
Source: `src/input.c` `input_poll`, `poll_direction`;
`src/shell_actions.c` `shell_dispatch_action`.

**DSGN-0091. Shortcuts in menus.** A game menu or combat menu row's shortcut
has been the map key for the same action (`GmItem.shortcut`), shown at its
right and acting on its row inside the menu: Army A, Character V, Contract
I, Puzzle P, Dismiss D, Map M, Cast U, Search S, Fly F or Land L, End the
week W, Rest 5, Set sail N, Controls C; in a fight Shoot S, Wait W, Fly F,
Cast U, Army A, Character V, Controls C, Give up G.
Source: `src/modern/gamemenu.c` `modern_gamemenu_page`, `gm_page_input`;
`src/combat_loop.c` `combat_menu_page`.

---

## 10. Text

**DSGN-0092. The font.** Modern text has used the pack's TrueType face
(`game.json:font.file`, `game.json:font.size`, `game.json:font.caps`) at a
fixed advance: `GW` has been the widest advance over printable ASCII and the
four arrows, and `GH` has been max(the deepest ink bottom,
`game.json:font.size`) + (`game.json:font.size` + 7) / 8. Each glyph has been
centred in its `GW` cell by its ink width. When the face has failed to load,
the pack's bitmap strip (`game.json:sprites.font`) has been used in an
8 × `UI` cell.
Source: `src/text.c` `preload_metrics`, `text_draw`, `text_width`;
`src/bfont.c` `bfont_preload_metrics`, `bfont_init`.

**DSGN-0093. Wrapping.** Text has been wrapped to a pixel width at the last
space before it, or just after a hyphen inside a word where that has been
later. A word longer than the line with no hyphen of its own has been broken
with a hyphen, which has taken the place of as many letters as it has needed.
Leading spaces have been skipped, every newline has broken the line, and blank
lines have been kept.
Source: `src/text.c` `text_take_line`; `src/bfont.c` `bfont_take_line`.

**DSGN-0094. Measuring and centring.** Text has been measured as its widest
line (`GH` a line) and centred on half that width; right-aligned text has
ended at its right edge.
Source: `src/bfont.c` `bfont_measure`, `bfont_draw_centered`,
`bfont_draw_right`.

**DSGN-0095. Line pitch.** Every modern line of text has stood `L` = `GH` + 2
below the one above it: words, messages, questions, sheets, the credits,
difficulty, the name and the world map, and the second line of a two-line
row.
Source: `src/modern/uikit.c` `uk_line_h`; `src/modern/mlist.c` `draw_row`;
`src/modern/views_render.c`; `src/startup.c` `draw_credits`,
`draw_difficulty_modern`, `draw_name_modern`.

**DSGN-0096. How much text a block has held.** A message's title has held at
most 3 lines and a page of words `PAGE_MSG_LINES`; a question's words at most
12 lines; a choice at most 2 lines of its row; a menu description 2 lines; the
battle column's name 2 lines; a count question's words 3 lines; a formatted
block has been paged when given a pager and has shown its first page
otherwise. Past those limits the last line shown has ended "..".
Source: `src/modern/page.c` `ask`, `page_message`, `page_question`,
`page_menu`; `src/modern/uikit.c` `uk_words_centred`, `uk_doc_draw`;
`src/modern/prompt.c` `prompt_row`, `modern_prompt_draw`;
`src/combat_loop.c` `combat_turn_draw`.

**DSGN-0097. Fixed cuts.** A save row has cut the hero's name at 10
characters and the rank at 13; a difficulty label at 12; the hero's name has
been at most 10 characters; the combat log line at 79; a message's header at
255 and body at 511.
Source: `src/modern/saveslots.c` `saveslots_row`; `src/startup.c`
`difficulty_row`, `name_char_allowed`; `src/ui.c` `open_dialog_flags`;
`engine/combat_log.c` `combat_log_template`.

---

## 11. Colour

**DSGN-0098. Fills and lattice.** Every page, the toast and the scenes' side
bars have been filled `uk_fill` (12,14,30); every border, band and ring has
been the lattice of DSGN-0013 to DSGN-0015.
Source: `src/modern/uikit.c` `uk_fill`; `src/lattice.c` `lattice_ring`,
`lattice_band_v`, `lattice_band_h`.

**DSGN-0099. Text roles.** One colour has meant one thing on every page:
- `YELLOW`: titles, labels and headings -- title strips and their right-hand
  text, message titles, the labels of numbers on every sheet and card, the
  column headings, the pager, the toast, key hints, the gold and days
  figures.
- `WHITE`: words and values -- body text, menu descriptions, Close, the
  numbers beside their labels, count badges (on `BLACK`).
- `DGREY`: what cannot be chosen, and the default name in an empty field.
The letter grid has been `YELLOW`, its cursor cell filled `YELLOW`.
Source: `src/modern/uikit.c`; `src/modern/views_render.c` `cv_row`,
`draw_army`; `src/modern/page.c` `ask`, `page_toast`; `src/combat_loop.c`
`combat_turn_draw`, `turn_stat`; `src/hud.c` `hud_key_hint`, `tile_number`;
`src/textsel.c` `textsel_draw`; `src/startup.c` `draw_name_modern`.

**DSGN-0100. The palette.** Named colours have come from the first 16 entries
of the pack's `palettes/palette.bin`: `BLACK` 0, `DBLUE` 1, `DGREEN` 2,
`DCYAN` 3, `DRED` 4, `MAGENTA` 5, `BROWN` 6, `GREY` 7, `DGREY` 8, `BLUE` 9,
`GREEN` 10, `CYAN` 11, `RED` 12, `VIOLET` 13, `YELLOW` 14, `WHITE` 15. A
built-in 16-colour table has been installed when the file has failed to load.
Source: `src/palette.c` `palette_init`.

**DSGN-0101. Signal colours.** `RED` has marked the foe (the battle column's
name on the foe's turn), low morale or an army out of control, and castles on
the world map; `GREEN` high morale; `CYAN` the Time Stop steps and the boat on
the world map; the hero's marker on the world map has blinked `YELLOW` and
`MAGENTA` at 3 Hz and towns have been `WHITE`. World-map terrain has used
`game.json:colors.minimap_terrain`.
Source: `src/combat_loop.c` `combat_turn_draw`; `src/modern/views_render.c`
`draw_army`, `draw_worldmap`; `src/hud.c` `days_tile`.

**DSGN-0102. Named colours of the kit.** Every literal colour a modern page
has used has had a name:
- `uk_edge` (150,118,48): a picture's edge.
- `uk_ghost` (60,60,70) and `uk_edge_dim` (60,52,34): an icon still to find.
- `uk_button` (200,160,60) and `uk_bar_edge` (90,72,30): the count's buttons
  and bar.
- `uk_shade` (0,0,0,150): laid over what cannot be used now (a command, an
  unpicked class).
- `uk_hint_bg` (0,0,0,190): behind a key name on a tile.
- Fog strips: black at 128, 64 and 32. The title's field: (65,9,104). The
  page dim: DSGN-0036.
Source: `src/modern/uikit.c`; `src/map_render.c` `map_render_draw`;
`src/startup.c` `draw_title_sequence`.

---

## 12. Art

**DSGN-0103. Filtering.** All pack art has been point-filtered, so the zoom
has magnified it by whole pixels; the whole frame has been filtered smoothly
only while fitted down below the smallest screen (DSGN-0007).
Source: `src/sprites.c` `load_filtered`; `src/tile_cache.c`
`tile_cache_get`; `src/present.c` `present_scaled`.

**DSGN-0104. Map, column and combat cells.** Map tiles, the hero, the boat,
column tiles, command tiles, battlefield cells and troops have been drawn
into one-tile cells (`TW` × `TH`), each art stretched to its cell.
Source: `src/map_render.c` `map_render_draw`; `src/hud.c` `blit_tile`;
`src/modern/rail.c` `rail_draw`; `src/combat_render.c` `draw_tile`,
`draw_unit`.

**DSGN-0105. Portraits.** Every portrait has been drawn at a whole multiple:
- 2× (2 × `TW` square): a person speaking (DSGN-0069), a castle's troop, the
  contract's villain, the promotion.
- 1×: a message's picture, the foe's cards, the army sheet (cut to its row,
  `uk_picture_cut`).
- The class portrait on the character sheet at its authored size.
- The ending picture at the largest whole multiple that has fitted half the
  page's width and the height above its row, flush top-left.
Talking faces have run at `UK_FACE_FPS`.
Source: `src/modern/overlay.c` `person_says`, `castle_troop_detail`,
`castle_draw_promotion`, `modern_overlay_draw_foe`; `src/modern/page.c`
`ask`; `src/modern/views_render.c` `draw_army`, `draw_character`,
`draw_contract`; `src/combat_loop.c` `combat_turn_draw`;
`src/screens/end_game.c` `draw_modern`.

**DSGN-0106. Figures.** A place's figure has stood at 2× on the band's foot
(DSGN-0060): the keeper one tile in from the backdrop's left, the Emperor
centred on his throne room, and the Augur centred where
`game.json:sprites.ui.alcove_figure_place` has placed him, in backdrop units
scaled with the backdrop. The foe's troops have stood at 1×, mirrored, over
their cards. Figures and troops standing still have run at `UK_IDLE_FPS`.
Source: `src/modern/uikit.c` `uk_scene_figure`, `uk_figure`;
`src/modern/overlay.c` `modern_overlay_draw_castle`,
`modern_overlay_draw_temple`, `modern_overlay_draw_foe`, `troop_standing`,
`figure`.

**DSGN-0107. Puzzle, gate and world map.** The puzzle sheet's cells and the
character sheet's icons have been one tile; an uncovered piece has shown the
land under it through `map_render_cell`. The gate picker's preview has drawn
the destination's surroundings at 1× through `map_render_cell`, cut at the
preview's edges, the landing square ringed. The world map has drawn flat
colour cells, the cell size min(room across / map width, room down / map
height), three times that on a chosen place.
Source: `src/modern/views_render.c` `draw_puzzle`, `cv_icon`,
`draw_gate_map`, `draw_worldmap`.

**DSGN-0108. Full-bleed art.** The logo, the title, the class painting and
the victory cartoon have been drawn at the largest whole multiple of their
authored size that has fitted the screen on both axes (at least 1×), centred.
Before the game the screen round the art has been black; round the victory
cartoon it has been the frame's lattice, railed against the art.
Source: `src/modern/page.c` `page_art`, `page_art_margins`; `src/ui.c`
`ui_fit_scale`; `src/startup.c`; `src/end_cartoon.c` `cartoon_place`.

---

## 13. Combat

**DSGN-0109. The battle's base screen.** A battle has drawn no world. Each
combat frame has drawn the chrome (the frame and both bands) and then the
battle in the base screen's places (DSGN-0110); every page of a fight has
stood over that.
Source: `src/combat_loop.c` `combat_present`; `src/modern/page.c`
`page_combat`.

**DSGN-0110. The battle's places.** A battle has had one column, two tiles
wide (2 × `TW`), against the right frame, the full height of the interior;
the battlefield (6 × `TW` by 5 × `TH`) has had the interior left of it, flush
with its top. Where that room has held the field with a `UK_BAND` band and a
pixel of ground either side, the field has been centred in it, with a `G`
band before the column; otherwise (the smallest screen) the field has stood
flush with the left frame and the band before the column has taken what was
left. In a siege, where the interior has been at least one tile taller than
the field, the castle's back wall row has stood above the field. Round the
field the interior has been lattice ground, with a band along each edge of
the field the ground has shown beside.
Source: `src/modern/page.c` `page_combat`.

**DSGN-0111. Battlefield.** Each of the 6 × 5 cells has been one tile at the
battlefield's origin.
- The ground has been the map tile of the hero's terrain (water drawn as
  grass) when `game.json:sprites.ui.combat_ground` has named the terrain.
- A siege's cells have come from `game.json:sprites.ui.siege_grid`: the five
  board rows, and the back wall row only where it has stood (DSGN-0110).
- Obstacles have been their tiles; a hit has shown the splat tile over the
  unit.
- The target cursor has shown only while a target has been picked, animating
  at `UK_IDLE_FPS`.
Source: `src/combat_render.c` `combat_render_frame`, `draw_tile`,
`cell_origin`; `src/shell_promptdispatch.c` `shell_set_combat_ground`.

**DSGN-0112. Troops.** Troops have been drawn one tile each, foes mirrored,
each with its count badge (`combat_count_badge`): `WHITE` digits on a `BLACK`
band centred across the cell's foot. The active unit has alternated frames 0
and 1 every 0.15 s; an attack strip has lasted 0.36 s, the splat 0.30 s, and
the field has been held 0.50 s before the end.
Source: `src/combat_render.c` `draw_unit`, `combat_count_badge`;
`src/combat_loop.c` `combat_tick_anim`, `attack_anim_start`, `splat_tick`,
`RunCombat`.

**DSGN-0113. Command grid.** Under the column's words, the commands have been
five tiles in a grid two across and three down, in a fixed order read across
then down: Menu (`game.json:sprites.rail.menu`), Shoot, Wait, Fly and Cast;
the sixth cell has held Round (`ui.combat_round`) over the round + 1. A
command the unit has not been able to use has been shaded `uk_shade` and has
never moved; on the foe's turn every tile has been shaded. The joins have
been DSGN-0022's across every tile edge of each grid column, with a 2 × `UI`
band down the middle, and the column's ground below.
Source: `src/combat_loop.c` `combat_column_draw`, `combat_panel_art`.

**DSGN-0114. Command taps.** The tiles have taken taps only while live (the
player's turn, and no menu, view, question, message, picker or cast open):
Menu has pressed Escape, Shoot S, Wait Space and Fly F on the next frame, and
Cast has opened the combat menu on its spells page.
Source: `src/combat_loop.c` `combat_panel_tap`, `combat_menu_page`.

**DSGN-0115. The column's words.** Above the grid, on lattice ground, from
`UK_INSET` under the column's top:
- the active unit's name, centred at the column's width less `UK_INSET`
  each side, at most 2 lines, `YELLOW` on the player's turn and `RED` on the
  foe's;
- its count under the name, `WHITE`;
- Moves and, when the unit has had shots, Shots, each one line, a `YELLOW`
  label and a `WHITE` figure (`ui.combat_moves`, `ui.combat_shots`), centred
  together;
- then `UK_INSET`, and the grid (DSGN-0113).

The unit itself has been marked on the field: it alone has animated
(DSGN-0112).

Under the grid, from `UK_BAND` below its foot band, the combat log has stood
as cards, newest at the top: each a plate of `uk_fill` the column's width, a
1 px edge, a 3 px bar down its left, its line wrapped inside (8 px in from
the bar, 4 px from the right, 6 px above and below, at most 3 lines, the
last cut with `..`), a `UK_BAND` of ground between cards. The newest has
been lit, `uk_edge` for its edge and bar and `YELLOW` words; the rest have
had `uk_edge_dim` and `WHITE`. As many cards have stood as the column has
held whole; the oldest have gone first. The lines have been the engine's
own (`COMBAT_LOG_LINES` at most).
Source: `src/combat_loop.c` `combat_column_draw`, `turn_line`, `turn_stat`,
`combat_log_cards`.

**DSGN-0116. Command keys.** While the keyboard has been the device last used
and the column has been live, each command that could be pressed has shown
its key in its tile's corner (DSGN-0028): `ui.key_esc`, S, W, F and U.
Source: `src/combat_loop.c` `combat_column_draw`, `combat_panel_key`.

**DSGN-0117. The combat log line.** Each new combat log line has been shown as
a toast (DSGN-0068) on the battlefield's top edge for 2.0 s, the same as a
toast on the map, and has stayed in the column's cards (DSGN-0115).
Source: `src/combat_loop.c` `combat_present`; `src/ui.c` `toast_show`.

**DSGN-0118. Combat menu.** The combat menu has been menu pages (DSGN-0071)
with the hero's name at the strip's right:
- Actions (`menu.items.gm_actions`): Unit >, Hero >, Game >, and Close on
  the foot.
- Unit: Shoot (S), Wait (W), Fly (F), Cast a spell > (U), and Back on the
  foot, always in that order, greyed with their reasons
  (`banners.gmr_no_shots`, `gmr_adjacent`, `gmr_cannot_fly`, `gmr_no_magic`,
  `gmr_one_spell`).
- Hero: Army (A), Character (V), and Back on the foot.
- Game: Controls (C), then Back and Give up (G) on the foot.
- Spells: the spells page (DSGN-0138) with the path as its title and Back as
  its exit.
Source: `src/combat_loop.c` `combat_menu_page`, `combat_action_menu_draw`.

**DSGN-0119. Opening the combat menu.** The combat menu has opened on its
Unit page with the cursor on the first command the unit has been able to use:
with Enter, keypad Enter, a tap on the active unit, Escape when nothing has
been armed on the player's turn, or the Menu tile. U or the Cast tile has
opened the spells page directly, on the first spell held. Every page has
opened on its first row that could be chosen. Escape, or a tap outside the
menu, has gone back one page, closing at the top.
Source: `src/combat_loop.c` `combat_menu_open`, `combat_menu_push`,
`combat_menu_first`, `combat_action_menu_draw`, `combat_player_action_full`,
`RunCombat`.

**DSGN-0120. Messages and questions in a fight.** The victory message and the
give-up question have stood on the battlefield's foot (DSGN-0035), the
message with one Continue row and a tap anywhere to continue, the question
with Yes and No. Defeat has drawn nothing on the field. The army sheet, the
character sheet and Controls have been the same pages as on the map, over the
battle.
Source: `src/combat_loop.c` `combat_present`, `RunCombat`;
`src/modern/overlay.c` `draw_message`; `src/modern/prompt.c`
`modern_prompt_draw`.

**DSGN-0121. The target picker.** A shoot, fly or spell target has been
picked with a cursor moved by the arrows and the keypad (diagonals on Home,
PgUp, End and PgDn) and confirmed on a legal cell with Enter, keypad Enter,
Space, A or C; Escape has cancelled. A tap on a cell has moved the cursor
there and confirmed it when legal.
Source: `src/combat_loop.c` `combat_pick_step`.

**DSGN-0122. Sheets and Controls in a fight.** In a fight a sheet has closed
on any key or a tap, as on the map, and Controls has read its keys through
the one Controls reader (DSGN-0135).
Source: `src/combat_loop.c` `RunCombat`; `src/views.c`
`views_controls_input`.

**DSGN-0123. Replayed fights.** A recorded fight (the demo, visible autoplay)
has been shown through the same frame, one recorded entry a beat: each entry
has moved the actor, set the counts, played the attack strip across the beat,
flashed the target and shown the log line as the toast; a unit killed has
stayed under its splat until the splat has faded, and the victory message has
been dismissed after the dwell.
Source: `src/combat_replay.c` `RenderCombatRecord`, `replay_apply_entry`,
`replay_beat`.

---

## 14. Screens

**DSGN-0124. A message on the map.** A message has been `page_message` with
one Continue row on the map's foot, whatever has been open (DSGN-0065), with
the picture its note has named: a villain, a troop, an artifact or a
portrait. Any key or a tap anywhere has turned its page or closed it.
Source: `src/modern/overlay.c` `draw_message`, `note_face`; `src/main.c`
`main`.

**DSGN-0125. The bridge.** The bridge's direction has been asked with
`banners.spell_bridge_prompt_modern` in a `page_message` with no row on the
map's foot, the four squares beside the hero outlined 3 px in `YELLOW`,
clipped to the map. A tap on the map has chosen the direction; Escape has
cancelled.
Source: `src/modern/overlay.c` `draw_message`; `src/main.c` `main`.

**DSGN-0126. A note with a face.** A note carrying a picture (a capture, the
week's creature) has been the message box with the picture at its left
(DSGN-0062).
Source: `src/modern/overlay.c` `draw_message`, `note_face`.

**DSGN-0127. A note as a scene.** A note drawn as a scene (temporary death,
a one-time vista, a refused gate) has been a room of its own
(`page_scene`): the title strip, then the scene's art **whole**, never
trimmed, at the largest scale (3 at most, never wider than the page) that
leaves every line of its words their room beside the one Continue row under
the band, else the smallest; the column bars either side of the picture as
in the room, and the words and the row laid as the room lays them. Without
its scene art it has been the message box.
Source: `src/modern/page.c` `page_scene`; `src/modern/overlay.c`
`draw_note_scene`.

**DSGN-0128. Yes/No questions.** A yes/no question has been two rows, Yes and
No, the cursor on Yes: the message box on the map's foot, on the
battlefield's foot for a question over the field, with a face when raised
with one (troops that ask to join), or in the open page's rows when raised in
place. Y has answered
yes and N no; Escape, or a tap outside the box, has answered Cancel, which
callers have treated as No.
Source: `src/prompt.c` `prompt_yes_no_open`, `prompt_update`;
`src/modern/prompt.c` `modern_prompt_draw`, `prompt_row`.

**DSGN-0129. Numbered and lettered questions.** A numbered or lettered (A/B)
question has been a menu page (DSGN-0071) with the question's words as its
description: one row per choice (each at most two lines of its row), and for
a numbered question a Cancel row on the foot. The rows have been the ones the
opener has named (`prompt_set_choices`, `prompt_set_lead`), or else the
answers themselves, 1 to N or A and B; nothing has been read out of the words.
A treasure chest's rows have come from `banners.chest_gold_take` and
`chest_gold_share`, its words from `chest_gold_found` and its title from
`chest_gold_title` (all optional). Each row has shown its digit or letter
while keys have been shown; digits and keypad digits (A and B on a lettered
question) have answered, and Escape has answered Cancel.
Source: `src/prompt.c` `default_choices`, `prompt_set_choices`,
`prompt_set_lead`, `prompt_ab_open`, `choice_rows_update`, `prompt_update`;
`src/modern/prompt.c` `modern_prompt_draw`, `prompt_row`.

**DSGN-0130. A count question.** A count not raised in place has been a menu
page: the question's words (at most 3 lines), the How many block (DSGN-0055)
and Continue and Cancel on the foot; Enter has committed and Escape
cancelled.
Source: `src/modern/prompt.c` `modern_prompt_draw`; `src/prompt.c`
`prompt_update`.

**DSGN-0131. The toast on the map.** A toast (DSGN-0068) has stood on the
map's top edge for 2.0 s, drawn after every page.
Source: `src/overlay.c` `draw_toast`; `src/ui.c` `toast_show`.

**DSGN-0132. Foe.** A hostile band has been the foe's page (DSGN-0070) with
Fight and Evade (`banners.foe_fight`, `foe_evade`):
- The title has been `dialog_titles.foes`, with the player's army
  (`banners.fv_your_army`) or, when there has been nowhere to run,
  `banners.foe_evade_blocked` at the strip's right.
- The band has been the plains backdrop, each troop one tile, mirrored, over
  its card.
- There have been five cards, (`page_full_w` − 4 × `UK_BAND`) / 5 wide, with
  lattice between them: the troop's portrait (one tile), how many (worded as
  the encounter message has worded it), its name (at most 2 lines), its hit
  points and its damage.
- When there has been nowhere to run, Evade has been greyed and Escape
  ignored.
- Y has fought; N and Escape have evaded.
Source: `src/modern/overlay.c` `modern_overlay_draw_foe`, `foe_row`;
`src/prompt.c` `prompt_update`.

**DSGN-0133. Sailing.** Sailing has been the room with the sail backdrop,
titled `dialog_titles.navigate`, from the first list on: the provinces
(`shell_navigate_choices`, each with its digit while keys have been shown)
from the rows column's top and Cancel on its foot, then the confirmation
(`banners.body_navigate_confirm`) with Yes and No on the foot. No has returned
to the provinces, and Escape or Cancel on the provinces has ended the sail.
Source: `src/shell_actions.c` `shell_dispatch_action`,
`shell_navigate_choices`; `src/shell_promptdispatch.c`
`prompt_dispatch_tick`; `src/modern/overlay.c` `modern_overlay_draw_sail`,
`sail_row`.

**DSGN-0134. The game menu.** The game menu has been menu pages (DSGN-0071)
with the hero's name at the strip's right:
- Menu (`menu.items.gm_title`): Hero >, World >, Game >, then Close and, on
  desktop and web, Exit on the foot.
- Hero: Army (A), Character (V), Contract (I), Puzzle (P), Dismiss (D, greyed
  with `banners.gmr_no_troops` without troops), Back.
- World: Map (M), Cast a spell (U), Search (S), Fly (F) or Land (L), End the
  week (W), Rest (5), Set sail (N, greyed with `banners.gmr_not_sailing` unless
  sailing), Back.
- Game: Debug > (with `--debug`), Save >, Load > (greyed with
  `banners.gmr_no_saves` without a save), Controls (C), New Game, Back.
- Save and Load: the five slots (`MODERN_SAVE_SLOTS`), empty slots greyed on
  Load (`banners.gmr_empty_slot`), then Back. The title's Load page has been
  this page (`gm_load_page`).
- Descriptions have come from `banners.gmd_*`.

Each page has opened with the cursor on its first row that could be chosen,
and going back has returned to the row that had opened it. Hero and World
rows have closed the menu and pressed their key on the next frame. Saving
over a slot, loading, a new game and Exit have asked Yes/No in the page's
place, the question as the description, Yes and No on the foot.
Source: `src/modern/gamemenu.c` `modern_gamemenu_page`, `gm_load_page`,
`open_cursor`, `modern_gamemenu_open`, `modern_gamemenu_update`,
`modern_gamemenu_draw`, `slot_row`; `src/modern/saveslots.c`
`saveslots_scan`, `saveslots_row`.

**DSGN-0135. Controls.** Controls has been a menu page with a body of its own
(DSGN-0072), with the hero's name at the strip's right, titled with the
menu's path and `menu.items.gm_controls` when opened from the game menu, and
`controls.title` otherwise:
- each visible setting (`game.json:controls.settings`), its digit before it
  while keys have been shown, its value at the right;
- then the exit: Back when opened from a menu, which has returned to it, and
  Close when opened from the map or a fight.

Every Controls page has read through one reader: Up, Down and keypad 8 and 2
have wrapped; Enter, keypad Enter, Space, a tap or a setting's digit has
stepped that setting; the exit, Escape, C or the pad's B has closed it; the
cursor has started on the first row each time. A setting with
`game.json:controls.settings[].audio` true has been greyed when there has
been no audio device.
Source: `src/modern/overlay.c` `modern_overlay_draw_controls`,
`controls_row`; `src/views.c` `views_controls_input`,
`views_controls_advance`, `views_controls_row_disabled`;
`src/modern/gamemenu.c` `modern_gamemenu_path`.

**DSGN-0136. Sheets.** The character, army, contract and puzzle sheets have
been pages to read (DSGN-0074):
- Character: titled with the name and rank; the class portrait (DSGN-0105),
  two headed columns (army and magic, then the next rank and how many more
  captures it has needed; the campaign), the eight artifacts as tile icons (a
  missing one a ghost), the continents, and the pack's honours where it has
  had them.
- Army (`menu.items.army`, the gold): five rows of equal height, each troop's
  portrait (1×, cut to its row), "count name" in `YELLOW`, its morale at the
  right, and its figures in aligned columns. Opened from a fight on the
  player's turn, the row of the troop whose turn it has been has been ringed
  in `YELLOW`.
- Contract: the villain at 2× and the contract's words beside it in one
  column, or the silhouette and how to take one.
- Puzzle: the 5 × 5 grid of tile cells as tall as the page at the left
  (`page_sheet_beside`), the words at the right under the strip.

A tap anywhere or any key has closed a sheet, on the map and in a fight.
Source: `src/modern/views_render.c` `draw_character`, `draw_army`,
`draw_contract`, `draw_puzzle`; `src/views.c` `views_army_mark`,
`views_closes_on_tap`; `src/main.c` `main`.

**DSGN-0137. The world map.** The world map has been a full page with choices
(DSGN-0073), titled with the continent:
- The map has been at the left, and a column 16 × `GW` + 2 × `UK_INSET` wide
  at the right.
- The column has held where the hero and the boat have stood, then the list:
  "All of" (`banners.worldmap_all`), the visited towns and castles, and Close
  alone on the foot; with the orb, a row over the foot has swapped the
  player's map and the whole map.

Its list has been read by the one reader (DSGN-0088): moving the cursor has
zoomed the map to the place under it; Close or Escape has closed it; the
whole-map choice has lasted between openings.
Source: `src/modern/views_render.c` `draw_worldmap`,
`modern_worldmap_input`.

**DSGN-0138. The spells.** The spells page has been one full page on the map
and in a fight (`modern_spells_draw`), with the hero's name at the strip's
right:
- Two columns of seven spells under their headings
  (`spells_view.combat_col`, `spells_view.adventure_col`), each with the held
  count at the right; a spell has been greyed where it could not be cast (the
  other column, or none held).
- Under them two lines saying what the spell under the cursor does, or why it
  cannot be cast here (`banners.gmr_spell_on_map`, `gmr_spell_in_fight`,
  `gmr_no_spell_held`).
- The exit on the foot as row 14: Close on the map, Back in a fight.
- The cursor has started on the first spell that could be cast. Up and Down
  (keypad 8 and 2) have moved within a column and through the exit at either
  end; Left and Right (keypad 4 and 6) have changed column; Enter, keypad
  Enter, Space or a tap has cast a spell that could be cast, or taken the
  exit; Escape has closed the page.
Source: `src/modern/views_render.c` `modern_spells_draw`, `draw_spells`;
`src/views.c` `views_spells_input`, `views_spells_first`,
`views_spell_castable`.

**DSGN-0139. The gate picker.** The gate picker has been a menu page with a
body of its own (DSGN-0072), titled with the gate spell, with the hero's name
at the strip's right:
- The destinations have been one list 16 × `GW` + 2 × `UK_INSET` wide.
- The chosen place's surroundings have been drawn beside the list
  (DSGN-0107).
- The foot rows have been Travel (`banners.gate_travel`) and Cancel.
- The places and the foot rows have been read by the one reader
  (DSGN-0088): Enter on a place or on Travel has travelled to the place under
  the cursor, and Cancel or Escape has closed it. A first tap on a place has
  moved the cursor there and shown its surroundings; a second has
  travelled.
Source: `src/modern/views_render.c` `draw_gate`, `draw_gate_map`;
`src/views.c` `gate_update_modern`, `views_gate_row`.

**DSGN-0140. Town.** A town has opened on its front, the room:
- Its rows have been Visit the town > (`banners.town_visit`) and Leave, the
  head townsperson standing in the square, the town's introduction beside
  them.
- Visiting has shown the services on a person's page (DSGN-0069): Contracts,
  Boat, Information, Temple and Siege works, each ending in " >", then Back.
  Boat has been greyed without a dock in reach, and Temple without the zone's
  rites; the person of the service under the cursor has spoken beside them.
- Each service has opened its own rows and Back in the same place: Contracts
  (the villains, the one held marked with a dot, the wanted face beside
  them), Boat (Rent or Cancel), Temple (Buy), Siege works (Buy or Owned),
  Information (Back only, a single action), with paged words.
- A paid action has asked Yes/No in the rows' place.
- The outcome has shown one Continue row and has been closed by any key or a
  tap, returning to the services.
- Escape has gone up one level.
Source: `src/views.c` `town_modern_update`, `views_town_list_row`;
`src/modern/overlay.c` `modern_overlay_draw_town`, `town_row_fn`.

**DSGN-0141. Castles.** A castle has opened on its front, the room:
- Its rows have been Recruit > and Audience > at home, Garrison > and
  Withdraw > in an owned castle, then Leave, the words beside them following
  the cursor. The Emperor's palace has had a room for each page (the atrium,
  the armoury, the throne room) and a keeper standing in each.
- Recruit, Garrison and Withdraw have been rolls in the room: the troops,
  then Back, with the troop under the cursor beside them at 2× with its
  figures.
  - Recruit has opened on the first troop offered. A recruit row has been
    greyed when leadership has fallen short of the troop's hit points × 6,
    saying so in `YELLOW` under its figures.
  - The count has opened in place (DSGN-0055).
- The audience has been the throne room with Promotion, Blessing and Tribute
  (with `game.json:audiences`) or a request for an audience, then Back, its
  words following the cursor. A tribute has asked Yes/No on a person's page,
  the Emperor asking.
- Every answer and message has been the keeper speaking on a person's page
  with one Continue row; the promotion has been a person's page with the
  rank's picture, the Emperor's words and the rank's gains.
- Escape has gone up one level.
Source: `src/modern/castle.c` `modern_castle_update`, `act`, `commit`;
`src/modern/overlay.c` `modern_overlay_draw_castle`, `castle_troop_detail`,
`castle_draw_promotion`.

**DSGN-0142. Temple and dwelling.** The temple and a dwelling have each been
the room:
- The temple's offer has been Learn the rites (`banners.temple_learn`) and
  Leave, the Augur standing in the alcove (DSGN-0106).
- The dwelling's offer has been Recruit (`banners.dwelling_recruit_row`,
  greyed when no troop has been recruitable) and Leave, the troop standing in
  its dwelling.
- The dwelling's count has opened in place (DSGN-0055).
- The outcome has been a person's page (the Augur's face, the troop's face)
  with one Continue row, closed by any key or a tap.
Source: `src/main.c` `main`; `src/prompt.c` `prompt_update`;
`src/modern/overlay.c` `modern_overlay_draw_temple`,
`modern_overlay_draw_dwelling`; `src/modern/location.c` `loc_deal_begin`,
`loc_deal_done`, `loc_deal_text`.

**DSGN-0143. Winning and losing.** The ending has been a page to read
(DSGN-0074) titled with the pack's header for it (`win.header`,
`lose.header`, else `dialog_titles.win_fallback` or `lose_fallback`): the
ending picture (DSGN-0105) flush top-left with a `UK_BAND` band
beside it, the words beside that, and one Continue row on the foot; a tap
anywhere has ended it.
Source: `src/screens/end_game.c` `draw_modern`.

**DSGN-0144. Progress screens.** Video encoding and autoplay processing have
each been the message box (`page_status`) with no answer and a bar `GH` tall
under its words.
Source: `src/modern/page.c` `page_status`; `src/encode_dialog.c`
`draw_panel_modern`; `src/shell_autoplay.c` `draw_processing`.

---

## 15. Before the game

**DSGN-0145. Pre-game frames.** The pre-game screens have drawn no frame:
each has cleared to black and set the frame bare (DSGN-0030), so a page over
the art has been measured from the screen's edges, and the art has been drawn
full-bleed on that black (DSGN-0108).
Source: `src/startup.c` `frame_begin`.

**DSGN-0146. Logo.** The publisher's logo (`game.json:sprites.ui.splash_logo`)
has been shown for 2.5 s or until any key or tap. The pack's title splash,
the timed credits and the new-game introduction have been skipped in modern.
Source: `src/startup.c` `run_splash`, `draw_splash`, `startup_flow`.

**DSGN-0147. Title sequence.** The title has played once a run, and any key
or tap has skipped to its end:
- The title field (65,9,104) has shown the size of the art.
- The battle (`game.json:sprites.ui.title_battle`) has faded in from 1.0 s to
  2.5 s.
- The eagle (`game.json:sprites.ui.title_eagle`) has slid left from 2.5 s to
  3.5 s, clipped to the art.
- The words (`game.json:sprites.ui.title_words`) have been drawn on top.
- The menu has faded in from 3.0 s to 3.5 s, drawn at the screen's zoom.

Its last frame has been the backdrop of the title menu, Load and the credits.
Source: `src/startup.c` `draw_title_sequence`, `draw_title_backdrop`,
`run_title_menu`.

**DSGN-0148. Title menu.** The title menu has been its own page
(`page_title_menu`): no title and no description, as wide as its widest label
+ 2 × `UK_INSET` + 4 × `GW`, centred:
- New Game (`ui.title_new_adventure`), Load Saved Game
  (`ui.title_load_adventure`), Credits (`ui.title_credits`) and, on desktop
  and web, Exit, the row Escape has pressed.
- It has had no outside: a tap off it has done nothing.
- Escape (Android Back) has quit.
Source: `src/modern/page.c` `page_title_menu`; `src/startup.c`
`draw_title_menu`, `title_menu_labels`, `run_title_menu`.

**DSGN-0149. Load.** Load has been the in-game Load page (`gm_load_page`)
titled `ui.title_load_adventure`: the five slots, empty ones greyed, then
Back, the cursor on the first save. Choosing a save has loaded it; Escape,
Back or a tap outside has returned to the title menu.
Source: `src/startup.c` `load_page_build`, `load_page_cursor`,
`run_save_picker_modern`.

**DSGN-0150. Credits.** The credits have been a page to read at a menu page's
size (`page_sheet_small`) over the title art, titled `ui.title_credits` with
Close, `WHITE` text, names indented 2 × `GW`; any key or tap has closed them.
Source: `src/startup.c` `draw_credits`, `run_credits`.

**DSGN-0151. Choosing a class.** Class select has been the class painting
(DSGN-0108) with a caption (DSGN-0075) on the screen's foot, Back in its strip
returning to the title menu:
- Before a pick, the caption has been its strip alone, titled
  `startup.class_select_hint`.
- After a pick, it has held the class's name, its description
  (`banners.class_desc_<id>`, read into the class) and one "Continue" row; a
  tap anywhere but a figure has pressed Enter.
- A pick has used the figure's own painting when the pack has shipped it
  (`game.json:sprites.ui.class_picker_selected`), and otherwise the others
  have been shaded `uk_shade` and the picked one outlined three times in
  `YELLOW`.
- Left and Right have stepped through the figures, and a tap on a figure's
  column has picked it.
- After 2.0 s with no pick, the first figure has been picked.
- Enter or the row has confirmed, and Escape has returned to the title menu.
Source: `src/startup.c` `draw_class_select`, `run_class_select`,
`class_confirm_row`; `engine/resources.c` (`ResClassHero.desc`).

**DSGN-0152. Difficulty.** Difficulty has been a menu page with a body of its
own (DSGN-0072) titled with the class, over the chosen figure:
- Three column headings (`startup.new_game_table_header`) in `YELLOW` over
  the four difficulties ("name days" with the score multiplier at the right),
  then Back on the foot.
- The cursor has started on the second row.
- Choosing has gone on to the name; Escape, Back or a tap outside has
  returned to the class.
Source: `src/startup.c` `draw_difficulty_modern`, `run_difficulty_modern`,
`difficulty_row`.

**DSGN-0153. Name.** The name page has been a menu page with a body of its
own (DSGN-0072) titled with the class:
- It has held "Hero Name:" (`ui.hero_name_label`) in `YELLOW` and the field,
  with `game.json:world.default_name` in `DGREY` when empty and a blinking
  `YELLOW` caret (`GW` × 2 px, 2 Hz).
- The letter grid (DSGN-0154) has shown while a finger or the pad has been
  the device last used, and `prompts.text_hint` otherwise; typed letters have
  been taken either way.
- Back has been the foot row; Down from the grid's last row has reached it.
- A name has held at most 10 letters, digits and spaces, its first letter a
  capital, and an empty name has become the default.
- Escape, Back or a tap outside has returned to the difficulty.
Source: `src/startup.c` `draw_name_modern`, `run_name_modern`,
`name_char_allowed`.

**DSGN-0154. The letter grid.** The letter grid has laid itself out inside
its box:
- The 29 cells have been A to Z, then "SPC", "DEL" and "OK".
- Cell size and columns: `cw` = max(want, 4 × `GW`), where want has been the
  touch unit on a touch device and 0 otherwise. The columns have been
  clamp(box width / `cw`, 4, 10), with `cw` shrunk to fit.
- Rows and cell height: rows = ceil(29 / columns);
  `ch` = max(want, `GH` + 6 × `UI`), shrunk to fit the box.
- The cursor cell has been filled `YELLOW`, and each cell has been a tap
  target.
- The arrows and the pad have moved the cursor, wrapping each axis. Enter or
  pad A has picked; Backspace or pad B has deleted.
Source: `src/textsel.c` `textsel_layout`, `textsel_input`, `textsel_move`,
`textsel_draw`.

**DSGN-0155. The victory cartoon.** The victory cartoon has been drawn with
no frame:
- It has been a grid of `game.json:ending.grid_width` ×
  `game.json:ending.grid_height` cells of `TW` × `k`.
- `k` has been the largest whole number that has let the grid fit the
  screen, at least 1, and the grid has been centred in the frame's lattice
  (DSGN-0108).
- The carpet has grown up `game.json:ending.carpet_column`, the hero on it.
- Every troop has filled the other cells, the last column mirrored, standing
  at `UK_IDLE_FPS`.
- It has ticked every 0.08 s, stepping every `game.json:ending.ticks_per_step`
  ticks, for `game.json:ending.frame_count` steps.
- Any key or tap has skipped it.
Source: `src/end_cartoon.c` `cartoon_place`, `draw_cartoon_frame`,
`run_end_cartoon`.

---

## 16. Build guards

**DSGN-0156. The touch guard.** `make all` has failed when a shell file other
than `src/uitouch.c` and `src/touch.c` has called a `touch_region*` function,
or when a file other than those and `src/modern/page.c` has called
`touch_page`.
Source: `Makefile` (`TOUCH_GUARD_SRC`, `TOUCH_PAGE_GUARD_SRC`,
`TOUCH_STAMP`).

**DSGN-0157. The page guard.** `make all` has failed when a shell file other
than `src/modern/page.c`, `src/chrome.c`, `src/lattice.c` and `src/ui.c` has
called `lattice_ring`, `uk_panel`, `ui_window_frame` or `ui_panel_frame`.
Legacy's panels have gone through `legacy_window_frame` and
`legacy_panel_frame` (`src/ui.c`), which have drawn nothing in modern.
Source: `Makefile` (`PAGE_GUARD_SRC`, `PAGE_STAMP`); `src/ui.c`
`legacy_window_frame`, `legacy_panel_frame`.

---

## 17. Outside this document

**DSGN-0158. Pack selection.** The pack selector (`src/pack_select.c`) has run
before any pack has loaded, in its own 640 × 400 window with its own face,
and has been outside the modern layout.
Source: `src/pack_select.c` `pack_select_flow`.

**DSGN-0159. Legacy screens reachable only from legacy.**
`VIEW_RECRUIT_SOLDIERS` and the legacy castle screens have been opened only by
legacy code paths; modern castles have used `src/modern/castle.c`.
Source: `src/main.c` `main`; `src/screens/recruit_soldiers.c`;
`src/screens/home_castle.c` `screen_home_castle_draw`.

---

## 18. Developer keys

**DSGN-0160. Developer keys.** The grave key (`) has saved a screenshot of
the frame. Escape has stopped an `--autoplay` run from its progress screen.
The game menu's Debug page has appeared only with `--debug` (DSGN-0134). The
video-encoding screen (DSGN-0144) has run only on the `--movie` shutdown path.
Source: `src/screenshot.c` `screenshot_tick`; `src/shell_autoplay.c`
`ap_progress_cb`; `src/modern/gamemenu.c` `modern_gamemenu_page`;
`src/encode_dialog.c` `encode_dialog_session`.

---

## Verifying

`./build/release/openbounty --pack glory-of-rome --gallery <dir>` has drawn
every modern screen into `<dir>` as a PNG with no input:
- `--window WxH` has given it a device's surface on the desk, and `--touch`
  has made it a touch device.
- Beside the PNGs it has written `manifest.txt` and `pages.txt`. For every
  shot, `pages.txt` has held the screen, the zoom, the font's glyph
  (`font=GW,GH`), whether the frame has been bare, the space inside the
  frame, the map, the battlefield, and every page's anchor, content and outer
  rects, floating or filling.
- It has checked that a tap has reached the rows each screen has drawn, and
  that a tap on a page's outside and on its ring has done what the page has
  registered; the exit rows have been checked on the screens that name them.

Inspect every affected PNG.

`tools/capture.sh` has driven a running window and pulled frames with
`import`; `tools/walkthrough.sh` has played a fresh game from class select to
the map and captured every view a key has been able to reach. For those
captures:
- Send keys with `xdotool key --window <wid>`, not to the focused window. This
  X server has had no window manager, so `windowactivate` has failed and keys
  sent to the focused window have been dropped.
- Use `--delay 200`; without it a press and its release have landed in one
  60 fps frame and `IsKeyPressed` has never fired.
- Keep the window at the origin; part of a window off the display has made
  `import` return a clipped image.

Rome captures have not been byte-comparable run to run, because the army
roster, puzzle, world map, spells and Controls screens have animated. Compare
layout by eye.
