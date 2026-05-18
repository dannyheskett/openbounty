// src/shell_ctx.h
//
// Per-frame shell context: bundles the live game state pointers + the
// render target that the main-loop body and the prompt dispatcher need.
// Avoids threading ~7 separate pointers through every extracted helper.

#ifndef OB_SHELL_CTX_H
#define OB_SHELL_CTX_H

#include "raylib.h"

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "sprites.h"

typedef struct {
    Game            *game;
    Map             *map;
    Fog             *fog;
    const Resources *res;
    const Sprites   *sprites;
    RenderTexture2D *render_target;
    bool            *quit_requested;
} ShellCtx;

#endif
