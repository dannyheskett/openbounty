// src/modern/location.h
//
// The temple and dwelling screens' deal (REQ-430r): what happened when the
// player answered, so the screen can stay up and say it in its in-lay --
// the Augur's reply, or how many troops joined and what they cost -- until
// the player leaves. Shell-only bookkeeping: the engine's flows are unchanged.

#ifndef OB_MODERN_LOCATION_H
#define OB_MODERN_LOCATION_H

#include "game.h"
#include <stdbool.h>

// Just before a temple or dwelling answer is carried out: note the purse and the army.
void loc_deal_begin(const Game *g);
// Just after: `recruited` troops of `troop_id` (0 if none, or for the temple).
void loc_deal_done(const Game *g, int recruited, const char *troop_id);
// A message the engine raised over the screen becomes the in-lay's text.
void loc_deal_absorb(const char *text);
// A result is waiting; the screen waits for Continue.
bool loc_deal_pending(void);
// The screen's panorama always comes first: a message that arrived without the
// player acting (rites already known) waits until they choose the action row.
bool loc_deal_revealed(void);
void loc_deal_reveal(void);
// The panorama's row cursor while such a message waits.
int *loc_deal_cursor(void);
// The in-lay text for the result (message, what joined, the purse before and after).
void loc_deal_text(const Game *g, char *out, int cap);
void loc_deal_clear(void);

#endif
