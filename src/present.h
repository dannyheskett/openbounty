// src/present.h
//
// The one place the 320x200 render target becomes window pixels.
//
// This blit used to be copy-pasted, identically, in seven files (the main
// loop, startup, combat, the ending cartoon, visible autoplay, the encode
// dialog, and the frame dispatcher). Capping the scale meant changing all
// seven, so it lives here instead: one implementation, one set of bounds.

#ifndef OB_PRESENT_H
#define OB_PRESENT_H

#include "raylib.h"
#include <stdbool.h>

// Re-fit the layout to the window and, if the design-space screen size changed,
// reallocate `*rt` to match. Returns true when the target was replaced.
//
// Modern mode derives the buffer FROM the window, so the buffer is stale the
// moment the window is resized: the frame is then drawn at the old size and
// present_scaled either crops it or leaves a border. Every loop that owns a
// render target has to call this at the top of its frame -- not just the main
// one. The startup loop not calling it is why a resized window clipped the
// class-select screen top and bottom.
//
// Legacy geometry is fixed, so layout_fit_window is a no-op and this always
// returns false.
bool present_refit(RenderTexture2D *rt);

// The integer scale to blit at, for a window of this size. Pure arithmetic --
// no GL, no window -- so it is testable on its own.
//
// LEGACY auto-fits, exactly as it did before render modes existed: the largest
// whole multiple of 320x200 that fits, floored at CL_SCALE_MIN and capped at
// CL_SCALE_MAX (CL_SCALE_MAX_WEB on web).
//
// MODERN does not auto-fit. The window decides how much you SEE -- resize it
// and the viewport gains or loses whole tiles -- while the scale decides how
// big a PIXEL is. At 1x, the default, one buffer pixel is one screen pixel and
// the pack renders at the resolution it was authored for. Higher scales exist
// for a 4K or 8K panel where 1:1 is too small; they never resize the window.
int present_scale(int win_w, int win_h);

// The player's chosen scale, in whole pixels, floored at 1. Not persisted: a
// per-machine viewing preference, not pack data and not game state.
void present_set_scale(int scale);
int  present_get_scale(void);

// Largest scale this window can show without dropping below the minimum
// viewport. The Scale menu wraps here, so the label always matches what is
// actually rendered.
int  present_max_scale(int win_w, int win_h);

// Begin the frame and blit `rt` to the window, integer-scaled and centred,
// with black letterbox around it. Takes the render texture BY VALUE; most
// callers hold a RenderTexture2D* and pass *ptr.
//
// This includes BeginDrawing() + ClearBackground(BLACK), because every call
// site did exactly those two calls immediately before its blit. Callers still
// own what happens AFTER -- frame_host_end_frame(), screenshot_tick(), and so
// on differ between them.
void present_scaled(RenderTexture2D rt);

// Map a window-pixel position to design-space pixels, using the dst rect the
// last present_scaled call actually blitted to. Returns false outside the
// game area (in the letterbox). Pure arithmetic over stored values, so the
// mapping is testable without a window.
bool present_window_to_screen(int wx, int wy, int *sx, int *sy);

// The letterboxed blit rect from the last present_scaled call, in window
// pixels. The touch layer lays its chrome out around this.
void present_last_dst(int *x, int *y, int *w, int *h);

// Record the blit rect + scale the mapping above reads. Called by
// present_scaled with what it actually drew; public so tests can exercise
// present_window_to_screen without a window.
void present_store_dst(int x, int y, int w, int h, int scale);

#endif
