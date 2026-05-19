// src/ai/ai_nav.c
//
// BFS over a 64x64 grid is cheap (4096 nodes); we don't bother with A*
// or priority queues. The cost is uniform: every step is 1 (deserts get
// a day-budget penalty in step.c but the *graph* distance is still 1).
// If the AI later wants to weight terrain it can swap this for Dijkstra
// — for now the macro strategy just wants "what's the first step".

#include "ai_nav.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "adventure.h"
#include "tile.h"

#define MAP_W MAP_MAX_W
#define MAP_H MAP_MAX_H

AiMoveMode ai_move_mode_for(const Game *g) {
    if (!g) return AI_MOVE_FOOT;
    if (g->character.mount == MOUNT_FLY) return AI_MOVE_FLY;
    if (g->travel_mode == TRAVEL_BOAT)   return AI_MOVE_BOAT;
    return AI_MOVE_FOOT;
}

bool ai_walkable(const Map *m, int x, int y, AiMoveMode mode,
                 bool treat_interact_as_blocker) {
    if (!m) return false;
    const Tile *t = MapGetTile(m, x, y);
    if (!t) return false;
    if (treat_interact_as_blocker && t->interactive != INTERACT_NONE) {
        return false;
    }
    switch (mode) {
        case AI_MOVE_FOOT: return adventure_walkable_on_foot(t);
        case AI_MOVE_BOAT: return adventure_walkable_in_boat(t);
        case AI_MOVE_FLY:  return adventure_walkable_in_flight(t);
    }
    return false;
}

// ---- BFS ------------------------------------------------------------------

// One 16-bit per tile is plenty for a 64x64 map. We store the previous-
// tile direction so we can walk the parent chain back to the start.
// 0xFF = unvisited.
//
// Direction indices: 0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE
static const int s_dx[8] = {  1,  1,  0, -1, -1, -1,  0,  1 };
static const int s_dy[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };

AiStep ai_path_step(const Map *m, AiMoveMode mode,
                    int sx, int sy, int gx, int gy,
                    bool avoid_interact) {
    AiStep out = { 0, 0, 0, false };
    if (!m) return out;
    if (sx == gx && sy == gy) { out.ok = true; return out; }

    static uint8_t  came_from[MAP_H][MAP_W];   // direction we entered from
    static uint16_t dist[MAP_H][MAP_W];
    memset(came_from, 0xFF, sizeof came_from);
    memset(dist,      0xFF, sizeof dist);

    // Ring buffer queue. 4096 entries max — easily fits.
    static uint16_t qx[MAP_W * MAP_H];
    static uint16_t qy[MAP_W * MAP_H];
    int qh = 0, qt = 0;

    qx[qt] = (uint16_t)sx;
    qy[qt] = (uint16_t)sy;
    qt++;
    dist[sy][sx] = 0;

    bool found = false;

    while (qh != qt) {
        int x = qx[qh];
        int y = qy[qh];
        qh++;
        if (x == gx && y == gy) { found = true; break; }
        for (int i = 0; i < 8; i++) {
            int nx = x + s_dx[i];
            int ny = y + s_dy[i];
            if (nx < 0 || ny < 0 || nx >= m->width || ny >= m->height) continue;
            if (dist[ny][nx] != 0xFFFF) continue;

            // The goal tile is always reachable if the BFS reaches its
            // neighbor — even if avoid_interact would otherwise block it.
            bool is_goal = (nx == gx && ny == gy);
            bool block_interact = avoid_interact && !is_goal;
            if (!ai_walkable(m, nx, ny, mode, block_interact)) continue;

            // Diagonal: require both orthogonal neighbors walkable,
            // matching engine/step.c diagonal-corner rule. Goal exception
            // does not extend to the corners — they must be actually
            // passable.
            if (s_dx[i] != 0 && s_dy[i] != 0) {
                if (!ai_walkable(m, x + s_dx[i], y, mode, block_interact)) continue;
                if (!ai_walkable(m, x, y + s_dy[i], mode, block_interact)) continue;
            }

            dist[ny][nx]      = (uint16_t)(dist[y][x] + 1);
            came_from[ny][nx] = (uint8_t)i;
            qx[qt] = (uint16_t)nx;
            qy[qt] = (uint16_t)ny;
            qt++;
        }
    }

    if (!found) return out;

    // Walk back from goal to start, recording the last direction we used.
    int cx = gx, cy = gy;
    int first_dir = -1;
    while (!(cx == sx && cy == sy)) {
        int d = came_from[cy][cx];
        if (d == 0xFF) return out;                // corrupt — give up
        first_dir = d;
        cx -= s_dx[d];
        cy -= s_dy[d];
    }
    if (first_dir < 0) return out;

    out.dx   = s_dx[first_dir];
    out.dy   = s_dy[first_dir];
    out.dist = dist[gy][gx];
    out.ok   = true;
    return out;
}
