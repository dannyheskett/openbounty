// src/combat_replay.c -- visible-mode combat animator.
//
// Draws an agent-resolved fight at game pace by ANIMATING the per-turn CombatTurnRecord
// the single authoritative resolution produced. It is a RENDERER over recorded
// data, never a second resolution: it builds a stack-local THROWAWAY Combat to
// the fight's start, then walks the record applying each entry as a state
// transition and presenting it on a paced beat. It makes NO combat_ai_action /
// policy call, NO RNG roll, and NEVER touches the live Game -- the executor
// applies the authoritative outcome separately (mode parity).
//
// The executor calls this BEFORE it re-runs the deterministic resolution to apply
// the outcome, so the live Game is still PRE-FIGHT here: the hero's army and the
// pending flow (pending_castle_id / pending_foe_id) are intact, which is exactly
// what combat_init + combat_prepare_* need to reconstruct the identical start
// board the resolution used.

#include "combat_replay.h"

#include "raylib.h"
#include "combat.h"
#include "combat_loop.h"     // (shares combat_present indirectly via this TU)
#include "combat_render.h"
#include "frame_host.h"
#include "audio.h"
#include "screenshot.h"
#include "game.h"
#include "pending.h"
#include "resources.h"
#include "tables.h"
#include "shell_demo.h"  // ShellCtx + the demo pacing source
#include "shell_autoplay.h"  // the visible-autoplay pacing source
#include "ui.h"              // open_dialog / dialog_is_active / dialog_dismiss
#include "prompt.h"          // prompt_is_active / prompt_dismiss (clear pre-fight)
#include "overlay.h"         // overlay_draw_dialog_centered (victory banner)

#include <string.h>
#include <stdio.h>

// The presenter (field draw + scale-to-window blit) is defined by
// combat_loop.c and exposed publicly so the replay animator can draw a
// throwaway Combat without duplicating the render path.
void combat_present_public(const Combat *c, const Game *g,
                           const Sprites *sprites, void *render_target);

// Build the CombatTarget for the pending flow on the live (pre-fight) game --
// the same target the headless resolution builds from the same pending flow.
// Returns false if there is no pending combat flow.
static bool replay_build_target(Game *g, CombatMode *out_mode,
                                CombatTarget *out_tgt) {
    memset(out_tgt, 0, sizeof *out_tgt);
    if (pending_flow == FLOW_SIEGE_MONSTER || pending_flow == FLOW_SIEGE_VILLAIN) {
        *out_mode = COMBAT_MODE_CASTLE;
        CastleRecord *cr = GameFindCastle(g, pending_castle_id);
        const ResCastle *rc = resources_castle_by_id(g->res, pending_castle_id);
        const VillainDef *v = (cr && cr->villain_id[0])
                                ? villain_by_id(cr->villain_id) : NULL;
        out_tgt->name = v && v->name[0] ? v->name
                      : (rc && rc->name[0] ? rc->name : pending_castle_id);
        out_tgt->seed_key = pending_castle_id;
        if (cr) { out_tgt->garrison = cr->garrison;
                  out_tgt->garrison_slots = GAME_ARMY_SLOTS; }
        return true;
    }
    if (pending_flow == FLOW_ATTACK_FOE) {
        *out_mode = COMBAT_MODE_FOE;
        FoeState *foe = pending_foe_id[0] ? GameFindFoe(g, pending_foe_id) : NULL;
        out_tgt->name = "Hostile band";
        out_tgt->seed_key = pending_foe_id;
        if (foe) { out_tgt->garrison = foe->garrison;
                   out_tgt->garrison_slots = GAME_ARMY_SLOTS; }
        return true;
    }
    return false;
}

// Pace one beat: present the current board for ~one beat (0.15s, scaled by
// the driver's step delay so the driver's pace also speeds/slows fights),
// pumping audio + screenshots + window-close each frame. Returns false if the
// window was asked to close (abort the run).
static bool replay_beat(const Combat *c, const Game *g, const Sprites *sprites,
                        void *rt, double beat_s) {
    double until = frame_host_time() + beat_s;
    do {
        if (frame_host_should_close()) return false;
        audio_tick();
        combat_present_public(c, g, sprites, rt);
        screenshot_tick(*(RenderTexture2D *)rt, "shot");
    } while (frame_host_time() < until);
    return true;
}

// Apply one recorded entry to the throwaway Combat as a STATE TRANSITION (no
// engine combat logic): move the acting unit, set counts, flash the target.
static void replay_apply_entry(Combat *c, const CombatTurnEntry *e) {
    if (e->act_side < COMBAT_SIDES && e->act_slot < COMBAT_SLOTS) {
        CombatUnit *au = &c->units[e->act_side][e->act_slot];
        if (au->troop_idx >= 0) {
            au->x = e->to_x; au->y = e->to_y;
            au->count = e->act_count_after;
            if (au->count <= 0) au->troop_idx = -1;   // acting stack wiped
        }
    }
    if (e->tgt_side < COMBAT_SIDES && e->tgt_slot < COMBAT_SLOTS) {
        CombatUnit *tu = &c->units[e->tgt_side][e->tgt_slot];
        if (tu->troop_idx >= 0) {
            tu->count = e->tgt_count_after;
            tu->hit_flash = 3;                          // splat overlay
            if (tu->count <= 0) tu->troop_idx = -1;     // target stack wiped
        }
    }
    if (e->log_line[0]) combat_log(c, "%s", e->log_line);
}

CombatReplayStatus RenderCombatRecord(void *shell_ctx, CombatMode mode,
                                      const CombatTurnRecord *rec,
                                      const Sprites *sprites,
                                      void *render_target) {
    ShellCtx *ctx = (ShellCtx *)shell_ctx;
    if (!ctx || !ctx->game || !rec || !render_target) return COMBAT_REPLAY_OK;
    Game *g = ctx->game;

    CombatMode tmode; CombatTarget tgt;
    if (!replay_build_target(g, &tmode, &tgt)) return COMBAT_REPLAY_OK;
    (void)mode;   // the live pending flow is authoritative for the target

    // Clear any adventure-mode modal still up from the step that triggered this
    // fight (the "Lay Siege" y/n prompt, a chest/encounter message, etc.).
    // combat_present() deliberately draws dialog_is_active()/prompt overlays so
    // the post-fight Victory banner shows; without this clear, that same code
    // path paints the leftover adventure dialog on top of the combat board.
    if (prompt_is_active()) prompt_dismiss();
    if (dialog_is_active()) dialog_dismiss();

    // Build the throwaway start board exactly as RunCombat does -- deterministic,
    // engine-pure, reads g read-only. We never roll the RNG (we replay deltas),
    // so combat_seed_rng here only matches the visual start, not the resolution.
    Combat c;
    combat_init(&c, g, tmode, &tgt);
    combat_seed_rng(&c, g, tmode, &tgt);
    combat_prepare_player(&c, g);
    if (tmode == COMBAT_MODE_CASTLE) combat_prepare_castle(&c, &tgt);
    else                            combat_prepare_foe(&c, &tgt);
    combat_reset_match(&c);

    audio_set_track(AUDIO_TRACK_COMBAT);

    // Beat length scales off the driver's step delay (demo's beat when demo
    // mode drives, else the visible-autoplay presenter's). step_delay 0 =>
    // beat 0 => one rendered frame per action (full machine frame rate, no
    // wall-clock throttle). A non-zero step_delay slows fights to a watchable
    // cadence proportional to the adventure pacing.
    double beat = shell_demo_active() ? shell_demo_step_delay()
                                      : shell_autoplay_step_delay();
    if (beat < 0.0) beat = 0.0;

    CombatReplayStatus rv = COMBAT_REPLAY_OK;
    // Show the opening board for a beat, then animate each recorded action.
    if (!replay_beat(&c, g, sprites, render_target, beat)) rv = COMBAT_REPLAY_ABORT;
    for (int i = 0; i < rec->count && rv == COMBAT_REPLAY_OK; i++) {
        replay_apply_entry(&c, &rec->entries[i]);
        // decay hit flashes a touch each beat so splats don't persist forever
        for (int s = 0; s < COMBAT_SIDES; s++)
            for (int u = 0; u < COMBAT_SLOTS; u++)
                if (c.units[s][u].hit_flash > 0 &&
                    !(s == rec->entries[i].tgt_side && u == rec->entries[i].tgt_slot))
                    c.units[s][u].hit_flash--;
        if (!replay_beat(&c, g, sprites, render_target, beat))
            rv = COMBAT_REPLAY_ABORT;
    }

    // VICTORY banner -- match the interactive RunCombat win screen so a paced
    // (demo) replay does not skip the post-fight screen. rec->result mirrors
    // Combat.result (1 = win); c.spoils[AI] is the full enemy-garrison worth,
    // already summed by combat_prepare_* at init (the same value RunCombat
    // awards). Held for the read-dwell beat, then dismissed (the driving agent
    // has no human keypress to ack it). Full-speed test replay has a
    // zero dwell, so this is a no-op there. Loss draws nothing (RunCombat is
    // silent on defeat too -- the disgrace flow is the caller's).
    if (rv == COMBAT_REPLAY_OK && rec->result == 1) {
        char body[400];
        const char *who = (g->character.name[0]) ? g->character.name : "warrior";
        if (c.target_name[0]) {
            snprintf(body, sizeof body,
                     "Well done %s, you have\n"
                     "successfully vanquished\n"
                     "%s.\n\n"
                     "Spoils of War: %d gold",
                     who, c.target_name, c.spoils[COMBAT_SIDE_AI]);
        } else {
            snprintf(body, sizeof body,
                     "Well done %s, you have\n"
                     "successfully vanquished\n"
                     "yet another foe.\n\n"
                     "Spoils of War: %d gold",
                     who, c.spoils[COMBAT_SIDE_AI]);
        }
        open_dialog("Victory!", body);
        double dwell = shell_demo_active() ? shell_demo_read_dwell()
                                           : shell_autoplay_read_dwell();
        if (dwell > 0.0) {
            // Hold the banner; combat_present draws it (overlay_draw_dialog_
            // centered) while dialog_is_active(). replay_beat pumps audio +
            // screenshots + movie frames + window-close each frame.
            if (!replay_beat(&c, g, sprites, render_target, dwell))
                rv = COMBAT_REPLAY_ABORT;
        }
        dialog_dismiss();
    }

    audio_set_track(AUDIO_TRACK_OPENWORLD);
    return rv;
}
