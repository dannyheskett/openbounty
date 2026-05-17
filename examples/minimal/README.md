# Minimal external-consumer example

A standalone program that links `libobengine.a` and drives the engine
via the public API. No raylib, no shell code, no window.

## Build

From this directory:

```
make
```

That builds `minimal` against `../../build/libobengine.a` (which the
repo's root `make` must have built first).

## Run

```
make run
```

Output:

```
Hero Hero the Knight, seed=42, starting at (11, 58) in continentia
After 10 attempted east steps: position (12, 58), 1 succeeded, gold=7500, days_left=600
```

## What this demonstrates

- **Linking the engine library alone.** The `gcc` invocation includes
  only `libobengine.a` + `engine/host_noop.c` (default no-op
  implementations of the host callbacks) + `-lm -lpthread`. No raylib,
  no X11, no audio device.
- **Public API surface.** Every header included by `main.c`
  (`game.h`, `map.h`, `fog.h`, `pack.h`, `resources.h`, `step.h`) is
  under `engine/include/`. No `src/` includes.
- **Self-contained library.** cJSON and miniz are vendored inside
  `libobengine.a`, so consumers don't need to provide them.

## Adapting this for your own consumer

1. Copy `main.c` and `Makefile` to your project root (alongside your
   own source).
2. Adjust paths in the Makefile if `libobengine.a` lives elsewhere.
3. Replace `engine/host_noop.c` with your own implementations of the
   `engine/include/ui_host.h` callbacks if you need real UI behavior.
4. Add your own game-loop logic on top of `step_try`,
   `combat_run_headless`, `state_build_snapshot`, etc.
