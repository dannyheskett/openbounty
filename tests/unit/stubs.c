// Stubs for symbols that live in src/main.c. Test binary excludes
// main.c (it has its own main), so the few symbols other game code
// references from main need stand-ins here.

#include <stdbool.h>

// src/main.c:73 — exposes the fast-quit prompt state to chrome /
// state_serialize. Tests never enter the fast-quit flow.
bool main_fast_quit_active(void) { return false; }

