// autoplay/exec_ledger.h
//
// Calendar/economy ledger (AP-172). Behaviour-inert: counters live in
// file-statics outside Game, so world fingerprints and saves are untouched.
// A LedgerSnap embedded in the WorldSnapshot reverts the committed tallies
// with a rolled-back attempt; the gross tally survives rollbacks.

#ifndef OB_AUTOPLAY_EXEC_LEDGER_H
#define OB_AUTOPLAY_EXEC_LEDGER_H

typedef enum {
    DAY_ACCT_OTHER = 0,
    DAY_ACCT_APPROACH,
    DAY_ACCT_RECRUIT,
    DAY_ACCT_CONTRACT,
    DAY_ACCT_GOLDWAIT,
    DAY_ACCT_CROSSING,
    DAY_ACCT_RECRUITMOVE,
    DAY_ACCT_STOCKMOVE,
    DAY_ACCT_COUNT,
} DayAcctTag;

typedef struct {
    long committed[DAY_ACCT_COUNT];
    // Recruiter-move split (snapshotted with the committed day tallies).
    long rct_inzone_trips, rct_inzone_walk;
    long rct_offzone_trips, rct_offzone_walk, rct_offzone_cross;
} LedgerSnap;

void ledger_reset(void);
void day_acct_add(DayAcctTag tag, int days);

// Current committed day tally for one tag. Used by the movement-tag booking
// sites to factor out crossing days spent inside an approach (AP-172), so the
// approach and crossing tags partition the calendar instead of double-counting.
long ledger_committed(DayAcctTag tag);

// Book a movement-tagged delta (before_days/now_days are days_left around the
// move; cross_before is ledger_committed(CROSSING) sampled before it) with the
// crossing days spent during the move factored out.
void ledger_book_move(DayAcctTag tag, int before_days, int now_days,
                      long cross_before);

// Watchdog saturation telemetry (AP-102): a named counter bumped when a
// runaway guard or sized cap actually saturates. Behaviour-inert (counters
// only; printed by ledger_report under --verbose). A nonzero count is a
// defect to investigate, never a scheduling signal.
void watchdog_hit(const char *name);
void ledger_snap(LedgerSnap *out);
void ledger_unsnap(const LedgerSnap *snap);
void ledger_report(void);

#endif
