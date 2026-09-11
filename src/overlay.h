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

// Modern: darken the chrome interior (map pane and sidebar) under a detail
// view, prompt or dialog, by the pack's render.dim percent (REQ-430g). A
// no-op in legacy and at 0. Combat calls it over the battlefield before its
// own panels.
void overlay_dim_scene(void);

// The alpha byte for a dim percent, clamped to 0..100. Pure.
int  overlay_dim_alpha(int percent);

#endif
