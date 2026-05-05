#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "tables.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

// Full adventure-screen game state. Fields mirror the JSON save schema so
// serialization is 1:1. All enumerations are keyed by string IDs from
// tables.h — that keeps save files readable and avoids magic numbers.
//
// This struct does NOT own the map tiles or fog (those live in Map/Fog in
// map.h/fog.h). It references them by zone id; the caller loads the
// matching map when `position.zone` changes.

#define GAME_NAME_LEN        16
#define GAME_ARMY_SLOTS       5
#define GAME_CONTINENTS       4
#define GAME_TOWNS           26
#define GAME_CASTLES         26
#define GAME_MAX_MUTATIONS   64    // claimed artifacts, opened chests, etc.
#define GAME_MAX_DWELLINGS   64    // per-zone dwelling state rows
#define GAME_MAX_PLACEMENTS 128    // randomized objects stamped per-zone at init
#define GAME_MAX_FOES        64    // per-zone hostile foe state rows

// Storage caps are compile-time (bound by struct sizes). Tunable constants
// that define gameplay (day/week, costs, contract cycle length, difficulty
// table) live in assets/game.json and are read via g->res.
#define CONTRACT_CYCLE_MAX    8

typedef enum {
    DIFFICULTY_EASY = 0,
    DIFFICULTY_NORMAL,
    DIFFICULTY_HARD,
    DIFFICULTY_IMPOSSIBLE,
} Difficulty;

typedef enum {
    MOUNT_RIDE = 0,
    MOUNT_SAIL,
    MOUNT_FLY,
} Mount;

// Current travel mode on the adventure screen. Distinct from Mount, which
// is a long-term possession (can the hero board, fly, etc.).
typedef enum {
    TRAVEL_WALK = 0,
    TRAVEL_BOAT,
} TravelMode;

typedef struct {
    char        id[32];           // troop id from tables.h ("peasants"); empty = slot empty
    int         count;            // number of troops in the stack
} ArmyStack;

typedef struct {
    char   id[24];                // troop id
    int    count;
} Unit;

typedef struct {
    char   id[24];                // town id from assets/towns
    bool   visited;
    char   spell_for_sale[24];    // spell id (randomized per-game)
} TownRecord;

typedef enum {
    CASTLE_OWNER_PLAYER = 0,      // conquered by player
    CASTLE_OWNER_MONSTERS,        // neutral monsters
    CASTLE_OWNER_VILLAIN,         // held by a specific villain
    CASTLE_OWNER_SPECIAL,         // quest/king castle; flow from res.special
} CastleOwnerKind;

typedef struct {
    char             id[24];      // castle id
    bool             visited;
    bool             known;       // location known (revealed by Find Villain etc.)
    CastleOwnerKind  owner_kind;
    char             villain_id[24];  // set when owner_kind == CASTLE_OWNER_VILLAIN
    Unit             garrison[GAME_ARMY_SLOTS];
} CastleRecord;

typedef struct {
    bool             has_boat;
    int              x, y;
    char             zone[24];        // zone id the boat sits on
} BoatState;

// A tile that has been permanently consumed on the overworld (artifact
// picked up, chest opened, etc.). On load, the caller re-applies these
// mutations to the map so the tile renders & behaves as plain terrain.
typedef struct {
    char zone[24];
    int  x, y;
} TileMutation;

// Per-dwelling state: how many troops remain available. Indexed by
// (zone, x, y). Created lazily on first visit and persisted.
typedef struct {
    char zone[24];
    int  x, y;
    char troop_id[32];       // deterministic, set on first visit
    int  count;              // current available recruits
    int  max_population;     // from troop def at creation
} DwellingState;

// Per-foe statefoe_coords parity. Every foe (friendly or hostile)
// gets one of these; classification is by `friendly` flag. Friendlies are
// salted (SALT_FRIENDLY) onto chest slots; hostiles come from the zone's
// `armies[]` static placements (0x91 tiles in the .land
// file). Garrison is rolled at salt time so the defending stacks stay
// consistent between encounters . `alive`
// gates whether the tile is re-stamped on zone load; combat victory or
// successful recruit clears it.
typedef struct {
    char zone[24];
    int  x, y;
    char placement_id[32];
    Unit garrison[GAME_ARMY_SLOTS];
    bool alive;
    bool friendly;          // true → recruit dialog; false → attack prompt
} FoeState;

// Randomized object placements produced by salt_continent / salt_spells /
// salt_villains / bury_scepter during GameInit and stamped onto each zone
// by MapLoadZone alongside ResZone's JSON-declared objects. `kind` is an
// Interact enum value; `id` is the object's payload id (troop id for a
// dwelling, artifact id, etc.) — may be empty when not applicable.
typedef struct {
    char      zone[24];
    int       x, y;
    int       kind;          // Interact enum (stored as int for save stability)
    char      id[32];
} SaltedPlacement;

typedef struct {
    char             zone[24];        // current zone id
    int              x, y;            // current tile coords (display-space)
    int              last_x, last_y;  // previous tile (for bump-back)
    bool             facing_left;     // hero sprite mirror
} Position;

typedef struct {
    int              gold;
    int              commission_weekly;
    int              leadership_base;
    int              leadership_current;
    int              followers_killed;
    int              score;
    int              spell_power;
    int              max_spells;
    bool             knows_magic;
    int              siege_weapons;
    int              time_stop;           // overworld steps where day does not advance
    int              steps_left_today;
    int              days_left;
    bool             game_over;           // set when days_left reaches 0
    int              last_commission;     // amount paid at the most recent week-end (for UI)
    int              last_astrology_troop; // troop idx broadcast at last week-end (for UI)
    // Controls-menu settings . Parallel to
    // res->controls.items[]. Stored per-game so preferences persist in
    // the save file.
    int              options[8];
} Stats;

typedef struct {
    char    id[24];               // class id ("knight", ...)
    int     rank_index;            // 0..3
    char    rank_id[24];           // denormalized ("general")
    char    rank_title[32];        // denormalized display ("General")
} ClassState;

typedef struct {
    char        name[GAME_NAME_LEN];
    ClassState  cls;
    Difficulty  difficulty;
    Mount       mount;
} Character;

typedef struct {
    char             active_id[24];   // villain id, empty = no contract
    char             cycle[CONTRACT_CYCLE_MAX][24];  // size-bounded; real length in res->contract.cycle_length
    int              last_contract;   // last slot issued ; initialized from res
    int              max_contract;    // next new villain to rotate in
    bool             villains_caught[17];
} Contract;

typedef struct {
    bool             found[8];        // parallel to ARTIFACTS[]
} Artifacts;

typedef struct {
    // Per-spell counts, parallel to SPELLS[] (14 entries).
    int              counts[14];
} Spellbook;

typedef struct {
    bool             zones_discovered[GAME_CONTINENTS];
    bool             orbs_found[GAME_CONTINENTS];
    // Puzzle: which cells have been revealed (17 villains + 8 artifacts = 25).
    bool             puzzle_revealed[25];
    // Per-continent fog snapshots. Active continent's fog lives in main.c's
    // standalone Fog. On zone switch the outgoing fog is copied into this
    // array and the incoming continent's snapshot is loaded back.
    Fog              continent_fog[GAME_CONTINENTS];
} WorldProgress;

typedef struct {
    char             zone[24];        // zone id
    int              x, y;            // tile coords
    unsigned         key;             // XOR key used; kept for save-load parity
} ScepterLocation;

typedef struct Game {
    const Resources *res;             // loaded at startup; never owned by Game
    int              version;         // always SAVE_VERSION at runtime
    uint64_t         seed;            // RNG seed for this game
    Character        character;
    Stats            stats;
    Position         position;
    TravelMode       travel_mode;     // walking vs in boat
    int              anim_frame;      // 0..3, shared by hero and boat sprites
    bool             hud_visible;     // floating HUD bar toggle (persisted)
    ArmyStack        army[GAME_ARMY_SLOTS];
    Spellbook        spells;
    Contract         contract;
    Artifacts        artifacts;
    WorldProgress    world;
    BoatState        boat;
    TownRecord       towns[GAME_TOWNS];
    CastleRecord     castles[GAME_CASTLES];
    ScepterLocation  scepter;

    // Tiles that have been permanently consumed (artifact pickups, etc.).
    TileMutation     consumed[GAME_MAX_MUTATIONS];
    int              consumed_count;

    // Per-dwelling state (troop count available + troop kind + max).
    DwellingState    dwellings[GAME_MAX_DWELLINGS];
    int              dwelling_count;

    // Randomized objects placed at GameInit and replayed by MapLoadZone.
    SaltedPlacement  placements[GAME_MAX_PLACEMENTS];
    int              placement_count;

    // Hostile foe state (rolled garrisons + mutable coords for foes_follow).
    FoeState         foes[GAME_MAX_FOES];
    int              foe_count;
} Game;

// ----- Lifecycle ------------------------------------------------------------

// spawn_game: initialize game from name, class index, difficulty, and map data.
// Takes: Game struct, character name, class index (0-3), difficulty, pre-loaded map data.
// Populates all game state: character, stats, army, castles, villains, scepter, etc.
void GameInit(Game *g, const char *name, int pclass, int difficulty, const unsigned char *land);

// Refill global rule/name arrays 
void refill_rules(void);
void refill_names(void);

// Rank initialization 
void player_accept_rank(Game *g);

// Adventure-mode spell effects. Each consumes one charge from
// g->spells.counts[] and applies  effect directly to game
// state. Invoked from main.c's spell dispatch.
void GameCastTimeStop(Game *g);
void GameCastFindVillain(Game *g);

// Append a randomized placement. Silent no-op if the table is full.
// `kind` is an Interact enum value.
void GameAddPlacement(Game *g, const char *zone, int x, int y, int kind, const char *id);

// Phase 2/3/4: Map and object initialization 
void salt_spells(Game *g);
// Salt randomized objects into a zone. has no hostile-foe budget;
// hostiles come from static `armies[]` placements in the zone definition,
// which this function also registers as foes.
void salt_continent(Game *g, int continent, int artifacts, int navmaps, int orbs, int telecaves, int dwellings, int friendly_foes);
void furnish_map(Game *g);
void clear_fog(Game *g);
// : walk the zone's tile grid row-major
// and bury the scepter on a grass tile chosen by the supplied 0..N
// fraction of grass tiles.
void bury_scepter(Game *g, int continent);
// Assign every villain in the resource catalog to a random unowned castle
// in that villain's declared zone (VillainDef.zone). Sets owner_kind to
// VILLAIN, copies villain_id, and populates the castle garrison from the
// villain's army_troops/army_counts. Skips castles flagged
// special.excluded_from_contract (e.g. the King's castle).
//  does the same per-continent using a global villain
// index; openbounty iterates per-villain using VillainDef.zone instead.
void salt_villains(Game *g);
void repopulate_castle(Game *g, int castle_id);

// Called after a successful overworld move onto `terrain_is_desert`.
// time_stop > 0 absorbs the step (no day progress) and decrements it.
// Deserts zero the day budget immediately. Handles day and week rollover
// and sets stats.game_over when days_left reaches 0.
// Out-params (may be NULL): *day_ended set if a new day started;
// *week_ended set if a week boundary was crossed; *commission_paid set to
// the gold credited at that boundary (0 otherwise).
void GameOnStep(Game *g, bool terrain_is_desert,
                bool *day_ended, bool *week_ended, int *commission_paid);

// True when the day counter has hit 0 (game-over timeout).
bool GameIsOver(const Game *g);

// Advance `days` days (capped by days_left). Returns the number of weeks
// crossed.  `spend_days`.
// Out-params (may be NULL): *total_commission gets the sum of gold paid
// out across all week-crossings during this call.
int  GameSpendDays(Game *g, int days, int *total_commission);

// Skip remaining days in the current week (press W). 
// play.c:523 `spend_week`. Returns the week-id crossed (1 if we crossed,
// 0 otherwise — today's day doesn't retrigger).
int  GameSpendWeek(Game *g, int *total_commission);

// Roll a random chest outcome per .
// Continent-index-dependent roll on 1..100 against cumulative thresholds
// from bounty.c: chance_for_gold/commission/spellpower/maxspell/newspell.
// Mutates game state (gold / commission_weekly / spell_power / max_spells /
// spells.counts[]) and writes a human-readable body to `out_body`.
typedef enum {
    CHEST_OUTCOME_GOLD = 0,
    CHEST_OUTCOME_LEADERSHIP,
    CHEST_OUTCOME_COMMISSION,
    CHEST_OUTCOME_SPELL_POWER,
    CHEST_OUTCOME_MAX_SPELLS,
    CHEST_OUTCOME_NEW_SPELL,
    CHEST_OUTCOME_EMPTY,
} ChestOutcome;

// When GameRollChest returns CHEST_OUTCOME_GOLD the caller must resolve
//  gold_or_leadership prompt (game.c:3069): player picks gold
// OR distributing it to peasants for a leadership bump. `pending_gold`
// and `pending_leadership` carry the offered amounts; neither is applied
// to the game state by GameRollChest — the caller chooses via
// GameAcceptChestGold / GameAcceptChestLeadership.
//
// For every other outcome, GameRollChest applies its effect eagerly
// (spell_power, commission, etc.) and these fields are zero.
typedef struct {
    int pending_gold;
    int pending_leadership;
} ChestPending;

ChestOutcome GameRollChest(Game *g, int zone_index, int x, int y,
                           char *out_body, int out_sz,
                           ChestPending *out_pending);

void GameAcceptChestGold(Game *g, int gold);
void GameAcceptChestLeadership(Game *g, int leadership);

// Deterministically pick which troop lives at a dwelling tile, filtered
// by the dwelling's "kind" ("plains"/"forest"/"hill"/"dungeon").
// Returns NULL if no matching troop exists.
const TroopDef *GameDwellingTroopAt(const Game *g,
                                    const char *dwelling_kind,
                                    int x, int y);

// Purchase `count` troops of `troop_id`, deducting gold and filling an
// army slot (stacks if the slot already holds the same troop).
// Returns 0 on success, 1 on insufficient gold, 2 on no free slot,
// 3 if count exceeds leadership (stays refused).
int GameBuyTroop(Game *g, const char *troop_id, int count);

// add_troop: stack onto matching slot or first empty slot.
// No gold cost, no leadership cap. Returns 0 on success, 1 if no slot.
int GameAddTroop(Game *g, const char *troop_id, int count);

//-parity garrison transfer. Moves army slot `slot` from the
// player's army into `castle`'s garrison (stacking on matching troop or
// filling an empty slot).
// Returns 0 on success, 1 if no garrison slot available, 2 if this
// would leave the player with no army at all .
int GameGarrisonTroop(Game *g, const char *castle_id, int slot);

//-parity ungarrison transfer. Moves `castle`'s garrison slot
// `slot` back into the player's army (stacking or empty slot), then
// compacts the garrison so the slot is closed up.
// Returns 0 on success, 1 if no army slot available.
int GameUngarrisonTroop(Game *g, const char *castle_id, int slot);

// Slide non-empty army stacks down so the filled slots run 0..N-1 with
// no gaps. Called after any operation that vacates a slot (garrison,
// dismiss, post-combat losses). Order is preserved.
void GameCompactArmy(Game *g);

// Max troops the player can handle based on leadership (leadership / hp).
int GameMaxRecruitable(const Game *g, const char *troop_id);

// Return (or lazily create) the dwelling state for the given tile.
// First-creation picks a troop via GameDwellingTroopAt and initializes
// count to its max_population. Returns NULL if storage is full.
DwellingState *GameTouchDwelling(Game *g, const char *zone, int x, int y,
                                 const char *dwelling_kind);

// Grow all dwellings by their troop's growth_per_week, capped at
// max_population. Called at week-end when no astrology event applies.
void GameGrowDwellings(Game *g);

// Week-end astrology: dwellings whose troop matches the astrology
// creature of the week are fully repopulated to max_population, and
// absorb-ability troops (ghosts) in the player's army are replaced by
// the astrology creature (). `troop_idx` is a
// troop catalog index; -1 = no astrology event (growth-only).
// Returns the troop id that was broadcast, or empty on no-op.
const char *GameApplyAstrology(Game *g, int troop_idx);

// Pick the astrology creature for a given week id. Deterministic from
// g->seed + week_id  — we stabilize so the player
// sees the same creature twice if they reload). Every 4th week yields
// troop 0 (peasants)  `if (week_id % 4 == 0) creature = 0`.
int GamePickAstrologyCreature(const Game *g, int week_id);

// Switch the hero to a new zone: reload the map, reset fog for that zone,
// move the hero to the target zone's hero_spawn. Returns true on success.
// Tile mutations persist across zone switches (per zone).
bool GameSwitchZone(Game *g, Map *map, Fog *fog, const char *zone_id);

// Close a contract: pay the villain's reward, mark them caught, and
// rotate them out of the 5-slot cycle (replacing with max_contract++).
// Mirrors . Returns true if the
// villain was in the cycle (so a rotation happened).
bool GameFulfillContract(Game *g, const char *villain_id);

// Fuzzy number label ("A few", "Some", "Many", ...) for a troop count.
// Reads thresholds from res->combat.number_names .
// Returns "" if count < 1 or no thresholds configured.
const char *GameNumberName(const Game *g, int count);

// True iff every occupied army stack contains a flying troop with
// skill_level >= 2. Mirrors .
// Empty armies return true (vacuous); caller should check for a non-
// empty army separately if that matters.
bool GamePlayerCanFly(const Game *g);

// Locate a CastleRecord by id. Returns NULL if no match.
CastleRecord *GameFindCastle(Game *g, const char *castle_id);
const CastleRecord *GameFindCastleConst(const Game *g, const char *castle_id);

// Mark artifact `idx` as found and apply its instant passive effects
// (double leadership, +commission, double spell power, etc.). Returns
// true if this call actually claimed the artifact (false if already owned).
bool GameClaimArtifact(Game *g, int idx);

// True if the player owns any artifact with the given passive power.
bool GameHasPower(const Game *g, ArtifactPower power);

// Append a tile-consumed record. No-op if the buffer is full or the entry
// is already present.
void GameAddConsumed(Game *g, const char *zone, int x, int y);

// Apply every consumed-tile record that matches the given zone id to
// `map` by clearing its interactive overlay. Call once after loading the
// map for the hero's current zone (on new game, save load, or any
// future zone switch).
void GameApplyTileMutations(const Game *g, Map *map, const char *zone);

// Total number of spell charges the hero is carrying (sum of counts[]).
int  GameKnownSpells(const Game *g);

// Boat rental cost — 100 with the Anchor of Admirability, 500 otherwise.
int  GameBoatCost(const Game *g);

// Locate the TownRecord for the given town id, creating an empty slot (and
// assigning spell_for_sale deterministically from the game seed) on first
// visit. Returns NULL only if all slots are full.
TownRecord *GameTouchTown(Game *g, const char *town_id);

// Advance the contract cycle one step  and set
// contract.active_id to the villain in the new slot. Returns the chosen
// villain id (pointer into the cycle buffer), or NULL if the cycle is
// empty.
const char *GameTakeNextContract(Game *g);

// Apply rank-up if villains_caught count meets the next rank's threshold.
// Returns true if rank changed.
bool GameMaybeRankUp(Game *g);

// Army helpers.
int  GameArmyTotalLeadership(const Game *g);     // sum of troop->hp * count for all stacks
int  GameArmyStackCount(const Game *g);          // number of non-empty stacks

// Count helpers used by view_character.
int  GameVillainsCaught(const Game *g);
int  GameArtifactsFound(const Game *g);
int  GameCastlesOwned(const Game *g);

// . Base formula:
//   500 * villains + 250 * artifacts + 100 * castles − followers_killed
// Modified by difficulty: Easy halves, Normal ×1, Hard ×2, Impossible ×4
// (impossible mod is ×4; the 5-slot table [0,1,2,4,8] reserves
// a ×8 for an unused 5th tier). Clamped at 0.
int  GameComputeScore(const Game *g);

// Look up or create a hostile-foe entry by placement id. Returns NULL if
// the id is empty / foe table is full. The returned pointer is stable
// until GameInit resets state.
FoeState *GameFindFoe(Game *g, const char *placement_id);
const FoeState *GameFindFoeConst(const Game *g, const char *placement_id);

// Advance every foe (friendly and hostile) on the hero's current zone one
// step toward the hero's previous tile .
// May step a foe onto the hero — that's the combat trigger (
// game.c:6552). Returns the index in g->foes[] of a foe that landed on
// the hero this step, or -1 if none did. Caller fires the attack/recruit
// flow against that foe. Map is mutated so foe tiles follow the foes
// (old tile reverts to grass, new tile gets INTERACT_FOE).
int GameFoesFollow(Game *g, Map *map);

#endif
