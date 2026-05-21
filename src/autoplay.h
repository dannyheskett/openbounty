#ifndef OB_AUTOPLAY_H
#define OB_AUTOPLAY_H

// Autoplay: drives the real game binary through a scripted input
// sequence and asserts on observable Game state. Exercises the full
// stack (windows, screens, prompts, rendered combat) — unlike the
// headless engine tests in tests/{unit,regression,e2e}/ which call
// engine functions directly.
//
// CLI:
//   ./build/openbounty --seed 1 --autoplay           (headless run)
//   ./build/openbounty --seed 1 --autoplay --visible (paced to be
//                                                    watchable)
//
// Returns the process exit code: 0 on success (the run completed
// and the end-state assertion held), non-zero on failure.

int autoplay_run(int argc, char **argv);

#endif
