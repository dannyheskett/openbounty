#ifndef OB_STATE_SERIALIZE_H
#define OB_STATE_SERIALIZE_H

#include <stdint.h>
#include "cJSON.h"

// Game state serialization. The function below is the single source
// of truth for "what does game state look like in JSON?". Both the
// save-file writer (savegame.c) and the in-memory recorder (recorder.c)
// call this; the harness reads the most-recent recorder entry's
// serialized JSON to answer `state` queries.
//
// The output schema is a SUPERSET of the save format. New fields are
// additive -- savegame's loader ignores keys it doesn't know, so older
// saves still round-trip and snapshot output is save-compatible.

// Game/Map/Fog/Combat are typedef'd from anonymous structs, so we
// can't forward-declare them as `struct Foo *`. Pull the headers.
#include "game.h"
#include "map.h"
#include "fog.h"
#include "combat.h"

// Build a complete game-state snapshot. Caller owns the returned cJSON
// and must cJSON_Delete it.
//
// Required:
//   g, map, fog       -- live game state.
// Optional (may be NULL):
//   combat            -- when in battle, the live Combat*; else NULL.
//   trigger           -- short tag like "step:right" or "combat:start".
//                       NULL -> omitted.
//   snap_path         -- path of the screenshot most-recently associated
//                       with this snapshot; NULL -> omitted.
//   seq, ms           -- monotonic id + wall-clock ms since session start.
//                       0 -> omitted (savegame uses this).
cJSON *state_build_snapshot(const Game *g,
                            const Combat *combat,
                            const Map *map,
                            const Fog *fog,
                            const char *trigger,
                            const char *snap_path,
                            uint64_t seq,
                            uint64_t ms);

#endif
