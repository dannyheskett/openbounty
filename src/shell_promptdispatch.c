// src/shell_promptdispatch.c
//
// Shell ADAPTER for prompt-flow resolution. The state-mutation half
// of each flow now lives in the engine (engine/flow_resolve.c) so autoplay can
// share it. This file keeps the three host-side concerns:
//   - reading the answer (prompt_update / prompt_text_input_value);
//   - running combat for siege/attack flows via RunCombat (rendered), then
//     handing the CombatResult to the engine apply-core;
//   - presentation: the win cartoon, view dismissal, and perform_temp_death
//     on the LOSS signal the apply-cores return.
//
// The dependency arrow stays shell -> engine: the engine apply-cores never
// run combat or touch render/view state.

#include "shell_promptdispatch.h"
#include "prompt_impl.h"      // prompt_view: the province rows, to put the picker back
// Sailing with the picture is a two-step in the SHELL: the province picked
// (1..5), then confirmed. -1 = no pick outstanding.
static int s_sail_pick = -1;
// The province rows, kept so "No" can put the picker back up.
static char s_sail_body[256];
#include "modern/location.h"

#include <stdio.h>
#include <string.h>

#include "combat_loop.h"
#include "combat_render.h"
#include "tile.h"
#include "map.h"
#include "tile_cache.h"
#include "end_cartoon.h"
#include "flow_resolve.h"
#include "flows.h"
#include "pending.h"
#include "prompt.h"
#include "shell_tempdeath.h"
#include "tables.h"
#include "ui.h"
#include "views.h"

// Map the shell's PromptResult to the engine's PromptAnswer (same ordering;
// kept as an explicit switch so a divergence is a compile-visible change).
static FlowAnswer to_flow_answer(PromptResult r, int number) {
    FlowAnswer a;
    a.number = number;
    switch (r) {
    case PROMPT_RESULT_YES:    a.kind = FLOW_ANS_YES;    break;
    case PROMPT_RESULT_NO:     a.kind = FLOW_ANS_NO;     break;
    case PROMPT_RESULT_CANCEL: a.kind = FLOW_ANS_CANCEL; break;
    case PROMPT_RESULT_1:      a.kind = FLOW_ANS_1;      break;
    case PROMPT_RESULT_2:      a.kind = FLOW_ANS_2;      break;
    case PROMPT_RESULT_3:      a.kind = FLOW_ANS_3;      break;
    case PROMPT_RESULT_4:      a.kind = FLOW_ANS_4;      break;
    case PROMPT_RESULT_5:      a.kind = FLOW_ANS_5;      break;
    case PROMPT_RESULT_NONE: default: a.kind = FLOW_ANS_NONE; break;
    }
    return a;
}

// Build a CombatTarget for a castle/foe siege, run the rendered fight, and
// return the outcome. Centralizes the shell-only RunCombat call so each flow
// just supplies identity.
// The combat ground: with sprites.ui.combat_ground "terrain" the map tile the
// hero stands on (grass, desert, ...); water, which the hero only crosses by
// boat, falls back to grass. Otherwise the pack's field tile.
void shell_set_combat_ground(ShellCtx *ctx) {
    Texture2D none = { 0 };
    if (!resources_combat_ground_is_terrain(ctx->res) || !ctx->map) {
        combat_render_set_ground(none);
        return;
    }
    const Tile *t = MapGetTile(ctx->map, ctx->game->position.x, ctx->game->position.y);
    const char *terrain = t ? TerrainName(t->terrain) : "grass";
    if (strcmp(terrain, "water") == 0) terrain = "grass";
    char art[TILE_ART_NAME_LEN];
    combat_render_set_ground(tile_cache_get(MapTerrainArt(ctx->map, terrain, art, sizeof art)));
}

static CombatResult run_castle_combat(ShellCtx *ctx, const char *castle_id) {
    shell_set_combat_ground(ctx);
    Game            *g  = ctx->game;
    const Resources *r_ = ctx->res;
    CastleRecord *cr = GameFindCastle(g, castle_id);
    const ResCastle *rc = resources_castle_by_id(r_, castle_id);
    const VillainDef *v = (cr && cr->villain_id[0])
                        ? villain_by_id(cr->villain_id) : NULL;
    CombatTarget tgt = { 0 };
    tgt.name = v && v->name[0] ? v->name
             : (rc && rc->name[0] ? rc->name : castle_id);
    tgt.seed_key = castle_id;            // stable identity for RNG seed
    if (cr) {
        tgt.garrison = cr->garrison;
        tgt.garrison_slots = GAME_ARMY_SLOTS;
    }
    return RunCombat(g, ctx->sprites, ctx->render_target,
                     COMBAT_MODE_CASTLE, &tgt);
}

bool prompt_dispatch_tick(ShellCtx *ctx) {
    if (!prompt_is_active()) return false;

    // A live message dialog OWNS input, so defer the whole dispatch while one
    // is up (issue #19). A single engine step can raise both: a chest result
    // message AND, from the foe that walked onto the hero in that same step,
    // the attack yes/no -- prompt_yes_no_open fires at the emit site while the
    // message is still queued behind it. Answering the prompt first started
    // RunCombat with the dialog still open, and combat draws an open dialog as
    // a centered modal over the battlefield while the main loop -- which owns
    // dialog input -- is suspended: an undismissable message covering the
    // fight. Returning false (rather than swallowing the frame) lets main.c's
    // if/else chain reach its dialog branch, so the player presses a key to
    // clear the message and the prompt underneath answers on the next frame.
    if (dialog_is_active()) return false;

    Game            *g  = ctx->game;
    Map             *m  = ctx->map;
    Fog             *f  = ctx->fog;
    const Resources *r_ = ctx->res;

    // Bottom-frame prompt (yes/no or numeric). When it returns a
    // result, dispatch based on pending_flow.
    PromptResult r = prompt_update();
    if (r == PROMPT_RESULT_NONE) return true;

    // Sailing, with the picture: pick a province, then confirm it. The engine
    // still sees ONE answer (the pick), so autoplay, replays and a pack
    // without the picture are unchanged (REQ-221c).
    if (pending_flow == FLOW_NAVIGATE && CL_IS_MODERN &&
        r_->sprites.sail_backdrop[0] && r_->banners.body_navigate_confirm[0]) {
        if (s_sail_pick < 0) {
            // Anything but a province (Cancel, Esc) falls through to the
            // ordinary answer below, which ends the sail as it always did.
            if (r >= PROMPT_RESULT_1 && r <= PROMPT_RESULT_5) {
            s_sail_pick = (int)r;
            snprintf(s_sail_body, sizeof s_sail_body, "%s",
                     prompt_view() ? prompt_view()->body : "");
            int zi = (int)r - (int)PROMPT_RESULT_1;
            const char *zone = (zi >= 0 && zi < pending_nav_count)
                             ? pending_nav_zones[zi] : "";
            const ResZone *z = resources_zone_by_id(r_, zone);
            ResTemplateVar v[] = { { "ZONE", (z && z->name[0]) ? z->name : zone } };
            char q[RES_BANNER_LEN];
            resources_format_template(q, sizeof q, r_->banners.body_navigate_confirm, v, 1);
            prompt_yes_no_open(r_->ui.dt_navigate, q);
            prompt_set_req_kind(PIO_ASK_SCENE);
            return true;
            }
        } else {
            int pick = s_sail_pick;
            s_sail_pick = -1;
            if (r != PROMPT_RESULT_YES) {
                // No, or Esc: back to the provinces, the sail still open.
                prompt_numeric_open(r_->ui.dt_navigate, s_sail_body,
                                    pending_nav_count);
                prompt_set_req_kind(PIO_ASK_SCENE);
                return true;
            }
            r = (PromptResult)pick;
        }
    }

    // Capture the flow being answered BEFORE the router pops the queue + clears
    // pending_flow, so the combat resolution + recruit-count read below key off
    // the right flow.
    PendingFlow flow = pending_flow;

    // Resolve combat for combat-bearing flows on the answer (the shell renders
    // it via RunCombat). The engine NEVER renders combat: we pass the
    // outcome into the one router. A NO answer means the player declined -- no
    // combat run.
    PlayerIoCombatOutcome outcome = PLAYER_IO_COMBAT_NOT_RUN;
    if (r == PROMPT_RESULT_YES &&
        (flow == FLOW_SIEGE_MONSTER || flow == FLOW_SIEGE_VILLAIN)) {
        CombatResult cr = run_castle_combat(ctx, pending_castle_id);
        outcome = (cr == COMBAT_RESULT_WIN) ? PLAYER_IO_COMBAT_WON
                                            : PLAYER_IO_COMBAT_LOST;
    } else if (r == PROMPT_RESULT_YES && flow == FLOW_ATTACK_FOE &&
               pending_foe_id[0]) {
        FoeState *foe = GameFindFoe(g, pending_foe_id);
        CombatTarget tgt = { 0 };
        tgt.name = "Hostile band";
        tgt.seed_key = pending_foe_id;      // stable identity for RNG seed
        if (foe) { tgt.garrison = foe->garrison;
                   tgt.garrison_slots = GAME_ARMY_SLOTS; }
        shell_set_combat_ground(ctx);
        CombatResult cr = RunCombat(g, ctx->sprites, ctx->render_target,
                                    COMBAT_MODE_FOE, &tgt);
        outcome = (cr == COMBAT_RESULT_WIN) ? PLAYER_IO_COMBAT_WON
                                            : PLAYER_IO_COMBAT_LOST;
    }

    // The recruit count is read render-side (text-input value) and carried in
    // the FlowAnswer.number.
    int typed = (flow == FLOW_RECRUIT && r == PROMPT_RESULT_YES)
                  ? prompt_text_input_value() : 0;

    // Modern temple and dwelling screens stay up to show the deal: note the
    // purse (and which troop) before the router carries the answer out.
    bool deal = CL_IS_MODERN && r == PROMPT_RESULT_YES &&
                (flow == FLOW_RECRUIT || flow == FLOW_ALCOVE);
    char deal_troop[32];
    snprintf(deal_troop, sizeof deal_troop, "%s", flow == FLOW_RECRUIT ? pending_dwelling_troop : "");
    if (deal) loc_deal_begin(g);

    // ONE shared router (mode parity): mutate engine state for this
    // flow, returning the host-side presentation directives we act on below.
    PlayerIoPresentation pres;
    player_io_answer(g, m, f, r_, to_flow_answer(r, typed), outcome, &pres);
    if (deal) loc_deal_done(g, typed, deal_troop);

    // Host-side presentation -- the engine cannot do these.
    if (pres.won_game) {
        run_end_cartoon(ctx->render_target, r_, ctx->sprites, g);
        show_win_game(g, r_);
    } else if (pres.game_over) {
        show_lose_game(g, r_);
    }
    if (pres.temp_death) perform_temp_death(g, m, f, r_);
    if (pres.week_commission > 0) schedule_week_end(g, pres.week_commission);
    // Modern temple and dwelling screens stay up to show the answer; the main
    // loop closes them once nothing is left to show (main.c).
    bool loc_screen = CL_IS_MODERN &&
        (pres.dismiss_view == VIEW_ALCOVE || pres.dismiss_view == VIEW_DWELLING);
    if (pres.dismiss_view != VIEW_NONE && !loc_screen &&
        views_active() == pres.dismiss_view) views_dismiss();

    if (pres.chain_dismiss_last && pres.chain_slot >= 0) {
        // Chain into the "sent back to King in disgrace" confirm. (The router
        // left pending_flow set so this next decision can be raised.)
        pending_castle_id[0] = (char)('0' + pres.chain_slot);
        pending_castle_id[1] = '\0';
        pending_flow = FLOW_DISMISS_LAST;
        char body[RES_BANNER_LEN];
        resources_format_template(body, sizeof body,
                                  g->res->banners.body_dismiss_last, NULL, 0);
        player_io_ask(g, FLOW_DISMISS_LAST, REQ_PROMPT_YES_NO,
                      g->res->ui.dt_dismiss_last, body);
        return true;   // chained-prompt: skip the FLOW_NONE reset
    }
    if (flow == FLOW_DISMISS_LAST) pending_castle_id[0] = '\0';
    return true;
}
