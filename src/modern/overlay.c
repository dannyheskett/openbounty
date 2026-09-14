// src/modern/overlay.c
//
// The overlay for a pack that declared render.mode "modern": square tiles, a
// TrueType face, one panel rect (REQ-430h), the inverted cursor row (REQ-430e)
// and the dimmed scene beneath (REQ-430g). Modern UI work happens here.
//
// The DOS original's overlay is in src/legacy/overlay.c and is frozen: it must
// not be edited to serve anything in this file.
//
// Called only through the dispatcher in src/overlay.c.

#include "overlay.h"
#include "overlay_impl.h"
#include "modern/mlayout.h"
#include "touch.h"
#include "select.h"
#include "layout.h"
#include "palette.h"
#include "views.h"
#include "bfont.h"
#include "ui.h"
#include "resources.h"
#include "lattice.h"
#include "hud.h"
#include "prompt.h"
#include "prompt_impl.h"
#include "pending.h"
#include "modern/castle.h"
#include "modern/mlist.h"
#include "shell_audience.h"
#include "screens/dwelling.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

// Width of a glyph + 1px spacing between glyphs . Bitmap font is
// fixed-pitch 8x8 with no extra spacing so chars-per-line at width W is
// W / BFONT_GLYPH_W.
#define GW  BFONT_GLYPH_W
#define GH  BFONT_GLYPH_H


// ---------------------------------------------------------------------------
// Bottom message frame: a solid black strip covering the map viewport from
// the status bar downward, plus a 1px yellow frame. This rendering reserves the
// entire bottom portion of the screen for long messages (KB_BottomBox); we
// draw into the map viewport because that's the biggest available area.
// ---------------------------------------------------------------------------

static void draw_panel(int x, int y, int w, int h, Color bg) {
    DrawRectangle(x, y, w, h, bg);
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));
}

// Pages the current dialog body wraps to in the bottom panel. Counts WRAPPED
// lines the same way draw_dialog_ex renders them -- same bfont_take_line, same
// CL_PANEL_COLS width, same rows-per-page -- so the pager (dialog_advance)
// and the display never disagree. The old pager counted raw newlines, so a
// long word-wrapped paragraph with few newlines was scored as one page and
// its overflow was unreachable.
// Wrapped line count of `text` at `max_w`, as bfont_take_line breaks it.
static int wrapped_lines(const char *text, int max_w) {
    int n = 0;
    const char *p = text ? text : "";
    char line[160];
    while (*p) {
        if (bfont_take_line(&p, max_w, line, (int)sizeof line) <= 0) break;
        n++;
    }
    return n;
}

// Which layout a message uses and how it pages. One function, called by both
// the pager and the panel, so the two can never disagree about how many pages
// a message has. A message goes in the small band when its header and whole
// body fit there; otherwise in the large rect, paging if even that is short.
typedef struct {
    ML_Rect r;
    int     header_lines;
    int     body_per_page;
    int     pages;
} DialogFit;

static DialogFit dialog_fit(bool force_large) {
    const char *hdr = dialog_header_text();
    const char *body = dialog_body_text();
    DialogFit f;
    for (int pass = force_large ? 1 : 0; pass < 2; pass++) {
        f.r = pass ? ml_large() : ml_small();
        int max_w = f.r.w - 2 * ML_PAD;
        int cap = ml_lines(f.r);
        f.header_lines = (hdr && hdr[0]) ? wrapped_lines(hdr, max_w) : 0;
        int body_lines = wrapped_lines(body, max_w);
        f.body_per_page = cap - f.header_lines;
        if (f.body_per_page < 1) f.body_per_page = 1;
        f.pages = (body_lines + f.body_per_page - 1) / f.body_per_page;
        if (f.pages < 1) f.pages = 1;
        if (pass == 0 && f.pages == 1) return f;   // it fits the small band
    }
    return f;
}

int modern_overlay_dialog_page_count(void) {
    return dialog_fit(false).pages;
}

// ---------------------------------------------------------------------------
// Dialog (bottom message framestyle).
// ---------------------------------------------------------------------------


// Dialog draw modes:
//   BOTTOM        -- bottom box (CL_PANEL_*, 30 cols x ~10 rows).
//                   Sits over the bottom frame so adventure-mode sidebar
//                   stays visible.
//   CENTERED_MODAL -- victory layout: a 36-col x 16-row
//                   border, centered on screen (RECT_Text(16,36) +
//                   RECT_Center). Used for the victory banner so the
//                   dialog floats over the still-rendered battlefield
//                   instead of replacing the bottom frame.
typedef enum {
    DLG_MODE_BOTTOM = 0,
    DLG_MODE_CENTERED_MODAL,
} DialogMode;

static void draw_dialog_ex(DialogMode mode);

void modern_overlay_draw_dialog(void)          { draw_dialog_ex(DLG_MODE_BOTTOM); }
void modern_overlay_draw_dialog_centered(void) { draw_dialog_ex(DLG_MODE_CENTERED_MODAL); }

static void draw_dialog_ex(DialogMode mode) {
    const char *hdr = dialog_header_text();
    const char *body = dialog_body_text();
    // Victory always takes the large rect; everything else picks by size.
    DialogFit f = dialog_fit(mode == DLG_MODE_CENTERED_MODAL);
    ML_Rect r = f.r;

    draw_panel(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));

    int tx = r.x + ML_PAD;
    int ty = r.y + ML_PAD;
    int max_w = r.w - 2 * ML_PAD;
    char line[160];

    // The header is prose and wraps like the body -- the audience passes the
    // Emperor's words as the header. Centred in the victory dialog.
    const char *hp = hdr ? hdr : "";
    for (int i = 0; i < f.header_lines && *hp; i++) {
        if (bfont_take_line(&hp, max_w, line, (int)sizeof line) <= 0) break;
        if (mode == DLG_MODE_CENTERED_MODAL)
            bfont_draw_centered(line, r.x + r.w / 2, ty, PAL_CLR(YELLOW));
        else
            bfont_draw(line, tx, ty, PAL_CLR(YELLOW));
        ty += GH;
    }

    // Body: skip the pages already read, then draw one page.
    const char *p = body ? body : "";
    int skip = dialog_page_current() * f.body_per_page;
    for (int i = 0; i < skip && *p; i++)
        if (bfont_take_line(&p, max_w, line, (int)sizeof line) <= 0) break;
    for (int i = 0; i < f.body_per_page && *p; i++) {
        if (bfont_take_line(&p, max_w, line, (int)sizeof line) <= 0) break;
        bfont_draw(line, tx, ty, PAL_CLR(WHITE));
        ty += GH;
    }

    // The save message offers its two ways on: Quit and Continue.
    const Resources *res = resources_current();
    if (res && body && strcmp(body, res->ui.save_confirm_modern) == 0) {
        const ResUI *ui = &res->ui;
        int by = r.y + r.h - ML_PAD - (GH + 8);
        int cw = ml_hint_width(ui->hint_continue, NULL, NULL);
        int qw = ml_hint_width(ui->hint_quit, ui->key_ctrl_q, NULL);
        int bx = r.x + r.w - ML_PAD - cw;
        ml_hint_button(bx, by, ui->hint_continue, NULL, NULL, KEY_ENTER);
        ml_hint_button(bx - ML_PAD - qw, by, ui->hint_quit, ui->key_ctrl_q, NULL, KEY_Q);
    }
}

// ---------------------------------------------------------------------------
// Game menu (nested, cursor-driven).
// ---------------------------------------------------------------------------

static bool menu_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)ctx;
    right[0] = '\0';
    const char *l = views_menu_entry_label(i);
    if (views_menu_entry_is_submenu(i)) snprintf(label, (size_t)cap, "%s >", l ? l : "");
    else                                snprintf(label, (size_t)cap, "%s", l ? l : "");
    return true;
}

void modern_overlay_draw_menu(void) {
    const char *title = views_menu_title();
    int count = views_menu_entry_count();
    int cursor = views_menu_cursor();

    // The large layout (REQ-430j): the title, then the entries as standard
    // select rows (REQ-430n), scrolling when they outrun the panel.
    ML_Rect r = ml_large();
    draw_panel(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));
    int ty = r.y + ML_PAD;
    if (title) {
        bfont_draw_centered(title, r.x + r.w / 2, ty, PAL_CLR(YELLOW));
        ty += GH + ML_PAD;
    }
    lattice_band_h(r.x, ty, r.w, 4);
    ty += 4;
    ml_list_draw(r.x, ty, r.w, r.y + r.h - ty, count, cursor, menu_row, NULL,
                 TOUCH_LIST_MENU, PAL_CLR(DBLUE));
}

// ---------------------------------------------------------------------------
// Town menu (bottom frame, A..E letter rows).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Location-backdrop renderer . Picks one of 6 backdrop
// PNGs and animates a troop sprite at the bottom-left, replacing the world
// map area. Used by every "location screen" view (TOWN, HOME_CASTLE,
// OWN_CASTLE, DWELLING, ALCOVE, RECRUIT_SOLDIERS).
//
// The backdrop is 240x102; we draw at the map area's top-left (16,22). The
// troop sprite is inset one sprite-width and pinned to the bottom of the
// backdrop, drawn beneath the bottom panel.
// ---------------------------------------------------------------------------

typedef enum {
    LOC_NONE = 0,
    LOC_CASTLE,
    LOC_TOWN,
    LOC_PLAINS,
    LOC_FOREST,
    LOC_HILLCAVE,
    LOC_DUNGEON,
    LOC_ALCOVE,
} LocKind;

static Texture2D loc_texture(const Sprites *s, LocKind kind) {
    if (!s) return (Texture2D){ 0 };
    switch (kind) {
        case LOC_CASTLE:   return s->castle_backdrop;
        case LOC_TOWN:     return s->town_backdrop;
        case LOC_PLAINS:   return s->plains_backdrop;
        case LOC_FOREST:   return s->forest_backdrop;
        case LOC_HILLCAVE: return s->hillcave_backdrop;
        case LOC_DUNGEON:  return s->dungeon_backdrop;
        // The alcove borrows the hill cave until a pack gives it its own.
        case LOC_ALCOVE:   return s->alcove_backdrop.id ? s->alcove_backdrop
                                                        : s->hillcave_backdrop;
        case LOC_NONE: default: return (Texture2D){ 0 };
    }
}

// Draw the backdrop + animated troop. troop_idx is a Game troop index
// (0..24); troop_frame is the 0..3 animation frame the screen owns.
// draw_location takes (loc_id, troop_id, frame) -- the screen
// advances `frame` from SYN ticks. We mirror that contract.
//
// Exposed via screens_draw_location_backdrop() below for screen modules.
static void draw_location_backdrop(const Game *g, const Sprites *s,
                                   LocKind kind, int troop_idx,
                                   int troop_frame) {
    // The location layout (REQ-430j): the backdrop across the top of the map
    // pane at the smallest whole-number scale that covers its width, the
    // overshoot cropped evenly off the two sides. The text area sits directly
    // under it, so the two share one edge instead of overlapping.
    ML_Rect b = ml_loc_backdrop();
    int S = ml_loc_scale();
    int crop = (ML_BACKDROP_W * S - b.w) / 2;    // screen px cut from each side
    Texture2D bd = loc_texture(s, kind);
    if (bd.id && bd.width > 0 && bd.height > 0) {
        float px_per_src = (float)(ML_BACKDROP_W * S) / (float)bd.width;
        Rectangle src = { crop / px_per_src, 0,
                          b.w / px_per_src, b.h / px_per_src };
        Rectangle dst = { (float)b.x, (float)b.y, (float)b.w, (float)b.h };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(b.x, b.y, b.w, b.h, PAL_CLR(BLACK));
    }

    // The alcove keeps its own figure rather than borrowing a troop sprite:
    // the place is a person, not a creature that dwells there.
    Texture2D fig = { 0 };
    if (kind == LOC_ALCOVE && s && s->alcove_figure.id) {
        fig = s->alcove_figure_anim[sprites_frame(troop_frame,
                                                  s->alcove_figure_frames)];
        if (!fig.id) fig = s->alcove_figure;
    }
    const Resources *r = (g && g->res) ? g->res : NULL;
    if (fig.id && r && r->sprites.alcove_figure_w > 0) {
        // Placed by the pack in the backdrop's own 240x102 units, so it lands
        // on the same spot whatever scale the backdrop is drawn at.
        ui_blit(fig, b.x + r->sprites.alcove_figure_x * S - crop,
                     b.y + r->sprites.alcove_figure_y * S,
                     r->sprites.alcove_figure_w * S,
                     r->sprites.alcove_figure_h * S);
        return;
    }
    // Otherwise the tile-sized slot a troop sprite always filled: one tile in
    // from the left, standing on the backdrop's bottom edge.
    Texture2D ts = fig;
    if (!ts.id && s && troop_idx >= 0 && troop_idx < 25) {
        int frame = sprites_frame(sprites_stand(troop_frame),
                                  s->troop_anim_frames[troop_idx]);
        ts = s->troop_anim[troop_idx][frame];
        if (!ts.id) ts = s->troop_sprite[troop_idx];
    }
    if (ts.id && ts.width > 0)
        ui_blit(ts, b.x + CL_TILE_W, b.y + b.h - CL_TILE_H, CL_TILE_W, CL_TILE_H);
}

// Public bridge for screen modules in src/screens/. Takes an int
// for loc_kind so the LocKind enum can stay private to this file.
// Constants (must match LocKind enum order):
//   1 = LOC_CASTLE  2 = LOC_TOWN     3 = LOC_PLAINS
//   4 = LOC_FOREST  5 = LOC_HILLCAVE 6 = LOC_DUNGEON
//   7 = LOC_ALCOVE
//
// `troop_frame` is the 0..3 animation frame the caller owns. The
// screens advance their own frame from SYN ticks (e.g. recruit_soldiers
// game.c:2148 / 2216-2217).
void modern_overlay_draw_location_backdrop(const Game *g, const Sprites *s,
                                    int loc_kind, int troop_idx,
                                    int troop_frame) {
    LocKind k = LOC_NONE;
    switch (loc_kind) {
        case 1: k = LOC_CASTLE;   break;
        case 2: k = LOC_TOWN;     break;
        case 3: k = LOC_PLAINS;   break;
        case 4: k = LOC_FOREST;   break;
        case 5: k = LOC_HILLCAVE; break;
        case 6: k = LOC_DUNGEON;  break;
        case 7: k = LOC_ALCOVE;   break;
        default: break;
    }
    draw_location_backdrop(g, s, k, troop_idx, troop_frame);
}

static int town_backdrop_troop(const Game *g, const char *key) {
    // Deterministic pick per (seed, town id). Limit the pool to troops
    // that dwell at castles (militia/archers/pikemen/cavalry/knights) --
    // the "civilian" army that would idle in a human town. Matches the
    // home_troops[] filter used for recruit_soldiers
    // (dwells == DWELLING_CASTLE).
    int nt = troops_count();
    int pool[32];
    int npool = 0;
    for (int i = 0; i < nt && npool < 32; i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t) continue;
        if (strcmp(t->dwelling, "castle") == 0) pool[npool++] = i;
    }
    if (npool < 1) {
        // Fallback: use the whole catalog.
        if (nt < 1) nt = 1;
        unsigned long h = g ? g->seed ^ 0xA1B2C3u : 0;
        for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
        return (int)(h % (unsigned long)nt);
    }
    unsigned long h = g ? g->seed ^ 0xA1B2C3u : 0;
    for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
    return pool[h % (unsigned long)npool];
}

// The detail panel's text, wrapped to its width and laid out as lines so it
// can be split into pages. A line may carry a yellow label before a white
// value, and a line may start a block that pages keep whole.
#define TOWN_DETAIL_LINES 64
typedef struct {
    char  text[72];
    int   label;    // bytes of `text` drawn yellow before the rest; 0 = none
    Color fg;
    bool  block;    // starts a block kept on one page when it fits one
} TownLine;
typedef struct {
    TownLine line[TOWN_DETAIL_LINES];
    int      n;
    int      max_w;
    bool     next_block;
} TownText;

static void town_text_add(TownText *t, const char *text, Color fg) {
    const char *p = text ? text : "";
    while (*p && t->n < TOWN_DETAIL_LINES) {
        TownLine *l = &t->line[t->n];
        if (bfont_take_line(&p, t->max_w, l->text, (int)sizeof l->text) <= 0) break;
        l->fg = fg;
        l->label = 0;
        l->block = t->next_block;
        t->next_block = false;
        t->n++;
    }
}

// A block break: a blank line, and the next line starts a block.
static void town_text_gap(TownText *t) {
    if (t->n > 0 && t->n < TOWN_DETAIL_LINES) {
        TownLine *l = &t->line[t->n++];
        l->text[0] = '\0';
        l->label = 0;
        l->block = false;
    }
    t->next_block = true;
}

// "Label: value" from a template holding %VALUE%: the part before the value
// is drawn yellow, the value white.
static void town_text_labeled(TownText *t, const char *tmpl, const char *value) {
    char buf[RES_BANNER_LEN];
    ResTemplateVar vars[] = { { "VALUE", value } };
    resources_format_template(buf, sizeof buf, tmpl, vars, 1);
    const char *at = tmpl ? strstr(tmpl, "%VALUE%") : NULL;
    int first = t->n;
    town_text_add(t, buf, PAL_CLR(WHITE));
    if (at && first < t->n) {
        int lab = (int)(at - tmpl);
        int len = (int)strlen(t->line[first].text);
        t->line[first].label = lab < len ? lab : len;
    }
}

// The contract to describe: the Contracts list row under the cursor, else the
// contract held.
static const VillainDef *town_shown_villain(const Game *g) {
    const char *id = g->contract.active_id;
    if (views_town_list() == TOWN_LIST_CONTRACTS) {
        int slot = views_town_contract_slot(g, views_town_list_cursor());
        if (slot >= 0) id = g->contract.cycle[slot];
    }
    return (id && id[0]) ? villain_by_id(id) : NULL;
}

static void town_compose_contract(const Game *g, const VillainDef *v, TownText *t) {
    const Resources *res = g->res;
    const ResUI *ui = &res->ui;
    if (!v) { town_text_add(t, ui->cv_title_no_contract, PAL_CLR(WHITE)); return; }
    const ResVillainDesc *d = resources_villain_desc(res, v->id);
    char val[RES_VDESC_TEXT_LEN];
    town_text_labeled(t, ui->cv_label_name, v->name);
    if (d && d->alias[0]) town_text_labeled(t, ui->cv_label_alias, d->alias);
    snprintf(val, sizeof val, "%d", v->reward);
    town_text_labeled(t, ui->cv_label_reward, val);
    const ResZone *z = resources_zone_by_id(res, v->zone);
    town_text_labeled(t, ui->cv_label_last_seen, (z && z->name[0]) ? z->name : v->zone);
    snprintf(val, sizeof val, "%s", ui->cv_castle_unknown);
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *c = &g->castles[i];
        if (!c->known || strcmp(c->villain_id, v->id) != 0) continue;
        const ResCastle *rc = resources_castle_by_id(res, c->id);
        snprintf(val, sizeof val, "%s", (rc && rc->name[0]) ? rc->name : c->id);
        break;
    }
    town_text_labeled(t, ui->cv_label_castle, val);
    if (d && d->features[0]) {
        town_text_gap(t);
        town_text_add(t, ui->cv_features_header, PAL_CLR(YELLOW));
        town_text_add(t, d->features, PAL_CLR(WHITE));
    }
    if (d && d->crimes[0]) {
        town_text_gap(t);
        town_text_add(t, ui->cv_crimes_header, PAL_CLR(YELLOW));
        town_text_add(t, d->crimes, PAL_CLR(WHITE));
    }
}

// Facts about the row under the cursor, from the game state.
// The screen whose facts are on show: the detail screen open, or on the menu
// the one the cursor row opens.
static TownList town_screen(void) {
    TownList l = views_town_list();
    if (l != TOWN_LIST_MENU) return l;
    switch (views_town_cursor()) {
        case TOWN_ROW_CONTRACT: return TOWN_LIST_CONTRACTS;
        case TOWN_ROW_INFO:     return TOWN_LIST_INFO;
        case TOWN_ROW_BOAT:     return TOWN_LIST_BOAT;
        case TOWN_ROW_SPELL:    return TOWN_LIST_TEMPLE;
        case TOWN_ROW_SIEGE:    return TOWN_LIST_SIEGE;
        default:                return TOWN_LIST_MENU;
    }
}

// The pack's full row text for a menu row, prices included, its legacy key
// letter dropped.
static void town_row_head(const Game *g, TownRow row, TownText *t) {
    char head[128];
    views_town_row_text(g, row, head, sizeof head);
    if (head[0] >= 'A' && head[0] <= 'Z' && head[1] == ')' && head[2] == ' ')
        memmove(head, head + 3, strlen(head + 3) + 1);
    town_text_add(t, head, PAL_CLR(YELLOW));
}

// Facts for the screen on show, from the game state.
static void town_compose_detail(const Game *g, TownText *t) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    char buf[RES_BANNER_LEN];
    const char *key = views_town_record_key();

    switch (town_screen()) {
        case TOWN_LIST_CONTRACTS:
            town_compose_contract(g, town_shown_villain(g), t);
            break;
        case TOWN_LIST_INFO: {
            char intel[512];
            views_town_intel_text(g, intel, sizeof intel);
            town_text_add(t, intel, PAL_CLR(WHITE));
            break;
        }
        case TOWN_LIST_BOAT: {
            if (!views_town_boat_available(g)) {
                town_text_add(t, bn->town_boat_no_master, PAL_CLR(WHITE));
                break;
            }
            town_row_head(g, TOWN_ROW_BOAT, t);
            const char *dock = key ? resources_town_dock(res, key) : NULL;
            if (dock && dock[0]) {
                town_text_gap(t);
                town_text_add(t, dock, PAL_CLR(WHITE));
            }
            break;
        }
        case TOWN_LIST_TEMPLE: {
            town_row_head(g, TOWN_ROW_SPELL, t);
            const SpellDef *sp = views_town_spell(g);
            if (!sp) break;
            const char *lore = resources_spell_lore(res, sp->id);
            if (!lore || !lore[0]) lore = sp->description;
            if (lore && lore[0]) {
                town_text_gap(t);
                town_text_add(t, lore, PAL_CLR(WHITE));
            }
            int left = g->stats.max_spells - GameKnownSpells(g);
            if (left <= 0) {
                resources_format_template(buf, sizeof buf, bn->town_spell_at_cap, NULL, 0);
            } else {
                char lbuf[16];
                snprintf(lbuf, sizeof lbuf, "%d", left);
                ResTemplateVar vars[] = { { "LEFT", lbuf }, { "S", left == 1 ? "" : "s" } };
                resources_format_template(buf, sizeof buf, bn->town_spell_can_learn, vars, 2);
            }
            town_text_gap(t);
            town_text_add(t, buf, PAL_CLR(WHITE));
            break;
        }
        case TOWN_LIST_SIEGE:
            town_row_head(g, TOWN_ROW_SIEGE, t);
            town_text_gap(t);
            town_text_add(t, bn->town_siege_lore, PAL_CLR(WHITE));
            break;
        default: break;
    }
}

// Split the lines into pages of `per` lines. A block that would straddle a
// page break, and fits on a page of its own, starts the next page instead.
// starts[] receives each page's first line; returns the page count.
static int town_paginate(const TownText *t, int per, int *starts, int max_pages) {
    int pages = 0, used = 0;
    starts[pages++] = 0;
    for (int i = 0; i < t->n; i++) {
        if (t->line[i].block && used > 0) {
            int len = 1;
            while (i + len < t->n && !t->line[i + len].block) len++;
            if (used + len > per && len <= per) used = per;   // push to next page
        }
        if (used == per) {
            if (pages == max_pages) break;
            used = 0;
            if (t->line[i].text[0] == '\0') {   // no blank line atop a page
                starts[pages++] = i + 1;
                continue;
            }
            starts[pages++] = i;
        }
        used++;
    }
    if (pages > 1 && starts[pages - 1] >= t->n) pages--;   // nothing left for it
    return pages;
}

void modern_overlay_draw_town(const Game *g, const Sprites *s) {
    const char *name = views_town_display_name();
    const char *info = views_town_info_text();
    TownList list = views_town_list();
    bool menu = (list == TOWN_LIST_MENU);
    TownList screen = town_screen();

    // Town view doesn't own a SYN-tick frame counter yet; derive a
    // free-running tick from real time at the SYN cadence (~150ms per
    // frame -> 6.7fps).
    int troop_idx = town_backdrop_troop(g, name);
    int town_frame = (int)(GetTime() * 6.66);

    // Full screen: the pane, the band and the HUD, tiled exactly, the boxes
    // split by the lattice. On Rome's 776x480:
    //   title 776x22
    //   backdrop 480x192 (2x, top cut) | face 192x192 (2x) | siege 96, gold 96
    //   list 256x258                   | detail 516x258
    const int BS = 2;
    const int BAND = 4;
    int pad = ML_PAD;
    ML_Rect r = ml_full();
    int right = r.x + r.w, bottom = r.y + r.h;
    DrawRectangle(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));
    const Resources *res = (g && g->res) ? g->res : NULL;

    // Title strip: "Town of <name>" (and "> <screen>" in a detail), the
    // town's zone on the right.
    int title_h = GH + 14;   // 30: the section rows below fill their column exactly
    char header[128] = "";
    if (res) {
        ResTemplateVar hv[] = { { "NAME", (name && name[0]) ? name : "" } };
        resources_format_template(header, sizeof header, res->banners.town_header, hv, 1);
        if (!menu) {
            char label[64];
            views_town_menu_label(g, views_town_cursor(), label, sizeof label);
            size_t n = strlen(header);
            snprintf(header + n, sizeof header - n, " > %s", label);
        }
        const char *key = views_town_record_key();
        const ResTown *tw = key ? resources_town_by_id(res, key) : NULL;
        const ResZone *z = tw ? resources_zone_by_id(res, tw->zone) : NULL;
        if (z && z->name[0])
            bfont_draw(z->name, right - pad - (int)bfont_measure(z->name).x,
                       r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    }
    bfont_draw(header, r.x + pad, r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    lattice_band_h(r.x, r.y + title_h, r.w, BAND);

    // Top row: as tall as the 2x face, so the face and the two HUD tiles sit
    // flush. The backdrop is drawn at 2x with its top rows cut to that height
    // (the ground stays, so the troop at 1x still stands on its bottom edge).
    int top = r.y + title_h + BAND;
    int fs = CL_TILE_W * BS;
    int bw = ML_BACKDROP_W * BS, bh = fs;
    Texture2D bd = loc_texture(s, LOC_TOWN);
    if (bd.id && bd.height > 0) {
        float src_h = (float)bd.height * (float)bh / (float)(ML_BACKDROP_H * BS);
        Rectangle src = { 0, (float)bd.height - src_h, (float)bd.width, src_h };
        Rectangle dst = { (float)r.x, (float)top, (float)bw, (float)bh };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(r.x, top, bw, bh, PAL_CLR(BLACK));
    }
    // The town's head townsperson stands where a troop used to, at 1x on the
    // bottom edge, playing their loop; a pack that names none keeps the troop.
    const char *tkey = views_town_record_key();
    const ResTown *town_res = (res && tkey) ? resources_town_by_id(res, tkey) : NULL;
    const ResZone *zone_res = town_res ? resources_zone_by_id(res, town_res->zone) : NULL;
    int headman = town_res ? resources_portrait_index(res, town_res->headman) : -1;
    if (s && headman >= 0 && s->portrait_frames[headman] > 0) {
        ui_blit(s->portrait_anim[headman][sprites_frame((int)(GetTime() * 1000.0 / 180.0),
                                                        s->portrait_frames[headman])],
                r.x + CL_TILE_W / 2, top + bh - CL_TILE_H, CL_TILE_W, CL_TILE_H);
    } else if (s && troop_idx >= 0 && troop_idx < 25) {
        Texture2D ts = s->troop_anim[troop_idx][sprites_frame(sprites_stand(town_frame),
                                                s->troop_anim_frames[troop_idx])];
        if (!ts.id) ts = s->troop_sprite[troop_idx];
        if (ts.id) ui_blit(ts, r.x + CL_TILE_W / 2, top + bh - CL_TILE_H,
                           CL_TILE_W, CL_TILE_H);
    }
    lattice_band_v(r.x + bw, top, BAND, bh);

    // The portrait slot at 2x shows the person of the screen on show: the
    // wanted face (or the empty silhouette) for Contracts, the informant, the
    // continent's boat master (none where the town has no dock), priest and
    // siege engineer.
    int fx = r.x + bw + BAND;
    const VillainDef *v = g ? town_shown_villain(g) : NULL;
    const char *who_id = NULL;
    switch (screen) {
        case TOWN_LIST_CONTRACTS: who_id = (menu && town_res) ? town_res->townhead : NULL; break;
        case TOWN_LIST_INFO:   who_id = town_res ? town_res->informant : NULL; break;
        case TOWN_LIST_BOAT:   who_id = (zone_res && views_town_boat_available(g))
                                      ? zone_res->boatmaster : NULL; break;
        case TOWN_LIST_TEMPLE: who_id = zone_res ? zone_res->pontifex : NULL; break;
        case TOWN_LIST_SIEGE:  who_id = zone_res ? zone_res->siegemaster : NULL; break;
        default: break;
    }
    int who = (res && who_id) ? resources_portrait_index(res, who_id) : -1;
    if (screen != TOWN_LIST_CONTRACTS || menu) {
        if (who >= 0 && s && s->portrait_frames[who] > 0)
            ui_blit(s->portrait_anim[who][sprites_frame((int)(GetTime() * 2.0),
                                                        s->portrait_frames[who])],
                    fx, top, fs, fs);
        else if (screen == TOWN_LIST_BOAT && s && s->hud_boat_silhouette.id)
            ui_blit(s->hud_boat_silhouette, fx, top, fs, fs);   // no boat here
        else
            DrawRectangle(fx, top, fs, fs, PAL_CLR(BLACK));
    } else if (v && s && v->index >= 0 && v->index < 17) {
        Texture2D face = s->villain_anim[v->index][sprites_frame(
            (int)(GetTime() * 2.0), s->villain_anim_frames[v->index])];
        if (!face.id) face = s->villain_portrait[v->index];
        ui_blit(face, fx, top, fs, fs);
    } else if (s && s->hud_contract_silhouette.id) {
        ui_blit(s->hud_contract_silhouette, fx, top, fs, fs);
    }
    lattice_band_v(fx + fs, top, BAND, bh);

    // The HUD's own siege weapons tile over its gold purse, flush.
    int hx = fx + fs + BAND;
    hud_draw_siege_tile(g, s, hx, top);
    hud_draw_gold_tile(g, s, hx, top + CL_TILE_H);

    int low = top + bh;
    lattice_band_h(r.x, low, r.w, BAND);
    low += BAND;

    // Left column: the menu (every row opens a detail, marked ">"), or the
    // detail's rows then Back. Standard select rows (ml_row_h, a rail under
    // each), stacked from the top; the height left below stays empty. The
    // cursor row is inverted.
    const int RULE = ML_ROW_RULE;
    int mw = 16 * GW;
    int lh = bottom - low;
    int rows = views_town_list_rows(g);
    int cursor = views_town_list_cursor();
    int rh = ml_row_h();
    // A list longer than the column scrolls to keep the cursor in view.
    int vis = (lh + RULE) / (rh + RULE);
    int first = 0;
    if (rows > vis && vis > 0) {
        first = cursor - vis + 1;
        if (first < 0) first = 0;
        if (first > rows - vis) first = rows - vis;
    }
    for (int i = first; i < rows; i++) {
        char label[64] = "";
        bool enabled = true, held = false;
        views_town_list_row(g, i, label, sizeof label, &enabled, &held);
        if (menu) {
            size_t n = strlen(label);
            snprintf(label + n, sizeof label - n, " >");
        }
        bool back = !menu && i == rows - 1;
        bool sel = i == cursor;
        Color fg = !(enabled || back) ? PAL_CLR(DGREY) : sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        int ry = low + (i - first) * (rh + RULE);
        int h = rh;
        if (ry + h > bottom) break;
        int tx = r.x + pad + (list == TOWN_LIST_CONTRACTS ? GW : 0);
        // A tap on a row that can be chosen selects and carries it out.
        sel_row(r.x, ry, mw, h, tx, label, sel, fg, PAL_CLR(DBLUE),
                (enabled || back) ? TOUCH_LIST_TOWN : 0, i);
        if (held)   // the contract held: a dot before its name
            DrawCircle(r.x + pad + GW / 2 - 2, ry + h / 2, 4,
                       sel ? PAL_CLR(DBLUE) : PAL_CLR(YELLOW));
        lattice_band_h(r.x, ry + h, mw, RULE);
    }
    lattice_band_v(r.x + mw, low, BAND, lh);

    // Detail (right): a result message until the next key, else the facts
    // for the screen on show. A detail screen pages its text (Up/Down); the
    // menu shows the first page as a summary.
    const int INSET = pad + 4;
    int dx = r.x + mw + BAND + INSET;
    int dw = right - dx - INSET;
    int line_h = GH + 2;
    int per = (lh - 2 * INSET) / line_h;
    TownText t = { .n = 0, .max_w = dw };
    if (info && info[0]) {
        town_text_add(&t, info, PAL_CLR(WHITE));
    } else if (menu) {
        // The main page: the section's person invites you in. Boat at a town
        // with no boat master says there is none.
        const ResTownInvite *inv = (res && town_res)
                                 ? resources_town_invite(res, town_res->invitations) : NULL;
        const char *line = NULL;
        char rites[RES_BANNER_LEN];
        if (inv) switch (screen) {
            case TOWN_LIST_CONTRACTS: line = inv->contracts; break;
            case TOWN_LIST_BOAT:      line = views_town_boat_available(g) ? inv->boat
                                                          : res->banners.town_boat_no_master; break;
            case TOWN_LIST_INFO:      line = inv->information; break;
            case TOWN_LIST_TEMPLE:
                if (!views_town_row_enabled(g, TOWN_ROW_SPELL)) {
                    views_town_rites_text(g, rites, sizeof rites);
                    line = rites;
                } else {
                    line = inv->temple;
                }
                break;
            case TOWN_LIST_SIEGE:     line = inv->siege; break;
            default: break;
        }
        if (line && line[0]) {
            char text[RES_BANNER_LEN];
            ResTemplateVar vars[] = {
                { "HERO", g ? g->character.name : "" },
                { "TOWN", (name && name[0]) ? name : "" },
            };
            resources_format_template(text, sizeof text, line, vars, 2);
            town_text_add(&t, text, PAL_CLR(WHITE));
        }
    } else {
        town_compose_detail(g, &t);
    }
    int starts[16];
    int pages = 1;
    starts[0] = 0;
    if (t.n > per && per > 1) pages = town_paginate(&t, per - 1, starts, 16);  // last line: pager
    int page = 0, end = t.n;
    if (menu) {
        // The summary: the first page only, and no pager.
        if (pages > 1) end = starts[1];
        if (end > per) end = per;
        pages = 1;
        views_town_set_detail_pages(1);
    } else {
        views_town_set_detail_pages(pages);
        page = views_town_detail_page();
        end = (page + 1 < pages) ? starts[page + 1] : t.n;
    }
    int ty = low + INSET;
    for (int i = starts[page]; i < end; i++) {
        const TownLine *l = &t.line[i];
        if (l->label > 0) {
            char lab[72];
            snprintf(lab, sizeof lab, "%.*s", l->label, l->text);
            bfont_draw(lab, dx, ty, PAL_CLR(YELLOW));
            bfont_draw(l->text + l->label, dx + (int)bfont_measure(lab).x, ty, l->fg);
        } else {
            bfont_draw(l->text, dx, ty, l->fg);
        }
        ty += line_h;
    }
    if (pages > 1) {
        // "1/2" between the arrows, bottom right; an arrow shows only where
        // there is a page to go to, and a tap on it pages (Up/Down).
        char pg[32];
        snprintf(pg, sizeof pg, "%d/%d", page + 1, pages);
        int py = bottom - INSET - GH;
        int aw = GH;
        int nx = right - INSET - aw;
        int tx = nx - pad - (int)bfont_measure(pg).x;
        int px = tx - pad - aw;
        bfont_draw(pg, tx, py, PAL_CLR(YELLOW));
        if (page > 0) {
            DrawTriangle((Vector2){ px + aw / 2, py }, (Vector2){ px, py + GH },
                         (Vector2){ px + aw, py + GH }, PAL_CLR(YELLOW));
            touch_region(px, py, aw, GH, KEY_UP);
        }
        if (page + 1 < pages) {
            DrawTriangle((Vector2){ nx, py }, (Vector2){ nx + aw / 2, py + GH },
                         (Vector2){ nx + aw, py }, PAL_CLR(YELLOW));
            touch_region(nx, py, aw, GH, KEY_DOWN);
        }
    }
}

// =============================================================================
//  Modern castle screens (home castle, owned castle) -- the town layout
// =============================================================================

// "Label value" with the label in yellow.
static void town_text_pair(TownText *t, const char *label, const char *value) {
    char buf[160];
    snprintf(buf, sizeof buf, "%s %s", label, value);
    int first = t->n;
    town_text_add(t, buf, PAL_CLR(WHITE));
    if (first < t->n) {
        int lab = (int)strlen(label);
        int len = (int)strlen(t->line[first].text);
        t->line[first].label = lab < len ? lab : len;
    }
}

static void castle_fmt(char *out, int cap, const char *tmpl, const char *count,
                       const char *max) {
    ResTemplateVar v[] = { { "COUNT", count }, { "MAX", max ? max : "" },
                           { "RANK", count }, { "GOLD", count } };
    resources_format_template(out, cap, tmpl, v, 4);
}

static void draw_portrait_anim(const Sprites *s, int idx, int x, int y, int size,
                               double fps) {
    if (!s || idx < 0 || s->portrait_frames[idx] <= 0) {
        DrawRectangle(x, y, size, size, PAL_CLR(BLACK));
        return;
    }
    ui_blit(s->portrait_anim[idx][sprites_frame((int)(GetTime() * fps),
                                                s->portrait_frames[idx])],
            x, y, size, size);
}

static int castle_pick_troop(const Game *g, const char *key) {
    int pool[8];
    int n = modern_castle_pool(pool, 8);
    if (n < 1) return -1;
    unsigned long h = g ? (g->seed ^ 0x0CA571E5u) : 0;
    for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
    return pool[h % (unsigned long)n];
}

// The Promotion page: the win/lose screen's layout -- the award image at its
// authored size times the UI scale on the right, the Emperor's words and the
// rank's gains beside it, and a Continue row under the text.
static void castle_draw_promotion(const Game *g, const Sprites *s, const ResCastle *rc) {
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    ML_Rect r = ml_full();
    int pad = ML_PAD;
    DrawRectangle(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));
    int aud_needed = 0, rank = 0;
    modern_castle_audience(&aud_needed, &rank);
    int idx = (rc && rank >= 0 && rank < 4) ? resources_portrait_index(res, rc->special.promotion[rank]) : -1;
    int img_w = 0;
    if (s && idx >= 0 && s->portrait_frames[idx] > 0) {
        Texture2D img = s->portrait_anim[idx][0];
        if (img.id && img.width > 0 && img.height > 0) {
            int sc = CL_UI;
            if (img.height * sc > r.h) sc = ui_fit_scale(img.width, img.height, r.w, r.h);
            img_w = img.width * sc;
            ui_blit(img, r.x + r.w - img_w, r.y, img_w, img.height * sc);
        }
    }
    const int INSET = pad + 4;
    int tx = r.x + INSET, tw = r.w - img_w - 2 * INSET;
    TownText t = { .n = 0, .max_w = tw };
    char buf[RES_BANNER_LEN], nb[16];
    if (rc) {
        audience_substitute(g, aud_needed, rc->special.audience_rank_up, buf, sizeof buf);
        town_text_add(&t, buf, PAL_CLR(WHITE));
    }
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (cls && rank > 0) {
        int l0, s0, p0, c0, l1, s1, p1, c1;
        class_stats_at_rank(cls, rank - 1, &l0, &s0, &p0, &c0);
        class_stats_at_rank(cls, rank, &l1, &s1, &p1, &c1);
        town_text_gap(&t);
        snprintf(nb, sizeof nb, "%d", l1 - l0);
        castle_fmt(buf, sizeof buf, bn->castle_gain_leadership, nb, NULL);
        town_text_add(&t, buf, PAL_CLR(YELLOW));
        snprintf(nb, sizeof nb, "%d", c1 - c0);
        castle_fmt(buf, sizeof buf, bn->castle_gain_commission, nb, NULL);
        town_text_add(&t, buf, PAL_CLR(YELLOW));
        if (s1 - s0 > 0) {
            snprintf(nb, sizeof nb, "%d", s1 - s0);
            castle_fmt(buf, sizeof buf, bn->castle_gain_spells, nb, NULL);
            town_text_add(&t, buf, PAL_CLR(YELLOW));
        }
    }
    int rh = ml_row_h();
    int row_y = r.y + r.h - rh - ML_ROW_RULE;
    int ty = r.y + INSET;
    for (int i = 0; i < t.n && ty + GH <= row_y; i++) {
        bfont_draw(t.line[i].text, tx, ty, t.line[i].fg);
        ty += GH + 2;
    }
    char label[64];
    modern_castle_row(g, 0, label, sizeof label, NULL);
    lattice_band_h(r.x, row_y - ML_ROW_RULE, r.w - img_w, ML_ROW_RULE);
    sel_row(r.x, row_y, r.w - img_w, rh, r.x + pad, label, true,
            PAL_CLR(YELLOW), PAL_CLR(DBLUE), TOUCH_LIST_CASTLE, 0);
}

void modern_overlay_draw_castle(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    if (modern_castle_page() == MC_PROMOTION) {
        castle_draw_promotion(g, s, resources_castle_by_id(g->res, modern_castle_id()));
        return;
    }
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const ResUI *ui = &res->ui;
    const char *cid = modern_castle_id();
    const ResCastle *rc = resources_castle_by_id(res, cid);
    const CastleRecord *cr = GameFindCastleConst(g, cid);
    bool home = modern_castle_is_home();
    McPage page = modern_castle_page();
    int cursor = modern_castle_cursor();
    int rows = modern_castle_rows(g);

    const int BS = 2, BAND = 4, RULE = ML_ROW_RULE;
    int pad = ML_PAD;
    ML_Rect r = ml_full();
    int right = r.x + r.w, bottom = r.y + r.h;
    DrawRectangle(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));

    // Title strip: the castle's name (and "> <page>"), its zone on the right.
    int title_h = GH + 14;
    char title[128];
    snprintf(title, sizeof title, "%s", (rc && rc->name[0]) ? rc->name : cid);
    if (page != MC_MENU) {
        char label[64];
        const char *tp[] = { "", bn->castle_menu_recruit, bn->castle_menu_audience,
                             bn->castle_menu_garrison, bn->castle_menu_withdraw };
        snprintf(label, sizeof label, "%s", tp[page]);
        size_t n = strlen(title);
        snprintf(title + n, sizeof title - n, " > %s", label);
    }
    bfont_draw(title, r.x + pad, r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    const ResZone *z = rc ? resources_zone_by_id(res, rc->zone) : NULL;
    if (z && z->name[0])
        bfont_draw(z->name, right - pad - (int)bfont_measure(z->name).x,
                   r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    lattice_band_h(r.x, r.y + title_h, r.w, BAND);

    // Backdrop at 2x, cut to the portrait's height; the ruler (or a castle
    // troop) standing on it at 1x.
    int top = r.y + title_h + BAND;
    int fs = CL_TILE_W * BS;
    int bw = ML_BACKDROP_W * BS, bh = fs;
    Texture2D bd = loc_texture(s, LOC_CASTLE);
    if (bd.id && bd.height > 0) {
        float src_h = (float)bd.height * (float)bh / (float)(ML_BACKDROP_H * BS);
        Rectangle src = { 0, (float)bd.height - src_h, (float)bd.width, src_h };
        Rectangle dst = { (float)r.x, (float)top, (float)bw, (float)bh };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(r.x, top, bw, bh, PAL_CLR(BLACK));
    }
    // The barracks keeper stands in the castle; the ruler only on Audience.
    bool barracks = rc && page != MC_AUDIENCE;
    const char *fig_id = !rc ? "" : (barracks && rc->special.barracks_figure[0])
                                  ? rc->special.barracks_figure : rc->special.figure;
    int figure = (home && rc) ? resources_portrait_index(res, fig_id) : -1;
    if (s && figure >= 0 && s->portrait_frames[figure] > 0) {
        ui_blit(s->portrait_anim[figure][sprites_frame((int)(GetTime() * 1000.0 / 180.0),
                                                       s->portrait_frames[figure])],
                r.x + CL_TILE_W / 2, top + bh - CL_TILE_H, CL_TILE_W, CL_TILE_H);
    } else if (s) {
        int ti = castle_pick_troop(g, cid);
        if (ti >= 0) {
            Texture2D ts = s->troop_anim[ti][sprites_frame(sprites_stand((int)(GetTime() * 6.66)),
                                                           s->troop_anim_frames[ti])];
            if (!ts.id) ts = s->troop_sprite[ti];
            if (ts.id) ui_blit(ts, r.x + CL_TILE_W / 2, top + bh - CL_TILE_H, CL_TILE_W, CL_TILE_H);
        }
    }
    lattice_band_v(r.x + bw, top, BAND, bh);

    // Portrait slot: the troop under the cursor at 2x on a troop page; the
    // promotion image after a promotion; else the ruler's portrait.
    int fx = r.x + bw + BAND;
    char label[64];
    const char *row_troop = NULL;
    if (page != MC_MENU && page != MC_AUDIENCE)
        modern_castle_row(g, cursor, label, sizeof label, &row_troop);
    int aud_needed = 0, aud_rank = 0;
    int aud = modern_castle_audience(&aud_needed, &aud_rank);
    const TroopDef *pt = row_troop ? troop_by_id(row_troop) : NULL;
    if (pt && s) {
        Texture2D ts = s->troop_anim[pt->index][sprites_frame(sprites_stand((int)(GetTime() * 6.66)),
                                                              s->troop_anim_frames[pt->index])];
        if (!ts.id) ts = s->troop_sprite[pt->index];
        DrawRectangle(fx, top, fs, fs, PAL_CLR(BLACK));
        if (s->troop_portrait[pt->index].id) ts = s->troop_portrait[pt->index];   // the troop's portrait, when it has one
        if (ts.id) ui_blit(ts, fx, top, fs, fs);
    } else if (home && rc) {
        int idx = resources_portrait_index(res, (barracks && rc->special.barracks_portrait[0])
                                                    ? rc->special.barracks_portrait : rc->special.portrait);
        if (page == MC_AUDIENCE && aud == GAME_AUDIENCE_PROMOTED + 1 &&
            aud_rank >= 0 && aud_rank < 4) {
            int pi = resources_portrait_index(res, rc->special.promotion[aud_rank]);
            if (pi >= 0) idx = pi;
        }
        draw_portrait_anim(s, idx, fx, top, fs, 2.0);
    } else {
        DrawRectangle(fx, top, fs, fs, PAL_CLR(BLACK));
    }
    lattice_band_v(fx + fs, top, BAND, bh);
    int hx = fx + fs + BAND;
    hud_draw_siege_tile(g, s, hx, top);
    hud_draw_gold_tile(g, s, hx, top + CL_TILE_H);

    int low = top + bh;
    lattice_band_h(r.x, low, r.w, BAND);
    low += BAND;

    // Left column: standard select rows, stacked from the top.
    int mw = 16 * GW;
    int lh = bottom - low;
    int rh = ml_row_h();
    int vis = (lh + RULE) / (rh + RULE);
    int first = 0;
    if (rows > vis && vis > 0) {
        first = cursor - vis + 1;
        if (first < 0) first = 0;
        if (first > rows - vis) first = rows - vis;
    }
    for (int i = first; i < rows; i++) {
        int ry = low + (i - first) * (rh + RULE);
        if (ry + rh > bottom) break;
        modern_castle_row(g, i, label, sizeof label, NULL);
        bool sel = (i == cursor);
        sel_row(r.x, ry, mw, rh, r.x + pad, label, sel,
                sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE), PAL_CLR(DBLUE),
                TOUCH_LIST_CASTLE, i);
        lattice_band_h(r.x, ry + rh, mw, RULE);
    }
    lattice_band_v(r.x + mw, low, BAND, lh);

    // Detail panel.
    const int INSET = pad + 4;
    int dx = r.x + mw + BAND + INSET;
    int dw = right - dx - INSET;
    int line_h = GH + 2;
    int step_v = 0, step_max = 0;
    bool stepper = modern_castle_stepper(&step_v, &step_max);
    TownText t = { .n = 0, .max_w = dw };
    char buf[RES_BANNER_LEN], nb[16], mb[16];
    const char *msg = modern_castle_message();
    if (msg) {
        town_text_add(&t, msg, PAL_CLR(WHITE));
    } else if (page == MC_MENU) {
        const char *inv = home ? (cursor == 0 ? bn->castle_invite_recruit : bn->castle_invite_audience)
                               : (cursor == 0 ? bn->castle_invite_garrison : bn->castle_invite_withdraw);
        ResTemplateVar v[] = { { "HERO", g->character.name },
                               { "CASTLE", (rc && rc->name[0]) ? rc->name : cid } };
        resources_format_template(buf, sizeof buf, inv, v, 2);
        town_text_add(&t, buf, PAL_CLR(WHITE));
    } else if (page == MC_AUDIENCE) {
        const ClassDef *cls = class_by_id(g->character.cls.id);
        int rank = g->character.cls.rank_index;
        const ResEconomy *ec = &res->economy;
        int ares = 0, aneed = 0;
        GameAudienceGain gain;
        McAudience akind = modern_castle_audience_result(&ares, &aneed, &gain);
        if (ares && rc) {
            // The Emperor's answer to a Blessing or Tribute, and what it gave.
            const char *tmpl = akind == MC_AUD_BLESSING
                ? (ares == GAME_BLESSING_GRANTED + 1 ? rc->special.audience_blessing_granted
                   : ares == GAME_BLESSING_NEED_ARTIFACTS + 1 ? rc->special.audience_blessing_needed
                   : rc->special.audience_blessing_already)
                : (ares == 1 ? rc->special.audience_tribute_paid : rc->special.audience_tribute_needed);
            audience_substitute(g, aneed, tmpl, buf, sizeof buf);
            town_text_add(&t, buf, PAL_CLR(WHITE));
            if (gain.leadership > 0 || gain.spell_power > 0 || gain.max_spells > 0) town_text_gap(&t);
            if (gain.leadership > 0) {
                snprintf(nb, sizeof nb, "%d", gain.leadership);
                castle_fmt(buf, sizeof buf, bn->castle_gain_leadership, nb, NULL);
                town_text_add(&t, buf, PAL_CLR(YELLOW));
            }
            if (gain.spell_power > 0) {
                snprintf(nb, sizeof nb, "%d", gain.spell_power);
                castle_fmt(buf, sizeof buf, bn->castle_gain_spell_power, nb, NULL);
                town_text_add(&t, buf, PAL_CLR(YELLOW));
            }
            if (gain.max_spells > 0) {
                snprintf(nb, sizeof nb, "%d", gain.max_spells);
                castle_fmt(buf, sizeof buf, bn->castle_gain_spells, nb, NULL);
                town_text_add(&t, buf, PAL_CLR(YELLOW));
            }
        } else if (ec->audiences && !aud && cursor == 1) {
            snprintf(nb, sizeof nb, "%d", GameArtifactsFound(g));
            snprintf(mb, sizeof mb, "%d", artifacts_count() < 8 ? artifacts_count() : 8);
            castle_fmt(buf, sizeof buf, bn->castle_artifacts, nb, mb);
            town_text_add(&t, buf, PAL_CLR(WHITE));
        } else if (ec->audiences && !aud && cursor == 2) {
            snprintf(nb, sizeof nb, "%d", ec->tribute_cost);
            castle_fmt(buf, sizeof buf, bn->castle_cost, nb, NULL);
            town_text_add(&t, buf, PAL_CLR(WHITE));
        } else if (aud && rc) {
            const char *tmpl = aud == GAME_AUDIENCE_PROMOTED + 1 ? rc->special.audience_rank_up
                             : aud == GAME_AUDIENCE_MORE_NEEDED + 1 ? rc->special.audience_more_needed
                             : rc->special.audience_final_rank;
            audience_substitute(g, aud_needed, tmpl, buf, sizeof buf);
            town_text_add(&t, buf, PAL_CLR(WHITE));
            if (aud == GAME_AUDIENCE_PROMOTED + 1 && cls && rank > 0) {
                int l0, s0, p0, c0, l1, s1, p1, c1;
                class_stats_at_rank(cls, rank - 1, &l0, &s0, &p0, &c0);
                class_stats_at_rank(cls, rank, &l1, &s1, &p1, &c1);
                town_text_gap(&t);
                snprintf(nb, sizeof nb, "%d", l1 - l0);
                castle_fmt(buf, sizeof buf, bn->castle_gain_leadership, nb, NULL);
                town_text_add(&t, buf, PAL_CLR(YELLOW));
                snprintf(nb, sizeof nb, "%d", c1 - c0);
                castle_fmt(buf, sizeof buf, bn->castle_gain_commission, nb, NULL);
                town_text_add(&t, buf, PAL_CLR(YELLOW));
                if (s1 - s0 > 0) {
                    snprintf(nb, sizeof nb, "%d", s1 - s0);
                    castle_fmt(buf, sizeof buf, bn->castle_gain_spells, nb, NULL);
                    town_text_add(&t, buf, PAL_CLR(YELLOW));
                }
            }
        } else {
            castle_fmt(buf, sizeof buf, bn->castle_rank, g->character.cls.rank_title, NULL);
            town_text_add(&t, buf, PAL_CLR(WHITE));
            if (cls && rank + 1 < cls->rank_count) {
                castle_fmt(buf, sizeof buf, bn->castle_next_rank, cls->ranks[rank + 1].name, NULL);
                town_text_add(&t, buf, PAL_CLR(WHITE));
                int need = cls->ranks[rank + 1].villains_needed - GameVillainsCaught(g);
                snprintf(nb, sizeof nb, "%d", need > 0 ? need : 0);
                castle_fmt(buf, sizeof buf, bn->castle_needed, nb, NULL);
                town_text_add(&t, buf, PAL_CLR(WHITE));
            }
        }
    } else if (pt) {
        town_text_add(&t, pt->name, PAL_CLR(YELLOW));
        snprintf(nb, sizeof nb, "%d", pt->skill_level); town_text_pair(&t, ui->army_skill, nb);
        snprintf(nb, sizeof nb, "%d", pt->move_rate);   town_text_pair(&t, ui->army_move, nb);
        snprintf(nb, sizeof nb, "%d", pt->hit_points);  town_text_pair(&t, ui->army_hit_points, nb);
        snprintf(nb, sizeof nb, "%d-%d", pt->melee_min, pt->melee_max); town_text_pair(&t, ui->army_damage, nb);
        snprintf(nb, sizeof nb, "%d", pt->recruit_cost); town_text_pair(&t, ui->army_g_cost, nb);
        town_text_gap(&t);
        int in_army = 0, in_garrison = 0;
        for (int k = 0; k < GAME_ARMY_SLOTS; k++) {
            if (strcmp(g->army[k].id, pt->id) == 0) in_army += g->army[k].count;
            if (cr && strcmp(cr->garrison[k].id, pt->id) == 0) in_garrison += cr->garrison[k].count;
        }
        snprintf(nb, sizeof nb, "%d", in_army);
        castle_fmt(buf, sizeof buf, bn->castle_have, nb, NULL);
        town_text_add(&t, buf, PAL_CLR(WHITE));
        if (page == MC_RECRUIT) {
            int m = GameMaxRecruitable(g, pt->id);
            if (m < 0) m = 0;
            if (pt->recruit_cost > 0 && g->stats.gold / pt->recruit_cost < m)
                m = g->stats.gold / pt->recruit_cost;
            snprintf(nb, sizeof nb, "%d", m);
            castle_fmt(buf, sizeof buf, bn->castle_can_recruit, nb, NULL);
            town_text_add(&t, buf, PAL_CLR(WHITE));
        } else {
            snprintf(nb, sizeof nb, "%d", in_garrison);
            castle_fmt(buf, sizeof buf, bn->castle_in_garrison, nb, NULL);
            town_text_add(&t, buf, PAL_CLR(WHITE));
            int moving = stepper ? step_v : in_garrison;
            if (page == MC_WITHDRAW &&
                GameArmyTotalLeadership(g) + pt->hit_points * moving > g->stats.leadership_current) {
                town_text_gap(&t);
                town_text_add(&t, bn->castle_over_leadership, PAL_CLR(YELLOW));
            }
        }
    } else if (rows == 1 && (page == MC_GARRISON || page == MC_WITHDRAW)) {
        town_text_add(&t, bn->castle_no_troops, PAL_CLR(WHITE));
    }

    int ty = low + INSET;
    int per = (lh - 2 * INSET) / line_h - (stepper ? 3 : 0);
    for (int i = 0; i < t.n && i < per; i++) {
        const TownLine *l = &t.line[i];
        if (l->label > 0) {
            char lab[72];
            snprintf(lab, sizeof lab, "%.*s", l->label, l->text);
            bfont_draw(lab, dx, ty, PAL_CLR(YELLOW));
            bfont_draw(l->text + l->label, dx + (int)bfont_measure(lab).x, ty, l->fg);
        } else {
            bfont_draw(l->text, dx, ty, l->fg);
        }
        ty += line_h;
    }

    // The count stepper, along the bottom of the panel:
    //   Cost: N gold                      (recruiting)
    //   <<  <   20 of 50   >  >>
    // Left/Right step one, Down/Up ten, Enter moves, Esc cancels; each arrow
    // is a tap target for the key it stands for.
    if (stepper) {
        int sy = bottom - INSET - 2 * GH - 4;
        if (page == MC_RECRUIT && pt) {
            snprintf(nb, sizeof nb, "%d", pt->recruit_cost * step_v);
            castle_fmt(buf, sizeof buf, bn->castle_cost, nb, NULL);
            bfont_draw(buf, dx, sy - line_h, PAL_CLR(WHITE));
        }
        snprintf(nb, sizeof nb, "%d", step_v);
        snprintf(mb, sizeof mb, "%d", step_max);
        castle_fmt(buf, sizeof buf, bn->castle_count_of, nb, mb);
        ml_stepper_draw(dx, sy, dw, buf);
    }
}

// =============================================================================
//  Modern foe view: a hostile band on the map (the town layout)
// =============================================================================
// Title "Foes!" and the zone; the plains backdrop at 2x with the band's lead
// troop standing on it; that troop at 2x in the portrait slot; the siege and
// gold tiles; rows Fight and Evade (Evade grey when there is nowhere to run);
// the encounter text beside them. The prompt behind it (src/prompt.c) owns the
// answer: Fight is Yes, Evade is No.

static bool foe_row(void *ctx, int i, char *label, char *right, int cap) {
    const Game *g = (const Game *)ctx;
    const ResBanners *bn = &g->res->banners;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", i == 0 ? bn->foe_fight : bn->foe_evade);
    return i == 0 || !pending_foe_evade_blocked;
}

void modern_overlay_draw_foe(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const PromptView *pv = prompt_view();
    const int BS = 2, BAND = 4;
    int pad = ML_PAD;
    ML_Rect r = ml_full();
    int right = r.x + r.w, bottom = r.y + r.h;
    DrawRectangle(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));

    int title_h = GH + 14;
    bfont_draw(res->ui.dt_foes, r.x + pad, r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    const ResZone *z = resources_zone_by_id(res, g->position.zone);
    if (z && z->name[0])
        bfont_draw(z->name, right - pad - (int)bfont_measure(z->name).x,
                   r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    lattice_band_h(r.x, r.y + title_h, r.w, BAND);

    // The band's lead troop: the first stack of its garrison.
    const FoeState *f = pending_foe_id[0] ? GameFindFoeConst(g, pending_foe_id) : NULL;
    const TroopDef *lead = NULL;
    for (int i = 0; f && i < GAME_ARMY_SLOTS && !lead; i++)
        if (f->garrison[i].id[0] && f->garrison[i].count > 0) lead = troop_by_id(f->garrison[i].id);

    int top = r.y + title_h + BAND;
    int fs = CL_TILE_W * BS;
    int bw = ML_BACKDROP_W * BS, bh = fs;
    Texture2D bd = loc_texture(s, LOC_PLAINS);
    if (bd.id && bd.height > 0) {
        float src_h = (float)bd.height * (float)bh / (float)(ML_BACKDROP_H * BS);
        Rectangle src = { 0, (float)bd.height - src_h, (float)bd.width, src_h };
        Rectangle dst = { (float)r.x, (float)top, (float)bw, (float)bh };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(r.x, top, bw, bh, PAL_CLR(BLACK));
    }
    Texture2D lt = { 0 };
    if (lead && s) {
        lt = s->troop_anim[lead->index][sprites_frame(sprites_stand((int)(GetTime() * 6.66)),
                                                      s->troop_anim_frames[lead->index])];
        if (!lt.id) lt = s->troop_sprite[lead->index];
    }
    if (lt.id) ui_blit(lt, r.x + CL_TILE_W / 2, top + bh - CL_TILE_H, CL_TILE_W, CL_TILE_H);
    lattice_band_v(r.x + bw, top, BAND, bh);

    int fx = r.x + bw + BAND;
    DrawRectangle(fx, top, fs, fs, PAL_CLR(BLACK));
    if (lt.id) ui_blit(lt, fx, top, fs, fs);
    lattice_band_v(fx + fs, top, BAND, bh);
    int hx = fx + fs + BAND;
    hud_draw_siege_tile(g, s, hx, top);
    hud_draw_gold_tile(g, s, hx, top + CL_TILE_H);

    int low = top + bh;
    lattice_band_h(r.x, low, r.w, BAND);
    low += BAND;
    int mw = 16 * GW, lh = bottom - low;
    ml_list_draw(r.x, low, mw, lh, 2, pv ? pv->yn_cursor : 0, foe_row, (void *)g,
                 TOUCH_LIST_PROMPT, PAL_CLR(DBLUE));
    lattice_band_v(r.x + mw, low, BAND, lh);

    // The encounter text (the prompt body: "You encounter:" and the stacks).
    const int INSET = pad + 4;
    int dx = r.x + mw + BAND + INSET, dw = right - dx - INSET;
    TownText t = { .n = 0, .max_w = dw };
    town_text_add(&t, prompt_body_text(), PAL_CLR(WHITE));
    if (pending_foe_evade_blocked) {
        town_text_gap(&t);
        town_text_add(&t, bn->foe_evade_blocked, PAL_CLR(YELLOW));
    }
    int ty = low + INSET;
    for (int i = 0; i < t.n && ty + GH <= bottom - INSET; i++) {
        bfont_draw(t.line[i].text, dx, ty, t.line[i].fg);
        ty += GH + 2;
    }
}

// ---------------------------------------------------------------------------
// Options screen (O key). A single text panel below the status bar
// listing every adventure keybinding. Matches options_menu() 
// (OpenKB's game.c:5144-5275). Esc or any key closes.
// ---------------------------------------------------------------------------

void modern_overlay_draw_options(const Game *g) {
    // : movement-reference rows on top
    // (8 direction keys + numpad equivalents), then the lettered command
    // list below. One unified blue panel below the status bar.
    int pad = 3 * CL_UI;
    // The list decides the panel. Movement rows on top, then the keybinds;
    // when they will not fit the pane in one column they go in two.
    int kb_n = (g && g->res) ? g->res->ui.keybind_count : 0;
    int fit = (CL_MAP_H - 2 * pad) / GH - 8 - 1;
    int kb_cols = (kb_n > fit) ? 2 : 1;
    int kb_rows = (kb_n + kb_cols - 1) / kb_cols;
    int rows = 8 + 1 + kb_rows;
    int w = (kb_cols == 2) ? CL_PANEL_WIDE_W : CL_PANEL_STD_W;
    int h = rows * GH + 2 * pad;
    if (h > CL_MAP_H) h = CL_MAP_H;
    // Anchored to the content rect, not the pane. The panel is a fixed 28
    // columns, so on a wide pane the pane's left edge strands it in the
    // corner. The content rect IS the pane in legacy, so this stays at 16.
    int x = CL_CONTENT_X;
    int y = CL_STATUS_Y + CL_STATUS_H + CL_BAR_H;

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad;
    int ty = y + pad;

    // Movement reference.
    static const struct { const char *keys; const char *label; } mv[8] = {
        { "\x18 or 2",   "Move Down"      },  // arrow-glyph codepoint, plus numpad 2
        { "\x1B or 4",   "Move Left"      },
        { "\x1A or 6",   "Move Right"     },
        { "\x19 or 8",   "Move Up"        },
        { "END or 1",    "Down Left"      },
        { "PGDN or 3",   "Down Right"     },
        { "HOME or 7",   "Up Left"        },
        { "PGUP or 9",   "Up Right"       },
    };
    for (int i = 0; i < 8; i++) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%-10s %s", mv[i].keys, mv[i].label);
        bfont_draw(buf, tx, ty, PAL_CLR(WHITE));
        ty += GH;
    }

    // Items are laid out as "<key label>  <item name>" per line, matching
    // options_menu rendering order. Source: res.ui.keybinds.
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    int n = ui ? ui->keybind_count : 0;
    int max_row = (y + h - pad - ty) / GH;
    if (kb_cols == 1 && n > max_row) n = max_row;

    int col_w = (w - 2 * pad) / kb_cols;
    int per_col = (n + kb_cols - 1) / kb_cols;
    int ty0 = ty;
    for (int i = 0; i < n; i++) {
        const ResKeybind *kb = &ui->keybinds[i];
        // Skip mount-conditional entries if they don't apply.
        if (strcmp(kb->key, "F") == 0 && g->character.mount == MOUNT_FLY) continue;
        if (strcmp(kb->key, "L") == 0 && g->character.mount != MOUNT_FLY) continue;
        if (strcmp(kb->key, "N") == 0 && g->character.mount != MOUNT_SAIL) continue;
        char buf[48];
        snprintf(buf, sizeof(buf), "%-4s %s", kb->key, kb->label);
        int cx = tx + (kb_cols == 2 ? (i / per_col) * col_w : 0);
        int cy = kb_cols == 2 ? ty0 + (i % per_col) * GH : ty;
        bfont_draw(buf, cx, cy, PAL_CLR(WHITE));
        if (kb_cols == 1) ty += GH;
    }
}

// ---------------------------------------------------------------------------
// Toast -- small strip at the top of the map area.
// ---------------------------------------------------------------------------

void modern_overlay_draw_toast(void) {
    const char *msg = toast_text_current();
    if (!msg) return;
    Vector2 m = bfont_measure(msg);
    int w = (int)m.x + 8;
    int h = GH + 4;
    int x = CL_MAP_X + (CL_MAP_W - w) / 2;
    int y = CL_MAP_Y + 2;
    draw_panel(x, y, w, h, PAL_CLR(BLACK));
    bfont_draw(msg, x + 4, y + 2, PAL_CLR(YELLOW));
}

// ---------------------------------------------------------------------------
// Controls settings panel .
// ---------------------------------------------------------------------------

typedef struct { const Game *g; int vis_idx[8]; int vis; } ControlsCtx;

static bool controls_row(void *ctx, int k, char *label, char *right, int cap) {
    const ControlsCtx *c = (const ControlsCtx *)ctx;
    const Game *g = c->g;
    const ResUI *ui = &g->res->ui;
    if (k == c->vis) {   // the shell's Scale row
        snprintf(label, (size_t)cap, "Scale");
        snprintf(right, 48, "%dx", views_controls_scale_value());
        return true;
    }
    int i = c->vis_idx[k];
    snprintf(label, (size_t)cap, "%s", g->res->controls.items[i].label);
    int val = g->stats.options[i];
    if (strcmp(g->res->controls.items[i].type, "bool") == 0)
        snprintf(right, 48, "%s", val == 1 ? ui->controls_on : ui->controls_off);
    else
        snprintf(right, 48, "%d", val);
    return !views_controls_row_disabled(g, i);
}

void modern_overlay_draw_controls(const Game *g) {
    if (!g || !g->res) return;
    int count = g->res->controls.count;

    // Visible settings: skip anything marked hidden (CGA in our data), then the
    // shell's Scale row. The cursor counts visible rows (main.c).
    ControlsCtx c = { .g = g, .vis = 0 };
    for (int i = 0; i < count && c.vis < 8; i++) {
        if (g->res->controls.items[i].hidden) continue;
        c.vis_idx[c.vis++] = i;
    }
    if (c.vis == 0) return;
    int cur_k = views_controls_cursor();
    if (cur_k < 0) cur_k = 0;
    if (cur_k > c.vis) cur_k = c.vis;

    // The large layout, the same rect as the game menu it opens from
    // (REQ-430j), its settings as standard select rows with the value at the
    // right (REQ-430n).
    ML_Rect lr = ml_large();
    draw_panel(lr.x, lr.y, lr.w, lr.h, PAL_CLR(DBLUE));
    const ResUI *ui = &g->res->ui;
    int ty = lr.y + ML_PAD;
    bfont_draw(ui->controls_title, lr.x + ML_PAD, ty, PAL_CLR(YELLOW));
    ty += GH + ML_PAD;
    lattice_band_h(lr.x, ty, lr.w, 4);
    ty += 4;
    int rows = c.vis + 1;
    int h = lr.y + lr.h - ty;
    ml_list_draw(lr.x, ty, lr.w, h, rows, cur_k, controls_row, &c, 0, PAL_CLR(DBLUE));
    // Taps answer to each row's digit (select and advance in one), as before.
    int vis_rows = ml_list_fit(h);
    int first = ml_list_first(rows, cur_k, vis_rows);
    for (int k = first; k < rows && k < first + vis_rows; k++)
        touch_region(lr.x, ty + (k - first) * (ml_row_h() + ML_ROW_RULE), lr.w, ml_row_h(),
                     KEY_ONE + k);
}

// ---------------------------------------------------------------------------
// The dimmed scene beneath a detail view, prompt or dialog (REQ-430g).
// ---------------------------------------------------------------------------

void modern_overlay_dim_scene(void) {
    const Resources *r = resources_current();
    int a = overlay_dim_alpha(r ? r->render.dim : 0);
    if (a == 0) return;
    // The whole chrome interior: map pane plus sidebar, not the status band
    // or the frame, so the frame keeps its weight and the band stays legible.
    Color shade = { 0, 0, 0, (unsigned char)a };
    DrawRectangle(CL_MAP_X, CL_MAP_Y, CL_SIDEBAR_X + CL_SIDEBAR_W - CL_MAP_X, CL_MAP_H, shade);
}

// =============================================================================
//  Temple (the Augur's alcove) and dwelling screens -- their own layouts, not
//  the town's. Drawn over the location view while its Yes/No or count prompt,
//  or the answer that follows it, is up.
// =============================================================================

// The title strip every full screen opens with: title left, zone right.
static int loc_title_strip(const Game *g, ML_Rect r, const char *title) {
    int title_h = GH + 14, pad = ML_PAD;
    bfont_draw(title, r.x + pad, r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    const ResZone *z = resources_zone_by_id(g->res, g->position.zone);
    if (z && z->name[0])
        bfont_draw(z->name, r.x + r.w - pad - (int)bfont_measure(z->name).x,
                   r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    lattice_band_h(r.x, r.y + title_h, r.w, 4);
    return r.y + title_h + 4;
}

typedef struct { const char *label[2]; bool enabled[2]; } LocRows;

static bool loc_row(void *ctx, int i, char *label, char *right, int cap) {
    const LocRows *lr = (const LocRows *)ctx;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", lr->label[i]);
    return lr->enabled[i];
}

// Rows on the left, wrapped text on the right, under `low`.
static void loc_lower(ML_Rect r, int low, LocRows *rows, int nrows, int cursor,
                      TownText *t, int dx) {
    int bottom = r.y + r.h, mw = 16 * GW, lh = bottom - low;
    ml_list_draw(r.x, low, mw, lh, nrows, cursor, loc_row, rows, TOUCH_LIST_PROMPT, PAL_CLR(DBLUE));
    lattice_band_v(r.x + mw, low, 4, lh);
    int ty = low + ML_PAD + 4;
    for (int i = 0; i < t->n && ty + GH <= bottom - ML_PAD; i++) {
        bfont_draw(t->line[i].text, dx, ty, t->line[i].fg);
        ty += GH + 2;
    }
}

void modern_overlay_draw_temple(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    ML_Rect r = ml_full();
    DrawRectangle(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));
    int top = loc_title_strip(g, r, bn->temple_title);

    // The precinct across the full width at the largest whole scale that fits
    // (3 on the standard screen), centred; the Augur where the pack places him.
    int S = r.w / ML_BACKDROP_W;
    if (S > 3) S = 3;
    if (S < 1) S = 1;
    int bw = ML_BACKDROP_W * S, bh = ML_BACKDROP_H * S;
    int bx = r.x + (r.w - bw) / 2;
    Texture2D bd = loc_texture(s, LOC_ALCOVE);
    if (bd.id) ui_blit(bd, bx, top, bw, bh);
    else       DrawRectangle(bx, top, bw, bh, PAL_CLR(BLACK));
    if (s && s->alcove_figure.id && res->sprites.alcove_figure_w > 0) {
        int ms = res->sprites.alcove_figure_frame_ms > 0 ? res->sprites.alcove_figure_frame_ms : 180;
        Texture2D fig = s->alcove_figure_anim[sprites_frame((int)(GetTime() * 1000.0 / ms),
                                                            s->alcove_figure_frames)];
        if (!fig.id) fig = s->alcove_figure;
        ui_blit(fig, bx + res->sprites.alcove_figure_x * S, top + res->sprites.alcove_figure_y * S,
                res->sprites.alcove_figure_w * S, res->sprites.alcove_figure_h * S);
    }
    int low = top + bh;
    lattice_band_h(r.x, low, r.w, 4);
    low += 4;

    const int INSET = ML_PAD + 4;
    int dx = r.x + 16 * GW + 4 + INSET, dw = r.x + r.w - dx - INSET;
    TownText t = { .n = 0, .max_w = dw };
    LocRows rows = { { bn->temple_learn, bn->location_leave }, { true, true } };
    const PromptView *pv = prompt_view();
    if (prompt_is_active() && pending_flow == FLOW_ALCOVE) {
        // The offer, then its price and the purse.
        const ResZone *z = resources_zone_by_id(res, g->position.zone);
        char buf[RES_BANNER_LEN], cost[16];
        snprintf(cost, sizeof cost, "%d", GameAlcoveCost(g, g->position.zone));
        ResTemplateVar v[] = { { "ZONE", (z && z->name[0]) ? z->name : g->position.zone },
                               { "COST", cost } };
        resources_format_template(buf, sizeof buf, bn->alcove_offer_modern, v, 2);
        town_text_add(&t, buf, PAL_CLR(WHITE));
        town_text_gap(&t);
        castle_fmt(buf, sizeof buf, bn->castle_cost, cost, NULL);
        town_text_add(&t, buf, PAL_CLR(YELLOW));
        loc_lower(r, low, &rows, 2, pv->yn_cursor, &t, dx);
    } else {
        // The Augur's answer (taught, no gold, rites already known): Leave.
        town_text_add(&t, dialog_is_active() ? dialog_body_text() : "", PAL_CLR(WHITE));
        rows.label[0] = bn->location_leave;
        loc_lower(r, low, &rows, 1, 0, &t, dx);
    }
}

void modern_overlay_draw_dwelling(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const ResUI *ui = &res->ui;
    int ti = -1, pop = 0, cost = 0, cap = 0;
    const char *kind = screen_dwelling_info(g, &ti, &pop, &cost, &cap);
    const TroopDef *tr = (ti >= 0) ? troop_by_index(ti) : NULL;
    ML_Rect r = ml_full();
    DrawRectangle(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));
    char title[128];
    snprintf(title, sizeof title, "%s - %s", kind ? kind : "", tr ? tr->name : "");
    int top = loc_title_strip(g, r, title);

    // The dwelling at 2x (cut to the portrait's height) with the troop
    // standing in it; the troop's portrait at 2x beside it; the purse.
    const int BS = 2, BAND = 4;
    int fs = CL_TILE_W * BS, bw = ML_BACKDROP_W * BS, bh = fs;
    LocKind lk = LOC_PLAINS;
    if (kind && strcmp(kind, ui->dwelling_kind_forest) == 0)  lk = LOC_FOREST;
    if (kind && strcmp(kind, ui->dwelling_kind_hill) == 0)    lk = LOC_HILLCAVE;
    if (kind && strcmp(kind, ui->dwelling_kind_dungeon) == 0) lk = LOC_DUNGEON;
    Texture2D bd = loc_texture(s, lk);
    if (bd.id && bd.height > 0) {
        float src_h = (float)bd.height * (float)bh / (float)(ML_BACKDROP_H * BS);
        Rectangle src = { 0, (float)bd.height - src_h, (float)bd.width, src_h };
        DrawTexturePro(bd, src, (Rectangle){ (float)r.x, (float)top, (float)bw, (float)bh },
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(r.x, top, bw, bh, PAL_CLR(BLACK));
    }
    Texture2D ts = { 0 };
    if (tr && s) {
        ts = s->troop_anim[ti][sprites_frame(sprites_stand((int)(GetTime() * 6.66)),
                                             s->troop_anim_frames[ti])];
        if (!ts.id) ts = s->troop_sprite[ti];
    }
    if (ts.id) ui_blit(ts, r.x + CL_TILE_W / 2, top + bh - CL_TILE_H, CL_TILE_W, CL_TILE_H);
    lattice_band_v(r.x + bw, top, BAND, bh);
    int fx = r.x + bw + BAND;
    DrawRectangle(fx, top, fs, fs, PAL_CLR(BLACK));
    Texture2D pt = (tr && s && s->troop_portrait[ti].id) ? s->troop_portrait[ti] : ts;
    if (pt.id) ui_blit(pt, fx, top, fs, fs);
    lattice_band_v(fx + fs, top, BAND, bh);
    int hx = fx + fs + BAND;
    hud_draw_siege_tile(g, s, hx, top);
    hud_draw_gold_tile(g, s, hx, top + CL_TILE_H);
    int low = top + bh;
    lattice_band_h(r.x, low, r.w, BAND);
    low += BAND;

    const int INSET = ML_PAD + 4;
    int dx = r.x + 16 * GW + BAND + INSET, dw = r.x + r.w - dx - INSET;
    TownText t = { .n = 0, .max_w = dw };
    LocRows rows = { { bn->dwelling_recruit_row, bn->location_leave }, { cap > 0, true } };
    const PromptView *pv = prompt_view();
    char buf[RES_BANNER_LEN], nb[16];
    if (prompt_is_active() && pending_flow == FLOW_RECRUIT) {
        snprintf(nb, sizeof nb, "%d", pop);
        ResTemplateVar av[] = { { "COUNT", nb }, { "TROOP", tr ? tr->name : "" } };
        resources_format_template(buf, sizeof buf, ui->dwelling_info_available, av, 2);
        town_text_add(&t, buf, PAL_CLR(WHITE));
        snprintf(nb, sizeof nb, "%d", cost);
        castle_fmt(buf, sizeof buf, bn->castle_cost, nb, NULL);
        town_text_add(&t, buf, PAL_CLR(WHITE));
        town_text_gap(&t);
        if (cap > 0) {
            snprintf(nb, sizeof nb, "%d", cap);
            castle_fmt(buf, sizeof buf, bn->castle_can_recruit, nb, NULL);
            town_text_add(&t, buf, PAL_CLR(YELLOW));
        } else {
            // Why none: the purse, else leadership.
            town_text_add(&t, (tr && g->stats.gold < tr->recruit_cost) ? bn->town_no_gold
                                                                       : bn->army_cannot_handle,
                          PAL_CLR(YELLOW));
        }
        if (pv->step_open) {
            // The stepper replaces the rows while it is up.
            int mw = 16 * GW, bottom = r.y + r.h;
            lattice_band_v(r.x + mw, low, BAND, bottom - low);
            char sv[32], sm[16];
            snprintf(sv, sizeof sv, "%d", pv->step_value);
            snprintf(sm, sizeof sm, "%d", pv->step_max);
            castle_fmt(buf, sizeof buf, bn->castle_count_of, sv, sm);
            ml_stepper_draw(r.x + ML_PAD, low + ML_PAD, mw - 2 * ML_PAD, buf);
            int ty = low + ML_PAD + 4;
            for (int i = 0; i < t.n && ty + GH <= bottom - ML_PAD; i++) {
                bfont_draw(t.line[i].text, dx, ty, t.line[i].fg);
                ty += GH + 2;
            }
        } else {
            loc_lower(r, low, &rows, 2, pv->yn_cursor, &t, dx);
        }
    } else {
        town_text_add(&t, dialog_is_active() ? dialog_body_text() : "", PAL_CLR(WHITE));
        rows.label[0] = bn->location_leave;
        rows.enabled[0] = true;
        loc_lower(r, low, &rows, 1, 0, &t, dx);
    }
}
