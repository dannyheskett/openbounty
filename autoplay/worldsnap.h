// autoplay/worldsnap.h
//
// Full world snapshot/rollback (AP-030, AP-031). A WorldSnapshot value-copies
// Game, Map, Fog, the world RNG, and the diagnostic ledger. These are flat
// structs with no owned heap (Game's only pointer is the shared read-only
// res; map tiles are inline), so the copy is complete and the restore is
// bit-identical. Captures are always taken at FLOW_NONE; restore resets the
// pending_* flow globals and the adventure-spell UI continuations (AP-033).

#ifndef OB_AUTOPLAY_WORLDSNAP_H
#define OB_AUTOPLAY_WORLDSNAP_H

#include <stdint.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "exec_ledger.h"

typedef struct {
    Game       game;
    Map        map;
    Fog        fog;
    uint64_t   rng;
    LedgerSnap ledger;
} WorldSnapshot;

void worldsnap_capture(WorldSnapshot *snap, const Game *g, const Map *map,
                       const Fog *fog);
void worldsnap_restore(const WorldSnapshot *snap, Game *g, Map *map, Fog *fog);

// Calendar-death latch (the budget-independence keystone). A rollback restores
// days_left to a positive value and clears game_over, which would RESURRECT a
// run that just died on the calendar and let it take a path a larger day
// budget never sees. worldsnap_restore latches when the world it is about to DISCARD
// died on the calendar (days_left==0, not won); the flag lives outside Game so
// no restore can clear it. The planner reads it and terminates: calendar death
// is strictly terminal, so game length never changes play. Reset per run.
void worldsnap_reset_calendar_dead(void);
bool worldsnap_calendar_dead(void);

// Offline fingerprint over the snapshot-relevant state, for tests: two
// identical worlds fingerprint identically; a restore reproduces the capture.
uint32_t worldsnap_fingerprint(const Game *g, const Map *map, const Fog *fog);

#endif
