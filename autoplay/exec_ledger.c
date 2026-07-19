// autoplay/exec_ledger.c -- day accounting (AP-172). Behaviour-inert.

#include "exec_ledger.h"

#include <stdio.h>
#include <string.h>

#include "diag.h"

static long s_gross[DAY_ACCT_COUNT];
static long s_committed[DAY_ACCT_COUNT];

// Recruiter-move telemetry (AP-172 diag): recruit realize trips split by whether
// the trip crossed a zone (off-zone source: off-zone dwelling, home castle,
// garrison, or spell-town in another zone) or stayed in-zone. Committed --
// reverted with the day tallies on a rolled-back attempt (LedgerSnap).
static long s_rct_inzone_trips, s_rct_inzone_walk;
static long s_rct_offzone_trips, s_rct_offzone_walk, s_rct_offzone_cross;

static const char *tag_name(int t) {
    switch (t) {
    case DAY_ACCT_OTHER:    return "other";
    case DAY_ACCT_APPROACH: return "approach";
    case DAY_ACCT_RECRUIT:  return "recruit";
    case DAY_ACCT_CONTRACT: return "contract";
    case DAY_ACCT_GOLDWAIT: return "goldwait";
    case DAY_ACCT_CROSSING: return "crossing";
    case DAY_ACCT_RECRUITMOVE: return "recruitmv";
    case DAY_ACCT_STOCKMOVE: return "stockmv";
    default:                return "?";
    }
}

#define WATCHDOG_MAX 16
static struct {
    const char *name;
    long        hits;
} s_watchdogs[WATCHDOG_MAX];
static int s_watchdog_n;

void watchdog_hit(const char *name) {
    if (!name) return;
    for (int i = 0; i < s_watchdog_n; i++) {
        if (strcmp(s_watchdogs[i].name, name) == 0) {
            s_watchdogs[i].hits++;
            return;
        }
    }
    if (s_watchdog_n >= WATCHDOG_MAX) return;
    s_watchdogs[s_watchdog_n].name = name;   // callers pass string literals
    s_watchdogs[s_watchdog_n].hits = 1;
    s_watchdog_n++;
}

void ledger_reset(void) {
    memset(s_gross, 0, sizeof s_gross);
    memset(s_committed, 0, sizeof s_committed);
    memset(s_watchdogs, 0, sizeof s_watchdogs);
    s_watchdog_n = 0;
    s_rct_inzone_trips = s_rct_inzone_walk = 0;
    s_rct_offzone_trips = s_rct_offzone_walk = s_rct_offzone_cross = 0;
}

void day_acct_add(DayAcctTag tag, int days) {
    if (tag < 0 || tag >= DAY_ACCT_COUNT || days <= 0) return;
    s_gross[tag] += days;
    s_committed[tag] += days;
}

long ledger_committed(DayAcctTag tag) {
    if (tag < 0 || tag >= DAY_ACCT_COUNT) return 0;
    return s_committed[tag];
}

// Book a movement-tagged calendar delta with the crossing days spent during the
// move factored out -- a sail books DAY_ACCT_CROSSING at its real day-delta
// (exec_move.c nav_travel_hop), so the enclosing approach/recruit move must not
// count those same days again or the tags double-count and overshoot days_used.
void ledger_book_move(DayAcctTag tag, int before_days, int now_days,
                      long cross_before) {
    int total = before_days - now_days;
    long cross = s_committed[DAY_ACCT_CROSSING] - cross_before;
    int walk = total - (int)cross;
    day_acct_add(tag, walk);
    if (tag == DAY_ACCT_RECRUITMOVE) {
        if (cross > 0) {
            s_rct_offzone_trips++;
            if (walk > 0) s_rct_offzone_walk += walk;
            s_rct_offzone_cross += cross;
        } else {
            s_rct_inzone_trips++;
            if (walk > 0) s_rct_inzone_walk += walk;
        }
    }
}

void ledger_snap(LedgerSnap *out) {
    if (!out) return;
    memcpy(out->committed, s_committed, sizeof s_committed);
    out->rct_inzone_trips = s_rct_inzone_trips;
    out->rct_inzone_walk = s_rct_inzone_walk;
    out->rct_offzone_trips = s_rct_offzone_trips;
    out->rct_offzone_walk = s_rct_offzone_walk;
    out->rct_offzone_cross = s_rct_offzone_cross;
}

void ledger_unsnap(const LedgerSnap *snap) {
    if (!snap) return;
    memcpy(s_committed, snap->committed, sizeof s_committed);
    s_rct_inzone_trips = snap->rct_inzone_trips;
    s_rct_inzone_walk = snap->rct_inzone_walk;
    s_rct_offzone_trips = snap->rct_offzone_trips;
    s_rct_offzone_walk = snap->rct_offzone_walk;
    s_rct_offzone_cross = snap->rct_offzone_cross;
}

void ledger_report(void) {
    if (!ob_diag_verbose()) return;
    long gross_total = 0, committed_total = 0;
    for (int t = 0; t < DAY_ACCT_COUNT; t++) {
        gross_total += s_gross[t];
        committed_total += s_committed[t];
    }
    printf("[LEDGER] committed days by tag (gross incl. rolled-back):\n");
    for (int t = 0; t < DAY_ACCT_COUNT; t++) {
        if (s_gross[t] == 0 && s_committed[t] == 0) continue;
        printf("[LEDGER]   %-9s committed=%ld gross=%ld\n",
               tag_name(t), s_committed[t], s_gross[t]);
    }
    printf("[LEDGER]   total     committed=%ld gross=%ld\n",
           committed_total, gross_total);
    if (s_rct_inzone_trips || s_rct_offzone_trips)
        printf("[LEDGER]   recruitmv split: in-zone %ld trips/%ld days; "
               "off-zone %ld trips/%ld walk +%ld cross\n",
               s_rct_inzone_trips, s_rct_inzone_walk,
               s_rct_offzone_trips, s_rct_offzone_walk, s_rct_offzone_cross);
    for (int i = 0; i < s_watchdog_n; i++)
        printf("[LEDGER]   watchdog %-12s hits=%ld\n",
               s_watchdogs[i].name, s_watchdogs[i].hits);
}
