// src/shell_weekend.h
//
// End-of-week two-screen sequence (astrology -> budget). The main loop
// calls pump_week_end_dialog() each frame; when pending_week_phase is
// non-NONE and no other dialog/prompt/view is up, the next screen
// opens. Returns true if a dialog was opened (so the caller can pause
// normal input).

#ifndef OB_SHELL_WEEKEND_H
#define OB_SHELL_WEEKEND_H

#include <stdbool.h>

#include "game.h"

bool pump_week_end_dialog(const Game *g);

#endif
