#ifndef OB_SHELL_RUN_H
#define OB_SHELL_RUN_H

#include <stdbool.h>

// These types are typedef'd anonymously by the engine headers, so a
// forward `struct X` isn't compatible with them. Pull the real
// headers in.
#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

// Per-frame hook verdict. CONTINUE = keep the main loop running.
// EXIT_PASS / EXIT_FAIL = stop the loop and report the verdict to the
// caller via shell_run_game's return value (0 on PASS, 1 on FAIL).
typedef enum {
    SHELL_RUN_CONTINUE  = 0,
    SHELL_RUN_EXIT_PASS = 1,
    SHELL_RUN_EXIT_FAIL = 2,
} ShellRunVerdict;

typedef struct ShellRunHooks ShellRunHooks;
struct ShellRunHooks {
    // Called once before startup_flow runs (i.e. before the splash /
    // pack-picker / new-game wizard). Use this to pre-queue startup
    // input. Optional.
    void (*before_startup)(ShellRunHooks *self);

    // Called once after GameInit completes and the starting zone has
    // been loaded, but BEFORE the main loop's first iteration. Use
    // this to pre-queue overworld input. Optional.
    void (*after_init)(ShellRunHooks *self,
                       Game *g, Map *m, Fog *f, Resources *res);

    // Called at the top of every main-loop iteration (before input
    // poll). The hook may queue input and/or call input_host_tick /
    // frame_host_tick. Return CONTINUE to keep the loop running, or
    // EXIT_PASS/EXIT_FAIL to terminate with that verdict. Optional —
    // NULL means "always continue".
    ShellRunVerdict (*per_frame)(ShellRunHooks *self,
                           Game *g, Map *m, Fog *f, Resources *res,
                           int frame_no);

    // Scenario-private state. The hook implementations cast this to
    // their own struct.
    void *user;
};

// Run the game: open the pack, load assets, run the startup flow,
// enter the main loop. With NULL hooks this is exactly what main()
// did before the refactor. With non-NULL hooks, the per_frame
// callback can short-circuit the loop and report a verdict.
//
// argc/argv are passed in so existing CLI knobs (--seed, --pack,
// --fullscreen, --movie, --save-dir) still work in scenarios.
//
// Returns the process exit code: 0 PASS, 1 FAIL (or any other
// non-zero error from setup).
int shell_run_game(int argc, char **argv, ShellRunHooks *hooks);

#endif
