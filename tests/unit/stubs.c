// Stubs for symbols that live in src/main.c. Test binary excludes
// main.c (it has its own main), so the few symbols other game code
// references from main need stand-ins here.

#include <stdbool.h>

// src/main.c:73 — exposes the fast-quit prompt state to chrome /
// state_serialize. Tests never enter the fast-quit flow.
bool main_fast_quit_active(void) { return false; }

// Combat shell-only functions. src/harness.c references the auto-
// player toggle which lives in src/combat_loop.c. The unit-test build
// links combat_loop.c so the real symbols resolve; the engplay build
// excludes combat_loop.c and gets these stubs.
#ifdef OB_HEADLESS
void combat_set_auto_player(bool on) { (void)on; }
bool combat_auto_player(void) { return false; }
#endif
