// engine/raylib.h
//
// Headless build bridge. When the playtest binary builds with -Iengine
// in front of -Ithird_party/raylib-install/include, this file resolves
// the `#include "raylib.h"` directives in src/ to the stub instead of
// real raylib.
//
// Game binary builds with -Iengine LAST (or not at all), so real raylib
// wins for those.
//
// Sanity check: the stub must define a sentinel so unit-test builds can
// verify they're getting the right header.

#ifndef OB_RAYLIB_STUB_SHIM
#define OB_RAYLIB_STUB_SHIM 1

#include "raylib_stub.h"

#endif
