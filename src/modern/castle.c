// src/modern/castle.c -- modern castle screens: state and input (see castle.h).
// Drawn by modern_overlay_draw_castle in src/modern/overlay.c.

#include "modern/castle.h"
#include "modern/mlist.h"
#include "input_host.h"
#include "touch.h"
#include "ui.h"
#include "tables.h"
#include "resources.h"
#include "shell_audience.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>

#define POOL_MAX 8

static struct {
    bool   home;
    char   castle_id[24];
    McPage page;
    int    menu_cursor;
    int    list_cursor;
    // Count stepper.
    bool   step_on;
    int    step_value, step_max;
    int    step_row;
    char   message[RES_BANNER_LEN];
    int    audience;          // GameAudienceOutcome + 1, 0 = none this visit
    int    audience_needed;
    int    audience_rank;
    // economy.audiences: the last Blessing or Tribute this visit.
    McAudience aud_kind;
    int    aud_result;        // 0 none; Blessing: GameBlessingOutcome + 1; Tribute: 1 paid, 2 short
    int    aud_needed;
    GameAudienceGain gain;
    bool   ask_tribute;       // a Yes/No waits to be opened
} mc;

int modern_castle_pool(int *out, int cap) {
    int n = 0, total = troops_count();
    for (int i = 0; i < total && n < cap; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0) out[n++] = i;
    }
    for (int i = 1; i < n; i++) {           // cheapest first
        int key = out[i], j = i - 1;
        int kc = troop_by_index(key)->recruit_cost;
        while (j >= 0 && troop_by_index(out[j])->recruit_cost > kc) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

void modern_castle_open(const Game *g, bool home, const char *castle_id) {
    (void)g;
    memset(&mc, 0, sizeof mc);
    mc.home = home;
    snprintf(mc.castle_id, sizeof mc.castle_id, "%s", castle_id ? castle_id : "");
}

void modern_castle_gallery_audience(int audience, int rank) {
    mc.audience = audience;
    mc.audience_rank = rank;
}

void modern_castle_gallery(McPage page, int cursor, int step_value, int step_max) {
    mc.page = page;
    if (page == MC_MENU) mc.menu_cursor = cursor; else mc.list_cursor = cursor;
    mc.step_on = step_max > 0;
    mc.step_value = step_value;
    mc.step_max = step_max;
    mc.step_row = cursor;
}

bool        modern_castle_is_home(void) { return mc.home; }
const char *modern_castle_id(void)      { return mc.castle_id; }
McPage      modern_castle_page(void)    { return mc.page; }
int modern_castle_cursor(void) { return mc.page == MC_MENU ? mc.menu_cursor : mc.list_cursor; }
int modern_castle_menu_cursor(void) { return mc.menu_cursor; }
const char *modern_castle_message(void) { return mc.message[0] ? mc.message : NULL; }

bool modern_castle_stepper(int *value, int *max) {
    if (!mc.step_on) return false;
    if (value) *value = mc.step_value;
    if (max) *max = mc.step_max;
    return true;
}

static bool audiences(const Game *g) {
    return mc.home && g && g->res && g->res->economy.audiences;
}

void modern_castle_gallery_answer(McAudience kind, int result, GameAudienceGain gain) {
    mc.aud_kind = kind;
    mc.aud_result = result;
    mc.aud_needed = 0;
    mc.gain = gain;
}

McAudience modern_castle_audience_result(int *result, int *needed, GameAudienceGain *gain) {
    if (result) *result = mc.aud_result;
    if (needed) *needed = mc.aud_needed;
    if (gain) *gain = mc.gain;
    return mc.aud_kind;
}

bool modern_castle_take_confirm(const Game *g, char *body, int cap) {
    if (!mc.ask_tribute) return false;
    mc.ask_tribute = false;
    char gold[16];
    snprintf(gold, sizeof gold, "%d", g->res->economy.tribute_cost);
    ResTemplateVar v[] = { { "GOLD", gold } };
    resources_format_template(body, cap, g->res->banners.castle_tribute_confirm, v, 1);
    return true;
}

void modern_castle_confirm_yes(Game *g) {
    mc.aud_kind = MC_AUD_TRIBUTE;
    mc.aud_result = GamePayTribute(g, &mc.aud_needed, &mc.gain) ? 1 : 2;
}

int modern_castle_audience(int *needed, int *rank) {
    if (needed) *needed = mc.audience_needed;
    if (rank) *rank = mc.audience_rank;
    return mc.audience;
}

// The army or garrison stacks a page lists, in slot order: out[k] = slot.
static int stacks(const Game *g, bool garrison, int *out) {
    int n = 0;
    const CastleRecord *cr = GameFindCastleConst(g, mc.castle_id);
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        const char *id = garrison ? (cr ? cr->garrison[i].id : "") : g->army[i].id;
        int count = garrison ? (cr ? cr->garrison[i].count : 0) : g->army[i].count;
        if (id[0] && count > 0) out[n++] = i;
    }
    return n;
}

int modern_castle_rows(const Game *g) {
    int tmp[GAME_ARMY_SLOTS], pool[POOL_MAX];
    switch (mc.page) {
        case MC_MENU:     return 3;   // two sections, Leave
        case MC_RECRUIT:  return modern_castle_pool(pool, POOL_MAX) + 1;
        case MC_AUDIENCE: return audiences(g) ? 4 : 2;
        case MC_GARRISON: return stacks(g, false, tmp) + 1;
        case MC_WITHDRAW: return stacks(g, true, tmp) + 1;
        case MC_PROMOTION: return 1;
    }
    return 1;
}

void modern_castle_row(const Game *g, int i, char *out, int cap,
                       const char **troop_id) {
    const ResBanners *bn = &g->res->banners;
    if (troop_id) *troop_id = NULL;
    out[0] = '\0';
    if (mc.page == MC_PROMOTION) {
        snprintf(out, (size_t)cap, "%s", bn->castle_continue);
        return;
    }
    if (mc.page == MC_MENU) {
        if (i == 2) { snprintf(out, (size_t)cap, "%s", bn->location_leave); return; }
        const char *s = mc.home ? (i == 0 ? bn->castle_menu_recruit : bn->castle_menu_audience)
                                : (i == 0 ? bn->castle_menu_garrison : bn->castle_menu_withdraw);
        snprintf(out, (size_t)cap, "%s >", s);
        return;
    }
    if (i == modern_castle_rows(g) - 1) {
        snprintf(out, (size_t)cap, "%s", bn->town_back);
        return;
    }
    int slots[GAME_ARMY_SLOTS], pool[POOL_MAX];
    const CastleRecord *cr = GameFindCastleConst(g, mc.castle_id);
    const char *id = NULL;
    switch (mc.page) {
        case MC_RECRUIT: {
            modern_castle_pool(pool, POOL_MAX);
            const TroopDef *t = troop_by_index(pool[i]);
            id = t ? t->id : NULL;
            break;
        }
        case MC_AUDIENCE:
            snprintf(out, (size_t)cap, "%s", !audiences(g) ? bn->castle_action_audience
                                             : i == 0 ? bn->castle_action_promotion
                                             : i == 1 ? bn->castle_action_blessing
                                                      : bn->castle_action_tribute);
            return;
        case MC_GARRISON:
            stacks(g, false, slots);
            id = g->army[slots[i]].id;
            break;
        case MC_WITHDRAW:
            stacks(g, true, slots);
            id = cr ? cr->garrison[slots[i]].id : NULL;
            break;
        default: break;
    }
    const TroopDef *t = id ? troop_by_id(id) : NULL;
    snprintf(out, (size_t)cap, "%s", t ? t->name : (id ? id : ""));
    if (troop_id) *troop_id = id;
}

static void set_message(const char *s) {
    snprintf(mc.message, sizeof mc.message, "%s", s ? s : "");
}

// The most of a castle troop the hero can recruit now: what leadership can
// control and what the purse pays for.
bool modern_castle_troop_offered(const Game *g, const TroopDef *t) {
    return g && t && t->hit_points > 0 && g->stats.leadership_current >= t->hit_points * 6;
}

static int recruit_max(const Game *g, const TroopDef *t) {
    int m = GameMaxRecruitable(g, t->id);
    if (m < 0) m = 0;
    if (t->recruit_cost > 0) {
        int afford = g->stats.gold / t->recruit_cost;
        if (afford < m) m = afford;
    }
    return m;
}

static void open_stepper(int row, int max) {
    mc.step_on = true;
    mc.step_row = row;
    mc.step_max = max;
    mc.step_value = max;
}

static void act(Game *g, int i) {
    const ResBanners *bn = &g->res->banners;
    if (mc.page == MC_PROMOTION) {         // Continue: back to the audience
        mc.page = MC_AUDIENCE;
        mc.audience = 0;
        return;
    }
    if (mc.page == MC_MENU) {
        mc.page = mc.home ? (i == 0 ? MC_RECRUIT : MC_AUDIENCE)
                          : (i == 0 ? MC_GARRISON : MC_WITHDRAW);
        mc.list_cursor = 0;
        mc.audience = 0;
        mc.aud_result = 0;
        return;
    }
    if (i == modern_castle_rows(g) - 1) {        // Back
        mc.page = MC_MENU;
        return;
    }
    int slots[GAME_ARMY_SLOTS], pool[POOL_MAX];
    switch (mc.page) {
        case MC_RECRUIT: {
            modern_castle_pool(pool, POOL_MAX);
            const TroopDef *t = troop_by_index(pool[i]);
            if (!t) return;
            if (!modern_castle_troop_offered(g, t)) return;     // greyed: not yet offered
            int max = recruit_max(g, t);
            if (max <= 0) {
                set_message(GameMaxRecruitable(g, t->id) <= 0 ? bn->army_cannot_handle
                                                              : bn->town_no_gold);
                return;
            }
            open_stepper(i, max);
            break;
        }
        case MC_AUDIENCE: {
            mc.aud_result = 0;
            mc.audience = 0;
            if (audiences(g) && i == 1) {
                mc.aud_kind = MC_AUD_BLESSING;
                mc.aud_result = (int)GameSeekBlessing(g, &mc.aud_needed, &mc.gain) + 1;
                break;
            }
            if (audiences(g) && i == 2) {
                // A full purse asks Yes/No first; a short one hears why at once.
                mc.aud_kind = MC_AUD_TRIBUTE;
                if (g->stats.gold >= g->res->economy.tribute_cost) mc.ask_tribute = true;
                else mc.aud_result = GamePayTribute(g, &mc.aud_needed, &mc.gain) ? 1 : 2;
                break;
            }
            mc.aud_kind = MC_AUD_PROMOTION;
            // Always granted: a promotion when one is due, else the Emperor's word.
            int needed = 0;
            GameAudienceOutcome o = GameAudienceWithKing(g, &needed);
            mc.audience = (int)o + 1;
            mc.audience_needed = needed;
            mc.audience_rank = g->character.cls.rank_index;
            if (o == GAME_AUDIENCE_PROMOTED) mc.page = MC_PROMOTION;   // the award
            break;
        }
        case MC_GARRISON:
            stacks(g, false, slots);
            open_stepper(i, g->army[slots[i]].count);
            break;
        case MC_WITHDRAW: {
            stacks(g, true, slots);
            const CastleRecord *cr = GameFindCastleConst(g, mc.castle_id);
            if (cr) open_stepper(i, cr->garrison[slots[i]].count);
            break;
        }
        default: break;
    }
}

static void commit(Game *g) {
    const ResBanners *bn = &g->res->banners;
    int slots[GAME_ARMY_SLOTS], pool[POOL_MAX];
    int n = mc.step_value, i = mc.step_row, rc;
    mc.step_on = false;
    switch (mc.page) {
        case MC_RECRUIT: {
            modern_castle_pool(pool, POOL_MAX);
            const TroopDef *t = troop_by_index(pool[i]);
            if (!t) return;
            rc = GameBuyTroop(g, t->id, n);
            if (rc == 1)      set_message(bn->town_no_gold);
            else if (rc == 2) set_message(bn->no_troop_slots);
            else if (rc == 3) set_message(bn->army_cannot_handle);
            break;
        }
        case MC_GARRISON:
            stacks(g, false, slots);
            rc = GameGarrisonTroopCount(g, mc.castle_id, slots[i], n);
            if (rc == 2)      set_message(bn->cannot_garrison_last);
            else if (rc == 1) set_message(bn->no_troop_slots);
            break;
        case MC_WITHDRAW:
            stacks(g, true, slots);
            rc = GameUngarrisonTroopCount(g, mc.castle_id, slots[i], n);
            if (rc == 1) set_message(bn->no_troop_slots);
            break;
        default: break;
    }
    // A row the move emptied is gone; keep the cursor on the list.
    int rows = modern_castle_rows(g);
    if (mc.list_cursor >= rows) mc.list_cursor = rows - 1;
}

static bool pressed_confirm(void) {
    return input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
           input_key_pressed(KEY_SPACE);
}

bool modern_castle_update(Game *g) {
    touch_request(TOUCH_CHROME_BACK);
    // A message or the Emperor's answer shows in its own in-lay: any key or a
    // tap on Continue puts it away, and does nothing else.
    if (mc.message[0] || (mc.page == MC_AUDIENCE && (mc.aud_result || mc.audience))) {
        if (ui_any_key_pressed() || touch_tapped_row(TOUCH_LIST_PROMPT) == 0) {
            mc.message[0] = '\0';
            mc.aud_result = 0;
            mc.audience = 0;
        }
        return false;
    }

    // The count stepper holds the keys while it is open: Left/Right step by
    // one, Down/Up by ten, Enter moves the count, Esc puts it away.
    if (mc.step_on) {
        ml_stepper_keys(&mc.step_value, 1, mc.step_max);
        int tapped = touch_tapped_row(TOUCH_LIST_CASTLE);    // "Recruit 20" / Cancel
        if (input_key_pressed(KEY_ESCAPE) || tapped == 1) mc.step_on = false;
        else if (pressed_confirm() || tapped == 0)         commit(g);
        return false;
    }

    int rows = modern_castle_rows(g);
    int *cur = (mc.page == MC_MENU) ? &mc.menu_cursor : &mc.list_cursor;
    if (*cur >= rows) *cur = rows - 1;

    if (input_key_pressed(KEY_ESCAPE)) {
        if (mc.page == MC_MENU) return true;
        mc.page = (mc.page == MC_PROMOTION) ? MC_AUDIENCE : MC_MENU;
        return false;
    }
    int tapped = touch_tapped_row(TOUCH_LIST_CASTLE);
    if (mc.page == MC_MENU && tapped == 2) return true;                      // Leave
    if (mc.page == MC_MENU && *cur == 2 && pressed_confirm()) return true;
    if (tapped >= 0 && tapped < rows) {
        *cur = tapped;
        act(g, tapped);
        return false;
    }
    if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_W) || input_key_pressed(KEY_KP_8)) {
        *cur = (*cur - 1 + rows) % rows;
        return false;
    }
    if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_S) || input_key_pressed(KEY_KP_2)) {
        *cur = (*cur + 1) % rows;
        return false;
    }
    if (pressed_confirm()) act(g, *cur);
    return false;
}
