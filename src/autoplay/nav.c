#include "autoplay/nav.h"
#include "autoplay/internal.h"
#include "adventure.h"
#include "tile.h"
#include <string.h>

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
// --------------------------------------------------------------------------
// Multi-mode BFS: explores both travel modes simultaneously. Each
// (x,y,mode) is a separate node. Transitions:
//   foot at boat-tile → boat at same tile (board)
//   boat at land tile (walkable on foot, not water) → foot at same
//     tile (disembark)
// Returns the first key to press from (sx,sy) in start_mode toward
// (gx,gy). Mode of goal not constrained.
// When `avoid_foes` is true, refuse to traverse any tile within chebyshev
// <= 2 of an alive hostile foe (the foe-follow trigger range — see
// engine/game.c:GameFoesFollow). The goal tile is exempt so we can still
// pick up a chest immediately adjacent to a foe. Friendly foes are
// ignored (they're recruit prompts, not combat triggers).
static bool tile_in_foe_envelope(const Game *g, int x, int y) {
    if (!g) return false;
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->alive || f->friendly) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        int dx = x - f->x; if (dx < 0) dx = -dx;
        int dy = y - f->y; if (dy < 0) dy = -dy;
        int ch = (dx > dy) ? dx : dy;
        if (ch <= 2) return true;
    }
    return false;
}

// Flags for bfs_multimode.
#define NAV_AVOID_FOES   (1u << 0)
#define NAV_AVOID_DESERT (1u << 1)

static int bfs_multimode(const Map *m, const Game *g,
                         int sx, int sy, int start_mode,
                         int gx, int gy, unsigned int flags) {
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
            bool is_goal = (ny == gy && nx == gx);
            if (t->interactive == INTERACT_CASTLE_GATE && !is_goal) continue;
            if (t->interactive == INTERACT_TOWN && !is_goal) continue;
            // Dwellings (plains/forest/hills/dungeon) all trigger a
            // recruit prompt on step-on. Skip unless explicit goal.
            if ((t->interactive == INTERACT_DWELLING_PLAINS ||
                 t->interactive == INTERACT_DWELLING_FOREST ||
                 t->interactive == INTERACT_DWELLING_HILLS  ||
                 t->interactive == INTERACT_DWELLING_DUNGEON) && !is_goal) continue;
            // Foe-avoidance: skip tiles within chebyshev 2 of any alive
            // hostile foe in the current zone. Goal is exempt so the
            // caller can route onto a chest adjacent to a foe.
            if ((flags & NAV_AVOID_FOES) && !is_goal &&
                tile_in_foe_envelope(g, nx, ny)) continue;
            // Desert-avoidance: each desert step zeros the day's step
            // budget (engine/tile.c:TerrainMoveCost), so even one
            // wastes a full day. Skip unless this is the goal tile
            // (allows picking up a chest that happens to sit in sand).
            if ((flags & NAV_AVOID_DESERT) && !is_goal &&
                t->terrain == TERRAIN_DESERT) continue;

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
                            goal_x, goal_y, /*flags=*/0u);
    if (key) return key;

    // No path. If the hero is on foot without a boat, the OLD
    // behavior was to route to the nearest town to auto-rent. That
    // produces an infinite KEY_LEFT/KEY_DOWN loop because:
    //   1. The hero bounces off the town tile (engine pushes
    //      VIEW_TOWN + bounce_back).
    //   2. The caller doesn't handle VIEW_TOWN (or just ESCs out
    //      without renting).
    //   3. Next tick BFS still has no boat → fallback returns the
    //      same town-direction key → repeat.
    //
    // User directive: never loop on this. Return 0 (no path); the
    // dispatcher's assertion failure will terminate the autoplay.
    return 0;
}

// Public entry point — like ap_nav_step, but the multi-mode BFS
// refuses to traverse any tile within chebyshev <= 2 of an alive
// hostile foe (so we never trigger pursuit). Goal tile is exempt.
int ap_nav_step_avoiding_foes(const Game *g, const Map *m,
                              int goal_x, int goal_y) {
    if (!g || !m) return 0;
    if (g->position.x == goal_x && g->position.y == goal_y) return 0;
    int mode = (g->travel_mode == TRAVEL_BOAT) ? 1 : 0;
    return bfs_multimode(m, g, g->position.x, g->position.y, mode,
                         goal_x, goal_y, NAV_AVOID_FOES);
}

// Public entry point — like ap_nav_step_avoiding_foes, but also
// refuses to step onto desert. Use when traversing or sailing past
// a continent (Saharia / Forestria) where desert peninsulas would
// otherwise cost a full day per step.
int ap_nav_step_avoiding_foes_and_desert(const Game *g, const Map *m,
                                         int goal_x, int goal_y) {
    if (!g || !m) return 0;
    if (g->position.x == goal_x && g->position.y == goal_y) return 0;
    int mode = (g->travel_mode == TRAVEL_BOAT) ? 1 : 0;
    return bfs_multimode(m, g, g->position.x, g->position.y, mode,
                         goal_x, goal_y,
                         NAV_AVOID_FOES | NAV_AVOID_DESERT);
}

// Public entry point — like ap_nav_step but also refuses to step
// onto desert. Use when the caller wants to fight wandering armies
// (foes allowed) but still wants to dodge the desert-day-penalty
// trap.
int ap_nav_step_avoiding_desert(const Game *g, const Map *m,
                                int goal_x, int goal_y) {
    if (!g || !m) return 0;
    if (g->position.x == goal_x && g->position.y == goal_y) return 0;
    int mode = (g->travel_mode == TRAVEL_BOAT) ? 1 : 0;
    return bfs_multimode(m, g, g->position.x, g->position.y, mode,
                         goal_x, goal_y, NAV_AVOID_DESERT);
}
