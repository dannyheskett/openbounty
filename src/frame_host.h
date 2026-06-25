#ifndef OB_FRAME_HOST_H
#define OB_FRAME_HOST_H

#include <stdbool.h>

// Frame / time host shim. Thin wrappers around the raylib time/window
// calls used by game logic, so callers don't include raylib directly.

double frame_host_time(void);          // GetTime equivalent
bool   frame_host_should_close(void);  // WindowShouldClose equivalent

#endif
