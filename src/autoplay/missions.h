// Generic per-zone autoplay solver.
//
// The autoplay runs a list of *missions*. Each mission self-resolves
// by reading the live game state — no hardcoded coords, no per-seed
// tables. The dispatcher in flow.c walks the mission queue and calls
// the appropriate mission handler each tick.
//
// Mission queue (executed in order):
//   1. STARTUP        — intro + initial starter recruit (zone 1 only)
//   2. ALCOVE         — visit the magic alcove if not yet claimed
//   3. PATHS_END_SPELLS — buy fireballs at the paths_end town
//   4. GHOST_DWELLING — recruit ghosts (caneghor counter)
//   5. (per zone, in discovery order)
//      a. SAFE_ACQUIRE     — chests/artifacts/navmaps with no foe in
//                            pursuit range
//      b. REHOME_RECRUIT   — sail back to king_maximus, max-buy army
//      c. CHEST_GRIND      — remaining chests, foes accepted
//      d. MONSTER_GRIND    — capture all monster-owned castles
//      e. VILLAIN_GRIND    — take contract + capture each villain
//      f. SAIL_TO_NEXT     — sail to next discovered zone if any
//
// Each mission is idempotent — re-entering it checks world state and
// re-decides what to do. Missions advance when their work is fully
// complete or when there's nothing applicable left.

#ifndef OB_AUTOPLAY_MISSIONS_H
#define OB_AUTOPLAY_MISSIONS_H

#include "autoplay/internal.h"

typedef enum {
    MISSION_STARTUP = 0,
    MISSION_ALCOVE,
    MISSION_PATHS_END_SPELLS,
    MISSION_GHOST_DWELLING,
    MISSION_SAFE_ACQUIRE,
    MISSION_REHOME_RECRUIT,
    MISSION_CHEST_GRIND,
    MISSION_MONSTER_GRIND,
    MISSION_VILLAIN_GRIND,
    MISSION_SAIL_TO_NEXT,
    MISSION_DONE,
} MissionKind;

// Dispatch one tick of the current mission. Reads st->mission_kind +
// st->mission_zone and st->mission_substep; advances them on
// completion.
ApCmd ap_mission_tick(const Game *g, const Map *m,
                      AutoplayState *st,
                      bool *out_phase_done,
                      AutoplayPhase *out_next_phase);

// Initialize the solver state to start of mission queue.
void ap_mission_reset(AutoplayState *st);

// Symbolic name of a MissionKind value (for logging).
const char *ap_mission_name(int kind);

// =========================================================================
// Runtime queries — used by missions to decide what to do.
// =========================================================================

// Look up tile of given interactive in the current zone. Optionally
// require id match (NULL or "" matches any). Returns true on hit.
bool ap_find_interact(const Map *m, int want_interact,
                      const char *want_id_or_null,
                      int *out_x, int *out_y);

// Returns true if any tile (chest/artifact/navmap/orb) is "safe" —
// reachable without entering any alive hostile foe's chebyshev-2
// envelope. Writes the nearest such tile to (out_x, out_y).
bool ap_pick_safe_acquisition(const Game *g, const Map *m,
                              int *out_x, int *out_y);

// Same but ignores foe envelope. Returns nearest remaining
// chest/artifact/navmap/orb tile.
bool ap_pick_any_acquisition(const Game *g, const Map *m,
                             int *out_x, int *out_y);

// Returns true if any alive hostile foe in the current zone sits
// within chebyshev-2 of (x,y). Used to decide chest "safety".
bool ap_foe_in_pursuit_range(const Game *g, int x, int y);

// Pick the lowest-HP monster castle in zone_id. Returns NULL if none.
const CastleRecord *ap_pick_weakest_monster_castle(
    const Game *g, const char *zone_id);

// Pick the next uncaptured villain in zone_id (a castle with owner=
// VILLAIN, villain_id not in villains_caught). Returns castle pointer
// or NULL.
const CastleRecord *ap_pick_next_villain(
    const Game *g, const char *zone_id);

// Pick the next unsolved discovered zone (in discovery order, skipping
// the home zone if it's solved). Returns true and writes zone id.
bool ap_pick_next_unsolved_zone(const Game *g, const AutoplayState *st,
                                char *out_zone, int cap);

// Pick any town in the current zone. Used for taking contracts —
// caller picks any town (all of them offer the same contract cycle).
bool ap_pick_any_town(const Map *m, int *out_x, int *out_y,
                      char *out_id, int id_cap);

// Leadership headroom until the next rank-up. Returns 0 if at max
// rank. Used to switch chest-prompt policy from leadership to gold
// when the cap is met.
int ap_leadership_until_next_rank(const Game *g);

// Estimate the total HP of a foe's garrison (alive hostiles only).
int ap_estimate_foe_hp(const FoeState *foe);

// Total army HP for the player.
// (Already declared in internal.h as ap_army_total_hp; reused.)

#endif
