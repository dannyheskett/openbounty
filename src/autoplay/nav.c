#include "autoplay/nav.h"
#include "autoplay/internal.h"
#include "adventure.h"
#include "tile.h"

#include "raylib.h"

#include <stdbool.h>
#include <string.h>

// BFS frontier: bounded queue indexed by linear (y * W + x).
// continentia / forestria / archipelia / saharia are all 64×64, so
// 4096 cells. A queue capacity of W*H covers any zone where BFS
// visits each cell at most once.
#define NAV_MAX_W   128
#define NAV_MAX_H   128
#define NAV_MAX_CELLS (NAV_MAX_W * NAV_MAX_H)

typedef enum {
    DIR_NONE = 0,
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT,
} NavDir;

static int nav_key(NavDir d) {
    switch (d) {
    case DIR_UP:    return KEY_UP;
    case DIR_DOWN:  return KEY_DOWN;
    case DIR_LEFT:  return KEY_LEFT;
    case DIR_RIGHT: return KEY_RIGHT;
    default:        return 0;
    }
}

// One BFS pass with the given walkability predicate. Records the
// direction taken to reach each visited cell. Returns true if (gx,gy)
// is reached.
//
// `start_x/y` is the BFS source. `goal_x/y` is the target. The
// predicate `walk` is called as walk(map, x, y, is_goal, ctx).
// is_goal=true is passed when the candidate tile equals the goal; the
// predicate may relax rules for the goal (e.g., allow disembarking
// onto a non-water tile).
typedef bool (*NavWalkable)(const Map *m, int x, int y, bool is_goal,
                            void *ctx);

static bool bfs(const Map *m, int sx, int sy, int gx, int gy,
                NavWalkable walk, void *ctx,
                NavDir *out_first_dir) {
    int W = m->width;
    int H = m->height;
    if (W <= 0 || H <= 0 || W > NAV_MAX_W || H > NAV_MAX_H) return false;
    if (sx < 0 || sy < 0 || sx >= W || sy >= H) return false;
    if (gx < 0 || gy < 0 || gx >= W || gy >= H) return false;
    if (sx == gx && sy == gy) {
        if (out_first_dir) *out_first_dir = DIR_NONE;
        return true;
    }

    static int   parent_dir[NAV_MAX_CELLS]; // DIR_* used to arrive at cell
    static int   visited[NAV_MAX_CELLS];    // generation counter for reuse
    static int   queue[NAV_MAX_CELLS];
    static int   generation = 0;
    generation++;

    int head = 0, tail = 0;
    int s_idx = sy * W + sx;
    visited[s_idx] = generation;
    parent_dir[s_idx] = DIR_NONE;
    queue[tail++] = s_idx;

    static const int dy[5] = {  0, -1,  1,  0,  0 };
    static const int dx[5] = {  0,  0,  0, -1,  1 };

    while (head < tail) {
        int cur = queue[head++];
        int cy = cur / W;
        int cx = cur % W;
        if (cy == gy && cx == gx) {
            // Walk back to the source to find the first direction.
            int n = cur;
            NavDir d = DIR_NONE;
            while (n != s_idx) {
                d = (NavDir)parent_dir[n];
                int ny = n / W;
                int nx = n % W;
                int py = ny, px = nx;
                if (d == DIR_UP)    py = ny + 1;
                if (d == DIR_DOWN)  py = ny - 1;
                if (d == DIR_LEFT)  px = nx + 1;
                if (d == DIR_RIGHT) px = nx - 1;
                n = py * W + px;
            }
            if (out_first_dir) *out_first_dir = d;
            return true;
        }
        for (int i = 1; i <= 4; i++) {
            int ny = cy + dy[i];
            int nx = cx + dx[i];
            if (ny < 0 || nx < 0 || ny >= H || nx >= W) continue;
            int n_idx = ny * W + nx;
            if (visited[n_idx] == generation) continue;
            bool is_goal = (ny == gy && nx == gx);
            if (!walk(m, nx, ny, is_goal, ctx)) continue;
            visited[n_idx] = generation;
            parent_dir[n_idx] = i; // 1..4 = NavDir
            queue[tail++] = n_idx;
        }
    }
    return false;
}

// --------------------------------------------------------------------------
// Walkability predicates

typedef struct {
    const Game *g;
} NavCtx;

// On foot: walk on land. Castle gates are never traversed. Step onto
// the boat tile counts as walkable (engine treats it as boarding).
static bool walk_foot(const Map *m, int x, int y, bool is_goal, void *vctx) {

    NavCtx *ctx = (NavCtx *)vctx;
    const Tile *t = MapGetTile(m, x, y);
    if (!t) return false;
    if (t->interactive == INTERACT_CASTLE_GATE) return false;
    // Towns trigger a modal on step-on; only allow stepping onto a
    // town tile if it's the explicit goal (e.g., routing-to-town to
    // rent a boat). The caller-provided `is_goal` flag indicates this.
    if (t->interactive == INTERACT_TOWN && !is_goal) return false;
    if ((t->interactive == INTERACT_DWELLING_PLAINS ||
         t->interactive == INTERACT_DWELLING_FOREST ||
         t->interactive == INTERACT_DWELLING_HILLS  ||
         t->interactive == INTERACT_DWELLING_DUNGEON) && !is_goal) return false;
    // Step onto the parked boat = board (engine handles this in step.c).
    if (ctx->g->boat.has_boat &&
        x == ctx->g->boat.x && y == ctx->g->boat.y) return true;
    return adventure_walkable_on_foot(t);
}

// In a boat: sail water. Goal is allowed even if non-water (disembark).
static bool walk_boat(const Map *m, int x, int y, bool is_goal, void *vctx) {
    (void)vctx;
    const Tile *t = MapGetTile(m, x, y);
    if (!t) return false;
    if (t->interactive == INTERACT_CASTLE_GATE) return false;
    // Towns trigger a modal on step-on; only allow stepping onto a
    // town tile if it's the explicit goal (e.g., routing-to-town to
    // rent a boat). The caller-provided `is_goal` flag indicates this.
    if (t->interactive == INTERACT_TOWN && !is_goal) return false;
    if ((t->interactive == INTERACT_DWELLING_PLAINS ||
         t->interactive == INTERACT_DWELLING_FOREST ||
         t->interactive == INTERACT_DWELLING_HILLS  ||
         t->interactive == INTERACT_DWELLING_DUNGEON) && !is_goal) return false;
    if (adventure_walkable_in_boat(t)) {
        // Disallow disembarking onto land unless it's the goal — sailing
        // semantics: only stop at the destination.
        if (t->terrain == TERRAIN_WATER || t->is_bridge) return true;
        return is_goal;
    }
    return false;
}

// On foot, but ALSO treat any town tile as "walkable goal candidate"
// for boat-renting routing. Used when the hero needs a boat but doesn't
// have one — we find the nearest town and walk to it.
static bool walk_foot_to_any_town(const Map *m, int x, int y, bool is_goal,
                                  void *vctx) {

    NavCtx *ctx = (NavCtx *)vctx;
    const Tile *t = MapGetTile(m, x, y);
    if (!t) return false;
    if (t->interactive == INTERACT_CASTLE_GATE) return false;
    // Towns trigger a modal on step-on; only allow stepping onto a
    // town tile if it's the explicit goal (e.g., routing-to-town to
    // rent a boat). The caller-provided `is_goal` flag indicates this.
    if (t->interactive == INTERACT_TOWN && !is_goal) return false;
    if ((t->interactive == INTERACT_DWELLING_PLAINS ||
         t->interactive == INTERACT_DWELLING_FOREST ||
         t->interactive == INTERACT_DWELLING_HILLS  ||
         t->interactive == INTERACT_DWELLING_DUNGEON) && !is_goal) return false;
    if (ctx->g->boat.has_boat &&
        x == ctx->g->boat.x && y == ctx->g->boat.y) return true;
    return adventure_walkable_on_foot(t);
}

// --------------------------------------------------------------------------
// Find the nearest town the hero can walk to (any town with a defined
// boat_spawn). Output its (x,y) on success.

static bool find_nearest_town(const Game *g, const Map *m,
                              int *out_x, int *out_y) {
    int W = m->width, H = m->height;
    int best_x = -1, best_y = -1;
    int best_d = -1;
    NavCtx ctx = { .g = g };
    NavDir tmp;
    // For each town on the map, BFS-check reachability and pick the
    // closest one with a defined boat_spawn.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const Tile *t = MapGetTile(m, x, y);
            if (!t || t->interactive != INTERACT_TOWN) continue;
            if (t->boat_spawn_x < 0 || t->boat_spawn_y < 0) continue;
            // Quick Manhattan filter to skip obviously-far towns.
            int md = (x > g->position.x ? x - g->position.x
                                        : g->position.x - x)
                   + (y > g->position.y ? y - g->position.y
                                        : g->position.y - y);
            if (best_d >= 0 && md > best_d) continue;
            if (bfs(m, g->position.x, g->position.y, x, y,
                    walk_foot_to_any_town, &ctx, &tmp)) {
                if (best_d < 0 || md < best_d) {
                    best_d = md;
                    best_x = x;
                    best_y = y;
                }
            }
        }
    }
    if (best_d < 0) return false;
    *out_x = best_x;
    *out_y = best_y;
    return true;
}

// --------------------------------------------------------------------------
// Multi-mode BFS: explores both travel modes simultaneously. Each
// (x,y,mode) is a separate node. Transitions:
//   foot at boat-tile → boat at same tile (board)
//   boat at land tile (walkable on foot, not water) → foot at same
//     tile (disembark)
// Returns the first key to press from (sx,sy) in start_mode toward
// (gx,gy). Mode of goal not constrained.
static int bfs_multimode(const Map *m, const Game *g,
                         int sx, int sy, int start_mode,
                         int gx, int gy) {
    int W = m->width;
    int H = m->height;
    if (W <= 0 || H <= 0 || W > NAV_MAX_W || H > NAV_MAX_H) return 0;
    if (sx < 0 || sy < 0 || sx >= W || sy >= H) return 0;
    if (gx < 0 || gy < 0 || gx >= W || gy >= H) return 0;
    if (sx == gx && sy == gy) return 0;

    // Two-layer state: index = mode * (W*H) + y*W + x. mode 0 = foot,
    // mode 1 = boat.
    static int   parent_dir[2 * NAV_MAX_CELLS];
    static int   parent_idx[2 * NAV_MAX_CELLS];
    static int   visited[2 * NAV_MAX_CELLS];
    static int   queue[2 * NAV_MAX_CELLS];
    static int   generation = 0;
    generation++;

    int head = 0, tail = 0;
    int s_idx = start_mode * (W * H) + sy * W + sx;
    visited[s_idx] = generation;
    parent_dir[s_idx] = DIR_NONE;
    parent_idx[s_idx] = -1;
    queue[tail++] = s_idx;

    static const int dy[5] = {  0, -1,  1,  0,  0 };
    static const int dx[5] = {  0,  0,  0, -1,  1 };

    int found = -1;
    while (head < tail) {
        int cur = queue[head++];
        int mode = cur / (W * H);
        int rem = cur % (W * H);
        int cy = rem / W;
        int cx = rem % W;
        if (cy == gy && cx == gx) { found = cur; break; }
        for (int i = 1; i <= 4; i++) {
            int ny = cy + dy[i];
            int nx = cx + dx[i];
            if (ny < 0 || nx < 0 || ny >= H || nx >= W) continue;
            const Tile *t = MapGetTile(m, nx, ny);
            if (!t) continue;
            if (t->interactive == INTERACT_CASTLE_GATE) continue;
            bool is_goal = (ny == gy && nx == gx);
            if (t->interactive == INTERACT_TOWN && !is_goal) continue;
            // Dwellings (plains/forest/hills/dungeon) all trigger a
            // recruit prompt on step-on. Skip unless explicit goal.
            if ((t->interactive == INTERACT_DWELLING_PLAINS ||
                 t->interactive == INTERACT_DWELLING_FOREST ||
                 t->interactive == INTERACT_DWELLING_HILLS  ||
                 t->interactive == INTERACT_DWELLING_DUNGEON) && !is_goal) continue;

            int next_mode = mode;
            bool ok = false;
            bool is_water = (t->terrain == TERRAIN_WATER) || t->is_bridge;
            if (mode == 0) {
                // Foot: walk to land, or board boat tile.
                bool is_boat_tile = (g->boat.has_boat &&
                                     nx == g->boat.x && ny == g->boat.y);
                if (is_boat_tile) {
                    ok = true;
                    next_mode = 1; // board
                } else if (adventure_walkable_on_foot(t)) {
                    ok = true;
                }
            } else {
                // Boat: sail water, or disembark onto land.
                if (is_water) {
                    ok = true;
                } else if (adventure_walkable_on_foot(t)) {
                    ok = true;
                    next_mode = 0; // disembark
                }
            }
            if (!ok) continue;
            int n_idx = next_mode * (W * H) + ny * W + nx;
            if (visited[n_idx] == generation) continue;
            visited[n_idx] = generation;
            parent_dir[n_idx] = i;
            parent_idx[n_idx] = cur;
            queue[tail++] = n_idx;
        }
    }
    if (found < 0) return 0;
    // Walk back to the source to find the first direction.
    NavDir first = DIR_NONE;
    int n = found;
    while (parent_idx[n] != -1) {
        first = (NavDir)parent_dir[n];
        n = parent_idx[n];
    }
    return nav_key(first);
}

// Public entry point.
int ap_nav_step(const Game *g, const Map *m, int goal_x, int goal_y) {
    if (!g || !m) return 0;
    if (g->position.x == goal_x && g->position.y == goal_y) return 0;

    int mode = (g->travel_mode == TRAVEL_BOAT) ? 1 : 0;

    // Try a direct multi-mode plan first. This handles foot, boat, and
    // any combination of board/disembark transitions in one BFS.
    int key = bfs_multimode(m, g, g->position.x, g->position.y, mode,
                            goal_x, goal_y);
    if (key) return key;

    // No path — likely we need a boat but don't have one. Route to the
    // nearest town to trigger an auto-rent. (Walk_foot_to_any_town
    // is the same as walk_foot here but treats the town as the goal.)
    if (mode == 0 && !g->boat.has_boat) {
        int tx, ty;
        if (find_nearest_town(g, m, &tx, &ty)) {
            NavCtx ctx = { .g = g };
            NavDir d = DIR_NONE;
            if (bfs(m, g->position.x, g->position.y, tx, ty,
                    walk_foot_to_any_town, &ctx, &d)) {
                return nav_key(d);
            }
        }
    }
    return 0;
}
