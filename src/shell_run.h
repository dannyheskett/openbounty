#ifndef OB_SHELL_RUN_H
#define OB_SHELL_RUN_H

#include <stdbool.h>

// Run the game: open the pack, load assets, run the startup flow,
// enter the main loop.
//
// argc/argv are passed in so the CLI knobs (--seed, --pack,
// --fullscreen, --movie, --save-dir) work.
//
// Returns the process exit code: 0 on success, non-zero on a setup error.
int shell_run_game(int argc, char **argv);

#endif
