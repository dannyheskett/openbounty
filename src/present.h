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

// The integer scale used for a window of this size: the largest whole
// multiple of 320x200 that fits, clamped to [CL_SCALE_MIN, ceiling], where
// the ceiling is CL_SCALE_MAX_WEB on web and CL_SCALE_MAX elsewhere
// (layout.h). Pure arithmetic -- no GL, no window -- so it is testable on its
// own.
int present_scale(int win_w, int win_h);

// Begin the frame and blit `rt` to the window, integer-scaled and centred,
// with black letterbox around it. Takes the render texture BY VALUE; most
// callers hold a RenderTexture2D* and pass *ptr.
//
// This includes BeginDrawing() + ClearBackground(BLACK), because every call
// site did exactly those two calls immediately before its blit. Callers still
// own what happens AFTER -- frame_host_end_frame(), screenshot_tick(), and so
// on differ between them.
void present_scaled(RenderTexture2D rt);

#endif
