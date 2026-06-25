// src/shell_cheats.h
//
// F10 debug cheat menu. Modal: F10 opens, letter dispatches an
// action, F10/ESC closes. Mutates game state directly. The W (win)
// and L (lose) actions short-circuit normal per-frame logic — the
// dispatcher returns CHEAT_DISPATCHED_TERMINAL to signal the caller
// to skip the rest of the frame's input handling.

#ifndef OB_SHELL_CHEATS_H
#define OB_SHELL_CHEATS_H

#include "raylib.h"

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "sprites.h"

typedef enum {
    CHEAT_IDLE,                   // no menu active, no input consumed
    CHEAT_OPENED,                 // F10 just opened the menu
    CHEAT_DISMISSED,              // ESC/F10 closed the menu
    CHEAT_DISPATCHED,             // letter applied; menu closed
    CHEAT_DISPATCHED_TERMINAL,    // W/L: caller should `continue` the loop
} CheatResult;

bool cheat_menu_is_active(void);

// Per-frame pump. Call from the main loop's input section.
CheatResult cheat_menu_tick(Game *game, Map *map, Fog *fog,
                            const Resources *res,
                            const Sprites *sprites,
                            RenderTexture2D *render_target);

#endif
