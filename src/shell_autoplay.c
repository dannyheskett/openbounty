// src/shell_autoplay.c -- visible autoplay (AP-024). See shell_autoplay.h.

#include "shell_autoplay.h"

#include <stdio.h>
#include <string.h>

#include "autoplay.h"
#include "exec.h"           // ExecCtx (autoplay's execution context)
#include "recording.h"

#include "raylib.h"
#include "audio.h"
#include "bfont.h"
#include "combat_replay.h"
#include "frame_host.h"
#include "layout.h"
#include "present.h"        // CL_SCREEN_W/H
#include "palette.h"       // PAL[] -- the pack's canonical colors
#include "pending.h"
#include "prompt.h"
#include "screenshot.h"
#include "shell_frame.h"
#include "ui.h"
#include "views.h"

// Per-speed pacing: (seconds between prims, seconds to hold a screen). All
// three show every move / town / combat; only the pace differs. FAST uses a
// zero step delay, so it advances one prim per rendered frame -- the quickest
// pace that still animates every step -- with brief but visible read dwells.
#define SHELL_AUTOPLAY_STEP_DELAY 0.06
#define SHELL_AUTOPLAY_READ_DWELL 0.90

static bool          s_active = false;
static int           s_next_prim = 0;
static double        s_next_at = 0.0;
static double        s_hold_until = 0.0;
static bool          s_holding = false;
static bool          s_cancelled = false;   // resolve stopped by ESC/close
static AutoplaySpeed s_speed = AUTOPLAY_SPEED_NORMAL;
static AutoplayResult s_result;

bool shell_autoplay_cancelled(void) { return s_cancelled; }

// The "Autoplay Processing" screen shown while the oracle resolves headlessly
// (the window would otherwise be blank for the whole resolve). Drawn in the
// pack's own palette + bitmap font. The progress bar tracks committed/total
// objectives -- the same count the search reports.
static void draw_processing(ShellCtx *ctx, int done, int total) {
    RenderTexture2D *target = (RenderTexture2D *)ctx->render_target;
    const int W = CL_SCREEN_W, H = CL_SCREEN_H;
    BeginTextureMode(*target);
    ClearBackground(BLACK);
    // A centered KB-style panel: blue field, yellow border.
    int pw = 240, ph = 90, px = (W - pw) / 2, py = (H - ph) / 2;
    DrawRectangle(px - 2, py - 2, pw + 4, ph + 4, PAL[14]);   // yellow border
    DrawRectangle(px, py, pw, ph, PAL[1]);                    // blue field
    bfont_draw_centered("AUTOPLAY PROCESSING", W / 2, py + 12, PAL[15]);
    // Progress bar.
    int bw = pw - 40, bh = 12, bx = (W - bw) / 2, by = py + 40;
    int fw = (total > 0) ? (bw * done) / total : 0;
    if (fw < 0) fw = 0;
    if (fw > bw) fw = bw;
    DrawRectangle(bx - 1, by - 1, bw + 2, bh + 2, PAL[15]);   // white frame
    DrawRectangle(bx, by, bw, bh, PAL[8]);                    // dark track
    DrawRectangle(bx, by, fw, bh, PAL[10]);                   // green fill
    char buf[64];
    snprintf(buf, sizeof buf, "%d / %d objectives", done, total);
    bfont_draw_centered(buf, W / 2, by + bh + 8, PAL[15]);
    EndTextureMode();

    // Scale + letterbox blit (same pattern as shell_present_frame).
    present_scaled(*target);
    frame_host_end_frame();   // polls window events (close / keys) for the cancel check
}

// Progress hook (autoplay.h): draw a frame, then report whether to continue.
// Returns false -- cancelling the resolve -- on window-close or ESC.
static bool ap_progress_cb(int done, int total, void *ud) {
    ShellCtx *ctx = (ShellCtx *)ud;
    if (ctx) draw_processing(ctx, done, total);
    if (frame_host_should_close() || IsKeyPressed(KEY_ESCAPE)) return false;
    return true;
}

static double speed_step_delay(void) {
    switch (s_speed) {
    case AUTOPLAY_SPEED_SLOW: return 0.15;
    case AUTOPLAY_SPEED_FAST: return 0.0;   // one prim per frame (~60/sec)
    default:                  return SHELL_AUTOPLAY_STEP_DELAY;   // 0.06 (~16/sec)
    }
}
static double speed_read_dwell(void) {
    switch (s_speed) {
    case AUTOPLAY_SPEED_SLOW: return 1.8;
    case AUTOPLAY_SPEED_FAST: return 0.3;
    default:                  return SHELL_AUTOPLAY_READ_DWELL;   // 0.90
    }
}

bool   shell_autoplay_active(void) { return s_active; }
double shell_autoplay_step_delay(void) { return speed_step_delay(); }
double shell_autoplay_read_dwell(void) { return speed_read_dwell(); }

bool shell_autoplay_begin(ShellCtx *ctx, int seed_index, const char *pack_dir,
                          const char *hero_id, int difficulty,
                          AutoplaySpeed speed) {
    s_active = false;
    s_next_prim = 0;
    s_cancelled = false;
    s_speed = speed;
    if (!ctx || !ctx->game) return false;
    AutoplayConfig cfg = { seed_index, pack_dir, hero_id, difficulty };
    // Show a first "Processing" frame before the resolve (the search's hook
    // then updates it), so the window is never blank. The hook also lets ESC /
    // window-close cancel the resolve.
    draw_processing(ctx, 0, 0);
    autoplay_set_progress(ap_progress_cb, ctx);
    bool ok = autoplay_run(&cfg, &s_result);
    autoplay_set_progress(NULL, NULL);
    if (s_result.cancelled) { s_cancelled = true; return false; }
    if (!ok) return false;
    fprintf(stdout, "--autoplay: resolved %d/%d (%s); replaying visibly\n",
            s_result.best_done, s_result.obj_total,
            autoplay_verdict_str(&s_result));
    // Snap to a 100% clean presentation + engine-flow state before the first
    // prim: the game boot and the headless resolve share process-global UI
    // state (the player-io queue, the view stack, dialog/prompt widgets, the
    // pending flow), and any of it left set would bleed a stale screen into
    // the replay. The replay re-raises everything it needs, so clearing here
    // is safe and authoritative.
    player_io_reset(ctx->game);      // drop every queued message/view/decision
    pending_reset();                 // engine pending-flow scratch
    views_set(VIEW_NONE);            // empty the shell view stack
    if (dialog_is_active()) dialog_dismiss();
    if (prompt_is_active()) prompt_dismiss();
    s_active = true;
    s_next_prim = 0;
    s_next_at = 0.0;
    s_holding = false;
    s_hold_until = 0.0;
    return true;
}

// Pre-animate a combat-bearing answer: re-run the identical resolution on a
// throwaway copy to produce the CombatTurnRecord, draw it, then let the
// applier run the authoritative resolution on the live world.
static void animate_pending_combat(ShellCtx *ctx) {
    Game *g = ctx->game;
    if (pending_flow != FLOW_SIEGE_MONSTER &&
        pending_flow != FLOW_SIEGE_VILLAIN &&
        pending_flow != FLOW_ATTACK_FOE)
        return;
    static Game tmp;
    static CombatTurnEntry entries[COMBAT_MAX_ROUNDS * 8];
    tmp = *g;
    CombatMode mode = (pending_flow == FLOW_ATTACK_FOE) ? COMBAT_MODE_FOE
                                                        : COMBAT_MODE_CASTLE;
    // Re-resolve on the copy with recording (pure fn of seed+identity+mode).
    uint64_t rng = GameRngSnapshot();
    CombatTurnRecord rec;
    memset(&rec, 0, sizeof rec);
    rec.entries = entries;
    rec.cap = (int)(sizeof entries / sizeof entries[0]);
    // Build the target from the live pending flow on the COPY.
    {
        CombatTarget tgt;
        memset(&tgt, 0, sizeof tgt);
        if (mode == COMBAT_MODE_CASTLE) {
            CastleRecord *cr = GameFindCastle(&tmp, pending_castle_id);
            tgt.name = pending_castle_id;
            tgt.seed_key = pending_castle_id;
            if (cr) {
                tgt.garrison = cr->garrison;
                tgt.garrison_slots = GAME_ARMY_SLOTS;
            }
        } else {
            FoeState *foe = pending_foe_id[0] ? GameFindFoe(&tmp,
                                                            pending_foe_id)
                                              : NULL;
            tgt.name = "Hostile band";
            tgt.seed_key = pending_foe_id;
            if (foe) {
                tgt.garrison = foe->garrison;
                tgt.garrison_slots = GAME_ARMY_SLOTS;
            }
        }
        combat_run_headless_rec(&tmp, mode, &tgt, COMBAT_MAX_ROUNDS, autoplay_combat_policy,
                                NULL, &rec);
    }
    GameRngRestore(rng);
    RenderCombatRecord(ctx, mode, &rec, ctx->sprites, ctx->render_target);
}

// Apply exactly one recorded primitive and consume the engine's raised
// views/messages the host way. Returns false when the recording is exhausted.
static bool apply_one_prim(ShellCtx *ctx) {
    RecSink *sink = recsink();
    if (s_next_prim >= sink->count) return false;
    const RecPrim *p = &sink->prims[s_next_prim++];
    struct ExecCtx ectx = { ctx->game, ctx->map, ctx->fog, ctx->res };
    if (p->kind == REC_ANSWER && p->ans_kind == FLOW_ANS_YES &&
        p->outcome != 0)
        animate_pending_combat(ctx);   // combat cartoon, shown at every speed
    plan_exec_step(&ectx, p);
    // The applier is presentation-neutral: it leaves the engine's raised
    // view/message requests in the queue for the HOST to consume. Present
    // views through the same dispatcher normal play uses (a replayed town
    // entry opens the town screen exactly like a walked one); the
    // hold-then-dismiss cycle then reads and clears them.
    while (shell_pump_player_io_view(ctx->game)) {}
    shell_pump_player_io_message(ctx->game);
    if (pending_flow == FLOW_NONE && prompt_is_active()) prompt_dismiss();
    return true;
}

bool shell_autoplay_tick(ShellCtx *ctx, double now_s, bool *out_done) {
    if (out_done) *out_done = false;
    if (!s_active || !ctx) return false;

    // Hold-then-dismiss (the same lifecycle demo mode uses): a replay has no
    // human to press ESC, so a screen the previous prim opened must be held
    // for a read beat and then DISMISSED here -- otherwise pushed views
    // (dwelling / castle / alcove) pile up on the stack and bleed through the
    // overworld. The engine raises; the host both shows AND clears.
    if (s_holding) {
        if (now_s < s_hold_until) return true;
        s_holding = false;
        if (dialog_is_active()) { dialog_dismiss(); return true; }
        if (views_active() != VIEW_NONE) { views_dismiss(); return true; }
    } else if (dialog_is_active() || views_active() != VIEW_NONE) {
        s_holding = true;
        s_hold_until = now_s + speed_read_dwell();
        return true;
    }

    if (now_s < s_next_at) return true;
    s_next_at = now_s + speed_step_delay();

    if (!apply_one_prim(ctx)) {
        if (out_done) *out_done = true;
    }
    return true;
}

void shell_autoplay_summary(const ShellCtx *ctx, char *out, int cap) {
    (void)ctx;
    if (!out || cap <= 0) return;
    snprintf(out, (size_t)cap,
             "Autoplay %s: %d/%d objectives.\n\nScore %d, %d days used, "
             "%d moves.\n\nYou now have control.",
             autoplay_verdict_str(&s_result), s_result.best_done,
             s_result.obj_total, s_result.score, s_result.days_used,
             s_result.moves);
}

void shell_autoplay_end(void) {
    s_active = false;
    recsink_free();
}
