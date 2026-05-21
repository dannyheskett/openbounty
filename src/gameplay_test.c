#include "gameplay_test.h"
#include "input_host.h"
#include "frame_host.h"
#include "shell_run.h"
#include "startup.h"
#include "tables.h"
#include "combat.h"
#include "tile.h"
#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ----- Scenario forward decls ---------------------------------------------
static int scenario_noop(int argc, char **argv);
static int scenario_overworld_step(int argc, char **argv);
static int scenario_recruit_at_home(int argc, char **argv);
static int scenario_capture_murray(int argc, char **argv);

static const GpScenario SCENARIOS[] = {
    { "noop",
      "smoke-test the runner; exits 0 without touching the game",
      scenario_noop },
    { "overworld_step",
      "drive startup, dismiss intro dialog, walk one tile north, assert position",
      scenario_overworld_step },
    { "recruit_at_home",
      "walk to king_maximus, recruit pikemen+militia+archers, assert army HP",
      scenario_recruit_at_home },
    { "capture_murray",
      "full Murray-capture run: take contract, recruit, sail, siege, fulfill",
      scenario_capture_murray },
};

static const int SCENARIO_COUNT = (int)(sizeof SCENARIOS / sizeof SCENARIOS[0]);

const GpScenario *gp_scenario_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < SCENARIO_COUNT; i++) {
        if (strcmp(SCENARIOS[i].name, name) == 0) return &SCENARIOS[i];
    }
    return NULL;
}

void gp_scenario_list(void) {
    for (int i = 0; i < SCENARIO_COUNT; i++) {
        printf("  %-32s %s\n", SCENARIOS[i].name, SCENARIOS[i].description);
    }
}

void gp_runner_init(void) {
    input_host_use_queue();
    frame_host_use_test();
    startup_skip_intros = true;
}

// =========================================================================
// noop — Slice 1 smoke test. No game interaction.
// =========================================================================

static int scenario_noop(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("gameplay-test: noop scenario passed\n");
    return 0;
}

// =========================================================================
// overworld_step — Slice 2. Drives the splashes (auto-advance on test
// clock), class-select (press A → knight), name entry (press ENTER →
// default "Hero"), difficulty (press ENTER → normal), the post-init
// "godlike actions" dialog (press SPACE → dismiss), then walks one
// tile north and asserts the hero is at (11,57) on seed=1 continentia.
// =========================================================================

typedef struct {
    int  phase;          // 0=pre-dialog, 1=post-dialog, 2=post-move
    int  move_queued_at; // frame the UP key was queued
} GpStepState;

static void overworld_step_before_startup(ShellRunHooks *self) {
    (void)self;
    // Class select: press A for knight (the picker accepts A/B/C/D).
    input_host_queue_key(KEY_A);
    // Name entry: press ENTER on an empty buffer → engine substitutes
    // res.world.default_name ("Hero" on the king's-bounty pack).
    input_host_queue_key(KEY_ENTER);
    // Difficulty selection defaults to Normal (sel=1). Press ENTER to
    // accept.
    input_host_queue_key(KEY_ENTER);
    // run_new_game_intro auto-advances after its timeout, no key
    // needed.
}

static void overworld_step_after_init(ShellRunHooks *self,
                                      Game *g, Map *m, Fog *f,
                                      Resources *res) {
    (void)self; (void)m; (void)f; (void)res;
    // Sanity check before the loop starts.
    if (strcmp(g->character.cls.id, "knight") != 0) {
        fprintf(stderr,
                "gameplay-test: expected class=knight, got class=%s\n",
                g->character.cls.id);
    }
    if (g->position.x != 11 || g->position.y != 58) {
        fprintf(stderr,
                "gameplay-test: unexpected start position (%d,%d), "
                "expected (11,58) on seed=1 continentia\n",
                g->position.x, g->position.y);
    }
}

static GpVerdict overworld_step_per_frame(ShellRunHooks *self,
                                          Game *g, Map *m, Fog *f,
                                          Resources *res, int frame_no) {
    (void)m; (void)f; (void)res;
    GpStepState *st = (GpStepState *)self->user;

    // Phase 0: wait for the "godlike actions" dialog to be active,
    // then queue SPACE to dismiss it.
    if (st->phase == 0) {
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            st->phase = 1;
        }
        if (frame_no > 1200) {
            fprintf(stderr,
                    "gameplay-test: timed out waiting for intro dialog\n");
            return GP_VERDICT_EXIT_FAIL;
        }
        return GP_VERDICT_CONTINUE;
    }

    // Phase 1: dialog dismissed → queue UP to step north.
    if (st->phase == 1) {
        if (!dialog_is_active()) {
            input_host_queue_key(KEY_UP);
            st->move_queued_at = frame_no;
            st->phase = 2;
        }
        if (frame_no > st->move_queued_at + 1200) {
            fprintf(stderr,
                    "gameplay-test: timed out waiting for dialog to clear\n");
            return GP_VERDICT_EXIT_FAIL;
        }
        return GP_VERDICT_CONTINUE;
    }

    // Phase 2: wait for hero to actually move one tile north.
    if (st->phase == 2) {
        if (g->position.x == 11 && g->position.y == 57) {
            printf("gameplay-test: overworld_step PASS — hero at (11,57) "
                   "after one UP press\n");
            return GP_VERDICT_EXIT_PASS;
        }
        if (frame_no > st->move_queued_at + 240) {
            fprintf(stderr,
                    "gameplay-test: hero failed to step north; "
                    "at (%d,%d), expected (11,57)\n",
                    g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        return GP_VERDICT_CONTINUE;
    }

    return GP_VERDICT_EXIT_FAIL;
}

static int scenario_overworld_step(int argc, char **argv) {
    GpStepState state = { 0, 0 };
    ShellRunHooks hooks = {
        .before_startup = overworld_step_before_startup,
        .after_init     = overworld_step_after_init,
        .per_frame      = overworld_step_per_frame,
        .user           = &state,
    };
    return shell_run_game(argc, argv, &hooks);
}

// =========================================================================
// Shared helpers
// =========================================================================

// Queue the standard startup sequence: knight class, default name,
// normal difficulty. With startup_skip_intros set by gp_runner_init,
// splashes/credits/new-game-intro all auto-skip and only these three
// keys are needed.
static void queue_standard_startup(void) {
    input_host_queue_key(KEY_A);     // class select: A = knight
    input_host_queue_key(KEY_ENTER); // name entry: empty -> "Hero"
    input_host_queue_key(KEY_ENTER); // difficulty: Normal (default)
}

// Queue the numeric digits of `n` followed by ENTER for the
// recruit-soldiers count-entry prompt.
static void queue_recruit_count(int n) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", n);
    for (const char *p = buf; *p; p++) {
        input_host_queue_key(KEY_ZERO + (*p - '0'));
    }
    input_host_queue_key(KEY_ENTER);
}

static int army_total_hp(const Game *g) {
    int hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0]) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        if (t) hp += t->hit_points * g->army[i].count;
    }
    return hp;
}

// =========================================================================
// recruit_at_home — Drive startup, walk to king_maximus, press A
// (Recruit), recruit pikemen×10 + militia×30 + archers×8, exit, walk
// back to spawn, assert gold and army HP match expectations.
//
// Recruit screen sorts dwelling="castle" troops by recruit_cost
// ascending: A=militia(50), B=archers(250), C=pikemen(300).
// So to recruit pikemen×10 we queue C → "10" → ENTER.
// =========================================================================

typedef enum {
    RECRUIT_PHASE_DISMISS_INTRO = 0,
    RECRUIT_PHASE_WALK_TO_GATE,
    RECRUIT_PHASE_WAIT_HOME_VIEW,
    RECRUIT_PHASE_OPEN_RECRUIT,
    RECRUIT_PHASE_WAIT_RECRUIT_VIEW,
    RECRUIT_PHASE_DO_RECRUITS,
    RECRUIT_PHASE_LEAVE_RECRUIT,
    RECRUIT_PHASE_LEAVE_HOME,
    RECRUIT_PHASE_VERIFY,
} RecruitPhase;

typedef struct {
    RecruitPhase phase;
    int          phase_started_at;
} RecruitState;

#define GP_TIMEOUT_FAIL(state_field, max_frames, msg) \
    do { \
        if (frame_no > (state_field) + (max_frames)) { \
            fprintf(stderr, "gameplay-test: timeout — %s\n", (msg)); \
            return GP_VERDICT_EXIT_FAIL; \
        } \
    } while (0)

static void recruit_at_home_before_startup(ShellRunHooks *self) {
    (void)self;
    queue_standard_startup();
}

static GpVerdict recruit_at_home_per_frame(ShellRunHooks *self,
                                           Game *g, Map *m, Fog *f,
                                           Resources *res, int frame_no) {
    (void)m; (void)f; (void)res;
    RecruitState *st = (RecruitState *)self->user;

    switch (st->phase) {
    case RECRUIT_PHASE_DISMISS_INTRO:
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            st->phase = RECRUIT_PHASE_WALK_TO_GATE;
            st->phase_started_at = frame_no;
        }
        GP_TIMEOUT_FAIL(0, 1200, "intro dialog never appeared");
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_WALK_TO_GATE:
        // Wait for the dialog to clear, then walk north toward
        // king_maximus at (11,56). Stepping onto a castle gate "bounces
        // back" the hero (position reverts to the prior tile) and pushes
        // VIEW_HOME_CASTLE, so we drive by view-state, not position.
        if (views_active() == VIEW_HOME_CASTLE) {
            st->phase = RECRUIT_PHASE_WAIT_HOME_VIEW;
            st->phase_started_at = frame_no;
            return GP_VERDICT_CONTINUE;
        }
        if (!dialog_is_active() && views_active() == VIEW_NONE) {
            input_host_queue_key(KEY_UP);
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 600,
                        "stuck walking to king_maximus");
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_WAIT_HOME_VIEW:
        if (views_active() == VIEW_HOME_CASTLE && !dialog_is_active()) {
            st->phase = RECRUIT_PHASE_OPEN_RECRUIT;
            st->phase_started_at = frame_no;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 300,
                        "VIEW_HOME_CASTLE never opened");
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_OPEN_RECRUIT:
        input_host_queue_key(KEY_A);  // A) Recruit Soldiers
        st->phase = RECRUIT_PHASE_WAIT_RECRUIT_VIEW;
        st->phase_started_at = frame_no;
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_WAIT_RECRUIT_VIEW:
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            // Recipe: pikemen(C)×10, militia(A)×30, archers(B)×8.
            // Queue the whole thing — the recruit screen consumes one
            // letter+count per ENTER then returns to idle.
            input_host_queue_key(KEY_C);  // pikemen
            queue_recruit_count(10);
            input_host_queue_key(KEY_A);  // militia
            queue_recruit_count(30);
            input_host_queue_key(KEY_B);  // archers
            queue_recruit_count(8);
            st->phase = RECRUIT_PHASE_DO_RECRUITS;
            st->phase_started_at = frame_no;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 300,
                        "VIEW_RECRUIT_SOLDIERS never opened");
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_DO_RECRUITS:
        // Wait until army HP reaches the expected 300. The recruit
        // screen consumes queued input one frame at a time; this
        // typically takes ~12 frames for three letter+digits+ENTER
        // bundles.
        if (army_total_hp(g) >= 300) {
            input_host_queue_key(KEY_ESCAPE);  // exit recruit screen
            st->phase = RECRUIT_PHASE_LEAVE_RECRUIT;
            st->phase_started_at = frame_no;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 300,
                        "army HP never reached 300");
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_LEAVE_RECRUIT:
        if (views_active() == VIEW_HOME_CASTLE) {
            input_host_queue_key(KEY_ESCAPE);  // exit home castle
            st->phase = RECRUIT_PHASE_LEAVE_HOME;
            st->phase_started_at = frame_no;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "VIEW_RECRUIT_SOLDIERS never dismissed");
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_LEAVE_HOME:
        if (views_active() == VIEW_NONE) {
            st->phase = RECRUIT_PHASE_VERIFY;
            st->phase_started_at = frame_no;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "VIEW_HOME_CASTLE never dismissed");
        return GP_VERDICT_CONTINUE;

    case RECRUIT_PHASE_VERIFY: {
        int hp = army_total_hp(g);
        // Expected after recipe: 60 (start: militia×20 + archers×2)
        // + pikemen×10 (100) + militia×30 (60) + archers×8 (80) = 300.
        if (hp != 300) {
            fprintf(stderr,
                    "gameplay-test: expected army HP 300, got %d\n", hp);
            return GP_VERDICT_EXIT_FAIL;
        }
        // Expected gold: 7500 (start) - 3000 (pikemen×10×300)
        // - 1500 (militia×30×50) - 2000 (archers×8×250) = 1000.
        if (g->stats.gold != 1000) {
            fprintf(stderr,
                    "gameplay-test: expected gold 1000, got %d\n",
                    g->stats.gold);
            return GP_VERDICT_EXIT_FAIL;
        }
        printf("gameplay-test: recruit_at_home PASS "
               "(army HP=300, gold=1000)\n");
        return GP_VERDICT_EXIT_PASS;
    }
    }
    return GP_VERDICT_EXIT_FAIL;
}

static int scenario_recruit_at_home(int argc, char **argv) {
    RecruitState state = { RECRUIT_PHASE_DISMISS_INTRO, 0 };
    ShellRunHooks hooks = {
        .before_startup = recruit_at_home_before_startup,
        .per_frame      = recruit_at_home_per_frame,
        .user           = &state,
    };
    return shell_run_game(argc, argv, &hooks);
}

// =========================================================================
// capture_murray — full Murray capture scenario.
//
// Path on seed=1, knight starting from (11,58) in continentia:
//   1. dismiss godlike-actions dialog
//   2. walk to japper town (gate at 13,55) — closest town to spawn
//   3. press A → take Murray contract
//   4. exit town
//   5. walk to king_maximus (11,56) — recruit pikemen×10 + militia×30 +
//      archers×8 = 240 HP added; total army HP 300, gold 1000 left
//   6. exit, walk back to japper
//   7. press B → rent boat (cost = GameBoatCost; on Normal that's
//      a few hundred gold). Boat appears at japper's boat slot (12,56)
//   8. exit town, walk onto (12,56) to board the boat
//   9. BFS-sail across water to a tile adjacent to azram's gate
//      (azram is at (30,36), gate at (30,37))
//   10. disembark, walk onto azram gate (30,37) → siege prompt
//   11. press Y → enter rendered combat
//   12. drive combat with a simple per-unit heuristic until WIN
//   13. dismiss victory dialog, dismiss "you captured a villain"
//       follow-up, assert villains_caught[murray] flag is set
//
// =========================================================================

// ---- Path-finding (BFS over MapWalkable + boat-aware extension) --------

#define GP_PATH_MAX 256

typedef struct {
    int x, y;
} GpPoint;

typedef enum {
    GP_TRAVEL_WALK = 0,
    GP_TRAVEL_BOAT,
} GpTravel;

// BFS path from (sx,sy) to (gx,gy). `mode` selects which terrain
// counts as passable: WALK = grass+desert (TerrainWalkable);
// BOAT = water-only (or land tiles adjacent for disembark).
//
// Returns the number of points stored in `out` (path includes the
// start tile at index 0 and the goal at the end). 0 if no path.
static int gp_bfs(const Map *m, int sx, int sy, int gx, int gy,
                  GpTravel mode, GpPoint *out, int out_cap) {
    if (!m || !MapInBounds(m, sx, sy) || !MapInBounds(m, gx, gy)) return 0;
    int W = m->width;
    int H = m->height;
    // parent[y*W+x] = direction taken to enter the tile, encoded as
    // (dy+1)*3 + (dx+1) + 1; 0 = unvisited.
    short *parent = (short *)calloc((size_t)W * H, sizeof(short));
    if (!parent) return 0;

    GpPoint *queue = (GpPoint *)malloc(sizeof(GpPoint) * (size_t)W * H);
    if (!queue) { free(parent); return 0; }
    int qh = 0, qt = 0;
    queue[qt++] = (GpPoint){ sx, sy };
    parent[sy * W + sx] = 1;  // mark visited with no-direction sentinel

    bool found = (sx == gx && sy == gy);

    static const int dxs[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    static const int dys[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

    while (!found && qh < qt) {
        GpPoint p = queue[qh++];
        for (int k = 0; k < 8; k++) {
            int nx = p.x + dxs[k];
            int ny = p.y + dys[k];
            if (!MapInBounds(m, nx, ny)) continue;
            if (parent[ny * W + nx]) continue;
            const Tile *t = MapGetTile(m, nx, ny);
            if (!t) continue;
            bool ok = false;
            if (mode == GP_TRAVEL_WALK) {
                ok = TerrainWalkable(t->terrain) && !t->blocks_foot;
                // Avoid stepping onto interactive tiles that would
                // open a screen, fire a prompt, or teleport the hero.
                // Treasure chests and artifacts just open a y/n prompt
                // we can decline, so they're tolerable along the path.
                // Telecaves teleport unconditionally, so they're not.
                if (ok && t->interactive != INTERACT_NONE &&
                    t->interactive != INTERACT_TREASURE_CHEST &&
                    t->interactive != INTERACT_ARTIFACT) {
                    ok = false;
                }
            } else {
                // Boat travel: water tiles (or bridges). Allow the
                // destination tile to be land too so we can disembark
                // by stepping onto coastline (will be filtered by goal
                // check).
                ok = (t->terrain == TERRAIN_WATER) || t->is_bridge;
            }
            // Always allow the goal even if mode-blocked (lets walk
            // mode reach a castle gate that's marked blocks_foot etc.;
            // boat mode lets us "reach" the goal coord even if it's
            // technically land — the disembark step is handled by the
            // caller).
            if (!ok && (nx != gx || ny != gy)) continue;
            parent[ny * W + nx] = (short)((dys[k] + 1) * 3 + (dxs[k] + 1) + 1);
            queue[qt++] = (GpPoint){ nx, ny };
            if (nx == gx && ny == gy) { found = true; break; }
        }
    }

    int n = 0;
    if (found) {
        // Walk parent chain back to start.
        GpPoint stack[GP_PATH_MAX];
        int sp = 0;
        int cx = gx, cy = gy;
        while (sp < GP_PATH_MAX) {
            stack[sp++] = (GpPoint){ cx, cy };
            if (cx == sx && cy == sy) break;
            short enc = parent[cy * W + cx];
            int dir = (int)enc - 1;
            int dy = dir / 3 - 1;
            int dx = dir % 3 - 1;
            cx -= dx;
            cy -= dy;
        }
        // Reverse into out[].
        n = (sp <= out_cap) ? sp : out_cap;
        for (int i = 0; i < n; i++) {
            out[i] = stack[sp - 1 - i];
        }
    }
    free(queue);
    free(parent);
    return n;
}

// Returns the raylib KEY_ for the direction needed to step from
// (cx,cy) to (nx,ny). 0 if not adjacent.
static int gp_dir_key(int cx, int cy, int nx, int ny) {
    int dx = nx - cx;
    int dy = ny - cy;
    if (dx == 0  && dy == -1) return KEY_UP;
    if (dx == 0  && dy ==  1) return KEY_DOWN;
    if (dx == -1 && dy ==  0) return KEY_LEFT;
    if (dx == 1  && dy ==  0) return KEY_RIGHT;
    if (dx == -1 && dy == -1) return KEY_HOME;
    if (dx == 1  && dy == -1) return KEY_PAGE_UP;
    if (dx == -1 && dy ==  1) return KEY_END;
    if (dx == 1  && dy ==  1) return KEY_PAGE_DOWN;
    return 0;
}

// ---- Combat decision helper --------------------------------------------

// Pick the (side=1) enemy unit with the smallest Manhattan distance
// from (ux,uy). Returns slot, or -1 if no enemies.
static int gp_closest_enemy(const Combat *c, int ux, int uy,
                            int *out_dist) {
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const CombatUnit *e = &c->units[COMBAT_SIDE_AI][i];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        int adx = e->x - ux; if (adx < 0) adx = -adx;
        int ady = e->y - uy; if (ady < 0) ady = -ady;
        int d = (adx > ady) ? adx : ady;  // chebyshev (8-direction)
        if (d < best_d) { best_d = d; best = i; }
    }
    if (out_dist) *out_dist = best_d;
    return best;
}

// Modal target picker traversal: from (cx,cy) cursor to (tx,ty),
// queue arrow presses then ENTER. Returns how many keys queued.
static int gp_queue_picker(int cx, int cy, int tx, int ty) {
    int queued = 0;
    while (cx != tx || cy != ty) {
        int dx = 0, dy = 0;
        if (tx > cx) dx = 1; else if (tx < cx) dx = -1;
        if (ty > cy) dy = 1; else if (ty < cy) dy = -1;
        int k = gp_dir_key(cx, cy, cx + dx, cy + dy);
        if (k == 0) break;
        input_host_queue_key(k);
        queued++;
        cx += dx;
        cy += dy;
        if (queued > 32) break;  // sanity
    }
    input_host_queue_key(KEY_ENTER);
    queued++;
    return queued;
}

// ---- Scenario state -----------------------------------------------------

typedef enum {
    MP_DISMISS_INTRO = 0,
    // Recruit first (king_maximus is 2 tiles north of spawn — short
    // walk with no foes in the way) so the army is fighting-ready
    // before we engage wandering armies on the way to a town.
    MP_WALK_TO_KING,
    MP_OPEN_RECRUIT,
    MP_DO_RECRUITS,
    MP_LEAVE_RECRUIT,
    MP_LEAVE_KING,
    MP_WALK_TO_TOWN_FOR_CONTRACT,
    MP_TAKE_CONTRACT,
    MP_CLOSE_TOWN_INFO_AFTER_CONTRACT,
    MP_EXIT_TOWN_AFTER_CONTRACT,
    MP_WALK_TO_TOWN_FOR_BOAT,
    MP_RENT_BOAT,
    MP_EXIT_TOWN_AFTER_BOAT,
    MP_BOARD_BOAT,
    MP_SAIL_TO_AZRAM_COAST,
    MP_DISEMBARK_NEAR_AZRAM,
    MP_WALK_TO_AZRAM_GATE,
    MP_SIEGE_PROMPT,
    MP_COMBAT,
    MP_POST_COMBAT_DIALOG,
    MP_VERIFY,
} MurrayPhase;

typedef struct {
    MurrayPhase phase;
    int         phase_started_at;
    bool        phase_action_queued;  // one-shot per phase entry
    // Cached current path for walk phases.
    GpPoint     path[GP_PATH_MAX];
    int         path_len;
    int         path_idx;
    int         path_target_x;
    int         path_target_y;
    // Combat helpers.
    int         combat_unit_id_last_acted;
    int         combat_frame_last_action;
    int         combat_picker_keys_left;
} MurrayState;

// Compute / cache a path from (cx,cy) to (tx,ty). The path is kept
// across frames. Recomputes if:
//  - no path cached
//  - target changed
//  - we drifted off the path entirely
// Otherwise we just sync path_idx to our current position so the
// caller's next step takes us toward path[idx+1].
static bool gp_ensure_path(MurrayState *st, const Map *m,
                           int cx, int cy, int tx, int ty,
                           GpTravel mode) {
    bool target_changed = (st->path_target_x != tx) ||
                          (st->path_target_y != ty);
    if (st->path_len > 0 && !target_changed) {
        // Try to find our position on the cached path. Search around
        // the current path_idx (forward first, then backward).
        for (int delta = 0; delta < st->path_len; delta++) {
            int i = st->path_idx + delta;
            if (i < st->path_len &&
                st->path[i].x == cx && st->path[i].y == cy) {
                st->path_idx = i;
                return true;
            }
            i = st->path_idx - delta;
            if (i >= 0 &&
                st->path[i].x == cx && st->path[i].y == cy) {
                st->path_idx = i;
                return true;
            }
        }
        // Drifted off the cached path — fall through to recompute.
    }
    st->path_len = gp_bfs(m, cx, cy, tx, ty, mode, st->path, GP_PATH_MAX);
    st->path_idx = 0;
    st->path_target_x = tx;
    st->path_target_y = ty;
    return st->path_len > 0;
}

// Issue the next step toward the cached path target. Returns true if
// a step was queued (or we've already arrived); false if pathing
// failed.
static bool gp_step_along_path(MurrayState *st, int cx, int cy) {
    if (st->path_len == 0 || st->path_idx >= st->path_len) return false;
    // Skip path entries that match our current position (in case the
    // engine bounced us back, e.g. stepping onto a castle gate).
    while (st->path_idx < st->path_len &&
           st->path[st->path_idx].x == cx &&
           st->path[st->path_idx].y == cy) {
        st->path_idx++;
    }
    if (st->path_idx >= st->path_len) return true;  // arrived
    GpPoint next = st->path[st->path_idx];
    int k = gp_dir_key(cx, cy, next.x, next.y);
    if (k == 0) {
        fprintf(stderr,
                "[gp] gp_dir_key 0: cur=(%d,%d) want=(%d,%d) idx=%d\n",
                cx, cy, next.x, next.y, st->path_idx);
        return false;
    }
    // Don't pile up keys when the engine hasn't drained the previous
    // one — keep at most one move-key pending. Without this, a
    // refused/blocked step (engine rejects entry) causes us to queue
    // the same key every frame until the queue cap is hit and the
    // engine sees many copies of the wrong key.
    if (input_host_queue_depth() > 0) return true;
    input_host_queue_key(k);
    return true;
}

// Forward-declared so the before-frame callback can find the live
// scenario state. Set by scenario_capture_murray; cleared on exit.
static MurrayState *g_active_murray_state = NULL;
int                 g_frame_counter = 0;

// Inline-handle combat input from inside RunCombat's loop (which
// doesn't go through the main loop's per_frame hook). Called from
// frame_host_should_close via before_frame; this is the only place
// scenario code runs while RunCombat owns the loop.
static void capture_murray_before_frame(void *user) {
    (void)user;
    MurrayState *st = g_active_murray_state;
    if (!st) return;
    int frame_no = g_frame_counter++;
    Combat *c = combat_current_rendered;
    if (!c) return;

    // Dismiss any post-combat dialog that pops up inside RunCombat
    // (the victory dialog uses dialog_advance / dialog_dismiss).
    if (dialog_is_active()) {
        if (input_host_queue_depth() == 0) {
            input_host_queue_key(KEY_SPACE);
        }
        return;
    }

    // Combat over? Just wait for RunCombat to return.
    if (c->result != 0) return;
    if (c->picker_active) return;
    if (c->side != COMBAT_SIDE_PLAYER) return;
    if (c->unit_id < 0) return;
    if (input_host_queue_depth() > 0) return;

    CombatUnit *u = &c->units[c->side][c->unit_id];
    if (u->acted || u->troop_idx < 0 || u->count <= 0) return;
    if (c->unit_id == st->combat_unit_id_last_acted &&
        frame_no < st->combat_frame_last_action + 6) {
        return;
    }
    int enemy_d = 0;
    int enemy_slot = gp_closest_enemy(c, u->x, u->y, &enemy_d);
    if (enemy_slot < 0) return;
    const CombatUnit *enemy = &c->units[COMBAT_SIDE_AI][enemy_slot];
    bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                      !combat_unit_surrounded(c, c->side, c->unit_id));
    int chosen_key = 0;
    if (can_shoot) {
        input_host_queue_key(KEY_S);
        gp_queue_picker(u->x, u->y, enemy->x, enemy->y);
        chosen_key = KEY_S;
    } else {
        // Pick a step that decreases distance to the enemy AND lands
        // on an unobstructed cell (or onto the enemy itself for
        // melee). Prefer diagonals when they help both axes; fall back
        // to cardinal then to any legal step; finally wait.
        static const int dxs[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const int dys[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        int best_k = 0;
        int best_d = enemy_d;
        for (int i = 0; i < 8; i++) {
            int nx = u->x + dxs[i];
            int ny = u->y + dys[i];
            if (!combat_in_bounds(nx, ny)) continue;
            if (c->omap[ny][nx]) continue;
            unsigned char other = c->umap[ny][nx];
            if (other) {
                int o_side = (other - 1) / COMBAT_SLOTS;
                int o_slot = (other - 1) % COMBAT_SLOTS;
                // Walking into a friendly cell is blocked; walking
                // into an enemy is a melee.
                if (units_are_friendly(c, c->side, c->unit_id,
                                       o_side, o_slot)) continue;
            }
            int adx = enemy->x - nx; if (adx < 0) adx = -adx;
            int ady = enemy->y - ny; if (ady < 0) ady = -ady;
            int nd = (adx > ady) ? adx : ady;
            if (nd < best_d) {
                best_d = nd;
                best_k = gp_dir_key(u->x, u->y, nx, ny);
            }
        }
        if (best_k != 0) {
            input_host_queue_key(best_k);
            chosen_key = best_k;
        } else {
            // No improving step — wait so the loop progresses.
            input_host_queue_key(KEY_SPACE);
            chosen_key = KEY_SPACE;
        }
    }
    (void)chosen_key;
    st->combat_unit_id_last_acted = c->unit_id;
    st->combat_frame_last_action = frame_no;
}

static void capture_murray_before_startup(ShellRunHooks *self) {
    (void)self;
    queue_standard_startup();
}

#define GP_LOG(fmt, ...) fprintf(stderr, "[gp] " fmt "\n", ##__VA_ARGS__)

static GpVerdict capture_murray_per_frame(ShellRunHooks *self,
                                          Game *g, Map *m, Fog *f,
                                          Resources *res, int frame_no) {
    (void)f; (void)res;
    MurrayState *st = (MurrayState *)self->user;


    // Common: dialogs that pop up unexpectedly (e.g. weekend) — dismiss
    // them with SPACE. Skip this in phases that EXPECT a specific
    // dialog. Only queue when the input queue has room — otherwise
    // we'd just pile up dismiss keys faster than the engine consumes.
    if (dialog_is_active() &&
        st->phase != MP_DISMISS_INTRO &&
        st->phase != MP_TAKE_CONTRACT &&
        st->phase != MP_POST_COMBAT_DIALOG) {
        if (input_host_queue_depth() == 0) {
            input_host_queue_key(KEY_SPACE);
        }
        return GP_VERDICT_CONTINUE;
    }

    // Common: stray prompts during overworld phases.
    //   - "Foes!" (hostile wandering army): press Y to attack.
    //     Recruit-recipe army handles wandering encounters; rendered
    //     combat is driven by the shared combat scripting below.
    //   - "Siege" (villain castle): press Y to attack — the next
    //     phase needs us to enter combat.
    //   - A/B picker (chest gold-or-leadership): press A (gold).
    //   - All other y/n (search, etc.): decline with N.
    if (prompt_is_active() &&
        st->phase != MP_SIEGE_PROMPT &&
        st->phase != MP_COMBAT) {
        if (input_host_queue_depth() == 0) {
            const char *hdr = prompt_header_text();
            const char *kind = prompt_kind_str();
            int key = KEY_N;
            const char *what = "decline";
            if (hdr && (strstr(hdr, "Foe") || strstr(hdr, "Siege") ||
                        strstr(hdr, "Castle"))) {
                key = KEY_Y; what = "ATTACK";
            } else if (kind && strcmp(kind, "ab") == 0) {
                key = KEY_A; what = "A (gold)";
            }
            GP_LOG("prompt kind=%s header='%s' → %s",
                   kind, hdr, what);
            input_host_queue_key(key);
        }
        return GP_VERDICT_CONTINUE;
    }

    // Combat in progress (foe encounter or siege): drive it with the
    // shared per-unit decision routine. Skip when combat hasn't
    // started yet or when combat has ended (result != 0 means the
    // outcome is set; victory dialog will be up).
    Combat *c_active = combat_current_rendered;
    if (c_active && c_active->result == 0 && !c_active->picker_active &&
        c_active->side == COMBAT_SIDE_PLAYER && c_active->unit_id >= 0 &&
        input_host_queue_depth() == 0) {
        CombatUnit *u = &c_active->units[c_active->side][c_active->unit_id];
        if (!u->acted && !(u->troop_idx < 0 || u->count <= 0)) {
            // Don't double-act on the same unit (let the loop roll it).
            if (c_active->unit_id != st->combat_unit_id_last_acted ||
                frame_no >= st->combat_frame_last_action + 6) {
                int enemy_d = 0;
                int enemy_slot = gp_closest_enemy(c_active, u->x, u->y,
                                                  &enemy_d);
                if (enemy_slot >= 0) {
                    const CombatUnit *enemy =
                        &c_active->units[COMBAT_SIDE_AI][enemy_slot];
                    bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                                      !combat_unit_surrounded(c_active,
                                          c_active->side,
                                          c_active->unit_id));
                    if (can_shoot) {
                        input_host_queue_key(KEY_S);
                        gp_queue_picker(u->x, u->y, enemy->x, enemy->y);
                    } else {
                        int dx = 0, dy = 0;
                        if (enemy->x > u->x) dx = 1;
                        else if (enemy->x < u->x) dx = -1;
                        if (enemy->y > u->y) dy = 1;
                        else if (enemy->y < u->y) dy = -1;
                        int k = gp_dir_key(u->x, u->y,
                                           u->x + dx, u->y + dy);
                        if (k == 0) input_host_queue_key(KEY_SPACE);
                        else        input_host_queue_key(k);
                    }
                } else {
                    input_host_queue_key(KEY_SPACE);
                }
                st->combat_unit_id_last_acted = c_active->unit_id;
                st->combat_frame_last_action = frame_no;
            }
        }
    }

    switch (st->phase) {
    case MP_DISMISS_INTRO:
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            st->phase = MP_WALK_TO_KING;
            st->phase_started_at = frame_no;
            GP_LOG("intro dismissed, walking to king_maximus to recruit");
        }
        GP_TIMEOUT_FAIL(0, 1200, "intro dialog never appeared");
        return GP_VERDICT_CONTINUE;

    case MP_WALK_TO_TOWN_FOR_CONTRACT:
        if (views_active() == VIEW_TOWN) {
            st->phase = MP_TAKE_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            st->path_len = 0;
            GP_LOG("entered town (japper for contract)");
            return GP_VERDICT_CONTINUE;
        }
        // Dismiss any non-town view we accidentally bumped into
        // (king_maximus, own castle, dwelling, etc.) — those are
        // not the goal of this phase.
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return GP_VERDICT_CONTINUE;
        }
        // japper in continentia is stamped at (13,56); walking onto
        // that tile opens VIEW_TOWN. (The catalog-level "gate (13,55)"
        // is for japper-in-archipelia, a different stamping.)
        if (!gp_ensure_path(st, m, g->position.x, g->position.y,
                            12, 60, GP_TRAVEL_WALK)) {
            GP_LOG("BFS failed: spawn->hunterville (12,60) "
                   "from (%d,%d)", g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        if (frame_no % 60 == 0) {
            GpPoint goal = (st->path_len > 0) ?
                st->path[st->path_len - 1] : (GpPoint){-1,-1};
            GpPoint nxt = (st->path_idx < st->path_len) ?
                st->path[st->path_idx] : (GpPoint){-1,-1};
            GP_LOG("walk f=%d pos=(%d,%d) path[%d/%d] next=(%d,%d) "
                   "goal=(%d,%d) view=%d dlg=%d prompt=%d",
                   frame_no, g->position.x, g->position.y,
                   st->path_idx, st->path_len, nxt.x, nxt.y,
                   goal.x, goal.y,
                   (int)views_active(), dialog_is_active(),
                   prompt_is_active());
        }
        if (!gp_step_along_path(st, g->position.x, g->position.y)) {
            GP_LOG("step failed walking to japper gate at (%d,%d)",
                   g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 3600,
                        "stuck walking to japper");
        return GP_VERDICT_CONTINUE;

    case MP_TAKE_CONTRACT: {
        // We're inside the japper town view. Press A to take the
        // contract; the town opens an info popup with the wanted
        // villain's blurb.
        const char *active = g->contract.active_id;
        if (active[0] && strcmp(active, "murray") == 0) {
            // contract was taken; close any info popup and exit town
            st->phase = MP_CLOSE_TOWN_INFO_AFTER_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            GP_LOG("contract taken: %s", active);
            return GP_VERDICT_CONTINUE;
        }
        if (active[0]) {
            GP_LOG("unexpected active contract '%s' (wanted murray)",
                   active);
            return GP_VERDICT_EXIT_FAIL;
        }
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_A);
            st->phase_action_queued = true;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "town didn't accept A for contract");
        return GP_VERDICT_CONTINUE;
    }

    case MP_CLOSE_TOWN_INFO_AFTER_CONTRACT:
        // The "wanted: Murray" dialog uses the town's info-popup
        // mechanism — a SPACE/ESC dismisses it.
        input_host_queue_key(KEY_SPACE);
        st->phase = MP_EXIT_TOWN_AFTER_CONTRACT;
        st->phase_started_at = frame_no;
        return GP_VERDICT_CONTINUE;

    case MP_EXIT_TOWN_AFTER_CONTRACT:
        // After the info popup dismisses, we're back in the town
        // menu. Press B to rent the boat instead of leaving and
        // walking back; we're already here.
        if (views_active() == VIEW_NONE) {
            // Shouldn't normally happen (we close info, not town);
            // fall through to walking back if it does.
            st->phase = MP_WALK_TO_TOWN_FOR_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            st->path_len = 0;
            GP_LOG("exited town after contract, walking back for boat");
            return GP_VERDICT_CONTINUE;
        }
        if (views_active() == VIEW_TOWN) {
            // Still in the town — proceed directly to boat rental.
            st->phase = MP_RENT_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            GP_LOG("in town after contract, renting boat");
            return GP_VERDICT_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        GP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't transition after town contract");
        return GP_VERDICT_CONTINUE;

    case MP_WALK_TO_KING:
        if (views_active() == VIEW_HOME_CASTLE) {
            st->phase = MP_OPEN_RECRUIT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            GP_LOG("entered king_maximus");
            return GP_VERDICT_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return GP_VERDICT_CONTINUE;
        }
        if (!gp_ensure_path(st, m, g->position.x, g->position.y,
                            11, 56, GP_TRAVEL_WALK)) {
            GP_LOG("BFS failed: pos->king_maximus (11,56)");
            return GP_VERDICT_EXIT_FAIL;
        }
        if (!gp_step_along_path(st, g->position.x, g->position.y)) {
            GP_LOG("step failed walking to king_maximus");
            return GP_VERDICT_EXIT_FAIL;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 1800,
                        "stuck walking to king_maximus");
        return GP_VERDICT_CONTINUE;

    case MP_OPEN_RECRUIT:
        input_host_queue_key(KEY_A);
        st->phase = MP_DO_RECRUITS;
        st->phase_started_at = frame_no;
        return GP_VERDICT_CONTINUE;

    case MP_DO_RECRUITS:
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            input_host_queue_key(KEY_C);  // pikemen
            queue_recruit_count(10);
            input_host_queue_key(KEY_A);  // militia
            queue_recruit_count(30);
            input_host_queue_key(KEY_B);  // archers
            queue_recruit_count(8);
            st->phase = MP_LEAVE_RECRUIT;
            st->phase_started_at = frame_no;
            return GP_VERDICT_CONTINUE;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 300,
                        "VIEW_RECRUIT_SOLDIERS never opened");
        return GP_VERDICT_CONTINUE;

    case MP_LEAVE_RECRUIT:
        if (army_total_hp(g) >= 300) {
            input_host_queue_key(KEY_ESCAPE);
            st->phase = MP_LEAVE_KING;
            st->phase_started_at = frame_no;
            GP_LOG("recruits done, army HP=%d gold=%d",
                   army_total_hp(g), g->stats.gold);
            return GP_VERDICT_CONTINUE;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 600,
                        "army HP never reached 300");
        return GP_VERDICT_CONTINUE;

    case MP_LEAVE_KING:
        if (views_active() == VIEW_NONE) {
            st->phase = MP_WALK_TO_TOWN_FOR_CONTRACT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            GP_LOG("exited king_maximus, walking to a town for contract");
            return GP_VERDICT_CONTINUE;
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            input_host_queue_key(KEY_ESCAPE);
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't exit king_maximus");
        return GP_VERDICT_CONTINUE;

    case MP_WALK_TO_TOWN_FOR_BOAT:
        if (views_active() == VIEW_TOWN) {
            st->phase = MP_RENT_BOAT;
            st->phase_started_at = frame_no;
            return GP_VERDICT_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return GP_VERDICT_CONTINUE;
        }
        if (!gp_ensure_path(st, m, g->position.x, g->position.y,
                            12, 60, GP_TRAVEL_WALK)) {
            GP_LOG("BFS failed back to japper from (%d,%d)",
                   g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        if (!gp_step_along_path(st, g->position.x, g->position.y)) {
            return GP_VERDICT_EXIT_FAIL;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 3600,
                        "stuck walking to japper (boat trip)");
        return GP_VERDICT_CONTINUE;

    case MP_RENT_BOAT:
        if (g->boat.has_boat) {
            // Town's boat rental just toggles the flag — no popup.
            input_host_queue_key(KEY_ESCAPE);
            st->phase = MP_EXIT_TOWN_AFTER_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            GP_LOG("boat rented, gold=%d boat=(%d,%d)",
                   g->stats.gold, g->boat.x, g->boat.y);
            return GP_VERDICT_CONTINUE;
        }
        // Queue exactly ONE B per phase entry. has_boat toggles on
        // every B press, so a second pending B (queued before the
        // first registers) would CANCEL the rental on the next frame.
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_B);
            st->phase_action_queued = true;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "town didn't accept B for boat rental");
        return GP_VERDICT_CONTINUE;

    case MP_EXIT_TOWN_AFTER_BOAT:
        if (views_active() == VIEW_NONE) {
            st->phase = MP_BOARD_BOAT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            GP_LOG("exited town, walking to boat at (%d,%d)",
                   g->boat.x, g->boat.y);
            return GP_VERDICT_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        GP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't exit town after boat rental");
        return GP_VERDICT_CONTINUE;

    case MP_BOARD_BOAT: {
        // Walk onto the boat tile. Even though it's a water tile, the
        // step engine sees boat.has_boat + boat.x/y matching the
        // destination and sets travel_mode=TRAVEL_BOAT.
        int bx = g->boat.x, by = g->boat.y;
        if (g->travel_mode == TRAVEL_BOAT) {
            st->phase = MP_SAIL_TO_AZRAM_COAST;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            GP_LOG("boarded boat at (%d,%d), now sailing", bx, by);
            return GP_VERDICT_CONTINUE;
        }
        // The boat tile is water; we need to walk INTO it from an
        // adjacent land tile. BFS using WALK terrain to a tile
        // adjacent to (bx,by) won't work directly since (bx,by) is
        // water. Instead, we step toward (bx,by) one tile at a time.
        int dx = 0, dy = 0;
        if (bx > g->position.x) dx = 1; else if (bx < g->position.x) dx = -1;
        if (by > g->position.y) dy = 1; else if (by < g->position.y) dy = -1;
        int k = gp_dir_key(g->position.x, g->position.y,
                           g->position.x + dx, g->position.y + dy);
        if (k == 0) {
            GP_LOG("already adjacent to boat? pos=(%d,%d) boat=(%d,%d)",
                   g->position.x, g->position.y, bx, by);
            return GP_VERDICT_EXIT_FAIL;
        }
        input_host_queue_key(k);
        GP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "couldn't board boat");
        return GP_VERDICT_CONTINUE;
    }

    case MP_SAIL_TO_AZRAM_COAST: {
        // We may end up on land mid-sail (combat bounce, BFS picked
        // a land tile on the way). If we're close to azram, just
        // walk the rest of the way; otherwise try to re-board.
        if (g->travel_mode == TRAVEL_WALK) {
            int adx = g->position.x - 30; if (adx < 0) adx = -adx;
            int ady = g->position.y - 37; if (ady < 0) ady = -ady;
            // "Close enough" if within manhattan 15 AND BFS_WALK
            // confirms an overland path.
            GpPoint walk_diag[GP_PATH_MAX];
            int n_walk_to_gate = (adx + ady <= 25)
                ? gp_bfs(m, g->position.x, g->position.y, 30, 37,
                         GP_TRAVEL_WALK, walk_diag, GP_PATH_MAX)
                : 0;
            if (n_walk_to_gate > 0) {
                st->phase = MP_WALK_TO_AZRAM_GATE;
                st->phase_started_at = frame_no;
                st->path_len = 0;
                GP_LOG("disembarked at (%d,%d), "
                       "azram reachable on foot (%d steps), walking",
                       g->position.x, g->position.y, n_walk_to_gate);
                return GP_VERDICT_CONTINUE;
            }
            // Too far on foot — re-board the boat (it's at prev water
            // tile) and keep sailing.
            if (g->boat.has_boat && g->boat.x >= 0) {
                int dx = 0, dy = 0;
                if (g->boat.x > g->position.x) dx = 1;
                else if (g->boat.x < g->position.x) dx = -1;
                if (g->boat.y > g->position.y) dy = 1;
                else if (g->boat.y < g->position.y) dy = -1;
                int k = gp_dir_key(g->position.x, g->position.y,
                                   g->position.x + dx,
                                   g->position.y + dy);
                if (k != 0 && input_host_queue_depth() == 0) {
                    input_host_queue_key(k);
                }
                GP_TIMEOUT_FAIL(st->phase_started_at, 14400,
                                "stuck reboarding boat after disembark");
                return GP_VERDICT_CONTINUE;
            }
            // No boat — give up sailing, try overland walk anyway.
            st->phase = MP_WALK_TO_AZRAM_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            GP_LOG("disembarked at (%d,%d) and no boat — fall through "
                   "to overland walk attempt",
                   g->position.x, g->position.y);
            return GP_VERDICT_CONTINUE;
        }
        // Plan: sail directly to a LAND tile near azram. Stepping
        // from water onto land in boat mode auto-disembarks at the
        // destination tile. Then a short walk reaches the gate.
        //
        // Search a wide ring around azram gate for a land tile that
        // (a) has a walking path to the gate and (b) is reachable by
        // sail. gp_bfs in BOAT mode allows the goal to be non-water.
        int best_x = -1, best_y = -1;
        if (st->path_len == 0) {
            // Azram's gate sits at the castle's (x,y) coord (engine
            // stamps gate at z->castles[i].x, .y). Catalog: (30,36).
            int azram_gate_x = 30, azram_gate_y = 36;
            int best_total = 1 << 30;
            GpPoint sail_path[GP_PATH_MAX];
            GpPoint walk_path[GP_PATH_MAX];
            for (int dy = -12; dy <= 12; dy++) {
                for (int dx = -12; dx <= 12; dx++) {
                    int tx = azram_gate_x + dx;
                    int ty = azram_gate_y + dy;
                    if (!MapInBounds(m, tx, ty)) continue;
                    const Tile *t = MapGetTile(m, tx, ty);
                    if (!t) continue;
                    if (!TerrainWalkable(t->terrain) || t->blocks_foot) continue;
                    if (t->interactive != INTERACT_NONE) continue;
                    int n_walk = gp_bfs(m, tx, ty,
                                        azram_gate_x, azram_gate_y,
                                        GP_TRAVEL_WALK,
                                        walk_path, GP_PATH_MAX);
                    if (n_walk <= 0) continue;
                    int n_sail = gp_bfs(m, g->position.x, g->position.y,
                                        tx, ty, GP_TRAVEL_BOAT,
                                        sail_path, GP_PATH_MAX);
                    if (n_sail <= 0) continue;
                    int total = n_sail + n_walk;
                    if (total < best_total) {
                        best_total = total;
                        best_x = tx;
                        best_y = ty;
                        memcpy(st->path, sail_path,
                               sizeof(GpPoint) * (size_t)n_sail);
                        st->path_len = n_sail;
                        st->path_idx = 0;
                        st->path_target_x = tx;
                        st->path_target_y = ty;
                    }
                }
            }
            if (best_x < 0) {
                GP_LOG("no sailable approach to azram gate from (%d,%d)",
                       g->position.x, g->position.y);
                return GP_VERDICT_EXIT_FAIL;
            }
            GP_LOG("sailing to land (%d,%d) (sail+walk=%d steps)",
                   best_x, best_y, best_total);
        }
        // If we've arrived at the planned coastline tile, advance.
        if (g->position.x == st->path_target_x &&
            g->position.y == st->path_target_y) {
            st->phase = MP_DISEMBARK_NEAR_AZRAM;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            return GP_VERDICT_CONTINUE;
        }
        // Sync path_idx to our current position; recompute path if we
        // drifted off entirely. Without this, a single off-path step
        // (e.g. foe combat that re-positioned us) leaves us querying
        // the wrong next-step direction forever.
        if (!gp_ensure_path(st, m, g->position.x, g->position.y,
                            st->path_target_x, st->path_target_y,
                            GP_TRAVEL_BOAT)) {
            GP_LOG("sail: BFS recompute failed from (%d,%d)",
                   g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        if (!gp_step_along_path(st, g->position.x, g->position.y)) {
            GP_LOG("sailing step failed at (%d,%d)",
                   g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "stuck sailing to azram coast");
        return GP_VERDICT_CONTINUE;
    }

    case MP_DISEMBARK_NEAR_AZRAM: {
        // From the boat-tile we're on, step onto an adjacent land
        // tile (preferring one that has a walkable path to the azram
        // gate). The engine handles disembark when stepping from
        // water onto land.
        if (g->travel_mode != TRAVEL_BOAT) {
            st->phase = MP_WALK_TO_AZRAM_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            GP_LOG("disembarked at (%d,%d)",
                   g->position.x, g->position.y);
            return GP_VERDICT_CONTINUE;
        }
        static const int dxs[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
        static const int dys[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
        int chosen_k = 0;
        int best_d = 1 << 30;
        for (int i = 0; i < 8; i++) {
            int nx = g->position.x + dxs[i];
            int ny = g->position.y + dys[i];
            if (!MapInBounds(m, nx, ny)) continue;
            const Tile *t = MapGetTile(m, nx, ny);
            if (!t || !TerrainWalkable(t->terrain) || t->blocks_foot) {
                continue;
            }
            int adx = nx - 30; if (adx < 0) adx = -adx;
            int ady = ny - 37; if (ady < 0) ady = -ady;
            int d = adx + ady;
            if (d < best_d) {
                best_d = d;
                chosen_k = gp_dir_key(g->position.x, g->position.y,
                                      nx, ny);
            }
        }
        if (chosen_k == 0) {
            GP_LOG("no adjacent land tile to disembark from (%d,%d)",
                   g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        input_host_queue_key(chosen_k);
        GP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "couldn't disembark");
        return GP_VERDICT_CONTINUE;
    }

    case MP_WALK_TO_AZRAM_GATE: {
        // Already won? If villains_caught[murray] flipped, we're
        // done — jump to VERIFY.
        const VillainDef *vm = villain_by_id("murray");
        if (vm && g->contract.villains_caught[vm->index]) {
            st->phase = MP_VERIFY;
            st->phase_started_at = frame_no;
            GP_LOG("villains_caught[murray] set — verifying");
            return GP_VERDICT_CONTINUE;
        }
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && strstr(hdr, "Siege")) {
                st->phase = MP_SIEGE_PROMPT;
                st->phase_started_at = frame_no;
                GP_LOG("siege prompt up");
                return GP_VERDICT_CONTINUE;
            }
            return GP_VERDICT_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return GP_VERDICT_CONTINUE;
        }
        if (!gp_ensure_path(st, m, g->position.x, g->position.y,
                            30, 36, GP_TRAVEL_WALK)) {
            GP_LOG("BFS failed walking to azram gate from (%d,%d)",
                   g->position.x, g->position.y);
            return GP_VERDICT_EXIT_FAIL;
        }
        if (!gp_step_along_path(st, g->position.x, g->position.y)) {
            return GP_VERDICT_EXIT_FAIL;
        }
        GP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "stuck walking to azram gate");
        return GP_VERDICT_CONTINUE;
    }

    case MP_SIEGE_PROMPT:
        // Press Y to accept the siege prompt.
        input_host_queue_key(KEY_Y);
        st->phase = MP_COMBAT;
        st->phase_started_at = frame_no;
        st->combat_unit_id_last_acted = -1;
        st->combat_frame_last_action = frame_no;
        st->combat_picker_keys_left = 0;
        GP_LOG("siege confirmed, entering combat");
        return GP_VERDICT_CONTINUE;

    case MP_COMBAT: {
        Combat *c = combat_current_rendered;
        if (!c) {
            // Combat hasn't started yet (siege prompt is still
            // resolving) or it has ended. If ended, check result.
            if (st->phase_started_at + 60 < frame_no) {
                // Combat ended — check if dialog is up (victory).
                if (dialog_is_active()) {
                    st->phase = MP_POST_COMBAT_DIALOG;
                    st->phase_started_at = frame_no;
                    return GP_VERDICT_CONTINUE;
                }
            }
            return GP_VERDICT_CONTINUE;
        }
        // Active unit on player side: decide an action.
        if (c->side != COMBAT_SIDE_PLAYER) return GP_VERDICT_CONTINUE;
        if (c->unit_id < 0) return GP_VERDICT_CONTINUE;
        // Don't queue more input while a target-picker is open and the
        // queued keys are still draining.
        if (c->picker_active) {
            // Picker is active — we already queued arrows + ENTER when
            // we initiated this turn; let them play out.
            return GP_VERDICT_CONTINUE;
        }
        if (c->units[c->side][c->unit_id].acted) {
            // Wait for the loop to roll the next unit.
            return GP_VERDICT_CONTINUE;
        }
        // Don't act twice in a row for the same unit (let frames
        // progress so c->unit_id rolls when the unit acts).
        if (c->unit_id == st->combat_unit_id_last_acted &&
            frame_no < st->combat_frame_last_action + 10) {
            return GP_VERDICT_CONTINUE;
        }
        CombatUnit *u = &c->units[c->side][c->unit_id];
        const TroopDef *td = troop_by_index(u->troop_idx);
        if (!td) return GP_VERDICT_CONTINUE;
        int enemy_d = 0;
        int enemy_slot = gp_closest_enemy(c, u->x, u->y, &enemy_d);
        if (enemy_slot < 0) return GP_VERDICT_CONTINUE;
        const CombatUnit *enemy = &c->units[COMBAT_SIDE_AI][enemy_slot];

        // Ranged shoot if archer-like and not surrounded.
        bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                          !combat_unit_surrounded(c, c->side,
                                                  c->unit_id));
        if (can_shoot) {
            input_host_queue_key(KEY_S);
            // Picker opens with cursor at u->x,u->y → navigate to
            // enemy.
            gp_queue_picker(u->x, u->y, enemy->x, enemy->y);
            st->combat_unit_id_last_acted = c->unit_id;
            st->combat_frame_last_action = frame_no;
            return GP_VERDICT_CONTINUE;
        }
        // Otherwise, step toward the enemy (stepping onto an enemy
        // tile counts as melee).
        int dx = 0, dy = 0;
        if (enemy->x > u->x) dx = 1; else if (enemy->x < u->x) dx = -1;
        if (enemy->y > u->y) dy = 1; else if (enemy->y < u->y) dy = -1;
        int k = gp_dir_key(u->x, u->y, u->x + dx, u->y + dy);
        if (k == 0) {
            // No legal step — just wait.
            input_host_queue_key(KEY_SPACE);
        } else {
            input_host_queue_key(k);
        }
        st->combat_unit_id_last_acted = c->unit_id;
        st->combat_frame_last_action = frame_no;
        GP_TIMEOUT_FAIL(st->phase_started_at, 36000,
                        "combat ran too long (no progress)");
        return GP_VERDICT_CONTINUE;
    }

    case MP_POST_COMBAT_DIALOG:
        // Victory dialog (and contract follow-up). Press SPACE to
        // dismiss each page.
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            return GP_VERDICT_CONTINUE;
        }
        st->phase = MP_VERIFY;
        st->phase_started_at = frame_no;
        return GP_VERDICT_CONTINUE;

    case MP_VERIFY: {
        const VillainDef *vm = villain_by_id("murray");
        if (!vm) {
            GP_LOG("villain catalog missing murray");
            return GP_VERDICT_EXIT_FAIL;
        }
        if (!g->contract.villains_caught[vm->index]) {
            GP_LOG("villains_caught[murray] is false; capture failed");
            return GP_VERDICT_EXIT_FAIL;
        }
        printf("gameplay-test: capture_murray PASS — "
               "villains_caught[murray]=true, gold=%d\n",
               g->stats.gold);
        return GP_VERDICT_EXIT_PASS;
    }
    }
    return GP_VERDICT_EXIT_FAIL;
}

static int scenario_capture_murray(int argc, char **argv) {
    MurrayState state = { 0 };
    state.phase = MP_DISMISS_INTRO;
    state.combat_unit_id_last_acted = -1;
    g_active_murray_state = &state;
    g_frame_counter = 0;
    frame_host_set_before_frame(capture_murray_before_frame, NULL);
    ShellRunHooks hooks = {
        .before_startup = capture_murray_before_startup,
        .per_frame      = capture_murray_per_frame,
        .user           = &state,
    };
    int rc = shell_run_game(argc, argv, &hooks);
    frame_host_set_before_frame(NULL, NULL);
    g_active_murray_state = NULL;
    return rc;
}

