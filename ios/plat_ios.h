// ios/plat_ios.h -- what UIKit tells the game, and what the game asks back.
//
// The app shell (ios_main.mm) is the only writer: it publishes the safe-area
// size, the current touch contact, the active/background state and the frame
// clock. src/frame_host.c and src/input_host.c's iOS halves read them.
//
// Plain C with C linkage, because the readers are C and the writer is
// Objective-C++.

#ifndef OB_PLAT_IOS_H
#define OB_PLAT_IOS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- published by the app shell -------------------------------------------

// The drawable area the game may use, in device pixels, and where it sits in
// the drawable (the safe-area inset). `scale` is the screen's contentScale,
// kept for diagnostics.
void plat_ios_set_screen(int w, int h, int origin_x, int origin_y, float scale);

// One contact, in device pixels relative to the view. `down` false ends it.
void plat_ios_set_touch(bool down, int x, int y);

// Foreground state. The game keeps running while inactive (the loops do not
// care), but audio is paused.
void plat_ios_set_active(bool active);

// One display-link tick. `delta` is the interval the link reports.
void plat_ios_tick(double delta);

// ---- read by the game ------------------------------------------------------

void   plat_ios_screen(int *w, int *h);
bool   plat_ios_touch(int *x, int *y);
bool   plat_ios_active(void);
double plat_ios_time(void);      // seconds since the first tick
double plat_ios_delta(void);     // the last tick's interval

#ifdef __cplusplus
}
#endif

#endif // OB_PLAT_IOS_H
