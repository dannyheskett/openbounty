// src/modern/overlay.c
//
// The overlay for a pack that declared render.mode "modern": square tiles, a
// TrueType face, the lit cursor row (REQ-430e) and the dimmed screen beneath
// (REQ-430g). Every panel here is a page of the page engine
// (src/modern/page.h), built from the shared kit (src/modern/uikit.h): a
// message is the message box on the foot of the map, and every step of a
// place -- its room, a roll of troops, How many, a question, an outcome -- is
// the one place page, standing in place of the step before it, never over it.
//
// The DOS original's overlay is in src/legacy/overlay.c and is frozen: it must
// not be edited to serve anything in this file.
//
// Called only through the dispatcher in src/overlay.c.

#include "overlay.h"
#include "gfx.h"
#include "overlay_impl.h"
#include "modern/mlayout.h"
#include "modern/uikit.h"
#include "modern/page.h"
#include "map_render.h"        // the hero's cell, for the bridge
#include "touch.h"
#include "uitouch.h"
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
#include "present.h"
#include "modern/castle.h"
#include "modern/mlist.h"
#include "modern/location.h"
#include "modern/gamemenu.h"
#include "shell_audience.h"
#include "screens/dwelling.h"
#include <stdio.h>
#include <string.h>

#define GW  BFONT_GLYPH_W
#define GH  BFONT_GLYPH_H

static const Sprites *s_dialog_sprites;
void modern_overlay_set_sprites(const Sprites *s) { s_dialog_sprites = s; }
const Sprites *modern_overlay_sprites(void) { return s_dialog_sprites; }
static const Game *s_game;
void modern_overlay_set_game(const Game *g) { s_game = g; }
const Game *modern_overlay_game(void) { return s_game; }

// =============================================================================
//  Faces and figures
// =============================================================================

// A portrait's current frame (animated), or nothing.
static Texture2D portrait_frame(const Sprites *s, int idx, double fps) {
    if (!s || idx < 0 || idx >= s->portrait_count || s->portrait_frames[idx] <= 0) return (Texture2D){ 0 };
    return sprites_strip(s->portrait_anim[idx], s->portrait_frames[idx], (int)(ui_anim_time() * fps));
}

static Texture2D villain_face(const Sprites *s, int idx) {
    if (!s || idx < 0 || idx >= s->villain_count) return (Texture2D){ 0 };
    Texture2D face = s->villain_anim_frames[idx] > 0
        ? sprites_strip(s->villain_anim[idx], s->villain_anim_frames[idx], (int)(ui_anim_time() * UK_FACE_FPS))
        : s->villain_portrait[idx];
    return face.id ? face : s->villain_portrait[idx];
}

static Texture2D troop_face(const Sprites *s, int idx) {
    if (!s || idx < 0 || idx >= s->troop_count) return (Texture2D){ 0 };
    return s->troop_portrait[idx].id ? s->troop_portrait[idx] : s->troop_sprite[idx];
}

// A troop standing: frames 0 and 1 in turn, at the one idle pace.
static Texture2D troop_standing(const Sprites *s, int idx) {
    if (!s || idx < 0 || idx >= s->troop_count) return (Texture2D){ 0 };
    Texture2D t = sprites_strip(s->troop_anim[idx], s->troop_anim_frames[idx],
                                sprites_stand((int)(ui_anim_time() * UK_IDLE_FPS)));
    return t.id ? t : s->troop_sprite[idx];
}

// A person's figure (a portrait's standing strip), at the one idle pace.
static Texture2D figure(const Sprites *s, int idx) {
    return portrait_frame(s, idx, UK_IDLE_FPS);
}

// A talking face (a portrait), at its own pace.
static Texture2D face_of(const Sprites *s, int idx) {
    return portrait_frame(s, idx, UK_FACE_FPS);
}

// =============================================================================
//  Messages
// =============================================================================

// The picture a message names (dialog_face), or none.
static Texture2D note_face(void) {
    int idx = 0;
    int kind = dialog_face(&idx);
    const Sprites *s = s_dialog_sprites;
    if (kind == REQ_FACE_VILLAIN) return villain_face(s, idx);
    if (kind == REQ_FACE_TROOP)   return troop_face(s, idx);
    if (kind == REQ_FACE_ARTIFACT && s && idx >= 0 && idx < s->view_icon_extra_base) return s->view_icon[idx];
    if (kind == REQ_FACE_PORTRAIT) return face_of(s, idx);
    return (Texture2D){ 0 };
}

int modern_overlay_dialog_page_count(void) {
    // A note drawn as a scene shows its words at once; every other note is
    // the message box, paged.
    if (dialog_kind() == PIO_NOTE_SCENE) return 1;
    return page_message_pages(dialog_body_text(), note_face().id != 0);
}

static void draw_message(void);
static void draw_note_scene(void);

// One drawing function per kind of note; nothing is inferred here.
void modern_overlay_draw_note(void) {
    if (dialog_kind() == PIO_NOTE_SCENE) draw_note_scene();
    else                                 draw_message();
}

// A message: always the message box on the foot of the map -- of the
// battlefield in a fight -- whatever is open, with its picture when it names
// one, paged.
static void draw_message(void) {
    const char *hdr = dialog_header_text();
    const char *body = dialog_body_text();
    const Resources *res = resources_current();
    const char *cont = res ? res->banners.castle_continue : "";
    PageAnchor at = dialog_kind() == PIO_NOTE_OVER_FIELD ? PAGE_FIELD_FOOT : PAGE_MAP_FOOT;
    bool bridge = res && bridge_state == BRIDGE_STATE_DIRECTION;
    if (bridge) {
        // The bridge asks for a square: the four beside the hero outlined, and
        // words for a touch screen as well as the keys. It waits for the
        // square, so it has no Continue, and the map round it takes the tap.
        body = res->banners.spell_bridge_prompt_modern;
        int hx = 0, hy = 0;
        map_render_last_hero_cell(&hx, &hy);
        static const int d[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
        int z = present_get_zoom();
        gfx_clip_begin(CL_MAP_X * z, CL_MAP_Y * z, CL_MAP_W * z, CL_MAP_H * z);   // the map's own edge
        for (int k = 0; k < 4; k++)
            for (int t = 0; t < 3; t++)
                gfx_rect_lines(hx + d[k][0] * CL_TILE_W + t, hy + d[k][1] * CL_TILE_H + t,
                               CL_TILE_W - 2 * t, CL_TILE_H - 2 * t, PAL_CLR(YELLOW));
        gfx_clip_end();
    }
    page_message(hdr, body, dialog_page_current(), bridge ? NULL : cont, note_face(), at);
}

// PIO_NOTE_SCENE: a place's page -- the scene's own picture as its band, the
// words beside Continue. A pack without the scene art gets the message box.
static void draw_note_scene(void) {
    const Resources *res = resources_current();
    if (!res) return;
    int idx = 0;
    int kind = dialog_face(&idx);
    const Sprites *s = s_dialog_sprites;
    // A one-time vista draws the pack's own scene art; the temporary-death
    // scene draws the class's.
    Texture2D scene = { 0 };
    if (kind == REQ_FACE_EVENT) {
        if (s && idx >= 0 && idx < s->event_scene_count) scene = s->event_scene[idx];
    } else if (s && idx >= 0 && idx < s->class_count) {
        scene = s->class_disgraced[idx];
    }
    if (!scene.id) { draw_message(); return; }
    const char *hdr = dialog_header_text();
    const char *title = (hdr && hdr[0]) ? hdr : "";
    for (int i = 0; !title[0] && i < res->castle_count; i++)
        if (resources_castle_is_home(&res->castles[i])) title = res->castles[i].name;
    PagePlace P = page_scene(title, NULL, scene, dialog_body_text());
    uk_flow(P.words.x, P.words.y, P.words.w, P.words.x, 0, P.words.y + P.words.h,
            dialog_body_text(), PAL_CLR(WHITE));
    // Continue is the page's one action: a tap anywhere is it.
    UkRows rows = { { res->banners.castle_continue }, { true }, 1 };
    ml_rows_draw(P.rows, 1, 1, 0, uk_rows_fn, &rows, 0);
}

// A person speaking in a place: their face at 2x in the words' place with
// what they say beside it, and the rows on the column's foot.
static void person_says(const PagePlace *P, Texture2D face, const UkDoc *d) {
    const int size = 2 * CL_TILE_W;
    if (face.id) uk_picture(face, P->words.x, P->words.y, size, size);
    uk_doc_draw(d, P->words, face.id ? size : 0, size, -1, true);
}

// An outcome in a place: a person's page -- the one who answers and what they
// say, Continue on the column's foot.
static void place_outcome(const char *title, const char *right, Texture2D bd, Texture2D face,
                          const UkDoc *d, const char *cont_label) {
    (void)bd;
    PagePlace P = page_person(title, right, 1);
    UkRows cont = { { cont_label }, { true }, 1 };
    ml_rows_draw(P.rows, 1, 1, 0, uk_rows_fn, &cont, TOUCH_LIST_PROMPT);
    person_says(&P, face, d);
}

// A place's Yes and No, on the column's foot: No is the row Escape presses.
static bool yes_no_rows(void *ctx, int i, char *label, char *right, int cap) {
    const Resources *res = (const Resources *)ctx;
    snprintf(label, (size_t)cap, "%s", i == 0 ? res->ui.prompt_yes : res->ui.prompt_no);
    if (i == 1) ml_exit_hint(right);
    else snprintf(right, 48, "%s", ml_keys_shown() ? "Y" : "");
    return true;
}

// =============================================================================
//  Location backdrops
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

// =============================================================================
//  Town -- the square (Visit the town, Leave), then the services and each
//  service, its question and its outcome, every one the town's place page
// =============================================================================

// The town screen's picture: the town's own if it names one, else its zone's,
// else the pack's shared one (REQ-221d).
static Texture2D town_backdrop_for(const Game *g, const Sprites *s, const ResTown *tw) {
    if (!s) return (Texture2D){ 0 };
    const Resources *res = g ? g->res : NULL;
    if (res && tw) {
        int ti = (int)(tw - res->towns);
        if (ti >= 0 && ti < s->town_backdrop_count && s->town_backdrop_own[ti].id)
            return s->town_backdrop_own[ti];
        for (int zi = 0; zi < res->zone_count && zi < s->zone_town_backdrop_count; zi++)
            if (strcmp(res->zones[zi].id, tw->zone) == 0 && s->zone_town_backdrop[zi].id)
                return s->zone_town_backdrop[zi];
    }
    return s->town_backdrop;
}

static int town_backdrop_troop(const Game *g, const char *key) {
    int nt = troops_count();
    int npool = 0;
    for (int i = 0; i < nt; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0) npool++;
    }
    unsigned long h = g ? g->seed ^ 0xA1B2C3u : 0;
    for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
    if (npool < 1) return nt > 0 ? (int)(h % (unsigned long)nt) : 0;
    // The pick-th castle troop in catalog order.
    int pick = (int)(h % (unsigned long)npool);
    for (int i = 0; i < nt; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0 && pick-- == 0) return i;
    }
    return 0;
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
    for (int i = 0; i < g->castle_count; i++) {
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
    // The last row goes back: it is the row Escape presses.
    bool back = i == views_town_list_rows(c->g) - 1;
    if (back) ml_exit_hint(right);
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

// The town's head townsperson standing in the square, or a castle troop.
static Texture2D town_figure(const Game *g, const Sprites *s, const ResTown *tw, const char *name) {
    int head = tw ? resources_portrait_index(g->res, tw->headman) : -1;
    Texture2D fig = figure(s, head);
    return fig.id ? fig : troop_standing(s, town_backdrop_troop(g, name));
}

void modern_overlay_draw_town(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const char *name = views_town_display_name();
    const char *key = views_town_record_key();
    const ResTown *tw = key ? resources_town_by_id(res, key) : NULL;
    char title[128], gold[48], buf[RES_BANNER_LEN];
    uk_gold_text(g, gold, sizeof gold);
    Texture2D bd = town_backdrop_for(g, s, tw);
    Texture2D fig = town_figure(g, s, tw, name);

    // The square: the head townsperson standing in it, Visit the town and
    // Leave.
    if (!views_town_visiting()) {
        town_title(g, title, sizeof title, false);
        ResTemplateVar iv[] = { { "HERO", g->character.name }, { "TOWN", (name && name[0]) ? name : "" },
                                { "ZONE", town_zone_name(g) } };
        // A town with no dock does not boast a harbour.
        const char *intro = (tw && tw->boat_x < 0 && bn->town_intro_inland[0])
                          ? bn->town_intro_inland : bn->town_intro;
        resources_format_template(buf, sizeof buf, intro, iv, 3);
        PagePlace P = page_place(title, gold, bd, 2);
        uk_scene_figure(&P.band, fig, CL_TILE_W);
        uk_flow(P.words.x, P.words.y, P.words.w, P.words.x, 0, P.words.y + P.words.h, buf, PAL_CLR(WHITE));
        char visit[RES_BANNER_LEN + 4];
        snprintf(visit, sizeof visit, "%s >", bn->town_visit);
        UkRows scene_rows = { { visit, bn->location_leave }, { true, true }, 2 };
        ml_rows_draw(P.rows, 2, 1, views_town_scene_cursor(), uk_rows_fn, &scene_rows, TOUCH_LIST_TOWN);
        return;
    }

    TownList list = views_town_list();
    bool menu = list == TOWN_LIST_MENU;
    const char *info = views_town_info_text();
    int rows = views_town_list_rows(g);
    int cursor = views_town_list_cursor();
    TownList shown = menu ? town_list_for(views_town_cursor()) : list;
    if (menu && views_town_cursor() == TOWN_ROW_LEAVE) shown = TOWN_LIST_MENU;

    // The services, a contract, a service, its question and its outcome are
    // a person's page: the rows at the left, and the one who speaks -- the
    // person of the service, or the wanted face -- with their words beside
    // them.
    char t2[128];
    town_title(g, t2, sizeof t2, !menu);
    Texture2D face = { 0 };
    if (list == TOWN_LIST_CONTRACTS) {
        const VillainDef *v = town_shown_villain(g);
        face = v ? villain_face(s, v->index) : (s ? s->hud_contract_silhouette : (Texture2D){ 0 });
    } else if (shown == TOWN_LIST_BOAT && !views_town_boat_available(g)) {
        face = s ? s->hud_boat_silhouette : (Texture2D){ 0 };
    } else {
        face = face_of(s, town_person(g, shown));
    }

    bool result = views_town_result_dialog() && info;
    UkDoc d = { 0 };
    const PromptView *pv = prompt_view();
    bool asking = prompt_is_active() && pv && pv->kind == PK_YES_NO;
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

    // The rows the page holds decide its taps: Continue alone is its one
    // action; anything else is a choice. Information has Back alone.
    int first_row = (!asking && !result && list == TOWN_LIST_INFO) ? rows - 1 : 0;
    int n_rows = asking ? 2 : result ? 1 : rows - first_row;
    PagePlace P = page_person(t2, gold, n_rows);
    if (asking) {
        // The question's answers stand on the column's foot.
        ml_rows_draw(P.rows, 2, 2, pv->yn_cursor, yes_no_rows, (void *)res, TOUCH_LIST_PROMPT);
    } else if (result) {
        UkRows cont = { { bn->castle_continue }, { true }, 1 };
        ml_rows_draw(P.rows, 1, 1, 0, uk_rows_fn, &cont, TOUCH_LIST_PROMPT);
    } else {
        // A list from the column's top, Back on its foot.
        TownRowsCtx rc = { g, menu };
        ML_Rect col = P.rows;
        if (first_row > 0) {
            ml_list_draw_ex(col.x, col.y + col.h - ml_list_height(1), col.w, ml_list_height(1), 1,
                            cursor - first_row, town_row_fn, &rc, TOUCH_LIST_TOWN, uk_ink(), first_row);
        } else {
            ml_rows_draw(col, rows, 1, cursor, town_row_fn, &rc, TOUCH_LIST_TOWN);
        }
        if (list == TOWN_LIST_CONTRACTS) {
            // The contract held: a dot before its name.
            int top_n = rows - 1;
            int vis = ml_list_fit(ml_rows_top_h(col, 1));
            int first = ml_list_first(top_n, cursor < top_n ? cursor : 0, vis);
            for (int i = first; i < top_n && i < first + vis; i++) {
                int slot = views_town_contract_slot(g, i);
                if (slot < 0 || strcmp(g->contract.cycle[slot], g->contract.active_id) != 0) continue;
                int ry = col.y + (i - first) * (ml_row_h() + ML_ROW_RULE);
                gfx_circle(col.x + UK_INSET + GW / 2 - 2, ry + ml_row_h() / 2, 3,
                           i == cursor ? uk_ink() : PAL_CLR(YELLOW));
            }
        }
    }
    const int size = 2 * CL_TILE_W;
    if (face.id) uk_picture(face, P.words.x, P.words.y, size, size);
    if (menu || asking || result) {
        views_town_set_detail_pages(1);
        uk_doc_draw(&d, P.words, face.id ? size : 0, size, -1, true);
    } else {
        int pages = uk_doc_draw(&d, P.words, face.id ? size : 0, size, 0, false);
        views_town_set_detail_pages(pages);
        uk_doc_draw(&d, P.words, face.id ? size : 0, size, views_town_detail_page(), true);
    }
}

// =============================================================================
//  Castles -- the scene (Recruit / Audience / Leave, or Garrison / Withdraw /
//  Leave), then each page, question and answer in place of the one before
// =============================================================================


static void castle_fmt(char *out, int cap, const char *tmpl, const char *count, const char *max) {
    ResTemplateVar v[] = { { "COUNT", count }, { "MAX", max ? max : "" },
                           { "RANK", count }, { "GOLD", count } };
    resources_format_template(out, cap, tmpl, v, 4);
}

static int castle_pick_troop(const Game *g, const char *key) {
    int n = modern_castle_pool_count();
    if (n < 1) return -1;
    unsigned long h = g ? (g->seed ^ 0x0CA571E5u) : 0;
    for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
    return modern_castle_pool_troop((int)(h % (unsigned long)n));
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

// The Promotion ceremony: the throne room, the award at 2x with the Emperor's
// words and the rank's gains beside it, Continue.
static void castle_draw_promotion(const Game *g, const Sprites *s, const ResCastle *rc, Texture2D bd) {
    const Resources *res = g->res;
    int needed = 0, rank = 0;
    modern_castle_audience(&needed, &rank);
    char pgold[48], buf[RES_BANNER_LEN];
    uk_gold_text(g, pgold, sizeof pgold);
    UkDoc d = { 0 };
    if (rc) {
        audience_substitute(g, needed, rc->special.audience_rank_up, buf, sizeof buf);
        uk_doc_add(&d, buf, PAL_CLR(WHITE));
    }
    castle_gains(g, rank, &d);
    int idx = (rc && rank >= 0 && rank < 4) ? resources_portrait_index(res, rc->special.promotion[rank]) : -1;
    // The award is still: its first frame.
    place_outcome(res->banners.castle_action_promotion, pgold, bd, portrait_frame(s, idx, 0.0), &d,
                  res->banners.castle_continue);
}

// The troop a castle row stands for, with its numbers beside its portrait:
// what it is, and what you have and can move.
static void castle_troop_detail(const Game *g, const Sprites *s, const TroopDef *pt, McPage page,
                                const CastleRecord *cr, ML_Rect a) {
    const ResBanners *bn = &g->res->banners;
    const ResUI *ui = &g->res->ui;
    const int size = 2 * CL_TILE_W;
    uk_picture(troop_face(s, pt->index), a.x, a.y, size, size);
    char buf[RES_BANNER_LEN], nb[32];
    int x = a.x + size + UK_INSET, y = a.y;
    int lh = uk_line_h();
    int tw = a.x + a.w - x;
    uk_line(pt->name, x, y, tw, PAL_CLR(YELLOW));
    y += lh;
    int in_army = 0, in_garrison = 0;
    for (int k = 0; k < GAME_ARMY_SLOTS; k++) {
        if (strcmp(g->army[k].id, pt->id) == 0) in_army += g->army[k].count;
        if (cr && strcmp(cr->garrison[k].id, pt->id) == 0) in_garrison += cr->garrison[k].count;
    }
    // The numbers are the whole troop's, as the Army sheet shows them: what
    // you have on this page. The recruit list is a price list, so it keeps one
    // soldier's. Skill and Move belong to one soldier either way.
    int many = page == MC_RECRUIT ? 1
             : page == MC_WITHDRAW ? in_garrison : in_army;
    struct { const char *label; char value[32]; } rows[5];
    snprintf(rows[0].value, 32, "%d", pt->skill_level);                      rows[0].label = ui->army_skill;
    snprintf(rows[1].value, 32, "%d", pt->move_rate);                        rows[1].label = ui->army_move;
    snprintf(rows[2].value, 32, "%d", pt->hit_points * many);                rows[2].label = ui->army_hit_points;
    snprintf(rows[3].value, 32, "%d-%d", pt->melee_min * many, pt->melee_max * many);
                                                                             rows[3].label = ui->army_damage;
    snprintf(rows[4].value, 32, "%d", pt->recruit_cost * many);              rows[4].label = ui->army_g_cost;
    for (int i = 0; i < 5; i++, y += lh) {
        bfont_draw(rows[i].label, x, y, PAL_CLR(YELLOW));
        bfont_draw_right(rows[i].value, x + tw, y, PAL_CLR(WHITE));
    }
    // What you have and can move goes under the numbers, still beside the
    // picture; a caution -- too few men to lead, or an army grown past your
    // leadership -- runs in gold along the foot, where it has the whole width,
    // when it clears the picture there, and under the numbers when not.
    UkDoc d = { 0 };
    char note[RES_BANNER_LEN] = "";
    snprintf(nb, sizeof nb, "%d", in_army);
    castle_fmt(buf, sizeof buf, bn->castle_have, nb, NULL);
    uk_doc_add(&d, buf, PAL_CLR(WHITE));
    if (page == MC_RECRUIT && !modern_castle_troop_offered(g, pt)) {
        snprintf(nb, sizeof nb, "%d", pt->hit_points * 6);
        castle_fmt(note, sizeof note, bn->castle_needs_leadership, nb, NULL);
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
            GameArmyTotalLeadership(g) + pt->hit_points * in_garrison > g->stats.leadership_current)
            snprintf(note, sizeof note, "%s", bn->castle_over_leadership);
    }
    int foot = a.y + a.h;
    if (note[0]) {
        int n = uk_lines(note, a.w);
        if (foot - n * lh >= a.y + size + UK_INSET) {
            foot -= n * lh;
            uk_flow(a.x, foot, a.w, 0, 0, a.y + a.h, note, PAL_CLR(YELLOW));
            foot -= UK_INSET;
        } else {
            uk_doc_gap(&d);
            uk_doc_add(&d, note, PAL_CLR(YELLOW));
        }
    }
    ML_Rect under = { x, y + UK_INSET, a.x + a.w - x, foot - (y + UK_INSET) };
    uk_doc_draw(&d, under, 0, 0, -1, true);
}

typedef struct { const Game *g; } CastleRowsCtx;

static bool castle_row_fn(void *ctx, int i, char *label, char *right, int cap) {
    const Game *g = ((const CastleRowsCtx *)ctx)->g;
    const char *id = NULL;
    right[0] = '\0';
    modern_castle_row(g, i, label, cap, &id);
    // The last row goes back: the row Escape presses.
    if (i == modern_castle_rows(g) - 1) ml_exit_hint(right);
    const TroopDef *t = (modern_castle_page() == MC_RECRUIT && id) ? troop_by_id(id) : NULL;
    return !t || modern_castle_troop_offered(g, t);
}

// The count's two answers: its action, and Cancel -- the row Escape presses.
static bool count_rows(void *ctx, int i, char *label, char *right, int cap) {
    const char *const *labels = (const char *const *)ctx;
    snprintf(label, (size_t)cap, "%s", labels[i]);
    if (i == 1) ml_exit_hint(right);
    else right[0] = '\0';
    return true;
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
    const char *msg = modern_castle_message();

    // The Emperor's palace has a room of its own for each page: the atrium
    // where his usher greets you, the armoury where the master of arms
    // musters recruits, and the throne room where the Emperor receives you.
    // Every other castle keeps the shared hall.
    bool audience = page == MC_AUDIENCE || page == MC_PROMOTION;
    Texture2D bd = loc_texture(s, LOC_CASTLE);
    if (home && s) {
        Texture2D own = audience ? s->palace[2] : (page == MC_MENU ? s->palace[0] : s->palace[1]);
        if (own.id) bd = own;
    }
    if (page == MC_PROMOTION) { castle_draw_promotion(g, s, rc, bd); return; }

    // Who stands in the room: the keeper of this page, or on a castle of
    // your own the troop it holds most.
    const char *fig_id = !rc ? ""
                       : audience ? rc->special.figure
                       : (page == MC_MENU && rc->special.greeter_figure[0]) ? rc->special.greeter_figure
                       : rc->special.barracks_figure[0] ? rc->special.barracks_figure : rc->special.figure;
    Texture2D fig = (home && rc) ? figure(s, resources_portrait_index(res, fig_id)) : (Texture2D){ 0 };
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
    // The keeper's face, for what the castle tells you.
    Texture2D keeper = (home && rc) ? face_of(s, resources_portrait_index(res,
                           audience ? rc->special.portrait
                           : rc->special.barracks_portrait[0] ? rc->special.barracks_portrait
                                                              : rc->special.portrait))
                                    : (Texture2D){ 0 };

    const char *titles[] = { "", bn->castle_menu_recruit, bn->castle_menu_audience,
                             bn->castle_menu_garrison, bn->castle_menu_withdraw, "" };
    char title[128];
    if (page == MC_MENU) snprintf(title, sizeof title, "%s", cname);
    else                 snprintf(title, sizeof title, "%s > %s", cname, titles[page]);

    // A message -- what came of a choice -- is the keeper speaking.
    if (msg) {
        UkDoc md = { 0 };
        uk_doc_add(&md, msg, PAL_CLR(WHITE));
        place_outcome(title, gold, bd, keeper, &md, bn->castle_continue);
        return;
    }

    if (page == MC_MENU) {
        int mcur = modern_castle_menu_cursor();
        const char *inv = mcur == 2 ? bn->gmd_leave
                        : home ? (mcur == 0 ? bn->castle_invite_recruit : bn->castle_invite_audience)
                               : (mcur == 0 ? bn->castle_invite_garrison : bn->castle_invite_withdraw);
        ResTemplateVar v[] = { { "HERO", g->character.name }, { "CASTLE", cname } };
        resources_format_template(buf, sizeof buf, inv, v, 2);
        PagePlace P = page_place(title, gold, bd, 3);
        uk_scene_figure(&P.band, fig, CL_TILE_W);
        uk_flow(P.words.x, P.words.y, P.words.w, P.words.x, 0, P.words.y + P.words.h, buf, PAL_CLR(WHITE));
        char r0[RES_BANNER_LEN + 4], r1[RES_BANNER_LEN + 4];
        snprintf(r0, sizeof r0, "%s >", home ? bn->castle_menu_recruit : bn->castle_menu_garrison);
        snprintf(r1, sizeof r1, "%s >", home ? bn->castle_menu_audience : bn->castle_menu_withdraw);
        UkRows menu_rows = { { r0, r1, bn->location_leave }, { true, true, true }, 3 };
        ml_rows_draw(P.rows, 3, 1, mcur, uk_rows_fn, &menu_rows, TOUCH_LIST_CASTLE);
        return;
    }

    int rows = modern_castle_rows(g);
    CastleRowsCtx cc = { g };
    const char *row_troop = NULL;
    char label[64];
    if (!audience) modern_castle_row(g, cursor, label, sizeof label, &row_troop);
    const TroopDef *pt = row_troop ? troop_by_id(row_troop) : NULL;

    if (audience) {
        // The throne room throughout: the Emperor standing before his throne.
        char nb[16], mb[16];
        const char *rank_title = g->character.cls.rank_title;
        int fig_x = (ML_BACKDROP_W * 3 - 2 * CL_TILE_W) / 2;       // centred on the room
        const PromptView *pv = prompt_view();
        if (prompt_is_active() && pv && pv->kind == PK_YES_NO) {
            // The tribute's question: the Emperor asking it.
            UkDoc qd = { 0 };
            uk_doc_add(&qd, pv->body, PAL_CLR(WHITE));
            PagePlace P = page_person(title, gold, 2);
            ml_rows_draw(P.rows, 2, 2, pv->yn_cursor, yes_no_rows, (void *)res, TOUCH_LIST_PROMPT);
            person_says(&P, keeper, &qd);
            return;
        }

        // His answer, until Continue.
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
        }
        if (rd.n > 0) {
            place_outcome(title, gold, bd, keeper, &rd, bn->castle_continue);
            return;
        }

        // The audiences, and where the hero stands written beside them.
        UkDoc d = { 0 };
        const ClassDef *cls = class_by_id(g->character.cls.id);
        int rank = g->character.cls.rank_index;
        const ResEconomy *ec = &res->economy;
        if (ec->audiences && cursor == 1) {
            snprintf(nb, sizeof nb, "%d", GameArtifactsFound(g));
            snprintf(mb, sizeof mb, "%d", artifacts_count());
            castle_fmt(buf, sizeof buf, bn->castle_artifacts, nb, mb);
            uk_doc_add(&d, buf, PAL_CLR(WHITE));
        } else if (ec->audiences && cursor == 2) {
            snprintf(nb, sizeof nb, "%d", ec->tribute_cost);
            castle_fmt(buf, sizeof buf, bn->castle_cost, nb, NULL);
            uk_doc_add(&d, buf, PAL_CLR(WHITE));
        } else {
            castle_fmt(buf, sizeof buf, bn->castle_rank, rank_title, NULL);
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
        PagePlace P = page_place(title, gold, bd, rows);
        uk_scene_figure(&P.band, fig, fig_x);
        ml_rows_draw(P.rows, rows, 1, cursor, castle_row_fn, &cc, TOUCH_LIST_CASTLE);
        uk_doc_draw(&d, P.words, 0, 0, -1, true);
        return;
    }

    // Recruit, Garrison, Withdraw: the room with its keeper standing in it,
    // the roll of troops at the left, the one under the cursor beside it.
    // How many takes the roll's rows and the troop's place in turn.
    int cv = 0, cmax = 0;
    bool counting = modern_castle_stepper(&cv, &cmax) && pt;
    PagePlace P = page_place(title, gold, bd, counting ? 2 : rows);
    uk_scene_figure(&P.band, fig, CL_TILE_W);
    if (counting) {
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
        const char *labels[2] = { act_label, bn->count_cancel };
        ml_rows_draw(P.rows, 2, 2, 0, count_rows, (void *)labels, TOUCH_LIST_CASTLE);
        uk_count(P.words, heading, sub, cost[0] ? cost : NULL, cv, cmax);
        return;
    }
    ml_rows_draw(P.rows, rows, 1, cursor, castle_row_fn, &cc, TOUCH_LIST_CASTLE);
    if (pt) {
        castle_troop_detail(g, s, pt, page, cr, P.words);
    } else {
        UkDoc d = { 0 };
        if (rows == 1 && (page == MC_GARRISON || page == MC_WITHDRAW)) {
            uk_doc_add(&d, bn->castle_no_troops, PAL_CLR(WHITE));
        } else {
            ResTemplateVar v2[] = { { "HERO", g->character.name }, { "CASTLE", cname } };
            resources_format_template(buf, sizeof buf, page == MC_RECRUIT ? bn->castle_invite_recruit
                                      : page == MC_GARRISON ? bn->castle_invite_garrison : bn->castle_invite_withdraw, v2, 2);
            uk_doc_add(&d, buf, PAL_CLR(WHITE));
        }
        uk_doc_draw(&d, P.words, 0, 0, -1, true);
    }
}

// =============================================================================
//  Foe -- the enemy standing on the plains, a card per troop, Fight / Evade
// =============================================================================

static bool foe_row(void *ctx, int i, char *label, char *right, int cap) {
    const Game *g = (const Game *)ctx;
    const ResBanners *bn = &g->res->banners;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", i == 0 ? bn->foe_fight : bn->foe_evade);
    // Evade is the row Escape presses, while there is somewhere to run.
    if (i == 1 && !pending_foe_evade_blocked) ml_exit_hint(right);
    return i == 0 || !pending_foe_evade_blocked;
}

// A number on a foe's card, centred: its label gold and its value white, as
// every number is labelled. Returns the y under it.
static int foe_stat(const char *label, const char *value, int cx, int y, int w) {
    char line[64];
    snprintf(line, sizeof line, "%s %s", label, value);
    if (bfont_text_width(line) > w) return uk_words_centred(line, cx, y, w, 1, PAL_CLR(WHITE));
    int x = cx - bfont_text_width(line) / 2;
    char head[48];
    snprintf(head, sizeof head, "%s ", label);
    bfont_draw(head, x, y, PAL_CLR(YELLOW));
    bfont_draw(value, x + bfont_text_width(head), y, PAL_CLR(WHITE));
    return y + uk_line_h();
}

void modern_overlay_draw_foe(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    const ResUI *ui = &res->ui;
    const PromptView *pv = prompt_view();
    const int tile = CL_TILE_W, lh = uk_line_h();

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
    UkScene L = page_foe(ui->dt_foes, right, loc_texture(s, LOC_PLAINS));

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
    // A card per troop, parted by bands, and each troop standing at 1x on the
    // plains over its own card, facing the hero.
    const int STRIPES = 5;
    int top = L.intro_y;
    int sw = (L.full.w - (STRIPES - 1) * UK_BAND) / STRIPES;
    int sh = L.rows_y - UK_BAND - top;
    for (int k = 1; k < STRIPES; k++)
        lattice_band_v(L.full.x + k * sw + (k - 1) * UK_BAND, top, UK_BAND, sh);
    // The card's three gaps -- over the face, under it, at the foot -- share
    // what the lines leave.
    int gap = (sh - tile - PAGE_FOE_CARD_LINES * lh) / 3;
    if (gap > UK_INSET) gap = UK_INSET;
    if (gap < 2) gap = 2;
    for (int k = 0; k < shown; k++) {
        const TroopDef *t = troops[k];
        int sx = L.full.x + k * (sw + UK_BAND);
        int cx = sx + sw / 2, tw = sw - 2 * UK_INSET;
        uk_figure(troop_standing(s, t->index), cx - tile / 2, L.scene.y + L.scene.h, 1, L.scene.y, true);
        uk_picture(troop_face(s, t->index), cx - tile / 2, top + gap, tile, tile);
        int ty = top + gap + tile + gap;
        char buf[64];
        const char *label = GameNumberName(g, counts[k]);
        if (label[0]) snprintf(buf, sizeof buf, "%s", label);
        else          snprintf(buf, sizeof buf, "%d", counts[k]);
        ty = uk_words_centred(buf, cx, ty, tw, 1, PAL_CLR(WHITE));
        int name_y = ty;
        uk_words_centred(t->name, cx, ty, tw, 2, PAL_CLR(YELLOW));
        ty = name_y + 2 * lh;
        snprintf(buf, sizeof buf, "%d", t->hit_points);
        ty = foe_stat(ui->fv_hp, buf, cx, ty, tw);
        snprintf(buf, sizeof buf, "%d-%d", t->melee_min, t->melee_max);
        foe_stat(ui->fv_dmg, buf, cx, ty, tw);
    }
    ml_list_draw(L.full.x, L.rows_y, L.full.w, ml_list_height(2), 2, pv ? pv->yn_cursor : 0,
                 foe_row, (void *)g, TOUCH_LIST_PROMPT, uk_ink());
}

// =============================================================================
//  Sailing -- the ship at sea, the provinces to sail for, then the confirmation
// =============================================================================

typedef struct { const PromptView *pv; const Resources *res; } SailRows;

static bool sail_row(void *ctx, int i, char *label, char *right, int cap) {
    const SailRows *sr = (const SailRows *)ctx;
    const PromptView *pv = sr->pv;
    right[0] = '\0';
    if (!pv) return false;
    if (pv->kind == PK_NUMERIC) {                 // the province picker
        if (i < pv->choice_n) {
            snprintf(label, (size_t)cap, "%s", pv->choices[i]);
            if (ml_keys_shown()) snprintf(right, 48, "%d", i + 1);
        } else {
            snprintf(label, (size_t)cap, "%s", sr->res->banners.count_cancel);
            ml_exit_hint(right);
        }
        return true;
    }
    return yes_no_rows((void *)sr->res, i, label, right, cap);
}

void modern_overlay_draw_sail(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const PromptView *pv = prompt_view();
    bool picking = pv && pv->kind == PK_NUMERIC;
    int rows = picking ? pv->choice_n + 1 : 2;   // the provinces and Cancel, or Yes and No
    const char *title = pv && pv->header && pv->header[0] ? pv->header : g->res->ui.dt_navigate;
    const char *words = picking ? pv->lead : (pv ? pv->body : NULL);
    PagePlace P = page_place(title, NULL, s ? s->sail_backdrop : (Texture2D){ 0 }, rows);
    if (words) uk_flow(P.words.x, P.words.y, P.words.w, P.words.x, 0, P.words.y + P.words.h,
                       words, PAL_CLR(WHITE));
    SailRows sr = { pv, g->res };
    int cursor = picking ? pv->choice_cursor : (pv ? pv->yn_cursor : 0);
    // The provinces from the column's top, Cancel on its foot; Yes and No
    // together on the foot.
    ml_rows_draw(P.rows, rows, picking ? 1 : 2, cursor, sail_row, &sr, TOUCH_LIST_PROMPT);
}

// ---------------------------------------------------------------------------
// Toast -- one line at the top of the area behind it.
// ---------------------------------------------------------------------------

void modern_overlay_draw_toast(void) {
    page_toast(toast_text_current());
}

// ---------------------------------------------------------------------------
// Controls: a menu page of the pack's settings, and its exit.
// ---------------------------------------------------------------------------

typedef struct { const Game *g; int vis_idx[8]; int vis; bool from_menu; } ControlsCtx;

static bool controls_row(void *ctx, int k, char *label, char *right, int cap) {
    const ControlsCtx *c = (const ControlsCtx *)ctx;
    const Game *g = c->g;
    const ResUI *ui = &g->res->ui;
    // No Scale row: the scale is chosen from the surface, not by the player
    // (present.c). The exit sits directly after the pack's own settings:
    // Back to the menu that opened it, else Close.
    if (k == c->vis) {
        snprintf(label, (size_t)cap, "%s", c->from_menu ? ui->gm_back : ui->gm_close);
        ml_exit_hint(right);
        return true;
    }
    int i = c->vis_idx[k];
    // The value at the right; with the keyboard, the digit that steps it.
    int val = g->stats.options[i];
    const char *v = strcmp(g->res->controls.items[i].type, "bool") == 0
                  ? (val == 1 ? ui->controls_on : ui->controls_off) : NULL;
    char vb[16];
    if (!v) { snprintf(vb, sizeof vb, "%d", val); v = vb; }
    if (ml_keys_shown()) snprintf(label, (size_t)cap, "%d  %s", k + 1, g->res->controls.items[i].label);
    else                 snprintf(label, (size_t)cap, "%s", g->res->controls.items[i].label);
    snprintf(right, 48, "%s", v);
    return !views_controls_row_disabled(g, i);
}

void modern_overlay_draw_controls(const Game *g) {
    if (!g || !g->res) return;
    int count = g->res->controls.count;
    ControlsCtx c = { .g = g, .vis = 0, .from_menu = views_depth() > 1 };
    for (int i = 0; i < count && c.vis < 8; i++) {
        if (g->res->controls.items[i].hidden) continue;
        c.vis_idx[c.vis++] = i;
    }
    if (c.vis == 0) return;
    int cur_k = views_controls_cursor();
    if (cur_k < 0) cur_k = 0;
    if (cur_k > c.vis) cur_k = c.vis;
    // A menu page like every other: the settings from the top, the exit on
    // the foot. A tap on a setting steps it; the exit closes. Opened from the
    // game menu, its title carries on the menu's path.
    char title[160];
    modern_gamemenu_path(g, title, (int)sizeof title);
    if (c.from_menu && title[0]) {
        size_t n = strlen(title);
        snprintf(title + n, sizeof title - n, " > %s", g->res->ui.gm_controls);
    } else {
        snprintf(title, sizeof title, "%s", g->res->ui.controls_title);
    }
    ML_Rect b = page_menu_body(title, g->character.name, 0);
    ml_rows_draw(b, c.vis + 1, 1, cur_k, controls_row, &c, TOUCH_LIST_CONTROLS);
}

// =============================================================================
//  Temple (the Augur's alcove) and dwelling -- every step the place page
// =============================================================================

void modern_overlay_draw_temple(const Game *g, const Sprites *s) {
    if (!g || !g->res) return;
    const Resources *res = g->res;
    const ResBanners *bn = &res->banners;
    char gold[48], buf[RES_BANNER_LEN], cost[16];
    uk_gold_text(g, gold, sizeof gold);
    const PromptView *pv = prompt_view();
    bool asking = prompt_is_active() && prompt_req_kind() == PIO_ASK_IN_PLACE;
    bool offering = !asking && loc_deal_pending() && !loc_deal_revealed();
    bool result = !asking && !offering;
    Texture2D bd = loc_texture(s, LOC_ALCOVE);

    UkDoc d = { 0 };
    if (result) {
        loc_deal_text(g, buf, sizeof buf);
    } else {
        const ResZone *z = resources_zone_by_id(res, g->position.zone);
        snprintf(cost, sizeof cost, "%d", GameAlcoveCost(g, g->position.zone));
        ResTemplateVar iv[] = { { "ZONE", (z && z->name[0]) ? z->name : g->position.zone }, { "COST", cost } };
        resources_format_template(buf, sizeof buf, bn->temple_intro, iv, 2);
    }
    uk_doc_add(&d, buf, PAL_CLR(WHITE));
    if (result) {
        // The Augur's answer: his face beside his words.
        place_outcome(bn->temple_title, gold, bd, s ? s->alcove_portrait : (Texture2D){ 0 }, &d,
                      bn->castle_continue);
        return;
    }
    PagePlace P = page_place(bn->temple_title, gold, bd, 2);
    if (s && s->alcove_figure.id && res->sprites.alcove_figure_w > 0) {
        // The Augur stands like every figure, at 2x on the room's floor,
        // centred where the pack places him.
        Texture2D fig = sprites_strip(s->alcove_figure_anim, s->alcove_figure_frames,
                                      (int)(ui_anim_time() * UK_IDLE_FPS));
        if (!fig.id) fig = s->alcove_figure;
        int cx = (res->sprites.alcove_figure_x + res->sprites.alcove_figure_w / 2) * P.band.scale;
        uk_scene_figure(&P.band, fig, cx - CL_TILE_W);
    }
    uk_doc_draw(&d, P.words, 0, 0, -1, true);
    UkRows rows = { { bn->temple_learn, bn->location_leave }, { true, true }, 2 };
    ml_rows_draw(P.rows, 2, 1, asking ? pv->yn_cursor : *loc_deal_cursor(), uk_rows_fn, &rows,
                 TOUCH_LIST_PROMPT);
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
    Texture2D bd = loc_texture(s, lk);

    char cb[16], pb[16];
    snprintf(pb, sizeof pb, "%d", pop);
    snprintf(cb, sizeof cb, "%d", cost);
    const PromptView *pv = prompt_view();
    bool offer = prompt_is_active() && prompt_req_kind() == PIO_ASK_NUMBER_IN_PLACE;
    bool counting = offer && pv->step_open;
    bool dealing = !offer && loc_deal_pending() && !loc_deal_revealed();
    bool result = !offer && !dealing;

    if (result) {
        // The outcome: the troop's own face beside the words.
        UkDoc d = { 0 };
        loc_deal_text(g, buf, sizeof buf);
        uk_doc_add(&d, buf, PAL_CLR(WHITE));
        place_outcome(title, gold, bd, troop_face(s, ti), &d, bn->castle_continue);
        return;
    }
    // The offer and How many are the same page: the dwelling with its troop
    // standing in it, the rows at the left and, beside them, the offer's words
    // or the count -- built exactly as the castle's.
    PagePlace P = page_place(title, gold, bd, 2);
    uk_scene_figure(&P.band, troop_standing(s, ti), CL_TILE_W);
    if (counting) {
        char heading[RES_BANNER_LEN], lead[RES_BANNER_LEN], total[RES_BANNER_LEN], vb[16];
        static char act_label[RES_BANNER_LEN];
        ResTemplateVar hv[] = { { "TROOP", tr ? tr->name : "" } };
        resources_format_template(heading, sizeof heading, bn->count_heading, hv, 1);
        snprintf(vb, sizeof vb, "%d", pv->step_max);
        ResTemplateVar sv[] = { { "MAX", vb } };
        resources_format_template(lead, sizeof lead, bn->count_of_lead, sv, 1);
        snprintf(vb, sizeof vb, "%d", cost * pv->step_value);
        ResTemplateVar c1[] = { { "GOLD", vb } };
        resources_format_template(total, sizeof total, bn->count_cost, c1, 1);
        snprintf(vb, sizeof vb, "%d", pv->step_value);
        ResTemplateVar av[] = { { "COUNT", vb } };
        resources_format_template(act_label, sizeof act_label, bn->count_recruit, av, 1);
        const char *labels[2] = { act_label, bn->count_cancel };
        ml_rows_draw(P.rows, 2, 2, 0, count_rows, (void *)labels, TOUCH_LIST_PROMPT);
        uk_count(P.words, heading, lead, total, pv->step_value, pv->step_max);
        return;
    }
    UkDoc d = { 0 };
    ResTemplateVar iv[] = { { "COUNT", pb }, { "TROOP", tr ? tr->name : "" }, { "COST", cb } };
    char intro[2 * RES_BANNER_LEN + 2];
    resources_format_template(buf, sizeof buf, bn->dwelling_intro, iv, 3);
    snprintf(intro, sizeof intro, "%s%s%s", buf, cap <= 0 ? " " : "",
             cap > 0 ? "" : (tr && g->stats.gold < tr->recruit_cost) ? bn->town_no_gold : bn->army_cannot_handle);
    uk_doc_add(&d, intro, PAL_CLR(WHITE));
    uk_doc_draw(&d, P.words, 0, 0, -1, true);
    UkRows rows = { { bn->dwelling_recruit_row, bn->location_leave }, { cap > 0 || dealing, true }, 2 };
    ml_rows_draw(P.rows, 2, 1, offer ? pv->yn_cursor : *loc_deal_cursor(), uk_rows_fn, &rows,
                 TOUCH_LIST_PROMPT);
}
