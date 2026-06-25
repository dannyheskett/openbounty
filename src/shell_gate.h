// src/shell_gate.h
//
// Town Gate / Castle Gate destination picker. The engine cast fns
// (cast_town_gate / cast_castle_gate, engine/spells_adventure.c) arm
// gate_state = GATE_STATE_SELECT and gate_mode (0 castle / 1 town). This shell
// pump then lists the hero's VISITED, gate-eligible destinations as a lettered
// body in the message box (the same primitive the F10 debug menu uses —
// player_io_message + a GetKeyPressed letter dispatch), and on a letter press
// teleports there (GameSwitchZone + position) and consumes one spell charge.
// ESC cancels with no charge spent. Modeled 1:1 on cheat_menu_tick.

#ifndef OB_SHELL_GATE_H
#define OB_SHELL_GATE_H

#include "game.h"
#include "map.h"
#include "fog.h"

typedef enum {
    GATE_MENU_IDLE,     // no picker armed/active; input untouched
    GATE_MENU_ACTIVE,   // picker is showing or just resolved — caller should
                        // skip the rest of the frame's input handling
} GateMenuResult;

// Per-frame pump. Call from the main loop's input section (beside
// cheat_menu_tick). Opens the picker when a gate spell armed gate_state, drives
// letter/ESC input while it's up, and teleports on a choice.
GateMenuResult gate_menu_tick(Game *game, Map *map, Fog *fog);

#endif
