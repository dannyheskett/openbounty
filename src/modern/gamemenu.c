// src/modern/gamemenu.c -- the modern drill-down menus (see gamemenu.h).

#include "modern/gamemenu.h"
#include "modern/mlayout.h"
#include "modern/uikit.h"
#include "modern/page.h"
#include "modern/saveslots.h"
#include "shell_cheats.h"
#include "input_host.h"
#include "lattice.h"
#include "palette.h"
#include "resources.h"
#include "touch.h"
#include "views.h"
#include "prompt_impl.h"
#include "bfont.h"
#include <stdio.h>
#include <string.h>

// ---- a page ------------------------------------------------------------------------

GmEvent gm_page_input(const GmPage *p, int *cursor, int touch_list) {
    if (p->n <= 0) return GM_EV_BACK;
    bool enabled[GM_ROWS_MAX];
    const char *keys[GM_ROWS_MAX];
    for (int i = 0; i < p->n; i++) {
        enabled[i] = p->item[i].enabled;
        keys[i] = p->item[i].shortcut;
    }
    MlList l = { p->n, *cursor, enabled, keys };
    MlEvent ev = ml_list_input(&l, touch_list, NULL);
    *cursor = l.cursor;
    return ev == ML_EV_ACT ? GM_EV_ACT : ev == ML_EV_BACK ? GM_EV_BACK : GM_EV_NONE;
}

int gm_first_enabled(const GmPage *p) {
    for (int i = 0; i < p->n; i++) if (p->item[i].enabled) return i;
    return 0;
}

bool gm_row(void *ctx, int i, char *label, char *right, int cap) {
    const GmPage *p = (const GmPage *)ctx;
    const GmItem *it = &p->item[i];
    bool sub = it->key >= GM_ACT_PAGE && it->key < GM_ACT_USER;
    snprintf(label, (size_t)cap, "%s%s", it->label ? it->label : "", sub ? " >" : "");
    if (it->key == GM_ACT_BACK) ml_exit_hint(right);
    else snprintf(right, 48, "%s", (ml_keys_shown() && it->shortcut) ? it->shortcut : "");
    return it->enabled;
}

// ---- the game menu ------------------------------------------------------------------

enum { ACT_CONTROLS = GM_ACT_USER, ACT_NEW, ACT_EXIT,
       ACT_SLOT = GM_ACT_USER + 100,      // + slot index
       ACT_CHEAT = GM_ACT_USER + 200 };   // + cheat index

typedef enum { ASK_NONE = 0, ASK_OVERWRITE, ASK_LOAD, ASK_EXIT, ASK_NEW } GmAsk;

static struct {
    GmPageId page[GM_DEPTH_MAX];
    int      cursor[GM_DEPTH_MAX];
    int      depth;
    bool     debug;
    SlotSet  slots;
    GmAsk    ask, asked;
    int      ask_slot;
    GmDo     act;
    int      act_slot;
    int      cheat;
} gm = { .cheat = -1 };

void modern_gamemenu_open_save(void) {
    // Straight to the save slots; Back closes the menu.
    gm.page[0] = GM_PAGE_SAVE;
    gm.cursor[0] = 0;
    gm.depth = 1;
}

void modern_gamemenu_open(bool debug) {
    memset(gm.page, 0, sizeof gm.page);
    memset(gm.cursor, 0, sizeof gm.cursor);
    gm.depth = 1;
    gm.debug = debug;
    gm.ask = gm.asked = ASK_NONE;
    gm.act = GM_DO_NONE;
    saveslots_scan(&gm.slots);
}

// A page opens with its cursor on its first row that can be chosen.
static int open_cursor(const Game *g, GmPageId id) {
    GmPage p;
    modern_gamemenu_page(g, id, &p);
    return gm_first_enabled(&p);
}

static void add(GmPage *p, const char *label, const char *desc, const char *reason,
                const char *shortcut, int key, bool enabled) {
    if (p->n >= GM_ROWS_MAX) return;
    p->item[p->n++] = (GmItem){ label, enabled ? desc : reason, shortcut, key, enabled };
}

void modern_gamemenu_page(const Game *g, GmPageId id, GmPage *p) {
    const ResUI *ui = &g->res->ui;
    const ResBanners *bn = &g->res->banners;
    memset(p, 0, sizeof *p);
    switch (id) {
    case GM_PAGE_ROOT:
        p->title = ui->gm_title;
        add(p, ui->gm_hero,  bn->gmd_hero,  NULL, "", GM_ACT_PAGE + GM_PAGE_HERO, true);
        add(p, ui->gm_world, bn->gmd_world, NULL, "", GM_ACT_PAGE + GM_PAGE_WORLD, true);
        add(p, ui->gm_game,  bn->gmd_game,  NULL, "", GM_ACT_PAGE + GM_PAGE_GAME, true);
        add(p, ui->gm_close, bn->gmd_back,  NULL, "", GM_ACT_BACK, true);
        p->foot = 1;
        // Exit only here, last on the foot -- and not at all on a phone,
        // where quitting an app is not the player's job.
#if !defined(PLATFORM_IOS) && !defined(PLATFORM_ANDROID)
        add(p, ui->gm_exit,  bn->gmd_exit,  NULL, "", ACT_EXIT, true);
        p->foot = 2;
#endif
        break;
    case GM_PAGE_HERO: {
        bool troops = false;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (g->army[i].id[0] && g->army[i].count > 0) troops = true;
        p->title = ui->gm_hero;
        add(p, ui->gm_army,      bn->gmd_army,      NULL, "A", KEY_A, true);
        add(p, ui->gm_character, bn->gmd_character, NULL, "V", KEY_V, true);
        add(p, ui->gm_contract,  bn->gmd_contract,  NULL, "I", KEY_I, true);
        add(p, ui->gm_puzzle,    bn->gmd_puzzle,    NULL, "P", KEY_P, true);
        add(p, ui->gm_dismiss,   bn->gmd_dismiss,   bn->gmr_no_troops, "D", KEY_D, troops);
        add(p, ui->gm_back,      bn->gmd_back_up,   NULL, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    }
    case GM_PAGE_WORLD: {
        bool flying = g->character.mount == MOUNT_FLY;
        bool sailing = g->character.mount == MOUNT_SAIL;
        p->title = ui->gm_world;
        add(p, ui->gm_map,    bn->gmd_map,    NULL, "M", KEY_M, true);
        add(p, ui->gm_cast,   bn->gmd_cast,   NULL, "U", KEY_U, true);
        add(p, ui->gm_search, bn->gmd_search, NULL, "S", KEY_S, true);
        if (flying) add(p, ui->gm_land, bn->gmd_land, NULL, "L", KEY_L, true);
        else        add(p, ui->gm_fly,  bn->gmd_fly,  NULL, "F", KEY_F, true);
        add(p, ui->gm_end_week, bn->gmd_end_week, NULL, "W", KEY_W, true);
        add(p, ui->gm_rest,     bn->gmd_rest,     NULL, "5", KEY_KP_5, true);
        add(p, ui->gm_sail,     bn->gmd_sail,     bn->gmr_not_sailing, "N", KEY_N, sailing);
        add(p, ui->gm_back,     bn->gmd_back_up,  NULL, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    }
    case GM_PAGE_GAME:
        p->title = ui->gm_game;
        if (gm.debug) add(p, ui->gm_debug, bn->gmd_debug, NULL, "", GM_ACT_PAGE + GM_PAGE_DEBUG, true);
        add(p, ui->gm_save,     bn->gmd_save,     NULL, "", GM_ACT_PAGE + GM_PAGE_SAVE, true);
        add(p, ui->gm_load,     bn->gmd_load,     bn->gmr_no_saves, "", GM_ACT_PAGE + GM_PAGE_LOAD,
            gm.slots.existing > 0);
        add(p, ui->gm_controls, bn->gmd_controls, NULL, "C", ACT_CONTROLS, true);
        add(p, ui->gm_new_game, bn->gmd_new_game, NULL, "", ACT_NEW, true);
        add(p, ui->gm_back,     bn->gmd_back_up,  NULL, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    case GM_PAGE_SAVE:
        p->title = ui->gm_save;
        for (int i = 0; i < MODERN_SAVE_SLOTS; i++)
            add(p, "", bn->gmd_save, NULL, "", ACT_SLOT + i, true);
        add(p, ui->gm_back, bn->gmd_back_up, NULL, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    case GM_PAGE_LOAD:
        gm_load_page(p, &gm.slots, ui->gm_load);
        break;
    case GM_PAGE_DEBUG:
        p->title = ui->gm_debug;
        for (int i = 0; i < CHEAT_COUNT; i++)
            add(p, cheat_label((CheatAction)i), cheat_desc((CheatAction)i), NULL, "", ACT_CHEAT + i, true);
        add(p, ui->gm_back, bn->gmd_back_up, NULL, "", GM_ACT_BACK, true);
        p->foot = 1;
        break;
    }
}

void gm_load_page(GmPage *p, const SlotSet *slots, const char *title) {
    const Resources *r = resources_current();
    memset(p, 0, sizeof *p);
    if (!r) return;
    const ResUI *ui = &r->ui;
    const ResBanners *bn = &r->banners;
    p->title = title;
    for (int i = 0; i < MODERN_SAVE_SLOTS; i++)
        add(p, "", bn->gmd_load, bn->gmr_empty_slot, "", ACT_SLOT + i, slots->hdrs[i].exists);
    add(p, ui->gm_back, bn->gmd_back_up, NULL, "", GM_ACT_BACK, true);
    p->foot = 1;
}

void modern_gamemenu_gallery(int n, const GmPageId *pages, int cursor) {
    gm.depth = 0;
    for (int i = 0; i < n && i < GM_DEPTH_MAX; i++) { gm.page[i] = pages[i]; gm.cursor[i] = 0; gm.depth++; }
    if (gm.depth < 1) { gm.page[0] = GM_PAGE_ROOT; gm.depth = 1; }
    gm.cursor[gm.depth - 1] = cursor;
}

static void go_back(void) {
    if (gm.depth > 1) gm.depth--;
    else              views_dismiss();
}

void modern_gamemenu_update(Game *g) {
    if (!g || !g->res || gm.depth < 1) return;
    int d = gm.depth - 1;
    GmPage p;
    modern_gamemenu_page(g, gm.page[d], &p);
    GmEvent ev = gm_page_input(&p, &gm.cursor[d], TOUCH_LIST_MENU);
    if (ev == GM_EV_BACK) { go_back(); return; }
    if (ev != GM_EV_ACT) return;
    int key = p.item[gm.cursor[d]].key;
    if (key == GM_ACT_BACK) {
        go_back();
    } else if (key >= GM_ACT_PAGE && key < GM_ACT_USER) {
        if (gm.depth < GM_DEPTH_MAX) {
            gm.page[gm.depth] = (GmPageId)(key - GM_ACT_PAGE);
            gm.cursor[gm.depth] = open_cursor(g, gm.page[gm.depth]);
            gm.depth++;
        }
    } else if (key >= ACT_CHEAT) {
        gm.cheat = key - ACT_CHEAT;
        views_dismiss();
    } else if (key >= ACT_SLOT) {
        int slot = key - ACT_SLOT;
        if (gm.page[d] == GM_PAGE_LOAD)      { gm.ask = ASK_LOAD; gm.ask_slot = slot; }
        else if (gm.slots.hdrs[slot].exists) { gm.ask = ASK_OVERWRITE; gm.ask_slot = slot; }
        else                                 { gm.act = GM_DO_SAVE; gm.act_slot = slot; }
    } else if (key == ACT_CONTROLS) {
        views_push(VIEW_CONTROLS);           // over the menu: Back comes back here
        views_controls_set_cursor(0);
    } else if (key == ACT_NEW) {
        gm.ask = ASK_NEW;                    // asked in this page, like every question here
    } else if (key == ACT_EXIT) {
        gm.ask = ASK_EXIT;
    } else {
        // A Hero or World row: close, then press its key next frame -- the
        // exact path the keypress runs (src/shell_actions.c).
        views_dismiss();
        input_host_inject_key_next_frame(key);
    }
}

bool modern_gamemenu_take_confirm(const Game *g, char *body, int cap) {
    if (gm.ask == ASK_NONE) return false;
    const ResBanners *bn = &g->res->banners;
    char sb[16];
    snprintf(sb, sizeof sb, "%d", gm.ask_slot + 1);
    ResTemplateVar v[] = { { "SLOT", sb } };
    resources_format_template(body, cap, gm.ask == ASK_OVERWRITE ? bn->gmc_overwrite
                                         : gm.ask == ASK_LOAD ? bn->gmc_load
                                         : gm.ask == ASK_NEW ? g->res->ui.new_game_confirm : bn->gmc_exit, v, 1);
    gm.asked = gm.ask;
    gm.ask = ASK_NONE;
    return true;
}

void modern_gamemenu_confirm_yes(void) {
    gm.act = gm.asked == ASK_OVERWRITE ? GM_DO_SAVE : gm.asked == ASK_LOAD ? GM_DO_LOAD
           : gm.asked == ASK_EXIT ? GM_DO_EXIT : gm.asked == ASK_NEW ? GM_DO_NEW : GM_DO_NONE;
    gm.act_slot = gm.ask_slot;
    gm.asked = ASK_NONE;
}

GmDo modern_gamemenu_take_action(int *slot) {
    GmDo d = gm.act;
    if (slot) *slot = gm.act_slot;
    gm.act = GM_DO_NONE;
    if (d == GM_DO_SAVE) saveslots_scan(&gm.slots);
    return d;
}

int modern_gamemenu_take_cheat(void) {
    int c = gm.cheat;
    gm.cheat = -1;
    return c;
}

// Slot pages: the rows carry the save headers ("1  Name  Rank  600d").
static bool slot_row(void *ctx, int i, char *label, char *right, int cap) {
    const GmPage *p = (const GmPage *)ctx;
    if (i >= MODERN_SAVE_SLOTS) return gm_row(ctx, i, label, right, cap);
    saveslots_row(&gm.slots, i, label, right, cap);
    return p->item[i].enabled;
}

// The menu's questions: Yes, and No -- the row Escape presses.
static bool yes_no_row(void *ctx, int i, char *label, char *right, int cap) {
    const Resources *res = (const Resources *)ctx;
    snprintf(label, (size_t)cap, "%s", i == 0 ? res->ui.prompt_yes : res->ui.prompt_no);
    if (i == 1) ml_exit_hint(right);
    else snprintf(right, 48, "%s", ml_keys_shown() ? "Y" : "");
    return true;
}

void modern_gamemenu_path(const Game *g, char *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (!g || !g->res) return;
    for (int i = 0; i < gm.depth; i++) {
        GmPage pi;
        modern_gamemenu_page(g, gm.page[i], &pi);
        size_t n = strlen(out);
        snprintf(out + n, (size_t)cap - n, "%s%s", i ? " > " : "", pi.title ? pi.title : "");
    }
}

void modern_gamemenu_draw(const Game *g) {
    if (!g || !g->res || gm.depth < 1) return;
    int d = gm.depth - 1;
    GmPage p;
    modern_gamemenu_page(g, gm.page[d], &p);
    // The path: "Menu > Game > Save".
    char path[128];
    modern_gamemenu_path(g, path, (int)sizeof path);
    bool slots = gm.page[d] == GM_PAGE_SAVE || gm.page[d] == GM_PAGE_LOAD;
    int cursor = gm.cursor[d] < p.n ? gm.cursor[d] : p.n - 1;
    const char *hero = g->character.name;
    // A question asked from a page (save over a slot, load, new game, leave)
    // takes that page's place: the same frame, the question where the
    // description was, Yes and No where the rows were. The shared prompt reads
    // the answer; overlay.c does not draw it over the menu.
    const PromptView *pv = prompt_view();
    if (prompt_is_active() && pv && pv->kind == PK_YES_NO) {
        GmPage q;
        memset(&q, 0, sizeof q);
        q.title = p.title;
        q.n = 2;
        q.foot = 2;
        q.item[0] = (GmItem){ g->res->ui.prompt_yes, pv->body, "Y", 0, true };
        q.item[1] = (GmItem){ g->res->ui.prompt_no, pv->body, "", GM_ACT_BACK, true };
        page_menu(&q, path, hero, pv->yn_cursor, TOUCH_LIST_PROMPT, yes_no_row, (void *)g->res);
        return;
    }
    page_menu(&p, path, hero, cursor, TOUCH_LIST_MENU, slots ? slot_row : NULL, &p);
}
