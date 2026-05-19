#include "views.h"
#include "raylib.h"
#include "audio.h"
#include "tables.h"
#include "recorder.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

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
} spell_state = { 0 };

void views_spells_set_mode(bool cast_mode) {
    spell_state.active = cast_mode;
    spell_state.column = 1;  // default to adventure column for overworld
    spell_state.chosen = -1;
}

int views_spells_chosen(void) {
    int result = spell_state.chosen;
    spell_state.chosen = -1;
    return result;
}

bool views_spells_update(void) {
    if (!spell_state.active) return false;
    if (IsKeyPressed(KEY_LEFT))  spell_state.column = 0;
    if (IsKeyPressed(KEY_RIGHT)) spell_state.column = 1;
    if (IsKeyPressed(KEY_ESCAPE)) {
        spell_state.active = false;
        views_dismiss();
        return false;
    }
    for (int i = 0; i < 7; i++) {
        if (IsKeyPressed(KEY_A + i)) {
            spell_state.chosen = spell_state.column * 7 + i;
            views_dismiss();
            return true;
        }
    }
    return false;
}

// =============================================================================
//  Game Menu — unified, nested
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
    { "Army",      MENU_KIND_VIEW,   NULL, VIEW_ARMY,      MENU_ACT_NONE },
    { "Spells",    MENU_KIND_VIEW,   NULL, VIEW_SPELLS,    MENU_ACT_NONE },
    { "Character", MENU_KIND_VIEW,   NULL, VIEW_CHARACTER, MENU_ACT_NONE },
    { "Contract",  MENU_KIND_VIEW,   NULL, VIEW_CONTRACT,  MENU_ACT_NONE },
    { "Puzzle",    MENU_KIND_VIEW,   NULL, VIEW_PUZZLE,    MENU_ACT_NONE },
    { "View Map",  MENU_KIND_VIEW,   NULL, VIEW_WORLDMAP,  MENU_ACT_NONE },
    { "Back",      MENU_KIND_BACK,   NULL, VIEW_NONE,      MENU_ACT_NONE },
};
static MenuPage VIEWS_PAGE = {
    "Views", VIEWS_ENTRIES, (int)(sizeof(VIEWS_ENTRIES) / sizeof(VIEWS_ENTRIES[0]))
};

static MenuEntry SYSTEM_ENTRIES[] = {
    { "Save",      MENU_KIND_ACTION, NULL, VIEW_NONE, MENU_ACT_SAVE },
    { "Load",      MENU_KIND_ACTION, NULL, VIEW_NONE, MENU_ACT_LOAD },
    { "New Game",  MENU_KIND_ACTION, NULL, VIEW_NONE, MENU_ACT_NEW  },
    { "Back",      MENU_KIND_BACK,   NULL, VIEW_NONE, MENU_ACT_NONE },
};
static MenuPage SYSTEM_PAGE = {
    "Options", SYSTEM_ENTRIES, (int)(sizeof(SYSTEM_ENTRIES) / sizeof(SYSTEM_ENTRIES[0]))
};

static MenuEntry ROOT_ENTRIES[] = {
    { "Views",   MENU_KIND_SUBMENU, &VIEWS_PAGE,  VIEW_NONE, MENU_ACT_NONE },
    { "Options", MENU_KIND_SUBMENU, &SYSTEM_PAGE, VIEW_NONE, MENU_ACT_NONE },
    { "Back",    MENU_KIND_BACK,    NULL,         VIEW_NONE, MENU_ACT_NONE },
    { "Exit",    MENU_KIND_ACTION,  NULL,         VIEW_NONE, MENU_ACT_QUIT },
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

static void menu_open_root(void) {
    menus_bind_labels();
    menu_depth = 1;
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
        fprintf(stderr, "views_push: stack overflow (depth=%d, max=%d)\n",
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

static bool s_contract_has_active = false;
void views_contract_set_active(bool active) { s_contract_has_active = active; }

bool views_wants_exit_hint(void) {
    switch (view_stack_top()) {
        case VIEW_CHARACTER:
        case VIEW_ARMY:
        case VIEW_SPELLS:
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

    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_KP_8)) {
        f->cursor = (f->cursor - 1 + n) % n;
        return true;
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsKeyPressed(KEY_KP_2)) {
        f->cursor = (f->cursor + 1) % n;
        return true;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
        IsKeyPressed(KEY_SPACE)) {
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
    if (IsKeyPressed(KEY_ESCAPE)) {
        menu_pop_or_close();
        return true;
    }
    return false;
}

// =============================================================================
//  Town — visit menu (contract / boat / info / spell / siege)
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
} TownState;
static TownState town;

// Menu rows. Keep  order: Contract, Boat, Info, Spell, Siege.
typedef enum {
    TOWN_ROW_CONTRACT = 0,
    TOWN_ROW_BOAT,
    TOWN_ROW_INFO,
    TOWN_ROW_SPELL,
    TOWN_ROW_SIEGE,
    TOWN_ROW_COUNT,
} TownRow;

static void copy_into(char *dst, size_t dst_sz, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t n = 0;
    while (n + 1 < dst_sz && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

void views_open_town(const char *display_name, const char *record_key,
                     int boat_x, int boat_y) {
    memset(&town, 0, sizeof(town));
    copy_into(town.display_name, sizeof(town.display_name), display_name);
    copy_into(town.record_key,   sizeof(town.record_key),   record_key);
    town.boat_x = boat_x;
    town.boat_y = boat_y;
    town.cursor = TOWN_ROW_CONTRACT;
    views_set(VIEW_TOWN);
}

static void town_show_info(const char *body) {
    size_t n = 0;
    while (n + 1 < sizeof(town.info_body) && body[n]) {
        town.info_body[n] = body[n];
        n++;
    }
    town.info_body[n] = '\0';
    town.info_active = true;
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
    if (g->boat.has_boat) {
        // : refuses cancellation while sailing,
        // otherwise silently sets boat = 0xFF and redraws the menu.
        // No "rental cancelled" popup — openbounty invention removed.
        if (g->travel_mode == TRAVEL_BOAT) {
            resources_format_template(buf, sizeof buf,
                                      bn->town_boat_vacate_first, NULL, 0);
            town_show_info(buf);
            return;
        }
        g->boat.has_boat = false;
        g->boat.x = -1;
        g->boat.y = -1;
        return;
    }
    int cost = GameBoatCost(g);
    // : `if (gold <= boat_cost)` — exact-match also fails.
    if (g->stats.gold <= cost) {
        resources_format_template(buf, sizeof buf, bn->town_no_gold, NULL, 0);
        town_show_info(buf);
        return;
    }
    // : unconditionally writes boat_coords[id] into
    // game->boat_x/y. No dock-validation popup.
    g->stats.gold -= cost;
    g->boat.has_boat = true;
    g->boat.x = town.boat_x;
    g->boat.y = town.boat_y;
    size_t m = 0;
    while (m + 1 < sizeof(g->boat.zone) && g->position.zone[m]) {
        g->boat.zone[m] = g->position.zone[m]; m++;
    }
    g->boat.zone[m] = '\0';
    // has no success popup — menu redraw shows "Cancel boat rental"
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

static void town_do_info(const Game *g) {
    // : this town reports intel
    // on the castle named in its intel_castle field. Display:
    //   "Castle <name> is under
    //    <owner>'s rule.
    //
    //      <number_name> <troop>
    //      ..."
    const ResBanners *bn = &g->res->banners;
    char buf[512];

    const ResTown *rt = g->res
        ? resources_town_by_id(g->res, town.record_key) : NULL;
    if (!rt || !rt->intel_castle[0]) {
        resources_format_template(buf, sizeof buf, bn->town_intel_unavailable,
                                  NULL, 0);
        town_show_info(buf);
        return;
    }
    const ResCastle *rc = g->res
        ? resources_castle_by_id(g->res, rt->intel_castle) : NULL;
    const CastleRecord *cr = GameFindCastleConst(g, rt->intel_castle);
    if (!rc || !cr || rc->special.excluded_from_intel) {
        resources_format_template(buf, sizeof buf, bn->town_intel_unavailable,
                                  NULL, 0);
        town_show_info(buf);
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
    town_show_info(buf);
}

static void town_do_spell(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char buf[128];
    TownRecord *t = NULL;
    for (int i = 0; i < GAME_TOWNS; i++) {
        if (strcmp(g->towns[i].id, town.record_key) == 0) {
            t = &g->towns[i]; break;
        }
    }
    const SpellDef *sp =
        (t && t->spell_for_sale[0]) ? spell_by_id(t->spell_for_sale) : NULL;
    if (!sp) {
        resources_format_template(buf, sizeof buf,
                                  bn->town_spell_unavailable, NULL, 0);
        town_show_info(buf);
        return;
    }
    int known = GameKnownSpells(g);
    if (known >= g->stats.max_spells) {
        resources_format_template(buf, sizeof buf, bn->town_spell_at_cap,
                                  NULL, 0);
        town_show_info(buf);
        return;
    }
    if (g->stats.gold <= sp->cost) {
        resources_format_template(buf, sizeof buf, bn->town_no_gold, NULL, 0);
        town_show_info(buf);
        return;
    }
    g->spells.counts[sp->index]++;
    g->stats.gold -= sp->cost;
    int left = g->stats.max_spells - known - 1;
    char lbuf[16];
    snprintf(lbuf, sizeof lbuf, "%d", left);
    ResTemplateVar vars[] = {
        { "LEFT", lbuf },
        { "S",    (left == 1 ? "" : "s") },
    };
    resources_format_template(buf, sizeof buf, bn->town_spell_can_learn,
                              vars, 2);
    town_show_info(buf);
}

static void town_do_siege(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char buf[96];
    if (g->stats.siege_weapons) {
        resources_format_template(buf, sizeof buf, bn->town_siege_already,
                                  NULL, 0);
        town_show_info(buf);
        return;
    }
    int cost = g->res->economy.siege_cost;
    if (g->stats.gold <= cost) {
        resources_format_template(buf, sizeof buf, bn->town_no_gold, NULL, 0);
        town_show_info(buf);
        return;
    }
    g->stats.gold -= cost;
    g->stats.siege_weapons = 1;
    resources_format_template(buf, sizeof buf, bn->town_siege_purchased,
                              NULL, 0);
    town_show_info(buf);
}

static void town_do_row(Game *g, TownRow r) {
    switch (r) {
        case TOWN_ROW_CONTRACT: town_do_contract(g); break;
        case TOWN_ROW_BOAT:     town_do_boat(g);     break;
        case TOWN_ROW_INFO:     town_do_info(g);     break;
        case TOWN_ROW_SPELL:    town_do_spell(g);    break;
        case TOWN_ROW_SIEGE:    town_do_siege(g);    break;
        default: break;
    }
}

void views_town_invoke_row(Game *g, int row) {
    if (view_stack_top() != VIEW_TOWN) return;
    if (row < 0 || row >= TOWN_ROW_COUNT) return;
    town.cursor = row;
    town_do_row(g, (TownRow)row);
}

bool views_town_update(Game *g) {
    if (view_stack_top() != VIEW_TOWN) return false;

    if (town.info_active) {
        // Any key dismisses the info panel and returns to the menu.
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) ||
            IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
            town.info_active = false;
            return true;
        }
        // Letters A-E also dismiss so the player can chain actions.
        for (int k = KEY_A; k <= KEY_E; k++) {
            if (IsKeyPressed(k)) { town.info_active = false; return true; }
        }
        return true;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        views_dismiss();
        return true;
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_KP_8)) {
        town.cursor = (town.cursor - 1 + TOWN_ROW_COUNT) % TOWN_ROW_COUNT;
        return true;
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsKeyPressed(KEY_KP_2)) {
        town.cursor = (town.cursor + 1) % TOWN_ROW_COUNT;
        return true;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
        IsKeyPressed(KEY_SPACE)) {
        town_do_row(g, (TownRow)town.cursor);
        return true;
    }
    if (IsKeyPressed(KEY_A)) { town.cursor = TOWN_ROW_CONTRACT; town_do_row(g, TOWN_ROW_CONTRACT); return true; }
    if (IsKeyPressed(KEY_B)) { town.cursor = TOWN_ROW_BOAT;     town_do_row(g, TOWN_ROW_BOAT);     return true; }
    if (IsKeyPressed(KEY_C)) { town.cursor = TOWN_ROW_INFO;     town_do_row(g, TOWN_ROW_INFO);     return true; }
    if (IsKeyPressed(KEY_D)) { town.cursor = TOWN_ROW_SPELL;    town_do_row(g, TOWN_ROW_SPELL);    return true; }
    if (IsKeyPressed(KEY_E)) { town.cursor = TOWN_ROW_SIEGE;    town_do_row(g, TOWN_ROW_SIEGE);    return true; }
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

bool views_town_row_text(const Game *g, int row, char *out, int out_sz) {
    if (view_stack_top() != VIEW_TOWN) return false;
    if (row < 0 || row >= TOWN_ROW_COUNT) return false;
    town_format_row(g, (TownRow)row, out, (size_t)out_sz);
    return true;
}

int views_town_row_count(void) {
    return TOWN_ROW_COUNT;
}

const char *views_town_info_text(void) {
    if (view_stack_top() != VIEW_TOWN || !town.info_active) return NULL;
    return town.info_body;
}

int views_town_cursor(void) {
    if (view_stack_top() != VIEW_TOWN) return -1;
    return town.cursor;
}

// ---- Controls settings panel -----------------------------------------------
static int g_controls_cursor = 0;

int views_controls_cursor(void) { return g_controls_cursor; }

void views_controls_set_cursor(int r) {
    if (r < 0) r = 0;
    g_controls_cursor = r;
}

// True iff the row labels Sounds / Music / Volume — the three controls
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
    return controls_row_is_audio(g, row) && !audio_is_available();
}

void views_controls_advance(struct Game *g, int row) {
    if (!g || !g->res) return;
    if (row < 0 || row >= g->res->controls.count) return;
    if (views_controls_row_disabled(g, row)) return; // grayed-out, no-op
    int range = g->res->controls.items[row].range;
    if (range < 2) range = 2;
    g->stats.options[row] = (g->stats.options[row] + 1) % range;
}
