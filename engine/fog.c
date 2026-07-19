#include "fog.h"
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

bool FogSeen(const Fog *fog, int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_MAX_W || y >= MAP_MAX_H) return false;
    return fog->seen[y][x];
}
