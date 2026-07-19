// src/shell_autoplay.h
//
// Visible autoplay (AP-024): the one shell adapter that knows autoplay
// (dependency arrow shell -> autoplay -> engine), mirroring src/shell_demo.c.
// The run is resolved headlessly first (the same single planner call on a
// separate world); this adapter replays the recording on the live shell world
// through plan_exec_step at a watchable pace, with fights animated from their
// CombatTurnRecord before the identical resolution is applied.

#ifndef OB_SHELL_AUTOPLAY_H
#define OB_SHELL_AUTOPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "shell_ctx.h"

// Visible replay pacing (--autoplay-speed). All three show EVERYTHING -- every
// move, town screen, combat cartoon, and read dwell -- only the pace differs:
// SLOW lingers, NORMAL is the default, FAST steps once per frame with brief
// dwells (the quickest pace that still animates every step).
typedef enum {
    AUTOPLAY_SPEED_NORMAL = 0,
    AUTOPLAY_SPEED_SLOW,
    AUTOPLAY_SPEED_FAST,
} AutoplaySpeed;

// Resolve the run headlessly (fresh world from the same pack + seed + hero +
// difficulty), keep the recording, and arm the replay at `speed`. False on
// setup failure (the caller drops to manual play).
bool shell_autoplay_begin(ShellCtx *ctx, int seed_index, const char *pack_dir,
                          const char *hero_id, int difficulty,
                          AutoplaySpeed speed);

// Apply the next recorded primitive at the paced beat. Sets *out_done when
// the replay is exhausted. Returns true while visible autoplay owns the frame.
bool shell_autoplay_tick(ShellCtx *ctx, double now_s, bool *out_done);

void shell_autoplay_summary(const ShellCtx *ctx, char *out, int cap);
void shell_autoplay_end(void);

// True when the last begin was cancelled from its progress screen (ESC or
// window-close during the resolve) -- the caller should quit, not play manually.
bool   shell_autoplay_cancelled(void);

// True while visible autoplay owns the frames; the pacing the combat animator
// scales off when demo mode is not the driver.
bool   shell_autoplay_active(void);
double shell_autoplay_step_delay(void);
double shell_autoplay_read_dwell(void);

#endif
