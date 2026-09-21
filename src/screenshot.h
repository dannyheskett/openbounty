#ifndef OB_SCREENSHOT_H
#define OB_SCREENSHOT_H

#include "gfx.h"

#if !defined(PLATFORM_IOS)

// Call once per frame, AFTER gfx_frame_end(). If the user has pressed ` (backtick)
// this frame, exports `rt`'s texture to screenshots/<prefix>_NNNN.png using the
// next free sequence number. Native 320x200, vertically flipped so the PNG
// matches what was on screen.
void screenshot_tick(RenderTexture2D rt, const char *prefix);

// Save `rt` immediately, regardless of key state. Used for auto-capture on
// specific triggers (e.g., the VIEW_CHARACTER rising edge).
void screenshot_save(RenderTexture2D rt, const char *prefix);

#else

// iOS has no screenshot key and no writable screenshots/ directory, so
// src/screenshot.c (raylib + stdio) is not in that build at all. The call
// sites stay where they are and compile to nothing.
static inline void screenshot_tick(RenderTexture2D rt, const char *prefix) {
    (void)rt; (void)prefix;
}
static inline void screenshot_save(RenderTexture2D rt, const char *prefix) {
    (void)rt; (void)prefix;
}

#endif

#endif
