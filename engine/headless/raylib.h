// engine/headless/raylib.h
//
// Headless build bridge. Translation units compiled with -Iengine/headless
// on the include path -- the engine archive, the autoplay and demo objects,
// and the library-boundary check (see the Makefile's ENGLIB_CFLAGS,
// AUTOPLAY_CFLAGS, DEMO_CFLAGS and LIBTEST_CFLAGS) -- resolve their
// `#include "raylib.h"` to the stub below instead of to real raylib.
//
// The shell (src/) is built with the real raylib include path and never
// sees this file, which is what keeps the engine raylib-free by
// construction.

#ifndef OB_RAYLIB_STUB_SHIM
#define OB_RAYLIB_STUB_SHIM 1

#include "raylib_stub.h"

#endif
