#ifndef OB_STARTUP_H
#define OB_STARTUP_H

#include <stdbool.h>
#include "game.h"
#include "resources.h"
#include "savepath.h"
#include "sprites.h"

// Result of the pre-game startup flow. One of these branches is always
// populated when startup_flow returns true.
typedef enum {
    STARTUP_LOAD,       // Load existing save from .slot
    STARTUP_NEW,        // Create new game with (class_id, name, diff) into .slot
    STARTUP_QUIT,       // Player pressed ESC at the root (class-select) screen
    STARTUP_BACK,       // Player pressed ESC on a sub-screen; return to class select
} StartupAction;

typedef struct {
    StartupAction action;
    int           slot;          // 0..SAVE_SLOT_COUNT-1
    char          class_id[24];  // new-game only
    char          name[32];      // new-game only
    Difficulty    difficulty;    // new-game only
} StartupChoice;

// Runs the pre-game flow after the splash screens:
//   Modern: the title menu (New Game / Load Saved Game / Credits /
//   Exit); New Game goes to class select, then name entry -> difficulty;
//   Load opens the save picker. ESC steps back toward the title menu.
//   Legacy: class select. Pressing L opens the 10-slot save picker; ESC from
//   the picker returns to class select; otherwise name entry -> difficulty.
// Returns true on success; `out` describes what the caller should do.
// Returns false if the player quit before a decision (also sets
// out->action = STARTUP_QUIT).
//
// Blocks and pumps its own raylib frames. `chrome_target` is the
// 320x200 render texture used for rendering (the caller
// owns it). `res` and `sprites` are read-only.
bool startup_flow(const Resources *res,
                          const Sprites   *sprites,
                          void            *chrome_target,   // RenderTexture2D *
                          StartupChoice   *out,
                          bool             skip_intro);     // no splashes or credits (back to the title)

#endif
