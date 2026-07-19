#ifndef OB_PACK_SELECT_H
#define OB_PACK_SELECT_H

#include "pack.h"

// Run a minimal pre-window pack selector. Opens a tiny raylib window,
// shows a numbered list of pack names, blocks until the user picks one
// (Enter/digit) or quits (ESC). Returns the chosen index in *chosen,
// or false if the user cancelled.
//
// This runs BEFORE the main game pack is opened -- so it cannot use
// bfont (which needs a pack), the chrome, or any sprites. It
// uses raylib's built-in DrawText.
bool pack_select_flow(const PackEntry *list, int n, int *chosen);

#endif
