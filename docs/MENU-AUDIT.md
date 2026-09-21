# Menu audit (modern mode)

Goal: modern is **menu driven** -- every action is a row you reach with the
arrows and Enter, or a tap. Keys stay for legacy, and as shortcuts in modern,
but nothing in modern is reachable **only** by a key (REQ-430k). Legacy is out
of scope except the debug cheats, which are unreachable without `--debug` in
every mode.

Audited 2026-09-12 from the source, then converted the same day. Each table
lists how the action is reached now and the keys that remain as shortcuts.

## Adventure map

| action | row | shortcut keys |
|---|---|---|
| Army, Contract, Auto-mapping, Puzzle, Character | game menu > Screens | A, I, M, P, V |
| Search, Use Magic, Fly / Land, Wait End Week, Dismiss Army, New Continent, Rest | game menu > Actions (only the rows that apply) | S, U, F / L, W, D, N, keypad 5 |
| Controls, Save, Load, New Game, Exit | game menu | C; Q saves |
| Fast quit | yes/no prompt with Yes / No rows | Ctrl+Q opens it; Y / N |
| Movement | taps on the map | arrows, keypad, gamepad |
| Fullscreen | none (window control) | Alt+Enter |

## Detail views

| view | row | shortcut keys |
|---|---|---|
| Army, Character, Contract, Puzzle | display only; any key or a tap dismisses | |
| World map: your map / whole map (with the orb) | a row under the map | Space |
| Spells (adventure cast), gate picker | rows | letters |

## Location screens

| screen | row | shortcut keys |
|---|---|---|
| Town | rows, no letters | A-E |
| Home castle | Recruit Soldiers and Audience rows | A, B |
| Own castle | a Garrison / Remove mode row, then the five slots, no letters | Space, A-E |
| Recruit soldiers | troop rows, no letters; count by digits or the selector | A-E |
| Dwelling | count prompt (digits or the selector) | |
| Alcove | Yes / No rows | Y, N |

## Prompts and dialogs

| kind | row | shortcut keys |
|---|---|---|
| Yes / No | Yes and No rows | Y, N |
| Numeric (continent, dismiss) | one row per choice: the body's own lines, or the troops | 1-5 |
| A / B choice (chest) | one row per choice, from the body's A) / B) lines | A, B |
| Count entry | typed, or the letter selector | digits |
| Message dialog | any key or a tap advances / dismisses | |

## Combat

| action | row | shortcut keys |
|---|---|---|
| Move, attack by moving | taps on the grid | arrows, keypad |
| Wait, Shoot, Fly, Cast a spell, Army, Character, Controls, Give up | Actions menu: Enter or a tap on the active unit (only the rows that apply) | Space / W, S, F, U, A, V, C, G |
| Shoot / Fly target | cursor or a tap | Enter / Space confirm |
| Spell picker | rows | letters |
| Victory dialog | any key or a tap | |

Options (O) is not used in modern combat. Keypad 5 "Pass" on the old touch
bar did nothing in combat and has no row.

## Startup and endings

| screen | row | shortcut keys |
|---|---|---|
| Splash, credits, new-game intro | any key, a tap, or a timeout | |
| Class select | the figures (the others dimmed, the selected framed and named) and a Load saved game row | A-D, L |
| Save picker, difficulty, pack select | rows | digits (pack select) |
| Name entry | typed, or the letter selector | |
| Win / lose cartoon | any key or a tap | |

## Debug and developer keys

| key | now |
|---|---|
| Debug cheats | a Debug page at the end of the modern game menu, only with `--debug`; F10 is gone in every mode |
| Screenshot (`) | key only, always on |
| Stop autoplay (Esc) | `--autoplay` only |
| Movie export dialog | `--movie` only |
