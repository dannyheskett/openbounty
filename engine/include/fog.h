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

// A rectangle of (2 * rx + 1) x (2 * ry + 1) tiles around (cx, cy), clamped
// to the map. FogReveal above is the original's fixed 5x5.
void FogRevealRect(Fog *fog, const Map *map, int cx, int cy, int rx, int ry);

// The reveal from where the hero stands: exactly the pack's viewport
// (render.tiles_w x tiles_h) around the hero, in both modes. The original's
// clear_fog reveals its 5x5 viewport, so the tile just past each edge of the
// view is unexplored until walked towards and the edge tiles show the fog
// fade; a 5x5 viewport -- the original's, and Rome's -- gets exactly 5x5, and
// a wider one its own width (a square reveal wider than the view's height
// leaves the rows past its top and bottom edges explored, so they never show
// fog).
void FogRevealFor(const Resources *res, Fog *fog, const Map *map,
                  int cx, int cy);

#endif
