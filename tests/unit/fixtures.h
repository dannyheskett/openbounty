// Shared test fixtures used by multiple suites. Keeps test bodies
// short and fixes a single asset path / seed convention.

#ifndef BNT_TEST_FIXTURES_H
#define BNT_TEST_FIXTURES_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

// Tests run against the in-repo loose-tree pack. fx_load_resources
// opens it as a directory pack on first call.
#define FIXTURE_PACK_DIR  "assets/kings-bounty"
#define FIXTURE_MANIFEST  "game.json"
#define FIXTURE_SEED      42UL

// Legacy alias still referenced by some callers.
#define FIXTURE_ASSET_PATH FIXTURE_MANIFEST

// Allocate (with calloc) and load a Resources from the canonical
// asset path. Returns NULL on failure. Caller must resources_free + free.
Resources *fx_load_resources(void);

// Init a game with a deterministic seed (default FIXTURE_SEED if seed==0).
// Caller-provided struct, must already have its own res pointer set via
// fx_init_game_full or by the caller.
void fx_init_game(Game *g, Resources *res, unsigned long seed);

// Higher-level: allocate Resources + Game + Map + Fog with seed=42 and
// load continentia. *out_* are caller-owned and must be freed in
// reverse order: free(fog); free(map); resources_free(res); free(res); free(game).
// Returns true on success.
bool fx_init_game_full(Resources **out_res, Game **out_game,
                       Map **out_map, Fog **out_fog,
                       const char *zone, unsigned long seed);

// Clean up everything allocated by fx_init_game_full. Safe with NULL.
void fx_free_game_full(Resources *res, Game *game, Map *map, Fog *fog);

#endif
