// autoplay/planner.h
//
// THE PLANNER — the layer directly under autoplay() and directly above the
// Executor (execute()). It decides WHICH primitives to carry out and IN WHAT
// ORDER to meet the objectives; it never touches the engine itself. For each
// objective it builds one Primitive and hands it to execute(), which solves the
// primitive into engine actions (movement via nav(), interaction at the tile).
//
// The search is the backtracking forward-simulation described in primitives.h:
// try a candidate primitive on the live (throwaway) Game, KEEP it on success
// (the Game is now advanced and the recorded engine prims are appended), or
// RESTORE the pre-step snapshot and try an alternative on failure. The planner
// is the ONLY component that snapshots/restores and that orders work; execute()
// + nav() are pure downward calls.
//
// STATUS: first increment. execute() currently carries out the FETCH group
// (chest / artifact / navmap / orb — all consumed by GameStep on arrival) on
// nav(); the remaining target primitives and the ARMY contract are not wired yet
// and return false, so the planner admits the consumables and reports the rest
// as unadmitted (an honest PARTIAL). The loop is structured so the other
// primitives slot in with no change to the search.
//
// LAYER: planner() is called only by autoplay(). It calls only execute() (and
// the goal enumerators / worldsnap, which are pure helpers).

#ifndef OB_AUTOPLAY_PLANNER_H
#define OB_AUTOPLAY_PLANNER_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "goals.h"        // GoalLog / PlanStep
#include "recording.h"    // RecBuf / CombatRecList
#include "autoplay.h"     // AutoplayVerdict

// The outcome of a planner() run: the ordered admitted objectives, the recording
// they emit (replayable on a fresh boot), and the verdict + objective tally.
typedef struct {
    GoalLog         plan;       // ordered admitted objectives (what was carried out)
    RecBuf          rec;        // recorded engine prims (REC_MOVE / RA_*) in order
    CombatRecList   combats;    // per-fight combat records (none until SIEGE/SLAY land)
    AutoplayVerdict verdict;    // CLEARED (all met) / PARTIAL (some unadmitted)
    int             obj_total;  // objectives enumerated in scope
    int             obj_done;   // objectives the planner carried out
} PrimRun;

// Plan over the in-scope objectives, driving execute()/nav() forward on `g`.
// `zone_scope` mirrors the autoplay config (currently only the hero's zone is
// enumerated; multi-zone scope is a follow-up). MUTATES g / map / fog (it leaves
// them at the terminal state of the last admitted primitive). Fills `out`.
// Returns false only on a setup failure (null args / enumeration error).
bool planner(Game *g, Map *map, Fog *fog, const Resources *res,
             int zone_scope, PrimRun *out);

// Release the heap owned by a PrimRun (plan items, recording, combat records).
void primrun_free(PrimRun *r);

#endif
