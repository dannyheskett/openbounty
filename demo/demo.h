// demo/demo.h
//
// DEMO MODE -- the human-like player. Where autoplay is the pack-winnability
// oracle (reads the world directly, snapshots and rolls back across many
// lines of play), demo mode PLAYS the game under a player's constraints:
// one forward-only timeline, no rollback, and only player-visible information
// (the fog, the views, the prompt banners, the puzzle screen) plus public
// rules knowledge. Like autoplay it simulates a fight before entering it,
// but on a discarded copy of a world it can already see -- it never peeks at
// hidden state and never un-does a move it has committed. It explores,
// recruits, fights on judgment, catches villains, deduces the scepter tile
// from the puzzle reveals, and digs. It can win, lose, or get stuck -- all
// three are honest outcomes.
//
// Engine-only: demo/ links libobengine.a and includes NOTHING from src/ or
// autoplay/. The visible shell drives it through demo_begin/demo_tick; the
// headless runner uses demo_run.

#ifndef OB_DEMO_H
#define OB_DEMO_H

#include <stdbool.h>
#include <stdint.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "combat.h"   // CombatMode / CombatTurnRecord (the animator hook)

// The demo hero boot profile (parallel to autoplay's, owned separately so
// tuning one mode can never shift the other's worlds).
#define DEMO_HERO_NAME        "Demo"
#define DEMO_HERO_CLASS       0
#define DEMO_HERO_DIFFICULTY  DIFFICULTY_NORMAL
#define DEMO_DEFAULT_SEED_INDEX 1

typedef struct {
    int         seed_index; // 0..255; the catalog world to play
    const char *pack_dir;   // pack to load; required for demo_run
} DemoConfig;

// How a demo run ended, plus the player-facing tallies.
typedef struct {
    bool     won;           // searched the scepter tile -- the game's own win
    bool     out_of_days;   // calendar died first -- the game's own loss
    bool     stuck;         // the player ran out of ideas (watchdog)
    int      days_used;
    int      score;
    int      villains;
    int      artifacts;
    int      castles;
    int      searches;      // scepter digs attempted (10 days each)
    int      chests;        // treasure chests opened
    int      navmaps;       // navmaps collected (continents discovered - home)
    int      orbs;          // orbs found
    int      towns;         // towns visited
    int      ticks;         // decisions taken
    int      seed_index;
} DemoResult;

// Visible-mode hooks, installed by the shell adapter (src/shell_demo.c) and
// unset headless. Both MUST be state-inert (they draw; they never mutate the
// game). The COMBAT ANIMATOR draws the recorded fight the decision's
// prediction produced, before the identical live resolution is applied. The
// TOWN PRESENTER shows the town menu walking to the row about to be
// transacted, so a viewer sees the purchase a player would make.
typedef enum {
    DEMO_TOWN_CONTRACT = 0,
    DEMO_TOWN_BOAT,
    DEMO_TOWN_SIEGE,
    DEMO_TOWN_SPELL,
} DemoTownAction;
typedef void (*DemoCombatAnimator)(void *ctx, CombatMode mode,
                                   const CombatTurnRecord *rec);
typedef void (*DemoTownPresenter)(void *ctx, DemoTownAction action);
void demo_set_hooks(DemoCombatAnimator animate, DemoTownPresenter town,
                    void *ctx);

// Demo diagnostics gate -- set once from the --verbose CLI flag before a run
// (src/main.c), read by the per-tick decision trace. CLI-flag only: the
// project takes NO configuration from environment variables (DM-030).
void demo_set_verbose(bool on);
bool demo_verbose(void);

// Headless run: boot the pack + world for cfg->seed_index, play to an ending, fill
// *out. Returns false only on setup failure (bad config / pack load).
bool demo_run(const DemoConfig *cfg, DemoResult *out);

// Live-world driving (the visible shell owns Game/Map/Fog/Resources):
// demo_begin resets the player state; demo_tick performs ONE action per call
// (a step, a flow answer, a transaction, a fight, a dig, a wait) and returns
// false once the run is over; demo_result fills *out from the live game;
// demo_end releases the run state.
bool demo_begin(Game *g, Map *map, Fog *fog, const Resources *res);
bool demo_tick(Game *g, Map *map, Fog *fog, const Resources *res);
void demo_result(const Game *g, DemoResult *out);
void demo_end(void);

#endif
