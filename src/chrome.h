#ifndef OB_CHROME_H
#define OB_CHROME_H

#include "game.h"
#include "sprites.h"

// Paint the screen frame: top / bottom / left / right borders, status
// strip, and the horizontal bar below status. Called once per frame
// before map and sidebar rendering. All drawing happens in the 320x200
// design target at integer coords. A modern declared buffer has no status
// strip: the frame, and the band between each column and the map.
void chrome_draw(const Game *g, const Sprites *s);

// The frame alone on black, no column bands: the battle, which lays its own
// column and field inside the ring (src/modern/page.c page_combat).
void chrome_draw_ring(const Game *g, const Sprites *s);

// Combat / sub-screen variant: same chrome (status fill, bar strip,
// frame bitmap) but the status-bar text is supplied by the caller.
// Used by legacy combat to put the active-troop name + move counter in the
// title bar without polluting the adventure-mode status path. A modern
// declared buffer has no band: the frame alone.
void chrome_draw_with_status(const Game *g, const Sprites *s,
                                     const char *status_text);

#endif
