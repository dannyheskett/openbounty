// src/shell_gate.c
//
// Town/Castle Gate destination picker -- thin shell glue. The engine cast fns
// (cast_town_gate / cast_castle_gate, engine/spells_adventure.c) arm
// gate_state = GATE_STATE_SELECT and gate_mode (0 castle / 1 town). This pump
// turns that into an open VIEW_GATE: it asks the engine for the visited,
// gate-eligible destinations (GameGateDestinations) and hands them to the
// cursored picker view (views_gate_open). Selection input, teleport, and cancel
// are handled in the VIEW_GATE branch of the main loop, which calls the engine's
// GameGateTeleport (boat-aware) and clears gate_state.

#include "shell_gate.h"

#include "views.h"            // views_gate_open / views_active
#include "spells_adventure.h" // gate_state / gate_mode

#include <stdlib.h>

GateMenuResult gate_menu_tick(Game *game, Map *map, Fog *fog) {
    (void)map;
    (void)fog;

    // Already showing the picker: the VIEW_GATE input branch owns the frame.
    if (views_active() == VIEW_GATE) return GATE_MENU_ACTIVE;

    // A gate spell armed gate_state this frame: build the list and open the view.
    if (gate_state != GATE_STATE_SELECT) return GATE_MENU_IDLE;
    gate_state = GATE_STATE_NONE;   // consumed: the view now drives the flow

    int cap = GameGateDestsMax(game);
    GateDestination *dests = cap > 0 ? malloc((size_t)cap * sizeof *dests) : NULL;
    if (!dests) return GATE_MENU_IDLE;
    int n = GameGateDestinations(game,
                                 gate_mode == 1 ? GATE_DEST_TOWN
                                                : GATE_DEST_CASTLE,
                                 dests, cap);
    if (n > 0) views_gate_open(dests, n, gate_mode == 1);
    free(dests);
    return n > 0 ? GATE_MENU_ACTIVE : GATE_MENU_IDLE;   // cast fn already guards the none case
}
