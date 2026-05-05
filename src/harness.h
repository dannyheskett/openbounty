#ifndef BNT_HARNESS_H
#define BNT_HARNESS_H

#include <stdbool.h>

// Headless control harness. When --harness <socket-path> is on the
// command line, openbounty binds an AF_UNIX SOCK_STREAM at that path
// and accepts one short-lived connection per command. The game's
// main loop polls the socket every frame; commands execute, replies
// flush, and the connection closes. The game window is normal and
// visible — the harness drives it from a separate process.
//
// Protocol (one line per command, one line per reply, \n terminated):
//
//   key:NAME            queue one IsKeyPressed-style edge for next frame
//   hold:NAME           queue a sustained IsKeyDown until release
//   release:NAME        end a queued hold
//   char:C              queue one GetCharPressed codepoint (single char)
//   text:STRING         queue each character of STRING via char:
//   frames:N            advance N frames with no input (default 1)
//   snap:PATH           write a PNG of the offscreen RT to PATH; the
//                       path is also recorded against the next captured
//                       state-trace entry so dumps can tie back to images
//   dump[:PATH]         write the in-memory state trace (NDJSON) to
//                       PATH or to an auto-generated screenshots/trace_*
//                       file; reply is "OK <count> <path>"
//   state               reply with one-line JSON of game state
//   quit                set the should-quit flag and exit cleanly
//
// Reply is one line: "OK [payload]" or "ERR [reason]". Empty payload
// is fine. snap and state always include payloads.
//
// The harness needs to know about the offscreen RenderTexture2D and
// the live Game pointer so state and snap can do their work. main.c
// installs those via harness_attach_*().

// These are typedef'd from anonymous structs (typedef struct {...} Foo),
// so we can't usefully forward-declare them as `struct Foo *`. Pull the
// headers — they're small and have no raylib deps that would pollute
// callers (well, map.h does pull raylib via tile.h indirectly... acceptable).
#include "game.h"
#include "combat.h"
#include "map.h"
#include "fog.h"

bool harness_init(const char *socket_path);
void harness_shutdown(void);

// Called every frame from each render loop. Accepts at most one
// connection per frame, processes its command, replies, closes,
// then clears the per-frame input queues. Returns true if a `quit`
// command was received this frame.
//
// Safe to call when --harness was not supplied — turns into a no-op
// because the listen socket isn't bound.
bool harness_tick(void);

// Lower-level: poll a single command without advancing the input
// frame queues. main.c uses this from the top-level adventure loop so
// it can interleave with its own frame-end logic. Most call sites
// should prefer harness_tick().
bool harness_poll(void);

// Used by main.c / combat.c to register the live state pointers so
// `state` and `snap` can serialize them. Pass NULL to clear.
void harness_attach_game(Game *g);
void harness_attach_combat(const Combat *c);
void harness_attach_map(const Map *m);
void harness_attach_fog(const Fog *f);
void harness_attach_render_target(void *render_texture);  // RenderTexture2D*

// Emitted by the input shim each frame so per-frame queues clear.
// Already wired through main.c; harness modules don't need to call.

#endif
