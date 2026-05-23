// nanosleep + clock_gettime + struct timespec require POSIX 199309L+;
// 200809L is a superset that's already used elsewhere in this shell.
#define _POSIX_C_SOURCE 200809L
#include "frame_host.h"
#include "input_host.h"
#include "raylib.h"
#include <stddef.h>
#include <time.h>

#define FH_DT (1.0 / 60.0)

static bool   s_test  = false;
static double s_clock = 0.0;
static bool   s_close = false;

static FrameHostBeforeFrameFn s_before_fn   = NULL;
static void                  *s_before_user = NULL;

// Wall-clock throttle for test mode. When > 0, each call to
// frame_host_should_close() sleeps until the previous call's start
// time + (1/fps) seconds, so the simulation matches real time and
// a human can follow it on screen.
static int             s_test_fps = 0;
static struct timespec s_last_frame_wall;
static bool            s_last_frame_wall_valid = false;

void frame_host_set_test_fps(int fps) {
    s_test_fps = fps;
    s_last_frame_wall_valid = false;
}

static void throttle_to_fps(int fps) {
    if (fps <= 0) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (s_last_frame_wall_valid) {
        long target_ns = 1000000000L / fps;
        long elapsed_ns =
            (long)(now.tv_sec - s_last_frame_wall.tv_sec) * 1000000000L +
            (long)(now.tv_nsec - s_last_frame_wall.tv_nsec);
        long sleep_ns = target_ns - elapsed_ns;
        if (sleep_ns > 0) {
            struct timespec ts = {
                .tv_sec  = sleep_ns / 1000000000L,
                .tv_nsec = sleep_ns % 1000000000L,
            };
            nanosleep(&ts, NULL);
            clock_gettime(CLOCK_MONOTONIC, &now);
        }
    }
    s_last_frame_wall = now;
    s_last_frame_wall_valid = true;
}

void frame_host_set_before_frame(FrameHostBeforeFrameFn fn, void *user) {
    s_before_fn   = fn;
    s_before_user = user;
}

void frame_host_use_raylib(void) {
    s_test = false;
}

void frame_host_use_test(void) {
    s_test = true;
    s_clock = 0.0;
    s_close = false;
}

double frame_host_time(void) {
    return s_test ? s_clock : GetTime();
}

bool frame_host_should_close(void) {
    // Test mode: every check of "should we keep looping" doubles as a
    // frame-boundary tick. This advances the input queue's active
    // key, the test clock, and any held-down decay. Game-logic loops
    // call frame_host_should_close once per iteration (replacing the
    // old WindowShouldClose), so this naturally drives test pacing
    // without invasive per-loop changes.
    if (s_test) {
        throttle_to_fps(s_test_fps);
        // Order matters: tick FIRST (clears stale live key, pops one
        // from FIFO if any), THEN before_fn (combat driver may set
        // the live key for THIS tick). Engine input_poll runs after
        // both and sees the latest live key.
        input_host_tick();
        if (s_before_fn) s_before_fn(s_before_user);
        s_clock += FH_DT;
    }
    return s_test ? s_close : WindowShouldClose();
}

void frame_host_tick(void) {
    // Kept for explicit callers; no-op in test mode since
    // frame_host_should_close already advances. In raylib mode this
    // is a no-op too.
    (void)0;
}

void frame_host_request_close(void) {
    s_close = true;
}
