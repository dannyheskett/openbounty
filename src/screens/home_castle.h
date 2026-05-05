// VIEW_HOME_CASTLE — Castle of King Maximus.
//
//   "Castle of King Maximus / A) Recruit Soldiers / B) Audience with
//   the King". The map area is replaced by the castle backdrop (with
//   an animated castle-class troop); the bottom panel shows the menu;
//   status bar shows "Press 'ESC' to exit".
//
// Sub-flows:
//   A) → VIEW_RECRUIT_SOLDIERS pushed on top (handled by the recruit
//        screen module).
//   B) → audience modal popup over the backdrop (run_audience_dialog).
//   ESC → returns to the overworld (pops VIEW_HOME_CASTLE).

#ifndef SCREENS_HOME_CASTLE_H
#define SCREENS_HOME_CASTLE_H

#include "../game.h"
#include "../sprites.h"

// Open the home-castle screen. Pushes VIEW_HOME_CASTLE on the view
// stack and primes the controller's internal state (random animated
// troop pick, etc.). Idempotent — safe to call repeatedly.
void screen_home_castle_open(Game *g);

// Render the backdrop + menu panel. Called from overlay_draw
// when views_active() == VIEW_HOME_CASTLE. Internally advances the
// SYN-driven frame counter at the throne_room_or_barracks SOFT_WAIT
// cadence (150ms).
void screen_home_castle_draw(const Game *g, const Sprites *s);

#endif
