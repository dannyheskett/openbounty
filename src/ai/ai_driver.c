// src/ai/ai_driver.c
//
// MVP scaffolding: observes Game/Map/Fog, dismisses blocking UI so
// the AI never sits stuck on a dialog, takes one step per frame in a
// dumb direction. Strategy modules slot in later — this file owns
// the frame-by-frame state machine and the trace log.

#include "ai_driver.h"
#include "ai_nav.h"
#include "ai_strategy.h"

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
#include "combat_loop.h"      // combat_set_auto_player

struct AiDriver {
    AiConfig cfg;
    FILE    *trace;           // NULL if no trace file
    int      ticks;
    bool     finished;
    char     last_goal[64];
    char     last_action[96];

    // Anti-stuck: remember the last few tiles we stepped onto. If we
    // revisit the same tile too many times in a short window we abandon
    // the current goal and try the next-best one.
    int      recent_x[8];
    int      recent_y[8];
    int      recent_n;
    int      stuck_strikes;
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

    // Auto-combat: when the AI driver enters a fight, RunCombat must
    // not block on keyboard input. Enabling auto_player makes RunCombat
    // route player turns through combat_ai_action — same routine that
    // drives enemy AI. Symmetric play, but no human input required.
    //
    // Fast-combat: bypass the animation rollover gate so a battle
    // resolves in a handful of frames instead of seconds of walk
    // cycles. The overworld driver pace is already 1 step / frame; if
    // a fight stretches to 30 seconds of animation, the AI's wall-time
    // throughput collapses.
    combat_set_auto_player(true);
    combat_set_fast_combat(true);
    return d;
}

void ai_destroy(AiDriver *d) {
    if (!d) return;
    if (d->trace) fclose(d->trace);
    combat_set_auto_player(false);
    combat_set_fast_combat(false);
    combat_set_player_ai(NULL, NULL);
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
        // Default heuristics: yes to most yes/no (attack, recruit,
        // siege — combat AI handles the fight), 1 to numeric, A to ab.
        // The macro strategy will refine this later (e.g. retreat on a
        // mismatched army).
        const char *kind = prompt_kind_str();
        if (strcmp(kind, "yes_no") == 0) {
            // Look for words that suggest the prompt is asking
            // permission to do something destructive to the player
            // (quit, dismiss army, etc.). For those we answer NO.
            const char *body = prompt_body_text();
            bool destructive = false;
            if (body) {
                static const char *no_words[] = {
                    "quit", "Quit", "dismiss", "Dismiss",
                    "delete", "Delete", "abandon", "Abandon",
                };
                for (size_t i = 0; i < sizeof no_words / sizeof no_words[0]; i++) {
                    if (strstr(body, no_words[i])) { destructive = true; break; }
                }
            }
            if (destructive) {
                prompt_force_resolve(PROMPT_RESULT_NO);
                ai_log(d, "ui", "prompt(yes_no)=NO (destructive)");
            } else {
                prompt_force_resolve(PROMPT_RESULT_YES);
                ai_log(d, "ui", "prompt(yes_no)=YES");
            }
        } else if (strcmp(kind, "numeric") == 0) {
            prompt_force_resolve(PROMPT_RESULT_1);
            ai_log(d, "ui", "prompt(numeric)=1");
        } else if (strcmp(kind, "ab") == 0) {
            prompt_force_resolve(PROMPT_RESULT_1);
            ai_log(d, "ui", "prompt(ab)=A");
        } else if (strcmp(kind, "text") == 0) {
            // Text prompts are usually recruit counts ("how many to
            // recruit?"). Accept the minimum non-zero so the dwelling
            // gets touched-and-consumed and the strategy stops
            // re-targeting it. Future revision: ask the strategy how
            // much to spend.
            prompt_force_resolve_text(1);
            ai_log(d, "ui", "prompt(text)=1");
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

static int ai_revisit_count(const AiDriver *d, int x, int y) {
    int n = 0;
    for (int i = 0; i < d->recent_n; i++) {
        if (d->recent_x[i] == x && d->recent_y[i] == y) n++;
    }
    return n;
}

static void ai_remember_pos(AiDriver *d, int x, int y) {
    int cap = (int)(sizeof d->recent_x / sizeof d->recent_x[0]);
    if (d->recent_n < cap) {
        d->recent_x[d->recent_n] = x;
        d->recent_y[d->recent_n] = y;
        d->recent_n++;
    } else {
        // Shift left.
        for (int i = 1; i < cap; i++) {
            d->recent_x[i - 1] = d->recent_x[i];
            d->recent_y[i - 1] = d->recent_y[i];
        }
        d->recent_x[cap - 1] = x;
        d->recent_y[cap - 1] = y;
    }
}

// Pick a one-tile move toward the strategy's current goal. Falls back to
// random-walkable if no path exists or the strategy returned no goal.
static bool ai_pick_move(AiDriver *d, Game *g, Map *m, Fog *fog,
                         int *out_dx, int *out_dy) {
    AiGoal goal = ai_strategy_pick(g, m, fog);
    AiMoveMode mode = ai_move_mode_for(g);

    if (goal.ok) {
        AiStep step = ai_path_step(m, mode,
                                   g->position.x, g->position.y,
                                   goal.gx, goal.gy, true);
        if (step.ok && (step.dx != 0 || step.dy != 0)) {
            *out_dx = step.dx;
            *out_dy = step.dy;
            ai_log(d, goal.label,
                   "step %+d%+d -> (%d,%d) dist=%d goal=(%d,%d)",
                   step.dx, step.dy,
                   g->position.x + step.dx, g->position.y + step.dy,
                   step.dist, goal.gx, goal.gy);
            return true;
        }
    }

    // Fallback: first-walkable scan so we don't freeze. The strategy
    // module rebids next frame.
    static const int dx[] = {  1, 0, -1,  0, 1, 1, -1, -1 };
    static const int dy[] = {  0, 1,  0, -1, 1,-1,  1, -1 };
    for (int i = 0; i < 8; i++) {
        int nx = g->position.x + dx[i];
        int ny = g->position.y + dy[i];
        if (!ai_walkable(m, nx, ny, mode, false)) continue;
        *out_dx = dx[i];
        *out_dy = dy[i];
        ai_log(d, "wander", "fallback step %+d%+d -> (%d,%d)",
               dx[i], dy[i], nx, ny);
        return true;
    }
    return false;
}

bool ai_tick(AiDriver *d, Game *game, Map *map, Fog *fog,
             const Resources *res, ShellCtx *sctx) {
    if (!d || !game || !map) return false;
    (void)sctx;
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
    if (ai_pick_move(d, game, map, fog, &dx, &dy)) {
        int prev_x = game->position.x;
        int prev_y = game->position.y;
        step_try(game, map, fog, res, dx, dy);
        // Record post-step position. If we revisit the same tile too
        // many times within the recent window, we're stuck. We keep
        // the history alive across strikes — only the strike counter
        // resets when we make genuine progress (moved to a new tile we
        // haven't seen often).
        ai_remember_pos(d, game->position.x, game->position.y);
        int revisits = ai_revisit_count(d, game->position.x, game->position.y);
        bool moved = (prev_x != game->position.x || prev_y != game->position.y);
        if (revisits >= 4) {
            d->stuck_strikes++;
            ai_log(d, "stuck",
                   "tile (%d,%d) revisited %dx; strikes=%d",
                   game->position.x, game->position.y, revisits,
                   d->stuck_strikes);
        } else if (moved && revisits <= 1) {
            // Real progress — reset strikes.
            d->stuck_strikes = 0;
        }
        if (d->stuck_strikes >= 6) {
            d->finished = true;
            ai_log(d, "exit", "stuck for too long, giving up");
        }
    } else {
        ai_log(d, "stuck", "no walkable neighbor");
        d->stuck_strikes++;
        if (d->stuck_strikes >= 8) {
            d->finished = true;
            ai_log(d, "exit", "no moves available, giving up");
        }
    }
    return true;
}
