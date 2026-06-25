// src/combat_loop.c — shell-side rendered combat loop.
//
// RunCombat plus the modal player-input helpers (target picker,
// spell selection, action dispatch) and the per-frame present/anim
// helpers. Engine-side combat state, AI, and damage formula live in
// engine/combat.c.

#include "frame_host.h"
#include "input_host.h"
#include "combat.h"
#include "combat_loop.h"
#include "combat_render.h"
#include "tables.h"
#include "resources.h"
#include "ui.h"
#include "raylib.h"
#include "recorder.h"
#include "audio.h"
#include "bfont.h"
#include "layout.h"
#include "chrome.h"
#include "palette.h"
#include "overlay.h"
#include "prompt.h"
#include "screenshot.h"
#include "views.h"
#include <stdio.h>
#include <string.h>

// COMBAT_SPELL_* indices + spell helpers (spell_damage, combat_cast_spell,
// combat_spell_target_filter) are defined in engine combat.h / combat.c.

// Single-frame target-picker step. The outer combat loop calls this
// once per frame while c->picker_active is set. Reads at most one
// arrow (cursor move) or one confirm/cancel input. The render +
// audio + input-poll for THIS frame are handled by the outer loop;
// this function only consumes input that's already been polled.
bool combat_pick_step(Combat *c, const Game *g, const Sprites *sprites,
                      void *render_target,
                      int *out_x, int *out_y, bool *out_cancelled) {
    (void)g; (void)sprites; (void)render_target;
    if (out_cancelled) *out_cancelled = false;

    int dx = 0, dy = 0;
    if      (input_key_pressed(KEY_UP)    || input_key_pressed(KEY_KP_8)) dy = -1;
    else if (input_key_pressed(KEY_DOWN)  || input_key_pressed(KEY_KP_2)) dy =  1;
    if      (input_key_pressed(KEY_LEFT)  || input_key_pressed(KEY_KP_4)) dx = -1;
    else if (input_key_pressed(KEY_RIGHT) || input_key_pressed(KEY_KP_6)) dx =  1;
    if      (input_key_pressed(KEY_KP_7) || input_key_pressed(KEY_HOME))      { dx = -1; dy = -1; }
    else if (input_key_pressed(KEY_KP_9) || input_key_pressed(KEY_PAGE_UP))   { dx =  1; dy = -1; }
    else if (input_key_pressed(KEY_KP_1) || input_key_pressed(KEY_END))       { dx = -1; dy =  1; }
    else if (input_key_pressed(KEY_KP_3) || input_key_pressed(KEY_PAGE_DOWN)) { dx =  1; dy =  1; }
    if (dx || dy) {
        int nx = c->cursor_x + dx, ny = c->cursor_y + dy;
        if (combat_in_bounds(nx, ny)) {
            c->cursor_x = nx;
            c->cursor_y = ny;
        }
        return false;
    }

    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
        input_key_pressed(KEY_SPACE) ||
        input_key_pressed(KEY_A)     || input_key_pressed(KEY_C)) {
        if (combat_cell_passes_filter(c, c->cursor_x, c->cursor_y,
                                       c->side, c->pick_filter)) {
            if (out_x) *out_x = c->cursor_x;
            if (out_y) *out_y = c->cursor_y;
            return true;
        }
        // Filter rejected -- leave cursor for another step.
    }
    if (input_key_pressed(KEY_ESCAPE)) {
        if (out_cancelled) *out_cancelled = true;
    }
    return false;
}

// ----- Input + action dispatch -----------------------------------------------

static bool combat_read_dir(int *dx, int *dy) {
    *dx = 0; *dy = 0;
    if (input_key_pressed(KEY_KP_5)) return false;
    if (input_key_pressed(KEY_UP)        || input_key_pressed(KEY_KP_8)) { *dy = -1; return true; }
    if (input_key_pressed(KEY_DOWN)      || input_key_pressed(KEY_KP_2)) { *dy =  1; return true; }
    if (input_key_pressed(KEY_LEFT)      || input_key_pressed(KEY_KP_4)) { *dx = -1; return true; }
    if (input_key_pressed(KEY_RIGHT)     || input_key_pressed(KEY_KP_6)) { *dx =  1; return true; }
    if (input_key_pressed(KEY_HOME)      || input_key_pressed(KEY_KP_7)) { *dx = -1; *dy = -1; return true; }
    if (input_key_pressed(KEY_PAGE_UP)   || input_key_pressed(KEY_KP_9)) { *dx =  1; *dy = -1; return true; }
    if (input_key_pressed(KEY_END)       || input_key_pressed(KEY_KP_1)) { *dx = -1; *dy =  1; return true; }
    if (input_key_pressed(KEY_PAGE_DOWN) || input_key_pressed(KEY_KP_3)) { *dx =  1; *dy =  1; return true; }
    return false;
}

// One-frame cast workflow step. Dispatches on c->cast_phase:
//   PICK_SPELL  : draw the spell menu and read one A..G letter.
//                 Validates it's an owned, in-bounds spell; sets up
//                 the picker for the spell's target filter.
//   PICK_TARGET : feed one input into combat_pick_step. On confirm,
//                 capture the target into c->pick_t1_*. Spells that
//                 take two targets (teleport) advance the
//                 pick_reason and stay in PICK_TARGET.
//   APPLY       : invoke the spell-effect function with the captured
//                 target. Decrement spells.counts[idx], increment
//                 spells_this_round, log the spell name.
// Returns 1 when the casting unit's turn is consumed (effect applied
// successfully). 0 when still mid-cast or cancelled/no-effect.
// Resets c->cast_phase = NONE on success, cancel, and no-effect.
int combat_cast_step(Combat *c, Game *g, const Sprites *sprites,
                     void *render_target) {
    (void)sprites; (void)render_target;
    const ResCombatLog *cl_pre = combat_log_strings(c);
    Game *gw = c->heroes[c->side];
    if (!gw) {
        c->cast_phase = COMBAT_CAST_NONE;
        c->picker_active = false;
        return 0;
    }
    if (c->cast_phase == COMBAT_CAST_PICK_SPELL) {
        if (input_key_pressed(KEY_ESCAPE)) {
            c->cast_phase = COMBAT_CAST_NONE;
            return 0;
        }
        int picked = -1;
        for (int i = 0; i < 7; i++) {
            if (input_key_pressed(KEY_A + i)) { picked = i; break; }
        }
        if (picked < 0) return 0;
        if (gw->spells.counts[picked] <= 0) {
            combat_log_template(c,
                cl_pre ? cl_pre->no_spell_type : "No spells of that type",
                NULL, 0);
            c->cast_phase = COMBAT_CAST_NONE;
            return 0;
        }
        c->cast_spell_idx = picked;
        // Set up the picker for this spell's target filter — shared with the
        // engine cast dispatcher / autoplay policy so shell + autoplay
        // agree on legal targets from one source.
        int filter = combat_spell_target_filter(picked);
        CombatPickReason reason = COMBAT_PICK_REASON_SPELL_TARGET;
        c->pick_filter   = filter;
        c->pick_reason   = reason;
        c->picker_active = true;
        if (c->unit_id >= 0) {
            c->cursor_x = c->units[c->side][c->unit_id].x;
            c->cursor_y = c->units[c->side][c->unit_id].y;
        }
        c->cast_phase = COMBAT_CAST_PICK_TARGET;
        return 0;
    }
    if (c->cast_phase == COMBAT_CAST_PICK_TARGET) {
        int tx = 0, ty = 0;
        bool cancelled = false;
        if (!combat_pick_step(c, g, sprites, render_target,
                               &tx, &ty, &cancelled)) {
            if (cancelled) {
                c->cast_phase   = COMBAT_CAST_NONE;
                c->pick_reason  = COMBAT_PICK_REASON_NONE;
                c->picker_active = false;
                return 0;
            }
            return 0;
        }
        // Picker resolved a valid cell. Decide whether this was the
        // first target or (for teleport) the destination pick.
        if (c->cast_spell_idx == COMBAT_SPELL_TELEPORT &&
            c->pick_reason == COMBAT_PICK_REASON_SPELL_TARGET) {
            // First pick: remember the unit; second pick is the
            // destination cell.
            unsigned char uid = c->umap[ty][tx];
            c->pick_t1_x    = tx;
            c->pick_t1_y    = ty;
            c->pick_t1_side = (uid - 1) / COMBAT_SLOTS;
            c->pick_t1_slot = (uid - 1) % COMBAT_SLOTS;
            c->pick_filter  = PICK_FILTER_EMPTY;
            c->pick_reason  = COMBAT_PICK_REASON_TELEPORT_DEST;
            // Reset cursor to caster for the destination pick.
            if (c->unit_id >= 0) {
                c->cursor_x = c->units[c->side][c->unit_id].x;
                c->cursor_y = c->units[c->side][c->unit_id].y;
            }
            return 0;
        }
        // Single-target spell, or teleport's destination: capture
        // and advance to APPLY.
        if (c->pick_reason == COMBAT_PICK_REASON_TELEPORT_DEST) {
            c->cast_dest_x = tx;
            c->cast_dest_y = ty;
        } else {
            unsigned char uid = c->umap[ty][tx];
            c->pick_t1_x    = tx;
            c->pick_t1_y    = ty;
            c->pick_t1_side = (uid - 1) / COMBAT_SLOTS;
            c->pick_t1_slot = (uid - 1) % COMBAT_SLOTS;
        }
        c->picker_active = false;
        c->pick_reason   = COMBAT_PICK_REASON_NONE;
        c->cast_phase    = COMBAT_CAST_APPLY;
        // Fall through to APPLY this same frame.
    }
    if (c->cast_phase == COMBAT_CAST_APPLY) {
        // The index->effect dispatch now lives in the engine (combat_cast_spell)
        // — shared with the autoplay casting policy so the spell dynamics
        // exist in exactly one place. The shell still owns the UI phases above
        // (PICK_SPELL / PICK_TARGET) that populated cast_spell_idx + the target;
        // here it just applies and clears its UI phase. Behavior is identical to
        // the old inline switch: same effects, logs, charge decrement, and the
        // spells_this_round latch.
        int r = combat_cast_spell(c, c->side, c->cast_spell_idx,
                                  c->pick_t1_side, c->pick_t1_slot,
                                  c->cast_dest_x, c->cast_dest_y);
        c->cast_phase = COMBAT_CAST_NONE;
        return (r == COMBAT_CAST_OK) ? 1 : 0;
    }
    return 0;
}

static int combat_player_action_full(Combat *c, const Game *g,
                                     const Sprites *sprites,
                                     RenderTexture2D *target) {
    (void)sprites; (void)target;
    int dx, dy;
    if (combat_read_dir(&dx, &dy)) {
        if (c->unit_id < 0) return 0;
        return combat_move_unit(c, c->side, c->unit_id, dx, dy);
    }
    if (input_key_pressed(KEY_SPACE) || input_key_pressed(KEY_W)) {
        if (c->unit_id >= 0) c->units[c->side][c->unit_id].acted = true;
        return 1;
    }
    if (input_key_pressed(KEY_G)) {
        // Open a y/n give-up confirm; the outer combat loop polls
        // prompt_update() and writes c->result to 2 on YES.
        if (g && g->res) {
            prompt_yes_no_open(g->res->banners.combat_give_up_header,
                               g->res->banners.combat_give_up_body);
        }
        return 0;
    }
    if (input_key_pressed(KEY_S)) {
        // Shoot: arm the picker; the outer loop drives combat_pick_step
        // and dispatches combat_hit_unit when the pick resolves.
        if (c->unit_id < 0) return 0;
        CombatUnit *u = &c->units[c->side][c->unit_id];
        const ResCombatLog *cl_sh = combat_log_strings(c);
        if (u->shots <= 0) {
            combat_log_template(c,
                cl_sh ? cl_sh->no_ammo : "No ammo", NULL, 0);
            return 0;
        }
        if (combat_unit_surrounded(c, c->side, c->unit_id)) {
            combat_log_template(c,
                cl_sh ? cl_sh->cant_shoot : "Can't Shoot", NULL, 0);
            return 0;
        }
        c->cursor_x = u->x;
        c->cursor_y = u->y;
        c->pick_filter   = PICK_FILTER_ENEMY;
        c->pick_reason   = COMBAT_PICK_REASON_SHOOT;
        c->picker_active = true;
        return 0;
    }
    if (input_key_pressed(KEY_F)) {
        // Fly: arm the picker for an empty cell anywhere; the outer
        // loop dispatches combat_fly_unit when the pick resolves.
        if (c->unit_id < 0) return 0;
        CombatUnit *u = &c->units[c->side][c->unit_id];
        const TroopDef *t = troop_by_index(u->troop_idx);
        const ResCombatLog *cl_fl = combat_log_strings(c);
        if (!t || !(t->abilities & TROOP_ABIL_FLY) || u->flights <= 0) {
            combat_log_template(c,
                cl_fl ? cl_fl->cant_fly : "Can't Fly", NULL, 0);
            return 0;
        }
        c->cursor_x = u->x;
        c->cursor_y = u->y;
        c->pick_filter   = PICK_FILTER_EMPTY;
        c->pick_reason   = COMBAT_PICK_REASON_FLY;
        c->picker_active = true;
        return 0;
    }
    if (input_key_pressed(KEY_U)) {
        // Start the cast workflow. The outer loop drives
        // combat_cast_step on subsequent frames; this just transitions
        // into PICK_SPELL so the menu overlay draws next frame.
        const ResCombatLog *cl_pre = combat_log_strings(c);
        Game *gw = c->heroes[c->side];
        if (!gw) return 0;
        if (c->spells_this_round >= 1) {
            combat_log_template(c,
                cl_pre ? cl_pre->only_one_spell
                       : "Only 1 spell per round!", NULL, 0);
            return 0;
        }
        if (!gw->stats.knows_magic) {
            combat_log_template(c,
                cl_pre ? cl_pre->cannot_cast : "You cannot cast magic",
                NULL, 0);
            return 0;
        }
        c->cast_phase = COMBAT_CAST_PICK_SPELL;
        return 0;
    }
    if (input_key_pressed(KEY_C)) {
        // Opening a view pauses combat in the outer loop; the view
        // handles its own input and dismissal. The C↔O swap is
        // implemented there too.
        views_set(VIEW_CONTROLS);
        return 0;
    }
    return 0;
}

static void combat_present(const Combat *c, const Game *g,
                           const Sprites *sprites,
                           RenderTexture2D *target) {
    BeginTextureMode(*target);
    combat_render_frame(c, g, sprites);
    // Open view (Options / Controls / Army / Character) draws over the
    // battlefield, on top of the still-visible field. map/fog are NULL
    // because the views combat can open never read them (WORLDMAP isn't
    // reachable from combat).
    if (views_active() != VIEW_NONE) {
        overlay_draw(g, NULL, NULL, sprites);
    }
    // Spell-pick menu overlay. Drawn while the cast state machine is
    // in PICK_SPELL phase; the outer loop drives combat_cast_step one
    // input per frame.
    if (c->cast_phase == COMBAT_CAST_PICK_SPELL) {
        DrawRectangle(40, 30, 240, 130, PAL_CLR(DBLUE));
        DrawRectangleLines(40, 30, 240, 130, PAL_CLR(YELLOW));
        const Game *gw = c->heroes[c->side];
        const ResUI *ui = (gw && gw->res) ? &gw->res->ui : NULL;
        bfont_draw(ui ? ui->combat_spells_title      : "Spells", 140, 36, PAL_CLR(YELLOW));
        bfont_draw(ui ? ui->combat_spells_col_combat : "Combat", 60, 50, PAL_CLR(YELLOW));
        char line[40];
        const char *names[] = {
            "Clone", "Teleport", "Fireball", "Lightning",
            "Freeze", "Resurrect", "Turn Undead",
        };
        for (int i = 0; i < 7; i++) {
            int count = (gw ? gw->spells.counts[i] : 0);
            snprintf(line, sizeof line, "%d %-12s %c", count, names[i], 'A' + i);
            bfont_draw(line, 56, 64 + i * 10, PAL_CLR(WHITE));
        }
        bfont_draw(ui ? ui->combat_spells_prompt : "Cast which (A-G)?",
                   70, 144, PAL_CLR(WHITE));
    }
    // Victory dialog : centered modal
    // floating over the still-rendered battlefield. Defeat does not
    // draw here — combat exits silently and perform_temp_death shows
    // the disgrace message at the home castle ().
    if (dialog_is_active()) overlay_draw_dialog_centered();
    // Give-up confirm and any other y/n / numeric prompt draws on top
    // of everything else as a bottom-frame modal.
    if (prompt_is_active()) prompt_draw();
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();
    int sx = win_w / CL_SCREEN_W;
    int sy = win_h / CL_SCREEN_H;
    int scale = (sx < sy) ? sx : sy;
    if (scale < 2) scale = 2;
    int dst_w = CL_SCREEN_W * scale;
    int dst_h = CL_SCREEN_H * scale;
    int dst_x = (win_w - dst_w) / 2;
    int dst_y = (win_h - dst_h) / 2;
    Rectangle src = { 0, 0,
                      (float)target->texture.width,
                      -(float)target->texture.height };
    Rectangle dst = { (float)dst_x, (float)dst_y,
                      (float)dst_w, (float)dst_h };
    DrawTexturePro(target->texture, src, dst,
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndDrawing();
}

// Public presenter for the visible-combat animator (WS-7): a thin wrapper over
// the static combat_present so src/combat_replay.c can draw a throwaway Combat
// without duplicating the field-draw + scale-to-window blit.
void combat_present_public(const Combat *c, const Game *g,
                           const Sprites *sprites, void *render_target) {
    combat_present(c, g, sprites, (RenderTexture2D *)render_target);
}

// Combat tick: decays damage-burst, advances the active unit's
// animation frame, signals AI/rollover. The original DOS KB 150ms SYN
// tick gates animation/AI so the human can see units walk between tiles;
// without the gate combat would look like instant teleportation.
static void combat_tick_anim(Combat *c, double *next_tick,
                             bool *rolled_over) {
    *rolled_over = false;
    {
        double now = frame_host_time();
        if (now < *next_tick) return;
        *next_tick = now + 0.15;
    }
    // Decay damage-burst on every stack (including dead ones, so the
    // splat plays out over a now-empty cell).
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c->units[s][i];
            if (u->troop_idx < 0) continue;
            if (u->hit_flash > 0) u->hit_flash--;
        }
    }
    // Advance only the active unit's animation frame (visual cue for
    // whose turn it is). All other stacks stay on frame 0.
    if (c->unit_id >= 0) {
        CombatUnit *act = &c->units[c->side][c->unit_id];
        if (act->troop_idx >= 0 && act->count > 0) {
            act->frame++;
            if (act->frame > 3) {
                act->frame = 0;
                *rolled_over = true;
            }
        }
    }
}

// Block until the player acknowledges the open end-of-combat dialog,
// keeping the battlefield rendered behind it. Without this, RunCombat
// would return the moment c.result is set, callers would mutate world
// state (castle ownership, perform_temp_death), and the dialog would
// be drawn over the overworld instead of over the battle that produced
// it.
static void combat_wait_for_dialog_ack(const Combat *c, const Game *g,
                                       const Sprites *sprites,
                                       RenderTexture2D *target) {
    while (dialog_is_active() && !frame_host_should_close()) {
        combat_present(c, g, sprites, target);
        // Allow screenshots while paused on the end-of-combat dialog
        // (main loop is suspended during RunCombat).
        screenshot_tick(*target, "shot");
        if (ui_any_key_pressed()) {
            if (!dialog_advance()) dialog_dismiss();
        }
    }
}

Combat *combat_current_rendered = NULL;

CombatResult RunCombat(Game *g, const Sprites *sprites,
                       void *render_target,
                       CombatMode mode, const CombatTarget *target) {
    Combat c;
    combat_init(&c, g, mode, target);
    combat_current_rendered = &c;
    // Expose the live combat to the harness so `state` can serialize it.
    // Detached on every return path below.
    combat_seed_rng(&c, g, mode, target);
    combat_prepare_player(&c, g);
    if (mode == COMBAT_MODE_CASTLE) combat_prepare_castle(&c, target);
    else                            combat_prepare_foe(&c, target);
    combat_reset_match(&c);
    {
        char tag[64];
        snprintf(tag, sizeof tag, "combat:start:%s:%s",
                 mode == COMBAT_MODE_CASTLE ? "castle" : "foe",
                 (target && target->name) ? target->name : "?");
        recorder_capture(tag);
    }
    audio_set_track(AUDIO_TRACK_COMBAT);

    // Prime the turn machinery: pretend AI just finished, refresh
    // counters, find first actable player unit.
    combat_reset_turn(&c, COMBAT_SIDE_AI);
    c.unit_id = -1;
    int nxt = combat_next_unit(&c);
    if (nxt < 0) {
        recorder_capture("combat:end:loss");
        audio_play_tune(AUDIO_TUNE_DEFEAT);
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        combat_current_rendered = NULL;
        return COMBAT_RESULT_LOSS;
    }
    c.unit_id = nxt;

    RenderTexture2D *rt = (RenderTexture2D *)render_target;
    double next_tick = frame_host_time() + 0.15;

    while (c.result == 0 && !frame_host_should_close()) {
        // Pump the harness every frame so external commands keep
        // working through combat. Without this the game appears
        // deadlocked from the harness POV the moment battle starts.
        audio_tick();
        combat_present(&c, g, sprites, rt);

        // Modal prompts (give-up confirm) take priority over every
        // other input path and pause combat — a blocking call inside
        // the dispatch.
        if (prompt_is_active()) {
            PromptResult pr = prompt_update();
            if (pr == PROMPT_RESULT_YES) {
                c.result = 2;
                break;
            }
            // NO / CANCEL / NONE: stay in combat, swallow the frame.
            continue;
        }

        // ESC is cancel-only in combat: ESC inside a menu just closes
        // it; ESC with no menu open is a no-op. Take this *before*
        // views eat their own input so dismissing always works from the
        // outer loop.
        if (input_key_pressed(KEY_ESCAPE)) {
            if (views_active() != VIEW_NONE) {
                views_dismiss();
            }
            continue;
        }

        // While a view is open, combat is paused: no AI advancement,
        // no animation frame ticks driving acts. The view handles its
        // own input (and number-key cycling for Controls — see below).
        if (views_active() != VIEW_NONE) {
            // Swap between Options and Controls without leaving the menu.
            if (views_active() == VIEW_OPTIONS && input_key_pressed(KEY_C)) {
                views_set(VIEW_CONTROLS);
            } else if (views_active() == VIEW_CONTROLS &&
                       input_key_pressed(KEY_O)) {
                views_set(VIEW_OPTIONS);
            } else if (views_active() == VIEW_CONTROLS) {
                // Number keys 1..N cycle the corresponding control row.
                for (int i = 0; i < 9; i++) {
                    if (input_key_pressed(KEY_ONE + i)) {
                        views_controls_advance(g, i);
                        break;
                    }
                }
            }
            continue;
        }

        bool frame_rollover;
        combat_tick_anim(&c, &next_tick, &frame_rollover);

        if (c.unit_id >= 0 && !c.picker_active &&
            c.cast_phase == COMBAT_CAST_NONE) {
            // Keep the cursor parked on the acting unit between
            // turns -- but NEVER clobber it while a picker is open,
            // or the player's cursor moves get reset every frame
            // and the pick can never resolve.
            const CombatUnit *act = &c.units[c.side][c.unit_id];
            if (act->troop_idx >= 0 && act->count > 0) {
                c.cursor_x = act->x;
                c.cursor_y = act->y;
            }
        }

        int acted = 0;
        bool player_turn = (c.side == COMBAT_SIDE_PLAYER &&
                            c.unit_id >= 0 &&
                            !c.units[c.side][c.unit_id].out_of_control);

        // Top-level view openers. Player can open these even on AI's
        // turn: input is discarded for moves during AI turn, but menu
        // keys still resolve.
        //
        // Gate KEY_A on no-active-picker: A is also "confirm" for the
        // picker, and we don't want a picker confirm to accidentally
        // open the Army view.
        if (c.cast_phase == COMBAT_CAST_NONE && !c.picker_active) {
            if (input_key_pressed(KEY_O)) {
                views_set(VIEW_OPTIONS);
                continue;
            }
            if (input_key_pressed(KEY_A)) {
                views_set(VIEW_ARMY);
                continue;
            }
            if (input_key_pressed(KEY_V)) {
                views_set(VIEW_CHARACTER);
                continue;
            }
        }

        // Cast workflow takes precedence over the normal player-input
        // dispatch: while c.cast_phase != NONE the spell menu /
        // target picker is on screen and we step that state machine
        // one phase per frame.
        if (c.cast_phase != COMBAT_CAST_NONE) {
            acted = combat_cast_step(&c, g, sprites, rt);
            // No fall-through: even if not yet acted, we don't want
            // KEY_S / KEY_F / KEY_U etc. to re-fire on the same input
            // tick. Resume the next-unit pick if acted; otherwise
            // continue the frame loop.
            if (!acted) continue;
        } else if (c.picker_active &&
                   (c.pick_reason == COMBAT_PICK_REASON_SHOOT ||
                    c.pick_reason == COMBAT_PICK_REASON_FLY)) {
            // Shoot/Fly picker: one combat_pick_step per frame.
            int tx = 0, ty = 0;
            bool cancelled = false;
            bool resolved = combat_pick_step(&c, g, sprites, rt,
                                             &tx, &ty, &cancelled);
            if (cancelled) {
                c.picker_active = false;
                c.pick_reason   = COMBAT_PICK_REASON_NONE;
                continue;
            }
            if (!resolved) continue;
            CombatUnit *u = &c.units[c.side][c.unit_id];
            if (c.pick_reason == COMBAT_PICK_REASON_SHOOT) {
                unsigned char uid = c.umap[ty][tx];
                int t_side = (uid - 1) / COMBAT_SLOTS;
                int t_slot = (uid - 1) % COMBAT_SLOTS;
                combat_hit_unit(&c, c.side, c.unit_id,
                                t_side, t_slot, true);
                u->acted = true;
                acted = 1;
            } else { // FLY
                const TroopDef *t = troop_by_index(u->troop_idx);
                const ResCombatLog *cl_fl = combat_log_strings(&c);
                if (combat_fly_unit(&c, c.side, c.unit_id, tx, ty)) {
                    if (t) {
                        ResTemplateVar vars[] = { { "TROOP", t->name } };
                        combat_log_template(&c,
                            cl_fl ? cl_fl->fly : "%TROOP% fly", vars, 1);
                    }
                    acted = 1;
                }
            }
            c.picker_active = false;
            c.pick_reason   = COMBAT_PICK_REASON_NONE;
        } else if (player_turn) {
            acted = combat_player_action_full(&c, g, sprites, rt);
        } else if (frame_rollover) {
            // AI drives this turn, paced to the SYN-tick rollover so units
            // visibly walk between tiles.
            acted = combat_ai_action(&c);
        }

        if (acted) {
            // Park the now-inactive unit on its idle frame so it stops
            // animating (). The next unit picked
            // below will start cycling from frame 0 in combat_tick_anim.
            if (c.unit_id >= 0) {
                CombatUnit *prev = &c.units[c.side][c.unit_id];
                if (prev->troop_idx >= 0) prev->frame = 0;
            }
            combat_compact(&c);
            if (combat_test_dead(&c, COMBAT_SIDE_AI))     { c.result = 1; break; }
            if (combat_test_dead(&c, COMBAT_SIDE_PLAYER)) { c.result = 2; break; }
            int n = combat_next_unit(&c);
            if (n < 0) combat_next_turn(&c);
            else        c.unit_id = n;
        }
    }

    // ----- End-of-combat ----------------------------------------------------

    // Victory: gold += AI-side spoils, show victory banner.
    // Defeat: show "disgraced" flavor (the temp_death and re-commission
    // flow lives in main.c's caller and stays there).
    if (c.result == 1) {
        g->stats.gold += c.spoils[COMBAT_SIDE_AI];
        char body[400];
        if (c.target_name[0]) {
            snprintf(body, sizeof body,
                     "Well done %s, you have\n"
                     "successfully vanquished\n"
                     "%s.\n\n"
                     "Spoils of War: %d gold",
                     g->character.name[0] ? g->character.name : "warrior",
                     c.target_name,
                     c.spoils[COMBAT_SIDE_AI]);
        } else {
            snprintf(body, sizeof body,
                     "Well done %s, you have\n"
                     "successfully vanquished\n"
                     "yet another foe.\n\n"
                     "Spoils of War: %d gold",
                     g->character.name[0] ? g->character.name : "warrior",
                     c.spoils[COMBAT_SIDE_AI]);
        }
        open_dialog("Victory!", body);
        combat_wait_for_dialog_ack(&c, g, sprites, rt);
        // Write surviving troops back to g->army so the player keeps
        // their losses. Vacated slots get compacted afterwards so the
        // army view stays contiguous (no gaps where a stack was wiped).
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c.units[COMBAT_SIDE_PLAYER][i];
            if (u->troop_idx < 0 || u->count == 0) {
                g->army[i].id[0] = '\0';
                g->army[i].count = 0;
            } else {
                const TroopDef *t = troop_by_index(u->troop_idx);
                if (t) {
                    snprintf(g->army[i].id, sizeof g->army[i].id, "%s", t->id);
                    g->army[i].count = u->count;
                }
            }
        }
        GameCompactArmy(g);
        recorder_capture("combat:end:win");
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        combat_current_rendered = NULL;
        return COMBAT_RESULT_WIN;
    }
    if (c.result == 2) {
        //  — defeat returns silently. The "After
        // being disgraced..." message is shown by perform_temp_death
        // in main.c after the player has been teleported back to the
        // home castle, drawn over the overworld in the bottom box
        // ( which calls draw_map/draw_sidebar
        // first). No battlefield-side dialog here.
        recorder_capture("combat:end:loss");
        audio_play_tune(AUDIO_TUNE_DEFEAT);
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        combat_current_rendered = NULL;
        return COMBAT_RESULT_LOSS;
    }
    // Unreachable in normal play: the loop only exits via WIN or LOSS.
    // Kept as a defensive return so the function is total.
    recorder_capture("combat:end:loss");
    audio_set_track(AUDIO_TRACK_OPENWORLD);
    combat_current_rendered = NULL;
    return COMBAT_RESULT_LOSS;
}
