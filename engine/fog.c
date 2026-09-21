#include "fog.h"
#include "resources.h"
#include <stdlib.h>
#include <string.h>

void FogInit(Fog *fog) {
    if (!fog) return;
    if (fog->seen && fog->width > 0 && fog->height > 0)
        memset(fog->seen, 0, (size_t)fog->width * (size_t)fog->height);
}

void FogFree(Fog *fog) {
    if (!fog) return;
    free(fog->seen);
    memset(fog, 0, sizeof *fog);
}

bool FogSize(Fog *fog, int width, int height) {
    if (!fog || width < 0 || height < 0) return false;
    if (fog->seen && fog->width == width && fog->height == height) return true;
    FogFree(fog);
    if ((size_t)width * (size_t)height == 0) return true;
    fog->seen = calloc((size_t)width * (size_t)height, sizeof *fog->seen);
    if (!fog->seen) return false;
    fog->width = width;
    fog->height = height;
    return true;
}

bool FogCopy(Fog *dst, const Fog *src) {
    if (!dst || !src || dst == src) return dst != NULL;
    if (!src->seen) { FogFree(dst); return true; }
    if (!FogSize(dst, src->width, src->height)) return false;
    memcpy(dst->seen, src->seen, (size_t)src->width * (size_t)src->height);
    return true;
}

void FogReveal(Fog *fog, const Map *map, int cx, int cy, int radius) {
    // `clear_fog` reveals a 5x5 square (-2..+2 on both axes)
    // regardless of the radius argument. We respect that for authenticity,
    // clamping to the map edges.
    (void)radius;
    if (!fog || !map || !FogSize(fog, map->width, map->height)) return;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || y < 0 || x >= map->width || y >= map->height) continue;
            fog->seen[y * fog->width + x] = true;
        }
    }
}

void FogRevealRect(Fog *fog, const Map *map, int cx, int cy, int rx, int ry) {
    if (!fog || !map || !FogSize(fog, map->width, map->height)) return;
    if (rx < 0) rx = 0;
    if (ry < 0) ry = 0;
    if (rx > map->width)  rx = map->width;
    if (ry > map->height) ry = map->height;
    for (int dy = -ry; dy <= ry; dy++) {
        for (int dx = -rx; dx <= rx; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || y < 0 || x >= map->width || y >= map->height) continue;
            fog->seen[y * fog->width + x] = true;
        }
    }
}

void FogRevealFor(const Resources *res, Fog *fog, const Map *map,
                  int cx, int cy) {
    if (res && res->render.tiles_w > 0 && res->render.tiles_h > 0)
        FogRevealRect(fog, map, cx, cy, res->render.tiles_w / 2, res->render.tiles_h / 2);
    else
        FogReveal(fog, map, cx, cy, 2);
}

void FogSet(Fog *fog, int x, int y, bool seen) {
    if (!fog || !fog->seen || x < 0 || y < 0 || x >= fog->width || y >= fog->height) return;
    fog->seen[y * fog->width + x] = seen;
}

bool FogSeen(const Fog *fog, int x, int y) {
    if (!fog || !fog->seen || x < 0 || y < 0 || x >= fog->width || y >= fog->height)
        return false;
    return fog->seen[y * fog->width + x];
}
