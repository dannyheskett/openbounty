#ifndef OB_SPELLS_ADVENTURE_H
#define OB_SPELLS_ADVENTURE_H

#include "game.h"
#include "map.h"

// Adventure-mode spell entry points and the bridge/gate continuation state.
// They live in the engine so every driver -- the shell, autoplay and demo --
// casts through one implementation rather than each rolling its own.

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

// Apply the EFFECT of adventure spell `spell_idx` (the single id->effect mapping,
// engine-only). Consumes one charge and mutates g; no precondition prompts.
// Shared by the UI dispatcher and headless callers (autoplay's generic spell
// economy applies any owned/buyable spell on a Game copy and re-checks the
// objective -- no spell id is known to autoplay). Returns true if an effect was
// applied (id is a known adventure spell), false otherwise.
bool GameApplyAdventureSpellEffect(Game *g, int spell_idx);

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

// Reset the adventure-spell UI process-globals (gate/bridge selection scratch) to
// idle. These are NOT part of Game, so worldsnap_restore/pending_reset do not
// revert them; an autoplay planning pass that probes gate/bridge spells can leave
// them armed. Call after a planning pass so planning leaves no residue (AP-033).
void spells_adventure_reset_ui(void);

#endif
