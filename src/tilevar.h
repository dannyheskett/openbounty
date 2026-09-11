// src/tilevar.h
//
// Cosmetic tile variants. A pack may give a tile code a list of alternate
// art names (`variants` in tile_codes, PACK-FORMAT section 4); the shell
// picks one per map cell when it draws, from the cell's x, y and a seed
// drawn once per session, so a field of grass is not one stamp repeated
// and looks a little different every launch. Draw-time only: the map, the
// game state, saves and replays are untouched (OPENBOUNTY-SPEC REQ-229d).
// Legacy packs declare no variants and draw exactly as before.

#ifndef OB_TILEVAR_H
#define OB_TILEVAR_H

#include <stddef.h>

struct Resources;

// Remember every code's variants and the session seed.
void tilevar_init(const struct Resources *res, unsigned seed);

// Which of n choices cell (x, y) gets under this seed: 0..n-1, the same
// for the same inputs, spread evenly. 0 when n <= 1. Pure.
int  tilevar_pick(unsigned seed, int x, int y, int n);

// The art to draw for a cell whose tile art is `art` (a stem, or
// "<set>/<stem>" for a zone with its own tile set). Returns `art` itself
// when the stem has no variants, else one of them, keeping the set prefix,
// written into out. The base art counts as one of the choices.
const char *tilevar_art(const char *art, int x, int y, char *out, size_t cap);

#endif
