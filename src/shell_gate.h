// src/shell_gate.h
//
// Town Gate / Castle Gate destination picker. The engine cast fns
// (cast_town_gate / cast_castle_gate, engine/spells_adventure.c) arm
// gate_state = GATE_STATE_SELECT and gate_mode (0 castle / 1 town). This shell
// pump then asks the engine for the hero's VISITED, gate-eligible destinations
// (GameGateDestinations) and raises the cursored VIEW_GATE panel
// (views_gate_open, drawn by views_render.c draw_gate). Choosing a destination
// teleports there (GameSwitchZone + position) and consumes one spell charge;
// ESC cancels with no charge spent.

#ifndef OB_SHELL_GATE_H
#define OB_SHELL_GATE_H

#include "game.h"
#include "map.h"
#include "fog.h"

typedef enum {
    GATE_MENU_IDLE,     // no picker armed/active; input untouched
    GATE_MENU_ACTIVE,   // picker is showing or just resolved -- caller should
                        // skip the rest of the frame's input handling
} GateMenuResult;

// Per-frame pump. Call from the main loop's input section (beside
// cheat_menu_tick). Opens the picker when a gate spell armed gate_state, drives
// letter/ESC input while it's up, and teleports on a choice.
GateMenuResult gate_menu_tick(Game *game, Map *map, Fog *fog);

#endif
