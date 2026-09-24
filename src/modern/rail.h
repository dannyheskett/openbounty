#ifndef OB_MODERN_RAIL_H
#define OB_MODERN_RAIL_H

// The left rail: five one-tile icons down the edge of the map -- Menu, Map,
// Army, Search, Puzzle -- mirroring the HUD sidebar on the other side. A
// modern declared buffer always has it (CL_RAIL_W).
//
// Drawn with the world by draw_frame (src/shell_frame.c), so the main loop,
// the held-screen path and --gallery all get it.

#include "game.h"
#include "sprites.h"
#include "input.h"

void rail_draw(const Game *g, const Sprites *s);

// The action the player tapped this frame, or INPUT_ACTION_NONE. The rail
// registers no taps while a page is open, so an open page owns every tap.
InputAction rail_tapped(void);

#endif
