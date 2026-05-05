// Dedicated TU for minimp4's implementation. Symbols collide with
// minih264 if both are compiled together, so each lives in its own .c.
//
// Vendored upstream code triggers warnings under our -Wall -Wextra.
// Suppressed here so the project's own code stays warning-clean.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
#pragma GCC diagnostic ignored "-Wint-conversion"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#pragma GCC diagnostic pop
