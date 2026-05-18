// src/shell_frame.h
//
// Per-frame render dispatcher. Draws into the 320x200 render target;
// outer scaling/letterboxing happens in the main loop's present step.

#ifndef OB_SHELL_FRAME_H
#define OB_SHELL_FRAME_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"

void draw_frame(const Game *game, const Map *map, const Fog *fog,
                const Sprites *sprites);

#endif
