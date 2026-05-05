#ifndef CHROME_H
#define CHROME_H

#include "game.h"
#include "sprites.h"

// Paint  DOS frame: top / bottom / left / right borders, status
// strip, and the horizontal bar below status. Called once per frame before
// map and sidebar rendering. All drawing happens in the 320x200 design
// target at integer coords.
void chrome_draw(const Game *g, const Sprites *s);

// Combat / sub-screen variant: same chrome (status fill, bar strip,
// frame bitmap) but the status-bar text is supplied by the caller.
// Used by combat to put the active-troop name + move counter in the
// title bar without polluting the adventure-mode status path.
void chrome_draw_with_status(const Game *g, const Sprites *s,
                                     const char *status_text);

#endif
