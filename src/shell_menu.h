// src/shell_menu.h
//
// Game-menu (Esc / O) callback shims. The view stack invokes these
// when the user picks Save / Load / New / Quit from the in-game menu.
// Each takes a MenuCtx via the menu's `void *ud` opaque pointer.

#ifndef OB_SHELL_MENU_H
#define OB_SHELL_MENU_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

typedef struct {
    Game            *game;
    Map             *map;
    Fog             *fog;
    const Resources *res;
    int              spawn_x, spawn_y;
    bool            *quit_flag;
    bool             hud_pref;
} MenuCtx;

bool menu_save(void *ud);
bool menu_load(void *ud);
bool menu_new(void *ud);
bool menu_quit(void *ud);

#endif
