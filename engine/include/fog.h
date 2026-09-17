#ifndef OB_FOG_H
#define OB_FOG_H

#include <stdbool.h>
#include "map.h"
#include "resources.h"   // a leaf header: no cycle back to fog.h

// Per-tile visibility for the active map. One Fog instance is kept per
// continent so revisited zones retain previously revealed terrain.
// FogReveal stamps a 5x5 square around (cx, cy), clamped to map bounds;
// the radius arg is preserved for API compatibility but ignored.

// Storage: a heap grid sized to its map (FogReveal* size it to the map they
// are given). A Fog starts zeroed (calloc or `= { 0 }`), is copied only with
// FogCopy and released with FogFree.
typedef struct {
    int   width, height;
    bool *seen;            // width x height, row by row
} Fog;

// Every cell unseen; the size is kept.
void FogInit(Fog *fog);
// Size the fog to width x height, every cell unseen, when its size differs.
// False when out of memory.
bool FogSize(Fog *fog, int width, int height);
// Release the grid and zero the fog. Safe on a zeroed fog.
void FogFree(Fog *fog);
// Make dst a copy of src. False when out of memory (dst then freed).
bool FogCopy(Fog *dst, const Fog *src);
void FogReveal(Fog *fog, const Map *map, int cx, int cy, int radius);
bool FogSeen(const Fog *fog, int x, int y);
// Mark (x, y) seen or unseen. No-op outside the fog.
void FogSet(Fog *fog, int x, int y, bool seen);

// A square of (2 * radius + 1) tiles around (cx, cy), clamped to the map,
// honouring the radius. FogReveal above is the original's fixed 5x5.
void FogRevealRadius(Fog *fog, const Map *map, int cx, int cy, int radius);

// The reveal the pack asks for, from where the hero stands. Legacy keeps the
// original's authentic 5x5, whatever the pack declares. Modern honours
// world.fog_sight: its viewport is wider than five tiles (Rome's is 7), and a
// 5x5 reveal left the outer viewport columns black wherever the hero had not
// already walked -- a band of unexplored map down each side of the pane.
void FogRevealFor(const Resources *res, Fog *fog, const Map *map,
                  int cx, int cy);

#endif
