// src/modern/overlay.c
//
// The overlay for a pack that declared render.mode "modern": square tiles, a
// TrueType face, the inverted cursor row (REQ-430e) and the dimmed scene
// beneath (REQ-430g). Every panel here is built from the shared kit
// (src/modern/uikit.h): place screens are a scene with rows, and what a place
// offers opens as an in-lay over it.
//
// The DOS original's overlay is in src/legacy/overlay.c and is frozen: it must
// not be edited to serve anything in this file.
//
// Called only through the dispatcher in src/overlay.c.

#include "overlay.h"
#include "overlay_impl.h"
#include "modern/mlayout.h"
#include "modern/uikit.h"
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
#include "player_io.h"
#include "spells_adventure.h"
#include "modern/castle.h"
#include "modern/mlist.h"
#include "modern/location.h"
#include "shell_audience.h"
#include "screens/dwelling.h"
#include <stdio.h>
#include <string.h>

#define GW  BFONT_GLYPH_W
#define GH  BFONT_GLYPH_H

// =============================================================================
//  Messages
// =============================================================================

// Wrapped line count of `text` at `max_w`, as bfont_take_line breaks it.
static int wrapped_lines(const char *text, int max_w) {
    return uk_lines(text, max_w);
}

// Which layout a message uses and how it pages. One function, called by both
// the pager and the panel, so the two can never disagree about how many pages
// a message has. A message goes in the band along the pane's foot when its
// header and whole body fit there; otherwise in the large rect, paging if even
// that is short.
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
        int max_w = f.r.w - 2 * UK_INSET;
        if (max_w > 40 * GW) max_w = 40 * GW;      // the card's widest column
        int cap = (f.r.h - 2 * UK_INSET) / GH;
        f.header_lines = (hdr && hdr[0]) ? wrapped_lines(hdr, max_w) : 0;
        int body_lines = wrapped_lines(body, max_w);
        f.body_per_page = cap - f.header_lines;
        if (f.body_per_page < 1) f.body_per_page = 1;
        f.pages = (body_lines + f.body_per_page - 1) / f.body_per_page;
        if (f.pages < 1) f.pages = 1;
        if (pass == 0 && f.pages == 1) return f;   // it fits the band
    }
    return f;
}

int modern_overlay_dialog_page_count(void) {
    if (dialog_face(NULL)) return 1;   // the in-lay shows its words on one page
    return dialog_fit(false).pages;
}

typedef enum { DLG_MODE_BOTTOM = 0, DLG_MODE_CENTERED_MODAL } DialogMode;

static const Sprites *s_dialog_sprites;
void modern_overlay_set_sprites(const Sprites *s) { s_dialog_sprites = s; }
const Sprites *modern_overlay_sprites(void) { return s_dialog_sprites; }

static void draw_dialog_ex(DialogMode mode);
static void draw_face_dialog(void);

void modern_overlay_draw_dialog(void) {
    if (dialog_face(NULL)) { draw_face_dialog(); return; }
    draw_dialog_ex(DLG_MODE_BOTTOM);
}
void modern_overlay_draw_dialog_centered(void) {
    if (dialog_face(NULL)) { draw_face_dialog(); return; }
    draw_dialog_ex(DLG_MODE_CENTERED_MODAL);
}

static void draw_dialog_ex(DialogMode mode) {
    const char *hdr = dialog_header_text();
    const char *body = dialog_body_text();
    const Resources *res = resources_current();
    if (res && bridge_state == BRIDGE_STATE_DIRECTION && mode == DLG_MODE_BOTTOM) {
        // The bridge asks for a square: the four beside the hero outlined, and
        // words for a touch screen as well as the keys.
        body = res->banners.spell_bridge_prompt_modern;
        int hx = CL_MAP_X + (CL_MAP_TILES_W / 2) * CL_TILE_W, hy = CL_MAP_Y + (CL_MAP_TILES_H / 2) * CL_TILE_H;
        static const int d[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
        for (int k = 0; k < 4; k++)
            for (int t = 0; t < 3; t++)
                DrawRectangleLines(hx + d[k][0] * CL_TILE_W + t, hy + d[k][1] * CL_TILE_H + t,
                                   CL_TILE_W - 2 * t, CL_TILE_H - 2 * t, PAL_CLR(YELLOW));
    }
    bool save = res && body && strcmp(body, res->ui.save_confirm_modern) == 0;
    int max_w;
    char line[200];

    if (mode == DLG_MODE_CENTERED_MODAL) {
        // Victory: a card sized to its words, the header as its title.
        UkDoc d = { 0 };
        uk_doc_add(&d, body, PAL_CLR(WHITE));
        UkCard c = { .title = hdr, .doc = &d };
        uk_card(&c, NULL);
        return;
    }

    // Along the pane's foot: a card as wide as its words and as tall as the
    // page it shows. The words are cut into pages at the card's widest column.
    DialogFit f = dialog_fit(false);
    max_w = f.r.w - 2 * UK_INSET;
    if (max_w > 40 * GW) max_w = 40 * GW;
    const char *p = body ? body : "";
    int skip = dialog_page_current() * f.body_per_page;
    for (int i = 0; i < skip && *p; i++)
        if (bfont_take_line(&p, max_w, line, (int)sizeof line) <= 0) break;
    UkDoc d = { 0 };
    const char *hp = hdr ? hdr : "";
    for (int i = 0; i < f.header_lines && *hp; i++) {
        if (bfont_take_line(&hp, max_w, line, (int)sizeof line) <= 0) break;
        uk_doc_add(&d, line, PAL_CLR(YELLOW));
    }
    for (int i = 0; i < f.body_per_page && *p; i++) {
        if (bfont_take_line(&p, max_w, line, (int)sizeof line) <= 0) break;
        uk_doc_add(&d, line, PAL_CLR(WHITE));
    }
    UkCard c = { .doc = &d, .at_foot = !(res && save), .extra_h = save ? GH + 8 : 0, .no_dim = true };
    UkCardOut o;
    uk_card(&c, &o);

    // The save message offers its two ways on: Quit and Continue.
    if (save) {
        const ResUI *ui = &res->ui;
        int by = o.extra.y;
        int cw = ml_hint_width(ui->hint_continue, NULL, NULL);
        int qw = ml_hint_width(ui->hint_quit, ui->key_ctrl_q, NULL);
        int bx = o.extra.x + o.extra.w - cw;
        ml_hint_button(bx, by, ui->hint_continue, NULL, NULL, KEY_ENTER);
        ml_hint_button(bx - ML_PAD - qw, by, ui->hint_quit, ui->key_ctrl_q, NULL, KEY_Q);
    }
}

// A portrait's current frame (animated), or nothing.
static Texture2D portrait_frame(const Sprites *s, int idx, double fps) {
    if (!s || idx < 0 || s->portrait_frames[idx] <= 0) return (Texture2D){ 0 };
    return s->portrait_anim[idx][sprites_frame((int)(GetTime() * fps), s->portrait_frames[idx])];
}

static Texture2D villain_face(const Sprites *s, int idx) {
    if (!s || idx < 0 || idx >= 17) return (Texture2D){ 0 };
    Texture2D face = s->villain_anim_frames[idx] > 0
        ? s->villain_anim[idx][sprites_frame((int)(GetTime() * 2.0), s->villain_anim_frames[idx])]
        : s->villain_portrait[idx];
    return face.id ? face : s->villain_portrait[idx];
}

static Texture2D troop_face(const Sprites *s, int idx) {
    if (!s || idx < 0 || idx >= 25) return (Texture2D){ 0 };
    return s->troop_portrait[idx].id ? s->troop_portrait[idx] : s->troop_sprite[idx];
}

static Texture2D troop_standing(const Sprites *s, int idx) {
    if (!s || idx < 0 || idx >= 25) return (Texture2D){ 0 };
    Texture2D t = s->troop_anim[idx][sprites_frame(sprites_stand((int)(GetTime() * 6.66)),
                                                   s->troop_anim_frames[idx])];
    return t.id ? t : s->troop_sprite[idx];
}

// A message with a picture hint (REQ-430t): the in-lay -- the header as its
// title, the picture at 2x, the words flowing beside and beneath it, Continue.
static void draw_face_dialog(void) {
    const Resources *res = resources_current();
    if (!res) return;
    int idx = 0;
    int kind = dialog_face(&idx);
    const Sprites *s = s_dialog_sprites;
    Texture2D face = { 0 };
    if (kind == REQ_FACE_VILLAIN)                                 face = villain_face(s, idx);
    else if (kind == REQ_FACE_TROOP)                              face = troop_face(s, idx);
    else if (kind == REQ_FACE_ARTIFACT && s && idx >= 0 && idx < 8) face = s->view_icon[idx];
    else if (kind == REQ_FACE_PORTRAIT)                           face = portrait_frame(s, idx, 2.0);
    if (kind == REQ_FACE_SCENE && s && idx >= 0 && idx < 4 && s->class_disgraced[idx].id) {
        // A scene, framed like a place: the title strip, the backdrop at 3x
        // with the lattice either side, a divider, the words, Continue.
        const char *hdr = dialog_header_text();
        const char *title = (hdr && hdr[0]) ? hdr : "";
        for (int i = 0; !title[0] && i < res->castle_count; i++)
            if (resources_castle_is_home(&res->castles[i])) title = res->castles[i].name;
        const char *body = dialog_body_text();
        int lines = wrapped_lines(body, ml_full().w - 2 * ML_PAD);
        UkScene L = uk_scene_ex(title, NULL, s->class_disgraced[idx], 1, lines * uk_line_h() + 3 * ML_PAD);
        uk_flow(L.full.x + ML_PAD, L.intro_y + ML_PAD, L.full.w - 2 * ML_PAD, L.full.x, 0, L.rows_y - ML_ROW_RULE,
                body, PAL_CLR(WHITE));
        UkRows rows = { { res->banners.castle_continue }, { true } };
        uk_scene_rows(&L, 1, 0, uk_rows_fn, &rows, TOUCH_LIST_PROMPT);
        return;
    }
    uk_result_inlay(dialog_header_text(), face, dialog_body_text(), res->banners.castle_continue,
                    TOUCH_LIST_PROMPT);
}

// =============================================================================
//  Location backdrops (the legacy-shaped screens modules still call this)
// =============================================================================

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

static void draw_location_backdrop(const Game *g, const Sprites *s,
                                   LocKind kind, int troop_idx,
                                   int troop_frame) {
    ML_Rect b = ml_loc_backdrop();
    int S = ml_loc_scale();
    int crop = (ML_BACKDROP_W * S - b.w) / 2;
    Texture2D bd = loc_texture(s, kind);
    if (bd.id && bd.width > 0 && bd.height > 0) {
        float px_per_src = (float)(ML_BACKDROP_W * S) / (float)bd.width;
        Rectangle src = { crop / px_per_src, 0, b.w / px_per_src, b.h / px_per_src };
        Rectangle dst = { (float)b.x, (float)b.y, (float)b.w, (float)b.h };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(b.x, b.y, b.w, b.h, PAL_CLR(BLACK));
    }
    Texture2D fig = { 0 };
    if (kind == LOC_ALCOVE && s && s->alcove_figure.id) {
        fig = s->alcove_figure_anim[sprites_frame(troop_frame, s->alcove_figure_frames)];
        if (!fig.id) fig = s->alcove_figure;
    }
    const Resources *r = (g && g->res) ? g->res : NULL;
    if (fig.id && r && r->sprites.alcove_figure_w > 0) {
        ui_blit(fig, b.x + r->sprites.alcove_figure_x * S - crop,
                     b.y + r->sprites.alcove_figure_y * S,
                     r->sprites.alcove_figure_w * S,
                     r->sprites.alcove_figure_h * S);
        return;
    }
    Texture2D ts = fig;
    if (!ts.id && s && troop_idx >= 0 && troop_idx < 25) {
        int frame = sprites_frame(sprites_stand(troop_frame), s->troop_anim_frames[troop_idx]);
        ts = s->troop_anim[troop_idx][frame];
        if (!ts.id) ts = s->troop_sprite[troop_idx];
    }
    if (ts.id && ts.width > 0)
        ui_blit(ts, b.x + CL_TILE_W, b.y + b.h - CL_TILE_H, CL_TILE_W, CL_TILE_H);
}

void modern_overlay_draw_location_backdrop(const Game *g, const Sprites *s,
                                    int loc_kind, int troop_idx,
                                    int troop_frame) {
    LocKind k = (loc_kind >= 1 && loc_kind <= 7) ? (LocKind)loc_kind : LOC_NONE;
    draw_location_backdrop(g, s, k, troop_idx, troop_frame);
}

// =============================================================================
//  Town -- the scene (Visit the town, Leave), the services in-lay, and an
//  in-lay per service with its person at 2x
// =============================================================================

static int town_backdrop_troop(const Game *g, const char *key) {
    int nt = troops_count();
    int pool[32];
    int npool = 0;
    for (int i = 0; i < nt && npool < 32; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0) pool[npool++] = i;
    }
    unsigned long h = g ? g->seed ^ 0xA1B2C3u : 0;
    for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
    if (npool < 1) return nt > 0 ? (int)(h % (unsigned long)nt) : 0;
    return pool[h % (unsigned long)npool];
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

static void compose_contract(const Game *g, const VillainDef *v, UkDoc *d) {
    const Resources *res = g->res;
    const ResUI *ui = &res->ui;
    if (!v) { uk_doc_add(d, ui->cv_title_no_contract, PAL_CLR(WHITE)); return; }
    const ResVillainDesc *vd = resources_villain_desc(res, v->id);
    char val[RES_VDESC_TEXT_LEN];
    uk_doc_labeled(d, ui->cv_label_name, v->name);
    if (vd && vd->alias[0]) uk_doc_labeled(d, ui->cv_label_alias, vd->alias);
    snprintf(val, sizeof val, "%d", v->reward);
    uk_doc_labeled(d, ui->cv_label_reward, val);
    const ResZone *z = resources_zone_by_id(res, v->zone);
    uk_doc_labeled(d, ui->cv_label_last_seen, (z && z->name[0]) ? z->name : v->zone);
    snprintf(val, sizeof val, "%s", ui->cv_castle_unknown);
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *c = &g->castles[i];
        if (!c->known || strcmp(c->villain_id, v->id) != 0) continue;
        const ResCastle *rc = resources_castle_by_id(res, c->id);
        snprintf(val, sizeof val, "%s", (rc && rc->name[0]) ? rc->name : c->id);
        break;
    }
    uk_doc_labeled(d, ui->cv_label_castle, val);
    if (vd && vd->features[0]) {
        uk_doc_gap(d);
        uk_doc_add(d, ui->cv_features_header, PAL_CLR(YELLOW));
        uk_doc_add(d, vd->features, PAL_CLR(WHITE));
    }
    if (vd && vd->crimes[0]) {
        uk_doc_gap(d);
        uk_doc_add(d, ui->cv_crimes_header, PAL_CLR(YELLOW));
        uk_doc_add(d, vd->crimes, PAL_CLR(WHITE));
    }
}

// The pack's full row text for a service, prices included, its legacy key
// letter dropped.
static void town_row_head(const Game *g, TownRow row, UkDoc *d) {
    char head[128];
    views_town_row_text(g, row, head, sizeof head);
    if (head[0] >= 'A' && head[0] <= 'Z' && head[1] == ')' && head[2] == ' ')
        memmove(head, head + 3, strlen(head + 3) + 1);
    uk_doc_add(d, head, PAL_CLR(YELLOW));
}

// The service in-lay's words, from the game state.
static void compose_service(const Game *g, TownList list, UkDoc *d) {
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    char buf[RES_BANNER_LEN];
    const char *key = views_town_record_key();
    switch (list) {
        case TOWN_LIST_CONTRACTS:
            compose_contract(g, town_shown_villain(g), d);
            break;
        case TOWN_LIST_INFO: {
            char intel[512];
            views_town_intel_text(g, intel, sizeof intel);
            uk_doc_add(d, intel, PAL_CLR(WHITE));
            break;
        }
        case TOWN_LIST_BOAT: {
            if (!views_town_boat_available(g)) { uk_doc_add(d, bn->town_boat_no_master, PAL_CLR(WHITE)); break; }
            town_row_head(g, TOWN_ROW_BOAT, d);
            const char *dock = key ? resources_town_dock(res, key) : NULL;
            if (dock && dock[0]) { uk_doc_gap(d); uk_doc_add(d, dock, PAL_CLR(WHITE)); }
            break;
        }
        case TOWN_LIST_TEMPLE: {
            town_row_head(g, TOWN_ROW_SPELL, d);
            const SpellDef *sp = views_town_spell(g);
            if (!sp) break;
            const char *lore = resources_spell_lore(res, sp->id);
            if (!lore || !lore[0]) lore = sp->description;
            if (lore && lore[0]) { uk_doc_gap(d); uk_doc_add(d, lore, PAL_CLR(WHITE)); }
            int left = g->stats.max_spells - GameKnownSpells(g);
            if (left <= 0) {
                resources_format_template(buf, sizeof buf, bn->town_spell_at_cap, NULL, 0);
            } else {
                char lbuf[16];
                snprintf(lbuf, sizeof lbuf, "%d", left);
                ResTemplateVar vars[] = { { "LEFT", lbuf }, { "S", left == 1 ? "" : "s" } };
                resources_format_template(buf, sizeof buf, bn->town_spell_can_learn, vars, 2);
            }
            uk_doc_gap(d);
            uk_doc_add(d, buf, PAL_CLR(WHITE));
            break;
        }
        case TOWN_LIST_SIEGE:
            town_row_head(g, TOWN_ROW_SIEGE, d);
            uk_doc_gap(d);
            uk_doc_add(d, bn->town_siege_lore, PAL_CLR(WHITE));
            break;
        default: break;
    }
}

static TownList town_list_for(int row) {
    switch (row) {
        case TOWN_ROW_CONTRACT: return TOWN_LIST_CONTRACTS;
        case TOWN_ROW_INFO:     return TOWN_LIST_INFO;
        case TOWN_ROW_BOAT:     return TOWN_LIST_BOAT;
        case TOWN_ROW_SPELL:    return TOWN_LIST_TEMPLE;
        case TOWN_ROW_SIEGE:    return TOWN_LIST_SIEGE;
        default:                return TOWN_LIST_MENU;
    }
}

// The person of a service, as a portrait index (-1: none).
static int town_person(const Game *g, TownList list) {
    const Resources *res = g->res;
    const char *key = views_town_record_key();
    const ResTown *tw = key ? resources_town_by_id(res, key) : NULL;
    const ResZone *z = tw ? resources_zone_by_id(res, tw->zone) : NULL;
    const char *id = NULL;
    switch (list) {
        case TOWN_LIST_INFO:   id = tw ? tw->informant : NULL; break;
        case TOWN_LIST_BOAT:   id = (z && views_town_boat_available(g)) ? z->boatmaster : NULL; break;
        case TOWN_LIST_TEMPLE: id = z ? z->pontifex : NULL; break;
        case TOWN_LIST_SIEGE:  id = z ? z->siegemaster : NULL; break;
        default:               id = tw ? tw->townhead : NULL; break;
    }
    return id ? resources_portrait_index(res, id) : -1;
}

typedef struct { const Game *g; bool menu; } TownRowsCtx;

static bool town_row_fn(void *ctx, int i, char *label, char *right, int cap) {
    const TownRowsCtx *c = (const TownRowsCtx *)ctx;
    bool enabled = true, held = false;
    char buf[64] = "";
    views_town_list_row(c->g, i, buf, sizeof buf, &enabled, &held);
    right[0] = '\0';
    bool back = i == views_town_list_rows(c->g) - 1;
    if (c->menu && !back)                         snprintf(label, (size_t)cap, "%s >", buf);
    else if (views_town_list() == TOWN_LIST_CONTRACTS && !back) snprintf(label, (size_t)cap, " %s", buf);
    else                                          snprintf(label, (size_t)cap, "%s", buf);
    return enabled || back;
}

static void town_title(const Game *g, char *out, int cap, bool with_section) {
    const char *name = views_town_display_name();
    ResTemplateVar hv[] = { { "NAME", (name && name[0]) ? name : "" } };
    resources_format_template(out, cap, g->res->banners.town_header, hv, 1);
    if (with_section) {
        char label[64];
        views_town_menu_label(g, views_town_cursor(), label, sizeof label);
        size_t n = strlen(out);
        snprintf(out + n, (size_t)cap - n, " > %s", label);
    }
}

static const char *town_zone_name(const Game *g) {
    const char *key = views_town_record_key();
    const ResTown *tw = key ? resources_town_by_id(g->res, key) : NULL;
    const ResZone *z = tw ? resources_zone_by_id(g->res, tw->zone) : NULL;
    return (z && z->name[0]) ? z->name : "";
}

void modern_overlay_draw_town(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const char *name = views_town_display_name();
    const char *key = views_town_record_key();
    const ResTown *tw = key ? resources_town_by_id(res, key) : NULL;
    char title[128], gold[48], buf[RES_BANNER_LEN];
    town_title(g, title, sizeof title, false);
    uk_gold_text(g, gold, sizeof gold);

    // The scene: the square at 3x, the head townsperson standing in it at 2x.
    ResTemplateVar iv[] = { { "HERO", g->character.name }, { "TOWN", (name && name[0]) ? name : "" },
                            { "ZONE", town_zone_name(g) } };
    resources_format_template(buf, sizeof buf, bn->town_intro, iv, 3);
    UkScene L = uk_scene_for(title, gold, loc_texture(s, LOC_TOWN), 2, buf);
    int head = tw ? resources_portrait_index(res, tw->headman) : -1;
    Texture2D fig = portrait_frame(s, head, 1000.0 / 180.0);
    if (!fig.id) fig = troop_standing(s, town_backdrop_troop(g, name));
    uk_scene_figure(&L, fig, CL_TILE_W);
    uk_scene_intro(&L, buf);
    bool visiting = views_town_visiting();
    char visit[RES_BANNER_LEN + 4];
    snprintf(visit, sizeof visit, "%s >", bn->town_visit);
    UkRows scene_rows = { { visit, bn->location_leave }, { true, true } };
    uk_scene_rows(&L, 2, visiting ? -1 : views_town_scene_cursor(), uk_rows_fn, &scene_rows,
                  visiting ? 0 : TOUCH_LIST_TOWN);
    if (!visiting) return;

    TownList list = views_town_list();
    bool menu = list == TOWN_LIST_MENU;
    const char *info = views_town_info_text();
    int rows = views_town_list_rows(g);
    int cursor = views_town_list_cursor();
    TownList shown = menu ? town_list_for(views_town_cursor()) : list;
    if (menu && views_town_cursor() == TOWN_ROW_LEAVE) shown = TOWN_LIST_MENU;

    // Every town panel is one frame: the same size and place, the title strip,
    // the rows in a column at the left, the person (or the wanted face) at 2x
    // and their words at the right. The services, a contract, a service, its
    // question and its outcome differ only in their rows and words.
    char t2[128];
    town_title(g, t2, sizeof t2, !menu);
    Texture2D face = { 0 };
    if (list == TOWN_LIST_CONTRACTS) {
        const VillainDef *v = town_shown_villain(g);
        face = v ? villain_face(s, v->index) : (s ? s->hud_contract_silhouette : (Texture2D){ 0 });
    } else if (shown == TOWN_LIST_BOAT && !views_town_boat_available(g)) {
        face = s ? s->hud_boat_silhouette : (Texture2D){ 0 };
    } else {
        face = portrait_frame(s, town_person(g, shown), 2.0);
    }

    UkDoc d = { 0 };
    const PromptView *pv = prompt_view();
    bool asking = prompt_is_active() && pv && pv->kind == PK_YES_NO;
    bool result = views_town_result_dialog() && info;
    if (asking) {
        uk_doc_add(&d, pv->body, PAL_CLR(WHITE));
    } else if (info && info[0]) {
        uk_doc_add(&d, info, PAL_CLR(WHITE));
    } else if (menu) {
        const ResTownInvite *inv = tw ? resources_town_invite(res, tw->invitations) : NULL;
        const char *line = NULL;
        char rites[RES_BANNER_LEN];
        if (inv) switch (shown) {
            case TOWN_LIST_CONTRACTS: line = inv->contracts; break;
            case TOWN_LIST_BOAT:      line = views_town_boat_available(g) ? inv->boat : bn->town_boat_no_master; break;
            case TOWN_LIST_INFO:      line = inv->information; break;
            case TOWN_LIST_TEMPLE:
                if (!views_town_row_enabled(g, TOWN_ROW_SPELL)) { views_town_rites_text(g, rites, sizeof rites); line = rites; }
                else line = inv->temple;
                break;
            case TOWN_LIST_SIEGE:     line = inv->siege; break;
            default:                  line = bn->gmd_back; break;
        }
        if (line && line[0]) {
            ResTemplateVar vars[] = { { "HERO", g->character.name }, { "TOWN", (name && name[0]) ? name : "" } };
            resources_format_template(buf, sizeof buf, line, vars, 2);
            uk_doc_add(&d, buf, PAL_CLR(WHITE));
        }
    } else {
        compose_service(g, list, &d);
    }

    ML_Rect b = uk_inlay(ml_full().w, UK_TALL_H, t2, town_zone_name(g));
    int lw = 14 * GW + 2 * ML_PAD;
    if (asking || result) {
        // The question's answers, or Continue, take the rows' place.
        const Resources *r0 = res;
        UkRows qr = { { asking ? r0->ui.prompt_yes : bn->castle_continue, r0->ui.prompt_no }, { true, true } };
        ml_list_draw(b.x, b.y, lw, b.h, asking ? 2 : 1, asking ? pv->yn_cursor : 0, uk_rows_fn, &qr,
                     TOUCH_LIST_PROMPT, uk_ink());
    } else if (menu || list == TOWN_LIST_CONTRACTS) {
        TownRowsCtx rc = { g, menu };
        ml_list_draw(b.x, b.y, lw, b.h, rows, cursor, town_row_fn, &rc, TOUCH_LIST_TOWN, uk_ink());
        if (list == TOWN_LIST_CONTRACTS) {
            // The contract held: a dot before its name.
            int vis = ml_list_fit(b.h), first = ml_list_first(rows, cursor, vis);
            for (int i = first; i < rows - 1 && i < first + vis; i++) {
                int slot = views_town_contract_slot(g, i);
                if (slot < 0 || strcmp(g->contract.cycle[slot], g->contract.active_id) != 0) continue;
                int ry = b.y + (i - first) * (ml_row_h() + ML_ROW_RULE);
                DrawCircle(b.x + ML_PAD + GW / 2 - 2, ry + ml_row_h() / 2, 3,
                           i == cursor ? uk_ink() : PAL_CLR(YELLOW));
            }
        }
    } else {
        // A service's own rows: its answer and Back (Information: Back only,
        // its castle is in the words).
        TownRowsCtx rc = { g, false };
        int first_row = list == TOWN_LIST_INFO ? rows - 1 : 0;
        ml_list_draw_ex(b.x, b.y, lw, b.h, rows - first_row, cursor - first_row, town_row_fn, &rc,
                        TOUCH_LIST_TOWN, uk_ink(), first_row);
    }
    lattice_band_v(b.x + lw, b.y, UK_BAND, b.h);

    ML_Rect a = { b.x + lw + UK_BAND + UK_INSET, b.y + UK_INSET, 0, 0 };
    a.w = b.x + b.w - UK_INSET - a.x;
    a.h = b.y + b.h - UK_INSET - a.y;
    const int size = 2 * CL_TILE_W;
    if (face.id) uk_picture(face, a.x, a.y, size, size);
    if (menu || asking || result) {
        views_town_set_detail_pages(1);
        uk_doc_draw(&d, a, face.id ? size : 0, size, -1, true);
    } else {
        int pages = uk_doc_draw(&d, a, face.id ? size : 0, size, 0, false);
        views_town_set_detail_pages(pages);
        uk_doc_draw(&d, a, face.id ? size : 0, size, views_town_detail_page(), true);
    }
}

// =============================================================================
//  Castles -- the scene (Recruit / Audience / Leave, or Garrison / Withdraw /
//  Leave), each page an in-lay over it
// =============================================================================

static void castle_fmt(char *out, int cap, const char *tmpl, const char *count, const char *max) {
    ResTemplateVar v[] = { { "COUNT", count }, { "MAX", max ? max : "" },
                           { "RANK", count }, { "GOLD", count } };
    resources_format_template(out, cap, tmpl, v, 4);
}

static int castle_pick_troop(const Game *g, const char *key) {
    int pool[8];
    int n = modern_castle_pool(pool, 8);
    if (n < 1) return -1;
    unsigned long h = g ? (g->seed ^ 0x0CA571E5u) : 0;
    for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
    return pool[h % (unsigned long)n];
}

static void castle_gains(const Game *g, int rank, UkDoc *d) {
    const ResBanners *bn = &g->res->banners;
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls || rank <= 0) return;
    char buf[RES_BANNER_LEN], nb[16];
    int l0, s0, p0, c0, l1, s1, p1, c1;
    class_stats_at_rank(cls, rank - 1, &l0, &s0, &p0, &c0);
    class_stats_at_rank(cls, rank, &l1, &s1, &p1, &c1);
    uk_doc_gap(d);
    snprintf(nb, sizeof nb, "%d", l1 - l0);
    castle_fmt(buf, sizeof buf, bn->castle_gain_leadership, nb, NULL);
    uk_doc_add(d, buf, PAL_CLR(YELLOW));
    snprintf(nb, sizeof nb, "%d", c1 - c0);
    castle_fmt(buf, sizeof buf, bn->castle_gain_commission, nb, NULL);
    uk_doc_add(d, buf, PAL_CLR(YELLOW));
    if (s1 - s0 > 0) {
        snprintf(nb, sizeof nb, "%d", s1 - s0);
        castle_fmt(buf, sizeof buf, bn->castle_gain_spells, nb, NULL);
        uk_doc_add(d, buf, PAL_CLR(YELLOW));
    }
}

// The Promotion ceremony: the award at 2x, the Emperor's words and the rank's
// gains beside it, Continue.
static void castle_draw_promotion(const Game *g, const Sprites *s, const ResCastle *rc) {
    const Resources *res = g->res;
    int needed = 0, rank = 0;
    modern_castle_audience(&needed, &rank);
    ML_Rect b = uk_inlay(UK_WIDE_W, UK_TALL_H, res->banners.castle_action_promotion, g->character.cls.rank_title);
    char label[64];
    modern_castle_row(g, 0, label, sizeof label, NULL);
    UkRows rows = { { label }, { true } };
    int foot = uk_foot_rows(b, 1, 0, uk_rows_fn, &rows, TOUCH_LIST_CASTLE);
    int idx = (rc && rank >= 0 && rank < 4) ? resources_portrait_index(res, rc->special.promotion[rank]) : -1;
    Texture2D img = portrait_frame(s, idx, 0.0);
    int iw = 0, ih = 0;
    int avail_h = foot - ML_PAD - b.y;
    if (img.id && img.width > 0 && img.height > 0) {
        int sc = 2;
        while (sc > 1 && img.height * sc > avail_h) sc--;
        iw = img.width * sc;
        ih = img.height * sc;
        if (ih > avail_h) ih = avail_h;
        ui_blit(img, b.x, b.y, iw, ih);
        lattice_band_v(b.x + iw, b.y, UK_BAND, foot - b.y);
    }
    ML_Rect a = { b.x + iw + UK_BAND + UK_INSET, b.y + UK_INSET, 0, 0 };
    a.w = b.x + b.w - UK_INSET - a.x;
    a.h = foot - ML_PAD - a.y;
    UkDoc d = { 0 };
    char buf[RES_BANNER_LEN];
    if (rc) {
        audience_substitute(g, needed, rc->special.audience_rank_up, buf, sizeof buf);
        uk_doc_add(&d, buf, PAL_CLR(WHITE));
    }
    castle_gains(g, rank, &d);
    uk_doc_draw(&d, a, 0, 0, -1, true);
}

// The troop a castle page's row stands for, with its numbers beside its
// portrait: what it is, and what you have and can move.
static void castle_troop_detail(const Game *g, const Sprites *s, const TroopDef *pt, McPage page,
                                const CastleRecord *cr, ML_Rect a) {
    const ResBanners *bn = &g->res->banners;
    const ResUI *ui = &g->res->ui;
    const int size = 2 * CL_TILE_W;
    uk_picture(troop_face(s, pt->index), a.x, a.y, size, size);
    char buf[RES_BANNER_LEN], nb[32];
    int x = a.x + size + UK_INSET, y = a.y;
    int lh = uk_line_h();
    bfont_draw(pt->name, x, y, PAL_CLR(YELLOW));
    y += lh + 4;
    struct { const char *label; char value[32]; } rows[5];
    snprintf(rows[0].value, 32, "%d", pt->skill_level);                      rows[0].label = ui->army_skill;
    snprintf(rows[1].value, 32, "%d", pt->move_rate);                        rows[1].label = ui->army_move;
    snprintf(rows[2].value, 32, "%d", pt->hit_points);                       rows[2].label = ui->army_hit_points;
    snprintf(rows[3].value, 32, "%d-%d", pt->melee_min, pt->melee_max);      rows[3].label = ui->army_damage;
    snprintf(rows[4].value, 32, "%d", pt->recruit_cost);                     rows[4].label = ui->army_g_cost;
    int vx = a.x + a.w;
    for (int i = 0; i < 5; i++, y += lh) {
        bfont_draw(rows[i].label, x, y, PAL_CLR(WHITE));
        bfont_draw_right(rows[i].value, vx, y, PAL_CLR(WHITE));
    }
    int in_army = 0, in_garrison = 0;
    for (int k = 0; k < GAME_ARMY_SLOTS; k++) {
        if (strcmp(g->army[k].id, pt->id) == 0) in_army += g->army[k].count;
        if (cr && strcmp(cr->garrison[k].id, pt->id) == 0) in_garrison += cr->garrison[k].count;
    }
    UkDoc d = { 0 };
    snprintf(nb, sizeof nb, "%d", in_army);
    castle_fmt(buf, sizeof buf, bn->castle_have, nb, NULL);
    uk_doc_add(&d, buf, PAL_CLR(WHITE));
    if (page == MC_RECRUIT && !modern_castle_troop_offered(g, pt)) {
        snprintf(nb, sizeof nb, "%d", pt->hit_points * 6);
        castle_fmt(buf, sizeof buf, bn->castle_needs_leadership, nb, NULL);
        uk_doc_add(&d, buf, PAL_CLR(YELLOW));
    } else if (page == MC_RECRUIT) {
        int m = GameMaxRecruitable(g, pt->id);
        if (m < 0) m = 0;
        if (pt->recruit_cost > 0 && g->stats.gold / pt->recruit_cost < m) m = g->stats.gold / pt->recruit_cost;
        snprintf(nb, sizeof nb, "%d", m);
        castle_fmt(buf, sizeof buf, bn->castle_can_recruit, nb, NULL);
        uk_doc_add(&d, buf, PAL_CLR(WHITE));
    } else {
        snprintf(nb, sizeof nb, "%d", in_garrison);
        castle_fmt(buf, sizeof buf, bn->castle_in_garrison, nb, NULL);
        uk_doc_add(&d, buf, PAL_CLR(WHITE));
        if (page == MC_WITHDRAW &&
            GameArmyTotalLeadership(g) + pt->hit_points * in_garrison > g->stats.leadership_current) {
            uk_doc_gap(&d);
            uk_doc_add(&d, bn->castle_over_leadership, PAL_CLR(YELLOW));
        }
    }
    ML_Rect under = { a.x, a.y + size + ML_PAD + 4, a.w, a.y + a.h - (a.y + size + ML_PAD + 4) };
    uk_doc_draw(&d, under, 0, 0, -1, true);
}

typedef struct { const Game *g; } CastleRowsCtx;

static bool castle_row_fn(void *ctx, int i, char *label, char *right, int cap) {
    const Game *g = ((const CastleRowsCtx *)ctx)->g;
    const char *id = NULL;
    right[0] = '\0';
    modern_castle_row(g, i, label, cap, &id);
    const TroopDef *t = (modern_castle_page() == MC_RECRUIT && id) ? troop_by_id(id) : NULL;
    return !t || modern_castle_troop_offered(g, t);
}

void modern_overlay_draw_castle(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const char *cid = modern_castle_id();
    const ResCastle *rc = resources_castle_by_id(res, cid);
    const CastleRecord *cr = GameFindCastleConst(g, cid);
    bool home = modern_castle_is_home();
    McPage page = modern_castle_page();
    int cursor = modern_castle_cursor();
    const char *cname = (rc && rc->name[0]) ? rc->name : cid;
    char gold[48], buf[RES_BANNER_LEN];
    uk_gold_text(g, gold, sizeof gold);

    // The scene: the barracks at 3x, their keeper (or the Emperor on Audience)
    // standing in it at 2x; a castle of your own shows the troop it holds most.
    int mcur = modern_castle_menu_cursor();
    const char *inv = mcur == 2 ? bn->gmd_leave
                    : home ? (mcur == 0 ? bn->castle_invite_recruit : bn->castle_invite_audience)
                           : (mcur == 0 ? bn->castle_invite_garrison : bn->castle_invite_withdraw);
    ResTemplateVar v[] = { { "HERO", g->character.name }, { "CASTLE", cname } };
    resources_format_template(buf, sizeof buf, inv, v, 2);
    // The backdrop, the words under it, the three answers as rows. The words'
    // band is as tall as the longest of the three invitations, so nothing
    // moves as the cursor does.
    char longest[RES_BANNER_LEN] = "";
    {
        const char *invs[3] = { home ? bn->castle_invite_recruit : bn->castle_invite_garrison,
                                home ? bn->castle_invite_audience : bn->castle_invite_withdraw, bn->gmd_leave };
        int most = -1;
        for (int k = 0; k < 3; k++) {
            char tb[RES_BANNER_LEN];
            resources_format_template(tb, sizeof tb, invs[k], v, 2);
            int n = uk_lines(tb, ml_full().w - 2 * ML_PAD);
            if (n > most) { most = n; snprintf(longest, sizeof longest, "%s", tb); }
        }
    }
    UkScene L = uk_scene_for(cname, gold, loc_texture(s, LOC_CASTLE), 3, longest);
    bool barracks = rc && page != MC_AUDIENCE && page != MC_PROMOTION;
    const char *fig_id = !rc ? "" : (barracks && rc->special.barracks_figure[0])
                                  ? rc->special.barracks_figure : rc->special.figure;
    Texture2D fig = (home && rc) ? portrait_frame(s, resources_portrait_index(res, fig_id), 1000.0 / 180.0)
                                 : (Texture2D){ 0 };
    if (!fig.id) {
        int ti = -1;
        if (!home) {
            int most = 0;
            for (int k = 0; cr && k < GAME_ARMY_SLOTS; k++) {
                const TroopDef *gt = cr->garrison[k].id[0] ? troop_by_id(cr->garrison[k].id) : NULL;
                if (gt && cr->garrison[k].count > most) { most = cr->garrison[k].count; ti = gt->index; }
            }
        } else {
            ti = castle_pick_troop(g, cid);
        }
        fig = troop_standing(s, ti);
    }
    uk_scene_figure(&L, fig, CL_TILE_W);
    uk_scene_intro(&L, buf);
    char r0[RES_BANNER_LEN + 4], r1[RES_BANNER_LEN + 4];
    snprintf(r0, sizeof r0, "%s >", home ? bn->castle_menu_recruit : bn->castle_menu_garrison);
    snprintf(r1, sizeof r1, "%s >", home ? bn->castle_menu_audience : bn->castle_menu_withdraw);
    UkRows menu_rows = { { r0, r1, bn->location_leave }, { true, true, true } };
    bool on_menu = page == MC_MENU;
    uk_scene_rows(&L, 3, on_menu ? mcur : -1, uk_rows_fn, &menu_rows, on_menu ? TOUCH_LIST_CASTLE : 0);

    const char *msg = modern_castle_message();
    Texture2D keeper = (home && rc) ? portrait_frame(s, resources_portrait_index(res,
                           rc->special.barracks_portrait[0] ? rc->special.barracks_portrait : rc->special.portrait), 2.0)
                                    : (Texture2D){ 0 };
    if (on_menu) {
        if (msg) uk_result_inlay(cname, keeper, msg, bn->castle_continue, TOUCH_LIST_PROMPT);
        return;
    }
    if (page == MC_PROMOTION) { castle_draw_promotion(g, s, rc); return; }

    const char *titles[] = { "", bn->castle_menu_recruit, bn->castle_menu_audience,
                             bn->castle_menu_garrison, bn->castle_menu_withdraw, "" };
    char title[128];
    snprintf(title, sizeof title, "%s > %s", cname, titles[page]);
    int rows = modern_castle_rows(g);
    CastleRowsCtx cc = { g };

    // The second step, "How many?": its own in-lay.
    int cv = 0, cmax = 0;
    const char *row_troop = NULL;
    char label[64];
    if (page != MC_AUDIENCE) modern_castle_row(g, cursor, label, sizeof label, &row_troop);
    const TroopDef *pt = row_troop ? troop_by_id(row_troop) : NULL;
    if (modern_castle_stepper(&cv, &cmax) && pt) {
        char heading[RES_BANNER_LEN], sub[RES_BANNER_LEN], cost[RES_BANNER_LEN] = "", mx[16], vb[16];
        static char act_label[RES_BANNER_LEN];
        ResTemplateVar hv[] = { { "TROOP", pt->name } };
        resources_format_template(heading, sizeof heading, bn->count_heading, hv, 1);
        snprintf(mx, sizeof mx, "%d", cmax);
        ResTemplateVar sv[] = { { "MAX", mx } };
        resources_format_template(sub, sizeof sub, page == MC_RECRUIT ? bn->count_of_lead
                                  : page == MC_GARRISON ? bn->count_of_army : bn->count_of_garrison, sv, 1);
        if (page == MC_RECRUIT) {
            snprintf(vb, sizeof vb, "%d", pt->recruit_cost * cv);
            ResTemplateVar c1[] = { { "GOLD", vb } };
            resources_format_template(cost, sizeof cost, bn->count_cost, c1, 1);
        }
        snprintf(vb, sizeof vb, "%d", cv);
        ResTemplateVar av[] = { { "COUNT", vb } };
        resources_format_template(act_label, sizeof act_label, page == MC_RECRUIT ? bn->count_recruit
                                  : page == MC_GARRISON ? bn->count_garrison : bn->count_withdraw, av, 1);
        const char *lines[] = { sub, cost };
        Color colors[] = { PAL_CLR(WHITE), PAL_CLR(YELLOW) };
        uk_count_inlay(heading, troop_face(s, pt->index), lines, colors, 2, cv, cmax, act_label, bn->count_cancel,
                       TOUCH_LIST_CASTLE);
        return;
    }

    if (page == MC_AUDIENCE) {
        // The Emperor at 2x and where the hero stands; the audiences along the foot.
        ML_Rect b = uk_inlay(UK_WIDE_W, UK_TALL_H, title, g->character.cls.rank_title);
        int foot = uk_foot_rows(b, rows, cursor, castle_row_fn, &cc, TOUCH_LIST_CASTLE);
        const int size = 2 * CL_TILE_W;
        ML_Rect a = { b.x + UK_INSET, b.y + UK_INSET, b.w - 2 * UK_INSET, foot - ML_PAD - (b.y + UK_INSET) };
        int pic = size < a.h ? size : a.h;
        Texture2D emperor = rc ? portrait_frame(s, resources_portrait_index(res, rc->special.portrait), 2.0)
                               : (Texture2D){ 0 };
        if (emperor.id) uk_picture(emperor, a.x, a.y, pic, pic);
        UkDoc d = { 0 };
        const ClassDef *cls = class_by_id(g->character.cls.id);
        int rank = g->character.cls.rank_index;
        const ResEconomy *ec = &res->economy;
        char nb[16], mb[16];
        if (ec->audiences && cursor == 1) {
            snprintf(nb, sizeof nb, "%d", GameArtifactsFound(g));
            snprintf(mb, sizeof mb, "%d", artifacts_count() < 8 ? artifacts_count() : 8);
            castle_fmt(buf, sizeof buf, bn->castle_artifacts, nb, mb);
            uk_doc_add(&d, buf, PAL_CLR(WHITE));
        } else if (ec->audiences && cursor == 2) {
            snprintf(nb, sizeof nb, "%d", ec->tribute_cost);
            castle_fmt(buf, sizeof buf, bn->castle_cost, nb, NULL);
            uk_doc_add(&d, buf, PAL_CLR(WHITE));
        } else {
            castle_fmt(buf, sizeof buf, bn->castle_rank, g->character.cls.rank_title, NULL);
            uk_doc_add(&d, buf, PAL_CLR(WHITE));
            if (cls && rank + 1 < cls->rank_count) {
                castle_fmt(buf, sizeof buf, bn->castle_next_rank, cls->ranks[rank + 1].name, NULL);
                uk_doc_add(&d, buf, PAL_CLR(WHITE));
                int need = cls->ranks[rank + 1].villains_needed - GameVillainsCaught(g);
                snprintf(nb, sizeof nb, "%d", need > 0 ? need : 0);
                castle_fmt(buf, sizeof buf, bn->castle_needed, nb, NULL);
                uk_doc_add(&d, buf, PAL_CLR(WHITE));
            }
        }
        uk_doc_draw(&d, a, emperor.id ? pic : 0, pic, -1, true);

        // The Emperor's answer, in its own in-lay until Continue.
        int ares = 0, aneed = 0, aud_needed = 0, aud_rank = 0;
        GameAudienceGain gain;
        McAudience akind = modern_castle_audience_result(&ares, &aneed, &gain);
        int aud = modern_castle_audience(&aud_needed, &aud_rank);
        UkDoc rd = { 0 };
        if (ares && rc) {
            const char *tmpl = akind == MC_AUD_BLESSING
                ? (ares == GAME_BLESSING_GRANTED + 1 ? rc->special.audience_blessing_granted
                   : ares == GAME_BLESSING_NEED_ARTIFACTS + 1 ? rc->special.audience_blessing_needed
                   : rc->special.audience_blessing_already)
                : (ares == 1 ? rc->special.audience_tribute_paid : rc->special.audience_tribute_needed);
            audience_substitute(g, aneed, tmpl, buf, sizeof buf);
            uk_doc_add(&rd, buf, PAL_CLR(WHITE));
            if (gain.leadership > 0 || gain.spell_power > 0 || gain.max_spells > 0) uk_doc_gap(&rd);
            if (gain.leadership > 0) {
                snprintf(nb, sizeof nb, "%d", gain.leadership);
                castle_fmt(buf, sizeof buf, bn->castle_gain_leadership, nb, NULL);
                uk_doc_add(&rd, buf, PAL_CLR(YELLOW));
            }
            if (gain.spell_power > 0) {
                snprintf(nb, sizeof nb, "%d", gain.spell_power);
                castle_fmt(buf, sizeof buf, bn->castle_gain_spell_power, nb, NULL);
                uk_doc_add(&rd, buf, PAL_CLR(YELLOW));
            }
            if (gain.max_spells > 0) {
                snprintf(nb, sizeof nb, "%d", gain.max_spells);
                castle_fmt(buf, sizeof buf, bn->castle_gain_spells, nb, NULL);
                uk_doc_add(&rd, buf, PAL_CLR(YELLOW));
            }
        } else if (aud && rc) {
            const char *tmpl = aud == GAME_AUDIENCE_PROMOTED + 1 ? rc->special.audience_rank_up
                             : aud == GAME_AUDIENCE_MORE_NEEDED + 1 ? rc->special.audience_more_needed
                             : rc->special.audience_final_rank;
            audience_substitute(g, aud_needed, tmpl, buf, sizeof buf);
            uk_doc_add(&rd, buf, PAL_CLR(WHITE));
        } else if (msg) {
            uk_doc_add(&rd, msg, PAL_CLR(WHITE));
        }
        if (rd.n > 0) {
            const char *rt = akind == MC_AUD_BLESSING && ares ? bn->castle_action_blessing
                           : akind == MC_AUD_TRIBUTE && ares ? bn->castle_action_tribute
                           : bn->castle_action_promotion;
            UkCard c = { .title = rt, .face = emperor, .doc = &rd, .answers = { bn->castle_continue },
                         .n_answers = 1, .touch_list = TOUCH_LIST_PROMPT };
            uk_card(&c, NULL);
        }
        return;
    }

    // Recruit, Garrison, Withdraw: the troops at the left, the one under the
    // cursor beside them.
    // As tall as the rows or the troop's picture and numbers.
    int detail = 2 * UK_INSET + 2 * CL_TILE_W + ML_PAD + 4 + 6 * uk_line_h();
    int body_h = ml_list_height(rows) > detail ? ml_list_height(rows) : detail;
    ML_Rect b = uk_inlay(UK_WIDE_W, uk_title_h() + UK_BAND + body_h, title, NULL);
    int lw = 15 * GW + 2 * ML_PAD;
    ml_list_draw(b.x, b.y, lw, b.h, rows, cursor, castle_row_fn, &cc, TOUCH_LIST_CASTLE, uk_ink());
    lattice_band_v(b.x + lw, b.y, UK_BAND, b.h);
    ML_Rect a = { b.x + lw + UK_BAND + UK_INSET, b.y + UK_INSET, 0, 0 };
    a.w = b.x + b.w - UK_INSET - a.x;
    a.h = b.y + b.h - UK_INSET - a.y;
    if (pt) {
        castle_troop_detail(g, s, pt, page, cr, a);
    } else if (rows == 1 && (page == MC_GARRISON || page == MC_WITHDRAW)) {
        UkDoc d = { 0 };
        uk_doc_add(&d, bn->castle_no_troops, PAL_CLR(WHITE));
        uk_doc_draw(&d, a, 0, 0, -1, true);
    } else {
        UkDoc d = { 0 };
        ResTemplateVar v2[] = { { "HERO", g->character.name }, { "CASTLE", cname } };
        resources_format_template(buf, sizeof buf, page == MC_RECRUIT ? bn->castle_invite_recruit
                                  : page == MC_GARRISON ? bn->castle_invite_garrison : bn->castle_invite_withdraw, v2, 2);
        uk_doc_add(&d, buf, PAL_CLR(WHITE));
        uk_doc_draw(&d, a, 0, 0, -1, true);
    }
    if (msg) uk_result_inlay(title, keeper, msg, bn->castle_continue, TOUCH_LIST_PROMPT);
}

// =============================================================================
//  Foe -- the enemy standing on the plains, a card per troop, Fight / Evade
// =============================================================================

static bool foe_row(void *ctx, int i, char *label, char *right, int cap) {
    const Game *g = (const Game *)ctx;
    const ResBanners *bn = &g->res->banners;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", i == 0 ? bn->foe_fight : bn->foe_evade);
    return i == 0 || !pending_foe_evade_blocked;
}

// Draw `text` wrapped to `w`, centred on `cx`; a word longer than the width is
// broken with a hyphen. Returns the y under the last line.
static int foe_text_centred(const char *text, int cx, int y, int w, int max_lines, Color fg) {
    const char *p = text ? text : "";
    char line[96];
    int lines = 0;
    while (*p && lines < max_lines) {
        const char *q = p;
        if (bfont_take_line(&q, w, line, (int)sizeof line) <= 0) break;
        if (bfont_text_width(line) > w) {
            int n = 0;
            char part[96];
            while (p[n] && p[n] != ' ' && n < (int)sizeof part - 2) {
                part[n] = p[n];
                part[n + 1] = '-';
                part[n + 2] = '\0';
                if (bfont_text_width(part) > w) break;
                n++;
            }
            if (n < 1) n = 1;
            memcpy(line, p, (size_t)n);
            line[n] = '-';
            line[n + 1] = '\0';
            q = p + n;
        }
        bfont_draw(line, cx - bfont_text_width(line) / 2, y, fg);
        y += GH + 2;
        lines++;
        p = q;
        while (*p == ' ') p++;
    }
    return y;
}

void modern_overlay_draw_foe(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const ResUI *ui = &res->ui;
    const PromptView *pv = prompt_view();
    const int tile = CL_TILE_W, lh = GH + 2;

    char right[RES_BANNER_LEN];
    if (pending_foe_evade_blocked) {
        snprintf(right, sizeof right, "%s", bn->foe_evade_blocked);
    } else {
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (g->army[i].id[0] && g->army[i].count > 0) n++;
        char cb[16], lb[16];
        snprintf(cb, sizeof cb, "%d", n);
        snprintf(lb, sizeof lb, "%d", g->stats.leadership_current);
        ResTemplateVar v[] = { { "COUNT", cb }, { "LEAD", lb } };
        resources_format_template(right, sizeof right, bn->fv_your_army, v, 2);
    }
    // The cards need a portrait, the count, two lines of name and two of numbers.
    const int card_h = ML_PAD + tile + 4 + 5 * lh + 4;
    UkScene L = uk_scene_ex(ui->dt_foes, right, loc_texture(s, LOC_PLAINS), 2, card_h);

    const FoeState *f = pending_foe_id[0] ? GameFindFoeConst(g, pending_foe_id) : NULL;
    const TroopDef *troops[5];
    int counts[5], shown = 0;
    for (int i = 0; f && i < GAME_ARMY_SLOTS && shown < 5; i++) {
        const Unit *u = &f->garrison[i];
        const TroopDef *t = (u->id[0] && u->count > 0) ? troop_by_id(u->id) : NULL;
        if (!t) continue;
        troops[shown] = t;
        counts[shown++] = u->count;
    }
    // The band on the plains at 1x, facing the hero, each troop standing over
    // its own card.
    {
        int sw0 = (L.full.w - 4 * UK_BAND) / 5;
        for (int k = 0; k < shown; k++) {
            Texture2D fig = troop_standing(s, troops[k]->index);
            int cx = L.full.x + k * (sw0 + UK_BAND) + sw0 / 2;
            int fh = tile < L.scene.h ? tile : L.scene.h;
            if (fig.id) ui_blit_mirrored(fig, cx - tile / 2, L.scene.y + L.scene.h - fh, tile, fh);
        }
    }

    // A card per troop: portrait, how many (worded as the encounter words it),
    // the name, its hit points and damage.
    const int STRIPES = 5;
    int top = L.intro_y;
    int sw = (L.full.w - (STRIPES - 1) * UK_BAND) / STRIPES;
    int sh = L.rows_y - ML_ROW_RULE - top;
    for (int k = 1; k < STRIPES; k++)
        lattice_band_v(L.full.x + k * sw + (k - 1) * UK_BAND, top, UK_BAND, sh);
    for (int k = 0; k < shown; k++) {
        const TroopDef *t = troops[k];
        int sx = L.full.x + k * (sw + UK_BAND);
        int cx = sx + sw / 2, tw = sw - ML_PAD;
        uk_picture(troop_face(s, t->index), cx - tile / 2, top + ML_PAD, tile, tile);
        int ty = top + ML_PAD + tile + 4;
        char buf[64];
        const char *label = GameNumberName(g, counts[k]);
        if (label[0]) snprintf(buf, sizeof buf, "%s", label);
        else          snprintf(buf, sizeof buf, "%d", counts[k]);
        ty = foe_text_centred(buf, cx, ty, tw, 1, PAL_CLR(WHITE));
        int name_y = ty;
        foe_text_centred(t->name, cx, ty, tw, 2, PAL_CLR(YELLOW));
        ty = name_y + 2 * lh;
        snprintf(buf, sizeof buf, "%s %d", ui->fv_hp, t->hit_points);
        ty = foe_text_centred(buf, cx, ty, tw, 1, PAL_CLR(WHITE));
        snprintf(buf, sizeof buf, "%s %d-%d", ui->fv_dmg, t->melee_min, t->melee_max);
        foe_text_centred(buf, cx, ty, tw, 1, PAL_CLR(WHITE));
    }
    uk_scene_rows(&L, 2, pv ? pv->yn_cursor : 0, foe_row, (void *)g, TOUCH_LIST_PROMPT);
}

// ---------------------------------------------------------------------------
// Options screen (O key): the movement keys and the keybinds.
// ---------------------------------------------------------------------------

void modern_overlay_draw_options(const Game *g) {
    int pad = UK_INSET;
    int kb_n = (g && g->res) ? g->res->ui.keybind_count : 0;
    int fit = (CL_MAP_H - 2 * pad) / GH - 8 - 1;
    int kb_cols = (kb_n > fit) ? 2 : 1;
    int kb_rows = (kb_n + kb_cols - 1) / kb_cols;
    int rows = 8 + 1 + kb_rows;
    int w = (kb_cols == 2) ? CL_PANEL_WIDE_W : CL_PANEL_STD_W;
    int h = rows * GH + 2 * pad;
    if (h > CL_MAP_H) h = CL_MAP_H;
    int x = CL_CONTENT_X;
    int y = CL_STATUS_Y + CL_STATUS_H + CL_BAR_H;
    uk_panel(x, y, w, h);
    int tx = x + pad, ty = y + pad;
    static const struct { const char *keys; const char *label; } mv[8] = {
        { "\x18 or 2",   "Move Down"      },
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
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    int n = ui ? ui->keybind_count : 0;
    int max_row = (y + h - pad - ty) / GH;
    if (kb_cols == 1 && n > max_row) n = max_row;
    int col_w = (w - 2 * pad) / kb_cols;
    int per_col = (n + kb_cols - 1) / kb_cols;
    int ty0 = ty;
    for (int i = 0; i < n; i++) {
        const ResKeybind *kb = &ui->keybinds[i];
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
// Toast -- one line at the top of the area behind it.
// ---------------------------------------------------------------------------

void modern_overlay_draw_toast(void) {
    const char *msg = toast_text_current();
    if (!msg) return;
    int w = bfont_text_width(msg) + 2 * UK_INSET;
    int h = GH + 10;
    ML_Rect a = ml_area();
    int x = a.x + (a.w - w) / 2;
    int y = a.y + ml_space();
    uk_panel(x, y, w, h);
    bfont_draw(msg, x + UK_INSET, y + 5, PAL_CLR(YELLOW));
}

// ---------------------------------------------------------------------------
// Controls: an in-lay as tall as its settings.
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
    ControlsCtx c = { .g = g, .vis = 0 };
    for (int i = 0; i < count && c.vis < 8; i++) {
        if (g->res->controls.items[i].hidden) continue;
        c.vis_idx[c.vis++] = i;
    }
    if (c.vis == 0) return;
    int cur_k = views_controls_cursor();
    if (cur_k < 0) cur_k = 0;
    if (cur_k > c.vis) cur_k = c.vis;
    int rows = c.vis + 1;
    int h = uk_title_h() + UK_BAND + ml_list_height(rows);
    // As wide as its longest setting and value.
    int w = 400;
    for (int k = 0; k < rows; k++) {
        char label[96], right[48] = "";
        controls_row(&c, k, label, right, (int)sizeof label);
        int need = bfont_text_width(label) + bfont_text_width(right) + 6 * GW + 2 * ML_PAD;
        if (need > w) w = need;
    }
    ML_Rect b = uk_inlay(w, h, g->res->ui.controls_title, NULL);
    ml_list_draw(b.x, b.y, b.w, b.h, rows, cur_k, controls_row, &c, 0, uk_ink());
    // Taps answer to each row's digit (select and advance in one).
    int vis_rows = ml_list_fit(b.h);
    int first = ml_list_first(rows, cur_k, vis_rows);
    for (int k = first; k < rows && k < first + vis_rows; k++)
        touch_region(b.x, b.y + (k - first) * (ml_row_h() + ML_ROW_RULE), b.w, ml_row_h(), KEY_ONE + k);
}

// ---------------------------------------------------------------------------
// The dimmed scene beneath a detail view, prompt or dialog (REQ-430g).
// ---------------------------------------------------------------------------

void modern_overlay_dim_scene(void) {
    const Resources *r = resources_current();
    int a = overlay_dim_alpha(r ? r->render.dim : 0);
    if (a == 0) return;
    Color shade = { 0, 0, 0, (unsigned char)a };
    DrawRectangle(CL_MAP_X, CL_MAP_Y, CL_SIDEBAR_X + CL_SIDEBAR_W - CL_MAP_X, CL_MAP_H, shade);
}

// =============================================================================
//  Temple (the Augur's alcove) and dwelling -- the scene, then the in-lay
//  after a deal (the panorama always shows first)
// =============================================================================

void modern_overlay_draw_temple(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    char gold[48], buf[RES_BANNER_LEN], cost[16];
    uk_gold_text(g, gold, sizeof gold);
    UkScene L = uk_scene(bn->temple_title, gold, loc_texture(s, LOC_ALCOVE), 2);
    if (s && s->alcove_figure.id && res->sprites.alcove_figure_w > 0) {
        int ms = res->sprites.alcove_figure_frame_ms > 0 ? res->sprites.alcove_figure_frame_ms : 180;
        Texture2D fig = s->alcove_figure_anim[sprites_frame((int)(GetTime() * 1000.0 / ms), s->alcove_figure_frames)];
        if (!fig.id) fig = s->alcove_figure;
        uk_scene_blit(&L, fig, res->sprites.alcove_figure_x, res->sprites.alcove_figure_y,
                      res->sprites.alcove_figure_w, res->sprites.alcove_figure_h);
    }
    const ResZone *z = resources_zone_by_id(res, g->position.zone);
    snprintf(cost, sizeof cost, "%d", GameAlcoveCost(g, g->position.zone));
    ResTemplateVar iv[] = { { "ZONE", (z && z->name[0]) ? z->name : g->position.zone }, { "COST", cost } };
    resources_format_template(buf, sizeof buf, bn->temple_intro, iv, 2);
    uk_scene_intro(&L, buf);
    UkRows rows = { { bn->temple_learn, bn->location_leave }, { true, true } };
    const PromptView *pv = prompt_view();
    if (prompt_is_active() && pending_flow == FLOW_ALCOVE) {
        uk_scene_rows(&L, 2, pv->yn_cursor, uk_rows_fn, &rows, TOUCH_LIST_PROMPT);
    } else if (loc_deal_pending() && !loc_deal_revealed()) {
        uk_scene_rows(&L, 2, *loc_deal_cursor(), uk_rows_fn, &rows, TOUCH_LIST_PROMPT);
    } else {
        uk_scene_rows(&L, 2, -1, uk_rows_fn, &rows, 0);
        loc_deal_text(g, buf, sizeof buf);
        uk_result_inlay(loc_deal_title(g, true), s ? s->alcove_portrait : (Texture2D){ 0 }, buf,
                        bn->castle_continue, TOUCH_LIST_PROMPT);
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
    char title[128], gold[48], buf[RES_BANNER_LEN];
    snprintf(title, sizeof title, "%s - %s", kind ? kind : "", tr ? tr->name : "");
    uk_gold_text(g, gold, sizeof gold);
    LocKind lk = LOC_PLAINS;
    if (kind && strcmp(kind, ui->dwelling_kind_forest) == 0)  lk = LOC_FOREST;
    if (kind && strcmp(kind, ui->dwelling_kind_hill) == 0)    lk = LOC_HILLCAVE;
    if (kind && strcmp(kind, ui->dwelling_kind_dungeon) == 0) lk = LOC_DUNGEON;
    UkScene L = uk_scene(title, gold, loc_texture(s, lk), 2);
    uk_scene_figure(&L, troop_standing(s, ti), CL_TILE_W);
    Texture2D face = troop_face(s, ti);

    char cb[16], pb[16];
    snprintf(pb, sizeof pb, "%d", pop);
    snprintf(cb, sizeof cb, "%d", cost);
    ResTemplateVar iv[] = { { "COUNT", pb }, { "TROOP", tr ? tr->name : "" }, { "COST", cb } };
    char intro[2 * RES_BANNER_LEN + 2];
    resources_format_template(buf, sizeof buf, bn->dwelling_intro, iv, 3);
    snprintf(intro, sizeof intro, "%s%s%s", buf, cap <= 0 ? " " : "",
             cap > 0 ? "" : (tr && g->stats.gold < tr->recruit_cost) ? bn->town_no_gold : bn->army_cannot_handle);
    uk_scene_intro(&L, intro);

    const PromptView *pv = prompt_view();
    bool offer = prompt_is_active() && pending_flow == FLOW_RECRUIT;
    UkRows rows = { { bn->dwelling_recruit_row, bn->location_leave }, { cap > 0, true } };
    if (offer && pv->step_open) {
        uk_scene_rows(&L, 2, -1, uk_rows_fn, &rows, 0);
        char heading[RES_BANNER_LEN], avail[RES_BANNER_LEN], each[RES_BANNER_LEN], lead[RES_BANNER_LEN],
             total[RES_BANNER_LEN], vb[16];
        static char act_label[RES_BANNER_LEN];
        ResTemplateVar hv[] = { { "TROOP", tr ? tr->name : "" } };
        resources_format_template(heading, sizeof heading, bn->count_heading, hv, 1);
        ResTemplateVar av2[] = { { "COUNT", pb }, { "TROOP", tr ? tr->name : "" } };
        resources_format_template(avail, sizeof avail, ui->dwelling_info_available, av2, 2);
        castle_fmt(each, sizeof each, bn->castle_cost, cb, NULL);
        snprintf(vb, sizeof vb, "%d", pv->step_max);
        ResTemplateVar sv[] = { { "MAX", vb } };
        resources_format_template(lead, sizeof lead, bn->count_of_lead, sv, 1);
        snprintf(vb, sizeof vb, "%d", cost * pv->step_value);
        ResTemplateVar c1[] = { { "GOLD", vb } };
        resources_format_template(total, sizeof total, bn->count_cost, c1, 1);
        snprintf(vb, sizeof vb, "%d", pv->step_value);
        ResTemplateVar av[] = { { "COUNT", vb } };
        resources_format_template(act_label, sizeof act_label, bn->count_recruit, av, 1);
        const char *lines[] = { avail, each, "", lead, total };
        Color colors[] = { PAL_CLR(WHITE), PAL_CLR(WHITE), PAL_CLR(WHITE), PAL_CLR(WHITE), PAL_CLR(YELLOW) };
        uk_count_inlay(heading, face, lines, colors, 5, pv->step_value, pv->step_max, act_label, bn->count_cancel,
                       TOUCH_LIST_PROMPT);
    } else if (offer) {
        uk_scene_rows(&L, 2, pv->yn_cursor, uk_rows_fn, &rows, TOUCH_LIST_PROMPT);
    } else if (loc_deal_pending() && !loc_deal_revealed()) {
        rows.enabled[0] = true;
        uk_scene_rows(&L, 2, *loc_deal_cursor(), uk_rows_fn, &rows, TOUCH_LIST_PROMPT);
    } else {
        uk_scene_rows(&L, 2, -1, uk_rows_fn, &rows, 0);
        loc_deal_text(g, buf, sizeof buf);
        uk_result_inlay(loc_deal_title(g, false), face, buf, bn->castle_continue, TOUCH_LIST_PROMPT);
    }
}
