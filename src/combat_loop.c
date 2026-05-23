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

// Spell catalog indices. Mirrors the spells block in game.json.
// Engine spell helpers (spell_damage etc.) are in engine/combat.c.
#define COMBAT_SPELL_CLONE        0
#define COMBAT_SPELL_TELEPORT     1
#define COMBAT_SPELL_FIREBALL     2
#define COMBAT_SPELL_LIGHTNING    3
#define COMBAT_SPELL_FREEZE       4
#define COMBAT_SPELL_RESURRECT    5
#define COMBAT_SPELL_TURN_UNDEAD  6

static bool combat_pick_target(Combat *c, const Game *g,
                               const Sprites *sprites,
                               RenderTexture2D *target,
                               int caster_side, int filter,
                               int *out_x, int *out_y) {
    // Enable cursor ring for the duration of the modal pick. Cleared
    // on every return path so the next render is back to normal play.
    c->picker_active = true;
    bool result = false;
    while (!frame_host_should_close()) {
        audio_tick();
        // Cycle the cursor-ring animation one frame per tick (same
        // cadence as everything else in combat now).
        c->cursor_frame = (c->cursor_frame + 1) & 3;
        BeginTextureMode(*target);
        combat_render_frame(c, g, sprites);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        int win_w = GetScreenWidth(), win_h = GetScreenHeight();
        int sx = win_w / CL_SCREEN_W, sy = win_h / CL_SCREEN_H;
        int scale = (sx < sy) ? sx : sy; if (scale < 2) scale = 2;
        int dst_w = CL_SCREEN_W * scale, dst_h = CL_SCREEN_H * scale;
        int dst_x = (win_w - dst_w) / 2, dst_y = (win_h - dst_h) / 2;
        Rectangle src = { 0, 0, (float)target->texture.width,
                          -(float)target->texture.height };
        Rectangle dst = { (float)dst_x, (float)dst_y,
                          (float)dst_w, (float)dst_h };
        DrawTexturePro(target->texture, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

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
                c->cursor_x = nx; c->cursor_y = ny;
            }
        }
        if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
            input_key_pressed(KEY_SPACE) ||
            input_key_pressed(KEY_A)     || input_key_pressed(KEY_C)) {
            if (combat_cell_passes_filter(c, c->cursor_x, c->cursor_y,
                                           caster_side, filter)) {
                if (out_x) *out_x = c->cursor_x;
                if (out_y) *out_y = c->cursor_y;
                result = true;
                goto done;
            }
            // Filter rejected -- leave cursor and re-loop.
        }
        if (input_key_pressed(KEY_ESCAPE)) goto done;
    }
done:
    c->picker_active = false;
    return result;
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

static int combat_player_cast(Combat *c, const Game *g,
                              const Sprites *sprites,
                              RenderTexture2D *target) {
    const ResCombatLog *cl_pre = combat_log_strings(c);
    if (c->spells_this_round >= 1) {
        combat_log_template(c,
            cl_pre ? cl_pre->only_one_spell : "Only 1 spell per round!",
            NULL, 0);
        return 0;
    }
    Game *gw = c->heroes[c->side];
    if (!gw) return 0;
    // Pre-check: hero must know magic. `g->stats.knows_magic` is the
    // live flag — initialised from the rank-0 class default, then set
    // true by the Archmage's alcove (REQ-123). Any class that bought
    // magic at the alcove can cast in combat.
    if (!gw->stats.knows_magic) {
        combat_log_template(c,
            cl_pre ? cl_pre->cannot_cast : "You cannot cast magic",
            NULL, 0);
        return 0;
    }

    // Modal: wait for A-G or Esc. The caller ran a render frame just
    // before this; we render-and-poll until a key resolves.
    int picked = -1;
    while (!frame_host_should_close() && picked < 0) {
        BeginTextureMode(*target);
        combat_render_frame(c, g, sprites);
        // Inline overlay: spells menu, abbreviated single-column layout.
        DrawRectangle(40, 30, 240, 130, PAL_CLR(DBLUE));
        DrawRectangleLines(40, 30, 240, 130, PAL_CLR(YELLOW));
        const ResUI *ui = (gw && gw->res) ? &gw->res->ui : NULL;
        bfont_draw(ui ? ui->combat_spells_title      : "Spells", 140, 36, PAL_CLR(YELLOW));
        bfont_draw(ui ? ui->combat_spells_col_combat : "Combat", 60, 50, PAL_CLR(YELLOW));
        char line[40];
        const char *names[] = {
            "Clone", "Teleport", "Fireball", "Lightning",
            "Freeze", "Resurrect", "Turn Undead",
        };
        for (int i = 0; i < 7; i++) {
            snprintf(line, sizeof line, "%d %-12s %c",
                     gw->spells.counts[i], names[i], 'A' + i);
            bfont_draw(line, 56, 64 + i * 10, PAL_CLR(WHITE));
        }
        bfont_draw(ui ? ui->combat_spells_prompt : "Cast which (A-G)?", 70, 144, PAL_CLR(WHITE));
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        int win_w = GetScreenWidth(), win_h = GetScreenHeight();
        int sx = win_w / CL_SCREEN_W, sy = win_h / CL_SCREEN_H;
        int sc = (sx < sy) ? sx : sy; if (sc < 2) sc = 2;
        int dst_w = CL_SCREEN_W * sc, dst_h = CL_SCREEN_H * sc;
        int dst_x = (win_w - dst_w) / 2, dst_y = (win_h - dst_h) / 2;
        Rectangle src = { 0, 0, (float)target->texture.width,
                          -(float)target->texture.height };
        Rectangle dst = { (float)dst_x, (float)dst_y,
                          (float)dst_w, (float)dst_h };
        DrawTexturePro(target->texture, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

        if (input_key_pressed(KEY_ESCAPE)) return 0;
        for (int i = 0; i < 7; i++) {
            if (input_key_pressed(KEY_A + i)) { picked = i; break; }
        }
    }
    if (picked < 0) return 0;
    if (gw->spells.counts[picked] <= 0) {
        combat_log_template(c,
            cl_pre ? cl_pre->no_spell_type : "No spells of that type",
            NULL, 0);
        return 0;
    }

    int caster_side = c->side;
    int sp = gw->stats.spell_power;
    if (sp < 1) sp = 1;
    if (gw->artifacts.found[0]) {
        // Amulet of Augmentation = DOUBLE_SPELL_POWER.
        // Index 0 is the Amulet's slot in game.json.
    }
    int tx = 0, ty = 0;

    int consumed = 1;     // most spells consume the turn
    int casted   = 0;
    switch (picked) {
        case COMBAT_SPELL_CLONE:
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (combat_pick_target(c, g, sprites, target,
                                    caster_side, PICK_FILTER_FRIENDLY,
                                    &tx, &ty)) {
                unsigned char uid = c->umap[ty][tx];
                int t_side = (uid - 1) / COMBAT_SLOTS;
                int t_slot = (uid - 1) % COMBAT_SLOTS;
                spell_clone(c, t_side, t_slot, sp);
                casted = 1;
            }
            break;
        case COMBAT_SPELL_TELEPORT: {
            int sx_p = 0, sy_p = 0;
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ANY_UNIT,
                                     &sx_p, &sy_p)) break;
            unsigned char uid = c->umap[sy_p][sx_p];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            if (combat_pick_target(c, g, sprites, target,
                                    caster_side, PICK_FILTER_EMPTY,
                                    &tx, &ty)) {
                spell_teleport(c, t_side, t_slot, tx, ty);
                casted = 1;
            }
            break;
        }
        case COMBAT_SPELL_FIREBALL: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ENEMY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            //  cancels on IMMUNE.
            // freeze_army pattern (game.c:3919): the caller treats a
            // failed cast as "spell not used", so casted stays 0 and
            // the spell count is preserved.
            const TroopDef *tt =
                troop_by_index(c->units[t_side][t_slot].troop_idx);
            if (tt && (tt->abilities & TROOP_ABIL_IMMUNE)) {
                combat_log_template(c,
                    cl_pre ? cl_pre->no_effect_msg
                           : "The spell seems to have no effect!",
                    NULL, 0);
                break;
            }
            spell_damage(c, t_side, t_slot, spell_damage_value(25, sp));
            combat_log_template(c,
                cl_pre ? cl_pre->cast_fireball : "Fireball!", NULL, 0);
            casted = 1;
            break;
        }
        case COMBAT_SPELL_LIGHTNING: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ENEMY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            const TroopDef *tt =
                troop_by_index(c->units[t_side][t_slot].troop_idx);
            if (tt && (tt->abilities & TROOP_ABIL_IMMUNE)) {
                combat_log_template(c,
                    cl_pre ? cl_pre->no_effect_msg
                           : "The spell seems to have no effect!",
                    NULL, 0);
                break;
            }
            spell_damage(c, t_side, t_slot, spell_damage_value(10, sp));
            combat_log_template(c,
                cl_pre ? cl_pre->cast_lightning : "Lightning!", NULL, 0);
            casted = 1;
            break;
        }
        case COMBAT_SPELL_FREEZE: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ENEMY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            // spell_freeze returns -1 on IMMUNE (no-op); only consume
            // the spell when it actually took effect.
            if (spell_freeze(c, t_side, t_slot) == 1) casted = 1;
            break;
        }
        case COMBAT_SPELL_RESURRECT: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_FRIENDLY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            // spell_resurrect returns 0 if the stack is full or dead;
            // only consume the spell when troops were actually revived.
            if (spell_resurrect(c, t_side, t_slot, sp) == 1) casted = 1;
            break;
        }
        case COMBAT_SPELL_TURN_UNDEAD: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_UNDEAD,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            const TroopDef *tt =
                troop_by_index(c->units[t_side][t_slot].troop_idx);
            if (tt && (tt->abilities & TROOP_ABIL_IMMUNE)) {
                combat_log_template(c,
                    cl_pre ? cl_pre->no_effect_msg
                           : "The spell seems to have no effect!",
                    NULL, 0);
                break;
            }
            spell_damage(c, t_side, t_slot, spell_damage_value(50, sp));
            combat_log_template(c,
                cl_pre ? cl_pre->cast_turn_undead : "Turn Undead!",
                NULL, 0);
            casted = 1;
            break;
        }
    }

    if (casted) {
        gw->spells.counts[picked]--;
        c->spells_this_round++;
        return consumed;  // most spells advance the turn
    }
    return 0;
}

static int combat_player_action_full(Combat *c, const Game *g,
                                     const Sprites *sprites,
                                     RenderTexture2D *target) {
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
        // Shoot. Pick enemy, ranged damage.
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
        int tx = 0, ty = 0;
        if (combat_pick_target(c, g, sprites, target,
                                c->side, PICK_FILTER_ENEMY, &tx, &ty)) {
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            combat_hit_unit(c, c->side, c->unit_id, t_side, t_slot, true);
            u->acted = true;
            return 1;
        }
        return 0;
    }
    if (input_key_pressed(KEY_F)) {
        // Fly: relocate the active unit to an empty, unobstructed cell
        // anywhere on the field. Only legal for TROOP_ABIL_FLY units
        // with flights > 0. Picker uses PICK_FILTER_EMPTY (no unit,
        // no obstacle). Spec lines 5588–5593.
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
        int tx = 0, ty = 0;
        if (combat_pick_target(c, g, sprites, target,
                                c->side, PICK_FILTER_EMPTY, &tx, &ty)) {
            if (combat_fly_unit(c, c->side, c->unit_id, tx, ty)) {
                // Mirror the AI's per-flight log line so the player
                // sees the same "<TROOP> fly" feedback.
                ResTemplateVar vars[] = { { "TROOP", t->name } };
                combat_log_template(c,
                    cl_fl ? cl_fl->fly : "%TROOP% fly", vars, 1);
                return 1;
            }
        }
        return 0;
    }
    if (input_key_pressed(KEY_U)) {
        return combat_player_cast(c, g, sprites, target);
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

// One tick = one logical step. No wall-clock gating; the engine
// advances animation frame, decays damage burst, and signals AI to
// act every call. Pacing for human watchability comes from the outer
// loop's frame-rate cap (frame_host_set_test_fps), not from a
// secondary timer inside combat. This matches open-world: one tick,
// one decision, one observable state change.
//
// Note: the original DOS KB had a 150ms SYN tick gating AI/animation
// here. We dropped it so combat runs at the same pace as open-world
// walking. The unit animation still cycles 0→1→2→3→0 visibly, just
// frame-per-frame instead of every 9 frames.
static void combat_tick_anim(Combat *c, bool *rolled_over) {
    *rolled_over = false;
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
        combat_tick_anim(&c, &frame_rollover);

        if (c.unit_id >= 0) {
            const CombatUnit *act = &c.units[c.side][c.unit_id];
            if (act->troop_idx >= 0 && act->count > 0) {
                c.cursor_x = act->x;
                c.cursor_y = act->y;
            }
        }

        // Top-level view openers. Player can open these even on AI's
        // turn: input is discarded for moves during AI turn, but menu
        // keys still resolve.
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

        int acted = 0;
        bool player_turn = (c.side == COMBAT_SIDE_PLAYER &&
                            c.unit_id >= 0 &&
                            !c.units[c.side][c.unit_id].out_of_control);
        if (player_turn) {
            acted = combat_player_action_full(&c, g, sprites, rt);
        } else {
            // AI drives this turn — either a real AI unit or an
            // out-of-control player unit. One decision per tick, same
            // pacing as the open-world step loop.
            (void)frame_rollover;
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
