#ifndef OB_COMBAT_H
#define OB_COMBAT_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"
#include "resources.h"  // ResCombatLog used by combat_log_strings()

// Combat module. Engine-side data and headless entry points.
//
// The shell-side rendered loop (RunCombat) lives in src/combat_loop.h
// and uses the same Combat struct defined here. The renderer reads
// from it; engine code mutates it through the step/AI functions.

// ----- Public outcome --------------------------------------------------------
typedef enum {
    COMBAT_RESULT_WIN = 0,
    COMBAT_RESULT_LOSS,
} CombatResult;

typedef enum {
    COMBAT_MODE_CASTLE = 0, // siege -- castle layout, walls, no field obstacles
    COMBAT_MODE_FOE    = 1, // overworld foe stack -- open field, scattered obstacles
} CombatMode;

typedef struct {
    const char *name;          // display string for banner / dialogs
    const Unit *garrison;      // defender's stacks; may be NULL
    int         garrison_slots;
} CombatTarget;

// ----- Battlefield constants -------------------------------------------------
#define COMBAT_W       6
#define COMBAT_H       5
#define COMBAT_SIDES   2
#define COMBAT_SLOTS   5

// Side identifiers.
#define COMBAT_SIDE_PLAYER  0
#define COMBAT_SIDE_AI      1

// Per-side artifact power bits used during combat. Translated from
// the player's found-artifact set at prepare time. Bit values are
// internal -- never serialized.
#define COMBAT_POWER_INCREASED_DAMAGE   (1 << 0)  // x1.5 damage dealt
#define COMBAT_POWER_QUARTER_PROTECTION (1 << 1)  // damage taken x 3/4

// Banner / log sizing.
#define COMBAT_BANNER_LEN     80
#define COMBAT_LOG_LINES       8
#define COMBAT_LOG_LINE_LEN   80

// ----- Per-unit state --------------------------------------------------------
// One slot per (side, id). troop_idx == -1 means the slot is empty.
typedef struct CombatUnit {
    int  troop_idx;       // index into troop catalog; -1 = empty
    int  count;           // current creatures in the stack
    int  turn_count;      // snapshot at start of unit's turn (for retaliation)
    int  max_count;       // snapshot at combat start (cap for ABSORB / LEECH)
    bool dead;            // marked when count reaches 0; cleared by compact
    int  frame;           // 0..3 idle anim
    int  injury;          // residual sub-HP damage
    bool acted;           // turn already taken
    bool retaliated;      // has retaliated this round (resets on next turn)
    int  moves;           // ground move budget remaining
    int  shots;           // ranged ammo remaining
    int  flights;         // airborne move budget (FLY) remaining
    bool frozen;          // skips next turn (cleared on reset_turn)
    bool out_of_control;  // leadership exceeded -- attacks own side
    int  x, y;            // grid position (0..COMBAT_W-1, 0..COMBAT_H-1)
    // Damage-burst overlay -- set when this unit takes a hit, decremented
    // by combat_tick_anim. While >0 the renderer paints the splat frame
    // over the unit's cell.
    int  hit_flash;
} CombatUnit;

// ----- Match state -----------------------------------------------------------
// umap and omap are sized [H+1][W+1] -- the +1 row/column padding guards
// against off-by-one in distance and adjacency math.
typedef struct Combat {
    CombatUnit    units[COMBAT_SIDES][COMBAT_SLOTS];
    unsigned char omap [COMBAT_H + 1][COMBAT_W + 1];   // obstacle tile codes (0 = open)
    unsigned char umap [COMBAT_H + 1][COMBAT_W + 1];   // packed UID = side*5 + id + 1, 0 = empty
    int           spoils[COMBAT_SIDES];                 // accumulated per side
    unsigned char powers[COMBAT_SIDES];                 // COMBAT_POWER_* bits
    Game         *heroes[COMBAT_SIDES];                 // [PLAYER] = g, [AI] = NULL
    int           turn;
    int           phase;                                // 0 or 1 (next_unit wrap counter)
    int           spells_this_round;                    // <= 1
    int           side;                                 // currently acting side
    int           unit_id;                              // currently acting unit slot
    bool          castle;                               // siege layout vs open field
    // Title-bar mode: pre-first-kill shows "Options / <Actor> M<n>",
    // post-first-kill shows "<Player> vs <Foe> killing <N>".
    bool          first_kill_seen;
    int           stacks_destroyed;
    // RNG (separate from world LCG).
    uint64_t      rng_state;
    // UI / log.
    char          banner[COMBAT_BANNER_LEN];
    char          log_lines[COMBAT_LOG_LINES][COMBAT_LOG_LINE_LEN];
    int           log_count;
    // Cursor / pick state.
    int           cursor_x, cursor_y;
    int           target_filter;
    // True only while a target picker (shoot, spell) is in its modal
    // input loop. The cursor ring is drawn by the renderer iff this is
    // set -- the cursor is hidden during normal turns.
    bool          picker_active;
    // Cursor anim frame, 0..3 -- cycles the cursor frames of the combat
    // tileset when picker_active is true. Advanced by combat_tick_anim
    // alongside the unit-frame animation.
    int           cursor_frame;
    // Outcome scratch.
    int           result;                               // 0 = running, 1 = player win, 2 = AI win
    char          villain_id[24];                       // for victory dialog substitution; empty = none
    // Mode metadata captured at entry, kept so end-of-combat can route.
    CombatMode    mode;
    char          target_name[48];
} Combat;

// ----- Test / determinism harness --------------------------------------------
// Runs a deterministic scripted combat scenario and prints a one-line
// digest (RNG seed, hit counts per side, final state hash). Used by the
// `make combat-test` target to catch regressions in the damage formula.
//
// Returns the digest hash; identical inputs must produce identical
// digests across builds. No raylib calls -- safe to invoke headlessly.
uint64_t combat_test_digest(uint64_t seed,
                            const char *attacker_id,
                            int attacker_count,
                            const char *defender_id,
                            int defender_count,
                            int rounds);

// Headless combat for the playtest binary. Drives both sides with
// combat_ai_action -- no raylib, no input, no animation timer. Mirrors
// RunCombat's setup + writeback so g->army reflects losses on win, and
// gold gains spoils. Used in tests/playtest/ to fight foes/villains
// without a render context. cap_rounds guards against infinite loops
// from edge cases (returns FLEE if exceeded).
CombatResult combat_run_headless(Game *g, CombatMode mode,
                                 const CombatTarget *target,
                                 int cap_rounds);

// ----- Engine helpers used by both engine/combat.c and src/combat_loop.c.
// These were file-static when both halves lived in src/combat.c; the
// split required them to cross translation units. Engine-only callers
// should still prefer the higher-level entry points above.

const ResCombatLog *combat_log_strings(const Combat *c);
unsigned char combat_player_powers(const Game *g);
int  combat_hit_unit(Combat *c, int a_side, int a_id,
                     int t_side, int t_id, bool is_ranged);
bool combat_in_bounds(int x, int y);
int  combat_move_unit(Combat *c, int side, int id, int dx, int dy);
int  combat_fly_unit(Combat *c, int side, int id, int nx, int ny);

// Target-picker filter values used by combat_cell_passes_filter and
// the shell-side combat_pick_target. 0 = any tile, 1 = empty + no
// obstacle, 2 = any unit, 3 = friendly (under control), 4 = enemy,
// 5 = enemy undead.
typedef enum {
    PICK_FILTER_ANY        = 0,
    PICK_FILTER_EMPTY      = 1,
    PICK_FILTER_ANY_UNIT   = 2,
    PICK_FILTER_FRIENDLY   = 3,
    PICK_FILTER_ENEMY      = 4,
    PICK_FILTER_UNDEAD     = 5,
} PickFilter;

// Append a line to the combat log + banner. Format-string flavor.
void combat_log(Combat *c, const char *fmt, ...);

// Same, but expanding a %TOKEN%-style template (typically a field of
// Resources.combat_log).
void combat_log_template(Combat *c, const char *template_str,
                         const ResTemplateVar *vars, int nvars);

// ----- Engine API: state machine + AI + spells.
// Used by src/combat_loop.c (the rendered loop) and by unit tests.

// State init / RNG
void combat_init(Combat *c, Game *g, CombatMode mode,
                 const CombatTarget *target);
void combat_seed_rng(Combat *c, const Game *g, CombatMode mode,
                     const CombatTarget *target);
int  combat_rand(Combat *c, int min, int max);

// Unit init / control / morale
void combat_init_unit(CombatUnit *u, int troop_idx, int count);
bool unit_under_control(const Game *g, int troop_idx, int count);
bool units_are_friendly(const Combat *c, int sA, int iA, int sB, int iB);
int  morale_to_rank(char r);

// Match / turn machinery
void combat_prepare_player(Combat *c, const Game *g);
void combat_prepare_foe(Combat *c, const CombatTarget *target);
void combat_prepare_castle(Combat *c, const CombatTarget *target);
void combat_reset_match(Combat *c);
void combat_reset_turn(Combat *c, int started_at);
int  combat_next_unit(Combat *c);
void combat_next_turn(Combat *c);
void combat_compact(Combat *c);

// State predicates
bool combat_test_dead(const Combat *c, int side);
bool combat_unit_surrounded(const Combat *c, int side, int slot);
bool combat_cell_passes_filter(const Combat *c, int x, int y,
                               int caster_side, int filter);
bool unit_touching(const CombatUnit *a, const CombatUnit *b);

// AI
unsigned char ai_pick_target(const Combat *c, int side, int slot, bool nearby);
void          unit_move_offset(const Combat *c, const CombatUnit *self,
                               int tx, int ty, int *ox, int *oy);
int           combat_ai_action(Combat *c);

// Damage core. Returns kill count.
int  combat_deal_damage(Combat *c, int a_side, int a_id,
                        int t_side, int t_id,
                        bool is_ranged, bool is_external,
                        int  external_damage, bool retaliation);

// Spells
int  spell_damage_value(int base, int sp);
int  spell_damage(Combat *c, int t_side, int t_slot, int dmg);
void spell_clone(Combat *c, int t_side, int t_slot, int sp);
void spell_teleport(Combat *c, int from_side, int from_slot, int to_x, int to_y);
int  spell_freeze(Combat *c, int t_side, int t_slot);
// Returns 1 if any troops were revived, 0 if no-op (stack full or
// already dead). Callers use the return value to decide whether to
// consume the spell charge.
int  spell_resurrect(Combat *c, int t_side, int t_slot, int sp);

#endif
