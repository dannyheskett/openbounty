# OpenBounty engine

Game logic, mechanics, and state. Builds as `libobengine.a` — a
self-contained static archive vendoring cJSON and miniz, with no
raylib, audio, or window dependency.

Consumers link the library and provide implementations of the
host-callback functions declared in `engine/include/ui_host.h`.

## Layout

```
engine/
├── include/              # PUBLIC API. Consumers add -Iengine/include.
│   ├── adventure.h
│   ├── assets_bytes.h    # LoadAssetBytes / UnloadAssetBytes
│   ├── combat.h          # Combat state + entire engine combat API
│   ├── dwelling_kind.h   # DwellingKind enum (engine/shell shared)
│   ├── end_screen.h      # screen_end_game_open contract
│   ├── fatal.h
│   ├── flows.h           # Encounter / week-end / endgame flows
│   ├── fog.h
│   ├── game.h            # Game state, GameInit
│   ├── map.h
│   ├── pack.h            # Pack discovery and access
│   ├── pending.h         # Deferred-action scratch
│   ├── resources.h       # game.json schema
│   ├── savegame.h
│   ├── savepath.h
│   ├── spells_adventure.h
│   ├── state_serialize.h # JSON snapshots
│   ├── step.h            # Adventure step
│   ├── tables.h          # Troop/spell/artifact catalogs
│   ├── tile.h
│   ├── ui_host.h         # CONSUMER MUST PROVIDE THESE CALLBACKS
│   └── view_kind.h       # ViewKind enum (engine/shell shared)
│
├── headless/             # raylib stub headers for headless consumers
│   ├── raylib.h          # bridge: includes raylib_stub.h
│   ├── raylib_stub.h     # type/function no-ops matching real raylib
│   └── input_keys.h      # OB_KEY_* constants (raylib-compatible ints)
│
├── *.c                   # Engine implementation (game.c, map.c,
│                         # combat.c, etc.)
└── host_noop.c           # Default no-op implementations of
                          # ui_host.h callbacks. Optional — link this
                          # if you don't need real UI behavior.
```

## Building the library

```
make build/libobengine.a
```

The archive is produced at `build/libobengine.a`. Internal `.o` files
live in `build/objs/englib/`.

Compile flags used (from the Makefile):
```
gcc -std=c99 -Wall -Wextra -O2 -fPIC \
    -Iengine/headless -Iengine/include -Isrc -Ibuild \
    -Ithird_party/cjson -Ithird_party/miniz \
    -DOB_HEADLESS \
    -c <engine sources>
ar rcs libobengine.a <objects>
```

The `-Isrc` is currently still in the engine library compile because
the shell's `combat_loop.h` is referenced by some headers — this is
benign; the engine doesn't pull any shell `.c` into the archive.

## Linking against the library

A minimal consumer links the archive + a host-callback implementation.
The simplest version uses the bundled `engine/host_noop.c`:

```
gcc -std=c99 -O2 \
    -Iengine/headless -Iengine/include -Ithird_party/cjson \
    my_consumer.c engine/host_noop.c build/libobengine.a \
    -o my_consumer \
    -lm -lpthread
```

Note the link line includes **only `-lm -lpthread`** — no raylib, no
X11, no audio device. If your consumer accidentally pulls those, the
link will fail.

For a working example, see `tests/library/consumer.c`. Build and run
with `make test-library`.

## Required host callbacks

Consumers must define every function declared in
`engine/include/ui_host.h`:

- Modal prompts (`prompt_yes_no_open`, `prompt_ab_open`,
  `prompt_text_input_open`, plus state queries)
- Bottom-frame dialogs (`open_dialog`, plus state queries)
- View stack (`views_active`, `views_open_town`, screen openers)
- Audio events (`audio_play_tune`)
- Recorder events (`recorder_capture`)
- Engine state queries (`main_fast_quit_active`)

Headless consumers can link `engine/host_noop.c` and get no-op
implementations of all of them. Real consumers (the game's shell)
implement them for real.

## Rules for engine code

1. No `#include "raylib.h"` outside `engine/headless/`. Engine `.c`
   files should be raylib-free.
2. No audio playback, no rendering, no input polling. The engine emits
   abstract events via `ui_host.h` callbacks.
3. No window or `GetTime()` dependencies in engine source. Time is a
   logical-tick counter on the game state.
4. The engine library is self-contained — cJSON and miniz are vendored
   inside `libobengine.a`. Consumers don't need their own copies.

## Architectural notes

- **Combat split.** Engine combat (state, AI, headless turn loop, damage
  formula) is in `engine/combat.c`. The rendered combat loop
  (`RunCombat`, modal input, target picker, per-frame present) is in
  `src/combat_loop.c` (shell). The same `Combat` struct (defined in
  `engine/include/combat.h`) is used by both.
- **Assets split.** `LoadAssetBytes` (byte-level reads from the pack
  stack) is engine. `LoadAssetTexture` (raylib `Texture2D` loader) is
  shell. The engine never touches GPU textures.
- **flows.h is clean.** `show_win_game` no longer takes
  `RenderTexture2D *` — render-side concerns (the win cartoon) are
  invoked separately by the host before calling `show_win_game`.
