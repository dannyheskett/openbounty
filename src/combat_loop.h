// src/combat_loop.h
//
// Shell-side combat: the rendered combat loop. Uses raylib types
// (via void * for render_target). Engine code uses combat.h
// (headless combat + Combat struct) instead.

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

#endif
