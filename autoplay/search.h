// autoplay/search.h
//
// The snapshot-tree search (AP-200): THE oracle, run once from the boot
// state. It expands a tree of reached world states -- ONE attempt per
// expansion, driven through the planner step core (planner.h) -- hunting any
// full clear within the day budget. Greedy is not a separate stage: it is the
// tree's first descent, taken by following the step core's own candidate
// order. Frontier nodes carry the world snapshot and recording delta captured
// when they were created, so visiting one is an O(1) restore; a node is freed
// the moment its branch is discarded (exhausted, looped, beam-evicted, or the
// search ends), bounding memory by the live frontier.

#ifndef OB_AUTOPLAY_SEARCH_H
#define OB_AUTOPLAY_SEARCH_H

#include "exec.h"
#include "goals.h"

// Run THE search from ctx's current state: seek ANY full clear within the
// remaining day budget. Returns true on a HIT -- the winning node's world and
// recording are left committed (the caller reports the win and replays the
// recording). Returns false when the DECLARED EXPANSION SET is exhausted
// (AP-203 names the bounds): the MOST-ADVANCED node's world, recording, and
// [UNMET] causes are committed instead -- the truthful "how close" state.
// Either way the committed state is one the search actually reached; nothing
// is re-simulated to produce it. *out_best_done and *out_total (either may be
// NULL) receive the progress counts for the caller's one verdict line.
bool search_run(ExecCtx *ctx, int *out_best_done, int *out_total);

#endif
