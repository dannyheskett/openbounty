// demo/demo_path.c
//
// Pathfinding over the PLAYER'S knowledge: a two-layer (foot / boat) BFS over
// fog-seen tiles of the current zone. This is deliberately a player's mental
// map, not an oracle: unexplored tiles are walls, interactive tiles are only
// ever destinations (stepping on one commits to its flow), and tiles beside a
// foe this run has already fought and lost to are given a wide berth (foes
// chase anyone who walks within reach; see s_avoid below for the exact rule). Uniform step cost -- a demo player does not
// micro-optimize desert days.

#include "demo_internal.h"

#include <string.h>

#include "adventure.h"
#include "tile.h"

#define DP_W MAP_MAX_W
#define DP_H MAP_MAX_H
#define DP_CELLS (DP_W * DP_H)
#define DP_FOOT 0
#define DP_BOAT 1

static const int DP_DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int DP_DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

// Avoidance is limited to KNOWN-LETHAL foes (fought and lost at this
// strength): their reach is kept out of the walkable set so the player stops
// re-entering the pockets they camp. Merely strong-looking foes cost nothing
// to pass -- a collision prompt is freely declined.
static unsigned char s_avoid[DP_H][DP_W];

static void dp_build_avoid(const Game *g, const Fog *fog) {
    memset(s_avoid, 0, sizeof s_avoid);
    if (demo_state()->rings_off) return;   // sealed in: brave the reach
    long ours = demo_army_power(g);
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->alive || f->friendly) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        if (!FogSeen(fog, f->x, f->y)) continue;
        if (!demo_known_loss(f->placement_id, ours)) continue;
        // Never wall the hero's own surroundings (he must stay free to leave).
        int hdx = f->x - g->position.x, hdy = f->y - g->position.y;
        if (hdx < 0) hdx = -hdx;
        if (hdy < 0) hdy = -hdy;
        if (hdx <= 1 && hdy <= 1) continue;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = f->x + dx, y = f->y + dy;
                if (x >= 0 && x < DP_W && y >= 0 && y < DP_H)
                    s_avoid[y][x] = 1;
            }
    }
}

// Can the walker ENTER (nx,ny) as a pass-through cell in `layer`, possibly
// switching layers? Returns -1 (no) or the layer occupied after the move.
static int dp_enter(const Game *g, const Map *map, const Fog *fog,
                    int layer, int nx, int ny) {
    if (!MapInBounds(map, nx, ny)) return -1;
    if (!FogSeen(fog, nx, ny)) return -1;
    if (s_avoid[ny][nx]) return -1;
    const Tile *t = MapGetTile(map, nx, ny);
    if (!t || t->interactive != INTERACT_NONE) return -1;
    bool boat_here = g->boat.has_boat &&
                     strcmp(g->boat.zone, g->position.zone) == 0;
    if (layer == DP_FOOT) {
        // Boarding: step onto the parked boat's tile.
        if (boat_here && nx == g->boat.x && ny == g->boat.y) return DP_BOAT;
        if (adventure_walkable_on_foot(t)) return DP_FOOT;
        return -1;
    }
    // Boat layer: only WATER (and bridges) keep the hero sailing; any other
    // step is a disembark onto foot. (adventure_walkable_in_boat returns true
    // for land too -- it answers "may the boat-mode hero step here", and that
    // step LANDS. Treating it as "stays afloat" sailed the model across
    // deserts the engine would never sail -- measured: it hid the real
    // water+landing route to azram behind phantom boat-land paths.)
    if (t->terrain == TERRAIN_WATER || t->is_bridge) return DP_BOAT;
    if (adventure_walkable_on_foot(t)) return DP_FOOT;
    return -1;
}

// Shared BFS core. When goal >= 0 (packed y*W+x), stops on reaching a cell
// ADJACENT to the goal and writes the first step of the path into out_dx/dy
// (the final lunge onto the goal tile included). When goal < 0, floods
// everything and fills pf (which must be non-NULL).
static bool dp_bfs(const Game *g, const Map *map, const Fog *fog,
                   int goal, DemoField *pf, int *out_dx, int *out_dy) {
    static int  q[2 * DP_CELLS];
    static int  parent[2][DP_CELLS];        // packed (layer<<16 | cell); -1 root
    static short dist[2][DP_CELLS];
    memset(dist, -1, sizeof dist);
    memset(parent, -1, sizeof parent);
    dp_build_avoid(g, fog);

    int hx = g->position.x, hy = g->position.y;
    int hlayer = (g->travel_mode == TRAVEL_BOAT) ? DP_BOAT : DP_FOOT;
    int hc = hy * DP_W + hx;
    if (goal == hc) { if (out_dx) *out_dx = 0; if (out_dy) *out_dy = 0; return true; }

    int head = 0, tail = 0;
    dist[hlayer][hc] = 0;
    q[tail++] = (hlayer << 16) | hc;

    int found = -1;   // packed (layer<<16 | cell) of the cell BESIDE the goal
    while (head < tail) {
        int cur = q[head++];
        int layer = cur >> 16, c = cur & 0xFFFF;
        int cx = c % DP_W, cy = c / DP_W;
        for (int k = 0; k < 8; k++) {
            int nx = cx + DP_DX[k], ny = cy + DP_DY[k];
            if (nx < 0 || nx >= DP_W || ny < 0 || ny >= DP_H) continue;
            int nc = ny * DP_W + nx;
            if (goal >= 0 && nc == goal) {
                // A land interactive can only be stepped onto ON FOOT -- from
                // a boat the engine refuses the lunge (towns are the one sea
                // entrance it handles). Water goals are boat-layer arrivals.
                const Tile *gt = MapGetTile(map, nx, ny);
                // Anything the boat may enter (water AND bridges) is a legal
                // boat-layer arrival; land interactives need the foot lunge.
                bool water_goal = gt && adventure_walkable_in_boat(gt) &&
                                  gt->interactive == INTERACT_NONE;
                bool town_goal = gt && gt->interactive == INTERACT_TOWN;
                if (water_goal || town_goal || layer == DP_FOOT) {
                    found = cur;
                    break;
                }
                continue;
            }
            int nl = dp_enter(g, map, fog, layer, nx, ny);
            if (nl < 0 || dist[nl][nc] >= 0) continue;
            dist[nl][nc] = (short)(dist[layer][c] + 1);
            parent[nl][nc] = cur;
            q[tail++] = (nl << 16) | nc;
        }
        if (found >= 0) break;
    }

    if (pf) {
        for (int l = 0; l < 2; l++)
            for (int y = 0; y < DP_H; y++)
                for (int x = 0; x < DP_W; x++)
                    pf->dist[l][y][x] = dist[l][y * DP_W + x];
    }
    if (goal < 0) return true;
    if (found < 0) return false;

    // Walk the parent chain back to the root; the first move off the root is
    // the step to take now. When `found` IS the root (the hero already stands
    // beside the goal), the first move is the lunge onto the goal itself.
    int gx = goal % DP_W, gy = goal / DP_W;
    if ((found & 0xFFFF) == hc && dist[found >> 16][hc] == 0) {
        if (out_dx) *out_dx = gx - hx;
        if (out_dy) *out_dy = gy - hy;
        return true;
    }
    int cur = found;
    while (dist[cur >> 16][cur & 0xFFFF] > 1)
        cur = parent[cur >> 16][cur & 0xFFFF];
    {
        int nx = (cur & 0xFFFF) % DP_W, ny = (cur & 0xFFFF) / DP_W;
        if (out_dx) *out_dx = nx - hx;
        if (out_dy) *out_dy = ny - hy;
    }
    return true;
}

void demo_field_build(const Game *g, const Map *map, const Fog *fog,
                      DemoField *pf) {
    dp_bfs(g, map, fog, -1, pf, NULL, NULL);
}

int demo_field_dist(const DemoField *pf, const Map *map, int x, int y) {
    if (!MapInBounds(map, x, y)) return -1;
    const Tile *t = MapGetTile(map, x, y);
    // A land interactive is entered ON FOOT (the arrival rule dp_bfs
    // enforces; towns excepted -- the engine handles sea entry), so its
    // reachability counts foot-layer neighbors only. Mismatching this made
    // the scan pick "boat-adjacent" goals the walk then refused forever.
    bool foot_only = t && t->interactive != INTERACT_NONE &&
                     t->interactive != INTERACT_TOWN;
    int best = -1;
    for (int l = 0; l < 2; l++) {
        int d = pf->dist[l][y][x];
        if (d >= 0 && (best < 0 || d < best)) best = d;
    }
    for (int k = 0; k < 8; k++) {
        int nx = x + DP_DX[k], ny = y + DP_DY[k];
        if (nx < 0 || nx >= DP_W || ny < 0 || ny >= DP_H) continue;
        for (int l = 0; l < (foot_only ? 1 : 2); l++) {
            int d = pf->dist[l][ny][nx];
            if (d >= 0 && (best < 0 || d + 1 < best)) best = d + 1;
        }
    }
    return best;
}

bool demo_path_step(const Game *g, const Map *map, const Fog *fog,
                    int gx, int gy, int *dx, int *dy) {
    if (!MapInBounds(map, gx, gy)) return false;
    return dp_bfs(g, map, fog, gy * DP_W + gx, NULL, dx, dy);
}

bool demo_frontier_pick(const Game *g, const Map *map, const Fog *fog,
                        const DemoField *pf, int *fx, int *fy) {
    int best = -1, bx = -1, by = -1;
    for (int y = 0; y < map->height && y < DP_H; y++) {
        for (int x = 0; x < map->width && x < DP_W; x++) {
            int d = -1;
            for (int l = 0; l < 2; l++) {
                int dd = pf->dist[l][y][x];
                if (dd >= 0 && (d < 0 || dd < d)) d = dd;
            }
            if (d < 0) continue;
            if (best >= 0 && d >= best) continue;
            if (demo_spot_blocked(g, x, y)) continue;   // death pockets repel
            for (int k = 0; k < 8; k++) {
                int nx = x + DP_DX[k], ny = y + DP_DY[k];
                if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height)
                    continue;
                if (!FogSeen(fog, nx, ny)) { best = d; bx = x; by = y; break; }
            }
        }
    }
    if (best < 0) return false;
    *fx = bx; *fy = by;
    return true;
}
