// src/shell_goto.h -- Goto's walk (#70): the hero travels a planned route one
// ordinary GameStep per beat, and stops the moment anything happens.

#ifndef OB_SHELL_GOTO_H
#define OB_SHELL_GOTO_H

#include <stdbool.h>
#include "game.h"
#include "goto.h"

// Start walking `p` from where the hero stands now.
void shell_goto_start(const Game *g, const GotoPath *p);
void shell_goto_cancel(void);
bool shell_goto_active(void);

// The step to take this frame, when the beat has come: false while waiting
// for it, and false (the walk ended) when the hero is no longer where the
// route left him, the province changed, or something waits on the player.
bool shell_goto_next(const Game *g, double now, int *dx, int *dy);

// After the step the walk asked for: ends the walk when the step did not move
// the hero, when it opened anything (a message, a question, a screen, a flow,
// the week's end), or when the route is done.
void shell_goto_after_step(const Game *g, bool moved);

#endif
