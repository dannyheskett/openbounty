// src/modern/gamemenu.c -- the modern drill-down menus (see gamemenu.h).

#include "modern/gamemenu.h"
#include "modern/mlayout.h"
#include "modern/saveslots.h"
#include "shell_cheats.h"
#include "input_host.h"
#include "lattice.h"
#include "palette.h"
#include "resources.h"
#include "touch.h"
#include "views.h"
#include "bfont.h"
#include <stdio.h>
#include <string.h>

// ---- a page ------------------------------------------------------------------------

GmEvent gm_page_input(const GmPage *p, int *cursor, int touch_list) {
    if (p->n <= 0) return GM_EV_BACK;
    if (*cursor >= p->n) *cursor = p->n - 1;
    if (*cursor < 0) *cursor = 0;
    touch_request(TOUCH_CHROME_BACK);
    int tapped = touch_tapped_row(touch_list);
    if (tapped >= 0 && tapped < p->n) {
        *cursor = tapped;
        return p->item[tapped].enabled ? GM_EV_ACT : GM_EV_NONE;
    }
    if (input_key_pressed(KEY_ESCAPE)) return GM_EV_BACK;
    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8))
        *cursor = (*cursor - 1 + p->n) % p->n;
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2))
        *cursor = (*cursor + 1) % p->n;
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) || input_key_pressed(KEY_SPACE))
        return p->item[*cursor].enabled ? GM_EV_ACT : GM_EV_NONE;
    return GM_EV_NONE;
}

static bool page_row(void *ctx, int i, char *label, char *right, int cap) {
    const GmPage *p = (const GmPage *)ctx;
    const GmItem *it = &p->item[i];
    bool sub = it->key >= GM_ACT_PAGE && it->key < GM_ACT_USER;
    snprintf(label, (size_t)cap, "%s%s", it->label ? it->label : "", sub ? " >" : "");
    snprintf(right, 48, "%s", (input_has_keyboard() && it->shortcut) ? it->shortcut : "");
    return it->enabled;
}

void gm_draw_page(const GmPage *p, const char *path, const char *right_title,
                  int x, int y, int w, int h, int list_w, int cursor, int touch_list,
                  MlRowFn row_fn, void *row_ctx) {
    const int pad = ML_PAD, BAND = 4, GH = BFONT_GLYPH_H;
    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    int title_h = GH + 14;
    bfont_draw(path ? path : "", x + pad, y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    if (right_title && right_title[0])
        bfont_draw(right_title, x + w - pad - (int)bfont_measure(right_title).x,
                   y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    lattice_band_h(x, y + title_h, w, BAND);
    int top = y + title_h + BAND, bottom = y + h;

    ml_list_draw(x, top, list_w, bottom - top, p->n, cursor,
                 row_fn ? row_fn : page_row, row_fn ? row_ctx : (void *)p,
                 touch_list, PAL_CLR(DBLUE));
    lattice_band_v(x + list_w, top, BAND, bottom - top);

    // The description of the row under the cursor, wrapped beside the rows;
    // yellow when it says why the row is greyed.
    const int INSET = pad + 4;
    int dx = x + list_w + BAND + INSET, dw = x + w - dx - INSET;
    const char *d = (cursor >= 0 && cursor < p->n) ? p->item[cursor].desc : NULL;
    Color fg = (cursor >= 0 && cursor < p->n && !p->item[cursor].enabled) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
    char line[160];
    int ty = top + INSET;
    while (d && *d && ty + GH <= bottom - INSET) {
        if (bfont_take_line(&d, dw, line, (int)sizeof line) <= 0) break;
        bfont_draw(line, dx, ty, fg);
        ty += GH + 2;
    }
}

// ---- the game menu ------------------------------------------------------------------

enum { ACT_CONTROLS = GM_ACT_USER, ACT_NEW, ACT_EXIT,
       ACT_SLOT = GM_ACT_USER + 100,      // + slot index
       ACT_CHEAT = GM_ACT_USER + 200 };   // + cheat index

typedef enum { ASK_NONE = 0, ASK_OVERWRITE, ASK_LOAD, ASK_EXIT } GmAsk;

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

void modern_gamemenu_open(bool debug) {
    memset(gm.page, 0, sizeof gm.page);
    memset(gm.cursor, 0, sizeof gm.cursor);
    gm.depth = 1;
    gm.debug = debug;
    gm.ask = gm.asked = ASK_NONE;
    gm.act = GM_DO_NONE;
    saveslots_scan(&gm.slots);
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
        add(p, ui->gm_back,  bn->gmd_back,  NULL, "", GM_ACT_BACK, true);
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
        add(p, ui->gm_exit,     bn->gmd_exit,     NULL, "", ACT_EXIT, true);   // always last
        break;
    case GM_PAGE_SAVE:
    case GM_PAGE_LOAD:
        p->title = id == GM_PAGE_SAVE ? ui->gm_save : ui->gm_load;
        for (int i = 0; i < SAVE_SLOT_COUNT; i++)
            add(p, "", id == GM_PAGE_SAVE ? bn->gmd_save : bn->gmd_load, bn->gmr_empty_slot, "",
                ACT_SLOT + i, id == GM_PAGE_SAVE || gm.slots.hdrs[i].exists);
        add(p, ui->gm_back, bn->gmd_back_up, NULL, "", GM_ACT_BACK, true);
        break;
    case GM_PAGE_DEBUG:
        p->title = ui->gm_debug;
        for (int i = 0; i < CHEAT_COUNT; i++)
            add(p, cheat_label((CheatAction)i), cheat_desc((CheatAction)i), NULL, "", ACT_CHEAT + i, true);
        add(p, ui->gm_back, bn->gmd_back_up, NULL, "", GM_ACT_BACK, true);
        break;
    }
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
            gm.cursor[gm.depth] = 0;
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
        views_push(VIEW_CONTROLS);           // over the menu: closing it comes back here
    } else if (key == ACT_NEW) {
        gm.act = GM_DO_NEW;
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
                                         : gm.ask == ASK_LOAD ? bn->gmc_load : bn->gmc_exit, v, 1);
    gm.asked = gm.ask;
    gm.ask = ASK_NONE;
    return true;
}

void modern_gamemenu_confirm_yes(void) {
    gm.act = gm.asked == ASK_OVERWRITE ? GM_DO_SAVE : gm.asked == ASK_LOAD ? GM_DO_LOAD
           : gm.asked == ASK_EXIT ? GM_DO_EXIT : GM_DO_NONE;
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
    if (i >= SAVE_SLOT_COUNT) return page_row(ctx, i, label, right, cap);
    saveslots_row(&gm.slots, i, label, right, cap);
    return p->item[i].enabled;
}

void modern_gamemenu_draw(const Game *g) {
    if (!g || !g->res || gm.depth < 1) return;
    int d = gm.depth - 1;
    GmPage p;
    modern_gamemenu_page(g, gm.page[d], &p);
    // The path: "Menu > Game > Save".
    char path[128] = "";
    for (int i = 0; i < gm.depth; i++) {
        GmPage pi;
        modern_gamemenu_page(g, gm.page[i], &pi);
        size_t n = strlen(path);
        snprintf(path + n, sizeof path - n, "%s%s", i ? " > " : "", pi.title ? pi.title : "");
    }
    const ResZone *z = resources_zone_by_id(g->res, g->position.zone);
    ML_Rect r = ml_full();
    bool slots = gm.page[d] == GM_PAGE_SAVE || gm.page[d] == GM_PAGE_LOAD;
    // The rows column fits the page's longest label (and its shortcut): at
    // least 16 characters, at most half the screen; slot rows take two thirds.
    int list_w = 16 * BFONT_GLYPH_W;
    for (int i = 0; i < p.n && !slots; i++) {
        char label[96], right[48];
        page_row(&p, i, label, right, (int)sizeof label);
        int need = bfont_text_width(label) + 2 * ML_PAD
                 + (right[0] ? bfont_text_width(right) + 2 * ML_PAD : 0);
        if (need > list_w) list_w = need;
    }
    if (slots) list_w = r.w * 2 / 3;
    else if (list_w > r.w / 2) list_w = r.w / 2;
    int cursor = gm.cursor[d] < p.n ? gm.cursor[d] : p.n - 1;
    gm_draw_page(&p, path, z ? z->name : "", r.x, r.y, r.w, r.h, list_w, cursor, TOUCH_LIST_MENU,
                 slots ? slot_row : NULL, &p);
}
