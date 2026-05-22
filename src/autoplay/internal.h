#ifndef OB_AUTOPLAY_INTERNAL_H
#define OB_AUTOPLAY_INTERNAL_H

// Private interface shared between the autoplay core and its
// per-villain modules. Not exposed outside src/autoplay/.

#include <stdbool.h>
#include <stddef.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "shell_run.h"

// =========================================================================
// Phase enum
// =========================================================================
//
// One enum, all villains. Each module owns a contiguous block of
// values; the core dispatcher routes per_frame to the right module
// by checking which block the current phase falls in. Modules
// transition between blocks in-memory (no save/load between
// villains) — this is one continuous play-through.
//
// Add new villains by extending this enum and writing a module.

typedef enum {
    // ---- Murray ---------------------------------------------------------
    AP_MURRAY_FIRST = 0,
    AP_MURRAY_DISMISS_INTRO = AP_MURRAY_FIRST,
    AP_MURRAY_WALK_TO_KING,
    AP_MURRAY_OPEN_RECRUIT,
    AP_MURRAY_DO_RECRUITS,
    AP_MURRAY_LEAVE_RECRUIT,
    AP_MURRAY_LEAVE_KING,
    AP_MURRAY_WALK_TO_TOWN_FOR_CONTRACT,
    AP_MURRAY_TAKE_CONTRACT,
    AP_MURRAY_CLOSE_TOWN_INFO_AFTER_CONTRACT,
    AP_MURRAY_EXIT_TOWN_AFTER_CONTRACT,
    AP_MURRAY_WALK_TO_TOWN_FOR_BOAT,
    AP_MURRAY_RENT_BOAT,
    AP_MURRAY_EXIT_TOWN_AFTER_BOAT,
    AP_MURRAY_BOARD_BOAT,
    AP_MURRAY_SAIL_TO_AZRAM_COAST,
    AP_MURRAY_DISEMBARK_NEAR_AZRAM,
    AP_MURRAY_WALK_TO_AZRAM_GATE,
    AP_MURRAY_SIEGE_PROMPT,
    AP_MURRAY_COMBAT,
    AP_MURRAY_POST_COMBAT_DIALOG,
    AP_MURRAY_VERIFY,
    AP_MURRAY_LAST = AP_MURRAY_VERIFY,

    // ---- Hack -----------------------------------------------------------
    AP_HACK_FIRST,
    AP_HACK_WALK_TO_TOWN_FOR_CONTRACT = AP_HACK_FIRST,
    AP_HACK_TAKE_CONTRACT,
    AP_HACK_CLOSE_TOWN_INFO_AFTER_CONTRACT,
    AP_HACK_EXIT_TOWN_AFTER_CONTRACT,
    AP_HACK_LOCATE_CASTLE,
    // Re-stock loop: sail back to Maximus's landmass, walk to the king,
    // recruit a fresh army, then walk to a coastal town there to rent
    // a fresh boat to Hack's gate.
    AP_HACK_WALK_TO_TOWN_FOR_HOMETRIP,
    AP_HACK_RENT_BOAT_FOR_HOMETRIP,
    AP_HACK_EXIT_TOWN_AFTER_HOMEBOAT,
    AP_HACK_BOARD_BOAT_FOR_HOMETRIP,
    AP_HACK_SAIL_HOME_TO_MAINLAND,
    AP_HACK_DISEMBARK_NEAR_MAXIMUS,
    AP_HACK_WALK_TO_KING,
    AP_HACK_OPEN_RECRUIT,
    AP_HACK_DO_RECRUITS,
    AP_HACK_LEAVE_RECRUIT,
    AP_HACK_LEAVE_KING,
    // Dwelling tour: walk N from Maximus to recruit elite troops
    // (gnomes, ghosts) that out-leverage castle militia per leadership
    // point. On any seed this is best-effort; we skip dwellings we
    // can't afford or that aren't reachable overland.
    AP_HACK_WALK_TO_DWELLING,
    AP_HACK_RECRUIT_AT_DWELLING,
    AP_HACK_EXIT_DWELLING,
    // Dwelling boat-ferry: when a dwelling waypoint is on a different
    // landmass, ferry across. Mirrors the to-Hack sail flow but the
    // terminal is a dwelling recruit prompt instead of a siege.
    // Sub-target for these phases lives in scratch[20..21] (ferry
    // destination land tile chosen by the planner).
    AP_HACK_WALK_TO_TOWN_FOR_DWELLING_BOAT,
    AP_HACK_RENT_DWELLING_BOAT,
    AP_HACK_EXIT_TOWN_AFTER_DWELLING_BOAT,
    AP_HACK_BOARD_DWELLING_BOAT,
    AP_HACK_SAIL_TO_DWELLING_COAST,
    AP_HACK_DISEMBARK_NEAR_FAR_DWELLING,
    AP_HACK_WALK_FROM_LAND_TO_DWELLING,
    // Boat trip from coastal Continentia town to a land tile near
    // Hack's castle. Same shape as the Murray sail: rent → board → sail
    // BFS → disembark.
    AP_HACK_WALK_TO_TOWN_FOR_BOAT,
    AP_HACK_RENT_BOAT,
    AP_HACK_EXIT_TOWN_AFTER_BOAT,
    AP_HACK_BOARD_BOAT,
    AP_HACK_SAIL_TO_GATE_COAST,
    AP_HACK_DISEMBARK_NEAR_GATE,
    AP_HACK_WALK_TO_GATE,
    AP_HACK_SIEGE_PROMPT,
    AP_HACK_COMBAT,
    AP_HACK_POST_COMBAT_DIALOG,
    AP_HACK_VERIFY,
    AP_HACK_LAST = AP_HACK_VERIFY,

    // ---- Terminal -------------------------------------------------------
    AP_ALL_DONE,
} AutoplayPhase;

// =========================================================================
// Shared state
// =========================================================================
//
// One struct, one phase enum. Each module reads and writes the same
// AutoplayState; nothing module-private. The fields are general
// enough to serve every "walk somewhere, dismiss prompt, fight,
// verify" loop — modules just orchestrate them differently.

#define AP_PATH_MAX 256

typedef struct { int x, y; } ApPoint;

typedef enum {
    AP_TRAVEL_WALK = 0,
    AP_TRAVEL_BOAT,
} ApTravel;

// Substate enum for ap_ferry_tick. The ferry helper drives a hero
// from wherever they are to (target_x, target_y), renting a boat and
// sailing if no overland route exists. Modules call ap_ferry_tick in
// a loop from one of their own phases and react to the return value.
typedef enum {
    FERRY_IDLE = 0,
    FERRY_WALK_DIRECT,      // overland path exists; just walking
    FERRY_WALK_TO_TOWN,
    FERRY_RENT_BOAT,
    FERRY_EXIT_TOWN,
    FERRY_BOARD_BOAT,
    FERRY_SAIL,
    FERRY_DISEMBARK,
    FERRY_WALK_TO_TARGET,
    FERRY_DONE,             // hero is at target
    FERRY_FAILED,           // unrecoverable (no town, no path)
} FerryState;

typedef struct AutoplayState {
    AutoplayPhase phase;
    int           phase_started_at;
    bool          phase_action_queued;
    // Cached path for walk phases (BFS over MapWalkable).
    ApPoint       path[AP_PATH_MAX];
    int           path_len;
    int           path_idx;
    int           path_target_x;
    int           path_target_y;
    // Combat scripting state.
    int           combat_unit_id_last_acted;
    int           combat_frame_last_action;
    // Ferry sub-state-machine. Driven by ap_ferry_tick; the calling
    // module's switch case stays in one phase across the whole ferry
    // and reacts to the FerryState return value.
    FerryState    ferry_state;
    int           ferry_target_x, ferry_target_y;  // final destination
    int           ferry_town_x, ferry_town_y;      // chosen rental town
    int           ferry_landing_x, ferry_landing_y; // chosen disembark tile
    int           ferry_press_expect;              // RENT_BOAT B-press state
    int           ferry_started_at;                // for timeouts
    // Module-specific scratch. Modules write/read these freely
    // between their own phases. By convention they're reset to -1 at
    // module entry; -1 = "not yet set". When you need more state
    // than two ints, extend this array — the cost is one int per
    // villain module that uses it, which is fine.
    int           module_scratch[24];
} AutoplayState;

// =========================================================================
// Shared helpers (defined in core.c)
// =========================================================================

#define AP_LOG(fmt, ...) fprintf(stderr, "[autoplay] " fmt "\n", ##__VA_ARGS__)

#define AP_TIMEOUT_FAIL(start_frame, max_frames, msg) \
    do { \
        if (frame_no > (start_frame) + (max_frames)) { \
            AP_LOG("timeout — %s", (msg)); \
            return SHELL_RUN_EXIT_FAIL; \
        } \
    } while (0)

// Queue the new-game wizard input (knight, default name, normal).
void ap_queue_standard_startup(void);

// Queue the digit characters of n followed by ENTER, for the
// recruit-soldiers count prompt.
void ap_queue_recruit_count(int n);

// Sum hit-points across g->army[].
int  ap_army_total_hp(const Game *g);

// BFS / step-following on the world map.
int  ap_bfs(const Map *m, int sx, int sy, int gx, int gy,
            ApTravel mode, ApPoint *out, int out_cap);
int  ap_dir_key(int cx, int cy, int nx, int ny);
bool ap_ensure_path(AutoplayState *st, const Map *m,
                    int cx, int cy, int tx, int ty, ApTravel mode);
bool ap_step_along_path(AutoplayState *st, int cx, int cy);

// Combat decision helpers.
struct Combat;
int  ap_closest_enemy(const struct Combat *c, int ux, int uy,
                      int *out_dist);
// Pick the enemy stack with highest total HP (count * hit_points),
// tiebroken by proximity. Used for ranged target selection so shooters
// focus-fire big threats instead of plinking the nearest weakling.
int  ap_highest_threat_enemy(const struct Combat *c, int ux, int uy,
                             int *out_dist);
int  ap_queue_picker(int cx, int cy, int tx, int ty);

// Common prompt/dialog handler. Returns true if it queued an input
// or otherwise wants the per_frame to bail (return CONTINUE).
// Modules call this near the top of their per_frame.
bool ap_handle_common_prompts(const AutoplayState *st);

// Walk g->res->towns[] for towns in the current zone, BFS-distance
// each from (cx,cy) on foot, return the closest. -1 if no town is
// overland-reachable.
int ap_find_nearest_town(const Game *g, const Map *m,
                         int cx, int cy, int *out_x, int *out_y);

// Ferry sub-state-machine. Call from a module phase to move the hero
// from anywhere to (target_x, target_y). Returns:
//   FERRY_DONE   — hero is at the target tile; resume your own flow.
//   FERRY_FAILED — couldn't get there (no town for ferry, no path).
//                  Module should give up on this target.
//   anything else — still in progress; return SHELL_RUN_CONTINUE and
//                  re-tick next frame.
//
// To start a new ferry, set st->ferry_state = FERRY_IDLE before the
// first call. Subsequent calls drive the substate forward.
FerryState ap_ferry_tick(AutoplayState *st, Game *g, Map *m,
                         int frame_no,
                         int target_x, int target_y);

// Per-module checkpoint slot assignments. Each module saves at the
// end of its capture flow so the run is resumable from any completed
// villain. Slots are reserved from the top down so player save slots
// 1..N stay free for human use.
//   slot 9 (10) = Hack
//   slot 8 (9)  = Murray
//   slot 7 (8)  = (reserved for next villain)
// Add new villains here, decrementing from the existing minimum.
#define AP_SLOT_MURRAY  8
#define AP_SLOT_HACK    9

// Write an autoplay checkpoint to the given save slot (0-indexed).
// Logs success or failure to stderr; otherwise silent.
bool ap_save_checkpoint(const Game *g, const Map *m, const Fog *f,
                        int slot);

// =========================================================================
// Module entry points (defined in their respective .c files)
// =========================================================================

ShellRunVerdict ap_murray_per_frame(Game *g, Map *m, Fog *f,
                                    Resources *res, int frame_no,
                                    AutoplayState *st);

ShellRunVerdict ap_hack_per_frame(Game *g, Map *m, Fog *f,
                                  Resources *res, int frame_no,
                                  AutoplayState *st);

#endif
