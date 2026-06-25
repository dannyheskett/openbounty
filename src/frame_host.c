#include "frame_host.h"
#include "raylib.h"

// Thin pass-through wrappers around raylib time/window calls. (Previously
// this file also hosted a deterministic test clock with wall-clock pacing
// and a before-frame hook; those existed only for the autoplay driver and
// have been removed.)

double frame_host_time(void)        { return GetTime(); }
bool   frame_host_should_close(void) { return WindowShouldClose(); }
