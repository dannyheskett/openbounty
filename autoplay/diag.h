// autoplay/diag.h
//
// Always-on (in headless mode) STRUCTURED DECISION TRACE for autoplay.
//
// Why this exists, separate from report.c: report_log emits a free-text decision
// line, but plan_build re-flattens the four CandResult enum values into four vague
// strings ("deferred X: not reachable on current prefix") — which conflate
// genuinely distinct causes (navigation unreachable, combat predicted-loss,
// contract unavailable, mobility trap, per-candidate budget tripped,
// livelock/stall, hero died). The diag sink preserves those distinctions.
//
// The diag sink fixes this WITHOUT touching report.c:
//   - it is INDEPENDENT of report.c, so it fires for the inner planner too;
//   - it is gated by a single `enabled` bit set only by the headless runner
//     (visible mode constructs no enabled sink, so it stays silent);
//   - it writes ONE structured, greppable line per event to STDOUT, sharing the
//     stream with the verdict / boot / combat-digest lines; the fixed "aptrace"
//     prefix lets either set of lines be filtered out of the other;
//   - every emit is PURE: it formats scalars already in hand and writes to the
//     stream. It calls no engine function, draws no RNG, and mutates no Game/Map.
//     This is what keeps the combat digests, mode-parity, and per-seed
//     verdicts byte-identical with tracing on.
//
// Engine-only; depends on libc stdio + stdbool/stdint.

#ifndef OB_AUTOPLAY_DIAG_H
#define OB_AUTOPLAY_DIAG_H

#include <stdbool.h>
#include <stdint.h>

// The single failure-cause taxonomy. Every deferral / failure / keep-reject
// resolves to exactly one value, so `grep cause=DIAG_COMBAT_LOSS` finds every
// combat deferral across STEP / CAND / PREDICT events regardless of which
// verdict bucket the old enum produced. Keep DIAG_CAUSE_NAMES (diag.c) in sync.
typedef enum {
    DIAG_NONE = 0,

    // ---- Deferral / unreachable causes (today all -> "not reachable") -------
    DIAG_NAV_UNREACHABLE,            // target/gate genuinely unreachable foot+boat
    DIAG_NAV_TRANSIENT,              // nearest-adjacent found but step_toward != OK
    DIAG_COMBAT_LOSS_PATHFOE,        // path-foe predicted LOSS, no recruits -> skip
    DIAG_COMBAT_LOSS_RECRUIT_EXHAUSTED,   // recruited every source, still lost
    DIAG_COMBAT_LOSS_RECRUIT_UNREACHABLE, // recruit source itself unreachable
    DIAG_MOBILITY_TRAP,              // objective done but hero can't reach home
    DIAG_RENT_NO_GOLD,               // boat rental refused: gold <= cost (engine
                                     // BOAT_RENT_NO_GOLD) — an economy failure,
                                     // NOT a navigation one
    DIAG_SIEGE_WON_NO_GARRISON,      // siege WON but single stack can't garrison;
                                     // the win is rolled back with the candidate
    DIAG_OTHER_ZONE,                 // objective lives in another zone than the
                                     // prefix; deferred until a committed travel
                                     // step moves the prefix there (never routed
                                     // to cross-zone)
    DIAG_TIME_EXHAUSTED,             // candidate simulation ran the calendar to
                                     // zero days (game over by time, hero alive)
                                     // — deferred with the honest time cause,
                                     // never FAILED (not a pack defect)
    DIAG_FLY_REQUIRED_NO_ARMY,       // target sealed off from every surface
                                     // route; a landing site exists but the
                                     // army cannot fly (yet) — the fly
                                     // recompose lever is the unlock
    DIAG_FLY_NO_LANDING,             // target sealed AND its foot region holds
                                     // no legal landing tile — unreachable even
                                     // by air (a genuine pack defect)
    DIAG_BUDGET_TURNS,               // per-candidate turn budget exhausted
    DIAG_STALL,                      // progress fingerprint frozen MAX_STALL ticks
    DIAG_ITERS,                      // MAX_ITERS exhausted (pure-ANSWERED livelock)
    DIAG_ARRIVAL_NOOP,               // reached tile, objective not done, no flow
    DIAG_NO_TOWN,                    // no reachable town for a non-tile-bound step
    DIAG_CONTRACT_UNAVAILABLE,       // contract cycle can't produce this villain

    // ---- Hard-fail (unwinnable) ---------------------------------------------
    DIAG_COMBAT_LOSS_GOAL_SIEGE,     // goal-siege predicted LOSS, recruits exhausted

    // ---- Death --------------------------------------------------------------
    DIAG_DIED_PREDICTED_WIN,         // predicted WIN but actually LOST (a bug)
    DIAG_DIED_MIDROUTE,              // death to an unpredicted mid-route hazard

    // ---- Intervention keep / reject -----------------------------------------
    DIAG_INTERV_KEEP_ADMIT,          // crit_a: directly admitted an objective
    DIAG_INTERV_KEEP_PREREQ,         // crit_b1: prereq chain progress
    DIAG_INTERV_KEEP_POWER,          // crit_b2: army growth toward blocked fight
    DIAG_INTERV_KEEP_GOLD,           // crit_c:  banked spoils gold (CLEAR_FOE)
    DIAG_INTERV_REJECT_FLAT,         // genuine zero measurable change
    DIAG_INTERV_REJECT_GATED,        // positive delta suppressed by a gate
    DIAG_INTERV_DIED,                // intervention ran and game_over
    DIAG_PLANNER_SET_FULL,           // intervention search aborted (set full)
    DIAG_STEP_SET_OVERFLOW,          // KEPT intervention dropped (STEP_MAX)

    // ---- Success terminals (so a run is fully accountable) ------------------
    DIAG_ADMITTED,                   // candidate admitted (plan simulates WIN)
    DIAG_ALL_RESOLVED,              // run reached "no actionable goal" cleanly
    DIAG_GAME_OVER_CLEAN,            // natural game end (days out, etc.)

    DIAG_CAUSE_COUNT,
} DiagCause;

// Event kinds. The line format is:
//   aptrace <EVENT> seed=S cand=G tick=N cause=C <key=val ...>
typedef enum {
    DIAG_EV_STEP = 0,   // a driver decision / BLOCKED-skip site
    DIAG_EV_PREDICT,    // a combat prediction (with spell-blindness evidence)
    DIAG_EV_NAV,        // a planner-chokepoint nav failure (level 2)
    DIAG_EV_RECRUIT,    // a recruit excursion outcome (level 2)
    DIAG_EV_CAND,       // a candidate outcome (ADMITTED/UNREACHABLE/...)
    DIAG_EV_INTERV,     // an intervention keep / reject
    DIAG_EV_RUNCAP,     // a run_to_terminal terminal-stop reason
    DIAG_EV_COUNT,
} DiagEventKind;

// The trace sink. Pure config: a stack value, no allocation, no engine handle.
typedef struct {
    bool enabled;   // false => every diag_emit is an instant no-op (visible mode)
    int  level;     // 1 = STEP/PREDICT/CAND/INTERV/RUNCAP; >=2 adds NAV/RECRUIT
    bool json;      // true => one JSON object per line instead of key=val text
} DiagSink;

// One trace event, populated by the call site, serialized by the sink. A stack
// struct — never heap. `extra` holds the event-specific "key=val ..." tail
// already formatted by the caller (no spaces inside values); the sink prepends
// the fixed "aptrace EVENT seed= cand= tick= cause=" header. Keep `extra` free
// of newlines.
typedef struct {
    DiagEventKind kind;
    DiagCause     cause;
    unsigned long seed;
    int           cand;     // candidate goal index, or -1 for run-level
    int           tick;     // planner tick within this inner run
    const char   *extra;    // pre-formatted, space-separated key=val tail (or NULL)
} DiagEvent;

// Initialize a sink. enabled=false makes the whole facility inert.
void diag_init(DiagSink *s, bool enabled, int level, bool json);

// Emit one event. No-op when s is NULL, !s->enabled, or ev->kind is gated out
// by s->level (NAV/RECRUIT need level >= 2). Writes to stdout. Pure: formats
// scalars + ev->extra; calls no engine function, draws no RNG, mutates nothing.
void diag_emit(const DiagSink *s, const DiagEvent *ev);

// Human/grep name for a cause (e.g. "DIAG_COMBAT_LOSS_PATHFOE"). Stable across
// runs; used by the sink and by callers building log lines. Returns "DIAG_?"
// for an out-of-range value rather than indexing past the table.
const char *diag_cause_name(DiagCause c);

// Human/grep token for an event kind (e.g. "STEP", "PREDICT").
const char *diag_event_name(DiagEventKind k);

#endif
