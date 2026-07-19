// demo/demo_scepter.c
//
// Scepter deduction -- the puzzle screen played the way a human plays it.
// Each villain caught / artifact found reveals one cell of the 5x5 puzzle
// window, which shows the map art around the buried scepter (centered on it,
// clamped at map edges -- engine tables.c PUZZLE_GRID). The player matches the
// revealed picture fragment against terrain they have explored and keeps a
// candidate list; a failed 10-day dig eliminates a tile. The information used
// here is exactly what the puzzle view renders plus the player's own fog.

#include "demo_internal.h"

#include <stdio.h>
#include <string.h>

#include "tables.h"
#include "tile.h"

// Two map scratches: the puzzle window's source zone and the zone being
// scanned. Static (a Map is ~1MB; single-threaded like the rest of the game).
static Map  s_shown_map;
static char s_shown_zone[32];
static Map  s_scan_map;
static char s_scan_zone[32];

// The camera clamp the puzzle view applies (views_render.c draw_puzzle).
static void puzzle_cam(const Map *m, int cx, int cy, int *out_x, int *out_y) {
    int camx = cx - 2, camy = cy - 2;
    if (camx < 0) camx = 0;
    if (camy < 0) camy = 0;
    if (camx > m->width - 5)  camx = m->width - 5;
    if (camy > m->height - 5) camy = m->height - 5;
    *out_x = camx; *out_y = camy;
}

// Is puzzle cell (row, col) revealed by the current catches/finds?
static bool cell_revealed(const Game *g, int row, int col) {
    int e = puzzle_grid_entity(row, col);
    if (e >= 0) {
        int cap = (int)(sizeof g->contract.villains_caught /
                        sizeof g->contract.villains_caught[0]);
        return e < cap && g->contract.villains_caught[e];
    }
    int ai = -e - 1;
    int acap = (int)(sizeof g->artifacts.found / sizeof g->artifacts.found[0]);
    return ai >= 0 && ai < acap && g->artifacts.found[ai];
}

static bool spot_searched(const DemoState *st, const char *zone, int x, int y) {
    for (int i = 0; i < st->searched_count; i++)
        if (st->searched[i].x == x && st->searched[i].y == y &&
            strcmp(st->searched[i].zone, zone) == 0) return true;
    return false;
}

void demo_scepter_update(const Game *g, const Fog *live_fog) {
    DemoState *st = demo_state();
    int reveals = GameVillainsCaught(g) + GameArtifactsFound(g);
    if (reveals == st->cand_reveals && st->searched_count == st->cand_searched)
        return;                                    // nothing new to deduce from
    st->cand_reveals  = reveals;
    st->cand_searched = st->searched_count;
    st->cand_count = 0;
    st->cand_total = 0;
    if (reveals == 0 || !g->scepter.zone[0]) return;

    // The picture fragment the puzzle screen shows: art names of the revealed
    // cells around the scepter (view-equivalent; the player cannot see which
    // zone it is). Same source map as the view: MapLoadZone, no salt.
    if (strcmp(s_shown_zone, g->scepter.zone) != 0) {
        if (!MapLoadZone(&s_shown_map, g->res, g->scepter.zone)) return;
        snprintf(s_shown_zone, sizeof s_shown_zone, "%s", g->scepter.zone);
    }
    int camx, camy;
    puzzle_cam(&s_shown_map, g->scepter.x, g->scepter.y, &camx, &camy);
    char shown[5][5][TILE_ART_NAME_LEN];
    int nshown = 0;
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 5; col++) {
            shown[row][col][0] = '\0';
            if (!cell_revealed(g, row, col)) continue;
            const Tile *t = MapGetTile(&s_shown_map, camx + col, camy + row);
            if (!t) continue;
            snprintf(shown[row][col], TILE_ART_NAME_LEN, "%s", t->art);
            nshown++;
        }
    if (nshown == 0) return;

    // Match the fragment against every DISCOVERED zone, over tiles the player
    // has actually explored (the per-zone fog). Burial-rule prefilter is the
    // game's own public rule: grass, no interactive, not foot-blocking.
    for (int zi = 0; zi < g->res->zone_count && zi < GAME_CONTINENTS; zi++) {
        if (!g->world.zones_discovered[zi]) continue;
        const char *zid = g->res->zones[zi].id;
        const Fog *zfog = (strcmp(zid, g->position.zone) == 0)
                              ? live_fog : &g->world.continent_fog[zi];
        if (strcmp(s_scan_zone, zid) != 0) {
            if (!MapLoadZone(&s_scan_map, g->res, zid)) continue;
            snprintf(s_scan_zone, sizeof s_scan_zone, "%s", zid);
        }
        const Map *m = &s_scan_map;
        for (int y = 0; y < m->height; y++) {
            for (int x = 0; x < m->width; x++) {
                const Tile *t = &m->tiles[y][x];
                if (t->terrain != TERRAIN_GRASS ||
                    t->interactive != INTERACT_NONE || t->blocks_foot)
                    continue;
                if (!FogSeen(zfog, x, y)) continue;   // can't dig blind
                int cx, cy;
                puzzle_cam(m, x, y, &cx, &cy);
                bool ok = true;
                for (int row = 0; row < 5 && ok; row++)
                    for (int col = 0; col < 5 && ok; col++) {
                        if (!shown[row][col][0]) continue;
                        int mx = cx + col, my = cy + row;
                        const Tile *ct = MapGetTile(m, mx, my);
                        if (!ct || !FogSeen(zfog, mx, my) ||
                            strcmp(ct->art, shown[row][col]) != 0)
                            ok = false;
                    }
                if (!ok) continue;
                if (spot_searched(demo_state(), zid, x, y)) continue;
                st->cand_total++;
                if (st->cand_count < DEMO_CAND_MAX) {
                    DemoSpot *c = &st->cand[st->cand_count++];
                    snprintf(c->zone, sizeof c->zone, "%s", zid);
                    c->x = x; c->y = y;
                }
            }
        }
    }
}

bool demo_scepter_nearest(const Game *g, DemoSpot *out) {
    const DemoState *st = demo_state();
    if (st->cand_count <= 0) return false;
    // Same zone preferred, then nearest by Chebyshev distance (a walking
    // player's rough estimate); cross-zone candidates rank behind all local.
    int best = -1;
    long best_key = 0;
    for (int i = 0; i < st->cand_count; i++) {
        const DemoSpot *c = &st->cand[i];
        bool local = strcmp(c->zone, g->position.zone) == 0;
        long d;
        if (local) {
            int dx = c->x - g->position.x, dy = c->y - g->position.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            d = (dx > dy) ? dx : dy;
        } else {
            d = 100000;
        }
        if (best < 0 || d < best_key) { best = i; best_key = d; }
    }
    if (best < 0) return false;
    *out = st->cand[best];
    return true;
}
