# UI panels: size and position by circumstance

Every text panel, dialog and prompt the shell draws in **modern** mode, where
it goes and how big it is. Modern has five named layouts and nothing else
(OPENBOUNTY-SPEC REQ-430j); every panel is one of them. Numbers are for Glory
of Rome: an 800x510 buffer, 96 px tiles, a 7x5 viewport, Press Start 2P at 16
(one glyph 16 px wide, one line 18 px). Ratified rules: REQ-430g (dim),
REQ-430i (the forked draw layer), REQ-430j (the layouts and the one menu).

The legacy pack keeps the original 320x200 geometry -- `CL_CONTENT_*` and
`CL_PANEL_*` in `src/layout.h`, its own nested Game Menu and its Options
panel -- and is not described here. `tests/unit/test_legacy_freeze.c` pins it.

**Which file to edit.** The rects live in one place, `src/modern/mlayout.c`,
and `tests/unit/test_modern_layout.c` pins them. A panel's contents are in
`src/modern/overlay.c`, `src/modern/views_render.c` or `src/modern/prompt.c`;
the four shared location screens (`src/screens/home_castle.c`,
`own_castle.c`, `dwelling.c`, `recruit_soldiers.c`) get their text rect from
`screens_text_rect`. The `src/legacy/` copies are FROZEN and never edited to
serve a modern need.

## The screen

| band | Rome | rule |
|---|---|---|
| left edge | x 0-11, lattice | the horizontal space the pane and HUD leave, split three ways |
| map pane | x 11, 672x480 | 7x5 tiles of 96 |
| middle band | x 683-693, lattice | the same module as the side bands: a gold rail against each panel, the pattern between |
| HUD sidebar | x 693, 96x480 | one tile wide |
| right edge | x 789-800, lattice | the third share; a remainder the three cannot share goes to the two edges |
| status band | above the pane | one text line plus padding |

800 = 11 + 672 + 10 + 96 + 11. Vertically there is no room to match: the
pane and the status band take 499 of 510, so the top and bottom bands stay
thin. No modern panel reads `ui_scale`.

## The five layouts

Capacity is measured from the rect with 8 px padding on every side, so it
follows the font.

| layout | rect | text | used by |
|---|---|---|---|
| **small** | six text lines plus padding tall (124), the pane's width, inset 10 from its left, right and bottom edges: 652x124 | 39 x 6 | yes/no, numeric, A/B and count prompts; any message whose header and whole body fit |
| **large** | 6x4 tiles centred in the pane: 576x384 | 35 x 20 | longer messages, the Emperor's audience, the game menu, its Controls page, the combat spell picker, the victory dialog |
| **location** | backdrop inset 10 from the pane's left, top and right, at the smallest integer scale that covers 652 (3x: 720 cropped 34 px each side, 652x306); text area directly under it, down to 10 above the pane's bottom: 652x154 | 39 x 7 | town, home castle, own castle, dwelling, alcove, recruit |
| **full screen** | the pane, the middle band and the HUD edge to edge, status band left visible: 778x480 | 47 x 25 | Army, Character, Contract, Spells, Gate, World map, Puzzle, Win, Lose |
| toast | one line, top of the pane, centred | 1 line | toasts |

Panels on the map keep the screen's own spacing (10) from the pane's edges,
so each sits inside the pane with the border the pane sits inside the screen
with. Full screen is not inset: it is a screen, laid out in whole tiles (Army
is five rows of 96 filling the 480), which a margin would cut.

### small or large

A message or prompt takes the small band when its header, its whole wrapped
body and its answer rows (Yes/No, a count, a hint) fit there, and the large
rect when they do not. The pager asks the same function that places the
panel, so the number of pages and the panel drawn always agree. A text prompt
showing the letter selector needs more rows than the band has and takes the
large rect. The victory dialog always takes the large rect.

### location

The backdrop and the text area share one edge, so they never overlap. Art
placed on a backdrop is placed in the backdrop's own 240x102 units (the
alcove figure: `sprites.ui.alcove_figure_place`) and so lands in the same spot
at any scale. A backdrop with no declared figure animates a troop in a
tile-sized slot one tile in from the left, standing on the backdrop's bottom
edge.

### full screen

Army is five rows of a full tile, filling the 480 exactly. The world map draws
whole pixels per map cell against 768x480 (Italia, 64x128, gets 3). The
puzzle's five-tile grid is centred in the rect.

## The game menu

Opened by Escape or O. One page in the large rect: a row for every
single-letter entry in the pack's `keybinds`, its hotkey right-aligned, left
out when it does not apply to the hero as he stands (Fly while flying, Land
while walking, New Continent off a boat); then Controls, Save, Load, New Game
and Exit. A row closes the menu and presses its key on the next frame, so the
action runs the path its keypress does. Controls opens over the menu in the
same rect and closes back to it. Every row is tappable.

## Dim

Whenever any view, prompt or dialog is open, the map pane and sidebar are
covered with black at `render.dim` percent (Rome: 55) before the panel is
drawn. The status band and frame are not dimmed. Toasts do not dim. Combat
dims the field under its spell picker, the victory dialog, prompts and any
view.

## Status band text

"Press 'ESC' to exit" while any view or dialog is open; otherwise the
Options / Controls / Days Left line; the fast-quit question overrides both.
