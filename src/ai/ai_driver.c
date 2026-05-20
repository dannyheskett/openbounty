// src/ai/ai_driver.c
//
// In-process AI driver. Each frame the driver:
//   1. Dismisses any blocking UI modal (dialog / view / prompt).
//   2. Asks the mission state machine for the current high-level
//      objective (PLAY_ZONE / GO_TO_DOCK / BOARD_BOAT / SAIL_NEXT /
//      ...).
//   3. Dispatches to a per-mission handler that produces either a
//      tile to step toward or a shell action to fire.
//
// The strategy module (ai_strategy.c) only handles PLAY_ZONE — the
// "what's the best thing to do on foot in this zone" question. The
// multi-step plans (rent a boat, sail to another continent) live in
// the mission layer (ai_mission.c) so we don't have stateless goal
// pickers fighting each other across travel modes.

#include "ai_driver.h"
#include "ai_nav.h"
#include "ai_strategy.h"
#include "ai_mission.h"

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
#include "views.h"            // views_dismiss(), views_town_invoke_row()
#include "combat_loop.h"      // combat_set_auto_player
#include "spells_adventure.h" // dispatch_adventure_spell
#include "tables.h"           // spell_index_by_id

#define AI_TOWN_ROW_BOAT      1   // mirrors TownRow enum in views.c
#define AI_TOWN_ROW_CONTRACT  0
#define AI_TOWN_ROW_SPELL     3
#define AI_TOWN_ROW_SIEGE     4

// Bridge between the SAIL_NEXT mission (dispatches NEW_CONTINENT,
// which opens a numeric prompt to pick a neighbor zone) and the
// prompt-dismissal code in ai_clear_ui (which resolves the numeric
// prompt). The mission sets pending=true with the desired pick; the
// prompt handler reads + clears.
static bool s_sail_prompt_pending = false;
static int  s_sail_prompt_pick    = 0;

struct AiDriver {
    AiConfig cfg;
    FILE    *trace;           // NULL if no trace file
    int      ticks;
    bool     finished;
    char     last_goal[64];
    char     last_action[96];

    AiMission mission;
    int       ticks_in_mission;     // resets on each mission transition
    int       ticks_since_sail;     // for SAIL_NEXT cooldown watchdog
    int       last_recover_tick;    // throttles end_week recoveries so
                                    // we don't burn days_left in a
                                    // recovery loop
    char      last_zone[24];        // tracks zone changes between ticks
    bool      sail_dispatched;      // SAIL_NEXT one-shot: only one NEW_CONTINENT
                                    // per mission entry. Cleared on transition out
                                    // of SAIL_NEXT or on a zone change.
    int       sail_pick_cycle;      // rotates over neighbors so successive sails
                                    // pick different destinations (avoids two-zone
                                    // ping-pong when neighbor[0] is always the
                                    // last visited zone)

    // Anti-stuck: remember the last few tiles we stepped onto. Revisits
    // count as "stuck"; enough strikes triggers a clean exit (we'd
    // rather end than wedge the user's terminal forever).
    int      recent_x[8];
    int      recent_y[8];
    int      recent_n;
    int      stuck_strikes;

    // Wander blacklist: tiles wander stepped onto recently and bounced
    // back from (interactive tiles like dwellings the AI can't drain,
    // towns with nothing to buy, etc.). Wander skips these so a
    // cornered AI doesn't oscillate on the same dead-end neighbor
    // forever. Entries expire after a fixed tick window.
    struct {
        int x, y;
        int expires_tick;
        char zone[24];
    } blacklist[8];
    int  blacklist_n;
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
    d->mission = AI_MISSION_PLAY_ZONE;

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

// ---- UI dismissal ----------------------------------------------------------

// Dismiss the topmost blocking UI element. Returns true if something
// was dismissed (caller treats the frame as consumed).
static bool ai_clear_ui(AiDriver *d) {
    if (prompt_is_active()) {
        // Default heuristics: yes to most yes/no (attack, recruit,
        // siege — combat AI handles the fight), 1 to numeric, A to ab.
        const char *kind = prompt_kind_str();
        if (strcmp(kind, "yes_no") == 0) {
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
            // The post-NEW_CONTINENT navigate prompt should rotate
            // through neighbors so we don't always pick the same
            // zone. The flag is one-shot — set when sail dispatches,
            // cleared here. Other numeric prompts (dismiss-army
            // slot picker etc.) get the default RESULT_1.
            PromptResult r = PROMPT_RESULT_1;
            if (s_sail_prompt_pending) {
                int pick = s_sail_prompt_pick;
                if (pick < 1) pick = 1;
                if (pick > 5) pick = 5;
                r = (PromptResult)(PROMPT_RESULT_1 + (pick - 1));
                s_sail_prompt_pending = false;
            }
            prompt_force_resolve(r);
            ai_log(d, "ui", "prompt(numeric)=%d", (int)(r - PROMPT_RESULT_1 + 1));
        } else if (strcmp(kind, "ab") == 0) {
            prompt_force_resolve(PROMPT_RESULT_1);
            ai_log(d, "ui", "prompt(ab)=A");
        } else if (strcmp(kind, "text") == 0) {
            // FLOW_RECRUIT clamps n to GameMaxRecruitable AND to
            // dwelling.count, so passing 9999 buys the maximum
            // possible. Drains the dwelling in one visit so the
            // strategy's "skip drained dwelling" filter kicks in
            // and we stop re-targeting it.
            prompt_force_resolve_text(9999);
            ai_log(d, "ui", "prompt(text)=9999");
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
        // VIEW_TOWN: let the mission layer drive the menu rows before
        // we dismiss. RENT_BOAT mission invokes the BOAT row from
        // here; future missions may take contracts or buy spells. The
        // PLAY_ZONE default falls through to the dismiss below — we
        // don't want to take a fresh contract every time the AI
        // walks into a town and breaks pathing.
        if (views_active() == VIEW_TOWN) {
            // Suppressed: handled by tactical_in_town() below, called
            // from the main tick BEFORE the dismiss step runs again.
            // Return false so the dispatcher's town handler can run.
            return false;
        }
        views_dismiss();
        ai_log(d, "ui", "dismiss view=%d", (int)views_active());
        return true;
    }
    return false;
}

// ---- anti-stuck bookkeeping -----------------------------------------------

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
        for (int i = 1; i < cap; i++) {
            d->recent_x[i - 1] = d->recent_x[i];
            d->recent_y[i - 1] = d->recent_y[i];
        }
        d->recent_x[cap - 1] = x;
        d->recent_y[cap - 1] = y;
    }
}

// True iff (zone,x,y) is currently on the wander blacklist (and the
// entry hasn't expired yet). Drops expired entries lazily as a side
// effect.
static bool ai_is_blacklisted(AiDriver *d, const char *zone, int x, int y) {
    int w = 0;
    for (int i = 0; i < d->blacklist_n; i++) {
        if (d->blacklist[i].expires_tick <= d->ticks) continue;
        if (w != i) d->blacklist[w] = d->blacklist[i];
        w++;
    }
    d->blacklist_n = w;
    for (int i = 0; i < d->blacklist_n; i++) {
        if (d->blacklist[i].x == x && d->blacklist[i].y == y &&
            strcmp(d->blacklist[i].zone, zone) == 0) return true;
    }
    return false;
}

// Add (zone,x,y) to the wander blacklist with a tick-count expiry.
// Capacity is fixed; oldest entries are evicted.
static void ai_blacklist_add(AiDriver *d, const char *zone, int x, int y,
                             int hold_ticks) {
    int cap = (int)(sizeof d->blacklist / sizeof d->blacklist[0]);
    for (int i = 0; i < d->blacklist_n; i++) {
        if (d->blacklist[i].x == x && d->blacklist[i].y == y &&
            strcmp(d->blacklist[i].zone, zone) == 0) {
            d->blacklist[i].expires_tick = d->ticks + hold_ticks;
            return;
        }
    }
    if (d->blacklist_n >= cap) {
        for (int i = 1; i < cap; i++) d->blacklist[i - 1] = d->blacklist[i];
        d->blacklist_n = cap - 1;
    }
    d->blacklist[d->blacklist_n].x = x;
    d->blacklist[d->blacklist_n].y = y;
    d->blacklist[d->blacklist_n].expires_tick = d->ticks + hold_ticks;
    snprintf(d->blacklist[d->blacklist_n].zone,
             sizeof d->blacklist[d->blacklist_n].zone, "%s", zone);
    d->blacklist_n++;
}

// ---- step helpers ----------------------------------------------------------

// Try to step toward (gx, gy). Returns true if a step was issued
// (success of the underlying step_try is best-effort — adventure
// engine handles blockers). The path-step uses boat-aware walkability:
// when on foot it treats the player's parked boat tile as walkable
// (boarding) so missions that need to board can path onto water.
static bool ai_step_toward(AiDriver *d, Game *g, Map *m, Fog *fog,
                           const Resources *res,
                           int gx, int gy, const char *label) {
    AiMoveMode mode = ai_move_mode_for(g);
    bool avoid_interact = true;
    AiStep step = ai_path_step(m, mode, g->position.x, g->position.y,
                               gx, gy, avoid_interact);
    if (!step.ok || (step.dx == 0 && step.dy == 0)) return false;
    ai_log(d, label, "step %+d%+d -> (%d,%d) dist=%d goal=(%d,%d)",
           step.dx, step.dy,
           g->position.x + step.dx, g->position.y + step.dy,
           step.dist, gx, gy);
    step_try(g, m, fog, res, step.dx, step.dy);
    return true;
}

// Fallback: take any walkable neighbor that isn't interactive. Used
// when the mission's goal is unreachable but we still want to wiggle
// (so stuck detection eventually trips a clean exit).
//
// Blacklist: when we have to fall through to pass 2 (the only walkable
// neighbor is an interactive tile), record the tile so we don't keep
// stepping onto the same dead-end dwelling / town / etc. that
// triggers a bounce-back. Pass 1 is preferred; if blacklist exhausts
// pass 2 we still try non-blacklisted interactives before giving up,
// then accept any walkable as last-resort.
static bool ai_step_wander(AiDriver *d, Game *g, Map *m, Fog *fog,
                           const Resources *res) {
    AiMoveMode mode = ai_move_mode_for(g);
    static const int dx[] = {  1, 0, -1,  0, 1, 1, -1, -1 };
    static const int dy[] = {  0, 1,  0, -1, 1,-1,  1, -1 };

    // Pass 1: prefer non-interactive, non-blacklisted neighbors.
    for (int i = 0; i < 8; i++) {
        int nx = g->position.x + dx[i];
        int ny = g->position.y + dy[i];
        if (!ai_walkable(m, nx, ny, mode, true)) continue;
        if (ai_is_blacklisted(d, g->position.zone, nx, ny)) continue;
        ai_log(d, "wander", "step %+d%+d -> (%d,%d) (non-interact)",
               dx[i], dy[i], nx, ny);
        step_try(g, m, fog, res, dx[i], dy[i]);
        return true;
    }
    // Pass 2: any walkable, non-blacklisted. Record an interactive
    // tile we step onto so the next stuck iteration won't pick it
    // again. Hold for 80 ticks — long enough to break the loop, short
    // enough that the AI revisits if state genuinely changed (e.g.
    // a dwelling repopulated at week-end).
    for (int i = 0; i < 8; i++) {
        int nx = g->position.x + dx[i];
        int ny = g->position.y + dy[i];
        if (!ai_walkable(m, nx, ny, mode, false)) continue;
        if (ai_is_blacklisted(d, g->position.zone, nx, ny)) continue;
        const Tile *t = MapGetTile(m, nx, ny);
        if (t && t->interactive != INTERACT_NONE) {
            ai_blacklist_add(d, g->position.zone, nx, ny, 80);
        }
        ai_log(d, "wander", "step %+d%+d -> (%d,%d) (any)",
               dx[i], dy[i], nx, ny);
        step_try(g, m, fog, res, dx[i], dy[i]);
        return true;
    }
    // Pass 3: last-resort, accept blacklisted. We're truly boxed in
    // and would rather trip stuck-strikes than freeze.
    for (int i = 0; i < 8; i++) {
        int nx = g->position.x + dx[i];
        int ny = g->position.y + dy[i];
        if (!ai_walkable(m, nx, ny, mode, false)) continue;
        ai_log(d, "wander", "step %+d%+d -> (%d,%d) (blacklisted-ok)",
               dx[i], dy[i], nx, ny);
        step_try(g, m, fog, res, dx[i], dy[i]);
        return true;
    }
    return false;
}

// ---- per-mission tactical handlers -----------------------------------------

// True iff the active contract's villain has been "found" (their
// castle is g->castles[i].known). Precondition for skipping the Find
// Villain cast.
static bool contract_villain_known(const Game *g) {
    if (!g || !g->contract.active_id[0]) return false;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (cr->owner_kind != CASTLE_OWNER_VILLAIN) continue;
        if (strcmp(cr->villain_id, g->contract.active_id) != 0) continue;
        return cr->known;
    }
    return false;
}

// Opportunistic adventure-spell casts. Called once per tick BEFORE
// mission dispatch. Each cast opens a dialog the next ai_clear_ui
// dismisses. Returns true if a spell was cast (the tile-step is
// skipped that tick so the dialog has a frame to render).
//
// Find Villain — when we have a contract whose villain isn't yet
//   located, casting reveals that villain's castle. The strategy's
//   contract-villain goal (priority 1) can then route us there.
//
// Time Stop — when days_left < 10 and no existing time-stop bonus
//   is running. Buys us steps without burning days.
//
// Castle Gate — not wired here; the shell-side letter-picker flow
//   is too entangled with main.c's input loop to invoke directly.
//
// dispatch_adventure_spell rejects casts when counts[spell] == 0 so
// we don't need to pre-check; the engine-level GameCastFindVillain /
// GameCastTimeStop decrement the charge.
static bool tactical_cast_spells(AiDriver *d, Game *g) {
    if (!g || !g->stats.knows_magic) return false;

    int fv = spell_index_by_id("find_villain");
    if (fv >= 0 && g->spells.counts[fv] > 0 &&
        g->contract.active_id[0] && !contract_villain_known(g)) {
        ai_log(d, "cast", "find_villain (charges=%d, contract=%s)",
               g->spells.counts[fv], g->contract.active_id);
        dispatch_adventure_spell(g, fv);
        return true;
    }

    int ts = spell_index_by_id("time_stop");
    if (ts >= 0 && g->spells.counts[ts] > 0 &&
        g->stats.time_stop == 0 && g->stats.days_left < 10) {
        ai_log(d, "cast", "time_stop (days_left=%d charges=%d)",
               g->stats.days_left, g->spells.counts[ts]);
        dispatch_adventure_spell(g, ts);
        return true;
    }

    return false;
}

// Walk toward whatever ai_strategy_pick returned. The strategy returns
// no goal when the zone is exhausted — at that point the mission
// layer should have already transitioned us away from PLAY_ZONE.
static bool tactical_play_zone(AiDriver *d, Game *g, Map *m, Fog *fog,
                               const Resources *res) {
    AiGoal goal = ai_strategy_pick(g, m, fog);
    if (!goal.ok) return false;
    return ai_step_toward(d, g, m, fog, res, goal.gx, goal.gy, goal.label);
}

// Walk to the nearest coastal town tile (ignoring `visited` because
// we may need to re-rent at a previously visited town after losing
// the boat to weekly upkeep).
static bool tactical_go_to_dock(AiDriver *d, Game *g, Map *m, Fog *fog,
                                const Resources *res) {
    int best = -1, bx = -1, by = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = MapGetTile(m, x, y);
            if (!t || t->interactive != INTERACT_TOWN) continue;
            if (!t->id[0]) continue;
            const ResTown *rt = resources_town_by_id(g->res, t->id);
            if (!rt) continue;
            if (rt->boat_x < 0 || rt->boat_y < 0) continue;
            AiStep s = ai_path_step(m, AI_MOVE_FOOT,
                                    g->position.x, g->position.y,
                                    x, y, true);
            if (!s.ok) continue;
            if (best < 0 || s.dist < best) {
                best = s.dist; bx = x; by = y;
            }
        }
    }
    if (best < 0) return false;
    char lbl[40];
    snprintf(lbl, sizeof lbl, "dock@%d,%d", bx, by);
    return ai_step_toward(d, g, m, fog, res, bx, by, lbl);
}

// Walk onto the parked boat tile.
static bool tactical_board_boat(AiDriver *d, Game *g, Map *m, Fog *fog,
                                const Resources *res) {
    if (!g->boat.has_boat ||
        strcmp(g->boat.zone, g->position.zone) != 0) return false;
    // We need the path-step to treat the boat tile as walkable on
    // foot. ai_walkable refuses water on foot, but stepping onto
    // boat.x/y is exactly how step.c boards. Compute the direction
    // manually if we're adjacent.
    int bx = g->boat.x, by = g->boat.y;
    int dx = bx - g->position.x;
    int dy = by - g->position.y;
    if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && (dx || dy)) {
        // Direct adjacency: step onto the boat tile.
        ai_log(d, "board_boat", "step %+d%+d -> (%d,%d) (board)",
               dx, dy, bx, by);
        step_try(g, m, fog, res, dx, dy);
        return true;
    }
    // Not adjacent — route toward the boat. Block interactive tiles
    // so we don't keep bumping into the town tile we just left.
    AiStep s = ai_path_step(m, AI_MOVE_FOOT,
                            g->position.x, g->position.y,
                            bx, by, true);
    if (!s.ok || (s.dx == 0 && s.dy == 0)) {
        // BFS rejected the goal (water tile) — walk toward the
        // nearest land tile adjacent to the boat.
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (!ox && !oy) continue;
                int ax = bx + ox, ay = by + oy;
                if (!ai_walkable(m, ax, ay, AI_MOVE_FOOT, false)) continue;
                AiStep s2 = ai_path_step(m, AI_MOVE_FOOT,
                                         g->position.x, g->position.y,
                                         ax, ay, true);
                if (s2.ok && (s2.dx || s2.dy)) {
                    char lbl[40];
                    snprintf(lbl, sizeof lbl, "board_boat@%d,%d", bx, by);
                    return ai_step_toward(d, g, m, fog, res, ax, ay, lbl);
                }
            }
        }
        return false;
    }
    char lbl[40];
    snprintf(lbl, sizeof lbl, "board_boat@%d,%d", bx, by);
    return ai_step_toward(d, g, m, fog, res, bx, by, lbl);
}

// Fire INPUT_ACTION_NEW_CONTINENT exactly once per entry into the
// SAIL_NEXT mission. After firing we wait for the zone to change
// (mission transitions to PLAY_ZONE) or for the watchdog to bail.
// Without this gate the AI would re-dispatch every tick — and if
// the arrival zone's spawn is on water, that produces zone-to-zone
// ping-pong without ever disembarking.
static bool tactical_sail_next(AiDriver *d, Game *g, ShellCtx *sctx,
                               bool *sail_dispatched) {
    (void)g;
    if (*sail_dispatched) {
        // Already requested; sit tight until the zone changes.
        return false;
    }
    InputState in = {0};
    in.action = INPUT_ACTION_NEW_CONTINENT;
    shell_dispatch_action(sctx, &in);
    *sail_dispatched = true;

    // Arm the navigate-prompt picker. Rotate through 3 entries (the
    // 3 neighbors a zone typically has) so we don't always sail back
    // to the same neighbor each cycle.
    s_sail_prompt_pending = true;
    s_sail_prompt_pick    = (d->sail_pick_cycle % 3) + 1;
    d->sail_pick_cycle++;

    ai_log(d, "sail_next", "dispatched NEW_CONTINENT from zone=%s pick=%d",
           g->position.zone, s_sail_prompt_pick);
    return true;
}

// (sail prompt bridge declared at file scope above)

// ---- per-tick orchestration -----------------------------------------------

bool ai_tick(AiDriver *d, Game *game, Map *map, Fog *fog,
             const Resources *res, ShellCtx *sctx) {
    if (!d || !game || !map) return false;
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

    // Pre-step: clear any blocking modal so we can act. VIEW_TOWN is
    // left intact for the town handler below (the mission may want
    // to invoke a row before dismissing).
    if (ai_clear_ui(d)) return true;

    // Endgame: standing on the scepter tile is an instant win. Honest
    // gate matches the strategy: only consider this if we've caught
    // every villain and found every artifact — the same prerequisites
    // a human player needs to assemble the puzzle map and locate the
    // scepter. The engine's FLOW_SEARCH itself only checks position
    // == scepter and not villain count, so the gate lives in the AI.
    if (GameVillainsCaught(game) >= 17 &&
        GameArtifactsFound(game) >= 8 &&
        game->scepter.zone[0] &&
        strcmp(game->scepter.zone, game->position.zone) == 0 &&
        game->scepter.x == game->position.x &&
        game->scepter.y == game->position.y) {
        InputState in = {0};
        in.action = INPUT_ACTION_SEARCH;
        shell_dispatch_action(sctx, &in);
        ai_log(d, "scepter", "SEARCH on (%d,%d) zone=%s",
               game->position.x, game->position.y, game->position.zone);
        return true;
    }

    // Town handler: if we're standing inside a town view, the
    // mission decides what to do. RENT_BOAT invokes the BOAT row
    // (rents a boat for cost gold). PLAY_ZONE pulls a fresh contract
    // and buys the for-sale spell when affordable. Other missions
    // just dismiss without taking actions.
    if (views_active() == VIEW_TOWN) {
        if (d->mission == AI_MISSION_GO_TO_DOCK ||
            d->mission == AI_MISSION_RENT_BOAT) {
            int cost = GameBoatCost(game);
            if (!game->boat.has_boat && game->stats.gold > cost) {
                views_town_invoke_row(game, AI_TOWN_ROW_BOAT);
                ai_log(d, "town", "BOAT row invoked (cost=%d gold=%d)",
                       cost, game->stats.gold);
            } else {
                ai_log(d, "town", "BOAT skipped (has=%d gold=%d cost=%d)",
                       (int)game->boat.has_boat, game->stats.gold, cost);
            }
        }
        if (d->mission == AI_MISSION_PLAY_ZONE) {
            // Take a fresh contract if we don't have one — the
            // strategy's villain goal can't fire without it.
            if (!game->contract.active_id[0]) {
                views_town_invoke_row(game, AI_TOWN_ROW_CONTRACT);
                ai_log(d, "town", "CONTRACT row invoked");
            }
            // Buy siege weapons if we don't have them and can afford
            // it. Siege weapons double damage during castle assaults,
            // which is the bulk of villain capture; the one-time
            // cost is worth it well before we've done much castle
            // work. game.json default siege_cost is 3000.
            int siege_cost = game->res->economy.siege_cost;
            if (!game->stats.siege_weapons &&
                game->stats.gold > siege_cost + 500) {
                views_town_invoke_row(game, AI_TOWN_ROW_SIEGE);
                ai_log(d, "town",
                       "SIEGE row invoked (cost=%d gold=%d)",
                       siege_cost, game->stats.gold);
            }
            // Buy the offered spell when we have a healthy gold
            // reserve and we're below the max-spells cap. The town
            // handler suppresses the "at cap" popup by gating on
            // GameKnownSpells (the engine still pops it if we beat
            // the gate by a race; harmless either way).
            int known = GameKnownSpells(game);
            if (known < game->stats.max_spells &&
                game->stats.gold > 1000) {
                views_town_invoke_row(game, AI_TOWN_ROW_SPELL);
                ai_log(d, "town",
                       "SPELL row invoked (known=%d cap=%d gold=%d)",
                       known, game->stats.max_spells, game->stats.gold);
            }
        }
        views_dismiss();
        ai_log(d, "ui", "dismiss town view");
        return true;
    }

    // Adventure spell casts (Find Villain, Time Stop). Runs BEFORE
    // mission dispatch so the spell's dialog is up by the time the
    // next tick's ai_clear_ui runs. The cast may have changed game
    // state the mission layer reads (e.g. find_villain reveals a
    // castle so the strategy's contract goal can fire).
    if (tactical_cast_spells(d, game)) return true;

    // Detect zone changes (sailing across continents) so the mission
    // layer can read a stable "we're somewhere new" signal.
    bool zone_changed = (d->last_zone[0] != '\0' &&
                         strcmp(d->last_zone, game->position.zone) != 0);
    snprintf(d->last_zone, sizeof d->last_zone, "%s", game->position.zone);
    if (zone_changed) {
        // Reset stuck history on zone change — we're starting fresh.
        d->recent_n = 0;
        d->stuck_strikes = 0;
        d->ticks_since_sail = 0;
        d->sail_dispatched = false;
    }
    if (d->mission == AI_MISSION_SAIL_NEXT) d->ticks_since_sail++;
    else                                    d->ticks_since_sail = 0;

    // Mission update. ai_zone_exhausted is the bedrock predicate —
    // if anything's reachable in this zone on foot, we stay in
    // PLAY_ZONE and the strategy keeps cycling local goals.
    AiMissionCtx ctx = {
        .g                  = game,
        .m                  = map,
        .fog                = fog,
        .zone_exhausted     = ai_zone_exhausted(game, map, fog),
        .boat_lost_recently = false,   // unused for now
        .ticks_since_sail   = d->ticks_since_sail,
        .zone_changed       = zone_changed,
        .ticks_in_mission   = d->ticks_in_mission,
    };
    AiMission next = ai_mission_update(d->mission, &ctx);
    if (next != d->mission) {
        ai_log(d, "mission", "%s -> %s (zone=%s exhausted=%d)",
               ai_mission_name(d->mission), ai_mission_name(next),
               game->position.zone, ctx.zone_exhausted);
        // Clearing the sail one-shot on any transition INTO SAIL_NEXT
        // means each entry fires exactly one NEW_CONTINENT; transitions
        // OUT also clear it for symmetry.
        if (d->mission == AI_MISSION_SAIL_NEXT ||
            next == AI_MISSION_SAIL_NEXT) {
            d->sail_dispatched = false;
        }
        d->mission = next;
        d->ticks_in_mission = 0;
    } else {
        d->ticks_in_mission++;
    }

    if (d->mission == AI_MISSION_DONE) {
        d->finished = true;
        ai_log(d, "exit", "mission DONE (zone=%s exhausted=%d)",
               game->position.zone, ctx.zone_exhausted);
        return true;
    }

    // Dispatch.
    int prev_x = game->position.x;
    int prev_y = game->position.y;
    bool acted = false;
    switch (d->mission) {
        case AI_MISSION_PLAY_ZONE:
            acted = tactical_play_zone(d, game, map, fog, res);
            break;
        case AI_MISSION_GO_TO_DOCK:
            acted = tactical_go_to_dock(d, game, map, fog, res);
            break;
        case AI_MISSION_RENT_BOAT:
            // The town handler above did the work. If we're still
            // in this mission this tick the view was already closed
            // OR we never entered it (bounced off the town tile);
            // either way, step toward the town to retry.
            acted = tactical_go_to_dock(d, game, map, fog, res);
            break;
        case AI_MISSION_BOARD_BOAT:
            acted = tactical_board_boat(d, game, map, fog, res);
            break;
        case AI_MISSION_SAIL_NEXT:
            acted = tactical_sail_next(d, game, sctx, &d->sail_dispatched);
            break;
        case AI_MISSION_FIND_SCEPTER:
        case AI_MISSION_SEARCH_SCEPTER:
        case AI_MISSION_DONE:
            // Unreachable: scepter missions aren't wired yet, DONE
            // exits above.
            break;
    }

    if (!acted) {
        // The mission's tactical layer had nothing to do this frame —
        // fall back to a wander step so stuck detection can fire.
        if (!ai_step_wander(d, game, map, fog, res)) {
            ai_log(d, "stuck", "no walkable neighbor");
            d->stuck_strikes++;
        }
    }

    // Stuck accounting.
    ai_remember_pos(d, game->position.x, game->position.y);
    int revisits = ai_revisit_count(d, game->position.x, game->position.y);
    bool moved = (prev_x != game->position.x || prev_y != game->position.y);
    if (revisits >= 4) {
        d->stuck_strikes++;
        ai_log(d, "stuck",
               "tile (%d,%d) revisited %dx; strikes=%d mission=%s",
               game->position.x, game->position.y, revisits,
               d->stuck_strikes, ai_mission_name(d->mission));
    } else if (moved && revisits <= 1) {
        d->stuck_strikes = 0;
    }

    // Mid-strike recovery: at strike 4, try advancing a week. Weekly
    // upkeep triggers astrology (which can repopulate a dwelling),
    // commission income (which may unstick gold-gated decisions), and
    // forces some game state to roll. Resets the strike counter on
    // success so the AI gets another window before bailing out.
    //
    // Throttled by `last_recover_tick`: only fire once per ~50 ticks
    // so we don't burn the entire days_left budget on a recovery
    // loop that the mission-level watchdog should be solving
    // instead.
    if (d->stuck_strikes == 4 && game->stats.days_left > 7 &&
        d->ticks - d->last_recover_tick > 50) {
        InputState in = {0};
        in.action = INPUT_ACTION_END_WEEK;
        shell_dispatch_action(sctx, &in);
        ai_log(d, "recover", "end_week (days_left=%d gold=%d)",
               game->stats.days_left, game->stats.gold);
        d->stuck_strikes = 0;
        // Clear position history so the freshly-changed map doesn't
        // immediately re-trip revisit detection.
        d->recent_n = 0;
        d->last_recover_tick = d->ticks;
    }

    if (d->stuck_strikes >= 12) {
        d->finished = true;
        ai_log(d, "exit", "stuck for too long, giving up (mission=%s)",
               ai_mission_name(d->mission));
    }

    return true;
}
