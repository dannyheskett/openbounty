# Menus (modern mode)

Modern mode has been **menu driven**: every action has been a row reached
with the arrows and Enter, or a tap. Keys have remained as shortcuts, but
nothing in modern has been reachable **only** by a key (REQ-430k). Legacy has
kept King's Bounty's keys and has not been covered here; the debug cheats have
needed `--debug` in every mode.

## The game menu

Esc, or a tap on the status band at the top of the map, has opened it. It has
been built in `src/modern/gamemenu.c`.

| Page | Rows | Shortcut keys |
|---|---|---|
| Menu | Hero, World, Game, Close, and Exit as the foot row | -- |
| Hero | Army, Character, Contract, Puzzle, Dismiss (only with troops), Back | A, V, I, P, D |
| World | Map, Cast, Search, Fly or Land, End Week, Rest, Sail (only when sailing), Back | M, U, S, F / L, W, keypad 5, N |
| Game | Debug (only with `--debug`), Save, Load (only with a save), Controls, New Game, Back | C |

**Exit has been absent on iOS and Android** (REQ-529): a phone app has not
quit itself. Fast quit has been a Yes / No prompt opened by Ctrl+Q.

The Controls page has listed the pack's own settings and Back. There has been
no scale row: the scale has followed the surface (REQ-528).

## The map

| Action | Row | Shortcut keys |
|---|---|---|
| Movement | a tap on the map | arrows, keypad, gamepad |
| Fullscreen | the window's own control | Alt+Enter |

## Detail views

| View | Row | Shortcut keys |
|---|---|---|
| Army, Character, Contract, Puzzle | display only; any key or a tap has dismissed it | |
| World map (your map, or the whole map with the orb) | a row under the map | Space |
| Spells (adventure cast), gate picker | rows | letters |

## Location screens

| Screen | Row | Shortcut keys |
|---|---|---|
| Town | rows, no letters | A-E |
| Home castle | Recruit Soldiers and Audience rows | A, B |
| Own castle | a Garrison / Remove mode row, then the five slots | Space, A-E |
| Recruit soldiers, dwelling | troop rows, then the How many panel (`docs/UI-PANELS.md`) | A-E |
| Alcove | Yes / No rows | Y, N |

## Prompts and dialogs

| Kind | Row | Shortcut keys |
|---|---|---|
| Yes / No | Yes and No rows | Y, N |
| Numeric (continent, dismiss) | one row per choice | 1-5 |
| A / B choice (chest) | one row per choice | A, B |
| Message | any key or a tap has advanced or dismissed it | |

## Combat

Enter, Esc or a tap on the active unit has opened the combat menu
(`src/combat_loop.c`).

| Page | Rows | Shortcut keys |
|---|---|---|
| Menu | Unit, Hero, Game, Close | -- |
| Unit | **Shoot first for a troop with ranged ammo** (REQ-533), Wait, Fly, Cast, Back | S, Space, F, U |
| Hero | Army, Character, Back | A, V |
| Game | Controls, Back, and Give up as the foot row | C, G |

Movement and melee have been taps on the grid (arrows and keypad as
shortcuts); Shoot and Fly targets have been a cursor or a tap; the spell
picker has been rows. The victory or defeat dialog has taken any key or a
tap.

## Starting a game

| Screen | Row | Shortcut keys |
|---|---|---|
| Splash, credits, new-game intro | any key, a tap, or a timeout | |
| Title | New Game, Load Saved Game, Credits, and Exit (not on a phone) | -- |
| Class select | pick a figure -- it has taken the gold outline and its description has appeared -- then **Continue** or **Cancel** (REQ-532) | arrows; Enter on the chosen row |
| Name and difficulty | the name field, then the difficulty rows | typed; on touch, the on-screen keyboard (REQ-531); with a gamepad, the letter grid |
| Save picker, pack select | rows | digits (pack select) |
| Win / lose cartoon | any key or a tap | |

## Debug and developer keys

| Key | Behaviour |
|---|---|
| Debug cheats | a Debug page in the game menu, only with `--debug` |
| Screenshot (`) | key only |
| Stop autoplay (Esc) | `--autoplay` only |
| Movie export dialog | `--movie` only |
