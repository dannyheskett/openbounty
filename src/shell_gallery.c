// src/shell_gallery.c -- --gallery <dir>: capture every modern screen to PNG.
//
// A layout audit tool, not play: after a new game is set up it puts the shell
// into one screen state after another (views, prompts, dialogs, the town and
// castle pages, the foe, temple and dwelling screens, the menus, combat), draws
// each with the game's own renderers into the render target, and writes the
// target to <dir>/<name>.png. No input is read, so nothing can be dropped.

#include "shell_gallery.h"
#include "views_render.h"
#include "views_render_impl.h"
#include "spells_adventure.h"
#include "shell_frame.h"
#include "raylib.h"
#include "layout.h"
#include "views.h"
#include "prompt.h"
#include "pending.h"
#include "player_io.h"
#include "ui.h"
#include "tables.h"
#include "resources.h"
#include "combat.h"
#include "combat_loop.h"
#include "combat_render.h"
#include "screens/dwelling.h"
#include "modern/castle.h"
#include "modern/gamemenu.h"
#include "modern/location.h"
#include "screens/end_game.h"
#include "tile_cache.h"
#include "shell_weekend.h"
#include "touch.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void combat_present_public(const Combat *c, const Game *g, const Sprites *sprites, void *render_target);
void combat_gallery_menu(bool open);
void combat_gallery_cast_page(void);
void end_cartoon_gallery_draw(RenderTexture2D *rt, const Resources *res, const Sprites *sprites,
                              const struct Game *game, int frame);

static void cpy(char *d, size_t n, const char *src) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; i++) d[i] = src[i];
    d[i] = '\0';
}

typedef struct {
    Game *g; Map *m; Fog *f; const Resources *res; const Sprites *s;
    RenderTexture2D *rt; const char *dir;
    FILE *manifest;
} Gal;

static void save_target(Gal *G, const char *name) {
    Image img = LoadImageFromTexture(G->rt->texture);
    ImageFlipVertical(&img);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", G->dir, name);
    ExportImage(img, path);
    UnloadImage(img);
    if (G->manifest) fprintf(G->manifest, "%s\n", name);
    fprintf(stdout, "[gallery] %s\n", path);
}

// Draw a few frames (animations settle, touch regions register) and capture.
static void shot(Gal *G, const char *name) {
    for (int i = 0; i < 3; i++) shell_present_frame(G->g, G->m, G->f, G->s, G->rt);
    save_target(G, name);
}

static void reset(Gal *G) {
    toast_show("");
    dialog_dismiss();
    prompt_dismiss();
    pending_flow = FLOW_NONE;
    pending_foe_id[0] = '\0';
    pending_foe_evade_blocked = false;
    loc_deal_clear();
    views_set(VIEW_NONE);
    G->g->player_io.count = 0;
    G->g->player_io.head = 0;
}

// ---- the tap check -------------------------------------------------------------------
//
// Each step that stands in place of another must offer a finger the rows its
// input reads -- the same list and row numbers -- where it drew them, with
// nothing of the step before it still registered. Checked on the regions the
// last captured frame registered (touch_last_*). A failure is printed and makes
// the gallery exit non-zero.

static int s_tap_checks, s_tap_fails;

static void tap_fail(const char *shot_name, const char *what) {
    s_tap_fails++;
    fprintf(stderr, "[tapcheck] FAIL %s: %s\n", shot_name, what);
}

// Row `row` of `list` is registered, and a tap at its centre reaches it.
static void tap_row(const char *shot_name, int list, int row) {
    char what[128];
    int x, y, w, h, hl, hr, hk;
    s_tap_checks++;
    if (!touch_last_row_rect(list, row, &x, &y, &w, &h)) {
        snprintf(what, sizeof what, "list %d row %d is not tappable", list, row);
        tap_fail(shot_name, what);
        return;
    }
    if (!touch_last_hit(x + w / 2, y + h / 2, &hl, &hr, &hk) || hl != list || hr != row) {
        snprintf(what, sizeof what, "a tap on list %d row %d reaches list %d row %d key %d",
                 list, row, hl, hr, hk);
        tap_fail(shot_name, what);
    }
}

// The button for `key` is registered, and a tap at its centre presses it.
static void tap_key(const char *shot_name, int key) {
    char what[128];
    int x, y, w, h, hl, hr, hk;
    s_tap_checks++;
    if (!touch_last_key_rect(key, &x, &y, &w, &h)) {
        snprintf(what, sizeof what, "no button for key %d", key);
        tap_fail(shot_name, what);
        return;
    }
    if (!touch_last_hit(x + w / 2, y + h / 2, &hl, &hr, &hk) || hk != key) {
        snprintf(what, sizeof what, "a tap on the key %d button reaches list %d row %d key %d", key, hl, hr, hk);
        tap_fail(shot_name, what);
    }
}

// Row `row` of `list` is not registered: that step's rows are gone.
static void tap_gone(const char *shot_name, int list, int row) {
    char what[96];
    s_tap_checks++;
    if (touch_last_row_rect(list, row, NULL, NULL, NULL, NULL)) {
        snprintf(what, sizeof what, "list %d row %d is still tappable", list, row);
        tap_fail(shot_name, what);
    }
}

// A Yes/No step in place of `parent`'s rows.
static void tap_yes_no(const char *shot_name, int parent) {
    tap_row(shot_name, TOUCH_LIST_PROMPT, 0);
    tap_row(shot_name, TOUCH_LIST_PROMPT, 1);
    if (parent) tap_gone(shot_name, parent, 0);
}

// A result with one Continue in place of `parent`'s rows.
static void tap_continue(const char *shot_name, int parent) {
    tap_row(shot_name, TOUCH_LIST_PROMPT, 0);
    tap_gone(shot_name, TOUCH_LIST_PROMPT, 1);
    if (parent) tap_gone(shot_name, parent, 0);
}

static const ResTown *first_town_in(const Resources *r, const char *zone) {
    for (int i = 0; i < r->town_count; i++)
        if (strcmp(r->towns[i].zone, zone) == 0) return &r->towns[i];
    return r->town_count ? &r->towns[0] : NULL;
}

static void set_army(Game *g, const char *ids[], int counts[], int n) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) { g->army[i].id[0] = '\0'; g->army[i].count = 0; }
    for (int i = 0; i < n && i < GAME_ARMY_SLOTS; i++) {
        cpy(g->army[i].id, sizeof g->army[i].id, ids[i]);
        g->army[i].count = counts[i];
    }
}

int gallery_run(Game *g, Map *m, Fog *f, const Resources *res, const Sprites *s,
                RenderTexture2D *rt, const char *dir) {
    mkdir(dir, 0755);
    char mpath[1024];
    snprintf(mpath, sizeof mpath, "%s/manifest.txt", dir);
    Gal G = { g, m, f, res, s, rt, dir, fopen(mpath, "w") };
    const ResUI *ui = &res->ui;
    const ResBanners *bn = &res->banners;

    // A hero with something to show: gold, an army of four troops, magic.
    g->stats.gold = 12000;
    g->stats.knows_magic = true;
    for (int i = 0; i < 14; i++) g->spells.counts[i] = (i % 3) + 1;
    {
        const TroopDef *t0 = troop_by_index(0), *t1 = troop_by_index(1),
                       *t2 = troop_by_index(2), *t4 = troop_by_index(4);
        const char *ids[4] = { t0->id, t1->id, t2->id, t4->id };
        int counts[4] = { 40, 25, 12, 6 };
        set_army(g, ids, counts, 4);
    }

    // ---- the map and its panels ----------------------------------------------
    reset(&G); shot(&G, "01_map");
    reset(&G);
    {
        // A save: the message src/main.c sends after the slot is written.
        char sbody[RES_BANNER_LEN], dd[12];
        snprintf(dd, sizeof dd, "%d", g->stats.days_left);
        ResTemplateVar sv[] = { { "SLOT", "2" }, { "NAME", g->character.name },
                                { "RANK", g->character.cls.rank_title }, { "DAYS", dd } };
        resources_format_template(sbody, sizeof sbody, bn->save_done, sv, 4);
        player_io_message(g, bn->save_done_title, sbody);
        shell_pump_player_io_message(g);
    }
    shot(&G, "02_map_toast");
    char tb[RES_BANNER_LEN];
    ResTemplateVar vars[] = { { "DAYS", "10" }, { "ZONE", "Italia" }, { "X", "12" }, { "Y", "40" },
                              { "COST", "2500" }, { "HERO", g->character.name } };
    // Real messages, as the engine sends them: no room in the army
    // (flow_resolve.c), and the Time Stop spell (spells_adventure.c).
    reset(&G); open_dialog(NULL, bn->no_troop_slots); shot(&G, "03_message_small");
    reset(&G);
    {
        ResTemplateVar sv[] = { { "STEPS", "10" } };
        resources_format_template(tb, sizeof tb, bn->spell_time_stop, sv, 1);
        open_dialog(spell_header("time_stop", "Time Stop"), tb);
    }
    shot(&G, "04_message_titled");
    // The artifact message: the engine's longest title, which wraps to two
    // lines. It used to print the second line under the body's first.
    reset(&G);
    {
        char hb[192], bb[512];
        ResTemplateVar av[] = { { "ARTIFACT", "The Sibylline Fragment" } };
        resources_format_template(hb, sizeof hb, bn->artifact_found, av, 1);
        snprintf(bb, sizeof bb, "Unknown power.\n\n%s", bn->artifact_map_piece);
        open_dialog(hb, bb);
    }
    shot(&G, "04b_message_artifact");
    reset(&G); resources_format_template(tb, sizeof tb, bn->no_spell_banner, vars, 6);
    open_dialog(NULL, tb); shot(&G, "05_message_long");
    reset(&G); resources_format_template(tb, sizeof tb, bn->body_search, vars, 6);
    prompt_yes_no_open(ui->dt_search, tb);
    shot(&G, "06_question_yes_no");
    reset(&G); prompt_yes_no_open(NULL, ui->quit_to_dos_prompt); shot(&G, "06b_quit_without_saving");
    reset(&G);
    {
        const char *labels[3] = { "Tirones (40)", "Velites (25)", "Hastati (12)" };
        int values[3] = { 1, 2, 3 };
        prompt_numeric_open(ui->dt_dismiss_army, bn->body_dismiss_pick, 5);
        prompt_set_choices(labels, values, 3);
    }
    shot(&G, "07_question_choices");
    reset(&G);
    {
        PlayerRequest *r = player_io_message(g, "Captured: Brennus",
            "...and the capture of Brennus.\n\nFor fulfilling your contract you receive a bounty of 7000 gold, and a piece of the map to the stolen Aquila.");
        if (r) { r->face = REQ_FACE_VILLAIN; r->face_index = 2; }
        shell_pump_player_io_message(g);
    }
    shot(&G, "08_inlay_capture");

    // The week's end: the astrology message, then the budget report.
    reset(&G);
    pending_week_phase = WK_PHASE_ASTROLOGY;
    pending_week_id = 3;
    pending_astrology_troop_idx = 6;
    pump_week_end_dialog(g); shell_pump_player_io_message(g);
    shot(&G, "09a_week_end_astrology");
    reset(&G);
    pending_week_phase = WK_PHASE_BUDGET;
    pump_week_end_dialog(g); shell_pump_player_io_message(g);
    shot(&G, "09b_week_end_budget");
    {
        // The same report late in a campaign: six-figure gold and a big army.
        Game keep = *g;
        g->stats.gold = 279635;
        pending_week_paid = 1000;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (g->army[i].id[0] && g->army[i].count > 0) g->army[i].count *= 40;
        reset(&G);
        pending_week_phase = WK_PHASE_BUDGET;
        pump_week_end_dialog(g); shell_pump_player_io_message(g);
        shot(&G, "09b2_week_end_budget_large");
        *g = keep;
    }
    pending_week_phase = WK_PHASE_NONE;

    // A treasure chest: gold or leadership.
    reset(&G);
    {
        ResTemplateVar cv[] = { { "GOLD", "1200" }, { "LEADERSHIP", "30" } };
        resources_format_template(tb, sizeof tb, bn->chest_gold, cv, 2);
        pending_flow = FLOW_CHEST_CHOICE;
        prompt_ab_open("", tb);
    }
    shot(&G, "09c_chest_choice");

    // Troops wish to join.
    reset(&G);
    {
        const TroopDef *t = troop_by_index(3);
        ResTemplateVar jv[] = { { "COUNT", "20" }, { "TROOP", t->name } };
        resources_format_template(tb, sizeof tb, bn->encounter_join_numeric, jv, 2);
        pending_flow = FLOW_ACCEPT_FRIENDLY;
        cpy(pending_dwelling_troop, sizeof pending_dwelling_troop, t->id);
        prompt_yes_no_open("", tb);
    }
    shot(&G, "09d_troops_join");

    // Navigate to another continent.
    reset(&G);
    {
        char body[256] = "";
        size_t off = 0;
        for (int zi = 1; zi < res->zone_count && zi < 4; zi++) {
            char ib[8], frag[96];
            snprintf(ib, sizeof ib, "%d", zi);
            ResTemplateVar nv[] = { { "INDEX", ib }, { "ZONE", res->zones[zi].name } };
            resources_format_template(frag, sizeof frag, bn->body_navigate_row, nv, 2);
            off += (size_t)snprintf(body + off, sizeof body - off, "%s", frag);
        }
        pending_flow = FLOW_NAVIGATE;
        prompt_numeric_open(ui->dt_navigate, body, 3);
    }
    shot(&G, "09e_navigate");

    // Temporary death: sent back to the Emperor in disgrace.
    reset(&G);
    {
        // The hero's own disgraced scene, as shell_tempdeath.c sends it; then
        // each class's scene for review.
        const ClassDef *hc = class_by_id(g->character.cls.id);
        PlayerRequest *r = player_io_message(g, NULL, bn->temp_death);
        if (r && hc) { r->face = REQ_FACE_SCENE; r->face_index = hc->index; }
        shell_pump_player_io_message(g);
    }
    shot(&G, "09f_temporary_death");
    for (int ci = 0; ci < res->classes_count && ci < 4; ci++) {
        reset(&G);
        PlayerRequest *r = player_io_message(g, NULL, bn->temp_death);
        if (r) { r->face = REQ_FACE_SCENE; r->face_index = ci; }
        shell_pump_player_io_message(g);
        char nm[64];
        snprintf(nm, sizeof nm, "09f_temporary_death_%s", res->classes[ci].id);
        shot(&G, nm);
    }

    // The bridge spell asks for a direction.
    reset(&G); bridge_state = BRIDGE_STATE_DIRECTION; open_dialog(NULL, bn->spell_bridge_prompt);
    shot(&G, "09g_bridge_direction"); bridge_state = BRIDGE_STATE_NONE;

    // ---- the game menu --------------------------------------------------------
    {
        GmPageId p1[1] = { GM_PAGE_ROOT };
        GmPageId p2[2] = { GM_PAGE_ROOT, GM_PAGE_HERO };
        GmPageId p3[2] = { GM_PAGE_ROOT, GM_PAGE_WORLD };
        GmPageId p4[2] = { GM_PAGE_ROOT, GM_PAGE_GAME };
        GmPageId p5[3] = { GM_PAGE_ROOT, GM_PAGE_GAME, GM_PAGE_SAVE };
        reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(1, p1, 0); shot(&G, "10_menu_top");
        reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(2, p2, 4); shot(&G, "11_menu_hero");
        reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(2, p3, 6); shot(&G, "12_menu_world_greyed");
        reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(2, p4, 0); shot(&G, "13_menu_game");
        reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(3, p5, 0); shot(&G, "14_menu_save_slots");
        reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(2, p4, 0);
        prompt_yes_no_open(NULL, bn->gmc_exit); shot(&G, "15_menu_exit_question"); tap_yes_no("15_menu_exit_question", TOUCH_LIST_MENU);
        // The menu's other confirmations, each over the page that asks it.
        {
            ResTemplateVar sv[] = { { "SLOT", "2" } };
            char ask[RES_BANNER_LEN];
            reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(3, p5, 1);
            resources_format_template(ask, sizeof ask, bn->gmc_overwrite, sv, 1);
            prompt_yes_no_open(NULL, ask); shot(&G, "15b_menu_overwrite_question"); tap_yes_no("15b_menu_overwrite_question", TOUCH_LIST_MENU);
            GmPageId p6[3] = { GM_PAGE_ROOT, GM_PAGE_GAME, GM_PAGE_LOAD };
            reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(3, p6, 1);
            resources_format_template(ask, sizeof ask, bn->gmc_load, sv, 1);
            prompt_yes_no_open(NULL, ask); shot(&G, "15c_menu_load_question"); tap_yes_no("15c_menu_load_question", TOUCH_LIST_MENU);
            reset(&G); views_set(VIEW_MENU); modern_gamemenu_gallery(2, p4, 3);
            prompt_yes_no_open(NULL, ui->new_game_confirm); shot(&G, "15d_menu_new_game_question"); tap_yes_no("15d_menu_new_game_question", TOUCH_LIST_MENU);
        }
        reset(&G); views_set(VIEW_MENU); views_push(VIEW_CONTROLS); shot(&G, "16_controls");
    }

    // ---- detail views ---------------------------------------------------------
    reset(&G); views_set(VIEW_CHARACTER); shot(&G, "20_character");
    reset(&G); views_set(VIEW_ARMY); shot(&G, "21_army");
    reset(&G); views_set(VIEW_CONTRACT); shot(&G, "22_contract_none");
    cpy(g->contract.active_id, sizeof g->contract.active_id, g->contract.cycle[0]);
    views_contract_set_active(true);
    reset(&G); views_set(VIEW_CONTRACT); shot(&G, "22_contract_held");
    reset(&G); views_set(VIEW_PUZZLE); shot(&G, "23_puzzle");
    {
        // Places visited on this continent, the orb's whole map, a boat.
        int zi = 0;
        for (int i = 0; i < res->zone_count; i++) if (strcmp(res->zones[i].id, g->position.zone) == 0) zi = i;
        bool keep_orb = g->world.orbs_found[zi];
        g->world.orbs_found[zi] = true;
        int marked = 0;
        for (int i = 0; i < res->town_count && marked < 3; i++) {
            if (strcmp(res->towns[i].zone, g->position.zone) != 0) continue;
            for (int k = 0; k < GAME_TOWNS; k++)
                if (strcmp(g->towns[k].id, res->towns[i].id) == 0) { g->towns[k].visited = true; marked++; }
        }
        for (int i = 0; i < res->castle_count; i++) {
            if (strcmp(res->castles[i].zone, g->position.zone) != 0) continue;
            CastleRecord *cr = GameFindCastle(g, res->castles[i].id);
            if (cr) { cr->visited = true; break; }
        }
        bool keep_boat = g->boat.has_boat;
        g->boat.has_boat = true;
        g->boat.x = g->position.x + 3; g->boat.y = g->position.y + 2;
        cpy(g->boat.zone, sizeof g->boat.zone, g->position.zone);
        if (!views_render_worldmap_whole()) views_render_worldmap_toggle_hero_only();
        reset(&G); views_set(VIEW_WORLDMAP); modern_worldmap_gallery(0); shot(&G, "24_worldmap");
        reset(&G); views_set(VIEW_WORLDMAP); modern_worldmap_gallery(1); shot(&G, "24b_worldmap_place");
        modern_worldmap_gallery(0);
        views_render_worldmap_toggle_hero_only();
        g->world.orbs_found[zi] = keep_orb;
        g->boat.has_boat = keep_boat;
    }
    reset(&G); views_set(VIEW_SPELLS); views_spells_set_mode(true); shot(&G, "25_spells");
    reset(&G);
    {
        GateDestination d[6];
        int n = 0;
        for (int i = 0; i < res->town_count && n < 6; i++, n++) {
            cpy(d[n].name, sizeof d[n].name, res->towns[i].name);
            cpy(d[n].zone, sizeof d[n].zone, res->towns[i].zone);
            d[n].x = res->towns[i].x; d[n].y = res->towns[i].y;
        }
        views_gate_open(d, n, true);
    }
    shot(&G, "26_gate_picker");

    // ---- a town ------------------------------------------------------------------
    const ResTown *tw = first_town_in(res, g->position.zone);
    if (tw) {
        cpy(g->position.in_town, sizeof g->position.in_town, tw->id);
        views_open_town(tw->name, tw->id, tw->boat_x, tw->boat_y);
        reset(&G); views_set(VIEW_TOWN); views_gallery_town_scene(0); shot(&G, "30_town_main");
        static const struct { int row; const char *name; } T[] = {
            { -1, "30b_town_services" }, { TOWN_ROW_CONTRACT, "31_town_contracts" },
            { TOWN_ROW_BOAT, "32_town_boat" }, { TOWN_ROW_INFO, "33_town_information" },
            { TOWN_ROW_SPELL, "34_town_temple" }, { TOWN_ROW_SIEGE, "35_town_siege" },
        };
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            reset(&G); views_set(VIEW_TOWN); views_gallery_town(g, T[i].row, 0, NULL, false);
            shot(&G, T[i].name);
            if (strcmp(T[i].name, "30b_town_services") == 0 || strcmp(T[i].name, "35_town_siege") == 0) {
                tap_row(T[i].name, TOUCH_LIST_TOWN, 0);
                tap_row(T[i].name, TOUCH_LIST_TOWN, 1);
                tap_gone(T[i].name, TOUCH_LIST_PROMPT, 0);
            }
        }
        reset(&G); views_set(VIEW_TOWN); views_gallery_town_scene(1);
        shot(&G, "36_town_leave_row");
        reset(&G); views_set(VIEW_TOWN); views_gallery_town(g, TOWN_ROW_SIEGE, 0, NULL, false);
        prompt_yes_no_open(NULL, "Buy siege weapons for 3000 gold?");
        shot(&G, "37_town_question"); tap_yes_no("37_town_question", TOUCH_LIST_TOWN);
        reset(&G); views_set(VIEW_TOWN);
        views_gallery_town(g, TOWN_ROW_SIEGE, 0, "Your engineers load the siege weapons onto carts. You can now lay siege to castles.", true);
        shot(&G, "38_town_result"); tap_continue("38_town_result", TOUCH_LIST_TOWN);
        reset(&G); views_set(VIEW_TOWN);
        views_gallery_town(g, TOWN_ROW_CONTRACT, 0,
                           "New contract: Catiline. Reward: 5000 gold. Last seen on Italia.", true);
        shot(&G, "38b_town_result_contract"); tap_continue("38b_town_result_contract", TOUCH_LIST_TOWN);
        reset(&G); views_set(VIEW_TOWN);
        views_gallery_town(g, TOWN_ROW_BOAT, 0, "The boat is yours for the week. It waits at the quay.", true);
        shot(&G, "38c_town_result_boat"); tap_continue("38c_town_result_boat", TOUCH_LIST_TOWN);
        g->position.in_town[0] = '\0';
    }

    // A town with no dock, whose informant reports on the sacred artifacts
    // instead of a castle's garrison (Rome's Roma). Packs without one skip it.
    const ResTown *inland = NULL;
    for (int i = 0; i < res->town_count; i++)
        if (res->towns[i].intel_artifact) { inland = &res->towns[i]; break; }
    if (inland) {
        cpy(g->position.in_town, sizeof g->position.in_town, inland->id);
        views_open_town(inland->name, inland->id, inland->boat_x, inland->boat_y);
        reset(&G); views_set(VIEW_TOWN); views_gallery_town_scene(0);
        shot(&G, "39_town_inland");
        reset(&G); views_set(VIEW_TOWN); views_gallery_town(g, TOWN_ROW_INFO, 0, NULL, false);
        shot(&G, "39b_town_artifact_intel");
        g->position.in_town[0] = '\0';
    }

    // ---- castles -----------------------------------------------------------------
    const ResCastle *home = NULL, *other = NULL;
    for (int i = 0; i < res->castle_count; i++) {
        if (resources_castle_is_home(&res->castles[i])) { if (!home) home = &res->castles[i]; }
        else if (!other) other = &res->castles[i];
    }
    if (home) {
        cpy(g->position.home_castle, sizeof g->position.home_castle, home->id);
        modern_castle_open(g, true, home->id);
        reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_MENU, 0, 0, 0); shot(&G, "40_home_castle");
        reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_RECRUIT, 1, 0, 0); shot(&G, "41_castle_recruit");
        reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_RECRUIT, 4, 0, 0); shot(&G, "42_castle_recruit_greyed");
        reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_RECRUIT, 1, 18, 30); shot(&G, "43_castle_how_many"); tap_row("43_castle_how_many", TOUCH_LIST_CASTLE, 0); tap_row("43_castle_how_many", TOUCH_LIST_CASTLE, 1); tap_gone("43_castle_how_many", TOUCH_LIST_CASTLE, 2); tap_key("43_castle_how_many", KEY_UP); tap_key("43_castle_how_many", KEY_DOWN);
        reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_AUDIENCE, 0, 0, 0); shot(&G, "44_castle_audience");
        // Tribute asks first: the question over the Audience scene.
        {
            char gold[16], ask[RES_BANNER_LEN];
            snprintf(gold, sizeof gold, "%d", res->economy.tribute_cost);
            ResTemplateVar tv[] = { { "GOLD", gold } };
            resources_format_template(ask, sizeof ask, bn->castle_tribute_confirm, tv, 1);
            reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_AUDIENCE, 2, 0, 0);
            prompt_yes_no_open(NULL, ask); shot(&G, "44a_castle_tribute_question"); tap_yes_no("44a_castle_tribute_question", TOUCH_LIST_CASTLE);
        }
        reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_AUDIENCE, 0, 0, 0);
        modern_castle_gallery_audience(GAME_AUDIENCE_MORE_NEEDED + 1, 0); shot(&G, "44b_castle_audience_answer"); tap_continue("44b_castle_audience_answer", TOUCH_LIST_CASTLE);
        modern_castle_gallery_audience(0, 0);
        {
            GameAudienceGain gain = { 0 };
            gain.leadership = 25; gain.spell_power = 1; gain.max_spells = 1;
            reset(&G); views_set(VIEW_HOME_CASTLE); modern_castle_gallery(MC_AUDIENCE, 2, 0, 0);
            modern_castle_gallery_answer(MC_AUD_TRIBUTE, 1, gain); shot(&G, "44c_castle_tribute_answer"); tap_continue("44c_castle_tribute_answer", TOUCH_LIST_CASTLE);
            GameAudienceGain none = { 0 };
            modern_castle_gallery_answer(MC_AUD_PROMOTION, 0, none);
        }
        {
            int keep = g->character.cls.rank_index;
            g->character.cls.rank_index = 1;
            reset(&G); views_set(VIEW_HOME_CASTLE);
            modern_castle_gallery_audience(GAME_AUDIENCE_PROMOTED + 1, 1);
            modern_castle_gallery(MC_PROMOTION, 0, 0, 0); shot(&G, "45_castle_promotion"); tap_row("45_castle_promotion", TOUCH_LIST_CASTLE, 0);
            g->character.cls.rank_index = keep;
        }
        g->position.home_castle[0] = '\0';
    }
    if (other) {
        CastleRecord *cr = GameFindCastle(g, other->id);
        if (cr) {
            cr->owner_kind = CASTLE_OWNER_PLAYER;
            const TroopDef *t1 = troop_by_index(1), *t2 = troop_by_index(2);
            cpy(cr->garrison[0].id, sizeof cr->garrison[0].id, t1->id); cr->garrison[0].count = 30;
            cpy(cr->garrison[1].id, sizeof cr->garrison[1].id, t2->id); cr->garrison[1].count = 8;
        }
        cpy(g->position.own_castle, sizeof g->position.own_castle, other->id);
        modern_castle_open(g, false, other->id);
        reset(&G); views_set(VIEW_OWN_CASTLE); modern_castle_gallery(MC_MENU, 0, 0, 0); shot(&G, "46_own_castle");
        reset(&G); views_set(VIEW_OWN_CASTLE); modern_castle_gallery(MC_GARRISON, 0, 0, 0); shot(&G, "47_own_castle_garrison");
        reset(&G); views_set(VIEW_OWN_CASTLE); modern_castle_gallery(MC_WITHDRAW, 0, 0, 0); shot(&G, "48_own_castle_withdraw");
        reset(&G); views_set(VIEW_OWN_CASTLE); modern_castle_gallery(MC_WITHDRAW, 0, 30, 30); shot(&G, "49_own_castle_how_many"); tap_row("49_own_castle_how_many", TOUCH_LIST_CASTLE, 0); tap_row("49_own_castle_how_many", TOUCH_LIST_CASTLE, 1); tap_gone("49_own_castle_how_many", TOUCH_LIST_CASTLE, 2); tap_key("49_own_castle_how_many", KEY_UP); tap_key("49_own_castle_how_many", KEY_DOWN);
        g->position.own_castle[0] = '\0';
    }

    // ---- the foe screen -----------------------------------------------------------
    for (int fi = 0; fi < g->foe_count; fi++) {
        FoeState *fo = &g->foes[fi];
        if (!fo->alive || fo->friendly) continue;
        for (int k = 0; k < GAME_ARMY_SLOTS; k++) {
            const TroopDef *t = troop_by_index(5 + k * 4);
            cpy(fo->garrison[k].id, sizeof fo->garrison[k].id, t ? t->id : "");
            fo->garrison[k].count = 7 + k * 11;
        }
        reset(&G);
        cpy(pending_foe_id, sizeof pending_foe_id, fo->placement_id);
        pending_flow = FLOW_ATTACK_FOE;
        prompt_yes_no_open(ui->dt_foes, "You encounter:");
        shot(&G, "50_foe_five_troops");
        for (int k = 2; k < GAME_ARMY_SLOTS; k++) { fo->garrison[k].id[0] = '\0'; fo->garrison[k].count = 0; }
        pending_foe_evade_blocked = true;
        shot(&G, "51_foe_two_troops_blocked");
        break;
    }

    // ---- temple and dwelling ----------------------------------------------------------
    reset(&G); views_set(VIEW_ALCOVE); pending_flow = FLOW_ALCOVE;
    prompt_yes_no_open(ui->dt_alcove_offer, bn->alcove_offer_modern);
    shot(&G, "60_temple"); tap_yes_no("60_temple", 0);
    reset(&G); views_set(VIEW_ALCOVE);
    loc_deal_begin(g); g->stats.gold -= 5000; loc_deal_done(g, 0, NULL); g->stats.gold += 5000;
    resources_format_template(tb, sizeof tb, bn->alcove_taught, vars, 6);
    loc_deal_absorb(tb);
    shot(&G, "61_temple_result"); tap_continue("61_temple_result", 0);
    {
        const TroopDef *t = troop_by_index(6);
        reset(&G);
        screen_dwelling_open(g, DWELLING_KIND_FOREST, t->id, 150, t->recruit_cost, g->stats.gold, 24);
        views_set(VIEW_DWELLING);
        g->player_io.count = 0;
        pending_flow = FLOW_RECRUIT;
        prompt_text_input_open(t->name, "", 4, 24);
        shot(&G, "62_dwelling"); tap_yes_no("62_dwelling", 0);
        prompt_gallery_step_open(true);
        shot(&G, "63_dwelling_how_many"); tap_yes_no("63_dwelling_how_many", 0); tap_key("63_dwelling_how_many", KEY_UP); tap_key("63_dwelling_how_many", KEY_DOWN);
        reset(&G); views_set(VIEW_DWELLING);
        loc_deal_begin(g); g->stats.gold -= 600; loc_deal_done(g, 20, t->id); g->stats.gold += 600;
        shot(&G, "64_dwelling_result"); tap_continue("64_dwelling_result", 0);
    }

    // ---- combat ---------------------------------------------------------------------
    {
        reset(&G);
        Unit garrison[GAME_ARMY_SLOTS] = { 0 };
        for (int k = 0; k < 3; k++) {
            const TroopDef *t = troop_by_index(10 + k);
            cpy(garrison[k].id, sizeof garrison[k].id, t->id);
            garrison[k].count = 10 + k * 5;
        }
        CombatTarget tgt = { 0 };
        tgt.name = "Hostile band";
        tgt.seed_key = "gallery";
        tgt.garrison = garrison;
        tgt.garrison_slots = GAME_ARMY_SLOTS;
        static Combat c;
        memset(&c, 0, sizeof c);
        combat_init(&c, g, COMBAT_MODE_FOE, &tgt);
        combat_seed_rng(&c, g, COMBAT_MODE_FOE, &tgt);
        combat_prepare_player(&c, g);
        combat_prepare_foe(&c, &tgt);
        combat_reset_match(&c);
        combat_render_set_ground(tile_cache_get("grass"));
        combat_reset_turn(&c, COMBAT_SIDE_AI);
        c.unit_id = combat_next_unit(&c);
        for (int i = 0; i < 3; i++) combat_present_public(&c, g, s, rt);
        save_target(&G, "70_combat");
        combat_gallery_menu(true);
        for (int i = 0; i < 3; i++) combat_present_public(&c, g, s, rt);
        save_target(&G, "71_combat_menu");
        combat_gallery_menu(false);
        c.picker_active = true;
        c.cursor_x = c.units[COMBAT_SIDE_AI][0].x;
        c.cursor_y = c.units[COMBAT_SIDE_AI][0].y;
        for (int i = 0; i < 3; i++) combat_present_public(&c, g, s, rt);
        save_target(&G, "71b_combat_target_picker");
        c.picker_active = false;
        views_set(VIEW_ARMY);
        for (int i = 0; i < 3; i++) combat_present_public(&c, g, s, rt);
        save_target(&G, "71c_combat_army_view");
        views_set(VIEW_NONE);
        combat_gallery_cast_page();
        for (int i = 0; i < 3; i++) combat_present_public(&c, g, s, rt);
        save_target(&G, "72_combat_spells");
        combat_gallery_menu(false);
        prompt_yes_no_open(ui->give_up_header_modern, bn->combat_give_up_body);
        for (int i = 0; i < 3; i++) combat_present_public(&c, g, s, rt);
        save_target(&G, "73_combat_give_up");
        prompt_dismiss();
        open_dialog(ui->dt_combat_victory, "You have defeated the hostile band.\n\nSpoils: 1,250 gold.");
        for (int i = 0; i < 3; i++) combat_present_public(&c, g, s, rt);
        save_target(&G, "74_combat_victory");
        dialog_dismiss();
    }

    // ---- the end ----------------------------------------------------------------------
    reset(&G);
    end_cartoon_gallery_draw(rt, res, s, g, 4);  save_target(&G, "79a_victory_cartoon_start");
    end_cartoon_gallery_draw(rt, res, s, g, 10); save_target(&G, "79b_victory_cartoon_end");
    {
        // The words as the game says them (engine/flows.c format_end_text).
        char score[16], end_body[RES_END_BODY_LEN];
        snprintf(score, sizeof score, "%d", GameComputeScore(g));
        ResTemplateVar ev[] = { { "NAME", g->character.name }, { "RANK", g->character.cls.rank_title },
                                { "SCORE", score } };
        resources_format_template(end_body, sizeof end_body, res->win_text.body, ev, 3);
        reset(&G); screen_end_game_open(true, end_body); views_set(VIEW_WIN); shot(&G, "80_win");
        resources_format_template(end_body, sizeof end_body, res->lose_text.body, ev, 3);
        reset(&G); screen_end_game_open(false, end_body); views_set(VIEW_LOSE); shot(&G, "81_lose");
    }

    reset(&G);
    if (G.manifest) fclose(G.manifest);
    fprintf(stdout, "[tapcheck] %d checks, %d failed\n", s_tap_checks, s_tap_fails);
    return s_tap_fails ? 1 : 0;
}
