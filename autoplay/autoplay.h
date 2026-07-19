// autoplay/autoplay.h
//
// The autoplay system's public surface (AP-010..AP-017): the headless
// automated player and pack-winnability oracle. Engine-only: autoplay/ links
// libobengine.a and includes nothing from src/; the one shell adapter is
// src/shell_autoplay.c (shell -> autoplay -> engine).

#ifndef OB_AUTOPLAY_H
#define OB_AUTOPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "recording.h"

// The autoplay hero profile (AP-011): the same defaults feed headless and
// visible mode, so "same seed" means "same world". Class and difficulty are
// selectable per run (--autoplay-hero / --autoplay-level); these are the
// defaults when neither flag is given.
#define AUTOPLAY_HERO_NAME       "Oracle"
#define AUTOPLAY_HERO_CLASS      "knight"             // default class id
#define AUTOPLAY_HERO_DIFFICULTY DIFFICULTY_NORMAL
#define AUTOPLAY_DEFAULT_SEED_INDEX 1

typedef struct {
    int         seed_index;    // 0..255; the catalog world to prove
    const char *pack_dir;      // pack to load
    const char *hero_id;       // class id (NULL/"" => AUTOPLAY_HERO_CLASS)
    int         difficulty;    // DIFFICULTY_*; sets the day budget via the pack
} AutoplayConfig;

typedef struct {
    bool     solved;        // a full clear was found and committed
    bool     cancelled;     // the resolve was cancelled via the progress hook
    int      obj_total;
    int      best_done;     // most objectives any attempt completed; == obj_total
                            //   iff solved. The "how close" figure on NOT-SOLVED.
    int      moves;         // REC_MOVE primitives (the turn tally, AP-012)
    int      days_used;
    int      score;
    int      seed_index;
    char     unmet_label[64];  // first objective left undone (NOT-SOLVED)
    char     unmet_cause[96];  // ...and why
} AutoplayResult;

// Resolve-progress hook. The visible shell resolves headlessly before it can
// draw anything, so the search invokes this periodically with the committed /
// total objective counts; the host draws a progress frame and returns false to
// CANCEL. A function pointer keeps the engine-only autoplay layer free of any
// src/ dependency. NULL (the default, and every headless run) means no hook.
typedef bool (*AutoplayProgressFn)(int done, int total, void *ud);
void autoplay_set_progress(AutoplayProgressFn fn, void *ud);
// True when the last search_run was stopped by a false progress return.
bool autoplay_progress_cancelled(void);

// The first objective the last run left undone, and why (empty on a solve).
const char *autoplay_unmet_label(void);
const char *autoplay_unmet_cause(void);

// Headless run (AP-010): boot the pack + world for cfg->seed_index, call the
// search exactly once, fill *out. Returns false only on setup failure.
bool autoplay_run(const AutoplayConfig *cfg, AutoplayResult *out);

const char *autoplay_verdict_str(const AutoplayResult *r);

// The one dumb replay applier (AP-023): apply one recorded primitive to the
// live world. A fingerprint mismatch is a HARD FAILURE -- reported
// ([REPLAY-DIVERGE]) and aborted, never played through. Shared verbatim by
// headless verification and the visible mode.
struct ExecCtx;
bool plan_exec_step(struct ExecCtx *ctx, const RecPrim *p);

// exec_replay.c internals shared with plan_exec_step.
bool autoplay_apply_rec_action(struct ExecCtx *ctx, const RecPrim *p);
int  autoplay_apply_recorded_combat(struct ExecCtx *ctx, const RecPrim *p);

// Silence the end-of-line report. The search runs quiet while expanding --
// a node's report is read for its counts only -- and clears this for the one
// committed node, so exactly one line of play prints its diagnostics.
void planner_set_quiet(bool on);

// Suppress ONLY the [VERDICT READY] line (keeping [UNMET]/ledger). autoplay_run
// owns the one authoritative SOLVED/NOT-SOLVED verdict, so the committed node's
// report sets this off and prints its causes without a second verdict line.
void planner_set_print_verdict(bool on);

#endif
