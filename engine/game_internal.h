// engine/game_internal.h
//
// Shared by the files that make up the game module (engine/game*.c). Not part
// of the engine's public API.

#ifndef OB_GAME_INTERNAL_H
#define OB_GAME_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "game.h"

// The world RNG (OPENBOUNTY-SPEC §6): seed it, then draw from min..max.
void game_rng_seed(uint64_t seed);
int  game_rng_next(int min, int max);

// Bounded copy of an id (always terminated; NULL copies as "").
void game_copy_id(char *dst, size_t dst_sz, const char *src);

// The dwelling state row for a tile, made on first use: the troop picked by
// the kind and the seed, or the pinned `troop_id`, at full population.
DwellingState *game_enforce_dwelling(Game *g, const char *zone, int x, int y,
                                     const char *dwelling_kind);
DwellingState *game_enforce_dwelling_pinned(Game *g, const char *zone,
                                            int x, int y,
                                            const char *troop_id);

#endif
