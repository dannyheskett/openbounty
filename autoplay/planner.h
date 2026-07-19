// autoplay/planner.h
//
// The re-entrant planner core (AP-050..AP-058): the ordering and attempt
// machinery exposed as a step API so the snapshot-tree search can expand ONE
// decision at a time -- planner_open (enumerate + state init),
// planner_candidates (logistics + pricing + ordering + promotion tiers),
// planner_step (one atomic attempt), planner_done (goal test). There is no
// full-game loop here: the search is the only driver. All planner memory
// rides in PlannerRun rather than in statics, so a search node can carry it.

#ifndef OB_AUTOPLAY_PLANNER_H
#define OB_AUTOPLAY_PLANNER_H

#include <stdbool.h>
#include <stdint.h>

#include "exec.h"
#include "goals.h"

typedef struct {
    bool      done;
    char      defer_why[96];
    ExecCause defer_cause;
    bool      ever_failed;      // has any attempt on this objective failed yet
    int       stuck_cycles;     // consecutive planner cycles this objective has
                                // stayed open+failed; the keystone promotion
                                // (AP-055) keys off it, calendar-free
} ObjState;

// Failure history keyed by objective IDENTITY, not enumeration index: a
// planner_open on a progressed world re-enumerates a SHRUNK universe (dead
// foes and consumed tiles drop out), so indices shift between opens while the
// history must follow the objective. 32-bit FNV over (kind, zone, x, y,
// handle); deterministic, so even a collision behaves identically per build.
typedef struct {
    uint32_t  key;              // 0 = empty slot
    uint8_t   ever_failed;
    uint8_t   stuck_cycles;
    uint8_t   defer_cause;      // ExecCause of the LAST failure (truthful
    char      defer_why[64];    //   [UNMET] reporting across re-opens)
} ObjHist;

// All planner memory for one line of play. A search node copies this whole
// struct, so every node evolves its own copy.
typedef struct {
    ObjState st[STEP_MAX];      // parallel to the CURRENT enumeration
    ObjHist  hist[STEP_MAX * 2];// identity-keyed failure history (open-addressed)
    bool     wait_armed;        // the no-burn law's one wait pass (AP-053)
    int      sacrifices;        // deliberate-defeat escapes taken on this line
    int      cycles_used;       // watchdog budget consumed
} PlannerRun;

// One orderable decision: the objective's stable identity plus its transient
// index into the CURRENT enumeration (valid until the next planner_open).
typedef struct {
    int  kind, zone_index, x, y;
    char handle[32];
    int  step_index;
} PlanCand;

typedef enum {
    PLANNER_STEP_FAIL = 0,      // attempt failed and rolled back whole
    PLANNER_STEP_OK,            // objective accomplished and admitted
    PLANNER_STEP_KEPT,          // PREFOUGHT: world kept, objective not done
    PLANNER_STEP_TERMINAL,      // calendar death / lost game: line is over
} PlannerStepResult;

// Enumerate the objective universe from the live world and initialize
// run->st (done flags from the world, failure history from run->hist).
// Reloads the hero's zone map (enumeration reuses a scratch map). Returns
// false on enumeration failure (no verdict possible). Allocates the attempt
// snapshot; every open is paired with planner_close.
bool planner_open(ExecCtx *ctx, PlannerRun *run);
void planner_close(void);

// One cycle head: age stuck counters, run spell logistics, build the ordered
// candidate list (mover pricing, zone tie-break, keystone / scarce-winner /
// alcove promotion tiers). Writes up to `cap` candidates; returns the count.
// Zero with planner_done() false means only the scepter remains gated or the
// line is terminal. The finale rule (AP-052) is applied here: the scepter is
// offered only when every other objective is done.
int planner_candidates(ExecCtx *ctx, PlannerRun *run, PlanCand *out, int cap);

// One atomic attempt of `cand`: prerequisite gates then the objective itself,
// under one world snapshot, with the maroon probe on success (planner_attempt
// does the work). Honors run->wait_armed for this attempt only. Updates
// run->st, run->hist, and the world.
PlannerStepResult planner_step(ExecCtx *ctx, PlannerRun *run,
                               const PlanCand *cand);

// The deliberate-defeat escape (temp death home): offered by the search when
// a node has no succeeding candidate. Bounded by run->sacrifices.
// Returns true when an escape was taken (world changed).
bool planner_step_sacrifice(ExecCtx *ctx, PlannerRun *run);

// Refresh done flags from the world (in-passing completions): a siege can
// complete a foe objective in passing and vice versa, and the predicate is
// the only truth. planner_step deliberately does NOT do this for the OTHER
// objectives, so a caller that wants per-step truth -- the search does, since
// each node's progress must be exact -- calls this after every step.
void planner_refresh_done(ExecCtx *ctx, PlannerRun *run);

// Goal test: every enumerated objective (scepter included) done.
bool planner_done(const ExecCtx *ctx, const PlannerRun *run);

// The first objective (enumeration order) `run` left undone, for a
// pack-validation report: its display label and a short cause phrase. Writes
// empty strings when everything is done. Reads the CURRENT enumeration, so
// call it against the committed node's run right after planner_report.
void planner_first_unmet(const PlannerRun *run, char *out_label, int label_cap,
                         char *out_cause, int cause_cap);

// The end-of-line report ([UNMET] causes + verdict + ledger), from run->st.
// Honors the quiet / print-verdict switches (autoplay.h). Returns done count.
int planner_report(ExecCtx *ctx, PlannerRun *run, int *out_total);

#endif
