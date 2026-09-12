#include "fog.h"
#include "resources.h"
#include <string.h>

void FogInit(Fog *fog) {
    memset(fog, 0, sizeof(*fog));
}

void FogReveal(Fog *fog, const Map *map, int cx, int cy, int radius) {
    // `clear_fog` reveals a 5x5 square (-2..+2 on both axes)
    // regardless of the radius argument. We respect that for authenticity,
    // clamping to the map edges.
    (void)radius;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || y < 0 || x >= map->width || y >= map->height) continue;
            fog->seen[y][x] = true;
        }
    }
}

void FogRevealRadius(Fog *fog, const Map *map, int cx, int cy, int radius) {
    if (!fog || !map) return;
    if (radius < 0) radius = 0;
    if (radius > MAP_MAX_W) radius = MAP_MAX_W;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || y < 0 || x >= map->width || y >= map->height) continue;
            fog->seen[y][x] = true;
        }
    }
}

void FogRevealFor(const Resources *res, Fog *fog, const Map *map,
                  int cx, int cy) {
    const Resources *r = res;
    if (r && r->render.mode == RENDER_MODE_MODERN)
        FogRevealRadius(fog, map, cx, cy, r->world.fog_sight);
    else
        FogReveal(fog, map, cx, cy, r ? r->world.fog_sight : 2);
}

bool FogSeen(const Fog *fog, int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_MAX_W || y >= MAP_MAX_H) return false;
    return fog->seen[y][x];
}
