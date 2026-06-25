#ifndef OB_END_CARTOON_H
#define OB_END_CARTOON_H

#include "raylib.h"
#include "resources.h"
#include "sprites.h"

// Run victory cutscene (game.c:4281 draw_cartoon_frame /
// game.c:4374 display_cartoon). Draws a grass grid with a carpet/bridge
// strip growing up, a hero tile advancing along it, and (optionally) the
// full troop roster cycling their walk frames around the border.
//
// Blocks the main loop while the cartoon plays. Advances one animation
// frame every `res->ending.ticks_per_step` ticks until
// `res->ending.frame_count` is reached, or until the player presses any
// key. Renders into `rt` and blits to the current window in the same
// letterboxed 2× (or higher) scale as the adventure view.
//
// No-op (returns immediately) if any of the required tile paths are
// missing from res->ending.
void run_end_cartoon(RenderTexture2D *rt,
                             const Resources *res,
                             const Sprites *sprites);

#endif
