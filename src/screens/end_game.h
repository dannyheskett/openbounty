// VIEW_WIN / VIEW_LOSE -- end-of-game screens.
//
//   1. Win path runs the bridge-walking cartoon first.
//   2. Both paths show the ending image on the RIGHT half of the
//      screen, with the LEFT half filled with the ending background
//      color (dark blue).
//   3. Player name / class / score substituted into the body text.
//   4. Wait for ESC.
//
// This is the only "fullscreen" view in the framework -- it covers
// both the map area AND the sidebar.
//
// The pre-screen cartoon is owned by run_end_cartoon() in
// end_cartoon.c and runs to completion before the screen is pushed.

#ifndef OB_SCREENS_END_GAME_H
#define OB_SCREENS_END_GAME_H

#include "game.h"
#include "sprites.h"
#include <stdbool.h>

// Open the win or lose screen. Caches the formatted body text so the
// renderer can paint it line by line each frame.
//
// `body` is the already-substituted, multi-line text to render in the
// LEFT half of the screen. Caller is responsible for inserting player
// name, class, score, etc.
void screen_end_game_open(bool won, const char *body);

void screen_end_game_draw(const Game *g, const Sprites *s);

#endif
