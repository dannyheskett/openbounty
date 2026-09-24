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
// Draw the open note as the kind it was raised as (player_io.h).
void overlay_draw_note(void);

// Number of pages the current dialog body wraps to in the bottom panel.
// The pager uses this so its page count matches what the renderer displays.
int overlay_dialog_page_count(void);

// The rect a location screen (home castle, own castle, dwelling, recruit)
// draws its text panel into, and the padding inside it: the bottom panel rect
// legacy has always used. Modern draws its places itself (src/modern/overlay.c).
void screens_text_rect(int *x, int *y, int *w, int *h);
int  screens_text_pad(void);

// The alpha byte for a dim percent, clamped to 0..100. Pure.
int  overlay_dim_alpha(int percent);

#endif
