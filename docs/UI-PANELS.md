# UI panels: size and position by circumstance

Every text panel, dialog and prompt the shell has drawn in **modern** mode,
where it has gone and how big it has been. Modern has had five named layouts
and nothing else (OPENBOUNTY-SPEC REQ-430j); every panel has been one of them.
Numbers are for Glory of Rome: an 800x532 buffer, 96 px tiles, a 7x5
viewport, Press Start 2P at 16 (one glyph 16 px wide, one line 18 px).
Ratified rules: REQ-430g (dim), REQ-430i (the forked draw layer), REQ-430j
(the layouts and the one menu).

The legacy pack has kept the original 320x200 geometry -- `CL_CONTENT_*` and
`CL_PANEL_*` in `src/layout.h`, its own nested Game Menu and its Options
panel -- and has not been described here. `tests/unit/test_legacy_freeze.c`
has pinned it.

**Which file to edit.** The rects have lived in one place,
`src/modern/mlayout.c`, and `tests/unit/test_modern_layout.c` has pinned them.
A panel's contents have been in `src/modern/overlay.c`,
`src/modern/views_render.c` or `src/modern/prompt.c`; the four shared location
screens (`src/screens/home_castle.c`, `own_castle.c`, `dwelling.c`,
`recruit_soldiers.c`) have taken their text rect from `screens_text_rect`.
The `src/legacy/` copies have been FROZEN and never edited to serve a modern
need.

## The screen

| band | Rome | rule |
|---|---|---|
| left edge | x 0-12, lattice | the horizontal space the pane and HUD leave, split 3 : 2 : 3 |
| left rail | x 12, 96x480 | one tile wide, five icons, only where the surface has spare width (REQ-533); the map has started after it |
| map pane | x 12, 672x480 | 7x5 tiles of 96 |
| middle band | x 684-692, lattice | the same module as the side bands: a gold rail against each panel, the pattern between |
| HUD sidebar | x 692, 96x480 | one tile wide |
| right edge | x 788-800, lattice | the third share, equal to the left |
| status band | y 12, 776x20 | the vertical counterpart of the HUD: what the mirrored stack leaves once the edges and band are placed |
| band under the status | y 32-40, lattice | the same module and width as the middle band |

800 = 12 + 672 + 8 + 96 + 12 across, and the vertical stack has mirrored it:
532 = 12 (top edge) + 20 (status band) + 8 (band) + 480 (map pane) + 12
(bottom edge). The status band has stood where the HUD stands, the band under
it has been the band beside the HUD, and every outer edge has been 12. No
modern panel has read `ui_scale`. On a larger screen the map pane has grown
in whole tiles and every panel below has kept its declared size (REQ-528).

## The five layouts

Capacity has been measured from the rect with 8 px padding on every side, so
it has followed the font.

| layout | rect | text | used by |
|---|---|---|---|
| **small** | six text lines plus padding tall (124), the pane's width, inset 8 from its left, right and bottom edges: 656x124 | 40 x 6 | yes/no, numeric, A/B and count prompts (numeric and A/B answer by rows, which count toward the fit); any message whose header and whole body fit |
| **large** | 6x4 tiles centred in the pane: 576x384 | 35 x 20 | longer messages, the Emperor's audience, the game menu, its Controls page, the combat action menu and spell picker, the victory dialog |
| **location** | backdrop inset 8 from the pane's left, top and right, at the smallest integer scale that covers 656 (3x: 720 cropped 32 px each side, 656x306); text area directly under it, down to 8 above the pane's bottom: 656x158 | 40 x 7 | town, home castle, own castle, dwelling, alcove, recruit |
| **full screen** | the pane, the middle band and the HUD edge to edge, status band left visible: 776x480 | 47 x 25 | Army, Character, Contract, Spells, Gate, World map, Puzzle, Win, Lose |
| toast | one line, top of the pane, centred | 1 line | toasts |

Panels on the map have kept the middle band's width (8) from the pane's
edges, so each has sat inside the pane with the border the pane sits inside
the screen with. Full screen has not been inset: it has been a screen, laid
out in whole tiles (Army has been five rows of 96 filling the 480), which a
margin would cut.

### small or large

A message or prompt has taken the small band when its header, its whole
wrapped body and its answer rows (Yes/No, a count, a hint) fit there, and the
large rect when they do not. The pager has asked the same function that
places the panel, so the number of pages and the panel drawn have always
agreed. A text prompt showing the letter selector has needed more rows than
the band has and has taken the large rect. The victory dialog has always
taken the large rect.

### location

The backdrop and the text area have shared one edge, so they have never
overlapped. Art placed on a backdrop has been placed in the backdrop's own
240x102 units (the alcove figure: `sprites.ui.alcove_figure_place`) and so
has landed in the same spot at any scale. A backdrop with no declared figure
has animated a troop in a tile-sized slot one tile in from the left, standing
on the backdrop's bottom edge.

### full screen

Army has been five rows of a full tile, filling the 480 exactly. The world
map has drawn whole pixels per map cell against 776x480 (Italia, 64x128, has
had 3). The puzzle's five-tile grid has been centred in the rect.

## What a panel has been centred on

The small and large layouts, questions and toasts have centred on the area
behind them (`ml_area`, src/modern/mlayout.c): the map pane while the
adventure map and its HUD show; the full width under the status band over
combat and every full-screen view (menu, town, castle, temple, dwelling, the
detail views); the whole screen at startup. The screen drawing the backdrop
has set it each frame.

## Character sheet (modern)

Full screen: the name and rank in the title strip with the next rank and how
many more captures it needs at the right; the class portrait at its authored
192x204; beside it two headed columns (Army: leadership, commission, gold;
Magic: spell power, spell capacity; Campaign: captured, artifacts, castles,
followers lost, score, days left); then the eight sacred artifacts as 96 px
icons (a missing one dark), the continents as 96 px icons, and beside them
the pack's honours (blessing and tributes, rites known) where it has them.

## Drag to scroll (modern)

A list with more rows than its space has scrolled under a finger: every row's
height of vertical drag has moved it one row (the finger up shows later
rows), and a tap on such a list has resolved when the finger lifts without
moving (`touch_region_scroll`, src/touch.c). The small arrows at a long list's
edges have been tap targets too.

## The game menu

Opened by Escape, O or a tap on the top bar (REQ-430s): a drill-down menu on
one full screen (`src/modern/gamemenu.c`). Each page has been one column of
standard rows ending in Back; a row that opens a page has shown ">". The
title strip has shown the path ("Menu > Game > Save") and the zone; the panel
beside the rows has described the row under the cursor, in yellow when it
says why a greyed row does not apply.

- **Menu:** Hero >, World >, Game >, Close, and Exit on the foot of the page
  (desktop and web only: iOS and Android have no Exit, REQ-529)
- **Hero:** Army, Character, Contract, Puzzle, Dismiss, Back
- **World:** Map, Cast a spell, Search, Fly (Land while flying), End the week,
  Rest, Set sail, Back
- **Game:** Debug > (first, with `--debug`), Save >, Load >, Controls,
  New Game, Back
- **Save / Load:** the ten slots (the title's slot rows), Back

Greyed rows have been selectable (no troops to dismiss, only while sailing,
no saved games, an empty slot to load). With a keyboard in use each row has
shown its shortcut at the right. Up/Down have moved and scrolled; the scroll
arrows on a long page have been tap targets. Enter or a tap has opened or
acted; Escape, Back or the top bar has gone up a page and closed the menu
from the top. A Hero or World row has closed the menu and pressed its key on
the next frame. Controls has opened over the menu. Saving over a slot,
loading and Exit have asked Yes/No.

## Select rows

The standard for every modern list of choices (`ml_row_h`, `ML_ROW_RULE` in
`src/modern/mlayout.h`). A row has been half a tile tall: 48 on Rome's 96
tile, room for one text line centred or a 48 px icon beside it, and a
comfortable touch target; never shorter than a text line plus 8 of padding.
Rows have stacked from the top of their column with a 2 px rail under each.
They have not stretched to fill the column: whatever height is left below the
last rail has stayed empty. The cursor row has been inverted; a row that
cannot be chosen has been grey and the cursor has skipped it. A list with
more rows than its column holds has scrolled to keep the cursor row in view.

## Question dialogs

Every modern question (yes/no, a numbered choice, the chest's A/B, a count)
has drawn one way: the header and body, a lattice band, then the answers --
two standard select rows (Yes, No), one row per choice (a long choice keeps to
one line), or the count stepper for a count (the dwelling recruit: it starts
at the most allowed, Enter recruits that many). The panel has run the map
pane's width inside its margin and sat on the pane's bottom edge so the hero
at the centre stays in view; it has been as tall as its text and answers, and
past the pane's top its rows have scrolled instead. Up/Down have moved, Enter
or a tap has answered, Esc has been No or Cancel; Y and N, the digits and A/B
have still answered directly. The game menu, Controls (value at each row's
right), the title menu, the load picker, the difficulty rows, the world map's
orb row, the spells view (two columns of seven), the gate picker (three
columns) and the combat menu have used the same rows.

## Foe view (modern)

A hostile band has opened a full screen instead of the yes/no prompt: "Foes!"
and the zone in the title strip, then five vertical stripes (152 px wide on
Rome's 776, 4 px rails between), one per enemy troop: the troop's portrait at
1x, how many (worded as the encounter message words it: a number, or "A horde
of" and the like), its name (a word too long for the stripe breaks with a
hyphen), then HP, skill, move, damage, range when it shoots, and Fly when it
flies, one to a line. Your own army (how many troops, your leadership) has
been at the right of the title strip -- or, in yellow, that there is nowhere
to run when no square is free. Along the foot, Fight and Evade have been
full-width standard rows, as in the question dialog (Up/Down, Enter or a
tap; Evade greyed when blocked).

## How many (modern)

Choosing a count has been its own step. After picking what to recruit
(castle Recruit, a dwelling's Recruit row) or which troop to garrison or
withdraw, a "How many?" panel has taken over (`src/modern/overlay.c`, its
-10/-1/+1/+10 row from `ml_count_buttons` in `src/modern/mlist.c`): the
heading ("How many Hastati?"), labelled -10 / -1 / +1 / +10 buttons round the
number framed in the middle, "of 20 you can lead" (in your army, in the
garrison) under it, and when recruiting the cost and the gold left. The
answers have been full-width rows: "Recruit 20" (Garrison / Withdraw) and
Cancel. Left/Right have stepped one, Down/Up ten, Enter or a tap on the first
row has carried it out, Escape or Cancel has gone back. At the castle the
panel has filled the screen's lower half; at a dwelling it has been an in-lay
centred on the scene.

## Castles (modern)

The home castle and a castle the hero owns have used the town screen's
layout: the castle backdrop at 2x (the ruler standing on it at the home
castle), the portrait slot, the siege and gold tiles, standard select rows and
the detail panel. The home castle has offered Recruit > (the castle troops,
each shown at 2x with its statistics, then the stepper) and Audience >
(Request audience, always available: a promotion when one is due, shown with
that rank's promotion image and the gains, else the ruler's word). An owned
castle has offered Garrison > and Withdraw >, listing the army's or the
garrison's troops; any part of a troop has moved, and withdrawing has warned
when the army would exceed its leadership. Esc has gone back a level.
Legacy's castle screens have been untouched by any of this.

The main page of the town, the home castle and an owned castle has ended in a
**Leave** row (the same as Escape: back to the map), like the temple and
dwelling screens.

With `game.json` `audiences` (Glory of Rome), the castle's barracks keeper
(`special.barracks_portrait` / `barracks_figure`) has stood on the main and
Recruit pages and the ruler has appeared only on Audience, whose rows have
been Promotion, Blessing and Tribute. Moving the cursor has shown the rank,
the artifacts found or the tribute's cost; picking a row has given the
ruler's answer (one short screen: what is granted and the gains, or what is
still needed). Tribute has asked Yes/No before paying.

## Hint buttons (modern)

On-screen notes have named no keys. A way out or on has been a small framed
button (`ml_hint_button`, src/modern/mlist.c) that a tap presses: "< Back" on
the top bar while a view or message is open (the whole bar is the button), on
the world map band, the gate spell and load picker titles and the combat
spell picker; Quit and Continue on the save message. The key has followed the
label for the device in use: "[Esc]" once a keyboard is used, "(B)" for a
gamepad, nothing on touch. The give-up question has read "Give up the
battle?", the spell picker "Cast which spell?" (letters still pick). Legacy
has kept its notes.

## In-lay dialog (modern)

A message that carries a picture hint (`PlayerRequest.face`: an enemy, a
troop, an artifact) has opened as an in-lay instead of the plain message box:
a large framed dialog centred over the screen with the message header as its
title, the picture at 2x (192x192) on the left, the words beside it, and
Continue (any key or a tap). The capture after a castle siege has used it:
the enemy's animated face, then the bounty and map piece, or that the
prisoner is set free (strings `capture_*`). The town's boat, spell and siege
outcomes and the temple and dwelling confirmations have used the same dialog
with their person's portrait.

## Temple and dwelling (modern)

Panorama, action, confirmation, map. The scene has been the screen: under the
title strip (the temple's name or the dwelling and its troop, the zone at the
right) the backdrop whole at 3x (720x306 on Rome, centred), with the Augur
where the pack places him or the dwelling's troop standing at 2x, and nothing
over it. Under the scene, a one- or two-sentence introduction
(`temple_intro`, `dwelling_intro`: what is offered and its price; no purse,
that is the HUD's); the answers have been full-width rows (Learn the rites /
Leave, Recruit / Leave). At a dwelling, Recruit has opened "How many?" as an
in-lay on the scene with "Recruit 20" / Cancel. Once the deal is done a large
framed dialog has opened over the scene: the portrait at 2x (the Augur's
`sprites.ui.alcove_portrait`, the troop's portrait), the confirmation beside
it (the Augur's reply, or how many troops joined; `src/modern/location.c`),
titled with what happened ("Rites learned", "The Augur refuses", "Recruits
join you"), and Continue, which has returned to the map. A known zone's
temple has opened with the Augur's words in the same dialog.

## Combat action menu

Enter or a tap on the active unit, Escape or a tap on the top bar has opened
Actions in the large rect, the same drill-down pages as the game menu
(`combat_menu_page`, `src/combat_loop.c`), on the **Unit** page: Shoot, Wait,
Fly, Cast a spell, Back for a troop with ranged ammo, and Wait, Shoot, Fly,
Cast a spell, Back for any other (REQ-533). Back (or Escape) has gone to the
top level (Unit >, Hero >, Game >, Close): **Hero** has held Army and
Character, **Game** Controls and Give up (last). Rows that cannot apply have
been greyed with the reason (no shots left, enemies too close, cannot fly,
one spell a round, no magic).

## Dim

Whenever any view, prompt or dialog is open, the map pane and sidebar have
been covered with black at `render.dim` percent (Rome: 35) before the panel
is drawn. The status band and frame have not been dimmed. Toasts have not
dimmed. Combat has dimmed the field under its spell picker, the victory
dialog, prompts and any view.

## Status band text

"Press 'ESC' to exit" while any view or dialog is open; otherwise the pack's
`status_days_left_modern` line (Rome: " Menu / Days Left:N "). Fast quit has
asked with the yes/no prompt.
