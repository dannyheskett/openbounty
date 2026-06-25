// autoplay/goals.h
//
// PlanStep / objective model. The zone-0 checklist (§3) becomes
// literal, individually checkable goals. Each goal carries a kind, a target
// location, and the predicates/decomposition the planner needs: is it done, is
// it actionable, and where do I go + what do I do on arrival.
//
// Phase 2 implements ONE goal kind end to end — the treasure chest —
// to prove the locate -> path -> act -> mark-done loop. Later phases add the
// remaining kinds (artifact, navmap, orb, alcove, siege weapons, castles,
// villains) behind this same model.
//
// Engine-only: reads Game / Resources / Map; no src/ dependency.

#ifndef OB_AUTOPLAY_GOALS_H
#define OB_AUTOPLAY_GOALS_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "resources.h"

#include "nav.h"

// Plan-primitive kinds. A plan is an ordered sequence of these. Two
// roles:
//   - OBJECTIVE primitives are the §3 milestone checklist; admitting them is the
//     goal and they count toward the verdict.
//   - ENABLING primitives are NOT milestone items — they are actions the
//     intervention search (§6.2) inserts to make objectives admissible
//     (grow the army, cross water, hold a contract). They are generated on
//     demand, not enumerated from the world.
// STEP_FIRST_ENABLING marks the boundary; planstep_is_objective() uses it.
typedef enum {
    // --- objective primitives (milestone checklist) ---
    STEP_CHEST = 0,
    STEP_ARTIFACT,
    STEP_NAVMAP,
    STEP_ORB,
    STEP_ALCOVE,
    STEP_SIEGE_WEAPONS,   // buy siege weapons — a milestone AND a hard prereq of
                          // every castle assault
    STEP_MONSTER_CASTLE,
    STEP_VILLAIN,
    STEP_FOE,             // a hand-placed HOSTILE wandering army (from the
                          // pack's game.json wandering_armies[]); an
                          // OBJECTIVE — counts toward CLEARED. Enumerated from
                          // the live g->foes[] array (data-driven, no hardcoding).
    STEP_SCEPTER,         // the WIN: stand on the buried scepter's tile and
                          // search (FLOW_SEARCH -> out_won). Enumerated once at
                          // g->scepter.{zone,x,y}; hard-gated on EVERY other
                          // objective being done (the King's quest finale),
                          // so a CLEARED run ends on the win cartoon.
    // --- enabling primitives (inserted by the intervention search) ---
    STEP_FIRST_ENABLING,
    STEP_RECRUIT_HOME = STEP_FIRST_ENABLING,  // GameBuyTroop, home-castle pool
    STEP_RECRUIT_DWELLING,                    // flow_apply_recruit at a dwelling
    STEP_RENT_BOAT,                           // GameRentBoat at a town
    STEP_TAKE_CONTRACT,                       // GameTakeNextContract, cycle to V
    STEP_CLEAR_FOE,                           // seek & destroy a winnable hostile
                                              // foe: spoils gold + clears the tile
                                              // (Part B economy/unblock phase)
    STEP_BUY_SPELLS,                          // GameBuySpell at a combat-spell
                                              // town: arm the caster for a
                                              // predicted-LOSS spell-favoring fight
    STEP_TRAVEL_ZONE,                         // cross-zone travel: board/rent a
                                              // boat, then the RA_TRAVEL_ZONE
                                              // primitive (GameSwitchZone +
                                              // GameSpendWeek). handle = target
                                              // zone id. Offered by the
                                              // intervention search as the last
                                              // resort when only other-zone
                                              // objectives remain.
    STEP_RECOMPOSE_ARMY,                      // stall-time army upgrade: dismiss
                                              // everything and re-buy the
                                              // strongest affordable composition
                                              // (one RA_SET_ARMY). The escape
                                              // from the leadership-cap-with-
                                              // chaff trap; kept iff army power
                                              // strictly grows.
    STEP_WAIT_WEEKS,                          // strategic wait: spend act_x weeks
                                              // in place (RA_WAIT_WEEK each) to
                                              // bank commission income, offered
                                              // only when a strictly-stronger
                                              // recompose becomes affordable at
                                              // the end of the wait.
    STEP_RECOMPOSE_FLY,                       // recompose into an ALL-FLYING
                                              // army (skill >= 2) so the fly
                                              // pursuit can reach an objective
                                              // no surface route touches; kept
                                              // iff such an objective then
                                              // admits.
    STEP_CAST_SPELL,                          // cast adventure spell; target.x
                                              // = spell_index. Never enumerated
                                              // from world — only exists to give
                                              // execute() a testable path to
                                              // PRIM_CAST.
    STEP_KIND_COUNT,
} PlanKind;

// What the planner does once the hero arrives at the step's location.
typedef enum {
    ARRIVE_CONSUME = 0,  // GameStep onto the tile consumes it (chest/artifact/
                         // navmap/orb) — no extra action needed
    ARRIVE_ALCOVE,       // GameStep onto the alcove raises FLOW_ALCOVE; accept
    ARRIVE_BUY_SIEGE,    // at a town: invoke GameBuySiege
    ARRIVE_SIEGE,        // step onto the castle gate -> siege flow (combat)
    ARRIVE_RECRUIT_HOME, // at the home castle gate: GameBuyTroop (castle pool)
    ARRIVE_RECRUIT_DWELL,// step onto a dwelling -> FLOW_RECRUIT
    ARRIVE_RENT_BOAT,    // at a town: GameRentBoat
    ARRIVE_TAKE_CONTRACT,// engine: cycle GameTakeNextContract to the wanted villain
    ARRIVE_CLEAR_FOE,    // step onto a hostile foe tile -> FLOW_ATTACK_FOE (fight)
    ARRIVE_BUY_SPELLS,   // at a combat-spell town: invoke GameBuySpell
    ARRIVE_TRAVEL_ZONE,  // aboard the boat: apply RA_TRAVEL_ZONE (switch zone
                         // + spend the rest of the week)
    ARRIVE_RECOMPOSE,    // in place: dismiss-all + re-buy (one RA_SET_ARMY)
    ARRIVE_WAIT,         // in place: spend weeks banking commission
    ARRIVE_SCEPTER,      // on the scepter tile: search (FLOW_SEARCH) -> win
} StepArrival;

// A single plan primitive. `target` is the tile to reach (when tile_bound).
// `tile_bound` is false for steps whose location is resolved at execution (e.g.
// buy-siege / rent-boat at the nearest town; take-contract needs no tile).
// `arrival` says what to do on arrival. `handle` is the STABLE ENTITY HANDLE:
// a named id where the engine has one — placement id (artifact/navmap/
// orb), castle id, villain id, town id, the home-castle id — or
// empty for an anonymous chest (whose physical key is `target`). The handle is
// what the committed plan + report print, so the plan reads by entity, not by
// bare coordinate.
typedef struct {
    PlanKind    kind;
    NavPoint    target;      // tile to navigate to (when tile_bound)
    bool        tile_bound;  // false => location resolved at execution time
    StepArrival arrival;
    int         zone_index;  // zone the primitive lives in
    char        handle[24];  // stable entity id (castle/villain/placement/town/
                             // home), empty for anonymous chests
    char        label[48];   // human-readable, for the decision log / checklist
} PlanStep;

// Maximum primitives tracked in one run. Sized for the FULL four-zone
// objective universe (~355 objectives on the default pack) plus generous
// headroom for inserted enabling steps. A single zone uses well under half.
#define STEP_MAX 768

// The run's primitive set. For enumeration this holds the objective universe;
// the committed plan (plan.h) records the ordered admitted subset plus inserted
// enabling steps. Built from omniscient state, then re-evaluated each step.
typedef struct {
    PlanStep steps[STEP_MAX];
    int  count;
} PlanStepSet;

// Dynamic-array of admitted objectives (the planner's output sequence).
typedef struct {
    PlanStep *items;
    int       count;
    int       cap;
} GoalLog;

// Is this primitive an OBJECTIVE (milestone checklist item) vs an ENABLING
// primitive (recruit/boat/contract)? Only objectives count toward the verdict.
static inline bool planstep_is_objective(PlanKind k) {
    return k < STEP_FIRST_ENABLING;
}

// Enumerate chest goals in `zone_index`. The zone's chests[] is the salt
// PLACEHOLDER list (REQ-231): salting rewrites some slots into dwellings /
// artifacts / navmaps / orbs / telecaves / foes, so a slot is a chest objective
// ONLY if its LIVE tile is still INTERACT_TREASURE_CHEST. `map` is the live
// salted map consulted for that check. Appends to `set`; returns the number
// added. Safe on an empty set; later phases append other kinds the same way.
int plansteps_enumerate_chests(PlanStepSet *set, const Game *g, const Map *map,
                           int zone_index);

// Enumerate ALL non-combat zone objectives for `zone_index`: chests
// (gated on the live salted map, see above), artifacts / navmap / orb (from
// g->placements[] by kind), the alcove (magic), and the siege-weapons purchase.
// `map` is the live salted map. Appends to `set`; returns the number added.
// This is what the planner builds its checklist from. (Combat goals — monster
// castles, villains — are added by plansteps_enumerate_combat.)
int plansteps_enumerate_noncombat(PlanStepSet *set, const Game *g, const Map *map,
                              int zone_index);

// Enumerate combat objectives for `zone_index`: monster castles
// (defeat + garrison) and villain castles (capture via contract). Each goal's
// `id` is the castle id and `target` is a tile adjacent to the castle gate
// (the gate bounces; the siege flow fires on stepping onto it). Appends to
// `set`; returns the number added.
int plansteps_enumerate_combat(PlanStepSet *set, const Game *g, int zone_index);

// Enumerate hostile-foe objectives for `zone_index`. Walks the live g->foes[]
// array (populated at GameInit from game.json wandering_armies[]) and emits one
// STEP_FOE per HOSTILE (!friendly), ALIVE foe in this zone. Fully data-driven:
// NO hardcoded foe list. handle = placement_id, target = (foe x,y),
// tile_bound = true, arrival = ARRIVE_CONSUME (the generic tile-bound path fights
// the foe via step 6b). Dead foes are skipped so a defeated foe drops out of the
// universe. Appends to `set`; returns the number added.
int plansteps_enumerate_foes(PlanStepSet *set, const Game *g, int zone_index);

// Emit the single STEP_SCEPTER win objective at g->scepter.{zone,x,y} — the
// King's-quest finale (stand on the buried scepter, search, win). Resolves the
// scepter zone to its zone index; emits nothing if that zone isn't in the
// pack. tile_bound = true, arrival = ARRIVE_SCEPTER. The finale gate in
// plan_build defers it until every other objective is resolved. Appends to
// `set`; returns 1 if added, else 0. Called ONCE per plan (not per zone).
int plansteps_enumerate_scepter(PlanStepSet *set, const Game *g);

// Is this goal already satisfied in the current Game state? For a
// chest: its tile is in g->consumed[] (the canonical, uniform pickup signal).
bool planstep_is_done(const PlanStep *gl, const Game *g);

// Count goals in `set` that are done vs total (for the checklist summary).
void planstepset_progress(const PlanStepSet *set, const Game *g,
                      int *out_done, int *out_total);

// Pick the next actionable, not-done goal. Phase 2 ordering is simply the
// enumeration order (chests only); the safe->power->combat priority tiers
// layer on here as more kinds arrive. Returns an index into
// set->steps[], or -1 if all goals are done.
int planstepset_next(const PlanStepSet *set, const Game *g);

// Human-readable kind name (decision log / checklist).
const char *planstep_kind_str(PlanKind k);

#endif
