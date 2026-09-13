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
}

// ---------------------------------------------------------------------------
// Game menu (nested, cursor-driven).
// ---------------------------------------------------------------------------

void modern_overlay_draw_menu(void) {
    const char *title = views_menu_title();
    int count = views_menu_entry_count();
    int cursor = views_menu_cursor();

    // The large layout (REQ-430j): the title, then one row per entry.
    ML_Rect r = ml_large();
    int row_h = GH + 2;
    draw_panel(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));

    int tx = r.x + ML_PAD;
    int ty = r.y + ML_PAD;
    if (title) {
        bfont_draw_centered(title, r.x + r.w / 2, ty, PAL_CLR(YELLOW));
        ty += row_h + row_h / 2;
    }

    for (int i = 0; i < count; i++) {
        const char *label = views_menu_entry_label(i);
        if (!label) continue;
        if (ty + row_h > r.y + r.h - ML_PAD) break;       // never past the panel
        bool sel = (i == cursor);
        Color fg = sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        char buf[64];
        if (views_menu_entry_is_submenu(i)) snprintf(buf, sizeof buf, "%s >", label);
        else                                snprintf(buf, sizeof buf, "%s", label);
        sel_row(r.x + ML_PAD / 2, ty, r.w - ML_PAD, row_h, tx, buf, sel, fg,
                PAL_CLR(DBLUE), TOUCH_LIST_MENU, i);
        ty += row_h;
    }
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
// can be split into pages.
#define TOWN_DETAIL_LINES 64
typedef struct {
    char  text[72];
    Color fg;
} TownLine;
typedef struct {
    TownLine line[TOWN_DETAIL_LINES];
    int      n;
    int      max_w;
} TownText;

static void town_text_add(TownText *t, const char *text, Color fg) {
    const char *p = text ? text : "";
    if (!*p) return;
    while (*p && t->n < TOWN_DETAIL_LINES) {
        TownLine *l = &t->line[t->n];
        if (bfont_take_line(&p, t->max_w, l->text, (int)sizeof l->text) <= 0) break;
        l->fg = fg;
        t->n++;
    }
}

static void town_text_gap(TownText *t) {
    if (t->n > 0 && t->n < TOWN_DETAIL_LINES) t->line[t->n++].text[0] = '\0';
}

// Facts about the cursor row, from the game state.
static void town_compose_detail(const Game *g, int row, TownText *t) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const ResUI *ui = &res->ui;
    char buf[RES_BANNER_LEN];

    if (row == TOWN_ROW_CONTRACT) {
        const VillainDef *v = g->contract.active_id[0]
                            ? villain_by_id(g->contract.active_id) : NULL;
        if (!v) { town_text_add(t, ui->cv_title_no_contract, PAL_CLR(WHITE)); return; }
        const ResVillainDesc *d = resources_villain_desc(res, v->id);
        char val[RES_VDESC_TEXT_LEN];
        ResTemplateVar vars[] = { { "VALUE", val } };
        snprintf(val, sizeof val, "%s", v->name);
        resources_format_template(buf, sizeof buf, ui->cv_label_name, vars, 1);
        town_text_add(t, buf, PAL_CLR(YELLOW));
        if (d && d->alias[0]) {
            snprintf(val, sizeof val, "%s", d->alias);
            resources_format_template(buf, sizeof buf, ui->cv_label_alias, vars, 1);
            town_text_add(t, buf, PAL_CLR(WHITE));
        }
        snprintf(val, sizeof val, "%d", v->reward);
        resources_format_template(buf, sizeof buf, ui->cv_label_reward, vars, 1);
        town_text_add(t, buf, PAL_CLR(WHITE));
        const ResZone *z = resources_zone_by_id(res, v->zone);
        snprintf(val, sizeof val, "%s", (z && z->name[0]) ? z->name : v->zone);
        resources_format_template(buf, sizeof buf, ui->cv_label_last_seen, vars, 1);
        town_text_add(t, buf, PAL_CLR(WHITE));
        snprintf(val, sizeof val, "%s", ui->cv_castle_unknown);
        for (int i = 0; i < GAME_CASTLES; i++) {
            const CastleRecord *c = &g->castles[i];
            if (!c->known || strcmp(c->villain_id, v->id) != 0) continue;
            const ResCastle *rc = resources_castle_by_id(res, c->id);
            snprintf(val, sizeof val, "%s", (rc && rc->name[0]) ? rc->name : c->id);
            break;
        }
        resources_format_template(buf, sizeof buf, ui->cv_label_castle, vars, 1);
        town_text_add(t, buf, PAL_CLR(WHITE));
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
        return;
    }

    // Every other row leads with the pack's full row text, prices included.
    char head[128];
    views_town_row_text(g, row, head, sizeof head);
    if (head[0] >= 'A' && head[0] <= 'Z' && head[1] == ')' && head[2] == ' ')
        memmove(head, head + 3, strlen(head + 3) + 1);
    town_text_add(t, head, PAL_CLR(YELLOW));

    const char *key = views_town_record_key();
    const ResTown *tw = key ? resources_town_by_id(res, key) : NULL;
    if (row == TOWN_ROW_BOAT && tw && tw->boat_x >= 0 && tw->boat_y >= 0) {
        char bx[12], by[12];
        snprintf(bx, sizeof bx, "%d", tw->boat_x);
        snprintf(by, sizeof by, "%d", tw->boat_y);
        ResTemplateVar vars[] = { { "X", bx }, { "Y", by } };
        resources_format_template(buf, sizeof buf, bn->town_detail_boat_dock, vars, 2);
        town_text_gap(t);
        town_text_add(t, buf, PAL_CLR(WHITE));
    } else if (row == TOWN_ROW_INFO && tw && tw->intel_castle[0]) {
        const ResCastle *rc = resources_castle_by_id(res, tw->intel_castle);
        ResTemplateVar vars[] = { { "CASTLE", (rc && rc->name[0]) ? rc->name
                                                                  : tw->intel_castle } };
        resources_format_template(buf, sizeof buf, bn->town_detail_intel, vars, 1);
        town_text_gap(t);
        town_text_add(t, buf, PAL_CLR(WHITE));
    } else if (row == TOWN_ROW_SPELL && key) {
        for (int i = 0; i < GAME_TOWNS; i++) {
            if (strcmp(g->towns[i].id, key) != 0) continue;
            const SpellDef *sp = g->towns[i].spell_for_sale[0]
                               ? spell_by_id(g->towns[i].spell_for_sale) : NULL;
            if (sp && sp->description[0]) {
                town_text_gap(t);
                town_text_add(t, sp->description, PAL_CLR(WHITE));
            }
            break;
        }
    }
}

void modern_overlay_draw_town(const Game *g, const Sprites *s) {
    const char *name = views_town_display_name();
    const char *info = views_town_info_text();
    int rows = views_town_row_count();
    int cursor = views_town_cursor();

    // Town view doesn't own a SYN-tick frame counter yet; derive a
    // free-running tick from real time at the SYN cadence (~150ms per
    // frame -> 6.7fps).
    int troop_idx = town_backdrop_troop(g, name);
    int town_frame = (int)(GetTime() * 6.66);

    // Full screen: the pane, the band and the HUD, tiled exactly, the boxes
    // split by the lattice. On Rome's 776x480:
    //   title 776x22
    //   backdrop 480x204 (2x) | face 192x204 (2x + dots) | siege, gold 96x204
    //   menu 256x246 (5 rows)  | detail 516x246
    const int BS = 2;
    const int BAND = 4;
    int pad = ML_PAD;
    ML_Rect r = ml_full();
    int right = r.x + r.w, bottom = r.y + r.h;
    DrawRectangle(r.x, r.y, r.w, r.h, PAL_CLR(DBLUE));

    // Title strip.
    int title_h = GH + 6;
    const ResBanners *bn = (g && g->res) ? &g->res->banners : NULL;
    char header[96] = "";
    if (bn) {
        ResTemplateVar hv[] = { { "NAME", (name && name[0]) ? name : "" } };
        resources_format_template(header, sizeof header, bn->town_header, hv, 1);
    }
    bfont_draw(header, r.x + pad, r.y + (title_h - GH) / 2, PAL_CLR(YELLOW));
    lattice_band_h(r.x, r.y + title_h, r.w, BAND);

    // Top row: backdrop at 2x with the troop at 1x on its bottom edge.
    int top = r.y + title_h + BAND;
    int bw = ML_BACKDROP_W * BS, bh = ML_BACKDROP_H * BS;
    Texture2D bd = loc_texture(s, LOC_TOWN);
    if (bd.id) ui_blit(bd, r.x, top, bw, bh);
    else       DrawRectangle(r.x, top, bw, bh, PAL_CLR(BLACK));
    if (s && troop_idx >= 0 && troop_idx < 25) {
        Texture2D ts = s->troop_anim[troop_idx][sprites_frame(sprites_stand(town_frame),
                                                s->troop_anim_frames[troop_idx])];
        if (!ts.id) ts = s->troop_sprite[troop_idx];
        if (ts.id) ui_blit(ts, r.x + CL_TILE_W / 2, top + bh - CL_TILE_H,
                           CL_TILE_W, CL_TILE_H);
    }
    lattice_band_v(r.x + bw, top, BAND, bh);

    // The wanted face at 2x (or the empty silhouette), and under it one dot
    // per contract in the rotation, the current one filled.
    int fs = CL_TILE_W * BS;
    int fx = r.x + bw + BAND;
    const VillainDef *v = (g && g->contract.active_id[0])
                        ? villain_by_id(g->contract.active_id) : NULL;
    if (v && s && v->index >= 0 && v->index < 17) {
        Texture2D face = s->villain_anim[v->index][sprites_frame(
            (int)(GetTime() * 2.0), s->villain_anim_frames[v->index])];
        if (!face.id) face = s->villain_portrait[v->index];
        ui_blit(face, fx, top, fs, fs);
    } else if (s && s->hud_contract_silhouette.id) {
        ui_blit(s->hud_contract_silhouette, fx, top, fs, fs);
    }
    int n = (g && g->res) ? g->res->contract.cycle_length : 0;
    if (v && n > 1) {
        const int DOT = 3, STEP = 12;
        int dy = top + fs + (bh - fs) / 2;
        int dx0 = fx + fs / 2 - (n - 1) * STEP / 2;
        for (int i = 0; i < n; i++) {
            if (i == g->contract.last_contract)
                DrawCircle(dx0 + i * STEP, dy, DOT + 1, PAL_CLR(YELLOW));
            else
                DrawCircleLines(dx0 + i * STEP, dy, DOT, PAL_CLR(YELLOW));
        }
    }
    lattice_band_v(fx + fs, top, BAND, bh);

    // The HUD's own siege weapons tile over its gold purse.
    int hx = fx + fs + BAND;
    int hgap = (bh - 2 * CL_TILE_H) / 3;
    hud_draw_siege_tile(g, s, hx, top + hgap);
    hud_draw_gold_tile(g, s, hx, top + 2 * hgap + CL_TILE_H);

    int low = top + bh;
    lattice_band_h(r.x, low, r.w, BAND);
    low += BAND;

    // Menu (left): rows fill the height, split by a thin rail.
    const int RULE = 2;
    int mw = 16 * GW;
    int lh = bottom - low;
    int rh = rows > 0 ? (lh - (rows - 1) * RULE) / rows : lh;
    bool live = !(info && info[0]);
    for (int i = 0; i < rows; i++) {
        char label[64];
        views_town_menu_label(g, i, label, sizeof label);
        bool enabled = views_town_row_enabled(g, i);
        bool sel = enabled && i == cursor;
        Color fg = !enabled ? PAL_CLR(DGREY) : sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        int ry = low + i * (rh + RULE);
        int h = (i == rows - 1) ? bottom - ry : rh;
        // The cursor row inverted; a tap on an enabled row selects and confirms
        // it (views_town_update reads TOUCH_LIST_TOWN).
        sel_row(r.x, ry, mw, h, r.x + pad, label, sel, fg, PAL_CLR(DBLUE),
                (live && enabled) ? TOUCH_LIST_TOWN : 0, i);
        if (i > 0) lattice_band_h(r.x, ry - RULE, mw, RULE);
    }
    lattice_band_v(r.x + mw, low, BAND, lh);

    // Detail (right): the cursor row's facts, or a result while one is up,
    // paged with Left/Right when it runs long.
    int dx = r.x + mw + BAND + pad;
    int dw = right - dx - pad;
    int line_h = GH + 2;
    int lines_per_page = (lh - 2 * pad) / line_h;
    TownText t = { .n = 0, .max_w = dw };
    if (!live) town_text_add(&t, info, PAL_CLR(WHITE));
    else       town_compose_detail(g, cursor, &t);
    int pages = 1;
    if (t.n > lines_per_page && lines_per_page > 1) {
        lines_per_page -= 1;                       // the last line holds the pager
        pages = (t.n + lines_per_page - 1) / lines_per_page;
    }
    views_town_set_detail_pages(pages);
    int page = views_town_detail_page();
    int ty = low + pad;
    for (int i = page * lines_per_page; i < t.n && i < (page + 1) * lines_per_page; i++) {
        bfont_draw(t.line[i].text, dx, ty, t.line[i].fg);
        ty += line_h;
    }
    if (pages > 1) {
        // "1/2" between the arrows, bottom right; an arrow shows only where
        // there is a page to go to, and a tap on it pages.
        char pg[32];
        snprintf(pg, sizeof pg, "%d/%d", page + 1, pages);
        int py = bottom - pad - GH;
        int aw = GH;
        int nx = right - pad - aw;
        int tx = nx - pad - (int)bfont_measure(pg).x;
        int px = tx - pad - aw;
        bfont_draw(pg, tx, py, PAL_CLR(YELLOW));
        if (page > 0) {
            DrawTriangle((Vector2){ px + aw, py }, (Vector2){ px, py + GH / 2 },
                         (Vector2){ px + aw, py + GH }, PAL_CLR(YELLOW));
            touch_region(px, py, aw, GH, KEY_LEFT);
        }
        if (page + 1 < pages) {
            DrawTriangle((Vector2){ nx, py }, (Vector2){ nx, py + GH },
                         (Vector2){ nx + aw, py + GH / 2 }, PAL_CLR(YELLOW));
            touch_region(nx, py, aw, GH, KEY_RIGHT);
        }
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

void modern_overlay_draw_controls(const Game *g) {
    if (!g || !g->res) return;
    int count = g->res->controls.count;
    int cursor = views_controls_cursor();
    if (cursor >= count) cursor = count ? count - 1 : 0;

    // Visible settings: skip anything marked hidden (CGA in our data).
    int vis_idx[8];
    int vis = 0;
    for (int i = 0; i < count && vis < 8; i++) {
        if (g->res->controls.items[i].hidden) continue;
        vis_idx[vis++] = i;
    }
    if (vis == 0) return;

    // The large layout, the same rect as the game menu it opens from
    // (REQ-430j), so Controls reads as a page of that menu.
    ML_Rect lr = ml_large();
    int pad = ML_PAD;
    int w = lr.w, h = lr.h, x = lr.x, y = lr.y;

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad;
    int ty = y + pad;

    // Title row (highlighted, centered-ish).
    const ResUI *ui_ctl = (g && g->res) ? &g->res->ui : NULL;
    bfont_draw(ui_ctl ? ui_ctl->controls_title : " Controls ",
               tx, ty, PAL_CLR(YELLOW));
    ty += GH + 2;

    for (int k = 0; k < vis; k++) {
        int i = vis_idx[k];
        bool is_selected = (i == cursor);
        bool disabled    = views_controls_row_disabled(g, i);
        Color fg;
        if (disabled) {
            fg = PAL_CLR(DGREY);
        } else {
            fg = is_selected ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        }

        char label[48];
        snprintf(label, sizeof(label), "%c %s",
                 '1' + k, g->res->controls.items[i].label);
        Color vfg = fg;   // value colour; inverted with the row
        sel_row(x, ty, w, GH + 2, tx, label, is_selected && !disabled, fg, PAL_CLR(DBLUE), 0, 0);
        if (is_selected && !disabled) vfg = PAL_CLR(DBLUE);
        // Touch: rows answer to their digit (select + advance in one).
        touch_region(x, ty, w, GH + 2, KEY_ONE + k);

        int val = g->stats.options[i];
        const char *type = g->res->controls.items[i].type;
        if (strcmp(type, "bool") == 0) {
            const char *text = (val == 1) ? ui_ctl->controls_on
                                          : ui_ctl->controls_off;
            int tw = (int)bfont_measure(text).x;
            bfont_draw(text, x + w - pad - tw, ty, vfg);
        } else {
            int range = g->res->controls.items[i].range;
            if (range > 10) range = 10;
            int sx = x + w - pad - range * GW;
            for (int n = 0; n < range; n++) {
                char buf[2] = { (char)('0' + n), 0 };
                Color nc;
                if (disabled) nc = PAL_CLR(DGREY);
                else if (vfg.r == PAL_CLR(DBLUE).r && vfg.g == PAL_CLR(DBLUE).g && vfg.b == PAL_CLR(DBLUE).b)
                                nc = (n == val) ? PAL_CLR(WHITE) : PAL_CLR(DBLUE);   // inverted row: the set digit stands out
                else            nc = (n == val) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
                bfont_draw(buf, sx + n * GW, ty, nc);
            }
        }
        ty += GH + 2;
    }

    // Scale: appended by the shell, not part of the pack's controls. Backed by
    // present.c rather than stats.options[], because a display preference must
    // not travel inside a save file. There is no legacy counterpart: a legacy
    // pack looks exactly as it did before render modes existed.
    {
        bool is_selected = (cursor == vis);
        Color fg = is_selected ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        char label[48];
        snprintf(label, sizeof(label), "%c Scale", '1' + vis);
        sel_row(x, ty, w, GH + 2, tx, label, is_selected, fg, PAL_CLR(DBLUE), 0, 0);
        touch_region(x, ty, w, GH + 2, KEY_ONE + vis);

        int sc = views_controls_scale_value();
        char val[16];
        snprintf(val, sizeof(val), "%dx", sc);
        int vw = (int)bfont_measure(val).x;
        bfont_draw(val, x + w - pad - vw, ty, is_selected ? PAL_CLR(DBLUE) : fg);
    }
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
