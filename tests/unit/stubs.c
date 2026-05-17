// Stubs for symbols that live in src/main.c. Test binary excludes
// main.c (it has its own main), so the few symbols other game code
// references from main need stand-ins here.

#include <stdbool.h>

// src/main.c:73 — exposes the fast-quit prompt state to chrome /
// state_serialize. Tests never enter the fast-quit flow.
bool main_fast_quit_active(void) { return false; }

// Combat shell-only functions. When OB_HEADLESS is defined the
// rendered combat loop in src/combat.c is omitted, but src/harness.c
// still references combat_set_auto_player / combat_auto_player. Stub
// them out for the engplay build.
#ifdef OB_HEADLESS
void combat_set_auto_player(bool on) { (void)on; }
bool combat_auto_player(void) { return false; }
// Render-side combat function used by harness/state_serialize that
// doesn't apply headlessly. CombatResult is in combat.h but we don't
// need to pull it just for a stub.
#endif
