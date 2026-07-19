// src/combat_replay.h -- visible-mode combat animator. See combat_replay.c.

#ifndef OB_COMBAT_REPLAY_H
#define OB_COMBAT_REPLAY_H

#include "combat.h"     // CombatMode, CombatTurnRecord
#include "sprites.h"

typedef enum {
    COMBAT_REPLAY_OK = 0,   // fully drawn (or nothing to draw)
    COMBAT_REPLAY_ABORT,    // window closed mid-fight -- abort the run
} CombatReplayStatus;

// Animate the recorded per-turn battle: build a throwaway Combat from
// the live PRE-FIGHT game (via shell_ctx->game + the pending flow) and render
// each recorded action on a paced beat. Never touches the live Game; never
// resolves combat. render_target is RenderTexture2D* (void* keeps raylib out of
// callers' headers). Returns ABORT if the window was closed mid-fight.
CombatReplayStatus RenderCombatRecord(void *shell_ctx, CombatMode mode,
                                      const CombatTurnRecord *rec,
                                      const Sprites *sprites,
                                      void *render_target);

#endif // OB_COMBAT_REPLAY_H
