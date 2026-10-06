# Architecture

How the parts of openbounty have fitted together. `engine/README.md` has
covered the engine library in detail, `OPENBOUNTY-SPEC.md` every rule, and
`GLOSSARY.md` the terms used here.

## The layers

```
 src/  (shell)          window, rendering, audio, input, screens, CLI
   │   ├── src/shell_demo.c      ──►  demo/      the human-like player
   │   └── src/shell_autoplay.c  ──►  autoplay/  the winnability oracle
   ▼
 engine/  (libobengine.a)   game state and every rule; no raylib
   ▲
 tools/   the pack extractor (C) and the art and map tools (Python)
```

- **Engine** (`engine/`): the game. State (`Game`, `engine/include/game.h`),
  movement (`GameStep`, `engine/step.c`), encounters and flows
  (`engine/flows.c`, `engine/flow_resolve.c`), combat (`engine/combat.c`),
  magic, saves and pack loading. It has built as the static archive
  `libobengine.a` with cJSON and miniz inside, and has linked with only
  `-lm -lpthread`. It has included nothing from `src/`, `demo/` or
  `autoplay/`; the library-boundary check in `make all` has failed the build
  whenever it has.
- **Shell** (`src/`): everything a player sees and touches. It has reached
  the platform only through five seams (`src/gfx.h`, `src/frame_host.h`,
  `src/input_host.h`, `src/audio_backend.h`, `src/font_backend.h`): raylib
  has implemented them on desktop, web and Android, and `ios/` natively on
  iOS (`IOS-BACKEND.md`).
- **Demo** (`demo/`) and **autoplay** (`autoplay/`): two players that have driven
  the engine without the shell's UI. Each has included only engine headers,
  and neither has included the other. The shell has reached each through one
  adapter file.
- **Tools** (`tools/`): `--extract` (the King's Bounty pack from `KB.EXE`) and
  the Glory of Rome art and map tools (`ART-PIPELINE.md`).

## One step of play

1. The shell has read input (`src/input.c`) and turned a move into
   `GameStep`.
2. `GameStep` has moved the hero, revealed the fog, handled what has stood
   on the tile (`engine/adventure.c` has classified it), moved the foes and ticked
   the day.
3. Anything the player must see or answer has been raised on the
   **player-IO queue** (`engine/include/player_io.h`), a FIFO inside the
   `Game`: decisions, messages and full-screen views, in order. A decision's
   payload has sat in the pending-flow scratch (`engine/include/pending.h`).
4. The shell has drawn the front request and taken the player's answer; the
   demo and autoplay have answered the same queue in code. The answer has gone
   back through `player_io_answer`, and the engine has applied it
   (`engine/flow_resolve.c`).
5. A fight has run in the engine (`combat_run_headless` and its variants);
   the shell's `RunCombat` (`src/combat_loop.c`) has drawn it and fed it the
   player's orders over the same `Combat` struct.

Because the human and the agents have answered the one queue, headless and
visible play have stayed the same game.

## Packs

A pack has been the game's content: `game.json`, `strings/`, `art/`,
`audio/`, `maps/`, as a loose directory or a `.openbounty` zip
(`PACK-FORMAT.md`). The engine has carried no text: every string has come
from the pack.

- **Finding one:** `pack_discover` (`engine/pack.c`) has searched the working
  directory, the user data directory and `<exe-dir>/assets`; `--pack` has
  named one.
- **Reading it:** opened packs have formed a stack, read top-down
  (`pack_stack_read`), and `resources_load` (`engine/resources.c`) has parsed
  `game.json` into one `Resources`, published to the catalog lookups in
  `engine/tables.h`.
- **Two presentations:** `render.mode` has chosen the presentation, `legacy`
  (King's Bounty's 320×200 screens, reproduced exactly) or `modern` (Glory
  of Rome's). The shell has read it through `CL_IS_MODERN` (`src/layout.h`).
  It has chosen how the game has looked, never its rules: every gameplay
  difference between the packs has been a `game.json` key the engine has
  read.
- **The legacy freeze:** the legacy presentation has been finished, and
  `tests/unit/test_legacy_freeze.c` has pinned its geometry and logic.

## Determinism

A world has been a pure function of its seed. A catalog world (`--seed N`,
`0`–`255`) has expanded its index to the full seed (`GameSeedFromIndex`).
Placements have been salted from the seed (`salt_continent`), a chest's
contents from the seed and its tile, and a fight's dice from the seed and the
foe's identity (`combat_seed_rng`). So a save has reproduced its world, and
autoplay has predicted a fight on a copy of the world and then won it live.

## Saves

`SaveGameWrite` and `SaveGameRead` (`engine/savegame.c`, the writer in
`engine/state_serialize.c`) have stored the `Game` and the fog as JSON; the
map has been reloaded from the pack. `OPENBOUNTY-SPEC.md` §27 has given the
schema and when `SAVE_VERSION` has changed.

## Where the rest has lived

| Area | Files | Doc |
|---|---|---|
| Rules and state | `engine/*.c`, `engine/include/*.h` | `OPENBOUNTY-SPEC.md` |
| Modern screens | `src/modern/`, `src/screens/` | `DESIGN-SPEC.md` |
| Legacy screens | `src/legacy/`, `src/screens/` | `OPENBOUNTY-SPEC.md` |
| Pack content | `assets/<pack>/` | `PACK-FORMAT.md`, `GLORY-OF-ROME.md` |
| Rome art | `art/`, `tools/romeart.py` | `ART-PIPELINE.md`, `ART-SPEC.md` |
| Demo player | `demo/` | `DEMO-SPEC.md` |
| Autoplay | `autoplay/` | `AUTOPLAY-SPECS.md` |
| iOS backend | `ios/` | `IOS-BACKEND.md` |
| Tests | `tests/` | `TESTING.md` |
| Releases | `.github/workflows/release.yml` | `RELEASE-PROCESS.md` |
