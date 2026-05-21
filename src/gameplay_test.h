#ifndef OB_GAMEPLAY_TEST_H
#define OB_GAMEPLAY_TEST_H

// Gameplay test runner. A gameplay test drives the real game binary
// through a scripted sequence of input events and asserts on
// observable Game state. It exercises the full stack (windows,
// screens, prompts, rendered combat) — unlike the headless engine
// tests in tests/{unit,regression,e2e}/ which call engine functions
// directly.
//
// Each scenario is a function: int gp_scenario_fn(void). It returns
// the process exit code (0 = PASS, non-zero = FAIL). Scenarios are
// registered below; main.c looks up by name when --gameplay-test
// <name> is passed.
//
// Scenarios drive the game through the input_host + frame_host
// shims, which let the runner script keys and step frames
// deterministically.

typedef int (*gp_scenario_fn)(int argc, char **argv);

typedef struct {
    const char     *name;
    const char     *description;
    gp_scenario_fn  fn;
} GpScenario;

// Look up a registered scenario by name. Returns NULL if not found.
const GpScenario *gp_scenario_find(const char *name);

// Print "name -- description" for every registered scenario.
void gp_scenario_list(void);

// ----- Runner helpers (used by scenarios) ---------------------------------

// One-time setup: switches input + frame hosts to test mode.
// Idempotent.
void gp_runner_init(void);

#endif
