// autoplay/autoplay.h
//
// Public API for autoplay mode — the pack-winnability oracle described in
// docs/OPENBOUNTY-SPEC.md §36. Autoplay is a development / QA tool: it drives a
// competent automated player to demonstrate that a gamepack is winnable from
// a given seed.
//
// This header is engine-only. The autoplay module links libobengine.a +
// engine headers and NOTHING from src/ (the raylib shell). The headless
// runner that exercises this API is verified engine-pure by the
// library-boundary check.
//
// This header is the headless ENTRY: autoplay() boots a game on the given seed,
// runs planner() to a verdict, and reports. The decision stack behind it —
// planner() -> executor (execute()) -> engine — lives in planner.h / exec.h; this
// header exposes only the run entry plus its config / result / verdict.

#ifndef OB_AUTOPLAY_H
#define OB_AUTOPLAY_H

#include <stdint.h>
#include <stdbool.h>

// Engine headers (engine-only, no src/ — keeps autoplay engine-pure). Map/Fog/
// Resources are anonymous-struct typedefs, so they must be the real declarations.
#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

// One run terminates with exactly one verdict. A crash / illegal
// engine state is a separate failure class handled out of band (process
// abort), not represented here.
typedef enum {
    AUTOPLAY_VERDICT_CLEARED = 0,  // all in-scope objectives met
    AUTOPLAY_VERDICT_FAILED,       // a committed objective proved unwinnable
                                   // (a pack defect)
    AUTOPLAY_VERDICT_PARTIAL,      // planner exhausted admissible candidates;
                                   // some objectives unaddable but nothing
                                   // committed is unwinnable (NOT a pack
                                   // defect — "as far as the planner
                                   // got")
    AUTOPLAY_VERDICT_NOOP,         // Phase 0: ran, did nothing, reported state
} AutoplayVerdict;

// Inputs to a run. Kept deliberately small for M1; grows with later phases.
typedef struct {
    uint64_t seed;       // REQUIRED, non-zero. Autoplay must always pass an
                         // explicit seed so GameInit never falls back to
                         // time(NULL). seed == 0 is rejected.
    const char *pack_dir;// pack to load (e.g. "assets/kings-bounty"). When
                         // NULL the runner uses its built-in default.
    int  zone_scope;     // objective-universe scope: 0..3 = enumerate only that
                         // zone (0 = the starting zone, the permanent regression
                         // baseline and the current default); -1 = enumerate all
                         // zones (multi-zone runs). Zero-init keeps the legacy
                         // single-zone behavior.
} AutoplayConfig;

// Outputs from a run.
typedef struct {
    AutoplayVerdict verdict;
    uint64_t        seed;        // echoed back — the seed to reproduce a run.
    int             turns;       // adventure turns taken (0 in Phase 0).
    // Objective-checklist counters (filled in as phases land). Phase 0
    // leaves these zero.
    int objectives_total;
    int objectives_done;
} AutoplayResult;

// Run autoplay headlessly to a verdict. Returns true if the run executed to a
// definitive verdict (CLEARED / FAILED / NOOP) and *out is populated; returns
// false only on setup failure (bad config, pack load failure) with *out
// undefined. This is the entry the headless runner calls.
bool autoplay(const AutoplayConfig *cfg, AutoplayResult *out);

// Human-readable name for a verdict (for the report line).
const char *autoplay_verdict_str(AutoplayVerdict v);


#endif
