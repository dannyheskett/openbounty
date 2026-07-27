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

// ---------------------------------------------------------------------------
// The selector's decision logic, factored out of the raylib loop so it can be
// exercised without a window. One call is one frame: the caller reads the
// input edges, hands them over, and reads the updated state back.
// ---------------------------------------------------------------------------

typedef struct PackSelectInput {
    bool close;    // window close signal (frame_host_should_close)
    bool cancel;   // ESC
    bool up;       // UP / KP_8
    bool down;     // DOWN / KP_2
    int  digit;    // 0-based index from a 1..9 keypress, -1 when none
    bool confirm;  // ENTER / KP_ENTER / SPACE
} PackSelectInput;

typedef struct PackSelectState {
    int  cursor;   // highlighted row, 0..n-1
    bool done;     // the loop should stop
    bool quit;     // stopped WITHOUT a selection; the caller loads nothing
} PackSelectState;

// Advance one frame. Closing the window and pressing ESC both end the flow
// as a cancel (done + quit): a dismissed picker must never load a pack the
// user never chose.
void pack_select_step(PackSelectState *st, const PackSelectInput *in, int n);

#endif
