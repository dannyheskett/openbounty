// tools/navprobe.c — THROWAWAY diagnostic. Boots continentia on a seed and, for
// every chest objective, reports reachability from the hero spawn under:
//   (a) foot only (no boat)
//   (b) a FRESH boat docked at each town in turn (hero teleported to the dock-
//       adjacent approach, boat at the dock)
// Goal: decide whether "unreachable" objectives are reachable with SOME boat
// choice (=> the blocker is single-boat stranding, fixable) or reachable by
// NONE (=> genuine partition for M1's single-zone foot+1-boat scope).
//
// Engine-pure: links libobengine + engine headers + the autoplay nav module.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "pack.h"
#include "resources.h"

#include "nav.h"

#define PACK "assets/kings-bounty"
#define MANIFEST "game.json"

static const int DX8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int DY8[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

int main(int argc, char **argv) {
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 10) : 1;

    Pack *pk = pack_open(PACK);
    if (!pk) { fprintf(stderr, "pack_open failed\n"); return 2; }
    pack_stack_push(pk);
    Resources *res = calloc(1, sizeof *res);
    if (!resources_load(res, MANIFEST)) { fprintf(stderr, "res load failed\n"); return 2; }

    Game *g = calloc(1, sizeof *g);
    Map *map = calloc(1, sizeof *map);
    Fog *fog = calloc(1, sizeof *fog);
    g->res = res; g->seed = seed;
    GameInit(g, "Probe", 0, 1, NULL);
    FogInit(fog);
    if (!MapLoadZoneWithPlacements(map, res, res->world.starting_zone, g)) {
        fprintf(stderr, "map load failed\n"); return 2;
    }

    // zone index for continentia
    int zi = -1;
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, g->position.zone) == 0) { zi = i; break; }
    const ResZone *z = &res->zones[zi];

    NavOptions opts; nav_default_options(&opts);
    NavPoint spawn = { g->position.x, g->position.y };
    printf("seed=%llu spawn=(%d,%d) zone=%s chests=%d towns=%d\n",
           (unsigned long long)seed, spawn.x, spawn.y, z->id,
           z->chest_count, z->town_count);

    // Terrain dump: `navprobe <seed> <x> <y> [radius]` prints the tile grid around
    // (x,y) — terrain glyph + a marker for interactive/blocks-foot — so the raw map
    // can be inspected directly. Glyphs: '.'=grass 'F'=forest '^'=mountain '~'=water
    // 'd'=desert; UPPERCASE or symbol overlays: '#'=blocks_foot, '*'=interactive,
    // 'X'=the target tile.
    extern bool adventure_walkable_on_foot(const Tile *);
    if (argc >= 4) {
        int tx = atoi(argv[2]), ty = atoi(argv[3]);
        int rad = (argc >= 5) ? atoi(argv[4]) : 6;
        printf("\nTERRAIN around (%d,%d) radius %d  [terrain | #=blocks_foot *=interactive X=target]\n", tx, ty, rad);
        printf("     ");
        for (int x = tx - rad; x <= tx + rad; x++) printf("%d", (x>=0&&x<map->width)?(x/10)%10:0);
        printf("\n     ");
        for (int x = tx - rad; x <= tx + rad; x++) printf("%d", (x>=0)?x%10:0);
        printf("\n");
        for (int y = ty - rad; y <= ty + rad; y++) {
            if (y < 0 || y >= map->height) continue;
            printf("%3d  ", y);
            for (int x = tx - rad; x <= tx + rad; x++) {
                if (x < 0 || x >= map->width) { printf(" "); continue; }
                const Tile *t = MapGetTile(map, x, y);
                char ch = '?';
                if (t) {
                    switch (t->terrain) {
                        case TERRAIN_GRASS: ch='.'; break;
                        case TERRAIN_FOREST: ch='F'; break;
                        case TERRAIN_MOUNTAIN: ch='^'; break;
                        case TERRAIN_WATER: ch='~'; break;
                        case TERRAIN_DESERT: ch='d'; break;
                        default: ch='?'; break;
                    }
                    if (t->interactive != INTERACT_NONE) ch='*';
                    if (t->blocks_foot) ch='#';
                }
                if (x == tx && y == ty) ch='X';
                printf("%c", ch);
            }
            const Tile *rt = MapGetTile(map, tx, ty);
            if (y == ty && rt) printf("   <- target: terrain=%d interactive=%d blocks_foot=%d walkable_foot=%d",
                                      rt->terrain, rt->interactive, rt->blocks_foot, adventure_walkable_on_foot(rt));
            printf("\n");
        }
        printf("\n");
    }

    // Precompute town approach tiles (first walkable neighbor of each town).
    int n_towns = z->town_count;
    int appr_x[64], appr_y[64], dock_x[64], dock_y[64]; int n_appr = 0;
    for (int t = 0; t < n_towns && t < 64; t++) {
        int tx = z->towns[t].x, ty = z->towns[t].y;
        for (int k = 0; k < 8; k++) {
            int ax = tx + DX8[k], ay = ty + DY8[k];
            if (ax < 0 || ax >= map->width || ay < 0 || ay >= map->height) continue;
            const Tile *nt = MapGetTile(map, ax, ay);
            if (!nt) continue;
            // approach must be foot-walkable to stand on
            extern bool adventure_walkable_on_foot(const Tile *);
            if (!adventure_walkable_on_foot(nt)) continue;
            appr_x[n_appr] = ax; appr_y[n_appr] = ay;
            dock_x[n_appr] = z->towns[t].boat_x; dock_y[n_appr] = z->towns[t].boat_y;
            n_appr++;
            break;
        }
    }
    printf("town approaches found: %d\n", n_appr);

    int reach_foot = 0, reach_some_boat = 0, reach_none = 0;
    for (int c = 0; c < z->chest_count; c++) {
        NavPoint tgt = { z->chests[c].x, z->chests[c].y };
        // (a) foot only from spawn
        bool foot = nav_reachable(map, spawn, tgt, &opts, NULL);
        // (b) fresh boat from each town approach
        bool any_boat = false; int which = -1;
        for (int a = 0; a < n_appr; a++) {
            NavTravel fresh = { NAV_MODE_FOOT, true, dock_x[a], dock_y[a] };
            NavPoint from = { appr_x[a], appr_y[a] };
            if (nav_reachable_travel(map, from, &fresh, tgt, &opts, NULL)) {
                any_boat = true; which = a; break;
            }
        }
        if (foot) reach_foot++;
        else if (any_boat) reach_some_boat++;
        else reach_none++;

        if (!foot) {
            printf("  chest (%2d,%2d): foot=%d boat=%s%s\n",
                   tgt.x, tgt.y, foot,
                   any_boat ? "YES" : "no ",
                   any_boat ? (which>=0 ? " (some dock)" : "") : "  <-- UNREACHABLE by foot or any single fresh boat");
        }
    }
    printf("\nSUMMARY seed=%llu: foot=%d  needs-boat(some dock works)=%d  unreachable-by-any=%d  total=%d\n",
           (unsigned long long)seed, reach_foot, reach_some_boat, reach_none, z->chest_count);
    return 0;
}
