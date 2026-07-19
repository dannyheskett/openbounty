// autoplay/exec_move.c
//
// THE SINGLE MOVEMENT FUNCTION (AP-090..AP-093, AP-181, AP-182). All transit --
// walk, boat, flight, gate teleport, bridge, telecave -- routes through
// move_to. The A* kernel (nav5_run) is a layered (x, y, mode) search with a
// binary heap, folded into this file. Gates and telecaves are priced as
// zero-calendar-day legs; bridges as a charge-gated water crossing; a zone
// crossing costs the engine's week quantum and is always scheduled behind
// in-zone work via the MV_XZONE_COST sentinel.

#include "exec.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "adventure.h"
#include "diag.h"
#include "exec_ledger.h"
#include "pending.h"
#include "recording.h"
#include "spells_adventure.h"
#include "step.h"
#include "tables.h"
#include "tile.h"

// ---- node space -------------------------------------------------------------
// Modes: 0 = on foot, 1 = in boat, 2 = flying. Flight is legal only when the
// whole army flies (GamePlayerCanFly); interactive tiles do not fire in it.
#define NAV_MODES 3
#define NAV_W MAP_MAX_W
#define NAV_H MAP_MAX_H
#define NAV_NODES (NAV_MODES * NAV_W * NAV_H)
#define NAV_INF   0x3FFFFFFF

// Per-step cost units. A desert entry exhausts the day (REQ-191), so it is
// priced at the pack's full day of steps.
static int step_cost(const ExecCtx *ctx, const Tile *t) {
    if (GameTerrainCostsFullDay(t->terrain)) {
        int d = ctx->res->time.day_steps;
        return d > 1 ? d : 1;
    }
    return 1;
}

static int nid(int m, int x, int y) { return (m * NAV_H + y) * NAV_W + x; }

static int  s_dist[NAV_NODES];
static int  s_from[NAV_NODES];
static int  s_bridges[NAV_NODES];   // bridge casts consumed along the path

// Lazy-deletion binary heap over (cost, node).
typedef struct { int cost, node; } HeapEntry;
static HeapEntry s_heap[NAV_NODES * 2];
static int s_heap_n;

static void heap_push(int cost, int node) {
    if (s_heap_n >= (int)(sizeof s_heap / sizeof s_heap[0])) {
        watchdog_hit("nav-heap");   // silent drop would mislabel reachability
        return;
    }
    int i = s_heap_n++;
    s_heap[i].cost = cost;
    s_heap[i].node = node;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (s_heap[p].cost <= s_heap[i].cost) break;
        HeapEntry tmp = s_heap[p];
        s_heap[p] = s_heap[i];
        s_heap[i] = tmp;
        i = p;
    }
}

static bool heap_pop(int *cost, int *node) {
    while (s_heap_n > 0) {
        int c = s_heap[0].cost, n = s_heap[0].node;
        s_heap_n--;
        s_heap[0] = s_heap[s_heap_n];
        int i = 0;
        for (;;) {
            int l = 2 * i + 1, r = 2 * i + 2, sm = i;
            if (l < s_heap_n && s_heap[l].cost < s_heap[sm].cost) sm = l;
            if (r < s_heap_n && s_heap[r].cost < s_heap[sm].cost) sm = r;
            if (sm == i) break;
            HeapEntry tmp = s_heap[sm];
            s_heap[sm] = s_heap[i];
            s_heap[i] = tmp;
            i = sm;
        }
        if (c > s_dist[n]) continue;   // lazy deletion
        *cost = c;
        *node = n;
        return true;
    }
    return false;
}

// ---- tile predicates ---------------------------------------------------------

// Interactive tiles the search may cross freely (non-bouncing, non-consuming).
static bool interact_transparent(Interact i) {
    return i == INTERACT_NONE || i == INTERACT_SIGN;
}

// A goal tile is enterable regardless of its interactive (that is the point).
static bool s_goal[NAV_H][NAV_W];
// Tiles this move_to call must avoid (foes whose fight was declined).
static bool s_avoid[NAV_H][NAV_W];

// A hostile foe's tile is not a wall: a player steps onto it, fights, and
// passes (foes chase the hero, so they routinely seal choke points). The
// edge is priced as one full day of steps (res->time.day_steps -- the same
// quantum as the full-day terrain rule); a declined fight adds the tile to
// the avoid set for the rest of the call.

static bool tile_is_foe(const Tile *t) {
    return t && t->interactive == INTERACT_FOE;
}

// The enterable predicates take the already-fetched tile (the nav5 hot loop has
// it in hand): re-fetching it via MapGetTile per neighbor was a top wall-clock
// cost. x/y are still needed for the per-call goal/avoid masks.
static bool foot_enterable(const Tile *t, int x, int y) {
    if (!t) return false;
    if (s_avoid[y][x]) return false;
    if (s_goal[y][x]) return true;
    if (tile_is_foe(t)) return adventure_walkable_on_foot(t);
    if (!interact_transparent(t->interactive)) return false;
    return adventure_walkable_on_foot(t);
}

static bool boat_enterable(const Tile *t, int x, int y) {
    if (!t) return false;
    if (!interact_transparent(t->interactive) && !s_goal[y][x]) return false;
    return adventure_walkable_in_boat(t) &&
           (t->terrain == TERRAIN_WATER || t->is_bridge);
}

static bool fly_enterable(const Tile *t) {
    if (!t) return false;
    return adventure_walkable_in_flight(t);
}

// The parked boat, when boardable in this zone.
static bool boat_here(const ExecCtx *ctx, int *bx, int *by) {
    const Game *g = ctx->g;
    if (!g->boat.has_boat) return false;
    if (g->boat.zone[0] &&
        strcmp(g->boat.zone, g->position.zone) != 0) return false;
    if (bx) *bx = g->boat.x;
    if (by) *by = g->boat.y;
    return g->boat.x >= 0 && g->boat.y >= 0;
}

// Telecave pair lookup: the destination tile of the paired cave, or false.
static bool telecave_pair(const ExecCtx *ctx, int x, int y, int *px, int *py) {
    const Game *g = ctx->g;
    int seq = 0, self_seq = -1;
    for (int i = 0; i < g->placement_count; i++) {
        const SaltedPlacement *p = &g->placements[i];
        if (strcmp(p->zone, g->position.zone) != 0) continue;
        if (p->kind != INTERACT_TELECAVE) continue;
        if (p->x == x && p->y == y) self_seq = seq;
        seq++;
    }
    if (self_seq < 0) return false;
    int want = (self_seq % 2 == 0) ? self_seq + 1 : self_seq - 1;
    seq = 0;
    for (int i = 0; i < g->placement_count; i++) {
        const SaltedPlacement *p = &g->placements[i];
        if (strcmp(p->zone, g->position.zone) != 0) continue;
        if (p->kind != INTERACT_TELECAVE) continue;
        if (seq == want) {
            if (px) *px = p->x;
            if (py) *py = p->y;
            return true;
        }
        seq++;
    }
    return false;
}

// ---- the A* kernel -----------------------------------------------------------
// One relaxation pass from the hero's live (x, y, mode) over the whole layered
// grid. Fills s_dist/s_from/s_bridges. `bridges_avail` gates the water-crossing
// edges (AP-181); flight layer opens only when the army can fly; `allow_rent`
// opens boat-boarding edges at every same-zone town dock the wallet can rent
// at (the drive performs the actual town visit + rent, AP-093).

// Approximate town-visit overhead of a rent-dock boarding, in steps. A
// named heuristic (no honest pack identity exists for it); the drive
// performs the real visit + rent, so the constant only ranks paths.
#define RENT_BOARD_COST 6
// A flight landing consumes the step onto the tile plus the land action.
#define NAV_LANDING_COST 2

static int  s_rent_n;
static int  s_rent_x[GAME_TOWNS], s_rent_y[GAME_TOWNS];
static int  s_rent_town[GAME_TOWNS];           // res->towns index per dock
static bool s_rent_disabled[GAME_TOWNS];        // per-move_to-call failures
static int  s_pending_rent = -1;                // dock hit by the drive

static void collect_rent_docks(const ExecCtx *ctx) {
    const Game *g = ctx->g;
    s_rent_n = 0;
    if (g->stats.gold <= GameBoatCost(g)) return;
    int zi = hero_zone_index(ctx);
    for (int i = 0; i < ctx->res->town_count && s_rent_n < GAME_TOWNS; i++) {
        const ResTown *t = &ctx->res->towns[i];
        if (zone_index_of(ctx->res, t->zone) != zi) continue;
        if (t->boat_x < 0 || t->boat_y < 0) continue;
        if (s_rent_disabled[i]) continue;
        s_rent_x[s_rent_n] = t->boat_x;
        s_rent_y[s_rent_n] = t->boat_y;
        s_rent_town[s_rent_n] = i;
        s_rent_n++;
    }
}

static int rent_dock_at(int x, int y) {
    for (int i = 0; i < s_rent_n; i++)
        if (s_rent_x[i] == x && s_rent_y[i] == y) return i;
    return -1;
}

static bool is_rent_dock(int x, int y) { return rent_dock_at(x, y) >= 0; }

// Fingerprint of everything one relaxation depends on: the seed and map (so
// runs/zones never collide), the recorder mark (every engine mutation is
// recorded, AP-021, so any world/map change moves it -- and a rollback restores
// it in lockstep with the world), the transit-mode params, and the goal/avoid
// masks. A single-target move re-runs the identical relaxation up to three times
// per leg (price, gate-leg check, drive) with nothing mutating between, so an
// immediately-repeated identical call can reuse the filled arrays.
static unsigned long long nav5_fp(const ExecCtx *ctx, int bridges_avail,
                                  bool can_fly, bool allow_rent) {
    unsigned long long h = 1469598103934665603ULL;
    const unsigned char *b;
    size_t i;
#define NAV5_MIX(P, N) do { b = (const unsigned char *)(P); \
    for (i = 0; i < (size_t)(N); i++) { h ^= b[i]; h *= 1099511628211ULL; } \
    } while (0)
    unsigned long long seed = ctx->g->seed;  NAV5_MIX(&seed, sizeof seed);
    const void *mp = ctx->map;               NAV5_MIX(&mp, sizeof mp);
    int mk = recsink_mark();                 NAV5_MIX(&mk, sizeof mk);
    int bv = bridges_avail;                  NAV5_MIX(&bv, sizeof bv);
    int cf = can_fly ? 1 : 0;                NAV5_MIX(&cf, sizeof cf);
    int ar = allow_rent ? 1 : 0;             NAV5_MIX(&ar, sizeof ar);
    NAV5_MIX(s_goal, sizeof s_goal);
    NAV5_MIX(s_avoid, sizeof s_avoid);
#undef NAV5_MIX
    return h;
}

// nav5 memo (perf): the previous fingerprint + its result (still held in
// s_dist/s_from/s_bridges/s_rent). Byte-identical inputs reuse it. File scope so
// the search can invalidate it on a cross-play snapshot jump: within one line
// of play the fingerprint's recording mark is monotonic and unique, but a
// DIFFERENT line reuses mark values, so the memo must be cleared whenever a
// foreign state is restored (search.c node_restore calls
// nav_cache_invalidate). A same-play rollback keeps the memo -- the mark still
// uniquely identifies the state.
static unsigned long long s_nav_last_fp;
static bool               s_nav_have_last = false;
void nav_cache_invalidate(void) { s_nav_have_last = false; }

static void nav5_run(const ExecCtx *ctx, int bridges_avail, bool can_fly,
                     bool allow_rent) {
    unsigned long long fp = nav5_fp(ctx, bridges_avail, can_fly, allow_rent);
    if (s_nav_have_last && fp == s_nav_last_fp) return;
    s_nav_last_fp = fp;
    s_nav_have_last = true;
    const Game *g = ctx->g;
    for (int i = 0; i < NAV_NODES; i++) {
        s_dist[i] = NAV_INF;
        s_from[i] = -1;
        s_bridges[i] = 0;
    }
    s_heap_n = 0;
    if (allow_rent) collect_rent_docks(ctx);
    else s_rent_n = 0;

    int m0 = (g->travel_mode == TRAVEL_BOAT) ? 1 : 0;
    if (g->character.mount == MOUNT_FLY) m0 = 2;
    int start = nid(m0, g->position.x, g->position.y);
    s_dist[start] = 0;
    heap_push(0, start);

    int bx = -1, by = -1;
    bool have_boat = boat_here(ctx, &bx, &by);

    int cost, node;
    while (heap_pop(&cost, &node)) {
        int m = node / (NAV_W * NAV_H);
        int rem = node % (NAV_W * NAV_H);
        int y = rem / NAV_W, x = rem % NAV_W;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= ctx->map->width ||
                    ny >= ctx->map->height) continue;
                // Bounds already checked above, so index the tile grid
                // directly -- MapGetTile's redundant bounds check was hot.
                const Tile *nt = &ctx->map->tiles[ny][nx];

                // Board the parked boat: a foot step onto the boat tile.
                if (m == 0 && have_boat && nx == bx && ny == by) {
                    int to = nid(1, nx, ny);
                    int c2 = cost + 1;
                    if (c2 < s_dist[to]) {
                        s_dist[to] = c2;
                        s_from[to] = node;
                        s_bridges[to] = s_bridges[node];
                        heap_push(c2, to);
                    }
                    continue;
                }
                // Rent + board at a town dock (AP-093): the drive performs the
                // town visit + rent; the edge prices the overhead.
                if (m == 0 && is_rent_dock(nx, ny) &&
                    !(have_boat && nx == bx && ny == by)) {
                    int to = nid(1, nx, ny);
                    int c2 = cost + RENT_BOARD_COST;
                    if (c2 < s_dist[to]) {
                        s_dist[to] = c2;
                        s_from[to] = node;
                        s_bridges[to] = s_bridges[node];
                        heap_push(c2, to);
                    }
                    continue;
                }

                if (m == 0 && foot_enterable(nt, nx, ny)) {
                    int to = nid(0, nx, ny);
                    int c2 = cost + step_cost(ctx, nt) +
                             (tile_is_foe(nt) && !s_goal[ny][nx]
                                  ? ctx->res->time.day_steps : 0);
                    if (c2 < s_dist[to]) {
                        s_dist[to] = c2;
                        s_from[to] = node;
                        s_bridges[to] = s_bridges[node];
                        heap_push(c2, to);
                    }
                    // Telecave hop: stepping onto the cave delivers the pair
                    // (AP-182) -- priced as the one step.
                    if (nt->interactive == INTERACT_TELECAVE) {
                        int px, py;
                        if (telecave_pair(ctx, nx, ny, &px, &py)) {
                            int tp = nid(0, px, py);
                            if (c2 < s_dist[tp]) {
                                s_dist[tp] = c2;
                                s_from[tp] = node;
                                s_bridges[tp] = s_bridges[node];
                                heap_push(c2, tp);
                            }
                        }
                    }
                } else if (m == 0 && nt->terrain == TERRAIN_WATER &&
                           !nt->is_bridge &&
                           s_bridges[node] < bridges_avail) {
                    // Bridge leg (AP-181): cross 1..2 straight water tiles onto
                    // land for one charge. Enumerate the run from here.
                    for (int run = 1; run <= 2; run++) {
                        int wx = x + dx * run, wy = y + dy * run;
                        const Tile *wt = MapGetTile(ctx->map, wx, wy);
                        if (!wt) break;
                        if (wt->terrain != TERRAIN_WATER || wt->is_bridge)
                            break;
                        int lx = x + dx * (run + 1), ly = y + dy * (run + 1);
                        const Tile *lt = MapGetTile(ctx->map, lx, ly);
                        if (!lt) break;
                        if (!foot_enterable(lt, lx, ly)) continue;
                        int to = nid(0, lx, ly);
                        int c2 = cost + run + step_cost(ctx, lt);
                        if (c2 < s_dist[to]) {
                            s_dist[to] = c2;
                            s_from[to] = node;
                            s_bridges[to] = s_bridges[node] + 1;
                            heap_push(c2, to);
                        }
                    }
                }

                if (m == 1) {
                    if (boat_enterable(nt, nx, ny)) {
                        int to = nid(1, nx, ny);
                        int c2 = cost + 1;
                        if (c2 < s_dist[to]) {
                            s_dist[to] = c2;
                            s_from[to] = node;
                            s_bridges[to] = s_bridges[node];
                            heap_push(c2, to);
                        }
                    } else if (foot_enterable(nt, nx, ny)) {
                        // Disembark: parks the boat, hero on foot.
                        int to = nid(0, nx, ny);
                        int c2 = cost + step_cost(ctx, nt);
                        if (c2 < s_dist[to]) {
                            s_dist[to] = c2;
                            s_from[to] = node;
                            s_bridges[to] = s_bridges[node];
                            heap_push(c2, to);
                        }
                    }
                }

                if (m == 2 && can_fly && fly_enterable(nt)) {
                    int to = nid(2, nx, ny);
                    int c2 = cost + 1;
                    if (c2 < s_dist[to]) {
                        s_dist[to] = c2;
                        s_from[to] = node;
                        s_bridges[to] = s_bridges[node];
                        heap_push(c2, to);
                    }
                    // Land beside the flight path: FLY -> foot on a landable
                    // tile (grass, no interactive, not blocking).
                    if (GameCanLandAt(ctx->g, ctx->map, nx, ny)) {
                        int to2 = nid(0, nx, ny);
                        int c3 = cost + NAV_LANDING_COST;
                        if (c3 < s_dist[to2]) {
                            s_dist[to2] = c3;
                            s_from[to2] = node;
                            s_bridges[to2] = s_bridges[node];
                            heap_push(c3, to2);
                        }
                    }
                }
            }
        }
        // Take off in place: foot -> flight when the army flies.
        if (m == 0 && can_fly) {
            int to = nid(2, x, y);
            int c2 = cost + 1;
            if (c2 < s_dist[to]) {
                s_dist[to] = c2;
                s_from[to] = node;
                s_bridges[to] = s_bridges[node];
                heap_push(c2, to);
            }
        }
    }
}

// Best arrival cost at (x, y) across modes 0/1 (a flight arrival cannot fire
// an interactive, so bouncer/consumable goals only count grounded arrivals).
static int goal_cost(int x, int y, int *out_mode) {
    int best = NAV_INF, bm = -1;
    for (int m = 0; m < 2; m++) {
        int d = s_dist[nid(m, x, y)];
        if (d < best) { best = d; bm = m; }
    }
    if (out_mode) *out_mode = bm;
    return best;
}

// ---- zone graph ----------------------------------------------------------------

static int zone_hops(const Resources *res, int from, int to) {
    if (from == to) return 0;
    int dist[RES_MAX_ZONES];
    for (int i = 0; i < res->zone_count; i++) dist[i] = -1;
    int q[RES_MAX_ZONES], qh = 0, qt = 0;
    dist[from] = 0;
    q[qt++] = from;
    while (qh < qt) {
        int z = q[qh++];
        for (int n = 0; n < res->zones[z].neighbor_count; n++) {
            int zn = zone_index_of(res, res->zones[z].neighbors[n]);
            if (zn < 0 || dist[zn] >= 0) continue;
            dist[zn] = dist[z] + 1;
            if (zn == to) return dist[zn];
            q[qt++] = zn;
        }
    }
    return dist[to];
}

static int zone_next_hop(const Resources *res, int from, int to) {
    // First neighbor of `from` on a shortest path to `to`.
    int best = -1, best_d = INT_MAX;
    for (int n = 0; n < res->zones[from].neighbor_count; n++) {
        int zn = zone_index_of(res, res->zones[from].neighbors[n]);
        if (zn < 0) continue;
        if (zn == to) return zn;
        int d = zone_hops(res, zn, to);
        if (d >= 0 && d < best_d) { best_d = d; best = zn; }
    }
    return best;
}

// ---- driving ---------------------------------------------------------------------

// Reconstruct the path to `goal_node` into (x,y,mode) triples, forward order.
static int s_path[NAV_NODES];
static int path_build(int goal_node) {
    int n = 0;
    for (int at = goal_node; at >= 0; at = s_from[at]) {
        if (n >= NAV_NODES) return -1;
        s_path[n++] = at;
    }
    // reverse in place
    for (int i = 0; i < n / 2; i++) {
        int t = s_path[i];
        s_path[i] = s_path[n - 1 - i];
        s_path[n - 1 - i] = t;
    }
    return n;
}

// One recorded overworld step. Returns GameStep's result; the move is recorded
// whether it moved or bounced-with-flow (a bounce that raised a flow must
// replay to re-fire it). A plain blocked step is not recorded.
bool exec_recorded_step(ExecCtx *ctx, int dx, int dy);

static void maybe_cast_time_stop(ExecCtx *ctx);

static bool drive_step(ExecCtx *ctx, int dx, int dy) {
    Game *g = ctx->g;
    maybe_cast_time_stop(ctx);
    uint32_t fp_flow_before = (uint32_t)pending_flow;
    Position pre = g->position;       // markers before the step (see below)
    int step_mark = recsink_mark();   // pre-push, so un-record is exact even
                                      // if the push itself was dropped at cap
    rec_push_move(g, dx, dy);
    bool ok = GameStep(g, ctx->map, ctx->fog, ctx->res, dx, dy);
    if (!ok && pending_flow == (PendingFlow)fp_flow_before &&
        g->position.x == pre.x && g->position.y == pre.y &&
        player_io_idle(g) &&
        strcmp(pre.in_town, g->position.in_town) == 0 &&
        strcmp(pre.home_castle, g->position.home_castle) == 0 &&
        strcmp(pre.own_castle, g->position.own_castle) == 0 &&
        strcmp(pre.dwelling_troop, g->position.dwelling_troop) == 0 &&
        pre.dwelling_x == g->position.dwelling_x &&
        pre.dwelling_y == g->position.dwelling_y) {
        // Nothing happened: un-record the no-op bump. "Nothing" must include
        // the transient location markers: a bounced audience / own-castle
        // entry moves nothing, raises no flow, and queues no message, yet it
        // sets home_castle / own_castle -- the very context the recruit or
        // garrison action recorded right after is gated on (GameBuyTroop,
        // GameGarrisonTroop). Un-recording such an entry made the replay skip
        // it, so the following action prim silently no-oped on a matching
        // fingerprint. The same test also keeps a bump that CLEARED a marker,
        // so the replay clears it identically.
        recsink_rollback(step_mark);
    }
    return ok;
}

bool exec_recorded_step(ExecCtx *ctx, int dx, int dy) {
    return drive_step(ctx, dx, dy);
}

// Convert gold into calendar (the Time Stop economy): with a charge held and
// no stop window live, cast before stepping so the step lands inside the
// window (GameOnStep spends time_stop instead of the day budget).
static void maybe_cast_time_stop(ExecCtx *ctx) {
    Game *g = ctx->g;
    if (!g->stats.knows_magic) return;             // play-legality (REQ-323)
    if (g->stats.time_stop > 0) return;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_TIME_STOP);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    // Record the spell's catalog id (replay resolves actions by id; the
    // engine binds adventure effects to these ids in tables.c).
    const SpellDef *sd = spell_by_index(idx);
    rec_push_action(g, RA_CAST_ADV_SPELL, sd->id, 0, 0);
    GameCastTimeStop(g);
    if (ob_diag_verbose())
        printf("[NAV] time-stop cast: window=%d charges-left=%d day=%d\n",
               g->stats.time_stop, g->spells.counts[idx],
               g->stats.days_left);
}

// Cast bridge across the water run starting one tile ahead in (dx, dy).
static bool drive_bridge(ExecCtx *ctx, int dx, int dy) {
    if (!ctx->g->stats.knows_magic) return false;   // play-legality (REQ-323)
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_BRIDGE);
    if (idx < 0 || ctx->g->spells.counts[idx] <= 0) return false;
    const SpellDef *sd = spell_by_index(idx);
    if (!sd) return false;
    // Record BEFORE the mutation (map tiles + the charge) so the prim's
    // fingerprint is the pre-effect world -- replay checks it as a
    // pre-condition; roll back if the cast fails.
    int mk = recsink_mark();
    rec_push_action(ctx->g, RA_CAST_ADV_SPELL, sd->id, dx, dy);
    int built = try_build_bridge(ctx->g, ctx->map, dx, dy);
    if (built <= 0) { recsink_rollback(mk); return false; }
    ctx->g->spells.counts[idx]--;
    return true;
}

// Mount / land around a flight leg. Recorded so replay reproduces the mode.
static bool drive_mount_fly(ExecCtx *ctx) {
    // Record BEFORE the mutation so the prim's fingerprint is the pre-effect
    // world (replay checks it as a pre-condition); roll back if it fails.
    int mk = recsink_mark();
    rec_push_action(ctx->g, RA_MOUNT_FLY, NULL, 0, 0);
    if (!GameMountFly(ctx->g)) { recsink_rollback(mk); return false; }
    return true;
}

static bool drive_land(ExecCtx *ctx) {
    int mk = recsink_mark();
    rec_push_action(ctx->g, RA_LAND, NULL, 0, 0);
    if (!GameLandHere(ctx->g, ctx->map)) { recsink_rollback(mk); return false; }
    return true;
}

bool exec_land_here(ExecCtx *ctx) { return drive_land(ctx); }
bool exec_is_flying(const ExecCtx *ctx) {
    return ctx->g->character.mount == MOUNT_FLY;
}

// The satisfied check (AP-091 step 0), reused at every give-up: a
// bounce-arrival (flow fired / at-location marker set) is an arrival.
static bool mv_satisfied(const ExecCtx *ctx, const NavPoint *t) {
    const Game *g = ctx->g;
    if (zone_index_of(ctx->res, g->position.zone) != t->zone_index)
        return false;
    if (g->position.x == t->x && g->position.y == t->y) return true;
    // Adjacent + a live flow or location marker = the bouncer fired.
    int dx = g->position.x - t->x, dy = g->position.y - t->y;
    if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
        if (pending_flow != FLOW_NONE) return true;
        if (g->position.in_town[0] || g->position.home_castle[0] ||
            g->position.own_castle[0] || g->position.dwelling_troop[0])
            return true;
    }
    return false;
}

// Drive one in-zone path to (tx, ty). Returns 1 arrived, 0 needs re-path,
// -1 unreachable this leg. `allow_rent` opens rent-dock boardings; when the
// drive hits one it sets s_pending_rent and returns 0 so the caller performs
// the town visit + rent, then re-paths onto the now-real boat.
static int drive_leg_ex(ExecCtx *ctx, const NavPoint *t, bool allow_rent) {
    Game *g = ctx->g;
    bool can_fly = GamePlayerCanFly(g) && GameArmyStackCount(g) > 0;
    int bridge_idx = spell_index_by_adventure_effect(ADV_EFFECT_BRIDGE);
    int bridges = (bridge_idx >= 0) ? g->spells.counts[bridge_idx] : 0;

    memset(s_goal, 0, sizeof s_goal);
    s_goal[t->y][t->x] = true;
    nav5_run(ctx, bridges, can_fly, allow_rent);
    int gm = -1;
    int cost = goal_cost(t->x, t->y, &gm);
    if (cost >= NAV_INF) return -1;

    int n = path_build(nid(gm, t->x, t->y));
    if (n <= 1) return 1;

    for (int i = 1; i < n; i++) {
        int pm = s_path[i - 1] / (NAV_W * NAV_H);
        int m = s_path[i] / (NAV_W * NAV_H);
        int rem = s_path[i] % (NAV_W * NAV_H);
        int ny = rem / NAV_W, nx = rem % NAV_W;
        int prem = s_path[i - 1] % (NAV_W * NAV_H);
        int py = prem / NAV_W, px = prem % NAV_W;

        // A rent-dock boarding: the boat is not really there yet -- hand the
        // rent back to the caller (AP-093).
        if (pm == 0 && m == 1) {
            int bx2, by2;
            bool real = boat_here(ctx, &bx2, &by2) && bx2 == nx && by2 == ny;
            if (!real) {
                int dock = rent_dock_at(nx, ny);
                if (dock >= 0) {
                    s_pending_rent = dock;
                    return 0;
                }
            }
        }

        if (pm != 2 && m == 2) {
            if (!drive_mount_fly(ctx)) return 0;
            if (nx == px && ny == py) continue;   // takeoff in place
        }
        if (pm == 2 && m != 2) {
            // Land: hero must be over the landing tile already (the A* landing
            // edge is from the adjacent flight node; step there first).
            if (g->position.x != nx || g->position.y != ny) {
                int dx = nx - g->position.x, dy = ny - g->position.y;
                if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return 0;
                if (!drive_step(ctx, dx, dy)) return 0;
            }
            if (!drive_land(ctx)) return 0;
            continue;
        }

        int dx = nx - g->position.x, dy = ny - g->position.y;
        if (dx == 0 && dy == 0) continue;   // telecave delivered us here
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
            // Non-adjacent jump in the plan: a telecave/bridge edge whose
            // entry tile is the PREVIOUS node. Handle bridge runs explicitly.
            int sdx = (dx > 0) - (dx < 0), sdy = (dy > 0) - (dy < 0);
            const Tile *ahead = MapGetTile(ctx->map,
                                           g->position.x + sdx,
                                           g->position.y + sdy);
            if (ahead && ahead->terrain == TERRAIN_WATER && !ahead->is_bridge) {
                if (!drive_bridge(ctx, sdx, sdy)) return 0;
            }
            // Walk the straight run to the node.
            while (g->position.x != nx || g->position.y != ny) {
                if (!drive_step(ctx, sdx, sdy)) return 0;
                if (pending_flow != FLOW_NONE) return 0;   // interrupted
            }
            continue;
        }

        // Bridge edge directly ahead?
        const Tile *nt = MapGetTile(ctx->map, nx, ny);
        if (pm == 0 && m == 0 && nt && nt->terrain == TERRAIN_WATER &&
            !nt->is_bridge) {
            if (!drive_bridge(ctx, dx, dy)) return 0;
        }

        bool moved = drive_step(ctx, dx, dy);
        if (mv_satisfied(ctx, t)) return 1;
        if (!moved) return 0;               // blocked or bounced: re-plan
        if (pending_flow != FLOW_NONE) return 0;   // collision flow: caller pumps
    }
    return mv_satisfied(ctx, t) ? 1 : 0;
}

static int drive_leg(ExecCtx *ctx, const NavPoint *t) {
    return drive_leg_ex(ctx, t, false);
}

// Perform the rent the drive handed back: visit the dock's town (a bounced
// step onto its tile sets the at-town marker), rent, record. On failure the
// dock is disabled for the rest of this move_to call.
static bool perform_pending_rent(ExecCtx *ctx) {
    int dock = s_pending_rent;
    s_pending_rent = -1;
    if (dock < 0 || dock >= s_rent_n) return false;
    int ti = s_rent_town[dock];
    const ResTown *town = &ctx->res->towns[ti];
    Game *g = ctx->g;
    NavPoint tp = { hero_zone_index(ctx), town->x, town->y };
    for (int leg = 0; leg < NAV_MAX_LEGS; leg++) {
        int r = drive_leg_ex(ctx, &tp, false);
        if (r == 1) break;
        if (pending_flow != FLOW_NONE) {
            if (!exec_answer_pending(ctx, true)) break;
            continue;
        }
        if (r < 0) break;
    }
    if (!g->position.in_town[0] ||
        strcmp(g->position.in_town, town->id) != 0) {
        if (ob_diag_verbose())
            printf("[NAV] rent failed: could not reach town %s\n", town->id);
        s_rent_disabled[ti] = true;
        return false;
    }
    exec_pump_passive(ctx);
    int rent_mk = recsink_mark();
    rec_push_action(g, RA_RENT_BOAT, g->position.zone,
                    town->boat_x, town->boat_y);
    if (GameRentBoat(g, town->boat_x, town->boat_y,
                     g->position.zone) != BOAT_RENT_OK) {
        recsink_rollback(rent_mk);
        if (ob_diag_verbose())
            printf("[NAV] rent refused at %s (gold=%d)\n", town->id,
                   g->stats.gold);
        s_rent_disabled[ti] = true;
        return false;
    }
    if (ob_diag_verbose())
        printf("[NAV] rented boat at %s dock=(%d,%d)\n", town->id,
               town->boat_x, town->boat_y);
    return true;
}

// ---- crossings ---------------------------------------------------------------

// Rent a boat at the nearest same-zone town dock (AP-093). Wait-gated on gold.
static bool nav_rent_boat(ExecCtx *ctx, ExecCause *out_cause) {
    Game *g = ctx->g;
    const Resources *res = ctx->res;
    int zi = hero_zone_index(ctx);
    int best = -1, best_cost = NAV_INF;
    memset(s_goal, 0, sizeof s_goal);
    // One relaxation, all town tiles as goals.
    for (int i = 0; i < res->town_count; i++) {
        const ResTown *t = &res->towns[i];
        if (zone_index_of(res, t->zone) != zi) continue;
        if (t->boat_x < 0 || t->boat_y < 0) continue;
        s_goal[t->y][t->x] = true;
    }
    nav5_run(ctx, 0, false, false);
    for (int i = 0; i < res->town_count; i++) {
        const ResTown *t = &res->towns[i];
        if (zone_index_of(res, t->zone) != zi) continue;
        if (t->boat_x < 0 || t->boat_y < 0) continue;
        int c = goal_cost(t->x, t->y, NULL);
        if (c < best_cost) { best_cost = c; best = i; }
    }
    if (best < 0) {
        if (out_cause) *out_cause = EXEC_CAUSE_REACH;
        return false;
    }
    const ResTown *town = &res->towns[best];
    int fare = GameBoatCost(g);
    if (g->stats.gold <= fare) {
        // Wait on the STANDING army's income only (no-trim): dismissing a
        // realized fight army to afford a boat fare is how a committed
        // winner arrives at the gate as one skeleton. A bleeding army's
        // negative net fails this fast -- the correct answer mid-move.
        ExecCause gc = EXEC_CAUSE_NONE;
        if (!exec_ensure_gold_no_trim(ctx, fare + 1, &gc)) {
            if (ob_diag_verbose())
                printf("[NAV] rent refused: gold=%d fare=%d cause=%s\n",
                       g->stats.gold, fare, exec_cause_name(gc));
            if (out_cause) *out_cause = gc;
            return false;
        }
    }
    NavPoint tp = { zi, town->x, town->y };
    for (int leg = 0; leg < NAV_MAX_LEGS; leg++) {
        int r = drive_leg(ctx, &tp);
        if (r == 1) break;
        if (r < 0) { if (out_cause) *out_cause = EXEC_CAUSE_REACH; return false; }
        if (pending_flow != FLOW_NONE && !exec_answer_pending(ctx, true)) {
            if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
            return false;
        }
    }
    if (!g->position.in_town[0]) {
        if (out_cause) *out_cause = EXEC_CAUSE_REACH;
        return false;
    }
    exec_pump_passive(ctx);
    int rent_mk = recsink_mark();
    rec_push_action(g, RA_RENT_BOAT, res->zones[zi].id,
                    town->boat_x, town->boat_y);
    BoatActionResult br = GameRentBoat(g, town->boat_x, town->boat_y,
                                       res->zones[zi].id);
    if (br != BOAT_RENT_OK) {
        recsink_rollback(rent_mk);
        if (out_cause) *out_cause = EXEC_CAUSE_GOLD;
        return false;
    }
    return true;
}

// Put the hero to sea (AP-093): board the parked boat, renting when none is
// boardable in the zone.
static bool nav_ensure_sailing(ExecCtx *ctx, bool *rerented,
                               ExecCause *out_cause) {
    Game *g = ctx->g;
    if (g->travel_mode == TRAVEL_BOAT) return true;
    int bx, by;
    if (!boat_here(ctx, &bx, &by)) {
        if (*rerented) {
            if (ob_diag_verbose())
                printf("[NAV] sail failed: rent already used this move "
                       "(hero=(%d,%d,%s))\n", g->position.x, g->position.y,
                       g->position.zone);
            if (out_cause) *out_cause = EXEC_CAUSE_REACH;
            return false;
        }
        *rerented = true;   // once per move_to call (AP-103)
        if (!nav_rent_boat(ctx, out_cause)) {
            if (ob_diag_verbose())
                printf("[NAV] sail failed: rent failed cause=%s "
                       "(hero=(%d,%d,%s) gold=%d)\n",
                       out_cause ? exec_cause_name(*out_cause) : "?",
                       g->position.x, g->position.y, g->position.zone,
                       g->stats.gold);
            return false;
        }
        if (!boat_here(ctx, &bx, &by)) {
            if (out_cause) *out_cause = EXEC_CAUSE_REACH;
            return false;
        }
    }
    for (int board = 0; board < 2; board++) {
        NavPoint bp = { hero_zone_index(ctx), bx, by };
        int r = 0;
        for (int leg = 0; leg < NAV_MAX_LEGS; leg++) {
            if (g->travel_mode == TRAVEL_BOAT) return true;
            r = drive_leg(ctx, &bp);
            if (g->travel_mode == TRAVEL_BOAT) return true;
            if (r == 1 || r < 0) break;
            if (pending_flow != FLOW_NONE && !exec_answer_pending(ctx, true)) {
                if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
                return false;
            }
        }
        if (r >= 0) break;
        // The owned boat is parked at an unreachable shore (disembarked into
        // a region and gated away): it must not block boarding -- rent a
        // fresh one at a reachable dock (the rent replaces the parked boat)
        // and board that instead.
        if (ob_diag_verbose())
            printf("[NAV] sail: parked boat at (%d,%d) unreachable "
                   "(hero=(%d,%d,%s)), re-renting\n", bx, by, g->position.x,
                   g->position.y, g->position.zone);
        if (*rerented) {
            if (out_cause) *out_cause = EXEC_CAUSE_REACH;
            return false;
        }
        *rerented = true;
        if (!nav_rent_boat(ctx, out_cause)) {
            if (ob_diag_verbose())
                printf("[NAV] sail failed: re-rent failed cause=%s "
                       "(hero=(%d,%d,%s) gold=%d)\n",
                       out_cause ? exec_cause_name(*out_cause) : "?",
                       g->position.x, g->position.y, g->position.zone,
                       g->stats.gold);
            return false;
        }
        if (!boat_here(ctx, &bx, &by)) {
            if (out_cause) *out_cause = EXEC_CAUSE_REACH;
            return false;
        }
    }
    return g->travel_mode == TRAVEL_BOAT;
}

// Sail one zone hop through the navigate flow (records RA_TRAVEL_ZONE; the
// engine spends the week quantum).
static bool nav_travel_hop(ExecCtx *ctx, int dest_zi, ExecCause *out_cause) {
    Game *g = ctx->g;
    const Resources *res = ctx->res;
    int cur = hero_zone_index(ctx);
    // Fill the navigate scratch exactly as the shell's N prompt would.
    int count = 0;
    int pick = -1;
    for (int n = 0; n < res->zones[cur].neighbor_count && count < 5; n++) {
        int zn = zone_index_of(res, res->zones[cur].neighbors[n]);
        if (zn < 0) continue;
        snprintf(pending_nav_zones[count], sizeof pending_nav_zones[count],
                 "%s", res->zones[zn].id);
        if (zn == dest_zi) pick = count;
        count++;
    }
    if (pick < 0) { if (out_cause) *out_cause = EXEC_CAUSE_REACH; return false; }
    pending_nav_count = count;
    pending_flow = FLOW_NAVIGATE;
    player_io_raise_decision(g, FLOW_NAVIGATE, REQ_PROMPT_NUMERIC, "", "");
    rec_push_action(g, RA_TRAVEL_ZONE, res->zones[dest_zi].id, 0, 0);
    FlowAnswer ans = { (PromptAnswer)(FLOW_ANS_1 + pick), 0 };
    PlayerIoPresentation pres;
    int before_days = g->stats.days_left;
    player_io_answer(g, ctx->map, ctx->fog, res, ans,
                     PLAYER_IO_COMBAT_NOT_RUN, &pres);
    int cross_days = before_days - g->stats.days_left;
    day_acct_add(DAY_ACCT_CROSSING, cross_days);
    if (ob_diag_verbose())
        printf("[LEDGER] crossing %s -> %s days=%d\n",
               res->zones[cur].id, res->zones[dest_zi].id, cross_days);
    exec_pump_passive(ctx);
    return hero_zone_index(ctx) == dest_zi;
}

// Cross to the target zone: a castable gate to a visited destination there is
// a zero-day leg and always beats the sail (AP-092); otherwise sail hop by hop.
static bool s_stocking_gates = false;   // recursion guard: the stocking trip
                                        // itself may cross zones

// Self-maroon guard for a LAST-PAIR gate cast: flood the DESTINATION zone's
// map on foot from the landing. A pocket landing (< 8 nodes) with no castable
// gate left afterward is a trap, not a crossing -- unless the move's own
// target sits inside the pocket (then arriving IS the point; the planner's
// maroon probe judges the aftermath).
static Map s_probe_map;
static int gate_dest_foot_nodes(const ExecCtx *ctx, const GateDestination *d,
                                const NavPoint *final_target,
                                bool *out_target_inside) {
    if (out_target_inside) *out_target_inside = false;
    if (!MapLoadZoneWithPlacements(&s_probe_map, ctx->res, d->zone,
                                   (Game *)ctx->g))
        return 1 << 20;   // unknown geometry: never block on it
    GameApplyTileMutations(ctx->g, &s_probe_map, d->zone);
    static unsigned char seen[NAV_H][NAV_W];
    memset(seen, 0, sizeof seen);
    static int qx[NAV_NODES], qy[NAV_NODES];
    int qh = 0, qt = 0, count = 0;
    if (d->x < 0 || d->y < 0 || d->x >= s_probe_map.width ||
        d->y >= s_probe_map.height)
        return 1 << 20;
    qx[qt] = d->x; qy[qt] = d->y; qt++;
    seen[d->y][d->x] = 1;
    while (qh < qt) {
        int x = qx[qh], y = qy[qh]; qh++;
        count++;
        if (final_target && out_target_inside &&
            x == final_target->x && y == final_target->y)
            *out_target_inside = true;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= s_probe_map.width ||
                    ny >= s_probe_map.height) continue;
                if (seen[ny][nx]) continue;
                const Tile *t = MapGetTile(&s_probe_map, nx, ny);
                if (!t) continue;
                // Foe/consumable overlays are doors in practice; count any
                // foot-walkable tile.
                if (!adventure_walkable_on_foot(t)) continue;
                seen[ny][nx] = 1;
                qx[qt] = nx; qy[qt] = ny; qt++;
                if (qt >= NAV_NODES) return count;   // saturated: huge
            }
    }
    return count;
}

static bool do_crossing_once(ExecCtx *ctx, int dest_zi, bool *rerented,
                             const NavPoint *final_target,
                             ExecCause *out_cause) {
    Game *g = ctx->g;
    const Resources *res = ctx->res;

    // Gate leg: any visited destination in the target zone, castable under R-C.
    GateDestination dests[GAME_GATE_DESTS_MAX];
    for (int town = 0; town < 2; town++) {
        int idx = gate_spell_index(town == 1);
        if (idx < 0) continue;
        // Town gates stay in play at ONE charge: a cast to the seller's own
        // town is legal from one (AP-110's exemption) and lands on a refill.
        // exec_gate_to itself enforces the law per destination.
        int floor2 = (town == 1) ? 1 : GATE_LAW_MIN_CHARGES;
        if (spell_charges(g, idx) < floor2) continue;
        bool last_pair =
            spell_charges(g, idx) - 1 < GATE_LAW_MIN_CHARGES;
        int n = GameGateDestinations(g, town ? GATE_DEST_TOWN : GATE_DEST_CASTLE,
                                     dests, GAME_GATE_DESTS_MAX);
        int in_dest = 0;
        for (int i = 0; i < n; i++) {
            if (zone_index_of(res, dests[i].zone) != dest_zi) continue;
            in_dest++;
            if (last_pair) {
                bool target_inside = false;
                int nodes = gate_dest_foot_nodes(ctx, &dests[i], final_target,
                                                 &target_inside);
                if (nodes < AUTOPLAY_POCKET_NODES && !target_inside) {
                    if (ob_diag_verbose())
                        printf("[NAV] gate dest skipped (self-maroon guard): "
                               "%s (%d,%d) nodes=%d charges=%d\n",
                               dests[i].zone, dests[i].x, dests[i].y, nodes,
                               spell_charges(g, idx));
                    continue;
                }
            }
            if (exec_gate_to(ctx, town == 1, dests[i].zone,
                             dests[i].x, dests[i].y))
                return true;
        }
        if (ob_diag_verbose() && in_dest > 0)
            printf("[NAV] gate crossing refused: %s charges=%d dests=%d "
                   "in-dest=%d magic=%d\n", town ? "town" : "castle",
                   spell_charges(g, idx), n, in_dest,
                   (int)g->stats.knows_magic);
    }
    if (ob_diag_verbose()) {
        int ci = gate_spell_index(false), ti2 = gate_spell_index(true);
        printf("[NAV] crossing falls to sail: dest=z%d castle-charges=%d "
               "town-charges=%d\n", dest_zi,
               ci >= 0 ? spell_charges(g, ci) : -1,
               ti2 >= 0 ? spell_charges(g, ti2) : -1);
    }

    // Sail. Multi-hop across the neighbor graph.
    for (int hop = 0; hop < RES_MAX_ZONES + 1; hop++) {
        int cur = hero_zone_index(ctx);
        if (cur == dest_zi) return true;
        int next = zone_next_hop(res, cur, dest_zi);
        if (next < 0) { if (out_cause) *out_cause = EXEC_CAUSE_REACH; return false; }
        if (!nav_ensure_sailing(ctx, rerented, out_cause)) return false;
        if (!nav_travel_hop(ctx, next, out_cause)) return false;
    }
    if (out_cause) *out_cause = EXEC_CAUSE_REACH;
    return false;
}

static bool do_crossing(ExecCtx *ctx, int dest_zi, bool *rerented,
                        const NavPoint *final_target, ExecCause *out_cause) {
    Game *g = ctx->g;
    if (do_crossing_once(ctx, dest_zi, rerented, final_target, out_cause))
        return true;

    // A crossing that failed both ways (gates refused AND the sail failed --
    // typically a pocket landing with no dock and books burned below the law
    // floor) earns one recovery: restock the gate books at their sellers and
    // retry. A crossing the sail can still make never takes this path. The
    // stocking trip itself crosses zones; the recursion guard keeps it from
    // re-entering.
    if (s_stocking_gates || !g->stats.knows_magic ||
        g->stats.gold <= AUTOPLAY_RICH_WALLET(ctx->res))
        return false;
    bool stocked = false;
    s_stocking_gates = true;
    for (int town = 0; town < 2; town++) {
        int idx = gate_spell_index(town == 1);
        if (idx < 0) continue;
        if (spell_charges(g, idx) >= GATE_LAW_MIN_CHARGES) continue;
        // The forced stocker: an endgame book at cap silently refuses the
        // plain buy -- the whole reason the gates died in the first place.
        if (exec_stock_spell_charges_public(ctx, idx,
                                            AUTOPLAY_GATE_RESTOCK_TARGET))
            stocked = true;
    }
    s_stocking_gates = false;
    if (!stocked) return false;
    if (ob_diag_verbose())
        printf("[NAV] crossing gate restock: castle=%d town=%d gold=%d\n",
               spell_charges(g, gate_spell_index(false)),
               spell_charges(g, gate_spell_index(true)), g->stats.gold);
    // The stocking trip may already have crossed us there.
    if (hero_zone_index(ctx) == dest_zi) return true;
    bool rerented2 = false;
    return do_crossing_once(ctx, dest_zi, &rerented2, final_target, out_cause);
}

// ---- gate legs inside the zone (AP-092) ----------------------------------------

// A castable same-zone gate landing whose walk remainder beats the direct
// route. Uses a reverse relaxation from the target (approximate for the
// asymmetric desert weighting; the drive re-paths exactly).
static bool mv_gate_castable(const ExecCtx *ctx, bool town_gate) {
    if (!ctx->g->stats.knows_magic) return false;   // play-legality (REQ-323)
    int idx = gate_spell_index(town_gate);
    return idx >= 0 && spell_charges(ctx->g, idx) >= GATE_LAW_MIN_CHARGES;
}

static int mv_gate_leg_score(ExecCtx *ctx, const NavPoint *t, int direct_cost,
                             bool *out_town, GateDestination *out_dest) {
    const Resources *res = ctx->res;
    // Distances FROM the target over the current map (reverse approximation).
    memset(s_goal, 0, sizeof s_goal);
    Game save_pos = *ctx->g;
    // Reuse nav5 by relaxing from the target on foot: temporarily reposition.
    ctx->g->position.x = t->x;
    ctx->g->position.y = t->y;
    ctx->g->travel_mode = TRAVEL_WALK;
    nav5_run(ctx, 0, false, false);
    *ctx->g = save_pos;

    int best = direct_cost;
    bool improved = false;
    for (int town = 0; town < 2; town++) {
        bool seller_only = false;
        if (!mv_gate_castable(ctx, town == 1)) {
            // The R-C seller exemption (AP-110): a town gate is castable
            // from ONE charge when the destination is the gate seller's own
            // town -- the leg that revives a burned book from inside a
            // pocket (the landing refill restores the law floor).
            if (town == 1 && ctx->g->stats.knows_magic &&
                gate_spell_index(true) >= 0 &&
                spell_charges(ctx->g, gate_spell_index(true)) == 1)
                seller_only = true;
            else
                continue;
        }
        GateDestination dests[GAME_GATE_DESTS_MAX];
        int n = GameGateDestinations(ctx->g,
                                     town ? GATE_DEST_TOWN : GATE_DEST_CASTLE,
                                     dests, GAME_GATE_DESTS_MAX);
        for (int i = 0; i < n; i++) {
            if (zone_index_of(res, dests[i].zone) != t->zone_index) continue;
            if (dests[i].x < 0 || dests[i].y < 0) continue;
            if (seller_only &&
                !exec_gate_dest_is_seller(ctx, true, dests[i].zone,
                                          dests[i].x, dests[i].y))
                continue;
            int c = s_dist[nid(0, dests[i].x, dests[i].y)];
            if (c < best) {
                best = c;
                improved = true;
                *out_town = (town == 1);
                *out_dest = dests[i];
            }
        }
    }
    return improved ? best : -1;
}

// ---- move_to -----------------------------------------------------------------

// Mobility probe for the stranded rule (AP-051): how many nodes the hero can
// reach right now (fight-through foes included). A near-zero count after a
// successful attempt means the success sealed the hero in. `ax/ay/an` marks
// danger centers (unbeatable hostiles); everything within their follow range
// counts as sealed, so a success parked inside a closing jaw rolls back.
int move_reachable_nodes_avoid(ExecCtx *ctx, const int *ax, const int *ay,
                               int an) {
    memset(s_goal, 0, sizeof s_goal);
    memset(s_avoid, 0, sizeof s_avoid);
    // The danger centers are WALLS here (their tiles only): the normal
    // fight-through edge must not count an unbeatable foe as an escape.
    for (int i = 0; i < an; i++) {
        int x = ax[i], y = ay[i];
        if (x < 0 || y < 0 || x >= NAV_W || y >= NAV_H) continue;
        if (x == ctx->g->position.x && y == ctx->g->position.y) continue;
        s_avoid[y][x] = true;
    }
    nav5_run(ctx, 0, false, true);
    memset(s_avoid, 0, sizeof s_avoid);
    int reached = 0;
    for (int i = 0; i < NAV_NODES; i++)
        if (s_dist[i] < NAV_INF) reached++;
    return reached;
}

// Escape a closing jaw (the stranded rule's second chance): walk out to the
// reachable tile farthest from every danger center, fighting through only
// what the drive already fights through. True when the hero ends >= 6 tiles
// from every center (or there are none).
bool move_escape_jaw(ExecCtx *ctx, const int *ax, const int *ay, int an) {
    Game *g = ctx->g;
    if (an <= 0) return true;
    // Wall-avoid relaxation to find candidate refuges.
    memset(s_goal, 0, sizeof s_goal);
    memset(s_avoid, 0, sizeof s_avoid);
    for (int i = 0; i < an; i++) {
        if (ax[i] == g->position.x && ay[i] == g->position.y) continue;
        if (ax[i] >= 0 && ay[i] >= 0 && ax[i] < NAV_W && ay[i] < NAV_H)
            s_avoid[ay[i]][ax[i]] = true;
    }
    nav5_run(ctx, 0, false, true);
    memset(s_avoid, 0, sizeof s_avoid);
    int best_x = -1, best_y = -1;
    long best_score = -1;
    for (int y = 0; y < NAV_H; y++) {
        for (int x = 0; x < NAV_W; x++) {
            int d0 = s_dist[nid(0, x, y)];
            int d1 = s_dist[nid(1, x, y)];
            int d = d0 < d1 ? d0 : d1;
            // Refuge cap: farther than one week of walking is not an escape.
            int walk_cap = ctx->res->time.week_days * ctx->res->time.day_steps;
            if (d >= NAV_INF || d > walk_cap) continue;
            long mind = 1 << 20;
            for (int i = 0; i < an; i++) {
                long ddx = x - ax[i], ddy = y - ay[i];
                if (ddx < 0) ddx = -ddx;
                if (ddy < 0) ddy = -ddy;
                long cd = ddx > ddy ? ddx : ddy;
                if (cd < mind) mind = cd;
            }
            long score = mind * 1000 - d;   // far from danger, near to walk
            if (score > best_score) {
                best_score = score;
                best_x = x;
                best_y = y;
            }
        }
    }
    if (best_x < 0) return false;
    NavPoint safe = { hero_zone_index(ctx), best_x, best_y };
    ExecCause cc;
    move_to(ctx, &safe, 1, true, NULL, &cc);
    long mind = 1 << 20;
    for (int i = 0; i < an; i++) {
        long ddx = g->position.x - ax[i], ddy = g->position.y - ay[i];
        if (ddx < 0) ddx = -ddx;
        if (ddy < 0) ddy = -ddy;
        long cd = ddx > ddy ? ddx : ddy;
        if (cd < mind) mind = cd;
    }
    return mind >= AUTOPLAY_ESCAPE_CLEAR;
}

void move_price_all(ExecCtx *ctx, const NavPoint *targets, int n,
                    int *out_costs) {
    Game *g = ctx->g;
    int cur_zi = hero_zone_index(ctx);
    bool can_fly = GamePlayerCanFly(g) && GameArmyStackCount(g) > 0;
    int bridge_idx = spell_index_by_adventure_effect(ADV_EFFECT_BRIDGE);
    int bridges = (bridge_idx >= 0) ? g->spells.counts[bridge_idx] : 0;
    memset(s_avoid, 0, sizeof s_avoid);
    memset(s_goal, 0, sizeof s_goal);
    for (int i = 0; i < n; i++) {
        if (targets[i].zone_index == cur_zi)
            s_goal[targets[i].y][targets[i].x] = true;
    }
    nav5_run(ctx, bridges, can_fly, true);
    for (int i = 0; i < n; i++) {
        if (targets[i].zone_index == cur_zi) {
            out_costs[i] = goal_cost(targets[i].x, targets[i].y, NULL);
        } else {
            int hops = zone_hops(ctx->res, cur_zi, targets[i].zone_index);
            out_costs[i] = hops < 0 ? NAV_INF : MV_XZONE_COST + hops;
        }
    }
}

// Match a gate destination back to its TownRecord (landings default to the
// town's gate/boat coords, REQ-322 -- the same matching dest_is_seller uses).
static const TownRecord *town_record_at(const ExecCtx *ctx,
                                        const char *zone, int x, int y) {
    for (int i = 0; i < ctx->res->town_count; i++) {
        const ResTown *t = &ctx->res->towns[i];
        if (strcmp(t->zone, zone) != 0) continue;
        if (!((t->gate_x == x && t->gate_y == y) ||
              (t->boat_x == x && t->boat_y == y) ||
              (t->x == x && t->y == y)))
            continue;
        for (int k = 0; k < GAME_TOWNS; k++)
            if (strcmp(ctx->g->towns[k].id, t->id) == 0)
                return &ctx->g->towns[k];
        return NULL;
    }
    return NULL;
}

// Zero-day stop restock (the AP-092 + AP-110 composition): with the stop
// book dry mid-attempt, a town-gate hop to a visited stop seller and a
// return hop to the gate seller's own town (legal from one charge under the
// seller exemption, refilled to the floor on landing) convert gold into stop
// charges without spending a day or leaving the working zone. Available only
// where the town-gate seller shares the hero's zone -- elsewhere the return
// leg would strand the hero, so the loop stays off.
static bool s_restock_suppressed = false;

void exec_set_gate_restock_suppressed(bool on) { s_restock_suppressed = on; }

static void maybe_gate_restock_stops(ExecCtx *ctx) {
    Game *g = ctx->g;
    if (s_restock_suppressed) return;
    // An endgame instrument: below a deep wallet and a widened book the
    // charges' gold belongs to the army economy.
    if (!g->stats.knows_magic ||
        g->stats.gold < AUTOPLAY_RICH_WALLET(ctx->res)) return;
    {
        BookBudget bb;
        exec_book_budget(g, &bb);
        if (bb.castle_gate_want <= GATE_LAW_MIN_CHARGES) return;
    }
    if (pending_flow != FLOW_NONE) return;
    if (g->stats.time_stop > 0) return;              // window still live
    int ts = spell_index_by_adventure_effect(ADV_EFFECT_TIME_STOP);
    if (ts < 0 || g->spells.counts[ts] > 0) return;  // book not dry
    if (g->stats.max_spells - GameKnownSpells(g) < 1) return;
    int tg = gate_spell_index(true);
    if (tg < 0 || spell_charges(g, tg) < 2) return;
    const SpellDef *ts_sd = spell_by_index(ts);
    const SpellDef *tg_sd = spell_by_index(tg);
    if (!ts_sd || !tg_sd) return;

    GateDestination dests[GAME_GATE_DESTS_MAX];
    int n = GameGateDestinations(g, GATE_DEST_TOWN, dests, GAME_GATE_DESTS_MAX);
    int out = -1, back = -1;
    char out_tid[24] = {0};
    char back_tid[24] = {0};
    for (int i = 0; i < n; i++) {
        const TownRecord *tr = town_record_at(ctx, dests[i].zone,
                                              dests[i].x, dests[i].y);
        if (!tr) continue;
        if (out < 0 && strcmp(tr->spell_for_sale, ts_sd->id) == 0) {
            out = i;
            snprintf(out_tid, sizeof out_tid, "%s", tr->id);
        }
        if (back < 0 && strcmp(tr->spell_for_sale, tg_sd->id) == 0 &&
            strcmp(dests[i].zone, g->position.zone) == 0) {
            back = i;
            snprintf(back_tid, sizeof back_tid, "%s", tr->id);
        }
    }
    if (out < 0 || back < 0) return;
    if (!exec_gate_to(ctx, true, dests[out].zone, dests[out].x,
                      dests[out].y))
        return;
    // The gate lands on the town's landing tile (REQ-322), not in the town:
    // step into it before buying.
    if (!g->position.in_town[0] ||
        strcmp(g->position.in_town, out_tid) != 0) {
        const ResTown *ot = resources_town_by_id(ctx->res, out_tid);
        if (ot) {
            int dx = ot->x > g->position.x ? 1
                   : ot->x < g->position.x ? -1 : 0;
            int dy = ot->y > g->position.y ? 1
                   : ot->y < g->position.y ? -1 : 0;
            if (dx || dy) drive_step(ctx, dx, dy);
            exec_answer_pending(ctx, true);
        }
    }
    // Fill the free book minus the two slots the return landing's floor
    // refill needs -- losing the gate kit would kill the loop for good.
    while (g->stats.max_spells - GameKnownSpells(g) > 2 &&
           exec_buy_spell_at(ctx, out_tid)) {}
    if (exec_gate_to(ctx, true, dests[back].zone, dests[back].x,
                     dests[back].y) &&
        spell_charges(g, tg) < GATE_LAW_MIN_CHARGES) {
        // The landing tile is beside the town (REQ-322): step in so the
        // law's floor refill (AP-111) can actually buy.
        if (!g->position.in_town[0] ||
            strcmp(g->position.in_town, back_tid) != 0) {
            const ResTown *bt = resources_town_by_id(ctx->res, back_tid);
            if (bt) {
                int dx = bt->x > g->position.x ? 1
                       : bt->x < g->position.x ? -1 : 0;
                int dy = bt->y > g->position.y ? 1
                       : bt->y < g->position.y ? -1 : 0;
                if (dx || dy) drive_step(ctx, dx, dy);
                exec_answer_pending(ctx, true);
            }
        }
        while (spell_charges(g, tg) < GATE_LAW_MIN_CHARGES &&
               exec_buy_spell_at(ctx, back_tid)) {}
    }
    if (ob_diag_verbose())
        printf("[NAV] stop restock loop: ts=%d town-gates=%d gold=%d day=%d\n",
               g->spells.counts[ts], spell_charges(g, tg), g->stats.gold,
               g->stats.days_left);
}

static int move_to_once(ExecCtx *ctx, const NavPoint *targets, int n,
                        bool commit, int *out_cost, ExecCause *out_cause);

int move_to(ExecCtx *ctx, const NavPoint *targets, int n, bool commit,
            int *out_cost, ExecCause *out_cause) {
    Game *g = ctx->g;
    int r = move_to_once(ctx, targets, n, commit, out_cost, out_cause);
    if (r >= 0 || !commit) return r;   // queries stay pure (no restock trips)
    if (out_cause && *out_cause != EXEC_CAUSE_REACH) return r;

    // REACH-restock retry: a committing move that failed on REACH with a
    // gate book below the law floor earns one forced restock + one retry --
    // the in-zone hop that would clear a pocket needs castable gates, and a
    // rich endgame hero rebuys the charges rather than staying sealed. The
    // stocker room-forces (a full book is why the plain buy is refused).
    // Books at or above the floor gain nothing from restocking, so those
    // moves and honestly-unreachable targets never take this path.
    if (s_stocking_gates || !g->stats.knows_magic ||
        g->stats.gold <= AUTOPLAY_RICH_WALLET(ctx->res))
        return r;
    int cg = gate_spell_index(false), tg = gate_spell_index(true);
    bool castle_low = cg >= 0 && spell_charges(g, cg) < GATE_LAW_MIN_CHARGES;
    bool town_low = tg >= 0 && spell_charges(g, tg) < GATE_LAW_MIN_CHARGES;
    if (!castle_low && !town_low) return r;
    bool stocked = false;
    s_stocking_gates = true;
    if (town_low &&
        exec_stock_spell_charges_public(ctx, tg, AUTOPLAY_GATE_RESTOCK_TARGET))
        stocked = true;
    if (castle_low &&
        exec_stock_spell_charges_public(ctx, cg, AUTOPLAY_GATE_RESTOCK_TARGET))
        stocked = true;
    s_stocking_gates = false;
    if (!stocked) return r;
    if (ob_diag_verbose())
        printf("[NAV] move gate restock: castle=%d town=%d gold=%d\n",
               cg >= 0 ? spell_charges(g, cg) : -1,
               tg >= 0 ? spell_charges(g, tg) : -1, g->stats.gold);
    return move_to_once(ctx, targets, n, commit, out_cost, out_cause);
}

static int move_to_once(ExecCtx *ctx, const NavPoint *targets, int n,
                        bool commit, int *out_cost, ExecCause *out_cause) {
    Game *g = ctx->g;
    if (out_cause) *out_cause = EXEC_CAUSE_NONE;
    if (!targets || n <= 0) return -1;

    // Fresh per-call state BEFORE pricing: a stale avoid/rent set from the
    // previous call must never seal this one's search.
    memset(s_avoid, 0, sizeof s_avoid);
    memset(s_rent_disabled, 0, sizeof s_rent_disabled);
    s_pending_rent = -1;

    int cur_zi = hero_zone_index(ctx);

    // Step 0: already satisfied?
    for (int i = 0; i < n; i++) {
        if (mv_satisfied(ctx, &targets[i])) {
            if (out_cost) *out_cost = 0;
            return i;
        }
    }

    maybe_gate_restock_stops(ctx);

    // Price every target: one relaxation covers all in-zone goals.
    bool can_fly = GamePlayerCanFly(g) && GameArmyStackCount(g) > 0;
    int bridge_idx = spell_index_by_adventure_effect(ADV_EFFECT_BRIDGE);
    int bridges = (bridge_idx >= 0) ? g->spells.counts[bridge_idx] : 0;
    memset(s_goal, 0, sizeof s_goal);
    for (int i = 0; i < n; i++) {
        if (targets[i].zone_index == cur_zi)
            s_goal[targets[i].y][targets[i].x] = true;
    }
    nav5_run(ctx, bridges, can_fly, true);

    int best = -1, best_cost = NAV_INF;
    for (int i = 0; i < n; i++) {
        int c;
        if (targets[i].zone_index == cur_zi) {
            c = goal_cost(targets[i].x, targets[i].y, NULL);
        } else {
            int hops = zone_hops(ctx->res, cur_zi, targets[i].zone_index);
            if (hops < 0) continue;
            c = MV_XZONE_COST + hops;
        }
        if (c < best_cost) { best_cost = c; best = i; }
    }
    if (best < 0 || best_cost >= NAV_INF) {
        if (ob_diag_verbose()) {
            int reached = 0;
            for (int i = 0; i < NAV_NODES; i++)
                if (s_dist[i] < NAV_INF) reached++;
            printf("[NAV] unreachable: hero=(%d,%d,%s) mode=%d map=%dx%d "
                   "targets=%d reached-nodes=%d first-target=(%d,%d,z%d)\n",
                   g->position.x, g->position.y, g->position.zone,
                   (int)g->travel_mode, ctx->map->width, ctx->map->height,
                   n, reached, targets[0].x, targets[0].y,
                   targets[0].zone_index);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const Tile *nt = MapGetTile(ctx->map, g->position.x + dx,
                                                g->position.y + dy);
                    if (!nt) { printf("[NAV]  n(%+d,%+d)=NULL\n", dx, dy);
                               continue; }
                    printf("[NAV]  n(%+d,%+d) art=%s terr=%d int=%d blocks=%d "
                           "foot=%d\n", dx, dy, nt->art, (int)nt->terrain,
                           (int)nt->interactive, (int)nt->blocks_foot,
                           (int)adventure_walkable_on_foot(nt));
                }
        }
        if (out_cause) *out_cause = EXEC_CAUSE_REACH;
        return -1;
    }
    if (!commit) {
        if (out_cost) *out_cost = best_cost;
        return best;
    }

    const NavPoint *t = &targets[best];
    bool rerented = false, gated_out = false;

    for (int leg = 0; leg < NAV_MAX_LEGS; leg++) {
        if (mv_satisfied(ctx, t)) return best;
        if (g->stats.game_over) {
            if (out_cause) *out_cause = EXEC_CAUSE_TIME;
            return -1;
        }
        cur_zi = hero_zone_index(ctx);
        if (t->zone_index != cur_zi) {
            ExecCause cc = EXEC_CAUSE_NONE;
            if (!do_crossing(ctx, t->zone_index, &rerented, t, &cc)) {
                if (out_cause) *out_cause = cc != EXEC_CAUSE_NONE
                                          ? cc : EXEC_CAUSE_REACH;
                return -1;
            }
            continue;
        }

        // Same-zone: gate leg when strictly cheaper (AP-092).
        {
            memset(s_goal, 0, sizeof s_goal);
            s_goal[t->y][t->x] = true;
            nav5_run(ctx, bridges, can_fly, true);
            int direct = goal_cost(t->x, t->y, NULL);
            bool town = false;
            GateDestination dest;
            int via = mv_gate_leg_score(ctx, t, direct, &town, &dest);
            if (via >= 0 && via < direct) {
                exec_gate_to(ctx, town, dest.zone, dest.x, dest.y);
            }
        }

        int r = drive_leg_ex(ctx, t, true);
        if (r == 1) return best;
        if (s_pending_rent >= 0) {
            // The path boards at a dock with no live boat: visit the town and
            // rent (AP-093), then re-path onto the now-real boat.
            perform_pending_rent(ctx);
            continue;
        }
        if (pending_flow != FLOW_NONE) {
            if (mv_satisfied(ctx, t)) return best;
            // Remember the foe tile before answering: a declined fight makes
            // it an avoid tile for the rest of this call, so re-paths do not
            // walk back into the same refusal.
            int fx = -1, fy = -1;
            if (pending_flow == FLOW_ATTACK_FOE && pending_foe_id[0]) {
                const FoeState *pf = GameFindFoeConst(g, pending_foe_id);
                if (pf) { fx = pf->x; fy = pf->y; }
            }
            if (!exec_answer_pending(ctx, true)) {
                if (out_cause) *out_cause = EXEC_CAUSE_OTHER;
                return -1;
            }
            if (fx >= 0 && fy >= 0) {
                const FoeState *pf2 = NULL;
                for (int fi = 0; fi < g->foe_count; fi++)
                    if (g->foes[fi].alive && g->foes[fi].x == fx &&
                        g->foes[fi].y == fy &&
                        strcmp(g->foes[fi].zone, g->position.zone) == 0)
                        pf2 = &g->foes[fi];
                if (pf2) s_avoid[fy][fx] = true;   // declined: route around
            }
            continue;
        }
        if (r < 0) {
            if (ob_diag_verbose())
                printf("[NAV] leg unreachable: hero=(%d,%d,%s mode=%d) "
                       "target=(%d,%d,z%d)\n", g->position.x, g->position.y,
                       g->position.zone, (int)g->travel_mode, t->x, t->y,
                       t->zone_index);
            // Recovery levers, once per call (AP-103): gate out of a pocket.
            if (!gated_out) {
                gated_out = true;
                GateDestination dests[GAME_GATE_DESTS_MAX];
                for (int town = 0; town < 2; town++) {
                    if (!mv_gate_castable(ctx, town == 1)) continue;
                    int dn = GameGateDestinations(
                        g, town ? GATE_DEST_TOWN : GATE_DEST_CASTLE, dests, GAME_GATE_DESTS_MAX);
                    if (dn > 0 && exec_gate_to(ctx, town == 1, dests[0].zone,
                                               dests[0].x, dests[0].y))
                        break;
                }
                continue;
            }
            if (out_cause) *out_cause = EXEC_CAUSE_REACH;
            return -1;
        }
    }
    if (mv_satisfied(ctx, t)) return best;
    watchdog_hit("nav-legs");
    if (ob_diag_verbose())
        printf("[NAV] drive gave up: hero=(%d,%d,%s mode=%d) target=(%d,%d,z%d) "
               "legs=%d\n", g->position.x, g->position.y, g->position.zone,
               (int)g->travel_mode, t->x, t->y, t->zone_index, NAV_MAX_LEGS);
    if (out_cause) *out_cause = EXEC_CAUSE_REACH;
    return -1;
}
