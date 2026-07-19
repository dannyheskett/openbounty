// autoplay/plan.c
//
// The ONE dumb replay applier (AP-023): one RecPrim per call, shared verbatim
// by headless verification and the visible mode. Each prim's fingerprint is
// checked as a PRECONDITION against the live world; a mismatch is localized
// ([REPLAY-DIVERGE]) and aborts the run, so a diverged replay stops at the
// first bad primitive instead of acting on a world it does not describe.
// A lost fight is mirrored home the same way the live side handled it
// (AP-026).

#include "autoplay.h"

#include <stdio.h>
#include <stdlib.h>

#include "exec.h"
#include "flow_answer.h"
#include "pending.h"
#include "player_io.h"
#include "step.h"

bool plan_exec_step(struct ExecCtx *ctx, const RecPrim *p) {
    Game *g = ctx->g;
    uint32_t fp = rec_world_fp(g);
    if (fp != p->fp) {
        // A replay divergence is a hard failure: the recording no longer
        // describes the world it is being applied to, so continuing would
        // silently act on a wrong state. Abort immediately, no exceptions.
        fprintf(stderr, "[REPLAY-DIVERGE] fp=%08x expected=%08x kind=%d\n",
                fp, p->fp, (int)p->kind);
        fflush(stderr);
        abort();
    }
    switch ((RecKind)p->kind) {
    case REC_MOVE:
        GameStep(g, ctx->map, ctx->fog, ctx->res, p->dx, p->dy);
        break;
    case REC_ANSWER: {
        FlowAnswer ans = { (PromptAnswer)p->ans_kind, p->number };
        PlayerIoCombatOutcome oc = PLAYER_IO_COMBAT_NOT_RUN;
        if (p->outcome != PLAYER_IO_COMBAT_NOT_RUN &&
            ans.kind == FLOW_ANS_YES)
            oc = (PlayerIoCombatOutcome)autoplay_apply_recorded_combat(ctx, p);
        PlayerIoPresentation pres;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, ans, oc, &pres);
        // A lost fight is a temp death: the engine teleported the hero home,
        // and the LIVE side then ran exec_temp_death (map reload + the
        // bookkeeping). The replay must mirror it or the hero walks the new
        // zone's coordinates on the OLD zone's map -- an invisible desync
        // until a terrain difference changes a day-spend (measured: seed 15
        // diverged one day at a desert tile the stale map called grass).
        if (pres.temp_death) exec_temp_death(ctx);
        break;
    }
    case REC_ACTION:
        autoplay_apply_rec_action(ctx, p);
        break;
    }
    // Presentation-neutral by design: the engine raised its view/message
    // requests identically to live play, and CONSUMING them is the host's
    // job, not this shared applier's -- the visible shell presents them (the
    // same shell_pump_player_io_view dispatcher normal play uses) and a
    // headless host drains them. Draining here made the visible replay eat
    // its own town screens.
    pending_week_phase = WK_PHASE_NONE;
    return true;
}
