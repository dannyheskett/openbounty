#ifndef OB_VIEWS_H
#define OB_VIEWS_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"

// Adventure-screen "view" overlays. Views are stacked (depth ≤ 4) so that
// sub-screens like VIEW_RECRUIT_SOLDIERS can push on top of VIEW_HOME_CASTLE
// and ESC pops back to the parent. The top of the stack is the "active" view.
//
// Views split into three categories:
//   - Modal overlays (CHARACTER, ARMY, SPELLS, CONTRACT, PUZZLE, WORLDMAP,
//     OPTIONS, CONTROLS, MENU): full-screen panels over the adventure map.
//   - Location-backdrop screens (TOWN, HOME_CASTLE, OWN_CASTLE, DWELLING,
//     ALCOVE, RECRUIT_SOLDIERS): replace the map area with a backdrop PNG;
//     status bar swaps to "Press 'ESC' to exit".
//   - End-game (WIN, LOSE): full-screen ending art.
typedef enum {
    VIEW_NONE = 0,
    // Modal overlays
    VIEW_MENU,        // Esc / O - unified Game Menu (view switch + save/load/quit)
    VIEW_CHARACTER,   // V - portrait + stats + inventory belt
    VIEW_ARMY,        // A - 5 troop rows with counts & stats
    VIEW_SPELLS,      // U - spellbook, combat vs adventuring columns
    VIEW_CONTRACT,    // I - wanted villain's portrait + bounty
    VIEW_PUZZLE,      // P - 5x5 puzzle grid (villain/artifact covers)
    VIEW_WORLDMAP,    // M / TAB - full-continent minimap modal
    VIEW_OPTIONS,     // O - keybind reference screen
    VIEW_CONTROLS,    // C - Controls settings panel (delay/sounds/etc.)
    // Location-backdrop screens (status bar shows "Press 'ESC' to exit")
    VIEW_TOWN,             // in-town menu (contract / boat / info / spell / siege)
    VIEW_HOME_CASTLE,      // Castle of King Maximus: Recruit / Audience
    VIEW_OWN_CASTLE,       // Garrison / Remove troops in a player castle
    VIEW_DWELLING,         // Recruit at outdoor dwelling (Plains/Forest/Hill/Dungeon)
    VIEW_ALCOVE,           // Archmage Aurange offer
    VIEW_RECRUIT_SOLDIERS, // Sub-screen of HOME_CASTLE
    // End game
    VIEW_WIN,
    VIEW_LOSE,
} ViewKind;

#define VIEWS_STACK_MAX 4

// Menu action callbacks. Each returns true on success (menu closes), false
// to keep the menu open (e.g. failed load).
typedef struct {
    bool (*on_save)(void *userdata);
    bool (*on_load)(void *userdata);
    bool (*on_new)(void *userdata);
    bool (*on_quit)(void *userdata);
} MenuCallbacks;

ViewKind views_active(void);
void     views_set(ViewKind v);   // Replace stack with [v] (or empty if VIEW_NONE).
void     views_dismiss(void);     // Pop top; if stack empty, do nothing.

// Push a new view on top of the stack. ESC returns to whatever was below.
// Caps at VIEWS_STACK_MAX; ignored on overflow with a stderr warning.
void     views_push(ViewKind v);

// True when the current view should display "Press 'ESC' to exit" in the
// status bar instead of "Days Left:N". Covers location-backdrop screens
// and full-screen overlays. Used by chrome to swap the status text.
bool     views_wants_exit_hint(void);

// Contract view shows "Press 'ESC' to exit" only when a contract is
// active; the no-contract case keeps the normal status bar.
void     views_contract_set_active(bool active);

// Menu input: arrow keys / WS move cursor, Enter/Space activates, Esc dismisses.
// Returns true if it consumed input this frame (caller should skip other input).
// Only meaningful when views_active() == VIEW_MENU.
bool     views_menu_update(const MenuCallbacks *cbs, void *userdata);

// Open the town view.
//   display_name: human-readable ("Riverton"), resolved from the resource pack.
//   record_key:   map-local id ("town_000"), key into Game.towns[].
//   boat_x/y:     where a rented boat should spawn.
void     views_open_town(const char *display_name, const char *record_key,
                         int boat_x, int boat_y);

// Town input: cursor movement and A-E action keys. Mutates `g` directly.
bool     views_town_update(Game *g);

// --- View-state accessors -------------------------------------------------
// Read-only access to menu/town internal state for -style
// renderer in src/.

// Returns the currently-active menu page's title ("Game Menu" / "Views" / …)
// or NULL if not in VIEW_MENU.
const char *views_menu_title(void);

// Returns the number of entries in the active menu page (0 if not in menu).
int  views_menu_entry_count(void);

// Returns the i-th entry's label for the active menu page, or NULL if out
// of range / not in menu.
const char *views_menu_entry_label(int i);

// Returns true if the i-th entry is a submenu (renders a trailing ">" arrow
// in menus).
bool views_menu_entry_is_submenu(int i);

// Returns the cursor position in the active menu (0..count-1), or -1 if
// not in menu.
int  views_menu_cursor(void);

// Town display name (from views_open_town), or NULL if not in VIEW_TOWN.
const char *views_town_display_name(void);

// Town menu row text, formatted per the current game state. Returns false
// if `row` is out of range. `out` is written up to `out_sz`.
bool views_town_row_text(const struct Game *g, int row,
                         char *out, int out_sz);

// Number of town rows (always 5 in KB: A-E).
int  views_town_row_count(void);

// Town info panel (the "You don't have enough gold!" popup that overlays
// the action list). Returns NULL if no info is active.
const char *views_town_info_text(void);

// Town action row the cursor is on (0..4).
int  views_town_cursor(void);

// ---- Controls settings panel (VIEW_CONTROLS) -------------------------------
// Row cursor for navigation (0..res->controls.count-1).
int  views_controls_cursor(void);
void views_controls_set_cursor(int r);
// Called with a game-state handle each frame — nudges value at `row` by
// +1 (wraps to 0 at range). Updates g->stats.options[row].
void views_controls_advance(struct Game *g, int row);
// True iff the row should render dimmed and ignore input — currently
// only the audio rows when no playback device is available.
bool views_controls_row_disabled(const struct Game *g, int row);

// ---- Spell casting (VIEW_SPELLS) -------------------------------------------
// Enter interactive cast mode for the spell view.
void views_spells_set_mode(bool cast_mode);
// Returns the chosen spell index (0-13) or -1 if none selected.
// Consumes the selection (next call returns -1 until a new selection).
int  views_spells_chosen(void);
// Update spell selection input (Left/Right columns, A-G cast, Esc dismiss).
// Returns true if a selection was made.
bool views_spells_update(void);

#endif
