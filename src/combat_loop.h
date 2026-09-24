// src/combat_loop.h
//
// Shell-side combat: the rendered combat loop. Uses raylib types
// (via void * for render_target). Engine code uses combat.h
// (headless combat + Combat struct) instead.

#ifndef OB_COMBAT_LOOP_H
#define OB_COMBAT_LOOP_H

#include "combat.h"
#include "sprites.h"
#include "map.h"
#include "fog.h"

// Runs a battle. render_target is the offscreen RenderTexture2D the
// rest of the game renders into; combat shares the same target so the
// outer scaling / letterbox logic in main.c is unchanged. Passed as
// void * to keep this header free of raylib.h. `m` and `f` are the world
// the battle is fought in: modern draws it behind the battle wherever the
// battle floats over it.
CombatResult RunCombat(Game *g, const Map *m, const Fog *f, const Sprites *sprites,
                       void *render_target,
                       CombatMode mode, const CombatTarget *target);

// One frame of a battle (field, columns, pages) into render_target and to the
// window: the replay animator and --gallery draw their throwaway fights with it.
void combat_present_public(const Combat *c, const Game *g, const Map *m, const Fog *f,
                           const Sprites *sprites, void *render_target);

// One-frame target-picker step. Reads at most one input. Returns
// true and writes (*out_x, *out_y) once the player confirms a valid
// cell; sets *out_cancelled true on ESC; returns false otherwise.
// Caller sets c->picker_active, c->pick_reason, c->pick_filter, and
// c->cursor_x/y before the first call and clears picker_active when
// the pick resolves.
// render_target is RenderTexture2D *; void * keeps raylib out.
bool combat_pick_step(Combat *c, const Game *g, const Sprites *sprites,
                      void *render_target,
                      int *out_x, int *out_y, bool *out_cancelled);

// One-frame cast workflow step. Dispatches on c->cast_phase.
// Returns 1 if the casting unit's turn is consumed this frame
// (effect applied), 0 if still mid-cast or cancelled. Resets
// c->cast_phase to NONE on APPLY and on cancel.
int combat_cast_step(Combat *c, Game *g, const Sprites *sprites,
                     void *render_target);

#endif
