// src/shell_cheats.h
//
// The debug cheats. Reachable ONLY when the game was started with --debug:
// then the modern game menu carries a Debug page with one row per cheat
// (src/views.c). Without --debug nothing calls these, in any mode -- there is
// no key that opens them.
//
// Each cheat mutates game state directly and reports what it did as a dialog.
// Win and Lose end the game: cheat_apply returns CHEAT_DISPATCHED_TERMINAL so
// the caller skips the rest of the frame.

#ifndef OB_SHELL_CHEATS_H
#define OB_SHELL_CHEATS_H

#include "raylib.h"

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "sprites.h"

typedef enum {
    CHEAT_DISPATCHED,             // applied; a dialog reports it
    CHEAT_DISPATCHED_TERMINAL,    // Win / Lose: the caller should `continue`
} CheatResult;

typedef enum {
    CHEAT_GOLD,
    CHEAT_LEADERSHIP,
    CHEAT_MAGIC,
    CHEAT_SPELLS,
    CHEAT_SIEGE,
    CHEAT_FLIGHT,
    CHEAT_ZONE,
    CHEAT_FOG,
    CHEAT_WIN,
    CHEAT_LOSE,
    CHEAT_COUNT
} CheatAction;

// The row label for a cheat on the Debug page.
const char *cheat_label(CheatAction a);
// What it does, one line, for the Debug page.
const char *cheat_desc(CheatAction a);

CheatResult cheat_apply(CheatAction a, Game *game, Map *map, Fog *fog,
                        const Resources *res, const Sprites *sprites,
                        RenderTexture2D *render_target);

#endif
