// Autoplay: scripted full-stack run of the real game binary. See
// src/autoplay.h for the public API.
//
// The autoplay routine drives the game through the same input and
// frame shims (input_host.{c,h}, frame_host.{c,h}) that any future
// scripted scenarios would reuse. The routine itself is hard-wired
// to a specific seed-1 run today (recruit at the home castle, take
// the next contract at the nearest town, sail to the villain's
// castle, fight through the rendered combat, verify the catch).
// Future expansions land here too — there is no scenario registry;
// this is the single entry point.

#include "autoplay.h"
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
#include "savegame.h"
#include "savepath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =========================================================================
// Shared helpers
// =========================================================================

#define AP_LOG(fmt, ...) fprintf(stderr, "[autoplay] " fmt "\n", ##__VA_ARGS__)

#define AP_TIMEOUT_FAIL(start_frame, max_frames, msg) \
    do { \
        if (frame_no > (start_frame) + (max_frames)) { \
            AP_LOG("timeout — %s", (msg)); \
            return SHELL_RUN_EXIT_FAIL; \
        } \
    } while (0)

// Queue the standard startup sequence: knight class, default name,
// normal difficulty. With startup_skip_intros set by the runner,
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

// Write a save into the last user-visible slot, fake-overwriting
// whatever was there. Reserves slots 0..SAVE_SLOT_COUNT-2 for the
// human player and uses the highest-numbered slot as a dedicated
// rolling autoplay checkpoint. Logs success to stderr and surfaces
// a toast for --visible runs. Returns true on a clean write.
static bool autoplay_save_checkpoint(const Game *g, const Map *m,
                                     const Fog *f) {
    const char *pack_id = (g->res && g->res->pack_id[0])
        ? g->res->pack_id : NULL;
    int slot = SAVE_SLOT_COUNT - 1;
    char path[512];
    if (!SavePathGetSlot(pack_id, slot, path, sizeof path)) {
        AP_LOG("checkpoint: SavePathGetSlot failed");
        return false;
    }
    SaveResult r = SaveGameWrite(path, g, m, f);
    if (r != SAVE_OK) {
        AP_LOG("checkpoint: SaveGameWrite failed (%s)",
               SaveResultText(r));
        return false;
    }
    // Slots are 0-indexed on disk but presented to the user as 1..N
    // in the save-picker UI; show the user-facing number.
    AP_LOG("checkpoint: saved to slot %d (%s)", slot + 1, path);
    char msg[64];
    snprintf(msg, sizeof msg, "AutoPlay checkpoint saved (slot %d)",
             slot + 1);
    toast_show(msg);
    return true;
}

// =========================================================================
// Path-finding (BFS over MapWalkable + boat-aware extension)
// =========================================================================

#define AP_PATH_MAX 256

typedef struct {
    int x, y;
} ApPoint;

typedef enum {
    AP_TRAVEL_WALK = 0,
    AP_TRAVEL_BOAT,
} ApTravel;

// BFS path from (sx,sy) to (gx,gy). `mode` selects which terrain
// counts as passable: WALK = grass+desert (TerrainWalkable);
// BOAT = water-only (or land tiles adjacent for disembark).
//
// Returns the number of points stored in `out` (path includes the
// start tile at index 0 and the goal at the end). 0 if no path.
static int ap_bfs(const Map *m, int sx, int sy, int gx, int gy,
                  ApTravel mode, ApPoint *out, int out_cap) {
    if (!m || !MapInBounds(m, sx, sy) || !MapInBounds(m, gx, gy)) return 0;
    int W = m->width;
    int H = m->height;
    // parent[y*W+x] = direction taken to enter the tile, encoded as
    // (dy+1)*3 + (dx+1) + 1; 0 = unvisited.
    short *parent = (short *)calloc((size_t)W * H, sizeof(short));
    if (!parent) return 0;

    ApPoint *queue = (ApPoint *)malloc(sizeof(ApPoint) * (size_t)W * H);
    if (!queue) { free(parent); return 0; }
    int qh = 0, qt = 0;
    queue[qt++] = (ApPoint){ sx, sy };
    parent[sy * W + sx] = 1;  // mark visited with no-direction sentinel

    bool found = (sx == gx && sy == gy);

    static const int dxs[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    static const int dys[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

    while (!found && qh < qt) {
        ApPoint p = queue[qh++];
        for (int k = 0; k < 8; k++) {
            int nx = p.x + dxs[k];
            int ny = p.y + dys[k];
            if (!MapInBounds(m, nx, ny)) continue;
            if (parent[ny * W + nx]) continue;
            const Tile *t = MapGetTile(m, nx, ny);
            if (!t) continue;
            bool ok = false;
            if (mode == AP_TRAVEL_WALK) {
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
            queue[qt++] = (ApPoint){ nx, ny };
            if (nx == gx && ny == gy) { found = true; break; }
        }
    }

    int n = 0;
    if (found) {
        // Walk parent chain back to start.
        ApPoint stack[AP_PATH_MAX];
        int sp = 0;
        int cx = gx, cy = gy;
        while (sp < AP_PATH_MAX) {
            stack[sp++] = (ApPoint){ cx, cy };
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
static int ap_dir_key(int cx, int cy, int nx, int ny) {
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

// =========================================================================
// Combat decision helpers
// =========================================================================

// Pick the (side=1) enemy unit with the smallest Chebyshev distance
// from (ux,uy). Returns slot, or -1 if no enemies.
static int ap_closest_enemy(const Combat *c, int ux, int uy,
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
static int ap_queue_picker(int cx, int cy, int tx, int ty) {
    int queued = 0;
    while (cx != tx || cy != ty) {
        int dx = 0, dy = 0;
        if (tx > cx) dx = 1; else if (tx < cx) dx = -1;
        if (ty > cy) dy = 1; else if (ty < cy) dy = -1;
        int k = ap_dir_key(cx, cy, cx + dx, cy + dy);
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

// =========================================================================
// Autoplay state machine
// =========================================================================

typedef enum {
    AP_DISMISS_INTRO = 0,
    // Recruit first (king_maximus is 2 tiles north of spawn — short
    // walk with no foes in the way) so the army is fighting-ready
    // before we engage wandering armies on the way to a town.
    AP_WALK_TO_KING,
    AP_OPEN_RECRUIT,
    AP_DO_RECRUITS,
    AP_LEAVE_RECRUIT,
    AP_LEAVE_KING,
    AP_WALK_TO_TOWN_FOR_CONTRACT,
    AP_TAKE_CONTRACT,
    AP_CLOSE_TOWN_INFO_AFTER_CONTRACT,
    AP_EXIT_TOWN_AFTER_CONTRACT,
    AP_WALK_TO_TOWN_FOR_BOAT,
    AP_RENT_BOAT,
    AP_EXIT_TOWN_AFTER_BOAT,
    AP_BOARD_BOAT,
    AP_SAIL_TO_AZRAM_COAST,
    AP_DISEMBARK_NEAR_AZRAM,
    AP_WALK_TO_AZRAM_GATE,
    AP_SIEGE_PROMPT,
    AP_COMBAT,
    AP_POST_COMBAT_DIALOG,
    AP_VERIFY,
} AutoplayPhase;

typedef struct {
    AutoplayPhase phase;
    int           phase_started_at;
    bool          phase_action_queued;  // one-shot per phase entry
    // Cached current path for walk phases.
    ApPoint       path[AP_PATH_MAX];
    int           path_len;
    int           path_idx;
    int           path_target_x;
    int           path_target_y;
    // Combat helpers.
    int           combat_unit_id_last_acted;
    int           combat_frame_last_action;
    int           combat_picker_keys_left;
} AutoplayState;

// Compute / cache a path from (cx,cy) to (tx,ty). The path is kept
// across frames. Recomputes if:
//   - no path cached
//   - target changed
//   - we drifted off the path entirely
// Otherwise we just sync path_idx to our current position so the
// caller's next step takes us toward path[idx+1].
static bool ap_ensure_path(AutoplayState *st, const Map *m,
                           int cx, int cy, int tx, int ty,
                           ApTravel mode) {
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
    st->path_len = ap_bfs(m, cx, cy, tx, ty, mode, st->path, AP_PATH_MAX);
    st->path_idx = 0;
    st->path_target_x = tx;
    st->path_target_y = ty;
    return st->path_len > 0;
}

// Issue the next step toward the cached path target. Returns true if
// a step was queued (or we've already arrived); false if pathing
// failed.
static bool ap_step_along_path(AutoplayState *st, int cx, int cy) {
    if (st->path_len == 0 || st->path_idx >= st->path_len) return false;
    // Skip path entries that match our current position (in case the
    // engine bounced us back, e.g. stepping onto a castle gate).
    while (st->path_idx < st->path_len &&
           st->path[st->path_idx].x == cx &&
           st->path[st->path_idx].y == cy) {
        st->path_idx++;
    }
    if (st->path_idx >= st->path_len) return true;  // arrived
    ApPoint next = st->path[st->path_idx];
    int k = ap_dir_key(cx, cy, next.x, next.y);
    if (k == 0) {
        AP_LOG("ap_dir_key 0: cur=(%d,%d) want=(%d,%d) idx=%d",
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

// =========================================================================
// Frame-host before-frame hook (combat input scripting)
// =========================================================================
//
// per_frame fires from the overworld main loop only. During RunCombat
// we drop into a different loop owned by combat_loop.c — so we wire a
// before-frame callback on the frame_host that fires every iteration
// of THAT loop too, and use it to script combat input. Outside of
// combat the callback is a no-op.

static AutoplayState *g_active_autoplay_state = NULL;
int                   g_autoplay_frame_counter = 0;

static void autoplay_before_frame(void *user) {
    (void)user;
    AutoplayState *st = g_active_autoplay_state;
    if (!st) return;
    int frame_no = g_autoplay_frame_counter++;
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

    // Combat over? Wait for RunCombat to return.
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
    int enemy_slot = ap_closest_enemy(c, u->x, u->y, &enemy_d);
    if (enemy_slot < 0) return;
    const CombatUnit *enemy = &c->units[COMBAT_SIDE_AI][enemy_slot];
    bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                      !combat_unit_surrounded(c, c->side, c->unit_id));
    if (can_shoot) {
        input_host_queue_key(KEY_S);
        ap_queue_picker(u->x, u->y, enemy->x, enemy->y);
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
                best_k = ap_dir_key(u->x, u->y, nx, ny);
            }
        }
        if (best_k != 0) {
            input_host_queue_key(best_k);
        } else {
            // No improving step — wait so the loop progresses.
            input_host_queue_key(KEY_SPACE);
        }
    }
    st->combat_unit_id_last_acted = c->unit_id;
    st->combat_frame_last_action = frame_no;
}

// =========================================================================
// shell_run hooks
// =========================================================================

static void autoplay_before_startup(ShellRunHooks *self) {
    (void)self;
    queue_standard_startup();
}

static ShellRunVerdict autoplay_per_frame(ShellRunHooks *self,
                                    Game *g, Map *m, Fog *f,
                                    Resources *res, int frame_no) {
    (void)f; (void)res;
    AutoplayState *st = (AutoplayState *)self->user;

    // Common: dialogs that pop up unexpectedly (e.g. weekend) — dismiss
    // them with SPACE. Skip this in phases that EXPECT a specific
    // dialog. Only queue when the input queue has room — otherwise
    // we'd just pile up dismiss keys faster than the engine consumes.
    if (dialog_is_active() &&
        st->phase != AP_DISMISS_INTRO &&
        st->phase != AP_TAKE_CONTRACT &&
        st->phase != AP_POST_COMBAT_DIALOG) {
        if (input_host_queue_depth() == 0) {
            input_host_queue_key(KEY_SPACE);
        }
        return SHELL_RUN_CONTINUE;
    }

    // Common: stray prompts during overworld phases.
    //   - "Foes!" (hostile wandering army): press Y to attack.
    //     Recruit-recipe army handles wandering encounters; rendered
    //     combat is driven by the shared combat scripting below.
    //   - "Castle X" / "Siege": villain castle gate — press Y so the
    //     dispatcher hands us into rendered combat.
    //   - A/B picker (chest gold-or-leadership): press A (gold).
    //   - All other y/n (search, etc.): decline with N.
    if (prompt_is_active() &&
        st->phase != AP_SIEGE_PROMPT &&
        st->phase != AP_COMBAT) {
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
            AP_LOG("prompt kind=%s header='%s' → %s",
                   kind, hdr, what);
            input_host_queue_key(key);
        }
        return SHELL_RUN_CONTINUE;
    }

    // Combat in progress (foe encounter or siege) handled from the
    // main loop too — the combat loop's frame_host_should_close
    // path runs the autoplay_before_frame callback that does the
    // actual input scripting, but mirroring the same logic here keeps
    // us moving when combat is invoked from a top-level prompt that
    // hasn't yielded to RunCombat yet.
    Combat *c_active = combat_current_rendered;
    if (c_active && c_active->result == 0 && !c_active->picker_active &&
        c_active->side == COMBAT_SIDE_PLAYER && c_active->unit_id >= 0 &&
        input_host_queue_depth() == 0) {
        CombatUnit *u = &c_active->units[c_active->side][c_active->unit_id];
        if (!u->acted && !(u->troop_idx < 0 || u->count <= 0)) {
            if (c_active->unit_id != st->combat_unit_id_last_acted ||
                frame_no >= st->combat_frame_last_action + 6) {
                int enemy_d = 0;
                int enemy_slot = ap_closest_enemy(c_active, u->x, u->y,
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
                        ap_queue_picker(u->x, u->y, enemy->x, enemy->y);
                    } else {
                        int dx = 0, dy = 0;
                        if (enemy->x > u->x) dx = 1;
                        else if (enemy->x < u->x) dx = -1;
                        if (enemy->y > u->y) dy = 1;
                        else if (enemy->y < u->y) dy = -1;
                        int k = ap_dir_key(u->x, u->y,
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
    case AP_DISMISS_INTRO:
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            st->phase = AP_WALK_TO_KING;
            st->phase_started_at = frame_no;
            AP_LOG("intro dismissed, walking to king_maximus to recruit");
        }
        AP_TIMEOUT_FAIL(0, 1200, "intro dialog never appeared");
        return SHELL_RUN_CONTINUE;

    case AP_WALK_TO_KING:
        if (views_active() == VIEW_HOME_CASTLE) {
            st->phase = AP_OPEN_RECRUIT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("entered king_maximus");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            11, 56, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed: pos->king_maximus (11,56)");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            AP_LOG("step failed walking to king_maximus");
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 1800,
                        "stuck walking to king_maximus");
        return SHELL_RUN_CONTINUE;

    case AP_OPEN_RECRUIT:
        input_host_queue_key(KEY_A);
        st->phase = AP_DO_RECRUITS;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_DO_RECRUITS:
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            input_host_queue_key(KEY_C);  // pikemen
            queue_recruit_count(10);
            input_host_queue_key(KEY_A);  // militia
            queue_recruit_count(30);
            input_host_queue_key(KEY_B);  // archers
            queue_recruit_count(8);
            st->phase = AP_LEAVE_RECRUIT;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 300,
                        "VIEW_RECRUIT_SOLDIERS never opened");
        return SHELL_RUN_CONTINUE;

    case AP_LEAVE_RECRUIT:
        if (army_total_hp(g) >= 300) {
            input_host_queue_key(KEY_ESCAPE);
            st->phase = AP_LEAVE_KING;
            st->phase_started_at = frame_no;
            AP_LOG("recruits done, army HP=%d gold=%d",
                   army_total_hp(g), g->stats.gold);
            return SHELL_RUN_CONTINUE;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 600,
                        "army HP never reached 300");
        return SHELL_RUN_CONTINUE;

    case AP_LEAVE_KING:
        if (views_active() == VIEW_NONE) {
            st->phase = AP_WALK_TO_TOWN_FOR_CONTRACT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("exited king_maximus, walking to a town for contract");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() == VIEW_HOME_CASTLE) {
            input_host_queue_key(KEY_ESCAPE);
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't exit king_maximus");
        return SHELL_RUN_CONTINUE;

    case AP_WALK_TO_TOWN_FOR_CONTRACT:
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_TAKE_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            st->path_len = 0;
            AP_LOG("entered town (contract)");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        // Hunterville (continentia, (12,60)) is the closest town to
        // the knight's spawn and the source of the next contract.
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            12, 60, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed: pos->hunterville (12,60) from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            AP_LOG("step failed walking to hunterville (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 3600,
                        "stuck walking to hunterville");
        return SHELL_RUN_CONTINUE;

    case AP_TAKE_CONTRACT: {
        // Town view is up. Press A to take the contract; the town
        // opens an info popup with the wanted villain's blurb.
        const char *active = g->contract.active_id;
        if (active[0] && strcmp(active, "murray") == 0) {
            st->phase = AP_CLOSE_TOWN_INFO_AFTER_CONTRACT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("contract taken: %s", active);
            return SHELL_RUN_CONTINUE;
        }
        if (active[0]) {
            AP_LOG("unexpected active contract '%s' (wanted murray)",
                   active);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_A);
            st->phase_action_queued = true;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "town didn't accept A for contract");
        return SHELL_RUN_CONTINUE;
    }

    case AP_CLOSE_TOWN_INFO_AFTER_CONTRACT:
        // The "wanted: Murray" dialog uses the town's info-popup
        // mechanism — SPACE/ESC dismisses it.
        input_host_queue_key(KEY_SPACE);
        st->phase = AP_EXIT_TOWN_AFTER_CONTRACT;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_EXIT_TOWN_AFTER_CONTRACT:
        // After the info popup dismisses we're back in the town menu.
        // Press B to rent the boat — we're already here.
        if (views_active() == VIEW_NONE) {
            st->phase = AP_WALK_TO_TOWN_FOR_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            st->path_len = 0;
            AP_LOG("exited town after contract, walking back for boat");
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_RENT_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("in town after contract, renting boat");
            return SHELL_RUN_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't transition after town contract");
        return SHELL_RUN_CONTINUE;

    case AP_WALK_TO_TOWN_FOR_BOAT:
        if (views_active() == VIEW_TOWN) {
            st->phase = AP_RENT_BOAT;
            st->phase_started_at = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            12, 60, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed back to hunterville from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 3600,
                        "stuck walking back to hunterville (boat)");
        return SHELL_RUN_CONTINUE;

    case AP_RENT_BOAT:
        if (g->boat.has_boat) {
            // Town's boat rental toggles the flag — no popup.
            input_host_queue_key(KEY_ESCAPE);
            st->phase = AP_EXIT_TOWN_AFTER_BOAT;
            st->phase_started_at = frame_no;
            st->phase_action_queued = false;
            AP_LOG("boat rented, gold=%d boat=(%d,%d)",
                   g->stats.gold, g->boat.x, g->boat.y);
            return SHELL_RUN_CONTINUE;
        }
        // Queue exactly ONE B per phase entry. has_boat toggles on
        // every B press, so a second pending B (queued before the
        // first registers) would CANCEL the rental on the next frame.
        if (!st->phase_action_queued) {
            input_host_queue_key(KEY_B);
            st->phase_action_queued = true;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "town didn't accept B for boat rental");
        return SHELL_RUN_CONTINUE;

    case AP_EXIT_TOWN_AFTER_BOAT:
        if (views_active() == VIEW_NONE) {
            st->phase = AP_BOARD_BOAT;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("exited town, walking to boat at (%d,%d)",
                   g->boat.x, g->boat.y);
            return SHELL_RUN_CONTINUE;
        }
        input_host_queue_key(KEY_ESCAPE);
        AP_TIMEOUT_FAIL(st->phase_started_at, 240,
                        "couldn't exit town after boat rental");
        return SHELL_RUN_CONTINUE;

    case AP_BOARD_BOAT: {
        // Walk onto the boat tile. Even though it's a water tile, the
        // step engine sees boat.has_boat + boat.x/y matching the
        // destination and sets travel_mode=TRAVEL_BOAT.
        int bx = g->boat.x, by = g->boat.y;
        if (g->travel_mode == TRAVEL_BOAT) {
            st->phase = AP_SAIL_TO_AZRAM_COAST;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("boarded boat at (%d,%d), now sailing", bx, by);
            return SHELL_RUN_CONTINUE;
        }
        int dx = 0, dy = 0;
        if (bx > g->position.x) dx = 1; else if (bx < g->position.x) dx = -1;
        if (by > g->position.y) dy = 1; else if (by < g->position.y) dy = -1;
        int k = ap_dir_key(g->position.x, g->position.y,
                           g->position.x + dx, g->position.y + dy);
        if (k == 0) {
            AP_LOG("already adjacent to boat? pos=(%d,%d) boat=(%d,%d)",
                   g->position.x, g->position.y, bx, by);
            return SHELL_RUN_EXIT_FAIL;
        }
        input_host_queue_key(k);
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "couldn't board boat");
        return SHELL_RUN_CONTINUE;
    }

    case AP_SAIL_TO_AZRAM_COAST: {
        // We may end up on land mid-sail (combat bounce, BFS picked
        // a land tile on the way). If we're close to azram, just
        // walk the rest of the way; otherwise try to re-board.
        if (g->travel_mode == TRAVEL_WALK) {
            int adx = g->position.x - 30; if (adx < 0) adx = -adx;
            int ady = g->position.y - 37; if (ady < 0) ady = -ady;
            // "Close enough" if within manhattan 25 AND BFS_WALK
            // confirms an overland path.
            ApPoint walk_diag[AP_PATH_MAX];
            int n_walk_to_gate = (adx + ady <= 25)
                ? ap_bfs(m, g->position.x, g->position.y, 30, 37,
                         AP_TRAVEL_WALK, walk_diag, AP_PATH_MAX)
                : 0;
            if (n_walk_to_gate > 0) {
                st->phase = AP_WALK_TO_AZRAM_GATE;
                st->phase_started_at = frame_no;
                st->path_len = 0;
                AP_LOG("disembarked at (%d,%d), "
                       "azram reachable on foot (%d steps), walking",
                       g->position.x, g->position.y, n_walk_to_gate);
                return SHELL_RUN_CONTINUE;
            }
            // Too far on foot — re-board the boat (it's at prev water
            // tile) and keep sailing.
            if (g->boat.has_boat && g->boat.x >= 0) {
                int dx = 0, dy = 0;
                if (g->boat.x > g->position.x) dx = 1;
                else if (g->boat.x < g->position.x) dx = -1;
                if (g->boat.y > g->position.y) dy = 1;
                else if (g->boat.y < g->position.y) dy = -1;
                int k = ap_dir_key(g->position.x, g->position.y,
                                   g->position.x + dx,
                                   g->position.y + dy);
                if (k != 0 && input_host_queue_depth() == 0) {
                    input_host_queue_key(k);
                }
                AP_TIMEOUT_FAIL(st->phase_started_at, 14400,
                                "stuck reboarding boat after disembark");
                return SHELL_RUN_CONTINUE;
            }
            // No boat — give up sailing, fall through to overland.
            st->phase = AP_WALK_TO_AZRAM_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("disembarked at (%d,%d) and no boat — fall through "
                   "to overland walk attempt",
                   g->position.x, g->position.y);
            return SHELL_RUN_CONTINUE;
        }
        // Plan: sail directly to a LAND tile near azram. Stepping
        // from water onto land in boat mode auto-disembarks at the
        // destination tile. Then a short walk reaches the gate.
        //
        // Search a wide ring around azram gate for a land tile that
        // (a) has a walking path to the gate and (b) is reachable by
        // sail. ap_bfs in BOAT mode allows the goal to be non-water.
        int best_x = -1, best_y = -1;
        if (st->path_len == 0) {
            // Azram's gate sits at the castle's (x,y) coord (engine
            // stamps gate at z->castles[i].x, .y). Catalog: (30,36).
            int azram_gate_x = 30, azram_gate_y = 36;
            int best_total = 1 << 30;
            ApPoint sail_path[AP_PATH_MAX];
            ApPoint walk_path[AP_PATH_MAX];
            for (int dy = -12; dy <= 12; dy++) {
                for (int dx = -12; dx <= 12; dx++) {
                    int tx = azram_gate_x + dx;
                    int ty = azram_gate_y + dy;
                    if (!MapInBounds(m, tx, ty)) continue;
                    const Tile *t = MapGetTile(m, tx, ty);
                    if (!t) continue;
                    if (!TerrainWalkable(t->terrain) || t->blocks_foot) continue;
                    if (t->interactive != INTERACT_NONE) continue;
                    int n_walk = ap_bfs(m, tx, ty,
                                        azram_gate_x, azram_gate_y,
                                        AP_TRAVEL_WALK,
                                        walk_path, AP_PATH_MAX);
                    if (n_walk <= 0) continue;
                    int n_sail = ap_bfs(m, g->position.x, g->position.y,
                                        tx, ty, AP_TRAVEL_BOAT,
                                        sail_path, AP_PATH_MAX);
                    if (n_sail <= 0) continue;
                    int total = n_sail + n_walk;
                    if (total < best_total) {
                        best_total = total;
                        best_x = tx;
                        best_y = ty;
                        memcpy(st->path, sail_path,
                               sizeof(ApPoint) * (size_t)n_sail);
                        st->path_len = n_sail;
                        st->path_idx = 0;
                        st->path_target_x = tx;
                        st->path_target_y = ty;
                    }
                }
            }
            if (best_x < 0) {
                AP_LOG("no sailable approach to azram gate from (%d,%d)",
                       g->position.x, g->position.y);
                return SHELL_RUN_EXIT_FAIL;
            }
            AP_LOG("sailing to land (%d,%d) (sail+walk=%d steps)",
                   best_x, best_y, best_total);
        }
        // If we've arrived at the planned coastline tile, advance.
        if (g->position.x == st->path_target_x &&
            g->position.y == st->path_target_y) {
            st->phase = AP_DISEMBARK_NEAR_AZRAM;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            return SHELL_RUN_CONTINUE;
        }
        // Sync path_idx to our current position; recompute if drifted.
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            st->path_target_x, st->path_target_y,
                            AP_TRAVEL_BOAT)) {
            AP_LOG("sail: BFS recompute failed from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            AP_LOG("sailing step failed at (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "stuck sailing to azram coast");
        return SHELL_RUN_CONTINUE;
    }

    case AP_DISEMBARK_NEAR_AZRAM: {
        // From the boat tile we're on, step onto an adjacent land
        // tile (preferring the one closest to azram's gate). The
        // engine handles disembark when stepping from water onto land.
        if (g->travel_mode != TRAVEL_BOAT) {
            st->phase = AP_WALK_TO_AZRAM_GATE;
            st->phase_started_at = frame_no;
            st->path_len = 0;
            AP_LOG("disembarked at (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_CONTINUE;
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
                chosen_k = ap_dir_key(g->position.x, g->position.y,
                                      nx, ny);
            }
        }
        if (chosen_k == 0) {
            AP_LOG("no adjacent land tile to disembark from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        input_host_queue_key(chosen_k);
        AP_TIMEOUT_FAIL(st->phase_started_at, 120,
                        "couldn't disembark");
        return SHELL_RUN_CONTINUE;
    }

    case AP_WALK_TO_AZRAM_GATE: {
        // Already won? If villains_caught[murray] flipped, we're
        // done — jump to VERIFY.
        const VillainDef *vm = villain_by_id("murray");
        if (vm && g->contract.villains_caught[vm->index]) {
            st->phase = AP_VERIFY;
            st->phase_started_at = frame_no;
            AP_LOG("villains_caught[murray] set — verifying");
            return SHELL_RUN_CONTINUE;
        }
        if (prompt_is_active()) {
            const char *hdr = prompt_header_text();
            if (hdr && strstr(hdr, "Siege")) {
                st->phase = AP_SIEGE_PROMPT;
                st->phase_started_at = frame_no;
                AP_LOG("siege prompt up");
                return SHELL_RUN_CONTINUE;
            }
            return SHELL_RUN_CONTINUE;
        }
        if (views_active() != VIEW_NONE) {
            input_host_queue_key(KEY_ESCAPE);
            return SHELL_RUN_CONTINUE;
        }
        if (!ap_ensure_path(st, m, g->position.x, g->position.y,
                            30, 36, AP_TRAVEL_WALK)) {
            AP_LOG("BFS failed walking to azram gate from (%d,%d)",
                   g->position.x, g->position.y);
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!ap_step_along_path(st, g->position.x, g->position.y)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        AP_TIMEOUT_FAIL(st->phase_started_at, 7200,
                        "stuck walking to azram gate");
        return SHELL_RUN_CONTINUE;
    }

    case AP_SIEGE_PROMPT:
        // Press Y to accept the siege prompt.
        input_host_queue_key(KEY_Y);
        st->phase = AP_COMBAT;
        st->phase_started_at = frame_no;
        st->combat_unit_id_last_acted = -1;
        st->combat_frame_last_action = frame_no;
        st->combat_picker_keys_left = 0;
        AP_LOG("siege confirmed, entering combat");
        return SHELL_RUN_CONTINUE;

    case AP_COMBAT: {
        Combat *c = combat_current_rendered;
        if (!c) {
            // Combat hasn't started yet (siege prompt still resolving)
            // or it has ended. If ended, check for victory dialog.
            if (st->phase_started_at + 60 < frame_no) {
                if (dialog_is_active()) {
                    st->phase = AP_POST_COMBAT_DIALOG;
                    st->phase_started_at = frame_no;
                    return SHELL_RUN_CONTINUE;
                }
            }
            return SHELL_RUN_CONTINUE;
        }
        // Active unit on player side: decide an action. (Same logic
        // as autoplay_before_frame; this branch only fires if the
        // per_frame hook happens to be called while combat is up.)
        if (c->side != COMBAT_SIDE_PLAYER) return SHELL_RUN_CONTINUE;
        if (c->unit_id < 0) return SHELL_RUN_CONTINUE;
        if (c->picker_active) return SHELL_RUN_CONTINUE;
        if (c->units[c->side][c->unit_id].acted) return SHELL_RUN_CONTINUE;
        if (c->unit_id == st->combat_unit_id_last_acted &&
            frame_no < st->combat_frame_last_action + 10) {
            return SHELL_RUN_CONTINUE;
        }
        CombatUnit *u = &c->units[c->side][c->unit_id];
        const TroopDef *td = troop_by_index(u->troop_idx);
        if (!td) return SHELL_RUN_CONTINUE;
        int enemy_d = 0;
        int enemy_slot = ap_closest_enemy(c, u->x, u->y, &enemy_d);
        if (enemy_slot < 0) return SHELL_RUN_CONTINUE;
        const CombatUnit *enemy = &c->units[COMBAT_SIDE_AI][enemy_slot];

        bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                          !combat_unit_surrounded(c, c->side,
                                                  c->unit_id));
        if (can_shoot) {
            input_host_queue_key(KEY_S);
            ap_queue_picker(u->x, u->y, enemy->x, enemy->y);
            st->combat_unit_id_last_acted = c->unit_id;
            st->combat_frame_last_action = frame_no;
            return SHELL_RUN_CONTINUE;
        }
        int dx = 0, dy = 0;
        if (enemy->x > u->x) dx = 1; else if (enemy->x < u->x) dx = -1;
        if (enemy->y > u->y) dy = 1; else if (enemy->y < u->y) dy = -1;
        int k = ap_dir_key(u->x, u->y, u->x + dx, u->y + dy);
        if (k == 0) input_host_queue_key(KEY_SPACE);
        else        input_host_queue_key(k);
        st->combat_unit_id_last_acted = c->unit_id;
        st->combat_frame_last_action = frame_no;
        AP_TIMEOUT_FAIL(st->phase_started_at, 36000,
                        "combat ran too long (no progress)");
        return SHELL_RUN_CONTINUE;
    }

    case AP_POST_COMBAT_DIALOG:
        // Victory dialog (and contract follow-up). SPACE dismisses
        // each page.
        if (dialog_is_active()) {
            input_host_queue_key(KEY_SPACE);
            return SHELL_RUN_CONTINUE;
        }
        st->phase = AP_VERIFY;
        st->phase_started_at = frame_no;
        return SHELL_RUN_CONTINUE;

    case AP_VERIFY: {
        const VillainDef *vm = villain_by_id("murray");
        if (!vm) {
            AP_LOG("villain catalog missing murray");
            return SHELL_RUN_EXIT_FAIL;
        }
        if (!g->contract.villains_caught[vm->index]) {
            AP_LOG("villains_caught[murray] is false; capture failed");
            return SHELL_RUN_EXIT_FAIL;
        }
        // Drop a rolling checkpoint at the last save slot so a human
        // can load the same world state and pick up where autoplay
        // left off. A failed write is treated as a run failure — the
        // checkpoint IS part of the autoplay contract.
        if (!autoplay_save_checkpoint(g, m, f)) {
            return SHELL_RUN_EXIT_FAIL;
        }
        printf("autoplay: PASS — villains_caught[murray]=true, "
               "gold=%d\n",
               g->stats.gold);
        return SHELL_RUN_EXIT_PASS;
    }
    }
    return SHELL_RUN_EXIT_FAIL;
}

// =========================================================================
// Public entry
// =========================================================================

int autoplay_run(int argc, char **argv) {
    // Switch the input + frame shims to test mode and tell startup
    // to skip splashes/credits. shell_run_game itself uses the hooks
    // we pass to drive the game; window visibility / pacing is
    // decided inside it based on the presence of --visible in argv.
    input_host_use_queue();
    frame_host_use_test();
    startup_skip_intros = true;

    AutoplayState state = { 0 };
    state.phase = AP_DISMISS_INTRO;
    state.combat_unit_id_last_acted = -1;
    g_active_autoplay_state = &state;
    g_autoplay_frame_counter = 0;
    frame_host_set_before_frame(autoplay_before_frame, NULL);

    ShellRunHooks hooks = {
        .before_startup = autoplay_before_startup,
        .per_frame      = autoplay_per_frame,
        .user           = &state,
    };
    int rc = shell_run_game(argc, argv, &hooks);

    frame_host_set_before_frame(NULL, NULL);
    g_active_autoplay_state = NULL;
    return rc;
}
