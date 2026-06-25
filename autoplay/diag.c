// autoplay/diag.c
//
// Structured decision-trace sink (see diag.h). Pure: reads its DiagEvent and
// writes one line to stdout. No engine calls, no RNG, no Game/Map mutation —
// the property that keeps combat digests / mode-parity / seed verdicts
// byte-identical with tracing on.

#include "diag.h"

#include <stdio.h>

// Parallel to the DiagCause enum. One name per value, in declaration order.
// A static assert below pins the length to DIAG_CAUSE_COUNT so a new enum
// value without a name is a compile error rather than a silent mismatch.
static const char *const DIAG_CAUSE_NAMES[] = {
    "DIAG_NONE",
    "DIAG_NAV_UNREACHABLE",
    "DIAG_NAV_TRANSIENT",
    "DIAG_COMBAT_LOSS_PATHFOE",
    "DIAG_COMBAT_LOSS_RECRUIT_EXHAUSTED",
    "DIAG_COMBAT_LOSS_RECRUIT_UNREACHABLE",
    "DIAG_MOBILITY_TRAP",
    "DIAG_RENT_NO_GOLD",
    "DIAG_SIEGE_WON_NO_GARRISON",
    "DIAG_OTHER_ZONE",
    "DIAG_TIME_EXHAUSTED",
    "DIAG_FLY_REQUIRED_NO_ARMY",
    "DIAG_FLY_NO_LANDING",
    "DIAG_BUDGET_TURNS",
    "DIAG_STALL",
    "DIAG_ITERS",
    "DIAG_ARRIVAL_NOOP",
    "DIAG_NO_TOWN",
    "DIAG_CONTRACT_UNAVAILABLE",
    "DIAG_COMBAT_LOSS_GOAL_SIEGE",
    "DIAG_DIED_PREDICTED_WIN",
    "DIAG_DIED_MIDROUTE",
    "DIAG_INTERV_KEEP_ADMIT",
    "DIAG_INTERV_KEEP_PREREQ",
    "DIAG_INTERV_KEEP_POWER",
    "DIAG_INTERV_KEEP_GOLD",
    "DIAG_INTERV_REJECT_FLAT",
    "DIAG_INTERV_REJECT_GATED",
    "DIAG_INTERV_DIED",
    "DIAG_PLANNER_SET_FULL",
    "DIAG_STEP_SET_OVERFLOW",
    "DIAG_ADMITTED",
    "DIAG_ALL_RESOLVED",
    "DIAG_GAME_OVER_CLEAN",
};

// Compile-time guard: the name table must cover every enum value exactly.
// (C99 has no static_assert; this negative-size-array trick fails to compile
// if the counts diverge.)
typedef char diag_cause_names_size_check[
    (sizeof(DIAG_CAUSE_NAMES) / sizeof(DIAG_CAUSE_NAMES[0]) == DIAG_CAUSE_COUNT)
        ? 1 : -1];

static const char *const DIAG_EVENT_NAMES[] = {
    "STEP", "PREDICT", "NAV", "RECRUIT", "CAND", "INTERV", "RUNCAP",
};
typedef char diag_event_names_size_check[
    (sizeof(DIAG_EVENT_NAMES) / sizeof(DIAG_EVENT_NAMES[0]) == DIAG_EV_COUNT)
        ? 1 : -1];

const char *diag_cause_name(DiagCause c) {
    if (c < 0 || c >= DIAG_CAUSE_COUNT) return "DIAG_?";
    return DIAG_CAUSE_NAMES[c];
}

const char *diag_event_name(DiagEventKind k) {
    if (k < 0 || k >= DIAG_EV_COUNT) return "?";
    return DIAG_EVENT_NAMES[k];
}

void diag_init(DiagSink *s, bool enabled, int level, bool json) {
    if (!s) return;
    s->enabled = enabled;
    s->level = level > 0 ? level : 1;
    s->json = json;
}

void diag_emit(const DiagSink *s, const DiagEvent *ev) {
    if (!s || !s->enabled || !ev) return;

    const char *evname = diag_event_name(ev->kind);
    const char *cause  = diag_cause_name(ev->cause);
    const char *extra  = ev->extra ? ev->extra : "";

    if (s->json) {
        // One JSON object per line (JSONL). The `extra` tail is already
        // key=val space-separated; emit it as a raw string field so the line
        // stays valid JSON without re-parsing it here. Tooling that wants the
        // tail structured can split extra on spaces / '='.
        fprintf(stdout,
                "{\"t\":\"aptrace\",\"ev\":\"%s\",\"seed\":%lu,\"cand\":%d,"
                "\"tick\":%d,\"cause\":\"%s\",\"extra\":\"%s\"}\n",
                evname, ev->seed, ev->cand, ev->tick, cause, extra);
        return;
    }

    // Greppable text: fixed leading triple, then cause, then the caller's tail.
    if (extra[0])
        fprintf(stdout, "aptrace %s seed=%lu cand=%d tick=%d cause=%s %s\n",
                evname, ev->seed, ev->cand, ev->tick, cause, extra);
    else
        fprintf(stdout, "aptrace %s seed=%lu cand=%d tick=%d cause=%s\n",
                evname, ev->seed, ev->cand, ev->tick, cause);
}
