#ifndef OB_OVERLAY_H
#define OB_OVERLAY_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "sprites.h"

// Draws whatever modal/overlay state is currently active on top of the
// classic map + chrome. Handles dialogs, menus, town menu, views, toasts.
// Returns after a single frame; no state of its own. Called last in the
// draw pipeline.
void overlay_draw(const Game *g, const Map *m, const Fog *f,
                          const Sprites *s);

// Render just the dialog box (if active). Used by combat to draw the
// victory dialog inside the offscreen texture so it scales with the
// battlefield. Caller is responsible for the active drawing surface.
// The _centered variant : a 36-col x 16-row
// modal centered on screen rather than the bottom-frame KB_BottomBox.
void overlay_draw_dialog(void);
void overlay_draw_dialog_centered(void);

// Number of pages the current dialog body wraps to in the bottom panel.
// The pager uses this so its page count matches what the renderer displays.
int overlay_dialog_page_count(void);

#endif
