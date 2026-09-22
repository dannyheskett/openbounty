#ifndef OB_VIEWS_H
#define OB_VIEWS_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"
#include "view_kind.h"  // engine-shared ViewKind enum
#include "ui_host.h"    // prototype for views_active() (declared with the
                        // engine->host callback contract; shell code that
                        // queries view state needs this prototype too).

#define VIEWS_STACK_MAX 4

// Menu action callbacks. Each returns true on success (menu closes), false
// to keep the menu open (e.g. failed load).
typedef struct {
    bool (*on_save)(void *userdata);
    bool (*on_load)(void *userdata);
    bool (*on_new)(void *userdata);
    bool (*on_quit)(void *userdata);
} MenuCallbacks;

// The callbacks the menu uses when it opens, so the modern root page can be
// built with the rows that apply at that moment. Call once at startup.
void views_menu_bind(const MenuCallbacks *cbs, void *userdata);

// --debug: the modern game menu carries a Debug page of cheat rows. Off by
// default, and then no cheat is reachable at all.
void views_menu_set_debug(bool on);

// The cheat a Debug row asked for this frame, as a CheatAction, or -1. The menu
// closes itself; the caller applies the cheat (it needs the map, fog, sprites
// and render target the menu does not have). Reading clears it.
int  views_menu_take_cheat(void);

// How many views are stacked (0 = none).
int views_depth(void);

// views_active() is declared in engine/include/ui_host.h since engine
// code (state_serialize, flows) also calls it.
void     views_set(ViewKind v);   // Replace stack with [v] (or empty if VIEW_NONE).
void     views_dismiss(void);     // Pop top; if stack empty, do nothing.

// Sync the shell view stack from the engine's player-IO queue. When the
// front request is a REQ_VIEW, push (or replace, per view_replace) the matching
// screen onto the local stack and ack the request (transport done; the view now
// lives on the stack and plain views_dismiss/ESC tears it down as before). Called
// once per frame in the HUMAN-input path; autoplay never calls it (it acks
// REQ_VIEWs directly and so never pushes a view -- the overflow fix). Returns true
// if it surfaced a view this frame.
bool     shell_pump_player_io_view(Game *g);

// Push a new view on top of the stack. ESC returns to whatever was below.
// Caps at VIEWS_STACK_MAX; ignored on overflow, with a warning printed.
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
//
// Declaration moved to engine/include/ui_host.h (engine step.c calls
// it). The shell defines it in src/views.c.

// Town input: cursor movement and A-E action keys. Mutates `g` directly.
bool     views_town_update(Game *g);

// --- View-state accessors -------------------------------------------------
// Read-only access to menu/town internal state for -style
// renderer in src/.

// Returns the currently-active menu page's title ("Game Menu" / "Views" / ...)
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
int  views_spells_cursor(void);      // modern: 0..13, legacy: -1
bool views_spells_casting(void);     // the Spells view was opened to cast

// Town display name (from views_open_town), or NULL if not in VIEW_TOWN.
const char *views_town_display_name(void);
// Modern: a boat / spell / siege outcome is showing as a dialog (until Continue).
bool views_town_result_dialog(void);
// --gallery: put the town screen in a state to capture (row < 0: the main page
// with the cursor on lcursor; else that section's page), with an optional
// result message, shown as its dialog when `dialog`.
void views_gallery_town(const struct Game *g, int row, int lcursor, const char *info, bool dialog);
void views_gallery_town_scene(int cursor);
// Modern: the services in-lay is open over the scene (else the scene's rows).
bool views_town_visiting(void);
int  views_town_scene_cursor(void);

// Town record id (canonical id for g->towns[] lookup), or NULL if not
// in VIEW_TOWN.
const char *views_town_record_key(void);

// Town menu row text, formatted per the current game state. Returns false
// if `row` is out of range. `out` is written up to `out_sz`.
bool views_town_row_text(const struct Game *g, int row,
                         char *out, int out_sz);

// Number of town rows (always 5 in KB: A-E).
int  views_town_row_count(void);

// False for a row that is shown greyed out and cannot be chosen. Modern enables
// the boat row only at a town whose dock lies within TOWN_BOAT_RANGE tiles;
// legacy enables every row.
bool views_town_row_enabled(const struct Game *g, int row);

// Modern: the short menu label for a town row (the pack's town_menu_* strings).
bool views_town_menu_label(const struct Game *g, int row, char *out, int out_sz);

// Town info panel (the "You don't have enough gold!" popup that overlays
// the action list). Returns NULL if no info is active.
const char *views_town_info_text(void);

// Modern: the detail panel's current page; the renderer reports how many pages
// its text needs so Left/Right stay in range.
int  views_town_detail_page(void);
void views_town_set_detail_pages(int pages);

// ---- Modern town ------------------------------------------------------------
// The menu opens a detail screen per row; the left column then shows that
// screen's rows (the contracts on offer, or the screen's one action) and Back.
typedef enum {
    TOWN_LIST_MENU = 0,
    TOWN_LIST_CONTRACTS,
    TOWN_LIST_INFO,
    TOWN_LIST_BOAT,
    TOWN_LIST_TEMPLE,
    TOWN_LIST_SIEGE,
} TownList;
TownList views_town_list(void);
int  views_town_list_rows(const struct Game *g);
int  views_town_list_cursor(void);
// Row i of the current list: its label, whether it can be chosen, and (in
// Contracts) whether it is the contract held.
bool views_town_list_row(const struct Game *g, int i, char *out, int cap,
                         bool *enabled, bool *held);
int  views_town_contract_slot(const struct Game *g, int row);  // rotation slot; -1 = Back
// This town's dock is set and within reach; the spell this town sells.
bool views_town_boat_available(const struct Game *g);
const SpellDef *views_town_spell(const struct Game *g);

// Anything that changes the game asks Yes/No. The main loop takes the request
// once (writing the question into body), opens the prompt, and on Yes calls
// views_town_confirm_yes, which carries the action out.
typedef enum {
    TOWN_CONFIRM_NONE = 0,
    TOWN_CONFIRM_CONTRACT,
    TOWN_CONFIRM_BOAT_RENT,
    TOWN_CONFIRM_BOAT_CANCEL,
    TOWN_CONFIRM_SPELL,
    TOWN_CONFIRM_SIEGE,
} TownConfirm;
TownConfirm views_town_take_confirm(const struct Game *g, char *body, int cap);
void views_town_confirm_yes(Game *g);

// The priest's refusal before the town's zone rites are learned.
void views_town_rites_text(const struct Game *g, char *out, int cap);

// The Information row's report, formatted for this town.
void views_town_intel_text(const struct Game *g, char *out, int cap);

// Town action row the cursor is on (0..4).
int  views_town_cursor(void);

// Demo/attract driver (RENDER-ONLY): advance the town cursor one row toward
// `target_row` per call so the visible demo SHOWS the navigation a player makes.
// Returns true once the cursor reaches target_row, false while still moving. It
// only moves the shell cursor -- it NEVER executes a row, so it cannot mutate game
// state; the real transaction is a separate recorded engine primitive (e.g.
// RA_BUY_SIEGE). No Game parameter, by construction state-inert.
bool views_town_demo_step_cursor(int target_row);

// ---- Controls settings panel (VIEW_CONTROLS) -------------------------------
// Row cursor for navigation (0..res->controls.count-1).
int  views_controls_cursor(void);
void views_controls_set_cursor(int r);
// Called with a game-state handle each frame -- nudges value at `row` by
// +1 (wraps to 0 at range). Updates g->stats.options[row].
void views_controls_advance(struct Game *g, int row);
// True iff the row should render dimmed and ignore input -- currently
// only the audio rows when no playback device is available.
bool views_controls_row_disabled(const struct Game *g, int row);

// The shell-appended Scale row. Not pack data and not game state: it is a
// runtime display preference held in present.c.

// ---- Spell casting (VIEW_SPELLS) -------------------------------------------
// Enter interactive cast mode for the spell view.
void views_spells_set_mode(bool cast_mode);
// Returns the chosen spell index (0-13) or -1 if none selected.
// Consumes the selection (next call returns -1 until a new selection).
int  views_spells_chosen(void);
// Update spell selection input (Left/Right columns, A-G cast, Esc dismiss).
// Returns true if a selection was made.
bool views_spells_update(void);

// ---- Gate destination picker (VIEW_GATE) -----------------------------------
// Open the gate picker over the supplied destination list. `is_town` selects
// the title ("Town Gate" vs "Castle Gate"). Pushes VIEW_GATE. The list is
// copied internally. Pass the array produced by GameGateDestinations.
void views_gate_open(const GateDestination *dests, int count,
                     bool is_town);
// Read accessors for the renderer.
int  views_gate_count(void);
// Modern: the gate picker's columns of standard rows, and rows in each.
#define VIEWS_GATE_COLUMNS 3
int  views_gate_rows_per_column(void);
bool views_gate_is_town(void);
int  views_gate_cursor(void);
const GateDestination *views_gate_dest(int idx);
// Returns the chosen destination index, or -1 if none yet. Consumes it.
int  views_gate_chosen(void);
// Update gate selection input (arrows/letters to pick, Enter confirm, Esc
// cancel). Returns true if a destination was confirmed this frame.
bool views_gate_update(void);

// Town-menu rows. Fixed order: Contract, Boat, Info, Spell, Siege. Exposed so the
// visible-autoplay presenter (shell_autoplay.c) can map a recorded town action to
// the row a player would select -- the ONE definition of the town-menu layout.
typedef enum {
    TOWN_ROW_CONTRACT = 0,
    TOWN_ROW_BOAT,
    TOWN_ROW_INFO,
    TOWN_ROW_SPELL,
    TOWN_ROW_SIEGE,
    TOWN_ROW_COUNT,
    TOWN_ROW_LEAVE = TOWN_ROW_COUNT,   // modern: the main page's last row, Leave
} TownRow;

#endif
