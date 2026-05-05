#include "flows.h"

#include <stdio.h>
#include <string.h>

#include "tables.h"
#include "ui.h"
#include "pending.h"
#include "prompt.h"
#include "end_cartoon.h"
#include "screens/end_game.h"

// Substitute %NAME% / %RANK% / %SCORE% in an end-of-game text template.
static void format_end_text(char *out, int out_sz, const char *src,
                            const Game *g) {
    if (!out || out_sz <= 0) return;
    out[0] = '\0';
    if (!src) return;
    int o = 0;
    while (*src && o + 1 < out_sz) {
        if (strncmp(src, "%NAME%", 6) == 0) {
            int n = snprintf(out + o, out_sz - o, "%s",
                             g->character.name);
            if (n < 0) break;
            o += n;
            src += 6;
        } else if (strncmp(src, "%RANK%", 6) == 0) {
            int n = snprintf(out + o, out_sz - o, "%s",
                             g->character.cls.rank_title);
            if (n < 0) break;
            o += n;
            src += 6;
        } else if (strncmp(src, "%SCORE%", 7) == 0) {
            int n = snprintf(out + o, out_sz - o, "%d",
                             GameComputeScore(g));
            if (n < 0) break;
            o += n;
            src += 7;
        } else {
            out[o++] = *src++;
        }
    }
    out[o] = '\0';
}

void show_lose_game(const Game *g, const Resources *res) {
    //  / : fullscreen ending
    // image (right half) + DBLUE-tinted left half with rendered text
    // = "Press 'ESC' to exit". We push VIEW_LOSE so chrome swaps the
    // status bar and the ending art renders behind the text.
    char header[128];
    char tmp_body[RES_END_BODY_LEN];
    char tmp_footer[RES_NAME_LEN * 2];
    format_end_text(header,     sizeof(header),     res->lose_text.header, g);
    format_end_text(tmp_body,   sizeof(tmp_body),   res->lose_text.body,   g);
    format_end_text(tmp_footer, sizeof(tmp_footer), res->lose_text.footer, g);
    char composed[RES_END_BODY_LEN + RES_NAME_LEN * 2 + 32];
    if (header[0] && tmp_footer[0]) {
        snprintf(composed, sizeof(composed), "%s\n\n%s\n\n%s",
                 header, tmp_body, tmp_footer);
    } else if (header[0]) {
        snprintf(composed, sizeof(composed), "%s\n\n%s", header, tmp_body);
    } else if (tmp_footer[0]) {
        snprintf(composed, sizeof(composed), "%s\n\n%s", tmp_body, tmp_footer);
    } else {
        snprintf(composed, sizeof(composed), "%s", tmp_body);
    }
    screen_end_game_open(false, composed);
}

void show_win_game(Game *g, const Resources *res,
                   RenderTexture2D *rt, const Sprites *sprites) {
    //  / :
    //   1. display_cartoon (hero walking the bridge cutscene).
    //   2. Push VIEW_WIN — fullscreen ending image (sub_id 0) on the
    //      right half + DBLUE on the left + rendered text. Status bar
    //      auto-swaps to "Press 'ESC' to exit" via views_wants_exit_hint.
    if (rt && sprites) {
        run_end_cartoon(rt, res, sprites);
    }

    char header[128];
    char tmp_body[RES_END_BODY_LEN];
    char tmp_footer[RES_NAME_LEN * 2];
    format_end_text(header,     sizeof(header),     res->win_text.header, g);
    format_end_text(tmp_body,   sizeof(tmp_body),   res->win_text.body,   g);
    format_end_text(tmp_footer, sizeof(tmp_footer), res->win_text.footer, g);
    char composed[RES_END_BODY_LEN + RES_NAME_LEN * 2 + 32];
    if (header[0] && tmp_footer[0]) {
        snprintf(composed, sizeof(composed), "%s\n\n%s\n\n%s",
                 header, tmp_body, tmp_footer);
    } else if (header[0]) {
        snprintf(composed, sizeof(composed), "%s\n\n%s", header, tmp_body);
    } else if (tmp_footer[0]) {
        snprintf(composed, sizeof(composed), "%s\n\n%s", tmp_body, tmp_footer);
    } else {
        snprintf(composed, sizeof(composed), "%s", tmp_body);
    }
    screen_end_game_open(true, composed);
    g->stats.game_over = true;   // ends input after view dismiss
}

void schedule_week_end(const Game *g, int commission_paid) {
    pending_week_phase = WK_PHASE_ASTROLOGY;
    pending_week_paid  = commission_paid;
    // week_id = (total_days - days_left) / WEEK_DAYS
    int wd = g->res ? g->res->time.week_days : 5;
    int start_days = g->res ? g->res->time.days_per_difficulty[0] : 900;
    if ((int)g->character.difficulty >= 0 &&
        (int)g->character.difficulty < 4 && g->res) {
        start_days = g->res->time.days_per_difficulty[(int)g->character.difficulty];
    }
    int passed = start_days - g->stats.days_left;
    pending_week_id = (wd > 0) ? (passed / wd) : 0;

    // Astrology creature: read the authoritative decision already made
    // in game.c end_day (GameApplyAstrology). This keeps the displayed
    // creature in sync with the dwelling repopulation that actually ran.
    pending_astrology_troop_idx = g->stats.last_astrology_troop;
}

// Open the friendly-foe accept flow .
// Used both when the hero steps onto a friendly foe and when a friendly
// foe steps onto the hero (foes_follow collision). nx/ny are the foe's
// tile (== hero's tile, since collision puts them on the same square).
//
// rolls a fresh creature on encounter and presents a free yes/no
// accept prompt; gold is never charged. If the player has no army slot
// available the foe flees ( banner).
void start_foe_friendly_flow(Game *game, Map *map, const Resources *res,
                             const char *foe_id, int nx, int ny) {
    int continent = 0;
    for (int i = 0; i < res->zone_count; i++) {
        if (strcmp(res->zones[i].id, game->position.zone) == 0) {
            continent = i;
            break;
        }
    }
    int kind = (int)((unsigned)(nx * 131 + ny * 17 + game->seed) & 3);
    int chance = (int)((unsigned)(nx * 17 + ny * 131 +
                       (game->seed >> 8)) % 100) + 1;
    int pool_slot = 0;
    while (pool_slot < RES_SPAWN_POOL_N - 1 &&
           chance > res->spawn.chance_curve[continent & 3][pool_slot]) {
        pool_slot++;
    }
    const char *troop_id = res->spawn.troop_pool[kind][pool_slot];
    const TroopDef *td = troop_id[0] ? troop_by_id(troop_id) : NULL;
    int tcount = td ? td->tier_counts[continent & 3] : 0;
    if (tcount < 2) tcount = 2;

    bool has_slot = false;
    if (td) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (strcmp(game->army[i].id, td->id) == 0 &&
                game->army[i].count > 0) { has_slot = true; break; }
            if (!game->army[i].id[0] || game->army[i].count == 0) {
                has_slot = true; break;
            }
        }
    }
    if (!td || !has_slot) {
        char body[RES_BANNER_LEN];
        resources_format_template(body, sizeof body,
                                  game->res->banners.encounter_wanderers,
                                  NULL, 0);
        open_dialog(NULL, body);
        MapClearInteractive(map, nx, ny);
        GameAddConsumed(game, game->position.zone, nx, ny);
        FoeState *f = GameFindFoe(game, foe_id);
        if (f) f->alive = false;
        return;
    }

    const char *label = GameNumberName(game, tcount);
    const ResBanners *bn = &game->res->banners;
    char body[240], cbuf[16];
    snprintf(cbuf, sizeof cbuf, "%d", tcount);
    if (label[0]) {
        ResTemplateVar vars[] = {
            { "LABEL", label },
            { "TROOP", td->name },
        };
        resources_format_template(body, sizeof body,
                                  bn->encounter_join_named, vars, 2);
    } else {
        ResTemplateVar vars[] = {
            { "COUNT", cbuf },
            { "TROOP", td->name },
        };
        resources_format_template(body, sizeof body,
                                  bn->encounter_join_numeric, vars, 2);
    }
    size_t k = 0;
    while (k + 1 < sizeof(pending_dwelling_troop) && td->id[k]) {
        pending_dwelling_troop[k] = td->id[k]; k++;
    }
    pending_dwelling_troop[k] = '\0';
    pending_friendly_count = tcount;
    k = 0;
    while (k + 1 < sizeof(pending_friendly_foe_id) && foe_id && foe_id[k]) {
        pending_friendly_foe_id[k] = foe_id[k]; k++;
    }
    pending_friendly_foe_id[k] = '\0';
    pending_dwelling_x = nx;
    pending_dwelling_y = ny;
    k = 0;
    while (k + 1 < sizeof(pending_dwelling_zone) && game->position.zone[k]) {
        pending_dwelling_zone[k] = game->position.zone[k]; k++;
    }
    pending_dwelling_zone[k] = '\0';
    pending_flow = FLOW_ACCEPT_FRIENDLY;
    prompt_yes_no_open(td->name, body);
    (void)map;
}

// Open the hostile-foe attack flow .
// Used both when the hero steps onto a hostile foe and when a hostile
// foe steps onto the hero.
void start_foe_hostile_flow(Game *game, const char *foe_id,
                            int nx, int ny) {
    const FoeState *f = GameFindFoeConst(game, foe_id);
    const ResBanners *bn = &game->res->banners;
    char prompt_body[320], frag[128];
    size_t off = 0;

    resources_format_template(frag, sizeof frag,
                              bn->encounter_hostile_header, NULL, 0);
    {
        int n = snprintf(prompt_body + off, sizeof prompt_body - off, "%s", frag);
        if (n > 0) off += (size_t)n;
    }

    int stacks_shown = 0;
    if (f) {
        for (int i = 0; i < GAME_ARMY_SLOTS &&
             off + 1 < sizeof(prompt_body); i++) {
            const Unit *u = &f->garrison[i];
            if (!u->id[0] || u->count == 0) continue;
            const TroopDef *td = troop_by_id(u->id);
            const char *tname = (td && td->name[0]) ? td->name : u->id;
            const char *label = GameNumberName(game, u->count);
            if (label[0]) {
                ResTemplateVar vars[] = {
                    { "LABEL", label }, { "TROOP", tname },
                };
                resources_format_template(frag, sizeof frag,
                                          bn->encounter_hostile_count_named,
                                          vars, 2);
            } else {
                char cbuf[16];
                snprintf(cbuf, sizeof cbuf, "%d", u->count);
                ResTemplateVar vars[] = {
                    { "COUNT", cbuf }, { "TROOP", tname },
                };
                resources_format_template(frag, sizeof frag,
                                          bn->encounter_hostile_count_numeric,
                                          vars, 2);
            }
            int n = snprintf(prompt_body + off, sizeof prompt_body - off,
                             "%s", frag);
            if (n > 0) off += (size_t)n;
            stacks_shown++;
        }
    }
    if (stacks_shown == 0) {
        resources_format_template(frag, sizeof frag,
                                  bn->encounter_hostile_unknown, NULL, 0);
        int n = snprintf(prompt_body + off, sizeof prompt_body - off, "%s", frag);
        if (n > 0) off += (size_t)n;
    }
    (void)off;
    size_t k = 0;
    while (k + 1 < sizeof(pending_foe_id) && foe_id[k]) {
        pending_foe_id[k] = foe_id[k]; k++;
    }
    pending_foe_id[k] = '\0';
    pending_foe_x = nx;
    pending_foe_y = ny;
    pending_flow = FLOW_ATTACK_FOE;
    prompt_yes_no_open(game->res->ui.dt_foes, prompt_body);
}
