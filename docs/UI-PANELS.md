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

## What a panel is centred on

The small and large layouts, questions and toasts centre on the area behind
them (`ml_area`, src/modern/mlayout.c): the map pane while the adventure map
and its HUD show; the full width under the status band over combat and every
full-screen view (menu, town, castle, temple, dwelling, the detail views); the
whole screen at startup. The screen drawing the backdrop sets it each frame.

## Character sheet (modern)

Full screen: the name and rank in the title strip with the next rank and how
many more captures it needs at the right; the class portrait at its authored
192x204; beside it two headed columns (Army: leadership, commission, gold;
Magic: spell power, spell capacity; Campaign: captured, artifacts, castles,
followers lost, score, days left); then the eight sacred artifacts as 96 px
icons (a missing one dark), the continents as 96 px icons, and beside them the
pack's honours (blessing and tributes, rites known) where it has them.

## Drag to scroll (modern)

A list with more rows than its space scrolls under a finger: every row's height
of vertical drag moves it one row (the finger up shows later rows), and a tap
on such a list resolves when the finger lifts without moving
(`touch_region_scroll`, src/touch.c). The small arrows at a long list's edges
are tap targets too.

## The game menu

Opened by Escape, O or a tap on the top bar (REQ-430s): a traditional
drill-down menu on one full screen (`src/modern/gamemenu.c`). Each page is one
column of standard rows ending in Back; a row that opens a page shows ">". The
title strip shows the path ("Menu > Game > Save") and the zone; the panel
beside the rows describes the row under the cursor, in yellow when it says why
a greyed row does not apply.

- **Menu:** Hero >, World >, Game >, Back
- **Hero:** Army, Character, Contract, Puzzle, Dismiss, Back
- **World:** Map, Cast a spell, Search, Fly (Land while flying), End the week,
  Rest, Set sail, Back
- **Game:** Debug > (first, with `--debug`), Save >, Load >, Controls,
  New Game, Back, Exit (always last)
- **Save / Load:** the ten slots (the title's slot rows), Back

Greyed rows can be selected (no troops to dismiss, only while sailing, no saved
games, an empty slot to load). With a keyboard in use each row shows its
shortcut at the right. Up/Down move and scroll; the scroll arrows on a long
page are tap targets. Enter or a tap opens or acts; Escape, Back or the top bar
goes up a page and closes the menu from the top. A Hero or World row closes
the menu and presses its key on the next frame. Controls opens over the menu.
Saving over a slot, loading and Exit ask Yes/No.

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

## Question dialogs

Every modern question (yes/no, a numbered choice, the chest's A/B, a count)
draws one way: the header and body, a lattice band, then the answers -- two
standard select rows (Yes, No), one row per choice (a long choice keeps to one
line), or the count stepper for a count (the dwelling recruit: it starts at the
most allowed, Enter recruits that many). The panel runs the map pane's width
inside its margin and sits on the pane's bottom edge so the hero at the centre
stays in view; it is as tall as its text and answers, and past the pane's top
its rows scroll instead. Up/Down move, Enter or a tap answers, Esc is No or
Cancel; Y and N, the digits and A/B still answer directly. The game menu,
Controls (value at each row's right), the title menu, the load picker, the
difficulty rows, the world map's orb row, the spells view (two columns of
seven) and the gate picker (three columns) use the same rows. Combat's own
menus are not changed yet.

## Foe view (modern)

A hostile band opens a full screen instead of the yes/no prompt: "Foes!" and
the zone in the title strip, then five vertical stripes (152 px wide on Rome's
776, 4 px rails between), one per enemy troop: the troop's portrait at 1x, how
many (worded as the encounter message words it: a number, or "A horde of" and
the like), its name (a word too long for the stripe breaks with a hyphen), then
HP, skill, move, damage, range when it shoots, and Fly when it flies, one to a
line. Your own army (how many troops, your leadership) is at the right of the
title strip -- or, in yellow, that there is nowhere to run when no square is
free. Along the foot, Fight and Evade are full-width standard rows, as in the
question dialog (Up/Down, Enter or a tap; Evade greyed when blocked).

## How many (modern)

Choosing a count is its own step. After picking what to recruit (castle
Recruit, a dwelling's Recruit row) or which troop to garrison or withdraw, a
"How many?" panel takes over (`ml_count_panel`, src/modern/mlist.c): the
heading ("How many Hastati?"), labelled -10 / -1 / +1 / +10 buttons round the
number framed in the middle, "of 20 you can lead" (in your army, in the
garrison) under it, and when recruiting the cost and the gold left. The answers
are full-width rows: "Recruit 20" (Garrison / Withdraw) and Cancel. Left/Right
step one, Down/Up ten, Enter or a tap on the first row carries it out, Escape
or Cancel goes back. At the castle the panel fills the screen's lower half; at
a dwelling it is an in-lay centred on the scene.

## Castles (modern)

The home castle and a castle the hero owns use the town screen's layout: the
castle backdrop at 2x (the ruler standing on it at the home castle), the
portrait slot, the siege and gold tiles, standard select rows and the detail
panel. The home castle offers Recruit > (the castle troops, each shown at 2x
with its statistics, then the stepper) and Audience > (Request audience, always
available: a promotion when one is due, shown with that rank's promotion image
and the gains, else the ruler's word). An owned castle offers Garrison > and
Withdraw >, listing the army's or the garrison's stacks; any part of a stack
moves, and withdrawing warns when the army would exceed its leadership. Esc
goes back a level. Legacy's castle screens are unchanged.

The main page of the town, the home castle and an owned castle ends in a
**Leave** row (the same as Escape: back to the map), like the temple and
dwelling screens.

With `game.json` `audiences` (Glory of Rome), the castle's barracks keeper
(`special.barracks_portrait` / `barracks_figure`) stands on the main and Recruit
pages and the ruler appears only on Audience, whose rows are Promotion,
Blessing and Tribute. Moving the cursor shows the rank, the artifacts found or
the tribute's cost; picking a row gives the ruler's answer (one short screen:
what was granted and the gains, or what is still needed). Tribute asks Yes/No
before paying.

## Hint buttons (modern)

On-screen notes name no keys. A way out or on is a small framed button
(`ml_hint_button`, src/modern/mlist.c) that a tap presses: "< Back" on the
top bar while a view or message is open (the whole bar is the button), on the
world map band, the gate spell and load picker titles and the combat spell
picker; Quit and Continue on the save message. The key follows the label for
the device in use: "[Esc]" once a keyboard is used, "(B)" for a gamepad,
nothing on touch. The give-up question reads "Give up the battle?", the spell
picker "Cast which spell?" (letters still pick). Legacy keeps its notes.

## In-lay dialog (modern, trial)

A message that carries a picture hint (`PlayerRequest.face`: an enemy, a troop,
an artifact) opens as an in-lay instead of the plain message box: a large framed
dialog centred over the screen with the message header as its title, the
picture at 2x (192x192) on the left, the words beside it, and Continue (any key
or a tap). The first user is the capture after a castle siege: the enemy's
animated face, then the bounty and map piece, or that the prisoner was set free
(strings `capture_*`). The town's boat, spell and siege outcomes and the temple
and dwelling confirmations use the same dialog with their person's portrait.

## Temple and dwelling (modern)

Panorama, action, confirmation, map. The scene is the screen: under the title
strip (the temple's name or the dwelling and its troop, the zone at the right)
the backdrop whole at 3x (720x306 on Rome, centred), with the Augur where the
pack places him or the dwelling's troop standing at 2x, and nothing over it.
Under the scene, what is on offer and your purse; the answers are full-width
rows (Learn the rites / Leave, Recruit / Leave). At a dwelling, Recruit opens
"How many?" as an in-lay on the scene with "Recruit 20" / Cancel. Once the deal
is done a large framed dialog opens over the scene: the portrait at 2x (the
Augur's `sprites.ui.alcove_portrait`, the troop's portrait), the confirmation
beside it (the Augur's reply, or how many troops joined, and the gold before and
after; `src/modern/location.c`), and Continue, which returns to the map. A known
zone's temple opens with the Augur's words in the same dialog.

## Combat action menu

Enter or a tap on the active unit, Escape or a tap on the top bar opens Actions
in the large rect, the same drill-down pages as the game menu, on the **Unit**
page: Wait, Shoot, Fly, Cast a spell, Back. Back (or Escape) goes to the top
level (Unit >, Hero >, Game >, Back): **Hero** holds Army and Character,
**Game** Controls and Give up (last). Rows that cannot apply are greyed with the
reason (no shots left, enemies too close, cannot fly, one spell a round, no
magic).

## Dim

Whenever any view, prompt or dialog is open, the map pane and sidebar are
covered with black at `render.dim` percent (Rome: 55) before the panel is
drawn. The status band and frame are not dimmed. Toasts do not dim. Combat
dims the field under its spell picker, the victory dialog, prompts and any
view.

## Status band text

"Press 'ESC' to exit" while any view or dialog is open; otherwise the
Options / Controls / Days Left line. Fast quit asks with the yes/no prompt.
