# Menu audit (modern mode)

Goal: modern is **menu driven** -- every action is a row you reach with the
arrows and Enter, or a tap. Keys stay for legacy, and as shortcuts in modern,
but nothing in modern may be reachable **only** by a key. Legacy is out of
scope except the debug menu, which becomes unreachable without `--debug` in
every mode.

Audited 2026-09-12 from the source: every `input_key_pressed`,
`IsKeyPressed`, `GetKeyPressed` and touch registration in `src/` and
`src/screens/`, excluding the frozen `src/legacy/` copies.

**Legend.** **Menu** = rows with arrow selection and taps. **Tap-only** =
reachable by a tap or a key but has no rows (touch chrome is shown only once a
touch has been seen, so a desktop player gets keys only). **Key-only** = no row,
no tap. **Letters** = rows exist but are labelled with key letters (`A)`,
`G  `) that are to be removed.

## Adventure map

| action | today | source |
|---|---|---|
| View Army, Contract, Auto-mapping, Puzzle, Search, Use Magic, View Character, Wait End Week, Fly / Land, Controls, Save, Load, New Game, Exit | **Menu** (game menu rows), hotkey letter shown beside each | `src/views.c` `modern_menu_build` |
| **Dismiss Army** (D) | **Key-only** -- not in the pack's `keybinds`, so no menu row | `src/input.c:141`, `src/shell_actions.c` |
| **New Continent / sail** (N) | **Key-only** -- not in `keybinds` | `src/input.c:145` |
| **Rest / pass a turn** (keypad 5) | **Key-only** | `src/input.c:146` |
| **Save and Quit** (Q) | Key-only, but Save and Exit rows cover it | `src/input.c:148` |
| **Fast quit** (Ctrl+Q) | **Key-only** y/n drawn in the status band, not a prompt | `src/shell_fastquit.c` |
| Movement | keys, gamepad, tap on the map | `src/input.c`, `src/main.c` `touch_region_map` |
| Fullscreen (Alt+Enter) | Key-only (window control) | `src/main.c` |

## Game menu and Controls

| screen | today | notes |
|---|---|---|
| Game menu | **Menu** | hotkey letters shown right-aligned -- **to remove** |
| Controls | **Menu** | digit keys 1-9 also act on rows; C closes |

## Detail views

| view | today | notes |
|---|---|---|
| Army, Character, Contract, Puzzle | display only; any key or a tap dismisses | the **Character view has no options** today -- see open question |
| **World map: whole-map reveal** (Space, with the orb) | **Key-only** | `src/main.c` `VIEW_WORLDMAP && KEY_SPACE` |
| Spells (adventure cast) | **Menu** | letters A-N also cast directly |
| Gate picker | **Menu** | letters also pick |

## Location screens

| screen | today | notes |
|---|---|---|
| Town | **Menu**, **Letters** (`A)`-`E)`) | `src/modern/overlay.c` town rows |
| **Home castle** | **Key-only**: A Recruit Soldiers, B Audience | `src/main.c` `VIEW_HOME_CASTLE`; drawn as text, no rows, no taps |
| Own castle | **Menu** for the five slots, **Letters** | **Garrison / Remove toggle (Space) is key-only** |
| Recruit soldiers | **Menu** for troops, **Letters** (`A)`-`E)`); count by digits, selector or digit chrome | `src/screens/recruit_soldiers.c` |
| Dwelling | count prompt -- selector or digit chrome | |
| Alcove | yes/no prompt -- **Menu** | |

## Prompts and dialogs

| kind | today | notes |
|---|---|---|
| Yes / No | **Menu** (Yes and No rows) | |
| **Numeric 1-5** (e.g. dismiss which troop) | **Tap-only** -- keys 1-5, touch answer bar | no rows |
| **A / B choice** (e.g. chest: gold or leadership) | **Tap-only** -- keys A/B, touch answer bar | no rows |
| Count entry | keyboard, or the letter selector | |
| Message dialog | any key or a tap advances / dismisses | no explicit OK row |

## Combat

| action | today | notes |
|---|---|---|
| Move, attack by moving | keys, gamepad, tap on the grid | |
| **Wait, Pass, Shoot, Fly, Cast, Give up** | **Tap-only** on a touch device (chrome bar labelled Wait / Pass / Shot / Fly / Cast / Give), **key-only** on desktop | `src/combat_loop.c:271-341`, `src/touch.c` |
| **Controls, Options, Army, Character** during combat | **Key-only** (C, O, A, V); Options is the panel retired everywhere else in modern | `src/combat_loop.c:572-626` |
| Shoot / Fly target | cursor keys or a tap; Enter / Space / A / C confirm | |
| Spell picker | **Menu**, letters also pick | |
| Victory dialog | any key or a tap | |

## Startup and endings

| screen | today | notes |
|---|---|---|
| Splash, credits, new-game intro | any key, a tap, or a timeout | |
| Class select | **Menu** (arrows + tap), **letters** A-D also pick | status line reads "Select Char A-D or L-Load saved game" |
| **Load a saved game** (L) | **Key-only** from class select | `src/startup.c:347` |
| Save picker | **Menu** | |
| Name entry | keyboard or letter selector | |
| Difficulty | **Menu** | |
| **Win / lose cartoon** | **Key-only** -- raw `GetKeyPressed`, no tap | `src/end_cartoon.c:25` |
| Pack select (multi-pack launcher) | **Menu** | digits also pick |

## Debug and developer keys

| key | today | notes |
|---|---|---|
| **F10 debug menu** | **Key-only**, a message-dialog text body with letter cheats, **on in every build** | `src/shell_cheats.c` -- to become a Debug row in the game menu, only with `--debug` |
| Screenshot (`) | Key-only, always on | `src/screenshot.c` |
| Stop autoplay (Esc) | Key-only | `--autoplay` only |
| Movie export dialog | Key-only | `--movie` only |

## Summary: what is not menu driven in modern

1. **Key-only actions with no row:** Dismiss Army, New Continent, Rest, Fast quit, world-map reveal, Load from class select, win/lose cartoon.
2. **A whole screen with no rows:** home castle (Recruit / Audience).
3. **Toggles with no row:** own castle Garrison / Remove.
4. **Prompts without rows:** numeric 1-5 and A/B choice.
5. **Combat actions and combat-time screens:** no menu at all on desktop.
6. **Debug menu:** letter cheats, always on.
7. **Letter labels to strip:** town, own castle, recruit rows; game menu hotkey column; class select's A-D and the "A-D / L" status hint.
