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

// Render ONE complete adventure frame to the window: draw_frame into the 320x200
// `render_target`, then blit it scaled+letterboxed to the screen (the same
// present step the main loop and combat replay use). Used by the visible-autoplay
// view presenter to show a held screen (town/castle) for a beat. `render_target`
// is a RenderTexture2D* (void* to keep raylib out of the header's public deps).
void shell_present_frame(const Game *game, const Map *map, const Fog *fog,
                         const Sprites *sprites, void *render_target);

#endif
