# UI panels: size and position by circumstance

Every text panel, dialog and prompt the shell draws, where it goes, how big
it is, and what sits behind it. Numbers are for Glory of Rome's modern
layout (832x540 buffer, 96 px tiles, `ui_scale` 2, Press Start 2P at 16, so
one text line is 18 px and one glyph is 16 px wide); the legacy pack keeps
the original 320x200 geometry in every case and is noted only where it
differs. Source of truth: `src/layout.h` (rects), `src/overlay.c`,
`src/prompt.c`, `src/views_render.c`, `src/screens/*.c`,
`src/combat_loop.c`. Ratified rules: OPENBOUNTY-SPEC REQ-430g (dim) and
REQ-430h (one panel rect).

## The rects everything is measured from

| rect | Rome, 1x | how it is derived |
|---|---|---|
| frame interior | x 32..800, y 16..524 | the lattice frame is 32 px at the sides, 16 top and bottom |
| status band | x 32, y 16, 768 x 20 | one text line plus 2 px, under the top frame |
| map pane | x 32, y 46, 672 x 480 | 7 x 5 tiles of 96; the viewport |
| sidebar | x 704, y 46, 96 x 480 | one tile wide, right of the pane |
| content rect | x 128, y 116, 480 x 340 | the original 240x170 design rect at 2x, centred in the pane |
| panel rect | x 128, y 304, 480 x 152 | `CL_PANEL_*`: content width, eight text lines plus 8 px, bottom-aligned in the content rect (REQ-430h) |
| wide view | x 128, y 46, 576 x 480 | content width plus one sidebar, centred in the frame interior, full pane height |

Legacy: content rect is the whole pane (16,22 240x170); panel is 16..288 wide at y 124, 68 tall.

## Dim

Whenever any view, prompt or dialog is open, the map pane and sidebar are
covered with black at `render.dim` percent (Rome: 55) before the panel is
drawn. The status band and frame are not dimmed. Toasts do not dim. Combat
dims the field under its spell picker, the victory dialog, prompts and any
view. Legacy never dims.

## Bottom-frame panels (the panel rect, 128,304 480x152)

| circumstance | panel | content |
|---|---|---|
| message dialog (`open_dialog`, `player_io_message`) | panel rect, blue, yellow frame | header in yellow, wrapped like body text; body wrapped to 472 px, 7 lines per page, form feed or overflow pages; any key dismisses |
| yes/no prompt | panel rect | header, body, then two selectable rows in modern (legacy: a hint line) |
| numeric prompt | panel rect | header, body, typed value, or the 4x3 digit selector when no keyboard has been seen |
| text prompt (hero name, recruit count) | panel rect | typed value, or the 6x5 letter selector |
| town menu | panel rect, height sized to its rows, bottom-aligned at y 304+152 | "Town of X", GP, five lettered rows with the cursor bar |
| home castle menu | panel rect | castle name, two rows |
| own castle menu, dwelling, alcove | panel rect | their rows |
| recruit soldiers | panel rect | title, GP, five troop rows padded to the longest name, count entry |
| audience (Emperor) | message dialog, two in sequence | fanfare, then the Emperor's words as the header |

All of these sit under the location backdrop card when one is shown.

## Location backdrop card

Town, home castle, own castle, dwelling, alcove and recruit draw a 480x204
card at the content rect's top-left (128,116) with an animated troop at its
bottom-left, on the dimmed map. Legacy blacks out the pane around the card;
modern does not.

Known overlap: the card ends at y 320 and the panel rect starts at y 304,
so the panel's top 16 px cover the card's bottom edge. In legacy the two
meet exactly (card 102 tall, panel 68 tall, content 170). The troop sprite
is lifted 4 px so its feet clear the panel in legacy; in modern the panel
covers 16 px of the card. Not yet fixed; the fix is to anchor the panel at
the card's bottom for location screens, or shorten the card's slot.

## Centred panels in the map pane

| circumstance | size | position |
|---|---|---|
| Game Menu (Escape) | width 320 or the widest entry plus 48; height rows x 22 plus 16 | centred in the map pane |
| combat spell picker | 504 x 234 | centred in the map pane |
| puzzle | five tiles square, 480 x 480 | centred in the map pane |
| combat victory dialog | 576 x 288 | centred on the whole screen |

## View panels (the content rect or the wide view, full pane height)

| view | rect | notes |
|---|---|---|
| Character | wide view, 128,46 576x480 | portrait left, stat table right with zeros shown, artifact belt below |
| Army | wide view | five rows of a tile each; empty slots stay panel-coloured |
| Gate picker | wide view | two columns of destinations |
| Contract | content rect, 128,46 480x480 | villain portrait, wrapped feature and crime blocks |
| Spells | content rect | two columns |
| World map | content rect | integer pixels per cell that fit, 3 for a 64x128 zone, centred |
| Win, Lose | wide view | 18-column text beside the ending picture |

## Top-anchored panels

| circumstance | size | position |
|---|---|---|
| Options (keybind reference) | width 28 columns, or two 24-column keybind columns when the list does not fit; height to the list, capped at the pane | x 128 (content left), y 46 (pane top) |
| Controls settings | 18 columns x (rows plus title plus Scale) | flush left at the pane's left edge, y 46 |
| Toast | text width plus 8, one line | top of the map pane, centred |

## Status band text

"Press 'ESC' to exit" while any view or dialog is open; otherwise the
Options / Controls / Days Left line; the fast-quit question overrides both.
