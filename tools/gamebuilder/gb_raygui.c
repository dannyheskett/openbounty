// raygui implementation, isolated in its own translation unit.
//
// Vendored third-party code (third_party/raygui, zlib, Ramon Santamaria).
// Kept separate so its warnings do not force us to relax -Wall -Wextra on
// GameBuilder's own sources.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#pragma GCC diagnostic pop
