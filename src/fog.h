#ifndef OB_FOG_H
#define OB_FOG_H

#include <stdbool.h>
#include "map.h"

// Per-tile visibility for the active map. One Fog instance is kept per
// continent so revisited zones retain previously revealed terrain.
// FogReveal stamps a 5x5 square around (cx, cy), clamped to map bounds;
// the radius arg is preserved for API compatibility but ignored.

typedef struct {
    bool seen[MAP_MAX_H][MAP_MAX_W];
} Fog;

void FogInit(Fog *fog);
void FogReveal(Fog *fog, const Map *map, int cx, int cy, int radius);
bool FogSeen(const Fog *fog, int x, int y);

#endif
