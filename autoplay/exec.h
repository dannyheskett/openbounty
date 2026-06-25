#ifndef OB_AUTOPLAY_EXEC_H
#define OB_AUTOPLAY_EXEC_H

// autoplay/exec.h
//
// THE EXECUTOR'S FLAT HELPER SET (see docs/EXECUTOR-REFACTOR.md).
//
// The architecture is exactly four levels, each calling only downward:
//
//     autoplay  ->  planner  ->  executor  ->  engine
//
// The executor is a FLAT set of functions split across a few translation units
// purely for readability (movement / combat / recruit / town / primitives) — NOT
// a stack of layers. Every executor function is exactly one of two kinds:
//
//   * a PRIMITIVE (the 11 the planner selects from — exec_fetch, exec_town, ...).
//     ORCHESTRATION ONLY: it calls helpers and NEVER touches the engine, game
//     state, or shell directly. Declared in primitives.h.
//
//   * a HELPER (the 18 below). Helpers are the ONLY code that touches the
//     engine/game state. Each is single-purpose and reusable. Three of them
//     (exec_path / exec_fight / exec_recruit) wrap real algorithms and own
//     private static internals in their .c file (the granted exception); those
//     internals are NOT declared here and are NOT reusable or unit-tested
//     individually.
//
// Recording is a write-only sink (recording.h): every engine-changing action a
// helper performs is emitted here via rec_push_move / rec_push_action so that
// headless == visible == planning, byte-for-byte.

#include <stdbool.h>

#include "game.h"         // Game
#include "map.h"          // Map
#include "fog.h"          // Fog
#include "resources.h"    // Resources
#include "recording.h"    // RecSink, RecActionKind (and FlowAnswer via flow_answer.h)
#include "recruit.h"       // RecruitSource (exec_recruit_sources output)
#include "nav.h"           // NavPoint / NavTravel / NavStatus (the pathfinder
                           // vocabulary; nav.c's kernel is folded into exec_move.c)

// ---------------------------------------------------------------------------------
// Recording-sink emitters (shared by every executor TU). Thin wrappers over
// recbuf_push that build the one RecPrim each kind needs. Safe with a NULL sink.
// ---------------------------------------------------------------------------------

// Append one REC_MOVE (a single recorded adventure step (dx,dy)).
void rec_push_move(RecSink *rec, int dx, int dy);

// Append one REC_ACTION (a direct engine action with no flow — garrison, rent
// boat, take contract, ...). `id` may be NULL.
void rec_push_action(RecSink *rec, RecActionKind a, const char *id, int x, int y);

// ---------------------------------------------------------------------------------
// The 18 helpers — the only engine-touching code. Declared here as they are built
// (P1, leaf-first). Signatures spell out the (g, map, fog, res, ..., rec) context
// the abbreviated names in the design doc omit.
// ---------------------------------------------------------------------------------

// --- Movement ---

// HELPER #1 — exec_travel(dz,x,y,fight_through): move the hero from his current tile
// to (dz,x,y) by ANY means — walk, board/rent boat, sail, cross zones — by reusing
// the single mover (nav()). When fight_through, a hostile foe blocking the only route
// is cleared (reach it + exec_fight) and the move retried; without it a foe-blocked
// target defers (false). The ONE fight-through router. True on arrival.
bool exec_travel(Game *g, Map *map, Fog *fog, const Resources *res,
                 const char *dz, int tx, int ty, bool fight_through, RecSink *rec);

// HELPER #2 — exec_reach(dz,x,y,fight_through): travel up to a BOUNCING interactive
// at (dz,x,y) — foe / dwelling / alcove / castle or town gate — and step ONTO it,
// raising its flow (the primitive then answers/fights it). Approaches the nearest
// foot-standable neighbour via exec_travel, then exec_step onto the tile. True once
// the onto-step is issued.
bool exec_reach(Game *g, Map *map, Fog *fog, const Resources *res,
                const char *dz, int tx, int ty, bool fight_through, RecSink *rec);

// HELPER #3 — exec_step(dx,dy): one recorded engine move. Records a REC_MOVE then
// applies it with GameStep, so the executor logs the move it intended and the
// engine decides the effect (board / disembark / consume / bounce) identically on
// replay. Returns GameStep's result (true iff the hero's tile changed).
bool exec_step(Game *g, Map *map, Fog *fog, const Resources *res,
               int dx, int dy, RecSink *rec);

// HELPER #4 — exec_path(from,to,travel): the executor's pathfinder. Next single A*
// step from `from` toward `to` in the hero's boat-aware `travel` state.
// `goal_is_bouncer` exempts the goal tile from the bouncer block so the hero is
// delivered ONTO an interactive target. Pure (no Map/Game mutation); the step is in
// *out_dx/out_dy on NAV_OK. Encapsulates the A* kernel; nav.c's six internals
// (path_passable / path_cost / path_heap_push / path_heap_pop / path_seen /
// path_search) port in here when nav.c is deleted (P6) — this signature is stable.
NavStatus exec_path(const Map *map, NavPoint from, const NavTravel *travel,
                    NavPoint to, bool goal_is_bouncer, int *out_dx, int *out_dy);

// --- Combat ---

// A CASTLE battle is WON for garrison purposes only with >= 2 SURVIVING stacks: the
// engine refuses to garrison the hero's LAST stack, so a single-survivor win cannot
// hold a captured monster castle. A villain capture needs no garrison (a plain win).
#define CASTLE_GARRISON_MIN_SURVIVORS 2

// ARMY PRESERVATION (exec_fight, FOE fights only). The army-build pipeline solves for
// the composition that wins with the best SURVIVAL RATIO (surviving hp-worth / committed
// hp-worth), not the cheapest win — try_recruit_for_win / try_recompose_for_win rank by
// it. A foe fight is still DECLINED when even that best army would win only Pyrrhically:
// surviving worth < PRESERVE_MIN_RATIO_PCT% of the committed worth. One dimensionless,
// scale-free knob (not an absolute hp count) — the same bar holds for a 60-worth starting
// army or a 1000-worth late one, across all zones. Foe-only: castles are objectives the
// hero wants captured, so a costly castle win can still be worth it.
#define PRESERVE_MIN_RATIO_PCT   25

// HELPER #5 — exec_fight: resolve the combat the caller just provoked by stepping
// onto a foe / castle gate. Everything is derived from the pending flow the step
// raised (pending_flow + pending_foe_id / pending_castle_id) — the mode, the target
// (name / seed_key / garrison), and the win-bar (a foe or villain needs a plain
// win; a monster castle needs >= 2 survivors so a stack can garrison). So callers
// just step on and call this — there is ONE place that knows how to fight the tile.
// PREDICTION-GATED on a discarded copy (combat RNG is a pure fn of seed+identity+
// mode, so a predicted win wins the live fight too): ENTER only on a predicted
// goal-win, run the authoritative headless fight (recording its CombatTurnRecord +
// REC_ANSWER), apply WON. On a predicted non-goal-win, DECLINE (answer NO, no fight).
// ALWAYS answers so the snapshot-invisible pending_flow is cleared. Returns true iff
// the target was defeated to the goal.
bool exec_fight(Game *g, Map *map, RecSink *rec);

// HELPER #6 — exec_garrison(castle): garrison the WEAKEST surviving stack
// (lowest count*hit_points; tie -> lowest slot) into a freshly-captured castle,
// keeping the strongest army with the hero. Records RA_GARRISON_WEAKEST. Returns
// false if the engine refuses (would empty the army, or the castle is not the
// hero's to garrison). Assumes the castle is already won/owned.
bool exec_garrison(Game *g, const char *castle_id, RecSink *rec);

// --- Recruit ---

// HELPER #7 — exec_recruit(target): assemble the held army to the composition
// `target` (the recruiting contract). Dismiss held stacks whose type the target
// dropped, then buy each target stack's shortfall at its source (exec_recruit_one).
// Do-or-fail: returns false if any source can't be reached/bought. (The planner
// supplies the composition `target`; the old simulation optimizer that used to
// decide it was deleted with the AutoplayPlanner at P6.)
bool exec_recruit(Game *g, Map *map, Fog *fog, const Resources *res,
                  const ArmyTarget *target, RecSink *rec);

// HELPER #8 — exec_recruit_one(source, n): buy up to n of source->troop_id at one
// source — an in/off-zone dwelling (reach it, answer its recruit flow) or the home
// pool (sail to the home gate, GameBuyTroop). Returns false if the source can't be
// reached or the buy is refused.
bool exec_recruit_one(Game *g, Map *map, Fog *fog, const Resources *res,
                      const RecruitSource *source, int n, RecSink *rec);

// HELPER #9 — exec_recruit_sources(out,cap): enumerate every recruitable troop
// and where to get it (in-zone dwellings, home-castle pool, off-zone dwellings),
// in preference-tier order. Read-only. Returns the count written (<= cap).
int exec_recruit_sources(const Game *g, RecruitSource *out, int cap);

// HELPER #10 — exec_dismiss(slot): dismiss the held stack in `slot` (frees
// leadership). Records RA_DISMISS_TYPE keyed by the dismissed troop's id (replay
// dismisses by id). Returns false if the slot is empty/invalid or the engine
// refused (e.g. it would empty the army).
bool exec_dismiss(Game *g, int slot, RecSink *rec);

// try_recruit_for_win / try_recompose_for_win — pre-fight army upgrade helpers.
// Try each available recruit source to find one whose purchase flips the combat
// prediction from LOSS to WIN against `tgt`. try_recruit adds a troop to an
// existing slot; try_recompose swaps the weakest held stack for a stronger type.
// Both trial on a discarded copy (g unchanged until a winning source is found),
// then buy for real. Return true if a recruit/recompose was performed. Used by
// exec_slay (SLAY objectives) and exec_travel (fight-through blocking foes).
bool try_recruit_for_win(Game *g, Map *map, Fog *fog, const Resources *res,
                         CombatMode mode, const CombatTarget *tgt,
                         RecSink *rec, int min_survivors);
bool try_recompose_for_win(Game *g, Map *map, Fog *fog, const Resources *res,
                           CombatMode mode, const CombatTarget *tgt,
                           RecSink *rec, int min_survivors);

// --- Town ---

// HELPER #11 — exec_enter_town(allow_castle, out_dock): reach the nearest HAVEN in the
// current zone and stop at it — a town (gate IN: sets position.in_town, yields the dock)
// or, when allow_castle is set, the home (audience) castle's gate-landing tile if it is
// a faster refuge. Candidates are scored by a FOE-PASSABLE reachability check and reached
// with the one fight-through router (foe-free tier preferred, fewest ticks within it), so
// a haven reachable only by fighting through a blocker still counts. Rule 6 sets
// allow_castle after a battle; TOWN services pass false (they need a town's dock). False
// only if no haven has any path.
bool exec_enter_town(Game *g, Map *map, Fog *fog, const Resources *res,
                     bool allow_castle, int *out_dock_x, int *out_dock_y, RecSink *rec);

// The transaction helpers below assume the hero is already gated into a town
// (exec_enter_town does the routing).

// HELPER #12 — exec_buy_siege(): buy siege weapons (RA_BUY_SIEGE). True if owned
// after (incl. already-owned). False if no town / no gold.
bool exec_buy_siege(Game *g, RecSink *rec);

// HELPER #13 — exec_take_contract(villain_id): cycle GameTakeNextContract until the
// contract for `villain_id` is active (empty id = take any next), bounded to one
// full pass over the cycle. Records RA_TAKE_CONTRACT per take. True once active.
bool exec_take_contract(Game *g, const Resources *res, const char *villain_id,
                        RecSink *rec);

// HELPER #14 — exec_buy_spell(): buy the combat spell the current town offers
// (RA_BUY_SPELLS). False if no spell for sale / at cap / no gold.
bool exec_buy_spell(Game *g, RecSink *rec);

// HELPER #15 — exec_rent_boat(dock): rent a boat at the town's dock coords
// (RA_RENT_BOAT). True if a boat is held after. False on no gold / bad dock.
bool exec_rent_boat(Game *g, int dock_x, int dock_y, RecSink *rec);

// --- Interaction / time / spell (defined in primitives.c alongside the
//     primitives they cut across). ---

// HELPER #16 — exec_answer(ans): apply the NON-combat flow the executor just
// provoked (alcove / dwelling recruit / chest choice / ...) via the shared
// player_io router and record a REC_ANSWER. Returns false if no flow is pending.
// (Combat answers are emitted inside exec_fight, which runs the fight first.)
bool exec_answer(Game *g, Map *map, FlowAnswer ans, RecSink *rec);

// HELPER #17 — exec_cast(spell_index): apply an adventure spell (RA_CAST_ADV_SPELL).
// Returns the engine's result (false on a bad index / no charge). Records the cast
// before applying so replay reproduces it.
bool exec_cast(Game *g, int spell_index, RecSink *rec);

// HELPER #18 — exec_spend_week(): pass one week, banking commission (RA_WAIT_WEEK).
// Returns true if the game is still live afterward.
bool exec_spend_week(Game *g, RecSink *rec);

#endif // OB_AUTOPLAY_EXEC_H
