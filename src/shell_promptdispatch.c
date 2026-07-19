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

#include <stdio.h>
#include <string.h>

#include "combat_loop.h"
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
static CombatResult run_castle_combat(ShellCtx *ctx, const char *castle_id) {
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

    Game            *g  = ctx->game;
    Map             *m  = ctx->map;
    Fog             *f  = ctx->fog;
    const Resources *r_ = ctx->res;

    // Bottom-frame prompt (yes/no or numeric). When it returns a
    // result, dispatch based on pending_flow.
    PromptResult r = prompt_update();
    if (r == PROMPT_RESULT_NONE) return true;

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
        CombatResult cr = RunCombat(g, ctx->sprites, ctx->render_target,
                                    COMBAT_MODE_FOE, &tgt);
        outcome = (cr == COMBAT_RESULT_WIN) ? PLAYER_IO_COMBAT_WON
                                            : PLAYER_IO_COMBAT_LOST;
    }

    // The recruit count is read render-side (text-input value) and carried in
    // the FlowAnswer.number.
    int typed = (flow == FLOW_RECRUIT && r == PROMPT_RESULT_YES)
                  ? prompt_text_input_value() : 0;

    // ONE shared router (mode parity): mutate engine state for this
    // flow, returning the host-side presentation directives we act on below.
    PlayerIoPresentation pres;
    player_io_answer(g, m, f, r_, to_flow_answer(r, typed), outcome, &pres);

    // Host-side presentation -- the engine cannot do these.
    if (pres.won_game) {
        run_end_cartoon(ctx->render_target, r_, ctx->sprites);
        show_win_game(g, r_);
    } else if (pres.game_over) {
        show_lose_game(g, r_);
    }
    if (pres.temp_death) perform_temp_death(g, m, f, r_);
    if (pres.week_commission > 0) schedule_week_end(g, pres.week_commission);
    if (pres.dismiss_view != VIEW_NONE &&
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
        prompt_yes_no_open(g->res->ui.dt_dismiss_last, body);
        player_io_raise_decision(g, FLOW_DISMISS_LAST, REQ_PROMPT_YES_NO,
                                 g->res->ui.dt_dismiss_last, body);
        return true;   // chained-prompt: skip the FLOW_NONE reset
    }
    if (flow == FLOW_DISMISS_LAST) pending_castle_id[0] = '\0';
    return true;
}
