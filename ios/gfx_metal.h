// ios/gfx_metal.h -- the handful of entry points the app shell (ios_main.mm)
// needs from the Metal backend. Everything the GAME calls is in src/gfx.h;
// this is only the wiring UIKit has to do.

#ifndef OB_GFX_METAL_H
#define OB_GFX_METAL_H

#ifdef __OBJC__
@class CAMetalLayer;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Hand the backend the layer it draws into. Called once, from the view's
// didMoveToWindow.
#ifdef __OBJC__
void gfx_metal_attach(CAMetalLayer *layer);
#endif

// The drawable's size in device pixels, and the origin the game's (0,0) maps
// to -- the safe-area inset, so the frame never sits under the notch or the
// home indicator. Called on every layout change.
void gfx_metal_set_viewport(int width, int height, int origin_x, int origin_y);

// True once a device and pipeline exist. Nothing draws before that.
bool gfx_metal_ready(void);

#ifdef __cplusplus
}
#endif

#endif // OB_GFX_METAL_H
