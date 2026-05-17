// src/combat_loop.h
//
// Shell-side combat: the rendered combat loop and the auto-player
// toggle. Uses raylib types (via void * for render_target). Engine
// code uses combat.h (headless combat + Combat struct) instead.

#ifndef OB_COMBAT_LOOP_H
#define OB_COMBAT_LOOP_H

#include "combat.h"
#include "sprites.h"

// Runs a battle. render_target is the offscreen RenderTexture2D the
// rest of the game renders into; combat shares the same target so the
// outer scaling / letterbox logic in main.c is unchanged. Passed as
// void * to keep this header free of raylib.h.
CombatResult RunCombat(Game *g, const Sprites *sprites,
                       void *render_target,
                       CombatMode mode, const CombatTarget *target);

// Auto-combat toggle. When on, RunCombat drives the player's turns
// through combat_ai_action (skipping the modal player-input flow).
// Persists across fights until cleared. Used by the harness so a
// scripted driver can run a full playthrough without authoring per-
// fight key sequences. No effect on combat_run_headless (already AI
// vs AI by definition).
void combat_set_auto_player(bool on);
bool combat_auto_player(void);

#endif
