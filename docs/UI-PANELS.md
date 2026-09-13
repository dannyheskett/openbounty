# UI panels: size and position by circumstance

Every text panel, dialog and prompt the shell draws in **modern** mode, where
it goes and how big it is. Modern has five named layouts and nothing else
(OPENBOUNTY-SPEC REQ-430j); every panel is one of them. Numbers are for Glory
of Rome: an 800x532 buffer, 96 px tiles, a 7x5 viewport, Press Start 2P at 16
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
| left edge | x 0-12, lattice | the horizontal space the pane and HUD leave, split 3 : 2 : 3 |
| map pane | x 12, 672x480 | 7x5 tiles of 96 |
| middle band | x 684-692, lattice | the same module as the side bands: a gold rail against each panel, the pattern between |
| HUD sidebar | x 692, 96x480 | one tile wide |
| right edge | x 788-800, lattice | the third share, equal to the left |
| status band | y 12, 776x20 | the vertical counterpart of the HUD: what the mirrored stack leaves once the edges and band are placed |
| band under the status | y 32-40, lattice | the same module and width as the middle band |

800 = 12 + 672 + 8 + 96 + 12 across, and the vertical stack mirrors it:
532 = 12 (top edge) + 20 (status band) + 8 (band) + 480 (map pane) + 12
(bottom edge). The status band stands where the HUD stands, the band under it
is the band beside the HUD, and every outer edge is 12. No modern panel reads
`ui_scale`.

## The five layouts

Capacity is measured from the rect with 8 px padding on every side, so it
follows the font.

| layout | rect | text | used by |
|---|---|---|---|
| **small** | six text lines plus padding tall (124), the pane's width, inset 8 from its left, right and bottom edges: 656x124 | 40 x 6 | yes/no, numeric, A/B and count prompts (numeric and A/B answer by rows, which count toward the fit); any message whose header and whole body fit |
| **large** | 6x4 tiles centred in the pane: 576x384 | 35 x 20 | longer messages, the Emperor's audience, the game menu, its Controls page, the combat action menu and spell picker, the victory dialog |
| **location** | backdrop inset 8 from the pane's left, top and right, at the smallest integer scale that covers 656 (3x: 720 cropped 32 px each side, 656x306); text area directly under it, down to 8 above the pane's bottom: 656x158 | 40 x 7 | town, home castle, own castle, dwelling, alcove, recruit |
| **full screen** | the pane, the middle band and the HUD edge to edge, status band left visible: 776x480 | 47 x 25 | Army, Character, Contract, Spells, Gate, World map, Puzzle, Win, Lose |
| toast | one line, top of the pane, centred | 1 line | toasts |

Panels on the map keep the middle band's width (8) from the pane's edges,
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
whole pixels per map cell against 776x480 (Italia, 64x128, gets 3). The
puzzle's five-tile grid is centred in the rect.

## The game menu

Opened by Escape or O, in the large rect: Screens >, Actions >, Controls,
Save, Load, New Game, Exit, and Debug > when started with `--debug`
(REQ-430k). Screens and Actions are built from the pack's `keybinds`
(Screens: Army, Contract, Auto-mapping, Puzzle, Character; Actions: the rest,
including Dismiss Army, New Continent and Rest), a row left out when it does
not apply to the hero as he stands (Fly while flying, Land while walking, New
Continent off a boat, Dismiss with no troops). No row shows a key. A Screens or
Actions row closes the menu and presses its key on the next frame, so the
action runs the path its keypress does. Controls opens over the menu in the
same rect and closes back to it. Every row is tappable.

## Select rows

The standard for every modern list of choices (`ml_row_h`, `ML_ROW_RULE` in
`src/modern/mlayout.h`). A row is half a tile tall: 48 on Rome's 96 tile, room
for one text line centred or a 48 px icon beside it, and a comfortable touch
target; never shorter than a text line plus 8 of padding. Rows stack from the
top of their column with a 2 px rail under each. They do not stretch to fill
the column: whatever height is left below the last rail stays empty. The
cursor row is inverted; a row that cannot be chosen is grey and the cursor
skips it. A list with more rows than its column holds scrolls to keep the
cursor row in view. First used by the town screen (menu and every detail list); other
modern lists move to it as they are touched.

## Combat action menu

Enter, or a tap on the active unit, opens Actions in the large rect: Wait,
Shoot, Fly, Cast a spell, Army, Character, Controls, Give up, without the rows
that cannot apply (no shots or surrounded, cannot fly, a spell already cast
this round, no magic). Escape closes it.

## Dim

Whenever any view, prompt or dialog is open, the map pane and sidebar are
covered with black at `render.dim` percent (Rome: 55) before the panel is
drawn. The status band and frame are not dimmed. Toasts do not dim. Combat
dims the field under its spell picker, the victory dialog, prompts and any
view.

## Status band text

"Press 'ESC' to exit" while any view or dialog is open; otherwise the
Options / Controls / Days Left line. Fast quit asks with the yes/no prompt.
