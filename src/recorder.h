#ifndef OB_RECORDER_H
#define OB_RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "combat.h"

// In-memory ring of (state JSON + framebuffer PNG) pairs, captured at
// canonical mutation sites (one capture per "tick" — the game is
// turn-based so emits fire on user-driven state changes, not per
// render frame).
//
// Default mode is memory-only with a bounded ring (entries + bytes).
// Disk dump is enabled by recorder_set_record_dir() — typically wired
// to a --record CLI flag; without it, captures stay in memory and the
// oldest is evicted on overflow.

typedef struct {
    uint64_t       seq;            // monotonic, starts at 1
    uint64_t       ms;             // wall-clock ms since recorder_init
    char          *trigger;        // owned, e.g. "step:right"
    char          *snap_path;      // owned, "" if no snap was associated
    char          *state_json;     // owned, full snapshot JSON
    unsigned char *frame_png;      // owned, NULL if RT not attached
    int            frame_png_len;
} TickRecord;

// Lifecycle. cap_entries <= 0 → default 16. cap_bytes 0 → default 8 MB.
// Idempotent.
void recorder_init(int cap_entries, size_t cap_bytes);
void recorder_shutdown(void);
bool recorder_active(void);

// Wire dependencies (all may be NULL to detach).
void recorder_attach_state(Game *g, const Map *map, const Fog *fog);
void recorder_attach_combat(const Combat *c);
void recorder_attach_render_target(void *rt);   // RenderTexture2D *

// Disk dump configuration. When non-NULL, every capture also writes
// tick_NNNNNN.json + tick_NNNNNN.png and appends a manifest line. NULL
// disables disk writes (default).
void recorder_set_record_dir(const char *dir);

// Capture using attached pointers. No-op if Game has not been attached
// (pre-game startup). Builds a state JSON snapshot and (if a render
// target is attached) a framebuffer PNG, pushes to ring, evicts
// oldest entries while either cap is exceeded.
void recorder_capture(const char *trigger);

// Most recent entry, or NULL. Read-only.
const TickRecord *recorder_last(void);

// Number of entries currently held / total ever captured.
int recorder_count(void);
uint64_t recorder_total(void);

// Tag a path to associate with the next capture (mirrors the harness
// `snap:` flow). One pending path at a time.
void recorder_pending_snap(const char *path);

// On-demand dump of the current ring to <dir>/manifest.ndjson +
// per-tick files. Returns number of entries written, or -1 on error.
int recorder_dump_ring(const char *dir);

// Suggested default dump dir under screenshots/. Caller-provided
// buffer; returns the buffer or NULL on truncation. Does not create
// directories.
char *recorder_default_dump_dir(char *buf, size_t cap);

#endif
