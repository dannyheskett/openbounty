// autoplay/worldsnap.h
//
// Full-world snapshot / restore: the cleanliness primitive that lets plan
// construction cache a last-good state and replay only the new tail.
//
// "The world" spans four values:
//   - the Game struct          (flat, value-copyable; its only pointer,
//                               `const Resources *res`, is read-only / shared)
//   - the active Map           (flat; all Tiles inline, zero owned pointers)
//   - the active Fog           (flat bool grid). Fog is presentation state,
//                               but a ZONE SWITCH snapshots it INTO the Game
//                               (world.continent_fog), so simulations must
//                               evolve fog exactly as the live replay will or
//                               the post-switch boundary fingerprints diverge.
//   - the single world-RNG global (a uint64_t behind GameRngSnapshot/Restore)
//
// Because all three are flat value state with no owned heap, capture/restore is
// a plain value copy and is provably BIT-IDENTICAL: restore returns the world
// indistinguishable from the snapshot instant, with no residual mutation. This
// is the absolute-cleanliness guarantee that licenses incremental replay;
// it is proven by the round-trip test (tests/autoplay/test_worldsnap.c).
//
// Engine-pure: links libobengine + engine headers only (no raylib, no src/).

#ifndef AUTOPLAY_WORLDSNAP_H
#define AUTOPLAY_WORLDSNAP_H

#include <stdint.h>

#include "game.h"
#include "map.h"
#include "fog.h"

// A captured world value. Holds full copies of Game, Map, and Fog plus the
// world-RNG position. No owned heap; copy/embed by value freely (the planner
// caches one of these after each admitted objective).
typedef struct {
    Game     game;   // value copy; game.res is the shared read-only pointer
    Map      map;    // value copy; all tiles inline
    Fog      fog;    // value copy; flat seen[][] grid
    uint64_t rng;    // GameRngSnapshot() at capture
} WorldSnapshot;

// Capture the whole world (Game + Map + Fog + world-RNG) into *out. Reads
// only; does not mutate g, m, fog, or the RNG.
void worldsnap_capture(WorldSnapshot *out, const Game *g, const Map *m,
                       const Fog *fog);

// Restore the whole world from *snap into *g, *m, *fog, and the world-RNG.
// After this all four are bit-identical to the capture instant.
void worldsnap_restore(const WorldSnapshot *snap, Game *g, Map *m, Fog *fog);

// FNV-1a fingerprint over the whole world byte image (Game + Map + world-RNG).
// The executor's step-boundary checkpoint (WS-4): recompute on the live
// world and assert it equals the recorded boundary_fp; any mismatch is a
// determinism regression. Reads only.
uint64_t worldsnap_fingerprint(const Game *g, const Map *m);

// Same fingerprint computed from a captured snapshot (using the snapshot's own
// RNG). Used by plan_build to record a step's boundary_fp from the proven prefix.
uint64_t worldsnap_fingerprint_snap(const WorldSnapshot *snap);

#endif // AUTOPLAY_WORLDSNAP_H
