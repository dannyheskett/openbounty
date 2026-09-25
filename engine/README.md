# OpenBounty engine

Game logic, mechanics, and state. The engine has built as `libobengine.a`, a
self-contained static archive vendoring cJSON and miniz, with no raylib,
audio, or window dependency.

Consumers have linked the library and provided implementations of the
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
│   ├── flow_answer.h     # The answer a resolved decision carries (leaf header)
│   ├── flow_resolve.h    # Engine-side apply-cores for prompt flows
│   ├── flows.h           # Encounter / week-end / endgame flows
│   ├── fog.h
│   ├── game.h            # Game state, GameInit
│   ├── game_fwd.h        # Forward declaration of Game
│   ├── map.h
│   ├── pack.h            # Pack discovery and access
│   ├── pending.h         # Deferred-action scratch
│   ├── player_io.h       # The player-IO request queue
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
├── game.c                # Game state, init, mechanics, salting
├── map.c                 # Tile grid + .dat parsing
├── fog.c                 # Fog of war
├── adventure.c           # Walkability + interact dispatch
├── step.c                # `GameStep`, one-tile movement
├── combat.c              # Combat state, AI, headless turn loop, damage
├── combat_log.c          # Combat log line append (pure data)
├── flows.c               # Encounter / week-end / endgame
├── flow_resolve.c        # Apply-cores: the state half of each prompt flow
├── player_io.c           # The player-IO request queue
├── pack.c                # Pack reader: ZIP or loose tree, held in RAM
├── savegame.c            # JSON save read/write
├── savepath.c            # User save dir
├── state_serialize.c     # JSON snapshot builder
├── tables.c              # Catalog lookups
├── resources.c           # game.json parser
├── tile.c                # Tile semantics
├── pending.c             # Continuation state
├── spells_adventure.c    # Adventure-mode spells
├── assets_bytes.c        # LoadAssetBytes
├── fatal.c               # Fatal-error helper
└── host_noop.c           # Default no-op host callbacks (link if
                          # your consumer doesn't need real UI).
```

## Building the library

```
make build/libobengine.a
```

The archive has been produced at `build/libobengine.a`. Internal `.o` files
have lived in `build/objs/englib/`.

Compile flags used (from the Makefile):
```
gcc -std=c99 -Wall -Wextra -O2 -fPIC \
    -Iengine/headless -Iengine/include -Ibuild \
    -Ithird_party/cjson -Ithird_party/miniz \
    -DOB_HEADLESS \
    -c <engine sources>
ar rcs libobengine.a <objects>
```

The engine library compile has used **no `-Isrc`**: engine sources have never
included a shell header. Everything the engine needs from the host (dialog,
prompt, audio, recorder, asset bytes) has been declared in `engine/include`
(`ui_host.h`, `assets_bytes.h`). If an engine `.c` ever reaches into `src/`,
this compile has failed: the missing include path has been the enforcement.

## Linking against the library

A minimal consumer has linked the archive + a host-callback implementation.
The simplest version has used the bundled `engine/host_noop.c`:

```
gcc -std=c99 -O2 \
    -Iengine/headless -Iengine/include -Ithird_party/cjson \
    my_consumer.c engine/host_noop.c build/libobengine.a \
    -o my_consumer \
    -lm -lpthread
```

The link line has included **only `-lm -lpthread`**: no raylib, no X11, no
audio device. If a consumer accidentally pulls those, the link has failed.

For a working example, see `tests/library/consumer.c`. `make all` has built
the same consumer + host_noop + libobengine.a, with the demo and autoplay
objects linked beside them, as a link-time boundary check;
the resulting binary has been discarded and a stamp file
(`build/libtest-pass.stamp`) has recorded success. A build that produces the
stamp has proved the library consumable in isolation. If the engine ever
depends on shell headers or shell symbols, that link has failed and `make
all` has failed.

## Required host callbacks

Consumers have had to define every function declared in
`engine/include/ui_host.h`:

- Modal prompts (`prompt_yes_no_open`, `prompt_ab_open`,
  `prompt_text_input_open`, plus state queries)
- Bottom-frame dialogs (`open_dialog`, plus state queries)
- View stack (`views_active`, `views_open_town`, screen openers)
- Audio events (`audio_play_tune`)
- Recorder events (`recorder_capture`)
- Engine state queries (`main_fast_quit_active`)

Headless consumers have been able to link `engine/host_noop.c` and get no-op
implementations of all of them. Real consumers (the game's shell) have
implemented them for real.

## Rules for engine code

1. No `#include "raylib.h"` outside `engine/headless/`. Engine `.c` files
   have been raylib-free.
2. No audio playback, no rendering, no input polling. The engine has emitted
   abstract events via `ui_host.h` callbacks.
3. No window or `GetTime()` dependencies in engine source. Time has been a
   logical-tick counter on the game state.
4. The engine library has been self-contained: cJSON and miniz have been
   vendored inside `libobengine.a`. Consumers have not needed their own
   copies.

## Architectural notes

- **Combat split.** Engine combat (state, AI, headless turn loop, damage
  formula) has lived in `engine/combat.c`. The rendered combat loop
  (`RunCombat`, modal input, target picker, per-frame present) has lived in
  `src/combat_loop.c` (shell). Both have used the same `Combat` struct
  (defined in `engine/include/combat.h`).
- **Assets split.** `LoadAssetBytes` (byte-level reads from the pack stack)
  has been engine. `LoadAssetTexture` (raylib `Texture2D` loader) has been
  shell. The engine has never touched GPU textures.
- **flows.h is clean.** `show_win_game` has taken no `RenderTexture2D *`:
  render-side concerns (the win cartoon) have been invoked separately by the
  host before calling `show_win_game`.
