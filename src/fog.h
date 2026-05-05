#ifndef FOG_H
#define FOG_H

#include <stdbool.h>
#include "map.h"

typedef struct {
    bool seen[MAP_MAX_H][MAP_MAX_W];
} Fog;

void FogInit(Fog *fog);
void FogReveal(Fog *fog, const Map *map, int cx, int cy, int radius);
bool FogSeen(const Fog *fog, int x, int y);

#endif
