// demo/demo_internal.h -- shared state + internals across the demo TUs.
// Not a public header; only demo/*.c include it.

#ifndef OB_DEMO_INTERNAL_H
#define OB_DEMO_INTERNAL_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "combat.h"

// ---- Player memory (everything here is derivable from what a player saw) --

#define DEMO_CAND_MAX     128   // scepter candidate tiles kept
#define DEMO_SEARCHED_MAX  64   // failed digs remembered (eliminated tiles)
#define DEMO_DECLINED_MAX  96   // fights declined, keyed by target id
#define DEMO_INTEL_MAX     32   // castles whose owner the prompts revealed

typedef struct { char zone[32]; int x, y; } DemoSpot;

typedef struct {
    char id[32];        // castle id or foe placement id
    long at_power;      // our army power when we declined / lost
    bool lost;          // we FOUGHT and lost at that power (not just declined)
} DemoDeclined;

// What a siege prompt taught us about a castle (the banner names the ruler).
typedef struct {
    char id[24];
    bool villain;           // villain-held (else monsters)
    char villain_id[24];    // when villain
} DemoIntel;

typedef struct {
    // Scepter deduction (demo_scepter.c).
    DemoSpot cand[DEMO_CAND_MAX];
    int      cand_count;      // matches stored (capped)
    int      cand_total;      // matches found (uncapped); 0 = none/unknown
    int      cand_reveals;    // villains+artifacts count at last recompute (-1 stale)
    int      cand_searched;   // searched_count at last recompute
    DemoSpot searched[DEMO_SEARCHED_MAX];
    int      searched_count;
    int      digs;            // searches attempted

    // Fight judgment memory: don't re-peek a target until the army outgrew
    // the power we declined (or lost) at.
    DemoDeclined declined[DEMO_DECLINED_MAX];
    int          declined_count;
    DemoIntel    intel[DEMO_INTEL_MAX];
    int          intel_count;

    // Zone campaign state.
    bool zone_done[GAME_CONTINENTS];   // explored dry + no targets; sail away
    bool want_boat;                    // set when a goal needs the water
    bool cornered;                     // walled in with a foe on the only exit:
                                       // take the fight at any odds (defeat =
                                       // temp death = the game's own escape)
    bool rings_off;                    // ignore lethal-foe avoidance this plan
                                       // (their rings sealed everything)

    // Committed goal: walked to completion instead of re-picked every tick
    // (per-tick re-selection dithered between equidistant targets).
    bool     goal_active;
    int      goal_x, goal_y;
    char     goal_zone[32];
    int      goal_interact;   // Interact expected at the goal; 0 = plain tile
    bool     goal_rings_off;  // the pick needed the avoidance rings off

    // Tiles whose lunge the engine refused with no effect -- and places that
    // killed us: skipped so a pick/refuse mismatch can never pin the run and
    // the frontier stops pulling the hero back into a death pocket.
    DemoSpot blocked[16];
    int      blocked_at[16];
    int      blocked_len[16];
    int      blocked_n;

    int  last_sail_tick;   // last decision taken while in the boat
    bool war;              // a castle target exists: recruit to the hilt

    // Watchdogs.
    int  waits;          // consecutive idle weeks
    long last_sig;       // cheap world signature for the stuck detector
    int  same_sig_ticks;
    int  ticks;

    // Ending.
    bool done, won, stuck;
    char end_reason[48];
} DemoState;

DemoState *demo_state(void);

// The shell-installed visible hooks (demo.c); fields NULL when headless.
typedef struct {
    void (*animate)(void *ctx, CombatMode mode, const CombatTurnRecord *rec);
    void (*town)(void *ctx, int action);
    void *ctx;
} DemoHooks;
DemoHooks *demo_hooks(void);

// ---- Pathfinding over the player's knowledge (demo_path.c) ----------------
// Two-layer (foot/boat) BFS over FOG-SEEN tiles of the current zone. Bouncer
// and interactive tiles are walls except as the goal; tiles within reach of a
// foe this run has already fought and LOST to are avoided.

typedef struct {
    short dist[2][MAP_MAX_H][MAP_MAX_W];   // [layer][y][x]; -1 unreachable
} DemoField;

void demo_field_build(const Game *g, const Map *map, const Fog *fog,
                      DemoField *pf);
// Steps to reach (x,y): the tile's own distance when enterable, else the best
// adjacent distance + 1 (bouncers are entered from beside). -1 unreachable.
int  demo_field_dist(const DemoField *pf, const Map *map, int x, int y);
// One step of the committed walk toward (gx,gy). False when unreachable.
bool demo_path_step(const Game *g, const Map *map, const Fog *fog,
                    int gx, int gy, int *dx, int *dy);
// Nearest reachable seen tile bordering unexplored fog. False when none.
bool demo_frontier_pick(const Game *g, const Map *map, const Fog *fog,
                        const DemoField *pf, int *fx, int *fy);

// ---- Scepter deduction (demo_scepter.c) ------------------------------------
// Recompute the candidate set from the puzzle view's revealed cells (only when
// the reveal count or the searched list changed).
void demo_scepter_update(const Game *g, const Fog *live_fog);
// Nearest un-searched candidate (same zone preferred). False when none known.
bool demo_scepter_nearest(const Game *g, DemoSpot *out);

// ---- Judgment (demo_brain.c) ------------------------------------------------
// Army-power estimate from the public troop catalog: count x hp x avg damage
// x skill factor. Out-of-control stacks count against us (they defect).
long demo_units_power(const Unit *units, int nslots);
long demo_army_power(const Game *g);
// Did we FIGHT this target and lose at (or below) `ours` power? Seed-keyed
// combat replays the loss, so such foes are avoided until the army outgrows it.
bool demo_known_loss(const char *id, long ours);
// Is (x,y) in the current zone inside an active no-go memory (dead lunges,
// death pockets)? The frontier picker consults it too.
bool demo_spot_blocked(const Game *g, int x, int y);
// One decision: sense, decide, act. False when the run has ended.
bool demo_brain_tick(Game *g, Map *map, Fog *fog, const Resources *res);

#endif
