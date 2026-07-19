// engine/include/end_screen.h
//
// Engine-side surface for opening the win/lose screen. The drawing
// counterpart (screen_end_game_draw) is shell-only and lives in
// src/screens/end_game.h.

#ifndef OB_ENGINE_END_SCREEN_H
#define OB_ENGINE_END_SCREEN_H

#include <stdbool.h>

// Open the win or lose screen. `body` is the already-substituted,
// multi-line text to render. flows.c calls this; the shell renderer
// reads back the cached state.
void screen_end_game_open(bool won, const char *body);

#endif
