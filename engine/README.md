# OpenBounty engine

Self-contained game logic. Builds as `libobengine.a` (vendoring cJSON +
miniz). External consumers link the library and provide implementations
of the `ui_host.h` callbacks.

## Layout

```
engine/
├── include/          # PUBLIC API. Consumers add -Iengine/include.
│   ├── game.h        # Game state, GameInit, stats/character/army
│   ├── map.h         # Map, MapLoadZoneWithPlacements
│   ├── fog.h         # Fog
│   ├── pack.h        # Pack discovery and access
│   ├── resources.h   # Game data tables (loaded from game.json)
│   ├── tables.h      # Troop/spell/artifact catalogs
│   ├── savegame.h    # Save/load
│   ├── savepath.h    # User save-dir resolution
│   ├── state_serialize.h  # JSON snapshots
│   ├── step.h        # Adventure step
│   ├── tile.h        # Tile semantics
│   ├── adventure.h   # Adventure-mode helpers
│   ├── combat.h      # Combat state + headless entry points
│   ├── flows.h       # Encounter/week-end/endgame flows
│   ├── pending.h     # Deferred-action scratch
│   ├── spells_adventure.h  # Adventure-mode spells
│   ├── assets_bytes.h      # LoadAssetBytes / UnloadAssetBytes
│   ├── end_screen.h        # screen_end_game_open (verdict text)
│   ├── ui_host.h           # CALLBACKS THE CONSUMER MUST PROVIDE
│   └── fatal.h             # Engine panic
├── headless/         # Raylib stub for headless builds. NOT part of
│   │                 # the public API; only used by the playtest
│   │                 # binary build. Consumers either link real
│   │                 # raylib (game-like host) or use the stub.
│   ├── raylib.h
│   ├── raylib_stub.h
│   └── input_keys.h
└── *.c               # Engine implementation. Compiled with the stub.
```

## Building

`make build/libobengine.a` produces the static archive. Internal
artifacts live in `build/objs/englib/`.

The library compiles with:
```
gcc -std=c99 -Wall -Wextra -O2 -fPIC \
    -Iengine/headless -Iengine/include -Isrc -Ibuild \
    -Ithird_party/cjson -Ithird_party/miniz \
    -DOB_HEADLESS \
    -c engine/*.c third_party/cjson/cJSON.c third_party/miniz/miniz.c
ar rcs libobengine.a *.o
```

(`-Isrc` is only there because a few engine .c files still include
shell headers like `audio.h`, `recorder.h` — for the prototypes they
need. Those prototypes redirect to the `ui_host.h` declarations at
link time.)

## Consumers

Three exist in this repo:

1. **`build/openbounty`** — the game. Links the engine sources + the
   shell (`src/*.c`) + raylib + audio.
2. **`build/openbounty-unittest`** — unit-test binary. Links engine +
   shell + tests + raylib (because some tests touch render code).
3. **`build/openbounty-engplay`** — headless playtest. Links engine +
   shell, with the raylib stub replacing real raylib. No window, no
   audio device, no X11.

An external consumer would link `libobengine.a` + their own host
implementations. See `engine/include/ui_host.h` for the required
surface.

## Rules for engine code

1. No `#include "raylib.h"` in engine source unless going through the
   stub-aware headless build. Engine `.c` files should be raylib-free.
2. No audio playback, no rendering, no input polling. The engine emits
   abstract events via `ui_host.h` callbacks.
3. No window or `GetTime()` dependencies. Time is a logical-tick
   counter on the game state.
4. The engine library is self-contained — cJSON and miniz are vendored
   inside `libobengine.a`. Consumers don't need their own copies.
