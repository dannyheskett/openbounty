// src/shell_demo.c
//
// Visible demo mode: pace the live player agent (demo/demo.h) so a viewer can
// follow the run. The agent answers engine prompts itself (player_io_answer),
// so this adapter only paces the ticks, holds engine-raised dialogs/prompts up
// for a read beat, and clears the shell widgets the answers resolve.

#include "shell_demo.h"

#include <stdio.h>

#include "demo.h"
#include "audio.h"          // audio_tick during held beats
#include "combat_replay.h"  // RenderCombatRecord -- the fight animator
#include "frame_host.h"     // frame_host_time / frame_host_should_close
#include "pending.h"     // pending_flow -- a raised prompt worth a read beat
#include "prompt.h"      // prompt_is_active / prompt_dismiss
#include "recorder.h"       // recorder_capture during held beats (--movie)
#include "screenshot.h"     // screenshot_tick during held beats
#include "shell_frame.h"    // shell_present_frame -- render a held screen
#include "ui.h"          // dialog_is_active / dialog_dismiss / message pump
#include "views.h"       // views_active / views_dismiss / town cursor walk

// Watchable pacing, code-fixed like the rest of the shell's pacing knobs:
// a walking cadence between actions, a longer beat on prompts and dialogs.
#define SHELL_DEMO_STEP_DELAY 0.12
#define SHELL_DEMO_READ_DWELL 1.10

static bool   s_active = false;
static bool   s_finished = false;
static double s_next_at = 0.0;
static double s_hold_until = 0.0;
static bool   s_holding = false;
static ShellCtx *s_ctx = NULL;   // valid only inside shell_demo_tick

bool shell_demo_active(void) { return s_active; }
double shell_demo_step_delay(void) { return SHELL_DEMO_STEP_DELAY; }
double shell_demo_read_dwell(void) { return SHELL_DEMO_READ_DWELL; }

// Present the current frame for `beat_s` seconds (audio + screenshots keep
// ticking). False when the window was asked to close.
static bool demo_beat(double beat_s) {
    if (!s_ctx) return true;
    double until = frame_host_time() + beat_s;
    do {
        if (frame_host_should_close()) return false;
        audio_tick();
        shell_present_frame(s_ctx->game, s_ctx->map, s_ctx->fog,
                            s_ctx->sprites, s_ctx->render_target);
        screenshot_tick(*(RenderTexture2D *)s_ctx->render_target, "shot");
    } while (frame_host_time() < until);
    return true;
}

// DemoCombatAnimator: draw the recorded fight the decision predicted.
static void demo_animate_combat(void *ctx, CombatMode mode,
                                const CombatTurnRecord *rec) {
    (void)ctx;
    if (!s_ctx || !rec) return;
    RenderCombatRecord(s_ctx, mode, rec, s_ctx->sprites,
                       s_ctx->render_target);
}

// DemoTownPresenter: put the town screen up and walk the menu cursor to the
// row about to be transacted, so the viewer sees the purchase happen.
static void demo_present_town(void *ctx, DemoTownAction action) {
    (void)ctx;
    if (!s_ctx) return;
    int row;
    switch (action) {
    case DEMO_TOWN_CONTRACT: row = TOWN_ROW_CONTRACT; break;
    case DEMO_TOWN_BOAT:     row = TOWN_ROW_BOAT;     break;
    case DEMO_TOWN_SIEGE:    row = TOWN_ROW_SIEGE;    break;
    case DEMO_TOWN_SPELL:    row = TOWN_ROW_SPELL;    break;
    default:                 return;
    }
    if (views_active() != VIEW_TOWN) views_set(VIEW_TOWN);
    if (!demo_beat(SHELL_DEMO_READ_DWELL)) return;
    for (;;) {
        bool done = views_town_demo_step_cursor(row);
        if (!demo_beat(SHELL_DEMO_STEP_DELAY * 2)) return;
        if (done) break;
    }
    demo_beat(SHELL_DEMO_READ_DWELL);
    views_dismiss();
}

bool shell_demo_begin(ShellCtx *ctx) {
    s_active = false;
    s_finished = false;
    if (!ctx || !ctx->game || !ctx->map || !ctx->fog || !ctx->res) return false;
    if (!demo_begin(ctx->game, ctx->map, ctx->fog, ctx->res)) return false;
    demo_set_hooks(demo_animate_combat, demo_present_town, NULL);
    s_active = true;
    s_next_at = 0.0;
    s_hold_until = 0.0;
    s_holding = false;
    return true;
}

bool shell_demo_tick(ShellCtx *ctx, double now_s, bool *out_done) {
    if (out_done) *out_done = false;
    if (!s_active || !ctx) return false;
    s_ctx = ctx;   // hooks fired from demo_tick render through this frame's ctx

    // Hold whatever the engine put on screen (a dialog the pump opened, a
    // screen the view pump pushed, or a prompt a step raised) for a read beat
    // before the agent acts on it / it is dismissed.
    if (s_holding) {
        if (now_s < s_hold_until) return true;
        s_holding = false;
        if (dialog_is_active()) { dialog_dismiss(); return true; }
        if (views_active() != VIEW_NONE) { views_dismiss(); return true; }
    } else if (dialog_is_active() || views_active() != VIEW_NONE ||
               pending_flow != FLOW_NONE) {
        s_holding = true;
        s_hold_until = now_s + SHELL_DEMO_READ_DWELL;
        return true;
    }

    if (now_s < s_next_at) return true;
    s_next_at = now_s + SHELL_DEMO_STEP_DELAY;

    if (!s_finished && !demo_tick(ctx->game, ctx->map, ctx->fog, ctx->res)) {
        s_finished = true;
        if (out_done) *out_done = true;
    }
    // The agent's answer resolved the flow; drop the stale prompt widget.
    if (pending_flow == FLOW_NONE && prompt_is_active()) prompt_dismiss();
    return true;
}

void shell_demo_summary(const ShellCtx *ctx, char *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (!ctx || !ctx->game) return;
    DemoResult r;
    demo_result(ctx->game, &r);
    snprintf(out, (size_t)cap,
             "Demo %s.\n\nScore %d, %d villains, %d artifacts, %d castles,\n"
             "%d days used, %d digs.\n\nYou now have control.",
             r.won ? "WON the scepter" : (r.out_of_days ? "ran out of days"
                                                        : "ran out of ideas"),
             r.score, r.villains, r.artifacts, r.castles, r.days_used,
             r.searches);
}

void shell_demo_end(void) {
    demo_set_hooks(NULL, NULL, NULL);
    demo_end();
    s_active = false;
    s_finished = false;
    s_ctx = NULL;
}
