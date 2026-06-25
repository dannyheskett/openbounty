// engine/include/flow_resolve.h
//
// Prompt-flow APPLY-CORES: the engine-side state-mutation half of each
// pending-flow resolution (see docs/OPENBOUNTY-SPEC.md §36). Phase 0.5 splits
// the shell's prompt resolver (src/shell_promptdispatch.c) into three layers:
//
//   (a) state mutation      → here, in the engine (callable by shell + autoplay)
//   (b) the answer read      → the per-host adapter (shell prompt UI / autoplay)
//   (c) presentation         → the per-host adapter (RunCombat, views_dismiss,
//                              run_end_cartoon, temp-death, recorder/audio)
//
// Each flow_apply_* performs ONLY (a): it mutates Game/Map and may open
// host-callback dialogs (open_dialog, declared in ui_host.h — the engine is
// allowed to call those). It does NOT run combat, dismiss views, or play the
// win cartoon; those stay with the host adapter. Combat-bearing flows take an
// already-resolved CombatResult so the engine never invokes the renderer:
// the shell adapter computes it via RunCombat, autoplay via
// combat_run_headless_ex, then both call the same apply-core.
//
// The answer a flow needs reduces to one enum value (+ one int for recruit).
// PromptAnswer mirrors the shell's PromptResult without leaking the shell type
// into the engine; the shell adapter maps one to the other.

#ifndef OB_FLOW_RESOLVE_H
#define OB_FLOW_RESOLVE_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "combat.h"        // CombatResult
#include "flow_answer.h"   // PromptAnswer, FlowAnswer (leaf header, shared with player_io.h)

// ---- Recruit params (FLOW_RECRUIT) -----------------------------------------
// The dwelling identity captured when the recruit prompt opened. The adapter
// fills this from the pending_* scratch; the apply-core needs it to find the
// dwelling and decrement its population.
typedef struct {
    const char *troop_id;   // troop being recruited
    const char *zone;       // dwelling's zone id
    int         x, y;       // dwelling tile
} RecruitParams;

// ---- Accept-friendly params (FLOW_ACCEPT_FRIENDLY) -------------------------
typedef struct {
    const char *troop_id;   // troop offered
    int         count;      // how many join on accept
    const char *foe_id;     // placement id of the friendly foe (to mark dead)
    const char *zone;       // zone of the foe tile (to consume)
    int         x, y;       // foe tile
} FriendlyParams;

// ---------------------------------------------------------------------------
// Apply-cores. One per pending flow that has a state effect. Each is pure
// engine: mutates state, may call open_dialog (host callback). None run
// combat or touch render/view state.
// ---------------------------------------------------------------------------

// FLOW_SEARCH (answer YES = the player chose to search this tile).
// Returns true iff the search landed on the scepter tile (a WIN): the adapter
// must then play the win cartoon and call show_win_game. On a non-win search
// this spends search-cost days (queuing a week-end via the out-params if a
// week boundary was crossed), opens the "revealed nothing" dialog, and sets
// game_over if the time spent ended the game (adapter shows the lose screen
// when *out_game_over is set). `*out_week_commission` > 0 means the adapter
// should schedule_week_end with that value.
bool flow_apply_search(Game *g, const Resources *res, FlowAnswer ans,
                       bool *out_won, bool *out_game_over,
                       int *out_week_commission);

// FLOW_DISMISS_ARMY (answer 1..5 selects a slot). Returns true iff the chosen
// slot was the LAST occupied stack, meaning the adapter must chain into the
// dismiss-last confirm prompt (the apply-core does NOT zero the slot in that
// case). For a non-last dismissal it zeroes the slot and compacts.
// `*out_slot` echoes the selected slot index (for the chained confirm).
bool flow_apply_dismiss_army(Game *g, FlowAnswer ans, int *out_slot);

// FLOW_DISMISS_LAST (answer YES confirms). When confirmed, the army is empty
// and the player is sent back to the King — but temp-death itself is shell
// orchestration (teleport + render), so this only reports the decision.
// Returns true iff the adapter should run perform_temp_death.
bool flow_apply_dismiss_last(FlowAnswer ans);

// FLOW_SIEGE_MONSTER. The adapter ran combat and passes `outcome`. On WIN the
// castle becomes player-owned, visited, garrison cleared (ready to garrison).
// Returns true iff the adapter should run perform_temp_death (i.e. LOSS).
bool flow_apply_siege_monster(Game *g, const char *castle_id,
                              CombatResult outcome);

// FLOW_SIEGE_VILLAIN. The adapter ran combat and passes `outcome`. On WIN the
// castle is captured (owner=player, villain cleared, garrison cleared); if the
// active contract matches the captured villain, the contract is fulfilled and
// rank-up applied, and the appropriate capture dialog is opened (the dialog
// text composition lives here so shell and autoplay say the same thing).
// Returns true iff the adapter should run perform_temp_death (LOSS).
bool flow_apply_siege_villain(Game *g, const Resources *res,
                              const char *castle_id, CombatResult outcome);

// FLOW_ATTACK_FOE. The adapter ran combat and passes `outcome`. On WIN the
// foe tile is cleared and the foe marked dead. Returns true iff the adapter
// should run perform_temp_death (LOSS).
bool flow_apply_attack_foe(Game *g, Map *map, const char *foe_id,
                           int foe_x, int foe_y, CombatResult outcome);

// FLOW_CHEST_CHOICE (answer 1 = gold (A), 2 = leadership (B)).
void flow_apply_chest_choice(Game *g, int chest_gold, int chest_leadership,
                             FlowAnswer ans);

// FLOW_DISCARD_SPELL (answer YES discards one charge of spell_idx, freeing a
// spellbook slot against the max_spells cap). NO/invalid/empty = no-op.
void flow_apply_discard_spell(Game *g, int spell_idx, FlowAnswer ans);

// FLOW_ALCOVE (answer YES accepts the magic lesson). On accept, deducts the
// alcove cost (or opens the no-gold dialog), sets knows_magic, clears the
// interactive tile, and consumes it; opens the result dialog either way.
void flow_apply_alcove(Game *g, Map *map, const Resources *res, FlowAnswer ans);

// FLOW_RECRUIT (answer YES with a typed count in ans.number). Buys up to the
// affordable/leadership cap, decrements dwelling population, and opens the
// no-gold / no-slots dialog on failure.
void flow_apply_recruit(Game *g, const RecruitParams *params, FlowAnswer ans);

// FLOW_ACCEPT_FRIENDLY (answer YES accepts the free join). The foe is always
// consumed (yes or no); on YES the troop joins the army.
void flow_apply_accept_friendly(Game *g, Map *map, const FriendlyParams *params,
                                FlowAnswer ans);

// FLOW_NAVIGATE (answer 1..5 selects a neighbor zone from `zones`). Switches
// zone on a valid pick and spends a week (the adapter schedules the week-end
// if `*out_week_commission` > 0). Returns false and opens a "cannot reach"
// dialog if the switch failed.
bool flow_apply_navigate(Game *g, Map *map, Fog *fog,
                         const char zones[][32], int zone_count,
                         FlowAnswer ans, int *out_week_commission);

#endif
