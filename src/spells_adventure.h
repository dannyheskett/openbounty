#ifndef SPELLS_ADVENTURE_H
#define SPELLS_ADVENTURE_H

#include "game.h"
#include "map.h"

// Adventure-mode spell entry points and the bridge/gate continuation
// state. Originally inline in main.c; extracted so main.c shrinks to
// bootstrap + main-loop dispatch.

// Display-name lookup used by every spell-effect dialog. Returns the
// spell's game.json name, or `fallback` if the spell id is missing.
const char *spell_header(const char *spell_id, const char *fallback);

// Build a stretch of bridge in (dx, dy) from the hero's current tile.
// Returns the number of tiles converted. Called from the main loop's
// post-cast direction handler.
int try_build_bridge(Game *g, Map *map, int dx, int dy);

// Open the spell described by `spell_idx` (index into the SPELLS catalog).
// Validates the spell is an adventure spell and the hero has a charge.
// Each cast_* implementation handles its own dialog + post-state setup.
void dispatch_adventure_spell(Game *g, int spell_idx);

// Bridge-direction continuation. cast_bridge() opens a dialog and sets
// bridge_state to DIRECTION; the main loop's input handler reads this
// to consume the next direction press.
typedef enum {
    BRIDGE_STATE_NONE = 0,
    BRIDGE_STATE_DIRECTION,
} BridgeState;
extern BridgeState bridge_state;

// Castle/town gate selection continuation. cast_castle_gate() and
// cast_town_gate() set gate_state to SELECT and gate_mode to 0/1; the
// main loop's input handler reads the next letter press.
typedef enum {
    GATE_STATE_NONE = 0,
    GATE_STATE_SELECT,
} GateState;
extern GateState gate_state;
extern int gate_mode;   // 0 = castle, 1 = town

#endif
