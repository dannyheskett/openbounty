// src/shell_autoplay.c
//
// Shell adapter for visible autoplay. See header. Builds the committed
// plan once (PLAN phase, §5.3), then drives the shared committed-plan executor
// one decision per pacing window (EXECUTE phase), rendering between steps via
// the normal main-loop draw. This is the SAME plan_build + plan_exec_step path
// the headless runner uses (autoplay/autoplay.c), so a visible run reproduces
// the headless decision sequence for a given seed (mode parity); the
// shell only adds rendering and pacing.

#include "shell_autoplay.h"

#include <stdio.h>      // snprintf, printf
#include <stdlib.h>
#include <string.h>     // memset

#include "autoplay.h"   // engine-pure planner API
#include "planner.h"    // planner() / PrimRun — the live planner the visible run replays
#include "worldsnap.h"  // snapshot the boot world, restore it after the planning pass
#include "plan.h"       // the dumb committed-plan replayer (plan_exec_*) the shell drives
#include "pending.h"    // pending_flow (to detect a raised prompt)
#include "prompt.h"     // prompt_is_active / prompt_dismiss (shell UI)
#include "ui.h"         // dialog_is_active / dialog_dismiss (shell UI)
#include "views.h"      // views_active / views_dismiss / town demo-select (VIEW hold)
#include "combat_replay.h"  // RenderCombatRecord (visible combat animator, WS-7)
#include "player_io.h"  // player_io_front / player_io_ack (queue state + view hold)
#include "shell_frame.h"    // shell_present_frame (render a held screen per frame)
#include "frame_host.h"     // frame_host_time / frame_host_should_close
#include "screenshot.h"     // screenshot_tick (backtick capture during the hold)
#include "audio.h"          // audio_tick (keep audio alive during the hold)
#include "view_kind.h"      // VIEW_TOWN
#include "ob_trace.h"   // TEMP OB_TRACE_PRES observational trace (AP-046)

static Plan             s_plan;            // committed plan (built at begin)
static PlanExecutor     s_exec;            // per-turn replay cursor
static bool   s_active = false;
static bool   s_have_plan = false;
static bool   s_demo = false;              // curated attract-mode run (loops)
static double s_step_delay = 0.0;
static double s_next_step_at = 0.0;
static int    s_obj_done = 0;              // planner tally (for the end summary)
static int    s_obj_total = 0;

// DEMO (attract) pacing + curation, baked in — no env / CLI knob (config rule:
// pacing is code-only). Tuned by eye for a brisk-but-legible watch: ~0.22 s
// between adventure steps (a walking cadence), ~1.4 s holding each chest/recruit/
// fight prompt so the viewer reads the choice. The combat animator scales its
// beat off s_step_delay, so fights slow with the walk.
//
// The DemoCap curates a short, RULE-LEGAL arc that ends on the first villain
// capture and showcases the core game loop:
//   * a couple of chests (treasure) + one misc pickup (navmap/orb/artifact);
//   * the hero grows its army by the rules — the planner recruits at the home
//     castle (STEP_RECRUIT_HOME) as needed, no cheat army;
//   * the villain prereq chain — buy siege weapons (a town visit) and take the
//     matching contract — then LAY SIEGE to the villain's castle and capture;
//   * one wandering-army (foe) battle.
// reach_villain shapes the fight bucket: foes are capped at max_fights, monster
// castles suppressed, and the run ends after exactly ONE villain capture (a real
// one — the contract gate is satisfied). No starting-army boost: the engine's
// recruit lever makes the siege winnable legitimately. PARTIAL verdict by design
// (the rest of the game is left unplayed). Confined to the starting zone.
#define DEMO_STEP_DELAY 0.22
#define DEMO_READ_DWELL 1.40
// (The old DemoCap curation lived in plan_build's AutoplayPlanner; with the single
// planner() the demo replays a full zone-0 planner run at the watchable pace below.)

// Presentation phase (WS-8): make engine-raised prompts + info dialogs readable
// for a beat before the executor's next tick answers them. Pacing only.
typedef enum { AP_PRES_IDLE = 0, AP_PRES_MESSAGE, AP_PRES_DECISION } ApPres;
static ApPres  s_pres = AP_PRES_IDLE;
static double  s_pres_until = 0.0;
static double  s_read_dwell = 0.0;    // seconds; 0 = full-speed replay

// Visible-mode combat animator (PlanCombatAnimator, WS-7): DRAW the recorded
// per-turn battle into a throwaway Combat (state-inert) via RenderCombatRecord.
// ctx is the ShellCtx (carries the live pre-fight game + sprites + render
// target). The executor calls this before applying the fight's outcome; on a
// window-close abort it flags the run done so main.c hands off / quits.
static bool s_combat_aborted = false;
void shell_autoplay_combat_animator(void *ctx, CombatMode mode,
                                    const CombatTurnRecord *rec) {
    ShellCtx *c = (ShellCtx *)ctx;
    if (!c || !rec) return;
    CombatReplayStatus st = RenderCombatRecord(c, mode, rec, c->sprites,
                                               c->render_target);
    if (st == COMBAT_REPLAY_ABORT) s_combat_aborted = true;
}

// Present the held screen for `beat_s` seconds, pumping audio / movie-frames /
// screenshots / window-close each frame (mirrors combat_replay's replay_beat).
// Returns false if the window was asked to close (abort the run).
static bool present_view_beat(ShellCtx *c, double beat_s) {
    double until = frame_host_time() + beat_s;
    do {
        if (frame_host_should_close()) return false;
        audio_tick();
        shell_present_frame(c->game, c->map, c->fog, c->sprites,
                            c->render_target);
        screenshot_tick(*(RenderTexture2D *)c->render_target, "shot");
    } while (frame_host_time() < until);
    return true;
}

// Visible-mode VIEW presenter (PlanViewPresenter): SHOW the screen the way a
// player sees it. Called by the executor right after a view-raising primitive
// enqueued its REQ_VIEW (the engine state mutation was a SEPARATE primitive, so
// this is state-inert — it only renders the shell view stack). For VIEW_TOWN it
// puts the town screen up, WALKS the menu cursor down to the Siege row one row per
// beat, selects it (the result panel shows — the purchase already happened in the
// engine, so this is the idempotent "already own" path), holds a read beat, then
// dismisses. Headless never calls this (NULL presenter) — it just drains the
// REQ_VIEW, so Game/queue end identical across modes.
void shell_autoplay_view_presenter(void *ctx, ViewKind view, int chosen_row) {
    ShellCtx *c = (ShellCtx *)ctx;
    if (!c || !c->game) return;
    double dwell = (s_read_dwell > 0.0) ? s_read_dwell : 0.0;
    double step  = (s_step_delay > 0.0) ? s_step_delay : 0.0;
    if (dwell <= 0.0 && step <= 0.0) return;   // full-speed (autoplay): no hold

    if (view == VIEW_TOWN) {
        // Put the town screen on the shell stack (context was set by the apply's
        // views_open_town). It is NOT in Game, so this never affects byte-equality.
        if (views_active() != VIEW_TOWN) views_set(VIEW_TOWN);
        // Hold the freshly-opened town menu a beat so the watcher reads it.
        if (!present_view_beat(c, dwell)) { views_dismiss(); return; }
        // RENDER-ONLY: walk the cursor toward the row the recorded action used
        // (chosen_row), one row per beat, so the watcher sees the navigation a
        // player would make. We NEVER execute the row — the transaction is a
        // SEPARATE recorded engine primitive (e.g. RA_BUY_SIEGE) the executor
        // already applies via the real gate-step — so this is state-inert and
        // headless==visible holds (no presenter-only mutation). chosen_row < 0
        // means "no town action" — just present the menu.
        if (chosen_row >= 0) {
            for (;;) {
                bool done = views_town_demo_step_cursor(chosen_row);
                if (!present_view_beat(c, step > 0.0 ? step : dwell)) break;
                if (done) break;
            }
            present_view_beat(c, dwell);   // hold on the selected row a beat
        }
        views_dismiss();
    }
}

double shell_autoplay_step_delay(void) { return s_step_delay; }

double shell_autoplay_read_dwell(void) { return s_read_dwell; }

bool shell_autoplay_active(void) { return s_active; }

bool shell_autoplay_is_demo(void) { return s_active && s_demo; }

bool shell_autoplay_begin(ShellCtx *ctx) {
    return shell_autoplay_begin_ex(ctx, /*demo=*/false);
}

bool shell_autoplay_begin_ex(ShellCtx *ctx, bool demo) {
    s_active = false;
    s_have_plan = false;
    s_demo = false;
    if (!ctx || !ctx->game || !ctx->map || !ctx->fog || !ctx->res) return false;

    // PLAN phase: run the SINGLE planner() on the live world (it mutates forward to
    // the terminal state, recording the proven primitive sequence), then RESTORE the
    // boot world so the visible replay starts from turn zero. The recording IS the run
    // — identical to the headless oracle for this seed (one planner, mode parity).
    printf("%s: planning (running the planner)...\n", demo ? "demo" : "autoplay");
    WorldSnapshot boot;
    worldsnap_capture(&boot, ctx->game, ctx->map, ctx->fog);
    PrimRun run; memset(&run, 0, sizeof run);
    bool ok = planner(ctx->game, ctx->map, ctx->fog, (Resources *)ctx->res,
                      /*diag=*/NULL, /*zone_scope=*/demo ? 0 : -1, &run);
    worldsnap_restore(&boot, ctx->game, ctx->map, ctx->fog);
    pending_reset();                       // the planner answered its flows; clear scratch
    if (!ok) {
        printf("%s: planner failed — inactive\n", demo ? "demo" : "autoplay");
        primrun_free(&run);
        return false;
    }

    // Adapt the planner's recording into the dumb replayer's Plan view. ONLY the
    // recording is replayed; admitted_count=0 disables the per-step boundary_fp
    // checkpoint (planner() emits no per-step fingerprints — the recording replays
    // deterministically by construction). Move ownership of the heap (rec/combats)
    // into s_plan; shell_autoplay_end's plan_free releases it.
    memset(&s_plan, 0, sizeof s_plan);
    s_plan.rec     = run.rec;
    s_plan.combat  = run.combats;
    s_plan.verdict = run.verdict;
    s_plan.admitted_count = 0;
    run.rec = (RecBuf){0};                 // ownership moved out; primrun_free won't double-free
    run.combats = (CombatRecList){0};
    s_obj_done  = run.obj_done;
    s_obj_total = run.obj_total;
    primrun_free(&run);

    s_have_plan = true;
    s_demo = demo;
    printf("%s: plan ready — %d/%d objectives, verdict %s. Executing...\n",
           demo ? "demo" : "autoplay", s_obj_done, s_obj_total,
           autoplay_verdict_str(s_plan.verdict));

    // Planning ran the FULL simulation in THIS shell process, so its last beat
    // (typically a foe fight) left the shell's prompt widget OPEN and a dialog
    // possibly up — shell statics the engine-side worldsnap_restore + pending_reset
    // cannot touch. Clear them so execution does not open with a stale phantom
    // "Foes!" prompt painted over the board at turn zero.
    if (prompt_is_active()) prompt_dismiss();
    if (dialog_is_active()) dialog_dismiss();

    // EXECUTE phase: the DUMB APPLIER replays the proven primitive recording on
    // the live (boot-state) world — no planner (WS-4). One primitive per pacing
    // window in tick(). The combat animator (WS-7) is wired here so visible mode
    // DRAWS each recorded fight; until then NULL = resolve invisibly.
    plan_exec_begin(&s_exec, &s_plan,
                    shell_autoplay_combat_animator,
                    shell_autoplay_view_presenter, ctx);

    s_active = true;
    // Pacing: autoplay replays at FULL SPEED (prompts flash by — "play back as
    // fast as possible"); demo replays at a TUNED, WATCHABLE pace with a per-
    // prompt read dwell. Both are code-fixed — no env / CLI knob.
    s_step_delay   = demo ? DEMO_STEP_DELAY : 0.0;
    s_read_dwell   = demo ? DEMO_READ_DWELL : 0.0;
    s_next_step_at = 0.0;   // first tick acts immediately
    s_combat_aborted = false;
    s_pres = AP_PRES_IDLE;
    s_pres_until = 0.0;
    return true;
}

// The tick is a presentation state machine (WS-8): IDLE advances the replay,
// MESSAGE/DECISION hold the engine-raised UI for a read beat before answering.
// Pacing only — it changes WHEN we advance, never the decision/state sequence
// (the recorded answer is pre-decided), so mode parity is preserved.
bool shell_autoplay_tick(ShellCtx *ctx, double now_s, bool *out_done) {
    if (out_done) *out_done = false;
    if (!s_active || !s_have_plan || !ctx) return false;

    // (A) Holding a MESSAGE dialog for a read beat: keep it up until the dwell
    // elapses, then dismiss and resume. (Covers a dialog WE opened and one
    // main.c's per-frame pump opened — we adopt either.)
    if (s_pres == AP_PRES_MESSAGE) {
        if (now_s < s_pres_until) return true;          // still reading
        if (dialog_is_active()) dialog_dismiss();
        s_pres = AP_PRES_IDLE;
        return true;
    }

    // (B) Holding a DECISION widget (chest A/B, recruit count, alcove y/n, siege
    // y/n) the previous MOVE raised: dwell so the watcher reads what the plan is
    // about to choose, then run the SINGLE plan_exec_step that applies the
    // pre-decided answer (and, for a fight, animates it via the WS-7 hook).
    if (s_pres == AP_PRES_DECISION) {
        if (now_s < s_pres_until) return true;          // still reading the prompt
        OB_TRACE("pump DECISION exec front=%s prompt_active=%d",
                 player_io_front(ctx->game) ? "set" : "NULL", prompt_is_active());
        PlanExecStatus st = plan_exec_step(&s_exec, ctx->game, ctx->map, ctx->fog,
                                           (Resources *)ctx->res);
        if (prompt_is_active()) prompt_dismiss();        // the answer cleared the flow
        s_pres = AP_PRES_IDLE;
        if (st == PLAN_EXEC_DONE || s_combat_aborted) {
            if (out_done) *out_done = true;
        }
        return true;
    }

    // NOTE: VIEW screens (town/castle/dwelling/alcove) are presented SYNCHRONOUSLY
    // inside the executor via the PlanViewPresenter hook (shell_autoplay_view_
    // presenter) at the moment the view-raising primitive runs — the same pattern
    // as the combat animator. So there is no cross-frame VIEW hold state here.

    // (C) IDLE — normal per-step pacing gate.
    if (s_step_delay > 0.0 && now_s < s_next_step_at) return true;
    s_next_step_at = now_s + s_step_delay;

    // Adopt a dialog main.c's per-frame pump already opened (or surface a queued
    // engine MESSAGE ourselves), and hold it for a read beat.
    if (dialog_is_active() || shell_pump_player_io_message(ctx->game)) {
        OB_TRACE("pump IDLE->MESSAGE front=%s",
                 player_io_front(ctx->game) ? "set" : "NULL");
        s_pres = AP_PRES_MESSAGE;
        s_pres_until = now_s + s_read_dwell;
        return true;
    }

    // A decision the PREVIOUS step's GameStep raised (the engine prompt widget is
    // already up via the shell host callback): dwell it before answering. The
    // answer happens in branch (B) after the dwell — NOT here — so the prompt is
    // visible for a full beat and we never answer twice.
    if (pending_flow != FLOW_NONE) {
        OB_TRACE("pump IDLE->DECISION pending_flow=%d front=%s",
                 (int)pending_flow,
                 player_io_front(ctx->game) ? "set" : "NULL");
        s_pres = AP_PRES_DECISION;
        s_pres_until = now_s + s_read_dwell;
        return true;
    }

    // No prompt/message pending: advance the replay by one primitive (a MOVE, a
    // direct action, or a fight). A MOVE that lands on a chest/gate/foe raises a
    // flow handled by branch (C) next tick; everything else just applies.
    OB_TRACE("pump IDLE exec-step");
    PlanExecStatus st = plan_exec_step(&s_exec, ctx->game, ctx->map, ctx->fog,
                                       (Resources *)ctx->res);
    if (st == PLAN_EXEC_DONE || s_combat_aborted) {
        if (out_done) *out_done = true;   // run finished, or window closed mid-fight
    }
    return true;   // autoplay owns the turn this frame
}

void shell_autoplay_summary(const ShellCtx *ctx, char *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (!s_have_plan || !ctx || !ctx->game) return;
    const char *verdict = autoplay_verdict_str(s_plan.verdict);
    // Objective tally + the planner's verdict. Fixed at plan time; the executor only
    // replays the recording, so this is the run's true outcome.
    snprintf(out, (size_t)cap,
             "Autoplay finished (%s).\n\n%d of %d objectives completed.\n\n"
             "You now have control.",
             verdict, s_obj_done, s_obj_total);
}

void shell_autoplay_end(void) {
    if (s_have_plan) plan_free(&s_plan);
    s_active = false;
    s_have_plan = false;
    s_demo = false;
}
