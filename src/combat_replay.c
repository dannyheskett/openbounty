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
#include "layout.h"

#include "gfx.h"
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
#include "overlay.h"         // overlay_draw_note (victory banner)
#include "shell_promptdispatch.h"   // shell_set_combat_ground

#include <string.h>
#include <stdio.h>

// The presenter (field draw + scale-to-window blit) is combat_loop.c's
// combat_present_public, so the replay animator draws a throwaway Combat
// without duplicating the render path.

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
                   out_tgt->garrison_slots = GAME_ARMY_SLOTS;
                   out_tgt->full_band = CL_IS_MODERN && foe->is_static; }
        return true;
    }
    return false;
}

// Pace one beat: present the current board for ~one beat (0.15s, scaled by
// the driver's step delay so the driver's pace also speeds/slows fights),
// pumping audio + screenshots + window-close each frame. A troop that struck
// in this beat (`atk_side` >= 0, standing at atk_x, atk_y) plays its attack
// strip across the beat, as it does in a fight. Returns false if the window
// was asked to close (abort the run).
static bool replay_beat(const Combat *c, const Game *g, const ShellCtx *ctx,
                        const Sprites *sprites, void *rt, double beat_s,
                        int atk_side, int atk_x, int atk_y, int atk_frames) {
    double start = frame_host_time(), until = start + beat_s;
    do {
        if (frame_host_should_close()) return false;
        audio_tick();
        if (CL_IS_MODERN && atk_side >= 0 && atk_frames > 1 && beat_s > 0.0) {
            int f = (int)((frame_host_time() - start) / beat_s * atk_frames);
            combat_render_set_attack(atk_side, atk_x, atk_y, f < atk_frames ? f : atk_frames - 1);
        }
        combat_present_public(c, g, ctx->map, ctx->fog, sprites, rt);
        screenshot_tick(*(RenderTexture2D *)rt, "shot");
    } while (frame_host_time() < until);
    combat_render_set_attack(-1, 0, 0, -1);
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
            // Modern: whose turn it is, as the turn column shows it in a fight.
            if (CL_IS_MODERN) {
                c->side = e->act_side;
                c->unit_id = au->troop_idx >= 0 ? e->act_slot : -1;
            }
        }
    }
    if (e->tgt_side < COMBAT_SIDES && e->tgt_slot < COMBAT_SLOTS) {
        CombatUnit *tu = &c->units[e->tgt_side][e->tgt_slot];
        if (tu->troop_idx >= 0) {
            tu->count = e->tgt_count_after;
            tu->hit_flash = 3;                          // splat overlay
            // Modern: a troop the blow killed stays under its splat until
            // the splat is done (replay's decay below), as in a fight.
            if (tu->count <= 0 && !CL_IS_MODERN) tu->troop_idx = -1;
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
    // Modern: the ground the fight is on, as RunCombat's caller sets it.
    if (CL_IS_MODERN) shell_set_combat_ground(ctx);

    // Beat length scales off the driver's step delay (demo's beat when demo
    // mode drives, else the visible-autoplay presenter's). step_delay 0 =>
    // beat 0 => one rendered frame per action (full machine frame rate, no
    // wall-clock throttle). A non-zero step_delay slows fights to a watchable
    // cadence proportional to the adventure pacing.
    double beat = shell_demo_active() ? shell_demo_step_delay()
                                      : shell_autoplay_step_delay();
    if (beat < 0.0) beat = 0.0;

    CombatReplayStatus rv = COMBAT_REPLAY_OK;
    combat_render_set_impact(true);
    // Show the opening board for a beat, then animate each recorded action.
    if (!replay_beat(&c, g, ctx, sprites, render_target, beat, -1, 0, 0, 0)) rv = COMBAT_REPLAY_ABORT;
    for (int i = 0; i < rec->count && rv == COMBAT_REPLAY_OK; i++) {
        const CombatTurnEntry *e = &rec->entries[i];
        replay_apply_entry(&c, e);
        // decay hit flashes a touch each beat so splats don't persist forever;
        // a troop killed under its splat leaves when it is done
        for (int s = 0; s < COMBAT_SIDES; s++)
            for (int u = 0; u < COMBAT_SLOTS; u++) {
                CombatUnit *cu = &c.units[s][u];
                if (cu->hit_flash > 0 && !(s == e->tgt_side && u == e->tgt_slot)) cu->hit_flash--;
                if (cu->troop_idx >= 0 && cu->count <= 0 && cu->hit_flash <= 0) cu->troop_idx = -1;
            }
        // A blow: the striker plays its attack strip over the beat.
        int atk_side = -1, frames = 0;
        if (e->tgt_side < COMBAT_SIDES && e->act_side < COMBAT_SIDES && e->act_slot < COMBAT_SLOTS) {
            const CombatUnit *au = &c.units[e->act_side][e->act_slot];
            if (au->troop_idx >= 0 && au->troop_idx < sprites->troop_count) {
                atk_side = e->act_side;
                frames = sprites->troop_anim_frames[au->troop_idx];
            }
        }
        if (!replay_beat(&c, g, ctx, sprites, render_target, beat, atk_side, e->to_x, e->to_y, frames))
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
        char body[400], gbuf[16];
        snprintf(gbuf, sizeof gbuf, "%d", c.spoils[COMBAT_SIDE_AI]);
        const ResBanners *bn = &g->res->banners;
        if (c.target_name[0]) {
            ResTemplateVar vars[] = {
                { "NAME",   g->character.name },
                { "TARGET", c.target_name },
                { "GOLD",   gbuf },
            };
            resources_format_template(body, sizeof body,
                                      bn->combat_victory_named, vars, 3);
        } else {
            ResTemplateVar vars[] = {
                { "NAME", g->character.name },
                { "GOLD", gbuf },
            };
            resources_format_template(body, sizeof body,
                                      bn->combat_victory_unnamed, vars, 2);
        }
        open_dialog_kind(g->res->ui.dt_combat_victory, body, PIO_NOTE_OVER_FIELD);
        double dwell = shell_demo_active() ? shell_demo_read_dwell()
                                           : shell_autoplay_read_dwell();
        if (dwell > 0.0) {
            // Hold the banner; combat_present draws it (overlay_draw_note
            // centered) while dialog_is_active(). replay_beat pumps audio +
            // screenshots + movie frames + window-close each frame.
            if (!replay_beat(&c, g, ctx, sprites, render_target, dwell, -1, 0, 0, 0))
                rv = COMBAT_REPLAY_ABORT;
        }
        dialog_dismiss();
    }

    audio_set_track(AUDIO_TRACK_OPENWORLD);
    return rv;
}
