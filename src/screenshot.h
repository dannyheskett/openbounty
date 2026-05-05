#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include "raylib.h"

// Call once per frame, AFTER EndDrawing(). If the user has pressed ` (backtick)
// this frame, exports `rt`'s texture to screenshots/<prefix>_NNNN.png using the
// next free sequence number. Native 320x200, vertically flipped so the PNG
// matches what was on screen.
void screenshot_tick(RenderTexture2D rt, const char *prefix);

// Save `rt` immediately, regardless of key state. Used for auto-capture on
// specific triggers (e.g., the VIEW_CHARACTER rising edge).
void screenshot_save(RenderTexture2D rt, const char *prefix);

#endif
