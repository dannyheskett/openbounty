#ifndef OB_AUTOPLAY_PRIMITIVES_H
#define OB_AUTOPLAY_PRIMITIVES_H

// The high-level primitive vocabulary the planner sequences to achieve objectives.
// This is the streamlined replacement for the 20-section per-tick autoplay_step
// machine: the planner emits an ordered list of these, and a single test()/execute
// pass turns each into the recorded engine primitives (recording.h).
//
// Two design rules, aligned to the game's mechanics:
//   1. TARGET primitives name a GAME OBJECT (a tile in a zone). Reaching it —
//      including cross-zone SAILING and clearing any path-blocking foe — is IMPLICIT
//      in the nav layer. The planner NEVER sequences travel or "clear this foe"
//      explicitly; it just says "FETCH the chest at (x,y) in zone Z" and the nav
//      layer figures out foot + boat + fight-through.
//   2. ARMY(comp) is the ONLY recruiting verb the planner emits. It is a CONTRACT:
//      the acquirer fully assembles `comp` (visiting whatever dwellings / the home
//      gate it takes, sailing if needed) or fails — never a per-tick re-decision.

#include <stdbool.h>
#include "goals.h"       // PlanStep (the objective the planner hands to execute)
#include "recruit.h"     // ArmyTarget (the committed composition for PRIM_ARMY)
#include "recording.h"   // RecSink (where execute appends the replayable prims)

typedef enum {
    // ---- TARGET primitives: reach the object (nav incl. sail + path-clear), interact
    PRIM_FETCH = 0, // pick up a consumable at (x,y): chest / artifact / navmap / orb
    PRIM_TOWN,      // visit a town and perform `service`
    PRIM_DWELL,     // recruit `count` of troop `id` at its dwelling (x,y)
    PRIM_HOME,      // the home (audience) castle: recruit the pool, or garrison
    PRIM_SIEGE,     // assault a monster-castle / villain castle (id); garrison on win
    PRIM_SLAY,      // engage a hostile wandering foe / path-blocker (id at x,y)
    PRIM_DIG,       // the buried-scepter finale (the WIN) at (x,y)
    PRIM_LEARN,     // an alcove: pay gold, learn magic, at (x,y)
    // ---- STATE primitives: no map target
    PRIM_ARMY,      // make the held army == `army` (the recompose contract)
    PRIM_CAST,      // apply an adventure spell `id` (time-stop, leadership boost, ...)
    PRIM_WAIT,      // bank `count` weeks of commission / let dwellings restock
    PRIM_KIND_COUNT
} PrimKind;

// What a TOWN visit transacts (entering a town is identical; only this differs).
typedef enum {
    TOWN_CONTRACT = 0,  // GameTakeNextContract, cycling to villain `id`
    TOWN_SPELL,         // buy the combat spell on sale
    TOWN_BOAT,          // rent a boat toward `id` (destination zone)
    TOWN_SIEGE,         // buy siege weapons
} TownService;

// What a HOME visit does.
typedef enum {
    HOME_RECRUIT = 0,   // GameBuyTroop over the whole home pool
    HOME_GARRISON,      // garrison the weakest surviving stack
} HomeService;

// One planned primitive. Only the fields relevant to `kind` are meaningful.
typedef struct {
    PrimKind    kind;
    char        zone[24];   // the object's zone; nav sails here if not current.
                            // empty => current zone / not zone-bound (state prims).
    int         x, y;       // the object's tile (target primitives); -1 if N/A.
    char        id[32];     // object / troop / villain / spell id (kind-dependent).
    int         count;      // PRIM_DWELL troop count; PRIM_WAIT weeks; PRIM_CAST index.
    TownService town;       // PRIM_TOWN service.
    HomeService home;       // PRIM_HOME service.
    ArmyTarget  army;       // PRIM_ARMY target composition (the contract).
} Primitive;

// ---------------------------------------------------------------------------------
// THE SEARCH MODEL (the whole point):
//
// The planner builds the plan ONE objective at a time and validates by simulation,
// BACKTRACKING when a step fails:
//
//   state  = copy(boot game)
//   stack  = []                     // (objective, state-snapshot-before-it)
//   while objectives remain:
//       for each candidate objective that could advance an unmet goal,
//                                    in heuristic order (cheapest / closest first):
//           snap = copy(state)
//           if execute(candidate, &state) == true:      // SIMULATE this one step
//               plan.append(candidate); stack.push(snap); break       // keep it
//           else:
//               state = snap                                           // undo, next
//       if no candidate advanced:
//           // dead end — remove the last committed step and try a DIFFERENT
//           // alternative / ordering there.
//           candidate, snap = stack.pop(); plan.pop(); state = snap
//
// So `execute` is the single deterministic oracle: it runs ONE objective (PlanStep)
// on a throwaway Game — translating it to the appropriate Primitive internally —
// and reports success/failure (and, on success, leaves the Game advanced + the
// recorded engine prims appended to `rec`). The planner never re-simulates the
// whole plan; it advances or rewinds one step at a time.
// ---------------------------------------------------------------------------------

// THE EXECUTOR. Solve ONE objective (PlanStep) into engine actions — exclusively.
// execute() translates the objective to its Primitive internally, then
// problem-solves the full sequence of engine calls that achieves it, recording the
// replayable prims it emits. It does NO planning (never chooses/reorders objectives)
// and reimplements NO game mechanic (it calls the engine). Returns true on success
// (objective progressed, hero survived, calendar held); false if it could not be
// carried out from this state (caller restores its snapshot and tries an
// alternative). MUTATES g / map / fog on success.
bool execute(const PlanStep *s, const Resources *res, Game *g, Map *map,
             Fog *fog, RecSink *rec);

// Human-readable name for a kind (diagnostics).
const char *prim_kind_name(PrimKind k);

#endif
