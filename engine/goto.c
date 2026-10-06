// engine/goto.c -- Goto (#70): the hero's route to a tile the player picked.
//
// Dijkstra over four layers of the province's cells: on foot, in the boat,
// in flight, and on foot after a landing. Only seen tiles are used (unexplored
// ground is a wall); an object other than a sign is a wall unless it is the
// target; the only way onto the water is the boat parked in this province (the
// boarding rule of engine/step.c), boarded once; land beside the water is a
// landing. A step costs 1, a desert step the rest of a day.

#include "goto.h"
#include "adventure.h"
#include "tile.h"

#include <stdlib.h>
#include <string.h>

// On foot before sailing, in the boat, flying, and on foot after a landing.
// The boat stays where the hero left it, so a route that has landed cannot
// plan to board again: L_LANDED never boards.
enum { L_FOOT, L_BOAT, L_FLY, L_LANDED, L_COUNT };

static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

typedef struct { int cost, node; } HeapItem;
typedef struct { HeapItem *a; int n, cap; } Heap;

static bool heap_push(Heap *h, int cost, int node) {
    if (h->n == h->cap) {
        int cap = h->cap ? h->cap * 2 : 256;
        HeapItem *a = realloc(h->a, (size_t)cap * sizeof *a);
        if (!a) return false;
        h->a = a; h->cap = cap;
    }
    int i = h->n++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->a[p].cost <= cost) break;
        h->a[i] = h->a[p];
        i = p;
    }
    h->a[i] = (HeapItem){ cost, node };
    return true;
}

static HeapItem heap_pop(Heap *h) {
    HeapItem top = h->a[0], last = h->a[--h->n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && h->a[c + 1].cost < h->a[c].cost) c++;
        if (h->a[c].cost >= last.cost) break;
        h->a[i] = h->a[c];
        i = c;
    }
    if (h->n > 0) h->a[i] = last;
    return top;
}

static bool boat_at(const Game *g, int x, int y) {
    return g->boat.has_boat && g->boat.x == x && g->boat.y == y &&
           (g->boat.zone[0] == '\0' || strcmp(g->boat.zone, g->position.zone) == 0);
}

static bool water(const Tile *t) {
    return t->terrain == TERRAIN_WATER || t->is_bridge;
}

// The layer a step from `layer` onto (x, y) lands in, or -1 when it cannot.
// The target is entered under the engine's own rules; anything else must
// hold no object but a sign, which the hero stands on to read (the walk
// stops at its message).
static int enter(const Game *g, const Tile *t, int layer, int x, int y, bool target) {
    if (layer == L_FLY) return L_FLY;
    if (!target && t->interactive != INTERACT_NONE && t->interactive != INTERACT_SIGN)
        return -1;
    if (layer == L_FOOT || layer == L_LANDED) {
        if (layer == L_FOOT && boat_at(g, x, y)) return L_BOAT;
        return adventure_walkable_on_foot(t) ? layer : -1;
    }
    if (water(t)) return L_BOAT;                         // sailing on
    return adventure_walkable_on_foot(t) ? L_LANDED : -1; // a landing
}

// Days the steps take to finish, counting from the hero's day: a desert step
// on the ground ends the day, banked Time Stop steps cost none.
static int route_days(const Game *g, const Map *m, const Resources *res,
                      const GotoPath *p, bool flying) {
    int day = res && res->time.day_steps > 0 ? res->time.day_steps : 1;
    int left = g->stats.steps_left_today, free_steps = g->stats.time_stop, days = 0;
    int x = g->position.x, y = g->position.y;
    for (int i = 0; i < p->n; i++) {
        x += p->dx[i]; y += p->dy[i];
        if (left <= 0) { days++; left = day; }
        if (free_steps > 0) { free_steps--; continue; }
        const Tile *t = MapGetTile(m, x, y);
        bool full = !flying && t && GameTerrainCostsFullDay(t->terrain);
        left = full ? 0 : left - 1;
    }
    return days;
}

bool GamePlanGoto(const Game *g, const Map *m, const Fog *f, const Resources *res,
                  int tx, int ty, GotoPath *out) {
    if (!g || !m || !f || !out) return false;
    out->n = 0;
    out->days = 0;
    int W = m->width, H = m->height;
    int sx = g->position.x, sy = g->position.y;
    if (tx < 0 || ty < 0 || tx >= W || ty >= H) return false;
    if (tx == sx && ty == sy) return false;
    if (!FogSeen(f, tx, ty)) return false;

    bool flying = g->character.mount == MOUNT_FLY;
    int start = flying ? L_FLY : g->travel_mode == TRAVEL_BOAT ? L_BOAT : L_FOOT;
    int cells = W * H, nodes = cells * L_COUNT;
    int day = res && res->time.day_steps > 0 ? res->time.day_steps : 1;

    int *dist = malloc((size_t)nodes * sizeof *dist);
    int *parent = malloc((size_t)nodes * sizeof *parent);
    Heap h = { 0 };
    bool ok = false;
    if (!dist || !parent) goto done;
    for (int i = 0; i < nodes; i++) { dist[i] = -1; parent[i] = -1; }

    int s = start * cells + sy * W + sx, goal = -1;
    dist[s] = 0;
    if (!heap_push(&h, 0, s)) goto done;
    while (h.n > 0) {
        HeapItem it = heap_pop(&h);
        if (it.cost != dist[it.node]) continue;      // a stale entry
        int layer = it.node / cells, cell = it.node % cells;
        int x = cell % W, y = cell / W;
        if (x == tx && y == ty) { goal = it.node; break; }
        for (int d = 0; d < 8; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H || !FogSeen(f, nx, ny)) continue;
            const Tile *t = MapGetTile(m, nx, ny);
            if (!t) continue;
            int nl = enter(g, t, layer, nx, ny, nx == tx && ny == ty);
            if (nl < 0) continue;
            int step = (nl != L_FLY && GameTerrainCostsFullDay(t->terrain)) ? day : 1;
            int n = nl * cells + ny * W + nx, c = it.cost + step;
            if (dist[n] >= 0 && dist[n] <= c) continue;
            dist[n] = c;
            parent[n] = it.node;
            if (!heap_push(&h, c, n)) goto done;
        }
    }
    if (goal < 0) goto done;

    int len = 0;
    for (int n = goal; n != s; n = parent[n]) len++;
    if (len <= 0 || len > GOTO_MAX) goto done;
    out->n = len;
    for (int n = goal, i = len - 1; n != s; n = parent[n], i--) {
        int c = n % cells, pc = parent[n] % cells;
        out->dx[i] = (signed char)(c % W - pc % W);
        out->dy[i] = (signed char)(c / W - pc / W);
    }
    out->days = route_days(g, m, res, out, flying);
    ok = true;
done:
    free(h.a);
    free(dist);
    free(parent);
    return ok;
}
