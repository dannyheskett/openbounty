// Reusable phase helpers for autoplay flow.c. Each helper consumes the
// same five out-parameters as a normal phase body and returns a single
// ApCmd. Phases collapse from ~25-line blocks to one-line calls.
//
// Convention: every helper handles its own prompt/dialog/town views,
// runs ap_nav_step_avoiding_*-then-fallback to a runtime-resolved
// goal, and on arrival sets *out_phase_done + *out_next_phase to the
// caller-provided continuation.

#ifndef OB_AUTOPLAY_MACROS_H
#define OB_AUTOPLAY_MACROS_H

#include "autoplay/internal.h"

// -- #2 ap_nav_to_xy --------------------------------------------------------
// Walk/sail to literal map coords. label is used as the cmd-trace prefix.
// resume_phase = the caller's own phase; combat handler resumes there
// after victory.
ApCmd ap_nav_to_xy(const Game *g, const Map *m,
                   AutoplayState *st,
                   const char *label,
                   int goal_x, int goal_y,
                   AutoplayPhase resume_phase,
                   AutoplayPhase next_phase,
                   bool *out_phase_done,
                   AutoplayPhase *out_next_phase);

// -- #3 ap_nav_to_tile ------------------------------------------------------
// Walk/sail to a tile in the hero's current zone whose interactive
// matches `want_interact` and whose id matches `want_id`. id == NULL
// matches any tile with the interactive (e.g. nearest unclaimed
// artifact); empty id is treated the same way.
//
// The target is resolved at every call. If no matching tile is found,
// the helper transitions to AP_FLOW_DONE.
ApCmd ap_nav_to_tile(const Game *g, const Map *m,
                     AutoplayState *st,
                     const char *label,
                     int want_interact,
                     const char *want_id,
                     AutoplayPhase resume_phase,
                     AutoplayPhase next_phase,
                     bool *out_phase_done,
                     AutoplayPhase *out_next_phase);

// Convenience: nav to artifact with matching id ("anchor" etc.).
// On arrival the engine auto-claims the artifact via step interact.
ApCmd ap_nav_to_artifact(const Game *g, const Map *m,
                         AutoplayState *st,
                         const char *artifact_id,
                         AutoplayPhase resume_phase,
                         AutoplayPhase next_phase,
                         bool *out_phase_done,
                         AutoplayPhase *out_next_phase);

// Convenience: nav to town with matching id. Walks the hero onto the
// town tile; the engine opens VIEW_TOWN; helper advances to next_phase
// when VIEW_TOWN appears (so the caller can immediately drive the
// town menu).
ApCmd ap_nav_to_town(const Game *g, const Map *m,
                     AutoplayState *st,
                     const char *town_id,
                     AutoplayPhase resume_phase,
                     AutoplayPhase next_phase,
                     bool *out_phase_done,
                     AutoplayPhase *out_next_phase);

// Convenience: nav to castle gate with matching id.
ApCmd ap_nav_to_castle(const Game *g, const Map *m,
                       AutoplayState *st,
                       const char *castle_id,
                       AutoplayPhase resume_phase,
                       AutoplayPhase next_phase,
                       bool *out_phase_done,
                       AutoplayPhase *out_next_phase);

// -- #5 ap_sail_to_zone -----------------------------------------------------
// Full cross-continent sail. Steps:
//   1) Find the nearest open-sea tile from the hero's current
//      position (already in a boat OR walks to a town to rent).
//   2) Press N to open the navmap prompt.
//   3) Press the digit matching `dest_zone_id` in the prompt's options.
//   4) Wait for g->position.zone == dest_zone_id; advance to next_phase.
// State: uses module_scratch[14] as a sub-step counter (0..3).
ApCmd ap_sail_to_zone(const Game *g, const Map *m,
                      AutoplayState *st,
                      const char *dest_zone_id,
                      AutoplayPhase resume_phase,
                      AutoplayPhase next_phase,
                      bool *out_phase_done,
                      AutoplayPhase *out_next_phase);

// -- #6 ap_collect_artifacts_in_zone ---------------------------------------
// Iterate every INTERACT_ARTIFACT tile in `zone_id`, BFS-nav to each in
// turn, handle the auto-pickup dialog. When the list is exhausted
// (find_tile_in_zone returns false for any artifact id), advance to
// next_phase.
// State: uses module_scratch[15] as a per-zone "current target index"
// but the iteration is by repeat-scan rather than by index, so the
// helper is idempotent across re-entry.
ApCmd ap_collect_artifacts_in_zone(const Game *g, const Map *m,
                                   AutoplayState *st,
                                   const char *zone_id,
                                   AutoplayPhase resume_phase,
                                   AutoplayPhase next_phase,
                                   bool *out_phase_done,
                                   AutoplayPhase *out_next_phase);

// -- #1 ap_rehome_and_recruit ----------------------------------------------
// "Go back to Maximus castle from wherever the hero is, then recruit
// every troop category and leave." If the hero isn't on Continentia,
// first invokes the sail flow internally. Sequence:
//   * (cross-continent) sail to Continentia
//   * nav to maximus_castle gate
//   * step into gate (UP)
//   * press A to open recruit
//   * for each category: row letter, "9999", ENTER
//   * ESC out of recruit, ESC out of castle
//
// min_gold_reserve: 0 = max-buy every row. Otherwise the macro stops
// row recruitment when g->stats.gold would drop below this floor
// before starting the next row, so the hero leaves the castle with
// at least this much gold (covers boat upkeep + weekly fees).
ApCmd ap_rehome_and_recruit(const Game *g, const Map *m,
                            AutoplayState *st,
                            int min_gold_reserve,
                            AutoplayPhase resume_phase,
                            AutoplayPhase next_phase,
                            bool *out_phase_done,
                            AutoplayPhase *out_next_phase);

// -- ap_town_action --------------------------------------------------------
// Drive one row of the town menu. The caller must already be in
// VIEW_TOWN. action_kind:
//   "siege"     — buy siege weapons if not yet owned (E)
//   "boat"      — rent a boat if not yet owned (B)
//   "contract"  — take the next available contract (A); presses A until
//                 g->contract.active_id is non-empty
//   "contract_zone:<zone_id>" — keep pressing A until the active
//                 contract's villain is in <zone_id>; this rerolls
//                 mismatched contracts
//   "info_dismiss" — used to dismiss the info-text panel that town
//                 actions sometimes pop up (SPACE)
// On completion: presses ESC to exit the town view, advances to
// next_phase.
ApCmd ap_town_action(const Game *g, const Map *m,
                     AutoplayState *st,
                     const char *action_kind,
                     AutoplayPhase resume_phase,
                     AutoplayPhase next_phase,
                     bool *out_phase_done,
                     AutoplayPhase *out_next_phase);

// -- ap_buy_spells_at_town -------------------------------------------------
// In a town's spell shop, keep buying spell_id until count is reached
// or "no gold" / "max reached" bounces. Caller is in VIEW_TOWN.
ApCmd ap_buy_spells_at_town(const Game *g, const Map *m,
                            AutoplayState *st,
                            const char *spell_id,
                            int target_count,
                            AutoplayPhase resume_phase,
                            AutoplayPhase next_phase,
                            bool *out_phase_done,
                            AutoplayPhase *out_next_phase);

// -- ap_visit_alcove -------------------------------------------------------
// Walk to the alcove at (x, y), accept the magic offer (Y on alcove
// prompt), dismiss the resulting dialog, advance. If g->stats.knows_magic
// is already true, advance immediately.
ApCmd ap_visit_alcove(const Game *g, const Map *m,
                      AutoplayState *st,
                      int x, int y,
                      AutoplayPhase resume_phase,
                      AutoplayPhase next_phase,
                      bool *out_phase_done,
                      AutoplayPhase *out_next_phase);

// -- ap_recruit_at_dwelling ------------------------------------------------
// Walk to the INTERACT_DWELLING_* tile whose id is dwelling_id (looked
// up at runtime). Step on it to open the dwelling prompt, type the
// count digits, ENTER. If already in army, advance immediately.
ApCmd ap_recruit_at_dwelling(const Game *g, const Map *m,
                             AutoplayState *st,
                             const char *dwelling_id,
                             const char *troop_id,
                             int target_count,
                             AutoplayPhase resume_phase,
                             AutoplayPhase next_phase,
                             bool *out_phase_done,
                             AutoplayPhase *out_next_phase);

// -- ap_hunt_foe -----------------------------------------------------------
// Walk onto an alive FoeState's current tile to trigger combat. If the
// foe is already dead, advance immediately.
ApCmd ap_hunt_foe(const Game *g, const Map *m,
                  AutoplayState *st,
                  const char *foe_id,
                  AutoplayPhase resume_phase,
                  AutoplayPhase next_phase,
                  bool *out_phase_done,
                  AutoplayPhase *out_next_phase);

// -- ap_garrison_at_castle -------------------------------------------------
// Step onto a player-owned castle gate; engine opens VIEW_OWN_CASTLE;
// the helper toggles GARRISON mode (SPACE) and presses the row letter
// for the troop_id slot, then ESC.
ApCmd ap_garrison_at_castle(const Game *g, const Map *m,
                            AutoplayState *st,
                            const char *castle_id,
                            const char *troop_id,
                            AutoplayPhase resume_phase,
                            AutoplayPhase next_phase,
                            bool *out_phase_done,
                            AutoplayPhase *out_next_phase);

// -- ap_chest_tour ---------------------------------------------------------
// Walk a fixed list of legs in order, collecting each chest. chest_policy:
//   0 = take gold (A)
//   1 = take leadership (B)
// Combat is accepted at each leg. If a leg has no path, skip and advance.
typedef struct { int x, y; const char *name; } ApChestLeg;
ApCmd ap_chest_tour(const Game *g, const Map *m,
                    AutoplayState *st,
                    const char *label,
                    const ApChestLeg *legs, int n_legs,
                    int chest_policy,
                    AutoplayPhase resume_phase,
                    AutoplayPhase next_phase,
                    bool *out_phase_done,
                    AutoplayPhase *out_next_phase);

// -- #7 ap_monster_castle_grind --------------------------------------------
// Iterate all CASTLE_OWNER_MONSTERS castles in `zone_id`, sorted by
// total garrison HP low → high. For each: BFS-nav to gate, fight
// (combat resume routes back), enter castle, garrison the cheapest
// available player troop, exit. After each capture, invokes
// `ap_rehome_and_recruit` to restock the army. When the list is empty,
// advance to next_phase.
// State: uses module_scratch[8] for outer step, [9] for sub-step.
ApCmd ap_monster_castle_grind(const Game *g, const Map *m,
                              AutoplayState *st,
                              const char *zone_id,
                              AutoplayPhase resume_phase,
                              AutoplayPhase next_phase,
                              bool *out_phase_done,
                              AutoplayPhase *out_next_phase);

#endif
