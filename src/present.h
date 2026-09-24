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

#include "ob_types.h"
#include "gfx.h"
#include <stdbool.h>

// Re-fit the layout to the window and, if the design-space screen size changed,
// reallocate `*rt` to match. Returns true when the target was replaced.
//
// Modern derives the buffer FROM the window, so the buffer is stale the moment
// the window is resized. Every loop that owns a render target calls this at
// the top of its frame -- not just the main one.
//
// A declared buffer (CL_IS_NATIVE) takes the whole surface -- the window less
// its safe-area insets -- on every screen, at the largest whole zoom the
// declared buffer fits (layout_grow_native). Legacy geometry is fixed, so
// layout_fit_window is a no-op and this always returns false.
bool present_refit(RenderTexture2D *rt);
// The last present_refit changed the screen: a tap made on the frame before
// aimed at a layout that is gone (touch.c drops it).
bool present_layout_changed(void);

// The size the render target should be for this window: the screen size, or
// for a fixed buffer (CL_IS_NATIVE) the screen times the presentation scale,
// so the frame is RENDERED at zoom and text rasterised at that zoom lands
// sharp. Pure arithmetic, testable without a window.
void present_target_size(int win_w, int win_h, int *w, int *h);

// The zoom the current target is rendered at: the presentation scale for a
// fixed buffer, 1 for everything else. Anything that works in framebuffer
// pixels rather than design pixels (a scissor rect) multiplies by this.
int  present_get_zoom(void);

// Begin/end drawing a frame into the target. For a fixed buffer this wraps
// the drawing in a camera at the zoom, so every draw call keeps its design
// coordinates and the art comes out pixel-identical to the integer blit it
// replaces. Legacy is a plain BeginTextureMode/EndTextureMode.
void present_begin(RenderTexture2D *rt);
void present_end(void);

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
// actually rendered. For a fixed buffer (CL_IS_NATIVE) it is the largest of
// 1x, 2x, 3x whose whole buffer fits the window.
int  present_max_scale(int win_w, int win_h);


// Begin the frame and blit `rt` to the window, integer-scaled and centred,
// with black letterbox around it. Takes the render texture BY VALUE; most
// callers hold a RenderTexture2D* and pass *ptr.
//
// This includes gfx_frame_begin() + gfx_clear(BLACK), because every call
// site did exactly those two calls immediately before its blit. Callers still
// own what happens AFTER -- frame_host_end_frame(), screenshot_tick(), and so
// on differ between them.
void present_scaled(RenderTexture2D rt);

// Map a window-pixel position to design-space pixels, using the dst rect the
// last present_scaled call actually blitted to. Returns false outside the
// game area (in the letterbox). Pure arithmetic over stored values, so the
// mapping is testable without a window.
bool present_window_to_screen(int wx, int wy, int *sx, int *sy);

// A length in window pixels (a touch unit, a finger's slack) in design-space
// pixels at the last present_scaled's blit: the length itself before any.
int present_window_len_to_design(int len);

// The letterboxed blit rect from the last present_scaled call, in window
// pixels. The touch layer lays its chrome out around this.
void present_last_dst(int *x, int *y, int *w, int *h);

// Record a blit rect drawn at a whole `scale`, for present_window_to_screen:
// public so tests can exercise the mapping without a window.
void present_store_dst(int x, int y, int w, int h, int scale);

// The largest whole number of times a dst_w x dst_h frame fits inside a
// safe_w x safe_h area, at least 1. The mobile presentation multiple; see
// present.c.
int present_fit_multiple(int dst_w, int dst_h, int safe_w, int safe_h);

// A w x h frame too big for a room_w x room_h area, shrunk to fit it with its
// shape kept: true and the fitted size when it had to shrink, false (and the
// size unchanged) when it already fits.
bool present_fit_down(int w, int h, int room_w, int room_h, int *out_w, int *out_h);

#endif
