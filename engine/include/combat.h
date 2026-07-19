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
    // Stable per-encounter identity used to derive the combat RNG seed
    // (a foe's placement_id, or a castle id). May be NULL, in which case
    // `name` is used. This makes a fight's RNG a pure function of
    // (world seed, this identity, mode) -- independent of casualty history
    // -- so the autoplay planner's prediction always matches the live
    // outcome for the same foe. See combat_seed_rng.
    const char *seed_key;
} CombatTarget;

// ----- Battlefield constants -------------------------------------------------
#define COMBAT_W       6
#define COMBAT_H       5
#define COMBAT_SIDES   2
#define COMBAT_SLOTS   5

// Side identifiers.
#define COMBAT_SIDE_PLAYER  0
#define COMBAT_SIDE_AI      1

// Cast workflow phase, lives on Combat. The outer RunCombat loop
// advances the phase one step per frame; observers (autoplay,
// renderer, save/restore) read this field instead of guessing at
// modal stack frames.
typedef enum {
    COMBAT_CAST_NONE = 0,
    COMBAT_CAST_PICK_SPELL,    // spell-menu overlay drawn; wait A..G or ESC
    COMBAT_CAST_PICK_TARGET,   // target picker active; wait cursor + confirm
    COMBAT_CAST_APPLY,         // target captured; effect applied this frame
} CombatCastPhase;

// What the target picker is being used for. Drives the post-pick
// dispatch (shoot resolves to combat_hit_unit, fly to
// combat_fly_unit, spell targets to the cast pipeline). Also lets
// the autoplay hook tell shoot picks (single-target) from spell
// picks (cursor needs to walk to a different cell first).
typedef enum {
    COMBAT_PICK_REASON_NONE = 0,
    COMBAT_PICK_REASON_SHOOT,
    COMBAT_PICK_REASON_FLY,
    COMBAT_PICK_REASON_SPELL_TARGET,   // first/only target for fireball/etc.
    COMBAT_PICK_REASON_TELEPORT_DEST,  // teleport second pick (destination)
} CombatPickReason;

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
    // Pick + cast state machine. Lives on Combat so the outer
    // RunCombat loop, the renderer, and autoplay all read the same
    // source of truth rather than each tracking its own.
    CombatCastPhase  cast_phase;
    int              cast_spell_idx;     // 0..6, set during PICK_SPELL
    CombatPickReason pick_reason;        // why the picker is active
    int              pick_filter;        // PICK_FILTER_* for the current pick
    // First target captured by the picker. For most spells this is
    // the spell's target. For teleport, this is the unit being
    // teleported; the destination cell is the second pick and lands
    // in cast_dest_x/y.
    int              pick_t1_x, pick_t1_y;
    int              pick_t1_side, pick_t1_slot;
    int              cast_dest_x, cast_dest_y;
    // Outcome scratch.
    int           result;                               // 0 = running, 1 = player win, 2 = AI win
    char          villain_id[24];                       // for victory dialog substitution; empty = none
    // Mode metadata captured at entry, kept so end-of-combat can route.
    CombatMode    mode;
    char          target_name[48];
} Combat;

// ----- Test / determinism harness --------------------------------------------
// Runs a deterministic scripted combat scenario and prints a one-line
// digest (RNG seed, hit counts per side, final state hash). Exercised by
// tests/regression/test_combat_digests.c under `make test` to catch
// regressions in the damage formula.
//
// Returns the digest hash; identical inputs must produce identical
// digests across builds. No raylib calls -- safe to invoke headlessly.
uint64_t combat_test_digest(uint64_t seed,
                            const char *attacker_id,
                            int attacker_count,
                            const char *defender_id,
                            int defender_count,
                            int rounds);

// The shared resolution round cap. DETERMINISM-COUPLED: the win predictor,
// the live headless fight, the replay re-resolution, and the shell's
// animation copy must all pass the SAME cap_rounds -- a cap-hitting fight
// resolves differently under different caps, splitting predicted from live.
#define COMBAT_MAX_ROUNDS 256

// Headless combat: drives both sides with combat_ai_action -- no raylib,
// no input, no animation timer. Mirrors RunCombat's setup + writeback so
// g->army reflects losses on win, and gold gains spoils. Used by autoplay,
// the replay path, and the tests to fight foes/villains without a render
// context. cap_rounds guards against infinite loops from edge cases; a
// fight that hits the cap resolves as COMBAT_RESULT_LOSS.
CombatResult combat_run_headless(Game *g, CombatMode mode,
                                 const CombatTarget *target,
                                 int cap_rounds);

// ----- Per-turn combat record (visible-mode replay) --------------------------
// The single authoritative resolution (combat_run_headless_rec) can emit an
// ordered per-action record. The SHELL animates this record to draw the fight
// at turn-for-turn parity with the resolution -- it is never a second resolution.
// Plain C, engine-pure: the engine only FILLS it, never draws.
// Headless discards the record (passes NULL); visible captures it and animates.
typedef enum {
    CT_MOVE = 0,   // acting unit walked to (to_x,to_y)
    CT_FLY,        // acting unit flew to (to_x,to_y)
    CT_MELEE,      // acting unit struck an adjacent target
    CT_SHOOT,      // acting unit shot a ranged target
    CT_WAIT,       // acting unit passed / no positional or damage change
} CombatActionKind;

typedef struct {
    unsigned char act_side, act_slot;   // acting unit (side, slot) before compaction
    unsigned char kind;                 // CombatActionKind
    unsigned char tgt_side, tgt_slot;   // target unit, or 0xFF when none
    signed char   from_x, from_y;       // acting unit position before the action
    signed char   to_x, to_y;           // acting unit position after the action
    short         act_count_after;      // acting stack count after
    short         tgt_count_after;      // target stack count after (when tgt valid)
    char          log_line[COMBAT_LOG_LINE_LEN];   // the line this action appended
} CombatTurnEntry;

typedef struct {
    int              count;      // entries filled
    int              cap;        // capacity of entries[]
    int              result;     // mirrors Combat.result: 1 win / 2 loss / 0 none
    CombatTurnEntry *entries;    // caller-allocated; engine only fills
    // Defender (AI side) survivors at combat end -- OBSERVATION-ONLY end-state
    // scalars, filled whether or not `entries` is set. A caller that wants ONLY
    // the outcome metric (not the per-action replay) passes entries=NULL/cap=0
    // and pays no per-action recording cost. ai_stacks = non-empty AI stacks
    // left; ai_hp = sum(count * catalog hit_points) over them. On a player WIN
    // both are 0 (the AI is dead). The autoplay recruiter ranks losing
    // candidates by ai_hp (lower = closer to a win). These never
    // feed the damage formula or the RNG, so golden digests are unchanged.
    int              ai_stacks;
    long             ai_hp;
} CombatTurnRecord;

// Player-side decision callback. Invoked for the acting unit when it is the
// player's turn (c->side == COMBAT_SIDE_PLAYER); returns nonzero when it
// consumed the turn (same contract as combat_ai_action). ctx is opaque,
// passed through. Lets autoplay drive the player side with its own policy
// while the AI side still uses combat_ai_action.
typedef int (*PlayerCombatPolicy)(Combat *c, void *ctx);

// Headless combat with a custom player-side policy. Identical to
// combat_run_headless except that on the player's turn it calls player_fn
// instead of combat_ai_action. combat_run_headless is a thin wrapper over this
// (player_fn = combat_ai_action), so every existing caller and golden digest
// is unchanged. player_fn must be deterministic (replays must reproduce it).
CombatResult combat_run_headless_ex(Game *g, CombatMode mode,
                                    const CombatTarget *target,
                                    int cap_rounds,
                                    PlayerCombatPolicy player_fn,
                                    void *player_ctx);

// Headless combat that additionally records the per-action sequence into
// `out_rec`: the ONE authoritative resolution, with a replayable record
// the shell can animate. `out_rec` may be NULL (no recording -- identical to
// combat_run_headless_ex). combat_run_headless_ex is a thin wrapper passing
// out_rec=NULL, so it and every existing caller / golden digest stay byte-
// identical: the record is write-only observation that never feeds the formula
// or RNG. The caller allocates out_rec->entries (cap = cap_rounds*64 covers a
// full fight) and sets out_rec->cap; the engine fills entries[0..count) and sets
// result. Deterministic: combat RNG is a pure fn of (seed, identity, mode).
CombatResult combat_run_headless_rec(Game *g, CombatMode mode,
                                     const CombatTarget *target,
                                     int cap_rounds,
                                     PlayerCombatPolicy player_fn,
                                     void *player_ctx,
                                     CombatTurnRecord *out_rec);

// ----- Engine helpers used by both engine/combat.c and src/combat_loop.c.
// They cross the translation-unit boundary so the engine half and the
// shell half share one implementation. Engine-only callers should still
// prefer the higher-level entry points above.

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

// Deterministic direct damage one cast of `spell_idx` deals to a non-IMMUNE
// target stack (the same bases combat_cast_spell applies); 0 for enhancer
// spells (clone/teleport/freeze/resurrect) whose value has no closed form.
// turn_undead bites only UNDEAD targets -- pass whether the target is one.
// Planning layers size loadouts with THIS, never with restated formulas.
int  combat_spell_direct_damage(int spell_idx, int spell_power, bool target_undead);
int  spell_damage(Combat *c, int t_side, int t_slot, int dmg);
void spell_clone(Combat *c, int t_side, int t_slot, int sp);
void spell_teleport(Combat *c, int from_side, int from_slot, int to_x, int to_y);
int  spell_freeze(Combat *c, int t_side, int t_slot);
// Returns 1 if any troops were revived, 0 if no-op (stack full or
// already dead). Callers use the return value to decide whether to
// consume the spell charge.
int  spell_resurrect(Combat *c, int t_side, int t_slot, int sp);

// Combat-spell catalog indices (game.json ordering). The single definition,
// shared by the engine dispatcher, the shell UI, and the autoplay policy.
// Adventure spells 7..13 are not legal in combat.
#define COMBAT_SPELL_CLONE        0
#define COMBAT_SPELL_TELEPORT     1
#define COMBAT_SPELL_FIREBALL     2
#define COMBAT_SPELL_LIGHTNING    3
#define COMBAT_SPELL_FREEZE       4
#define COMBAT_SPELL_RESURRECT    5
#define COMBAT_SPELL_TURN_UNDEAD  6

// ----- Combat spell cast dispatcher -----------------------------------------
// Single engine entry point for "cast combat spell `spell_idx` from `side` at
// the given target", shared by the shell (src/combat_loop.c APPLY phase) and
// the autoplay combat policy so the index->effect dispatch, IMMUNE no-effect
// handling, charge decrement, and one-cast-per-round latch live in ONE place
// (no game-dynamics duplication into autoplay).
typedef enum {
    COMBAT_CAST_ILLEGAL   = -1,  // precondition failed (no hero/magic/charge/
                                 //   latch); NOTHING mutated
    COMBAT_CAST_NO_EFFECT =  0,  // legal cast but the effect was a no-op (e.g.
                                 //   IMMUNE target, full stack); charge NOT spent
    COMBAT_CAST_OK        =  1,  // cast applied; charge spent, latch consumed
} CombatCastOutcome;

// Cast combat spell `spell_idx` (0..6) from `side` at target (t_side,t_slot);
// dest_x/dest_y are the teleport destination (ignored by other spells). Reads
// spell_power from c->heroes[side]. Preconditions checked internally (returns
// COMBAT_CAST_ILLEGAL, mutating nothing, if any fail):
//   - side in range and c->heroes[side] != NULL  (the digest-path guard:
//     combat_run_headless leaves heroes[PLAYER]=NULL, so casting is inert there)
//   - heroes[side]->stats.knows_magic
//   - c->spells_this_round < 1   (one cast per side activation)
//   - spell_idx in [0,6] and heroes[side]->spells.counts[spell_idx] > 0
// On a successful cast decrements the charge and bumps spells_this_round.
int combat_cast_spell(Combat *c, int side, int spell_idx,
                      int t_side, int t_slot, int dest_x, int dest_y);

// The legal target filter (PickFilter) for a combat spell index, so the shell
// picker and the autoplay policy agree on who a spell may target.
int combat_spell_target_filter(int spell_idx);

#endif
