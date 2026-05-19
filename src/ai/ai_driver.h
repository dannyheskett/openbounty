// src/ai/ai_driver.h
//
// In-process AI driver. When --ai is set on the command line, main.c
// constructs an AiDriver after the game is loaded and ticks it once
// per frame before reading keyboard input. The driver observes the
// Game/Map/Fog directly and emits actions through the existing
// dispatch helpers (no synthetic key events).
//
// Driver lifecycle:
//   ai_init       — allocate state, open the trace file (if requested)
//   ai_tick       — once per frame; may dismiss dialogs, respond to
//                   prompts, step, cast spells, etc. Returns true if
//                   it consumed the frame (so main.c skips its own
//                   input poll).
//   ai_shutdown   — flush trace, free state
//
// The driver is a state machine. Each frame:
//   1. If a blocking dialog / view / prompt is up, dismiss or answer it.
//   2. Otherwise, pick a goal and walk one step toward it.
//   3. Log the action taken to the trace.

#ifndef OB_AI_DRIVER_H
#define OB_AI_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

// We need the typedefs, not bare struct tags — Game/Map/Fog/Resources
// are typedef'd from anonymous structs in their headers, so forward
// `struct Game` declarations don't bind to the right type. Just include.
#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "shell_ctx.h"

typedef struct AiDriver AiDriver;

typedef struct {
    const char *trace_path;     // NULL = no trace file
    uint64_t    seed;           // 0 = use game seed
    int         max_ticks;      // 0 = unlimited
    bool        verbose;        // stderr decision log
} AiConfig;

AiDriver *ai_create(const AiConfig *cfg);
void      ai_destroy(AiDriver *d);

// Run one frame of AI. Reads game/map/fog, may mutate game via shell
// helpers and engine calls. Returns true if the driver took over this
// frame (so caller should skip its own input poll); false if the AI
// is idle this frame (caller may proceed normally — useful in mixed
// modes for debugging).
//
// `sctx` is the same ShellDispatchCtx main.c passes to
// shell_dispatch_action — the driver uses it to issue InputActions.
bool ai_tick(AiDriver *d, Game *game, Map *map, Fog *fog,
             const Resources *res, ShellCtx *sctx);

// True if the driver decided the game is done (won, lost, or stuck).
// main.c can read this to break out of the frame loop cleanly.
bool ai_finished(const AiDriver *d);

// Snapshot of the last decision for status overlay / debug HUD.
const char *ai_last_goal(const AiDriver *d);
const char *ai_last_action(const AiDriver *d);

#endif
