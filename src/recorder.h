#ifndef OB_RECORDER_H
#define OB_RECORDER_H

#include <stdbool.h>

#include "game.h"
#include "map.h"
#include "fog.h"

// Gameplay-movie recorder. When enabled via recorder_init(), each
// recorder_capture() call writes one state-JSON + framebuffer-PNG pair
// into a hidden temp directory; on shutdown, the encoder muxes those
// PNGs into the configured output .mp4.
//
// When NOT initialized, every recorder_capture() call is a free no-op
// (one branch). The 45 call sites scattered through engine + shell
// stay always-on; the recorder decides whether to do work.

// Initialize the recorder. `out_mp4_path` is where the final .mp4 lands
// at shutdown. The recorder picks its own temp directory for the
// intermediate per-tick files (deleted after the encode). Idempotent:
// a second call is a no-op.
void recorder_init(const char *out_mp4_path);

// Stop recording, encode the intermediate frames into the configured
// .mp4, delete the temp directory. Safe to call when never initialized.
// Returns true on a successful encode, false otherwise (no frames, no
// init, encode failure). The render-side caller is responsible for
// running the user-facing "Encoding..." dialog around this; see
// recorder_temp_dir() + the encode dialog wiring in main.c.
bool recorder_shutdown(void);

// True iff recorder_init() has been called and shutdown hasn't.
bool recorder_active(void);

// Attach live state pointers used by recorder_capture() to serialize
// state JSON. Pass NULL to detach. Captures before attach are no-ops.
void recorder_attach_state(Game *g, const Map *map, const Fog *fog);
void recorder_attach_render_target(void *rt);   // RenderTexture2D *

// Capture: serializes Game/Map/Fog to JSON, reads back framebuffer to
// PNG, writes both to the temp dir under tick_NNNNNN.{json,png}, and
// appends a manifest line. No-op when recorder isn't active or state
// isn't attached. `trigger` is a short tag like "step:right" for the
// manifest.
void recorder_capture(const char *trigger);

// Path of the active temp directory (NULL when not recording). The
// encoder reads this to find the intermediate frames; the shutdown
// step deletes it.
const char *recorder_temp_dir(void);

// Path of the final .mp4 output (NULL when not recording).
const char *recorder_output_path(void);

#endif
