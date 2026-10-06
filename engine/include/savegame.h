#ifndef OB_SAVEGAME_H
#define OB_SAVEGAME_H

#include <stdbool.h>
#include <stdint.h>
#include "map.h"
#include "fog.h"

// Keep this in sync with game.h / savegame.c. Bump when the schema changes
// in a way that can't be read by an older loader.
#define SAVE_VERSION 11u

typedef enum {
    SAVE_OK = 0,
    SAVE_ERR_IO,
    SAVE_ERR_PARSE,
    SAVE_ERR_VERSION,
    SAVE_ERR_MISMATCH,
    // Save was written against a different game pack than the one
    // currently loaded. Loader refuses to deserialize because IDs from
    // the other pack would be meaningless here.
    SAVE_ERR_PACK,
} SaveResult;

// Forward-declared so savegame.h doesn't need to drag game.h into every
// file that includes map.h.
#include "game_fwd.h"

// Write the Game and the fog to `path` as JSON (engine/state_serialize.c).
// SAVE_ERR_IO when the file cannot be written in full.
SaveResult SaveGameWrite(const char *path,
                         const Game *game,
                         const Map *map,
                         const Fog *fog);

// Read a save into `game` and `fog`, refusing another SAVE_VERSION or another
// pack. The map is not read: the caller reloads the zone (GameReloadZoneMap,
// OPENBOUNTY-SPEC REQ-415).
SaveResult SaveGameRead(const char *path,
                        Game *game,
                        Map *map,
                        Fog *fog);

// A short English description of a SaveResult, for logs.
const char *SaveResultText(SaveResult r);

// Lightweight header summary for the save-slot picker. No Map/Fog load.
typedef struct {
    bool exists;
    char name[32];          // character name
    char class_id[24];
    char rank_title[32];    // denormalized for display
    char zone[24];          // current zone id
    int  days_left;
    int  gold;
    char pack_id[64];       // game pack the save was written against; loader gates on this
    char pack_hash[17];     // whole-pack zip hash (FNV1a-64 hex); advisory only
} SaveHeader;

// Read only the header fields above. SAVE_ERR_IO, with `*out` zeroed, when
// there is no save at `path`.
SaveResult SaveGameReadHeader(const char *path, SaveHeader *out);

#endif
