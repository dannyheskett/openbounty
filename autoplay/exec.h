// autoplay/exec.h
//
// The flat executor's shared declarations (AP-002): the execution context,
// the typed-cause seam (AP-083), the wait-allowed flag (AP-053), the
// gate-charge law floor (AP-110), and the cross-translation-unit helper
// surface. Primitives orchestrate; helpers are the only code that touches
// engine/game state.

#ifndef OB_AUTOPLAY_EXEC_H
#define OB_AUTOPLAY_EXEC_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "combat.h"
#include "goals.h"

// The one execution context threaded through every helper.
typedef struct ExecCtx {
    Game            *g;
    Map             *map;
    Fog             *fog;
    const Resources *res;
} ExecCtx;

// Typed failure seam (AP-083).
typedef enum {
    EXEC_CAUSE_NONE = 0,
    EXEC_CAUSE_OTHER,
    EXEC_CAUSE_NO_WINNING_ARMY,
    EXEC_CAUSE_STRANDED,      // done-but-marooned -> roll back
    EXEC_CAUSE_TIME,          // calendar exhausted, terminal
    EXEC_CAUSE_PREFOUGHT,     // keep-without-admit (AP-051)
    EXEC_CAUSE_GOLD,
    EXEC_CAUSE_STOCK,
    EXEC_CAUSE_LEADERSHIP,
    EXEC_CAUSE_REACH,
} ExecCause;

const char *exec_cause_name(ExecCause c);

// THE NO-BURN LAW (AP-053): wait sites fail immediately unless the planner
// has armed this flag for the zero-success pass.
void exec_set_wait_allowed(bool on);

// Shape the world for a long gold wait (boat return + army trim/swap).
// Idempotent; no-op unless waits are armed. Budget bisects MUST run after it.
void exec_prepare_gold_wait(ExecCtx *ctx);

// Suppress the mover's zero-day stop-restock loop (realize trips carry
// exact candidate armies and pending prompts a mid-move teleport would
// scramble).
void exec_set_gate_restock_suppressed(bool on);
bool exec_wait_allowed(void);

// The gate-charge law floor (AP-110, R-C): a cast is legal only from a supply
// of two (or one, to the town that sells the gate spell). Derived from the
// law -- the surviving charge is the escape -- not tuned.
#define GATE_LAW_MIN_CHARGES 2

// Gate-restock target for the recovery/pre-stock sites (AP-186/AP-189): the
// law floor plus a two-cast working margin (the approach's own casts).
#define AUTOPLAY_GATE_RESTOCK_TARGET (GATE_LAW_MIN_CHARGES + 2)

// ---- tuned planner levers, one definition site each -------------------------
// Values measured on the reference pack's 30-seed validation sweep. Where a
// pack or engine quantity exists, use sites derive from it; the rest are
// documented heuristics (R-A), candidates for by-outcome replacements.

// Deep-wallet gate for the charge-restock recovery and pre-stock paths (the
// crossing and move REACH recoveries, the stop-restock loop, the ratchet
// gate refill, the siege approach pre-stock): the wallet level at which
// spell-economy spending stops competing with the army economy, expressed
// in the pack's own spell-economy price scale -- the alcove fee, the price
// of entering that economy (the same anchor the logistics floor uses). The
// multiplier is measured on the reference validation sweep, where the gate
// evaluates to its long-validated value (6 x 5000 = 30000).
#define AUTOPLAY_RICH_WALLET_ALCOVES 6
#define AUTOPLAY_RICH_WALLET(res) \
    (AUTOPLAY_RICH_WALLET_ALCOVES * (res)->economy.alcove_cost)

// Pursuit-geometry radii in engine follow-range units (GAME_FOE_FOLLOW_RANGE):
// measured planner margins around the engine's own pursuit gate.
#define AUTOPLAY_PURSUIT_RADIUS    (4 * GAME_FOE_FOLLOW_RANGE)  // danger-scan range
#define AUTOPLAY_MOUTH_CAMP_RADIUS (GAME_FOE_FOLLOW_RANGE + 1)  // jaw-trap mouth band
#define AUTOPLAY_ESCAPE_CLEAR      (3 * GAME_FOE_FOLLOW_RANGE)  // escape success distance

// Reachable-node count below which a region reads as a POCKET (the maroon
// probe, AP-051): a measured heuristic expressed, like the sibling radii
// above, in engine follow-range units. (A functional replacement -- "no
// reachable tile clears the danger centers" -- was tried and regressed
// validation seeds; the judgment stays the measured node bound.)
#define AUTOPLAY_POCKET_NODES (4 * GAME_FOE_FOLLOW_RANGE)

// Count-based planner bounds. NOTHING here reads the remaining calendar:
// decisions are keyed on cycles/attempts/rounds so the run plays the same
// regardless of the day budget the difficulty granted, and the engine's own
// exec_spend_week TIME failure is the only thing that knows the calendar is
// running out (AP-050/053). Measured planner heuristics, chosen to keep every
// bound here calendar-agnostic.
#define KEYSTONE_STUCK_CYCLES        1  // promote the alcove once stuck > this many cycles (AP-055)
#define RECRUIT_RESTOCK_MAX_ROUNDS 512  // restock-loop safety backstop; engine TIME binds first

// Funded-wait working-capital margin: budget + budget/DEN (AP-162). Measured.
#define REALIZE_OVERHEAD_DEN 16

// Gold-wait pig-stack test (AP-161): swap the last remaining stack when its
// weekly upkeep exceeds commission_weekly / DEN -- the wait exists to harvest
// the commission, so the threshold scales with it. DEN measured.
#define GOLD_WAIT_PIG_DEN 8

// The spellbook split (AP-055/AP-112), in law-floor units (F =
// GATE_LAW_MIN_CHARGES) scaled by the pack's transport-gate count. The tier
// ladder is reverse-fitted to the measured widened-book tiers on the
// reference pack; the semantic reading -- the book must hold both law floors
// plus working stop stock before widening -- is constructed, not proven.
typedef struct {
    int  floor_all;         // n_transport_gates * F
    int  castle_gate_want;  // F, 2F, or 3F by book width
    bool town_participates; // town-gate stocking joins once the book affords both
    int  stop_clamp;        // cap on stop charges bought in one logistics pass
} BookBudget;
void exec_book_budget(const Game *g, BookBudget *out);

// ---- movement (autoplay/exec_move.c) ---------------------------------------

typedef struct {
    int zone_index;   // index into res->zones
    int x, y;
} NavPoint;

// Sentinel step-cost for a cross-zone target in query mode (AP-090): in-zone
// work always schedules ahead of a sail.
#define MV_XZONE_COST 1000000

// The single movement function (AP-090). Returns the arrival index of the
// cheapest reachable target (arrival = onto a passable target tile or
// adjacent-with-flow-fired for a bouncer), -1 to defer. In commit=false query
// mode nothing moves; the chosen index is returned with its step-cost in
// *out_cost (MV_XZONE_COST(+hops) for a cross-zone target).
int move_to(ExecCtx *ctx, const NavPoint *targets, int n, bool commit,
            int *out_cost, ExecCause *out_cause);

// Query-mode pricing of EVERY target in one relaxation: fills out_costs[i]
// with each target's step-cost (NAV-infinite = unreachable, MV_XZONE_COST+hops
// for cross-zone). Never moves.
void move_price_all(ExecCtx *ctx, const NavPoint *targets, int n,
                    int *out_costs);

// Mobility probe for the stranded rule (AP-051): reachable node count from
// the hero's live position (fight-through foes included). Never moves. The
// ax/ay/an danger centers (unbeatable hostiles) are sealed so a park inside
// a closing jaw reads as marooned; pass an=0 for an unconstrained probe.
int move_reachable_nodes_avoid(ExecCtx *ctx, const int *ax, const int *ay,
                               int an);
bool move_escape_jaw(ExecCtx *ctx, const int *ax, const int *ay, int an);

// Runaway watchdog for the movement leg loop (AP-102, R-A): far beyond any
// reachable behavior; hitting it is a defect to fix, never a scheduling signal.
#define NAV_MAX_LEGS 32

// ---- fights (autoplay/exec_fight.c) -----------------------------------------

// Player-side combat policy (deterministic, engine-probing; the same policy
// runs in the predictor and the live fight, AP-084/AP-133).
int autoplay_combat_policy(Combat *c, void *ctx);

// Resolve the currently-pending combat-bearing flow (siege/attack-foe):
// want_fight=false answers NO (bounce); want_fight=true runs the headless
// fight with the autoplay policy and routes the outcome through
// player_io_answer. Returns the engine CombatResult (WIN on want_fight only).
bool exec_fight(ExecCtx *ctx, bool want_fight, CombatResult *out_result);

// Predict a fight without touching the live world (AP-133, AP-183): simulate
// (mode, target garrison grown by `grow_weeks` of astrology, army override,
// what-if leadership / spellbook) on a throwaway copy under RNG
// snapshot/restore, memoized by the full input signature.
bool predict_combat_cached(const ExecCtx *ctx, CombatMode mode,
                           const char *seed_key, const char *display_name,
                           const Unit *garrison,
                           const ArmyStack *army_override,
                           int leadership_what_if,
                           const int *book_what_if,
                           int grow_weeks,
                           long *out_ai_hp);

// Predict with the LIVE army/leadership/book (the held-army check).
bool exec_fight_winnable(ExecCtx *ctx, CombatMode mode, const char *seed_key,
                         const char *display_name, const Unit *garrison,
                         int grow_weeks, long *out_ai_hp);

// ---- recruiting (autoplay/exec_recruit.c) -----------------------------------

typedef enum {
    RECRUIT_FOR_WIN = 0,
} RecruitMode;

typedef struct {
    RecruitMode  mode;
    CombatMode   combat_mode;
    const char  *seed_key;       // encounter identity (castle id / placement id)
    const char  *display_name;
    const Unit  *garrison;       // defender stacks (5)
    int          grow_weeks;     // astrology weeks before the fight (AP-183)
} RecruitRequest;

// The recruiting entry (AP-120): produce and carry out an acquisition plan
// whose fielded army the engine simulation verifies to win, or defer with a
// truthful cause.
bool exec_recruit(ExecCtx *ctx, const RecruitRequest *req, ExecCause *out_cause);

// Realize-failure memory (AP-138): per-run recruiter memory of (target,
// troop) plan shapes a realize proved unbuyable. Reset at run boot, and again
// on every search node restore (AP-208).
void recruit_exclusions_reset(void);

// Invalidate the mover's nav5 memo (AP-208). Needed only when a FOREIGN world
// is restored (the search jumping between lines of play); a same-play
// rollback keeps the memo valid.
void nav_cache_invalidate(void);

// Recruit-search memoization (AP-208). The search turns it on around its own
// expansions and reads the hit/miss counters; other callers leave it off
// (byte-identical). The key is complete, so an enabled cache returns the same
// winner the search would have computed.
void recruit_cache_enable(bool on);
void recruit_cache_reset(void);
void recruit_cache_stats(long *hits, long *misses);

// Pure projection queries for the scarce-winner conflict tier (AP-054).
bool recruit_winner_finite_draw(ExecCtx *ctx, const RecruitRequest *req,
                                int *out_draw /* per-troop units drawn, by
                                                 catalog index, caller-sized
                                                 CAT_TROOPS_MAX */);
bool recruit_winner_survives_less(ExecCtx *ctx, const RecruitRequest *req,
                                  const int *deduct, bool restock_ceiling);

// Gold-wait site (AP-161): dismiss into solvency, then wait exactly the weeks
// the engine's own income needs (wait-allowed pass only).
bool exec_ensure_gold(ExecCtx *ctx, int need, ExecCause *out_cause);
// The mid-move variant: waits on the STANDING army's income, never trims.
bool exec_ensure_gold_no_trim(ExecCtx *ctx, int need, ExecCause *out_cause);
// The last realize's banked approach reserve (0 = none) -- the siege
// approach keys its transport pre-stock on it.
int exec_last_realize_reserve(void);

// The raise-lift ladder (AP-132): find the smallest k of raise casts that
// turns the LIVE army into a predicted winner, acquire the charges, and (at
// the gate) cast them. exec_raise_for_fight finds+stocks; exec_rearm_raise_for
// re-arms the stock for a later fight and casts at the gate (cycling
// buy -> cast -> buy when the book cannot hold the whole stock at once).
int  exec_raise_k_for_win(ExecCtx *ctx, const RecruitRequest *req);
bool exec_raise_for_fight(ExecCtx *ctx, const RecruitRequest *req, int k);
bool exec_rearm_raise_for(ExecCtx *ctx, int k, int reserve_gold);

// Raise-cast ceiling (AP-102, runaway watchdog -- sized beyond any reachable
// lift; RAISE_K_MAX raise casts at floor 100 per cast is +6400 leadership).
#define RAISE_K_MAX 64

// ---- shared small helpers (autoplay/exec_loc.c) ------------------------------

int  zone_index_of(const Resources *res, const char *zone_id);
int  hero_zone_index(const ExecCtx *ctx);
bool exec_recorded_step(ExecCtx *ctx, int dx, int dy);   // one recorded GameStep
bool exec_land_here(ExecCtx *ctx);         // FLY -> RIDE on this tile (recorded)
bool exec_is_flying(const ExecCtx *ctx);

// The dismiss-last escape (marooned, no foe to lose to): sheds the army and
// takes the engine's "sent back to King" temp-death chain home. Recorded.
bool exec_dismiss_last_escape(ExecCtx *ctx);
void exec_temp_death(ExecCtx *ctx);        // the engine temp-death transition
bool exec_spend_week(ExecCtx *ctx, int acct_tag);
void exec_pump_passive(ExecCtx *ctx);                // drain messages/views/week dialogs
bool exec_answer_pending(ExecCtx *ctx, bool fight_ok); // resolve any pending decision
bool exec_gate_to(ExecCtx *ctx, bool town_gate, const char *dest_zone,
                  int dest_x, int dest_y);           // gate cast under R-C (AP-111)
bool exec_gate_dest_is_seller(ExecCtx *ctx, bool town_gate,
                              const char *dest_zone, int x, int y);
int  gate_spell_index(bool town_gate);
int  spell_charges(const Game *g, int spell_idx);
bool exec_buy_spell_at(ExecCtx *ctx, const char *town_id);
bool exec_stock_spell_charges_public(ExecCtx *ctx, int spell_idx, int want);
bool exec_stock_gate_charges(ExecCtx *ctx, bool town_gate, int want);
bool exec_topup_gate_kit(ExecCtx *ctx);
bool exec_cast_raise(ExecCtx *ctx);                  // one recorded raise cast
bool exec_discard_spell(ExecCtx *ctx, int spell_idx); // recorded field discard
bool exec_discard_junk_spell(ExecCtx *ctx, int keep_idx); // evict adventure junk (AP-112)
bool exec_garrison_slot(ExecCtx *ctx, const char *castle_id, int slot);
bool exec_ungarrison_slot(ExecCtx *ctx, const char *castle_id, int slot);
bool exec_dismiss_slot(ExecCtx *ctx, int slot);

#endif
