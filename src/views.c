#include "input_host.h"
#include "views.h"
#include "touch.h"
#include "select.h"
#include "present.h"
#include "layout.h"
#include "player_io.h"   // engine views arrive via the player-IO queue
#include "raylib.h"
#include "audio.h"
#include "tables.h"
#include "recorder.h"
#include "modern/gamemenu.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// ---- Active view stack ----------------------------------------------------
// Views are stacked so sub-screens (e.g. VIEW_RECRUIT_SOLDIERS over
// VIEW_HOME_CASTLE) can ESC back to their parent. Top of stack = active
// view; empty stack = VIEW_NONE (overworld).
static ViewKind view_stack[VIEWS_STACK_MAX];
static int      view_stack_depth = 0;

static ViewKind view_stack_top(void) {
    return (view_stack_depth > 0) ? view_stack[view_stack_depth - 1] : VIEW_NONE;
}

// ---- Spell casting state ---------------------------------------------------
static struct {
    bool active;       // in cast mode (not just viewing)
    int  column;       // 0=combat, 1=adventure
    int  chosen;       // spell index after A-G press, -1=none
    int  cursor;       // modern: the selected row, 0..13 (column * 7 + slot)
} spell_state = { 0 };

int views_spells_cursor(void) { return CL_IS_MODERN ? spell_state.cursor : -1; }
bool views_spells_casting(void) { return spell_state.active; }

void views_spells_set_mode(bool cast_mode) {
    spell_state.active = cast_mode;
    spell_state.column = 1;  // default to adventure column for overworld
    spell_state.chosen = -1;
    spell_state.cursor = 7;
}

int views_spells_chosen(void) {
    int result = spell_state.chosen;
    spell_state.chosen = -1;
    return result;
}

bool views_spells_update(void) {
    if (!spell_state.active) return false;
    touch_request(TOUCH_CHROME_BACK);
    // Touch: a tapped cell carries its column, so one tap casts.
    int tapped = touch_tapped_row(TOUCH_LIST_SPELLS);
    if (tapped >= 0) {
        spell_state.column = tapped / 7;
        spell_state.chosen = tapped;
        views_dismiss();
        return true;
    }
    if (input_key_pressed(KEY_LEFT))  { spell_state.column = 0; spell_state.cursor %= 7; }
    if (input_key_pressed(KEY_RIGHT)) { spell_state.column = 1; spell_state.cursor = 7 + spell_state.cursor % 7; }
    if (input_key_pressed(KEY_ESCAPE)) {
        spell_state.active = false;
        views_dismiss();
        return false;
    }
    // Modern: up/down move within the column, Enter casts the cursor row;
    // the letters still cast directly in either mode.
    {
        SelList l = { 7, spell_state.cursor % 7 };
        int row = -1;
        SelEvent ev = sel_input(&l, 0, 0, &row);
        spell_state.cursor = spell_state.column * 7 + l.cursor;
        if (ev == SEL_CONFIRM) {
            spell_state.chosen = spell_state.cursor;
            views_dismiss();
            return true;
        }
    }
    for (int i = 0; i < 7; i++) {
        if (input_key_pressed(KEY_A + i)) {
            spell_state.chosen = spell_state.column * 7 + i;
            views_dismiss();
            return true;
        }
    }
    return false;
}

// ---- Gate destination picker (VIEW_GATE) -----------------------------------
// A cursored, two-column panel (see views_render.c draw_gate). The list is
// snapshotted on open so the renderer/input never touch game state. Navigation:
// Up/Down moves within a column, Left/Right jumps columns, Enter confirms the
// cursor, a letter jumps straight to that row, Esc cancels.
#define GATE_VIEW_MAX 26
static struct {
    GateDestination list[GATE_VIEW_MAX];
    int  count;
    bool is_town;
    int  cursor;       // 0..count-1
    int  chosen;       // confirmed index, -1 = none
} gate_view = { 0 };

void views_gate_open(const GateDestination *dests, int count, bool is_town) {
    if (count > GATE_VIEW_MAX) count = GATE_VIEW_MAX;
    if (count < 0) count = 0;
    for (int i = 0; i < count; i++) gate_view.list[i] = dests[i];
    gate_view.count = count;
    gate_view.is_town = is_town;
    gate_view.cursor = 0;
    gate_view.chosen = -1;
    views_push(VIEW_GATE);
}

int  views_gate_count(void)   { return gate_view.count; }
int  views_gate_rows_per_column(void) {
    int n = (gate_view.count + VIEWS_GATE_COLUMNS - 1) / VIEWS_GATE_COLUMNS;
    return n < 1 ? 1 : n;
}
bool views_gate_is_town(void) { return gate_view.is_town; }
int  views_gate_cursor(void)  { return gate_view.cursor; }

const GateDestination *views_gate_dest(int idx) {
    if (idx < 0 || idx >= gate_view.count) return NULL;
    return &gate_view.list[idx];
}

int views_gate_chosen(void) {
    int r = gate_view.chosen;
    gate_view.chosen = -1;
    return r;
}

bool views_gate_update(void) {
    int n = gate_view.count;
    if (n <= 0) return false;
    int left = (n + 1) / 2;   // rows in the left column (matches the renderer)
    // Modern: three columns of standard rows, each this many rows long.
    if (CL_IS_MODERN) left = views_gate_rows_per_column();

    touch_request(TOUCH_CHROME_BACK);
    int tapped = touch_tapped_row(TOUCH_LIST_GATE);
    if (tapped >= 0 && tapped < n) {
        gate_view.chosen = tapped;
        views_dismiss();
        return true;
    }
    if (input_key_pressed(KEY_ESCAPE)) {
        views_dismiss();
        return false;
    }
    // Letter shortcut: each town/castle is keyed by the FIRST LETTER of its
    // name (Elan's Landing -> 'E'), not its row position -- the original A-Z
    // gate grid, where the 26 destinations each own a distinct letter
    // (OPENKB-SPEC 11.5). Press that letter to jump straight to it and confirm.
    for (int i = 0; i < n; i++) {
        int letter = toupper((unsigned char)gate_view.list[i].name[0]) - 'A';
        if (letter < 0 || letter > 25) continue;
        if (input_key_pressed(KEY_A + letter)) {
            gate_view.chosen = i;
            views_dismiss();
            return true;
        }
    }
    if (input_key_pressed(KEY_DOWN)) {
        gate_view.cursor++;
        if (gate_view.cursor >= n) gate_view.cursor = 0;
    }
    if (input_key_pressed(KEY_UP)) {
        gate_view.cursor--;
        if (gate_view.cursor < 0) gate_view.cursor = n - 1;
    }
    // Left/Right jump between the two columns, preserving the row offset.
    if (input_key_pressed(KEY_RIGHT) && (CL_IS_MODERN || gate_view.cursor < left)) {
        int target = gate_view.cursor + left;
        if (target < n) gate_view.cursor = target;
    }
    if (input_key_pressed(KEY_LEFT) && gate_view.cursor >= left) {
        gate_view.cursor -= left;
    }
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_SPACE)) {
        gate_view.chosen = gate_view.cursor;
        views_dismiss();
        return true;
    }
    return false;
}

// =============================================================================
//  Game Menu -- unified, nested
// =============================================================================
typedef enum {
    MENU_KIND_SUBMENU,    // push MenuEntry.page
    MENU_KIND_VIEW,       // switch to MenuEntry.view
    MENU_KIND_ACTION,     // call MenuEntry.action on callbacks
    MENU_KIND_BACK,       // pop to previous page (or close at root)
} MenuKind;

typedef enum {
    MENU_ACT_NONE = 0,
    MENU_ACT_SAVE,
    MENU_ACT_LOAD,
    MENU_ACT_NEW,
    MENU_ACT_QUIT,
} MenuAction;

struct MenuPage;
typedef struct {
    const char            *label;
    MenuKind               kind;
    const struct MenuPage *page;   // MENU_KIND_SUBMENU
    ViewKind               view;   // MENU_KIND_VIEW
    MenuAction             action; // MENU_KIND_ACTION
    int                    key;    // unused
} MenuEntry;

typedef struct MenuPage {
    const char      *title;
    const MenuEntry *entries;
    int              count;
} MenuPage;

// ----- Page definitions -----------------------------------------------------
// Menu labels (and page titles) are sourced from res.ui.menu_*. Each entry's
// label is bound at first use via menus_bind_labels(), driven by the loaded
// Resources singleton. Static struct fields are initialized to safe defaults
// so a missing Resources still yields a usable menu.
static MenuEntry VIEWS_ENTRIES[] = {
    { "Army",      MENU_KIND_VIEW,   NULL, VIEW_ARMY,      MENU_ACT_NONE, 0 },
    { "Spells",    MENU_KIND_VIEW,   NULL, VIEW_SPELLS,    MENU_ACT_NONE, 0 },
    { "Character", MENU_KIND_VIEW,   NULL, VIEW_CHARACTER, MENU_ACT_NONE, 0 },
    { "Contract",  MENU_KIND_VIEW,   NULL, VIEW_CONTRACT,  MENU_ACT_NONE, 0 },
    { "Puzzle",    MENU_KIND_VIEW,   NULL, VIEW_PUZZLE,    MENU_ACT_NONE, 0 },
    { "View Map",  MENU_KIND_VIEW,   NULL, VIEW_WORLDMAP,  MENU_ACT_NONE, 0 },
    { "Back",      MENU_KIND_BACK,   NULL, VIEW_NONE,      MENU_ACT_NONE, 0 },
};
static MenuPage VIEWS_PAGE = {
    "Views", VIEWS_ENTRIES, (int)(sizeof(VIEWS_ENTRIES) / sizeof(VIEWS_ENTRIES[0]))
};

static MenuEntry SYSTEM_ENTRIES[] = {
    { "Save",      MENU_KIND_ACTION, NULL, VIEW_NONE, MENU_ACT_SAVE, 0 },
    { "Load",      MENU_KIND_ACTION, NULL, VIEW_NONE, MENU_ACT_LOAD, 0 },
    { "New Game",  MENU_KIND_ACTION, NULL, VIEW_NONE, MENU_ACT_NEW, 0 },
    { "Back",      MENU_KIND_BACK,   NULL, VIEW_NONE, MENU_ACT_NONE, 0 },
};
static MenuPage SYSTEM_PAGE = {
    "Options", SYSTEM_ENTRIES, (int)(sizeof(SYSTEM_ENTRIES) / sizeof(SYSTEM_ENTRIES[0]))
};

static MenuEntry ROOT_ENTRIES[] = {
    { "Views",   MENU_KIND_SUBMENU, &VIEWS_PAGE,  VIEW_NONE, MENU_ACT_NONE, 0 },
    { "Options", MENU_KIND_SUBMENU, &SYSTEM_PAGE, VIEW_NONE, MENU_ACT_NONE, 0 },
    { "Back",    MENU_KIND_BACK,    NULL,         VIEW_NONE, MENU_ACT_NONE, 0 },
    { "Exit",    MENU_KIND_ACTION,  NULL,         VIEW_NONE, MENU_ACT_QUIT, 0 },
};
static MenuPage ROOT_PAGE = {
    "Game Menu", ROOT_ENTRIES, (int)(sizeof(ROOT_ENTRIES) / sizeof(ROOT_ENTRIES[0]))
};

static bool s_menus_bound = false;
static void menus_bind_labels(void) {
    if (s_menus_bound) return;
    const Resources *res = resources_current();
    if (!res) return;
    const ResUI *ui = &res->ui;
    VIEWS_PAGE.title  = ui->menu_views_title;
    SYSTEM_PAGE.title = ui->menu_options_title;
    ROOT_PAGE.title   = ui->menu_root_title;

    VIEWS_ENTRIES[0].label = ui->menu_army;
    VIEWS_ENTRIES[1].label = ui->menu_spells;
    VIEWS_ENTRIES[2].label = ui->menu_character;
    VIEWS_ENTRIES[3].label = ui->menu_contract;
    VIEWS_ENTRIES[4].label = ui->menu_puzzle;
    VIEWS_ENTRIES[5].label = ui->menu_view_map;
    VIEWS_ENTRIES[6].label = ui->menu_back;

    SYSTEM_ENTRIES[0].label = ui->menu_save;
    SYSTEM_ENTRIES[1].label = ui->menu_load;
    SYSTEM_ENTRIES[2].label = ui->menu_new_game;
    SYSTEM_ENTRIES[3].label = ui->menu_back;

    ROOT_ENTRIES[0].label = ui->menu_views;
    ROOT_ENTRIES[1].label = ui->menu_options;
    ROOT_ENTRIES[2].label = ui->menu_back;
    ROOT_ENTRIES[3].label = ui->menu_exit;

    s_menus_bound = true;
}

// ----- Navigation stack -----------------------------------------------------
#define MENU_STACK_MAX 4

typedef struct {
    const MenuPage *page;
    int             cursor;
} MenuFrame;

static MenuFrame menu_stack[MENU_STACK_MAX];
static int       menu_depth = 0;   // 0 = closed; 1 = root; >1 = nested

// ----- Modern: one menu --------------------------------------------------------
// Modern's game menu is its own full screen (src/modern/gamemenu.c, REQ-430s);
// the pages below are legacy's.
static bool s_debug = false;
void views_menu_bind(const MenuCallbacks *cbs, void *userdata) { (void)cbs; (void)userdata; }
void views_menu_set_debug(bool on) { s_debug = on; }
int  views_menu_take_cheat(void) { return CL_IS_MODERN ? modern_gamemenu_take_cheat() : -1; }

static void menu_open_root(void) {
    menus_bind_labels();
    menu_depth = 1;
    if (CL_IS_MODERN) modern_gamemenu_open(s_debug);
    menu_stack[0] = (MenuFrame){ &ROOT_PAGE, 0 };
}

static void menu_push(const MenuPage *p) {
    if (menu_depth >= MENU_STACK_MAX) return;
    menu_stack[menu_depth++] = (MenuFrame){ p, 0 };
}

static void menu_pop_or_close(void) {
    if (menu_depth > 1) {
        menu_depth--;
    } else {
        menu_depth = 0;
        views_dismiss();
    }
}

static const MenuFrame *menu_top(void) {
    return (menu_depth > 0) ? &menu_stack[menu_depth - 1] : NULL;
}

ViewKind views_active(void)    { return view_stack_top(); }
int      views_depth(void)     { return view_stack_depth; }

void     views_set(ViewKind v) {
    // Replace the stack with a single entry (or clear if VIEW_NONE).
    view_stack_depth = 0;
    if (v != VIEW_NONE) {
        view_stack[view_stack_depth++] = v;
    }
    if (v == VIEW_MENU) menu_open_root();
    {
        char tag[32];
        snprintf(tag, sizeof tag, "view:set:%d", (int)v);
        recorder_capture(tag);
    }
}

void     views_push(ViewKind v) {
    if (v == VIEW_NONE) return;
    if (view_stack_depth >= VIEWS_STACK_MAX) {
        fprintf(stdout, "views_push: stack overflow (depth=%d, max=%d)\n",
                view_stack_depth, VIEWS_STACK_MAX);
        return;
    }
    view_stack[view_stack_depth++] = v;
    if (v == VIEW_MENU) menu_open_root();
    {
        char tag[32];
        snprintf(tag, sizeof tag, "view:push:%d", (int)v);
        recorder_capture(tag);
    }
}

void     views_dismiss(void)   {
    // Pop the top of the stack. If empty after pop, fully reset menu state.
    bool had = (view_stack_depth > 0);
    if (view_stack_depth > 0) view_stack_depth--;
    if (view_stack_depth == 0) {
        menu_depth = 0;
    }
    if (had) recorder_capture("view:pop");
}

bool shell_pump_player_io_view(Game *g) {
    if (!g) return false;
    const PlayerRequest *r = player_io_front(g);
    if (!r || r->role != REQ_VIEW) return false;
    ViewKind v = r->view;
    bool replace = r->view_replace;
    // Idempotent: if this engine view is already active, the request is stale
    // (already surfaced) -- just ack it. Otherwise put it on the stack. replace
    // (VIEW_TOWN) resets the stack to a single entry (old views_set semantics);
    // others push atop it (old views_push semantics).
    if (views_active() != v) {
        if (replace) views_set(v);
        else         views_push(v);
    }
    player_io_ack(g);   // transport complete; the view now lives on the stack
    return true;
}

static bool s_contract_has_active = false;
void views_contract_set_active(bool active) { s_contract_has_active = active; }

bool views_wants_exit_hint(void) {
    switch (view_stack_top()) {
        case VIEW_CHARACTER:
        case VIEW_ARMY:
        case VIEW_SPELLS:
        case VIEW_GATE:
        case VIEW_PUZZLE:
        case VIEW_WORLDMAP:
        case VIEW_OPTIONS:
        case VIEW_CONTROLS:
        case VIEW_TOWN:
        case VIEW_HOME_CASTLE:
        case VIEW_OWN_CASTLE:
        case VIEW_DWELLING:
        case VIEW_ALCOVE:
        case VIEW_RECRUIT_SOLDIERS:
        case VIEW_WIN:
        case VIEW_LOSE:
            return true;
        case VIEW_CONTRACT:
            return s_contract_has_active;
        case VIEW_NONE:
        case VIEW_MENU:
            return false;
    }
    return false;
}

static bool menu_invoke_action(MenuAction a, const MenuCallbacks *cbs,
                               void *userdata) {
    if (!cbs) return false;
    bool (*fn)(void *) = NULL;
    switch (a) {
        case MENU_ACT_SAVE: fn = cbs->on_save; break;
        case MENU_ACT_LOAD: fn = cbs->on_load; break;
        case MENU_ACT_NEW:  fn = cbs->on_new;  break;
        case MENU_ACT_QUIT: fn = cbs->on_quit; break;
        default: break;
    }
    return fn ? fn(userdata) : false;
}

bool views_menu_update(const MenuCallbacks *cbs, void *userdata) {
    if (view_stack_top() != VIEW_MENU) return false;
    if (menu_depth == 0) menu_open_root();

    MenuFrame *f = &menu_stack[menu_depth - 1];
    int n = f->page->count;

    touch_request(TOUCH_CHROME_BACK);
    // Touch: a tapped row selects and confirms in one go.
    int tapped = touch_tapped_row(TOUCH_LIST_MENU);
    if (tapped >= 0 && tapped < n) f->cursor = tapped;

    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_W) || input_key_pressed(KEY_KP_8)) {
        f->cursor = (f->cursor - 1 + n) % n;
        return true;
    }
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_S) || input_key_pressed(KEY_KP_2)) {
        f->cursor = (f->cursor + 1) % n;
        return true;
    }
    if (tapped >= 0 ||
        input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
        input_key_pressed(KEY_SPACE)) {
        const MenuEntry *e = &f->page->entries[f->cursor];
        switch (e->kind) {
            case MENU_KIND_SUBMENU:
                if (e->page) menu_push(e->page);
                break;
            case MENU_KIND_VIEW:
                views_set(e->view);
                break;
            case MENU_KIND_ACTION:
                if (menu_invoke_action(e->action, cbs, userdata)) {
                    views_dismiss();
                }
                break;
            case MENU_KIND_BACK:
                menu_pop_or_close();
                break;
        }
        return true;
    }
    if (input_key_pressed(KEY_ESCAPE)) {
        menu_pop_or_close();
        return true;
    }
    return false;
}

// =============================================================================
//  Town -- visit menu (contract / boat / info / spell / siege)
// =============================================================================
// Context for the currently-visited town. Set by views_open_town(), cleared
// when the view is dismissed. Only meaningful when view_stack_top() == VIEW_TOWN.
typedef struct {
    char display_name[48];   // resolved by the caller from the resource pack
    char record_key[24];     // key into g->towns[] (map-local id like "town_000")
    int  boat_x, boat_y;
    int  cursor;             // 0..4 = action row; separators skipped
    // Sub-state: when info or message is showing, input routes to that
    // first and returns to the action list on dismiss.
    char info_body[512];
    bool info_active;
    // Modern: the detail panel's page, and how many pages the renderer last
    // laid its text out into (Left/Right page within that).
    int  detail_page;
    int  detail_pages;
    // Modern: which list the left column shows, the cursor in the Contracts
    // list, and the action waiting on a Yes/No (the main loop owns the prompt).
    TownList    list;
    int         lcursor;     // cursor in a detail list
    TownConfirm confirm;
    TownConfirm asked;       // the one the open prompt is answering
    bool        result_dialog;   // modern: the outcome shows as a dialog until Continue
    int         confirm_slot;
} TownState;
static TownState town;

static void copy_into(char *dst, size_t dst_sz, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t n = 0;
    while (n + 1 < dst_sz && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

void views_open_town(const char *display_name, const char *record_key,
                     int boat_x, int boat_y) {
    // Set the town context statics only. The VIEW_TOWN presentation is
    // raised through the player-IO queue by the engine caller (step.c, which has
    // a Game*) via player_io_raise_view(..., replace=true); the shell's per-frame
    // sync does the views_set, and autoplay acks it. This keeps views_open_town
    // (a ui_host callback with no Game*) free of the queue while still routing the
    // view uniformly.
    memset(&town, 0, sizeof(town));
    copy_into(town.display_name, sizeof(town.display_name), display_name);
    copy_into(town.record_key,   sizeof(town.record_key),   record_key);
    town.boat_x = boat_x;
    town.boat_y = boat_y;
    town.cursor = TOWN_ROW_CONTRACT;
}

static void town_show_info(const char *body) {
    size_t n = 0;
    while (n + 1 < sizeof(town.info_body) && body[n]) {
        town.info_body[n] = body[n];
        n++;
    }
    town.info_body[n] = '\0';
    town.info_active = true;
    town.detail_page = 0;
}

static void town_format_row(const Game *g, TownRow r, char *out, size_t n) {
    const ResBanners *bn = &g->res->banners;
    switch (r) {
        case TOWN_ROW_CONTRACT:
            resources_format_template(out, (int)n, bn->town_row_contract, NULL, 0);
            break;
        case TOWN_ROW_BOAT:
            if (g->boat.has_boat) {
                resources_format_template(out, (int)n, bn->town_row_boat_cancel,
                                          NULL, 0);
            } else {
                char cbuf[16];
                snprintf(cbuf, sizeof cbuf, "%d", GameBoatCost(g));
                ResTemplateVar vars[] = { { "COST", cbuf } };
                resources_format_template(out, (int)n, bn->town_row_boat_rent,
                                          vars, 1);
            }
            break;
        case TOWN_ROW_INFO:
            resources_format_template(out, (int)n, bn->town_row_info, NULL, 0);
            break;
        case TOWN_ROW_SPELL: {
            const TownRecord *t = NULL;
            for (int i = 0; i < GAME_TOWNS; i++) {
                if (strcmp(g->towns[i].id, town.record_key) == 0) {
                    t = &g->towns[i];
                    break;
                }
            }
            if (t && t->spell_for_sale[0]) {
                const SpellDef *sp = spell_by_id(t->spell_for_sale);
                if (sp) {
                    char ccost[16];
                    snprintf(ccost, sizeof ccost, "%d", sp->cost);
                    ResTemplateVar vars[] = {
                        { "SPELL", sp->name },
                        { "SPELL_COST", ccost },
                    };
                    resources_format_template(out, (int)n, bn->town_row_spell,
                                              vars, 2);
                    break;
                }
            }
            resources_format_template(out, (int)n, bn->town_row_spell_none,
                                      NULL, 0);
            break;
        }
        case TOWN_ROW_SIEGE:
            if (g->stats.siege_weapons) {
                resources_format_template(out, (int)n, bn->town_row_siege_owned,
                                          NULL, 0);
            } else {
                char sbuf[16];
                snprintf(sbuf, sizeof sbuf, "%d", g->res->economy.siege_cost);
                ResTemplateVar vars[] = { { "SIEGE_COST", sbuf } };
                resources_format_template(out, (int)n, bn->town_row_siege_buy,
                                          vars, 1);
            }
            break;
        default: out[0] = '\0'; break;
    }
}

static void town_do_contract(Game *g) {
    const ResBanners *bn = &g->res->banners;
    const char *vid = GameTakeNextContract(g);
    const VillainDef *v = vid ? villain_by_id(vid) : NULL;
    char buf[256];
    if (v) {
        char rbuf[16];
        snprintf(rbuf, sizeof rbuf, "%d", v->reward);
        const ResZone *vz = resources_zone_by_id(g->res, v->zone);
        const char *zone_label = (vz && vz->name[0]) ? vz->name : v->zone;
        ResTemplateVar vars[] = {
            { "VILLAIN", v->name },
            { "REWARD",  rbuf },
            { "ZONE",    zone_label },
        };
        resources_format_template(buf, sizeof buf, bn->town_contract_new,
                                  vars, 3);
    } else {
        resources_format_template(buf, sizeof buf, bn->town_contract_none,
                                  NULL, 0);
    }
    town_show_info(buf);
}

static void town_do_boat(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char buf[96];
    // State mutation lives in the engine (GameRentBoat / GameCancelBoat) so
    // headless autoplay can rent too; this function keeps only the dialogs.
    if (g->boat.has_boat) {
        BoatActionResult r = GameCancelBoat(g);
        if (r == BOAT_CANCEL_AT_SEA) {
            // KB: refuses cancellation while sailing.
            resources_format_template(buf, sizeof buf,
                                      bn->town_boat_vacate_first, NULL, 0);
            town_show_info(buf);
        }
        // BOAT_CANCEL_OK: silent (menu redraw shows the rent row again).
        return;
    }
    BoatActionResult r = GameRentBoat(g, town.boat_x, town.boat_y,
                                      g->position.zone);
    if (r == BOAT_RENT_NO_GOLD) {
        // KB: `if (gold <= boat_cost)` -- exact-match also fails.
        resources_format_template(buf, sizeof buf, bn->town_no_gold, NULL, 0);
        town_show_info(buf);
    }
    // BOAT_RENT_OK: no success popup -- menu redraw shows "Cancel boat rental"
    // in row B as the visible confirmation.
}

// Append `frag` to `buf` (NUL-terminated) starting at `*off`, advancing the
// offset. No-op once the buffer is full. Used by town_do_info to compose
// the multi-fragment intel banner.
static void append_fragment(char *buf, size_t cap, size_t *off,
                            const char *frag) {
    if (*off + 1 >= cap) return;
    int n = snprintf(buf + *off, cap - *off, "%s", frag);
    if (n < 0) return;
    if ((size_t)n >= cap - *off) { *off = cap - 1; return; }
    *off += (size_t)n;
}

static void town_format_intel(const Game *g, char *buf, size_t cap);

static void town_do_info(const Game *g) {
    char buf[512];
    town_format_intel(g, buf, sizeof buf);
    town_show_info(buf);
}

static void town_format_intel(const Game *g, char *out, size_t cap) {
    // : this town reports intel
    // on the castle named in its intel_castle field. Display:
    //   "Castle <name> is under
    //    <owner>'s rule.
    //
    //      <number_name> <troop>
    //      ..."
    const ResBanners *bn = &g->res->banners;
    char buf[512];
    buf[0] = '\0';

    const ResTown *rt = g->res
        ? resources_town_by_id(g->res, town.record_key) : NULL;
    if (!rt || !rt->intel_castle[0]) {
        resources_format_template(buf, sizeof buf, bn->town_intel_unavailable,
                                  NULL, 0);
        snprintf(out, cap, "%s", buf);
        return;
    }
    const ResCastle *rc = g->res
        ? resources_castle_by_id(g->res, rt->intel_castle) : NULL;
    const CastleRecord *cr = GameFindCastleConst(g, rt->intel_castle);
    if (!rc || !cr || rc->special.excluded_from_intel) {
        resources_format_template(buf, sizeof buf, bn->town_intel_unavailable,
                                  NULL, 0);
        snprintf(out, cap, "%s", buf);
        return;
    }

    size_t off = 0;
    char tmp[256];
    const char *disp_name = rc->name[0] ? rc->name : cr->id;
    {
        ResTemplateVar vars[] = { { "NAME", disp_name } };
        resources_format_template(tmp, sizeof tmp,
                                  bn->town_intel_castle_under, vars, 1);
        append_fragment(buf, sizeof buf, &off, tmp);
    }

    const char *owner = bn->town_intel_owner_none;
    switch (cr->owner_kind) {
        case CASTLE_OWNER_PLAYER:
            owner = bn->town_intel_owner_player;
            break;
        case CASTLE_OWNER_MONSTERS:
            owner = bn->town_intel_owner_none;
            break;
        case CASTLE_OWNER_VILLAIN: {
            const VillainDef *v = villain_by_id(cr->villain_id);
            owner = (v && v->name[0]) ? v->name : cr->villain_id;
            break;
        }
        case CASTLE_OWNER_SPECIAL:
            owner = bn->town_intel_owner_king;  // not reachable under normal intel
            break;
    }
    {
        ResTemplateVar vars[] = { { "OWNER", owner } };
        resources_format_template(tmp, sizeof tmp,
                                  bn->town_intel_owner_rule, vars, 1);
        append_fragment(buf, sizeof buf, &off, tmp);
    }

    int stacks_shown = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS && off + 1 < sizeof(buf); i++) {
        const Unit *u = &cr->garrison[i];
        if (!u->id[0] || u->count == 0) continue;
        const TroopDef *t = troop_by_id(u->id);
        const char *tname = (t && t->name[0]) ? t->name : u->id;
        const char *count_label = GameNumberName(g, u->count);
        if (count_label[0]) {
            ResTemplateVar vars[] = {
                { "LABEL", count_label }, { "TROOP", tname },
            };
            resources_format_template(tmp, sizeof tmp,
                                      bn->town_intel_count_named, vars, 2);
        } else {
            char cbuf[16];
            snprintf(cbuf, sizeof cbuf, "%d", u->count);
            ResTemplateVar vars[] = {
                { "COUNT", cbuf }, { "TROOP", tname },
            };
            resources_format_template(tmp, sizeof tmp,
                                      bn->town_intel_count_numeric, vars, 2);
        }
        append_fragment(buf, sizeof buf, &off, tmp);
        stacks_shown++;
    }
    if (!stacks_shown && off + 1 < sizeof(buf)) {
        // No specific stack data. For monster castles shows a
        // generic "Various groups of monsters" line until the garrison
        // is rolled (which happens the first time the castle is sieged).
        const char *src = (cr->owner_kind == CASTLE_OWNER_MONSTERS)
            ? bn->town_intel_monsters_generic
            : bn->town_intel_no_garrison;
        resources_format_template(tmp, sizeof tmp, src, NULL, 0);
        append_fragment(buf, sizeof buf, &off, tmp);
    }
    snprintf(out, cap, "%s", buf);
}

static void town_do_spell(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char buf[128];
    // State mutation lives in GameBuySpell (engine) so autoplay can buy too;
    // this keeps the dialogs. Behavior unchanged (same gates, same order).
    SpellBuyResult r = GameBuySpell(g, town.record_key);
    switch (r) {
    case SPELL_BUY_NO_SPELL:
        resources_format_template(buf, sizeof buf,
                                  bn->town_spell_unavailable, NULL, 0);
        break;
    case SPELL_BUY_AT_CAP:
        resources_format_template(buf, sizeof buf, bn->town_spell_at_cap,
                                  NULL, 0);
        break;
    case SPELL_BUY_NO_GOLD:
        resources_format_template(buf, sizeof buf, bn->town_no_gold, NULL, 0);
        break;
    case SPELL_BUY_NO_RITES:
        views_town_rites_text(g, buf, sizeof buf);
        break;
    case SPELL_BUY_OK: {
        // Spells remaining after this buy = max_spells - known(now). Equals the
        // old "max_spells - known_before - 1" since known_before went up by one.
        int left = g->stats.max_spells - GameKnownSpells(g);
        char lbuf[16];
        snprintf(lbuf, sizeof lbuf, "%d", left);
        ResTemplateVar vars[] = {
            { "LEFT", lbuf },
            { "S",    (left == 1 ? "" : "s") },
        };
        resources_format_template(buf, sizeof buf, bn->town_spell_can_learn,
                                  vars, 2);
        break;
    }
    }
    town_show_info(buf);
}

static void town_do_siege(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char buf[96];
    // State mutation lives in GameBuySiege (engine) so autoplay can buy too;
    // this keeps the dialogs. Behavior unchanged.
    SiegeBuyResult r = GameBuySiege(g);
    const char *tmpl = NULL;
    switch (r) {
    case SIEGE_BUY_ALREADY: tmpl = bn->town_siege_already;   break;
    case SIEGE_BUY_NO_GOLD: tmpl = bn->town_no_gold;         break;
    case SIEGE_BUY_OK:      tmpl = bn->town_siege_purchased; break;
    }
    resources_format_template(buf, sizeof buf, tmpl, NULL, 0);
    town_show_info(buf);
}

// Modern: a boat is rented only near the sea -- the town's dock must be set and
// no more than this many tiles from the town in either direction.
#define TOWN_BOAT_RANGE 4

bool views_town_boat_available(const Game *g) {
    const ResTown *t = (g && g->res) ? resources_town_by_id(g->res, town.record_key)
                                     : NULL;
    if (!t || t->boat_x < 0 || t->boat_y < 0) return false;
    int dx = abs(t->boat_x - t->x), dy = abs(t->boat_y - t->y);
    return dx <= TOWN_BOAT_RANGE && dy <= TOWN_BOAT_RANGE;
}

// Modern: every section opens except Boat at a town with no boat master, which
// shows the empty boat and cannot be entered; legacy offers every row.
bool views_town_row_enabled(const Game *g, int row) {
    if (row < 0 || row >= TOWN_ROW_COUNT) return false;
    if (row == TOWN_ROW_BOAT && CL_IS_MODERN) return views_town_boat_available(g);
    if (row == TOWN_ROW_SPELL && CL_IS_MODERN && g && g->res && g->res->economy.rites_per_zone)
        return GameTownHasRites(g, town.record_key);
    return true;
}

static void town_do_row(Game *g, TownRow r) {
    if (!views_town_row_enabled(g, r)) return;
    switch (r) {
        case TOWN_ROW_CONTRACT: town_do_contract(g); break;
        case TOWN_ROW_BOAT:     town_do_boat(g);     break;
        case TOWN_ROW_INFO:     town_do_info(g);     break;
        case TOWN_ROW_SPELL:    town_do_spell(g);    break;
        case TOWN_ROW_SIEGE:    town_do_siege(g);    break;
        default: break;
    }
}

// =============================================================================
//  Modern town input
// =============================================================================
// Two levels, one rule set:
//   the menu     Up/Down move, Enter opens the row's detail, Esc leaves town
//   a detail     Up/Down read the text a page at a time and then move to the
//                next row; Enter carries out the row (Back returns);
//                Esc returns to the menu
// Anything that changes the game asks Yes/No first. Nothing else ever holds
// the keys: a result message shows in the detail panel until the next key.

static int town_cycle_len(const Game *g) {
    int n = g->res->contract.cycle_length;
    if (n < 1) n = 1;
    return n > CONTRACT_CYCLE_MAX ? CONTRACT_CYCLE_MAX : n;
}

int views_town_contract_slot(const Game *g, int row) {
    int k = 0, n = town_cycle_len(g);
    for (int i = 0; i < n; i++) {
        if (!g->contract.cycle[i][0]) continue;
        if (k++ == row) return i;
    }
    return -1;      // the Back row
}

static const TownRecord *town_record(const Game *g) {
    for (int i = 0; i < GAME_TOWNS; i++)
        if (strcmp(g->towns[i].id, town.record_key) == 0) return &g->towns[i];
    return NULL;
}

const SpellDef *views_town_spell(const Game *g) {
    const TownRecord *t = g ? town_record(g) : NULL;
    return (t && t->spell_for_sale[0]) ? spell_by_id(t->spell_for_sale) : NULL;
}

static TownList town_list_for_row(TownRow r) {
    switch (r) {
        case TOWN_ROW_CONTRACT: return TOWN_LIST_CONTRACTS;
        case TOWN_ROW_INFO:     return TOWN_LIST_INFO;
        case TOWN_ROW_BOAT:     return TOWN_LIST_BOAT;
        case TOWN_ROW_SPELL:    return TOWN_LIST_TEMPLE;
        case TOWN_ROW_SIEGE:    return TOWN_LIST_SIEGE;
        default:                return TOWN_LIST_MENU;
    }
}

int views_town_list_rows(const Game *g) {
    if (!g) return 1;
    if (town.list == TOWN_LIST_MENU) return TOWN_ROW_COUNT + (CL_IS_MODERN ? 1 : 0);   // + Leave
    if (town.list == TOWN_LIST_CONTRACTS) {
        int k = 0, n = town_cycle_len(g);
        for (int i = 0; i < n; i++) if (g->contract.cycle[i][0]) k++;
        return k + 1;
    }
    return 2;   // the screen's one action, then Back
}

int views_town_list_cursor(void) {
    return town.list == TOWN_LIST_MENU ? town.cursor : town.lcursor;
}

bool views_town_list_row(const Game *g, int i, char *out, int cap,
                         bool *enabled, bool *held) {
    if (enabled) *enabled = true;
    if (held) *held = false;
    out[0] = '\0';
    if (!g || !g->res || i < 0 || i >= views_town_list_rows(g)) return false;
    const ResBanners *bn = &g->res->banners;
    if (town.list == TOWN_LIST_MENU) {
        if (i == TOWN_ROW_LEAVE) {
            snprintf(out, (size_t)cap, "%s", bn->location_leave);
            return true;
        }
        if (enabled) *enabled = views_town_row_enabled(g, i);
        return views_town_menu_label(g, i, out, cap);
    }
    if (i == views_town_list_rows(g) - 1) {
        resources_format_template(out, cap, bn->town_back, NULL, 0);
        return true;
    }
    switch (town.list) {
        case TOWN_LIST_CONTRACTS: {
            int slot = views_town_contract_slot(g, i);
            const VillainDef *v = villain_by_id(g->contract.cycle[slot]);
            snprintf(out, (size_t)cap, "%s", v ? v->name : g->contract.cycle[slot]);
            if (held) *held = strcmp(g->contract.cycle[slot], g->contract.active_id) == 0;
            break;
        }
        case TOWN_LIST_INFO: {
            const ResTown *rt = resources_town_by_id(g->res, town.record_key);
            const ResCastle *rc = (rt && rt->intel_castle[0])
                                ? resources_castle_by_id(g->res, rt->intel_castle) : NULL;
            snprintf(out, (size_t)cap, "%s", rc ? rc->name : "");
            if (enabled) *enabled = false;   // the report is on show; nothing to do
            break;
        }
        case TOWN_LIST_BOAT:
            resources_format_template(out, cap, g->boat.has_boat ? bn->town_menu_boat_cancel
                                                                 : bn->town_menu_boat_rent, NULL, 0);
            if (enabled) *enabled = views_town_boat_available(g);
            break;
        case TOWN_LIST_TEMPLE: {
            const SpellDef *sp = views_town_spell(g);
            ResTemplateVar v[] = { { "SPELL", sp ? sp->name : "" } };
            resources_format_template(out, cap, bn->town_action_spell, v, 1);
            if (enabled) *enabled = sp != NULL;
            break;
        }
        case TOWN_LIST_SIEGE:
            resources_format_template(out, cap, g->stats.siege_weapons ? bn->town_action_owned
                                                                       : bn->town_action_siege, NULL, 0);
            if (enabled) *enabled = !g->stats.siege_weapons;
            break;
        default: break;
    }
    return true;
}

static bool town_row_live(const Game *g, int i) {
    bool en = true;
    char tmp[64];
    views_town_list_row(g, i, tmp, sizeof tmp, &en, NULL);
    return en;
}

// The next enabled row from `from` in the current list, wrapping.
static int town_list_step(const Game *g, int from, int dir) {
    int n = views_town_list_rows(g), r = from;
    for (int k = 0; k < n; k++) {
        r = (r + dir + n) % n;
        if (town_row_live(g, r)) return r;
    }
    return from;
}

static void town_ask(TownConfirm c) {
    town.confirm = c;
}

static void town_open(const Game *g, TownRow r) {
    if (!views_town_row_enabled(g, r)) return;
    town.list = town_list_for_row(r);
    town.detail_page = 0;
    int n = views_town_list_rows(g);
    town.lcursor = town_row_live(g, 0) ? 0 : n - 1;   // the action, else Back
    if (town.list == TOWN_LIST_CONTRACTS)
        for (int i = 0; i + 1 < n; i++) {             // start on the one held
            int slot = views_town_contract_slot(g, i);
            if (strcmp(g->contract.cycle[slot], g->contract.active_id) == 0) town.lcursor = i;
        }
}

// Enter (or a tap) on a detail list row.
static void town_list_do_row(Game *g, int i) {
    const ResBanners *bn = &g->res->banners;
    char buf[RES_BANNER_LEN];
    if (i == views_town_list_rows(g) - 1) {                 // Back
        town.list = TOWN_LIST_MENU;
        town.detail_page = 0;
        return;
    }
    if (!town_row_live(g, i)) return;
    switch (town.list) {
        case TOWN_LIST_CONTRACTS: {
            int slot = views_town_contract_slot(g, i);
            if (strcmp(g->contract.cycle[slot], g->contract.active_id) == 0) return;
            town.confirm_slot = slot;
            town_ask(TOWN_CONFIRM_CONTRACT);
            break;
        }
        case TOWN_LIST_BOAT:
            if (g->boat.has_boat && g->travel_mode == TRAVEL_BOAT) {
                resources_format_template(buf, sizeof buf, bn->town_boat_vacate_first, NULL, 0);
                town_show_info(buf);
            } else {
                town_ask(g->boat.has_boat ? TOWN_CONFIRM_BOAT_CANCEL : TOWN_CONFIRM_BOAT_RENT);
            }
            break;
        case TOWN_LIST_TEMPLE: town_ask(TOWN_CONFIRM_SPELL); break;
        case TOWN_LIST_SIEGE:  town_ask(TOWN_CONFIRM_SIEGE); break;
        default: break;
    }
}

void views_gallery_town(const Game *g, int row, int lcursor, const char *info, bool dialog) {
    town.list = (row < 0) ? TOWN_LIST_MENU : town_list_for_row((TownRow)row);
    if (row >= 0) town.cursor = row;
    else          town.cursor = lcursor;
    town.lcursor = lcursor;
    town.detail_page = 0;
    town.info_active = false;
    if (info && info[0]) town_show_info(info);
    town.result_dialog = dialog;
    (void)g;
}

bool views_town_result_dialog(void) {
    return view_stack_top() == VIEW_TOWN && town.result_dialog;
}

static bool town_modern_update(Game *g) {
    if (town.result_dialog) {
        // Continue: any key or a tap closes it, back to the main page.
        if (ui_any_key_pressed() || touch_tapped_row(TOUCH_LIST_PROMPT) == 0) {
            town.result_dialog = false;
            town.info_active = false;
            town.list = TOWN_LIST_MENU;
            town.detail_page = 0;
        }
        return true;
    }
    bool menu = (town.list == TOWN_LIST_MENU);
    int rows = views_town_list_rows(g);

    // A message stays only until the next key; the key still does its job.
    if (town.info_active && ui_any_key_pressed()) town.info_active = false;

    if (input_key_pressed(KEY_ESCAPE)) {
        if (menu) views_dismiss();
        else      { town.list = TOWN_LIST_MENU; town.detail_page = 0; }
        return true;
    }
    int tapped = touch_tapped_row(TOUCH_LIST_TOWN);
    if (tapped >= 0 && tapped < rows) {
        if (menu && tapped == TOWN_ROW_LEAVE) { views_dismiss(); return true; }
        if (menu) { town.cursor = tapped; town_open(g, (TownRow)tapped); }
        else      { town.lcursor = tapped; town.detail_page = 0; town_list_do_row(g, tapped); }
        return true;
    }
    int dir = (input_key_pressed(KEY_UP) || input_key_pressed(KEY_W) ||
               input_key_pressed(KEY_KP_8)) ? -1
            : (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_S) ||
               input_key_pressed(KEY_KP_2)) ? +1 : 0;
    if (dir && menu) {
        town.cursor = (town.cursor + dir + rows) % rows;
        town.detail_page = 0;
        return true;
    }
    if (dir) {
        // Up/Down read straight through: down turns the page, and past the
        // last page moves to the next row's first; up turns back, and before
        // the first page moves to the previous row's last (the renderer
        // clamps the page to the pages it lays out).
        if (dir > 0 && town.detail_page + 1 < town.detail_pages) {
            town.detail_page++;
        } else if (dir < 0 && town.detail_page > 0) {
            town.detail_page--;
        } else {
            int next = town_list_step(g, town.lcursor, dir);
            if (next != town.lcursor) {
                town.lcursor = next;
                town.detail_page = (dir > 0) ? 0 : 1000;
            }
        }
        return true;
    }
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
        input_key_pressed(KEY_SPACE)) {
        if (menu && town.cursor == TOWN_ROW_LEAVE) views_dismiss();
        else if (menu) town_open(g, (TownRow)town.cursor);
        else           town_list_do_row(g, town.lcursor);
        return true;
    }
    return false;
}

bool views_town_update(Game *g) {
    if (view_stack_top() != VIEW_TOWN) return false;

    touch_request(TOUCH_CHROME_BACK);
    if (CL_IS_MODERN) return town_modern_update(g);

    if (town.info_active) {
        // Any key dismisses the info panel and returns to the menu.
        touch_region_any(KEY_ENTER);
        if (input_key_pressed(KEY_ESCAPE) || input_key_pressed(KEY_ENTER) ||
            input_key_pressed(KEY_KP_ENTER) || input_key_pressed(KEY_SPACE)) {
            town.info_active = false;
            return true;
        }
        // Letters A-E also dismiss so the player can chain actions.
        for (int k = KEY_A; k <= KEY_E; k++) {
            if (input_key_pressed(k)) { town.info_active = false; return true; }
        }
        return true;
    }

    if (input_key_pressed(KEY_ESCAPE)) {
        views_dismiss();
        return true;
    }
    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_W) || input_key_pressed(KEY_KP_8)) {
        town.cursor = (town.cursor - 1 + TOWN_ROW_COUNT) % TOWN_ROW_COUNT;
        return true;
    }
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_S) || input_key_pressed(KEY_KP_2)) {
        town.cursor = (town.cursor + 1) % TOWN_ROW_COUNT;
        return true;
    }
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
        input_key_pressed(KEY_SPACE)) {
        town_do_row(g, (TownRow)town.cursor);
        return true;
    }
    if (input_key_pressed(KEY_A)) { town.cursor = TOWN_ROW_CONTRACT; town_do_row(g, TOWN_ROW_CONTRACT); return true; }
    if (input_key_pressed(KEY_B)) { town.cursor = TOWN_ROW_BOAT;     town_do_row(g, TOWN_ROW_BOAT);     return true; }
    if (input_key_pressed(KEY_C)) { town.cursor = TOWN_ROW_INFO;     town_do_row(g, TOWN_ROW_INFO);     return true; }
    if (input_key_pressed(KEY_D)) { town.cursor = TOWN_ROW_SPELL;    town_do_row(g, TOWN_ROW_SPELL);    return true; }
    if (input_key_pressed(KEY_E)) { town.cursor = TOWN_ROW_SIEGE;    town_do_row(g, TOWN_ROW_SIEGE);    return true; }
    return false;
}

// ===========================================================================
//  View-state accessors (read-only views of menu / town internal state)
// ===========================================================================

const char *views_menu_title(void) {
    if (view_stack_top() != VIEW_MENU) return NULL;
    const MenuFrame *f = menu_top();
    return (f && f->page) ? f->page->title : NULL;
}

int views_menu_entry_count(void) {
    if (view_stack_top() != VIEW_MENU) return 0;
    const MenuFrame *f = menu_top();
    return (f && f->page) ? f->page->count : 0;
}

const char *views_menu_entry_label(int i) {
    if (view_stack_top() != VIEW_MENU) return NULL;
    const MenuFrame *f = menu_top();
    if (!f || !f->page) return NULL;
    if (i < 0 || i >= f->page->count) return NULL;
    return f->page->entries[i].label;
}

bool views_menu_entry_is_submenu(int i) {
    if (view_stack_top() != VIEW_MENU) return false;
    const MenuFrame *f = menu_top();
    if (!f || !f->page) return false;
    if (i < 0 || i >= f->page->count) return false;
    return f->page->entries[i].kind == MENU_KIND_SUBMENU;
}

int views_menu_cursor(void) {
    if (view_stack_top() != VIEW_MENU) return -1;
    const MenuFrame *f = menu_top();
    return f ? f->cursor : -1;
}

const char *views_town_display_name(void) {
    if (view_stack_top() != VIEW_TOWN) return NULL;
    return town.display_name[0] ? town.display_name : NULL;
}

const char *views_town_record_key(void) {
    if (view_stack_top() != VIEW_TOWN) return NULL;
    return town.record_key[0] ? town.record_key : NULL;
}

bool views_town_row_text(const Game *g, int row, char *out, int out_sz) {
    if (view_stack_top() != VIEW_TOWN) return false;
    if (row < 0 || row >= TOWN_ROW_COUNT) return false;
    town_format_row(g, (TownRow)row, out, (size_t)out_sz);
    return true;
}

bool views_town_menu_label(const Game *g, int row, char *out, int out_sz) {
    if (!g || !g->res || row < 0 || row >= TOWN_ROW_COUNT) return false;
    const ResBanners *bn = &g->res->banners;
    const char *s = "";
    switch ((TownRow)row) {
        case TOWN_ROW_CONTRACT: s = bn->town_menu_contract; break;
        case TOWN_ROW_BOAT:     s = bn->town_menu_boat; break;
        case TOWN_ROW_INFO:     s = bn->town_menu_info;  break;
        case TOWN_ROW_SPELL:    s = bn->town_menu_spell; break;
        case TOWN_ROW_SIEGE:    s = bn->town_menu_siege; break;
        default: break;
    }
    resources_format_template(out, out_sz, s, NULL, 0);
    return true;
}

int views_town_row_count(void) {
    return TOWN_ROW_COUNT;
}

const char *views_town_info_text(void) {
    if (view_stack_top() != VIEW_TOWN || !town.info_active) return NULL;
    return town.info_body;
}

TownConfirm views_town_take_confirm(const Game *g, char *body, int cap) {
    TownConfirm c = town.confirm;
    town.confirm = TOWN_CONFIRM_NONE;
    town.asked = c;
    if (c == TOWN_CONFIRM_NONE || !g) return c;
    const ResBanners *bn = &g->res->banners;
    char a[16], b[16];
    switch (c) {
        case TOWN_CONFIRM_CONTRACT:
            resources_format_template(body, cap, bn->town_contract_confirm, NULL, 0);
            break;
        case TOWN_CONFIRM_BOAT_RENT: {
            snprintf(a, sizeof a, "%d", GameBoatCost(g));
            ResTemplateVar v[] = { { "COST", a } };
            resources_format_template(body, cap, bn->town_confirm_boat_rent, v, 1);
            break;
        }
        case TOWN_CONFIRM_BOAT_CANCEL:
            resources_format_template(body, cap, bn->town_confirm_boat_cancel, NULL, 0);
            break;
        case TOWN_CONFIRM_SPELL: {
            const SpellDef *sp = NULL;
            for (int i = 0; i < GAME_TOWNS; i++)
                if (strcmp(g->towns[i].id, town.record_key) == 0)
                    sp = spell_by_id(g->towns[i].spell_for_sale);
            snprintf(b, sizeof b, "%d", sp ? sp->cost : 0);
            ResTemplateVar v[] = { { "SPELL", sp ? sp->name : "" }, { "SPELL_COST", b } };
            resources_format_template(body, cap, bn->town_confirm_spell, v, 2);
            break;
        }
        case TOWN_CONFIRM_SIEGE: {
            snprintf(a, sizeof a, "%d", g->res->economy.siege_cost);
            ResTemplateVar v[] = { { "SIEGE_COST", a } };
            resources_format_template(body, cap, bn->town_confirm_siege, v, 1);
            break;
        }
        default: break;
    }
    return c;
}

void views_town_confirm_yes(Game *g) {
    if (view_stack_top() != VIEW_TOWN || !g) return;
    switch (town.asked) {
        case TOWN_CONFIRM_CONTRACT:    GameTakeContractAt(g, town.confirm_slot); break;
        case TOWN_CONFIRM_BOAT_RENT:
        case TOWN_CONFIRM_BOAT_CANCEL: town_do_boat(g);  break;
        case TOWN_CONFIRM_SPELL:       town_do_spell(g); break;
        case TOWN_CONFIRM_SIEGE:       town_do_siege(g); break;
        default: break;
    }
    // Modern: the boat, spell and siege outcomes are said in a dialog with the
    // section's person, and Continue returns to the town's main page.
    if (CL_IS_MODERN && town.info_active &&
        (town.asked == TOWN_CONFIRM_BOAT_RENT || town.asked == TOWN_CONFIRM_BOAT_CANCEL ||
         town.asked == TOWN_CONFIRM_SPELL || town.asked == TOWN_CONFIRM_SIEGE))
        town.result_dialog = true;
    town.asked = TOWN_CONFIRM_NONE;
    town.detail_page = 0;
}

TownList views_town_list(void) { return town.list; }

void views_town_rites_text(const Game *g, char *out, int cap) {
    const Resources *res = g ? g->res : NULL;
    const ResTown *t = res ? resources_town_by_id(res, town.record_key) : NULL;
    const ResZone *z = t ? resources_zone_by_id(res, t->zone) : NULL;
    char xb[12], yb[12];
    snprintf(xb, sizeof xb, "%d", z ? z->magic_alcove_x : 0);
    snprintf(yb, sizeof yb, "%d", z ? z->magic_alcove_y : 0);
    ResTemplateVar vars[] = {
        { "HERO", g ? g->character.name : "" },
        { "ZONE", (z && z->name[0]) ? z->name : "" },
        { "X", xb }, { "Y", yb },
    };
    resources_format_template(out, cap, res ? res->banners.town_temple_needs_rites : "", vars, 4);
}

void views_town_intel_text(const Game *g, char *out, int cap) {
    town_format_intel(g, out, (size_t)cap);
}

int views_town_detail_page(void) { return town.detail_page; }

void views_town_set_detail_pages(int pages) {
    town.detail_pages = pages < 1 ? 1 : pages;
    if (town.detail_page >= town.detail_pages) town.detail_page = town.detail_pages - 1;
}

int views_town_cursor(void) {
    if (view_stack_top() != VIEW_TOWN) return -1;
    return town.cursor;
}

// Demo/attract driver (RENDER-ONLY): step the town cursor one row toward
// `target_row` per call so the watcher SEES it move (like a human pressing UP/
// DOWN). Returns true once the cursor has REACHED target_row, false while still
// advancing. STATE-INERT: it moves ONLY the shell's town cursor and NEVER
// executes a row (no town_do_row / GameBuySiege). The transaction itself is a
// SEPARATE recorded engine primitive (e.g. RA_BUY_SIEGE) the executor applies on
// the live world via the same gate-step a player uses -- so this presenter only
// renders the screen the way a player sees it and can never mutate game state or
// make visible diverge from headless. No-op (returns true) if not in VIEW_TOWN
// or target_row is out of range.
bool views_town_demo_step_cursor(int target_row) {
    if (view_stack_top() != VIEW_TOWN) return true;
    if (target_row < 0 || target_row >= TOWN_ROW_COUNT) return true;
    if (town.cursor == target_row) return true;
    if (town.cursor < target_row) town.cursor++;
    else                          town.cursor--;
    return town.cursor == target_row;
}

// ---- Controls settings panel -----------------------------------------------
static int g_controls_cursor = 0;

int views_controls_cursor(void) { return g_controls_cursor; }

void views_controls_set_cursor(int r) {
    if (r < 0) r = 0;
    g_controls_cursor = r;
}

// True iff the row labels Sounds / Music / Volume -- the three controls
// that depend on a working audio device.
static bool controls_row_is_audio(const struct Game *g, int row) {
    if (!g || !g->res || row < 0 || row >= g->res->controls.count) return false;
    const char *L = g->res->controls.items[row].label;
    if (!L) return false;
    return strcmp(L, "Sounds") == 0 ||
           strcmp(L, "Music")  == 0 ||
           strcmp(L, "Volume") == 0;
}

bool views_controls_row_disabled(const struct Game *g, int row) {
    // Only once the device has definitely failed: while it is still opening
    // in the background the rows stay live.
    return controls_row_is_audio(g, row) && audio_status() == AUDIO_UNAVAILABLE;
}

// The Scale row is not one of the pack's controls: it is appended by the shell
// and backed by present.c, not by stats.options[]. Display scale belongs to the
// machine looking at the game, and stats.options[] is serialized into saves.
// Cycles 1x -> 2x -> ... -> the largest scale this window can show -> 1x.
// There is no Auto: 1x is one buffer pixel to one screen pixel, which is what a
// modern pack is authored for, and a bigger window shows more tiles rather than
// bigger ones. Wrapping at the measured maximum rather than a constant is what
// keeps the label honest -- an entry that the window cannot show would render
// clamped and say something else.
//
// A fixed buffer (CL_IS_NATIVE) cycles 1x -> 2x -> 3x -> 1x and resizes the
// window to the buffer times the scale, wrapping at the largest one the monitor
// can hold whole.
void views_controls_advance_scale(void) {
    int s = present_get_scale() + 1;
    if (CL_IS_NATIVE) {
        // A movie needs one frame size, and a fixed buffer renders at the
        // zoom, so the zoom is locked while the recorder runs.
        if (recorder_active()) return;
        int mon = GetCurrentMonitor();
        int fit = present_max_scale(GetMonitorWidth(mon), GetMonitorHeight(mon));
        if (IsWindowFullscreen())
            fit = present_max_scale(GetScreenWidth(), GetScreenHeight());
        if (s > fit) s = 1;
        present_set_scale(s);
        present_zoom_window(s);
        return;
    }
    if (s > present_max_scale(GetScreenWidth(), GetScreenHeight())) s = 1;
    present_set_scale(s);
}

int views_controls_scale_value(void) { return present_get_scale(); }

void views_controls_advance(struct Game *g, int row) {
    if (!g || !g->res) return;
    if (row < 0 || row >= g->res->controls.count) return;
    if (views_controls_row_disabled(g, row)) return; // grayed-out, no-op
    int range = g->res->controls.items[row].range;
    if (range < 2) range = 2;
    g->stats.options[row] = (g->stats.options[row] + 1) % range;
}
