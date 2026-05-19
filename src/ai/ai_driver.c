// src/ai/ai_driver.c
//
// MVP scaffolding: observes Game/Map/Fog, dismisses blocking UI so
// the AI never sits stuck on a dialog, takes one step per frame in a
// dumb direction. Strategy modules slot in later — this file owns
// the frame-by-frame state machine and the trace log.

#include "ai_driver.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "tile.h"
#include "step.h"
#include "adventure.h"

#include "input.h"
#include "shell_ctx.h"
#include "shell_actions.h"
#include "prompt.h"
#include "ui.h"
#include "ui_host.h"          // views_active()
#include "views.h"            // views_dismiss()

struct AiDriver {
    AiConfig cfg;
    FILE    *trace;           // NULL if no trace file
    int      ticks;
    bool     finished;
    char     last_goal[64];
    char     last_action[96];
};

static void ai_log(AiDriver *d, const char *goal, const char *fmt, ...);

AiDriver *ai_create(const AiConfig *cfg_in) {
    AiDriver *d = (AiDriver *)calloc(1, sizeof *d);
    if (!d) return NULL;
    if (cfg_in) d->cfg = *cfg_in;
    if (d->cfg.trace_path && d->cfg.trace_path[0]) {
        d->trace = fopen(d->cfg.trace_path, "w");
        if (!d->trace) {
            fprintf(stderr, "ai: cannot open trace %s\n", d->cfg.trace_path);
        }
    }
    snprintf(d->last_goal,   sizeof d->last_goal,   "init");
    snprintf(d->last_action, sizeof d->last_action, "(none)");
    return d;
}

void ai_destroy(AiDriver *d) {
    if (!d) return;
    if (d->trace) fclose(d->trace);
    free(d);
}

bool ai_finished(const AiDriver *d)        { return d && d->finished; }
const char *ai_last_goal(const AiDriver *d)   { return d ? d->last_goal   : ""; }
const char *ai_last_action(const AiDriver *d) { return d ? d->last_action : ""; }

// ---- logging ---------------------------------------------------------------

static void ai_log(AiDriver *d, const char *goal, const char *fmt, ...) {
    if (!d) return;
    char action[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(action, sizeof action, fmt, ap);
    va_end(ap);

    if (goal)   snprintf(d->last_goal,   sizeof d->last_goal,   "%s", goal);
    snprintf(d->last_action, sizeof d->last_action, "%s", action);

    if (d->cfg.verbose) {
        fprintf(stderr, "ai[%d] %s :: %s\n", d->ticks, goal ? goal : "?", action);
    }
    if (d->trace) {
        // JSONL: {"t":N,"goal":"...","action":"..."}
        fprintf(d->trace, "{\"t\":%d,\"goal\":\"%s\",\"action\":\"%s\"}\n",
                d->ticks, goal ? goal : "", action);
        fflush(d->trace);
    }
}

// ---- per-frame tick --------------------------------------------------------

// Dismiss the topmost blocking UI element. Returns true if something
// was dismissed (caller treats the frame as consumed).
static bool ai_clear_ui(AiDriver *d) {
    if (prompt_is_active()) {
        // Default-safe answer: YES to most yes/no, 1 to numeric, A to ab.
        // The macro strategy will override this with context-aware
        // resolutions in later milestones. For MVP, NO to everything
        // so we never accidentally siege something we can't beat.
        const char *kind = prompt_kind_str();
        if (strcmp(kind, "yes_no") == 0) {
            prompt_force_resolve(PROMPT_RESULT_NO);
            ai_log(d, "ui", "prompt(yes_no)=NO");
        } else if (strcmp(kind, "numeric") == 0) {
            prompt_force_resolve(PROMPT_RESULT_1);
            ai_log(d, "ui", "prompt(numeric)=1");
        } else if (strcmp(kind, "ab") == 0) {
            prompt_force_resolve(PROMPT_RESULT_1);
            ai_log(d, "ui", "prompt(ab)=A");
        } else if (strcmp(kind, "text") == 0) {
            // Cancel — typing 0 would be accepted-but-zero. Cancel
            // unwinds the flow without committing.
            prompt_force_resolve(PROMPT_RESULT_CANCEL);
            ai_log(d, "ui", "prompt(text)=CANCEL");
        } else {
            prompt_force_resolve(PROMPT_RESULT_CANCEL);
            ai_log(d, "ui", "prompt(?)=CANCEL");
        }
        return true;
    }
    if (dialog_is_active()) {
        dialog_dismiss();
        ai_log(d, "ui", "dismiss dialog");
        return true;
    }
    if (views_active() != VIEW_NONE) {
        views_dismiss();
        ai_log(d, "ui", "dismiss view=%d", (int)views_active());
        return true;
    }
    return false;
}

// MVP placeholder for goal selection: just step east toward the
// scepter zone's general direction. Replaced by ai_strategy in next
// milestone.
static int s_dir_x[] = { 1, 0, -1,  0, 1, 1, -1, -1 };
static int s_dir_y[] = { 0, 1,  0, -1, 1,-1,  1, -1 };

static bool ai_pick_move(AiDriver *d, Game *g, Map *m, int *out_dx, int *out_dy) {
    // Try directions in order; pick the first walkable.
    bool flying = (g->character.mount == MOUNT_FLY);
    for (int i = 0; i < 8; i++) {
        int nx = g->position.x + s_dir_x[i];
        int ny = g->position.y + s_dir_y[i];
        const Tile *t = MapGetTile(m, nx, ny);
        if (!t) continue;
        bool ok = flying ? adventure_walkable_in_flight(t)
                         : (g->travel_mode == TRAVEL_BOAT
                            ? adventure_walkable_in_boat(t)
                            : adventure_walkable_on_foot(t));
        if (ok) {
            *out_dx = s_dir_x[i];
            *out_dy = s_dir_y[i];
            ai_log(d, "explore", "step %+d%+d -> (%d,%d)",
                   *out_dx, *out_dy, nx, ny);
            return true;
        }
    }
    return false;
}

bool ai_tick(AiDriver *d, Game *game, Map *map, Fog *fog,
             const Resources *res, ShellCtx *sctx) {
    if (!d || !game || !map) return false;
    (void)fog; (void)sctx;
    d->ticks++;

    if (d->cfg.max_ticks > 0 && d->ticks >= d->cfg.max_ticks) {
        d->finished = true;
        ai_log(d, "exit", "max_ticks=%d reached", d->cfg.max_ticks);
        return true;
    }

    if (game->stats.game_over) {
        d->finished = true;
        ai_log(d, "exit", "game_over (days_left=%d)", game->stats.days_left);
        return true;
    }

    // Pre-step: clear any blocking modal so we can act.
    if (ai_clear_ui(d)) return true;

    // Decide.
    int dx = 0, dy = 0;
    if (ai_pick_move(d, game, map, &dx, &dy)) {
        step_try(game, map, fog, res, dx, dy);
    } else {
        ai_log(d, "stuck", "no walkable neighbor");
    }
    return true;
}
