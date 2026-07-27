// Terrain edge-variant furnishing (OPENBOUNTY-SPEC REQ-229 / REQ-229a).
//
// A .dat holds the FULLY RENDERED map: edge variants are baked in here, at
// author time, and the engine computes nothing about a map's appearance at
// load. furnish_map() in engine/game.c is a permanent no-op.
//
// Two passes, in order:
//
//   despeckle -- absorb any tile whose neighbour pattern has no legal edge
//                variant. Three shapes qualify: three or more differing
//                cardinals (a 1-tile-wide feature), exactly two differing
//                cardinals that are opposite (a 1-tile channel or isthmus),
//                and two or more differing diagonals with no differing
//                cardinal. Each is absorbed into whichever neighbouring
//                terrain surrounds it most, iterating to a fixed point.
//
//   furnish   -- swap each remaining plain tile that borders another terrain
//                for the variant matching its neighbour pattern.
//
// Without despeckle, furnish would leave those tiles plain and they would
// render as hard stair-steps. The shipped kings-bounty maps contain zero
// three-cardinal tiles: the original authors shaped terrain so the case
// cannot arise, and despeckle enforces the same discipline mechanically.

#include "mapedit.h"

#include <string.h>

// Neighbour offsets. Cardinals first: the selection rules test them before
// diagonals, and several loops below rely on that ordering.
static const int DX[8] = {  0,  0,  1, -1,  1,  1, -1, -1 };
static const int DY[8] = { -1,  1,  0,  0, -1,  1,  1, -1 };
enum { N_ = 0, S_ = 1, E_ = 2, W_ = 3, NE_ = 4, SE_ = 5, SW_ = 6, NW_ = 7 };

// Variant index per neighbour pattern. Water is 0-based (00..11); forest,
// mountain and desert are 1-based (01..12) with a different permutation.
// Derived from the shipped maps and verified to reproduce 96.4% of their
// 7,870 edge tiles exactly (REQ-229b).
typedef struct { int n, s, e, w, ne_c, nw_c, sw_c, se_c, ne, se, sw, nw; } EdgeSet;
static const EdgeSet EDGE_WATER = { 10, 11,  8,  9,  0,  1,  2,  3,  5,  4,  6,  7 };
static const EdgeSet EDGE_OTHER = { 11, 12,  9, 10,  3,  1,  2,  4,  6,  5,  7,  8 };

static bool in_bounds(const MapGrid *m, int x, int y) {
    return x >= 0 && y >= 0 && x < m->w && y < m->h;
}

// Terrain of a neighbour, or the tile's own terrain when out of bounds --
// so a range running off the map edge stays solid rather than transitioning
// into nothing.
static Terrain neighbour_terrain(const MapGrid *m, int x, int y, int d,
                                 Terrain own) {
    int nx = x + DX[d], ny = y + DY[d];
    return in_bounds(m, nx, ny) ? m->cell[ny][nx].terrain : own;
}

int mapedit_despeckle(MapGrid *m) {
    int total = 0;
    for (int pass = 0; pass < 32; pass++) {
        int fixed = 0;
        for (int y = 0; y < m->h; y++) {
            for (int x = 0; x < m->w; x++) {
                Terrain t = m->cell[y][x].terrain;
                Terrain nb[8];
                int diff_card = 0, diff_diag = 0;
                bool d_n = false, d_s = false, d_e = false, d_w = false;
                for (int d = 0; d < 8; d++) {
                    nb[d] = neighbour_terrain(m, x, y, d, t);
                    if (nb[d] == t) continue;
                    if (d < 4) {
                        diff_card++;
                        if (d == N_) d_n = true;
                        if (d == S_) d_s = true;
                        if (d == E_) d_e = true;
                        if (d == W_) d_w = true;
                    } else {
                        diff_diag++;
                    }
                }
                bool opposite_pair = (diff_card == 2) &&
                                     ((d_n && d_s) || (d_e && d_w));
                bool illegal = (diff_card >= 3) || opposite_pair ||
                               (diff_card == 0 && diff_diag >= 2);
                if (!illegal) continue;

                // Absorb into the terrain that surrounds it most. Only the
                // differing neighbours vote; ties go to water, which keeps
                // coastlines clean rather than growing spits.
                int votes[TERRAIN_COUNT];
                memset(votes, 0, sizeof votes);
                int lo = (diff_card == 0) ? 4 : 0;
                int hi = (diff_card == 0) ? 8 : 4;
                for (int d = lo; d < hi; d++) {
                    if (nb[d] != t) votes[nb[d]]++;
                }
                Terrain best = t;
                int best_v = 0;
                for (int i = 0; i < TERRAIN_COUNT; i++) {
                    if (votes[i] > best_v ||
                        (votes[i] == best_v && votes[i] > 0 &&
                         i == TERRAIN_WATER)) {
                        best_v = votes[i];
                        best = (Terrain)i;
                    }
                }
                if (best == t) continue;
                m->cell[y][x].terrain = best;
                m->cell[y][x].variant = -1;      // back to plain
                m->cell[y][x].decor   = 0;       // absorbed: drop decoration
                fixed++;
            }
        }
        total += fixed;
        if (fixed == 0) break;
    }
    return total;
}

int mapedit_furnish(MapGrid *m, int *unresolved_out) {
    int changed = 0, unresolved = 0;

    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            Terrain t = m->cell[y][x].terrain;
            m->cell[y][x].variant = -1;         // start from plain every pass
            if (t == TERRAIN_GRASS) continue;   // grass ships no variants

            bool d[8];
            int diff_card = 0, diff_diag = 0, only_diag = -1;
            for (int i = 0; i < 8; i++) {
                d[i] = neighbour_terrain(m, x, y, i, t) != t;
                if (!d[i]) continue;
                if (i < 4) diff_card++;
                else { diff_diag++; only_diag = i; }
            }
            if (diff_card == 0 && diff_diag == 0) continue;

            const EdgeSet *e = (t == TERRAIN_WATER) ? &EDGE_WATER : &EDGE_OTHER;
            int idx = -1;
            if (diff_card == 2) {
                if (d[N_] && d[E_]) idx = e->ne_c;
                else if (d[N_] && d[W_]) idx = e->nw_c;
                else if (d[S_] && d[W_]) idx = e->sw_c;
                else if (d[S_] && d[E_]) idx = e->se_c;
            } else if (diff_card == 1) {
                idx = d[N_] ? e->n : d[S_] ? e->s : d[E_] ? e->e : e->w;
            } else if (diff_card == 0 && diff_diag == 1) {
                idx = (only_diag == NE_) ? e->ne : (only_diag == SE_) ? e->se
                    : (only_diag == SW_) ? e->sw : e->nw;
            }
            if (idx < 0) {                      // despeckle should prevent this
                unresolved++;
                continue;
            }
            m->cell[y][x].variant = idx;
            changed++;
        }
    }
    if (unresolved_out) *unresolved_out = unresolved;
    return changed;
}
