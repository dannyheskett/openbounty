// src/shell_actions.c

#include "shell_actions.h"

#include <stdio.h>

#include "flows.h"
#include "pending.h"
#include "prompt.h"
#include "savegame.h"
#include "savepath.h"
#include "shell_fastquit.h"
#include "tile.h"
#include "ui.h"
#include "views.h"

void shell_dispatch_action(ShellCtx *ctx, const InputState *in) {
    Game            *g  = ctx->game;
    Map             *m  = ctx->map;
    Fog             *f  = ctx->fog;
    const Resources *r_ = ctx->res;

    switch (in->action) {
    case INPUT_ACTION_VIEW_ARMY:       views_set(VIEW_ARMY);      break;
    case INPUT_ACTION_VIEW_CHARACTER:  views_set(VIEW_CHARACTER); break;
    case INPUT_ACTION_VIEW_CONTRACT:   views_set(VIEW_CONTRACT);  break;
    case INPUT_ACTION_VIEW_PUZZLE:     views_set(VIEW_PUZZLE);    break;
    case INPUT_ACTION_VIEW_MAP:        views_set(VIEW_WORLDMAP);  break;
    case INPUT_ACTION_CAST_SPELL:
        // no_spell_banner pops if the player doesn't know magic yet.
        // Substitute the alcove location from data so the banner stays
        // in sync with whatever zone hosts it.
        if (!g->stats.knows_magic) {
            char body[RES_BANNER_LEN];
            const ResZone *az = NULL;
            for (int zi = 0; zi < r_->zone_count; zi++) {
                if (r_->zones[zi].magic_alcove_x >= 0 &&
                    r_->zones[zi].magic_alcove_y >= 0) {
                    az = &r_->zones[zi];
                    break;
                }
            }
            char xs[16], ys[16];
            snprintf(xs, sizeof xs, "%d", az ? az->magic_alcove_x : 0);
            snprintf(ys, sizeof ys, "%d", az ? az->magic_alcove_y : 0);
            ResTemplateVar vars[3] = {
                { "ZONE", az ? (az->name[0] ? az->name : az->id) : "" },
                { "X",    xs },
                { "Y",    ys },
            };
            resources_format_template(body, sizeof body,
                                      r_->banners.no_spell_banner, vars, 3);
            player_io_message(g, NULL, body);
        } else {
            views_set(VIEW_SPELLS);
            views_spells_set_mode(true);
        }
        break;
    case INPUT_ACTION_OPTIONS_MENU:    views_set(VIEW_OPTIONS);   break;
    case INPUT_ACTION_SAVE_QUIT: {
        // Q saves unconditionally, then displays a "Press Ctrl-Q to
        // Quit / any other key to continue" dialog. The dialog handler
        // at the bottom of the main loop watches Ctrl-Q to exit.
        char path[1024];
        const char *pid = r_->pack_id[0] ? r_->pack_id : NULL;
        if (SavePathGetSlot(pid, 0, path, sizeof(path))) {
            SaveGameWrite(path, g, m, f);
            SavePathFlush();
        }
        {
            char body[RES_BANNER_LEN];
            resources_format_template(body, sizeof body,
                                      r_->banners.body_save_confirm,
                                      NULL, 0);
            player_io_message(g, NULL, body);
        }
        break;
    }
    case INPUT_ACTION_FAST_QUIT:
        // Status-bar prompt, NOT a bottom dialog. chrome.c queries
        // fast_quit_is_active() to substitute the prompt string for
        // the "Days Left:N" status.
        fast_quit_open();
        break;

    case INPUT_ACTION_END_WEEK: {
        // spend_week: advance to next week boundary.
        int paid = 0;
        GameSpendWeek(g, &paid);
        if (paid > 0) schedule_week_end(g, paid);
        if (g->stats.game_over) show_lose_game(g, r_);
        break;
    }
    case INPUT_ACTION_SEARCH: {
        // "It will take 10 days to do a search of this area.
        // Search (y/n)?"
        pending_flow = FLOW_SEARCH;
        {
            char body[RES_BANNER_LEN], dbuf[12];
            snprintf(dbuf, sizeof dbuf, "%d", r_->tuning.search_cost_days);
            ResTemplateVar v[] = { { "DAYS", dbuf } };
            resources_format_template(body, sizeof body,
                                      r_->banners.body_search, v, 1);
            prompt_yes_no_open(r_->ui.dt_search, body);
            player_io_raise_decision(g, FLOW_SEARCH, REQ_PROMPT_YES_NO,
                                     r_->ui.dt_search, body);
        }
        break;
    }
    case INPUT_ACTION_DISMISS_ARMY: {
        // pick a slot (1..5) to dismiss one troop stack.
        int n_slots = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (g->army[i].id[0] && g->army[i].count > 0) n_slots++;
        }
        if (n_slots <= 0) break;
        pending_flow = FLOW_DISMISS_ARMY;
        {
            char body[RES_BANNER_LEN];
            resources_format_template(body, sizeof body,
                                      r_->banners.body_dismiss_pick,
                                      NULL, 0);
            prompt_numeric_open(r_->ui.dt_dismiss_army, body,
                                GAME_ARMY_SLOTS);
            PlayerRequest *pr = player_io_raise_decision(
                g, FLOW_DISMISS_ARMY, REQ_PROMPT_NUMERIC,
                r_->ui.dt_dismiss_army, body);
            if (pr) pr->prompt_max = GAME_ARMY_SLOTS;
        }
        break;
    }
    case INPUT_ACTION_FLY:
        // RIDE -> FLY; legality lives in the engine (GamePlayerCanFly inside).
        // Silent on failure, as before.
        GameMountFly(g);
        break;
    case INPUT_ACTION_LAND:
        // FLY -> RIDE only on plain grass; legality lives in the engine.
        GameLandHere(g, m);
        break;
    case INPUT_ACTION_NEW_CONTINENT: {
        // On water, show "Go to which continent?" with discovered
        // continents numbered 1..N; player picks a digit; spend_week
        // runs after the switch.
        const ResBanners *bn = &r_->banners;
        if (g->travel_mode != TRAVEL_BOAT) {
            player_io_message(g, NULL, bn->body_must_be_sailing);
            break;
        }
        const ResZone *cur = resources_zone_by_id(r_, g->position.zone);
        if (!cur || cur->neighbor_count == 0) {
            player_io_message(g, NULL, bn->body_no_continents);
            break;
        }
        // Only offer neighbors whose navmap has been picked up.
        // game.c:167 auto-discovers the home zone; every other zone
        // requires the player to step onto an INTERACT_NAVMAP tile
        // (engine/step.c:454, which flips world.zones_discovered[i]).
        // Without this gate, the new-continent prompt would let the
        // hero sail anywhere on day one.
        pending_nav_count = 0;
        char body[256];
        int bo = 0;
        for (int ni = 0; ni < cur->neighbor_count &&
                         pending_nav_count < 5; ni++) {
            const ResZone *nz = resources_zone_by_id(r_, cur->neighbors[ni]);
            if (!nz) continue;
            int nz_idx = (int)(nz - r_->zones);
            if (nz_idx < 0 || nz_idx >= GAME_CONTINENTS ||
                !g->world.zones_discovered[nz_idx]) continue;
            size_t k = 0;
            while (k + 1 < sizeof(pending_nav_zones[0]) && nz->id[k]) {
                pending_nav_zones[pending_nav_count][k] = nz->id[k];
                k++;
            }
            pending_nav_zones[pending_nav_count][k] = '\0';
            char ibuf[12];
            snprintf(ibuf, sizeof ibuf, "%d", pending_nav_count + 1);
            ResTemplateVar v[] = {
                { "INDEX", ibuf },
                { "ZONE",  nz->name[0] ? nz->name : nz->id },
            };
            char frag[96];
            resources_format_template(frag, sizeof frag,
                                      bn->body_navigate_row, v, 2);
            int n = snprintf(body + bo, sizeof(body) - bo, "%s", frag);
            if (n < 0 || n >= (int)(sizeof(body) - bo)) break;
            bo += n;
            pending_nav_count++;
        }
        if (pending_nav_count == 0) {
            player_io_message(g, NULL, bn->body_no_continents);
            break;
        }
        pending_flow = FLOW_NAVIGATE;
        prompt_numeric_open(r_->ui.dt_navigate, body, pending_nav_count);
        {
            PlayerRequest *pr = player_io_raise_decision(
                g, FLOW_NAVIGATE, REQ_PROMPT_NUMERIC, r_->ui.dt_navigate, body);
            if (pr) pr->prompt_max = pending_nav_count;
        }
        break;
    }
    case INPUT_ACTION_VIEW_CONTROLS:
        views_set(VIEW_CONTROLS);
        break;
    case INPUT_ACTION_REST: {
        // numpad 5 rests one day (one step worth) in place. Drives
        // day/week tick the same way a real step does.
        bool day_end = false, week_end = false;
        int paid = 0;
        GameOnStep(g, false, &day_end, &week_end, &paid);
        (void)day_end;
        if (week_end) schedule_week_end(g, paid);
        if (g->stats.game_over) show_lose_game(g, r_);
        break;
    }
    case INPUT_ACTION_NONE:
    default: break;
    }
}
