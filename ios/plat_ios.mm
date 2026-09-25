// The state UIKit publishes and the game reads (ios/plat_ios.h), plus the
// frame clock. Objective-C++ only for CACurrentMediaTime; everything else here
// is plain C and could move if that ever mattered.
//
// Threading: the app shell writes from the main thread, the game thread reads.
// Every value is a word-sized scalar written in one store, and the game is
// tolerant of reading a frame-old touch or size -- the same tolerance it has
// for raylib's own once-a-frame input snapshot -- so no lock is taken. The one
// thing that must not tear is the touch position, which is why it is published
// as a packed pair under a sequence counter rather than two independent ints.

#import <QuartzCore/QuartzCore.h>

extern "C" {
#include "plat_ios.h"
}

#include <stdatomic.h>

static _Atomic int s_w = 1, s_h = 1;
static _Atomic int s_origin_x = 0, s_origin_y = 0;
static _Atomic bool s_active = true;

// touch: (down, x, y) published together. The reader retries while the
// sequence is odd or changed under it, so it never sees half an update.
static _Atomic unsigned s_touch_seq;
static int  s_touch_x, s_touch_y;
static bool s_touch_down;

static _Atomic double s_delta;
static double s_start_time;

void plat_ios_set_screen(int w, int h, int origin_x, int origin_y, float scale) {
    (void)scale;
    atomic_store(&s_w, w > 0 ? w : 1);
    atomic_store(&s_h, h > 0 ? h : 1);
    atomic_store(&s_origin_x, origin_x);
    atomic_store(&s_origin_y, origin_y);
}

// A press the game has not sampled yet. The game looks once a frame, and a
// light tap can begin and end between two looks; the press is kept until a
// read has seen a contact, so no tap is ever shorter than a frame.
static _Atomic bool s_unread_press;

void plat_ios_set_touch(bool down, int x, int y) {
    unsigned seq = atomic_load(&s_touch_seq);
    atomic_store(&s_touch_seq, seq + 1);        // odd: update in progress
    if (down) atomic_store(&s_unread_press, true);
    s_touch_down = down;
    // Into the game's space: it draws inside the safe area, so a contact is
    // measured from the same origin the frame is.
    s_touch_x = x - atomic_load(&s_origin_x);
    s_touch_y = y - atomic_load(&s_origin_y);
    atomic_store(&s_touch_seq, seq + 2);        // even: settled
}

void plat_ios_set_active(bool active) { atomic_store(&s_active, active); }

void plat_ios_tick(double delta) {
    if (s_start_time == 0.0) s_start_time = CACurrentMediaTime();
    atomic_store(&s_delta, delta > 0.0 ? delta : 1.0 / 60.0);
}

void plat_ios_screen(int *w, int *h) {
    if (w) *w = atomic_load(&s_w);
    if (h) *h = atomic_load(&s_h);
}

bool plat_ios_touch(int *x, int *y) {
    for (;;) {
        unsigned a = atomic_load(&s_touch_seq);
        if (a & 1u) continue;                   // mid-update, try again
        bool down = s_touch_down;
        int tx = s_touch_x, ty = s_touch_y;
        unsigned b = atomic_load(&s_touch_seq);
        if (a != b) continue;                   // changed under us
        // A press that came and went since the last read is reported once as
        // a contact, so the game sees its press edge and, next read, its
        // release.
        bool unread = atomic_exchange(&s_unread_press, false);
        if (!down && unread) down = true;
        if (x) *x = tx;
        if (y) *y = ty;
        return down;
    }
}

bool plat_ios_active(void) { return atomic_load(&s_active); }

double plat_ios_time(void) {
    if (s_start_time == 0.0) return 0.0;
    return CACurrentMediaTime() - s_start_time;
}

double plat_ios_delta(void) { return atomic_load(&s_delta); }

// ---------------------------------------------------------------------------
// Bundle and Documents access, for src/plat_ios.c. Foundation lives here, so
// the shell's half stays plain C like its Android counterpart.
// ---------------------------------------------------------------------------

#import <Foundation/Foundation.h>

#include <unistd.h>
#include <os/log.h>

#include <stdlib.h>
#include <string.h>

// The game reports everything it does on stdout -- the pack it opened, the
// seed, every fatal. On iOS that goes nowhere a developer or a CI job can
// read, so stdout is piped into os_log, one line at a time. This
// is the ONLY way the app is observable on a device.
extern "C" void plat_ios_log_stdout(void) {
    static bool started = false;
    if (started) return;
    started = true;

    int fds[2];
    if (pipe(fds) != 0) return;
    // STDOUT ONLY. Redirecting stderr as well feeds this thread's own output
    // back into the pipe -- NSLog writes to stderr -- and the line count
    // doubles every pass until the log is useless. Which is exactly what it
    // did the first time.
    dup2(fds[1], STDOUT_FILENO);
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rd = fds[0];
    [NSThread detachNewThreadWithBlock:^{
        char buf[1024];
        size_t used = 0;
        for (;;) {
            ssize_t n = read(rd, buf + used, sizeof buf - used - 1);
            if (n <= 0) break;
            used += (size_t)n;
            buf[used] = '\0';
            char *line = buf, *nl;
            while ((nl = strchr(line, '\n')) != NULL) {
                *nl = '\0';
                // os_log, not NSLog: os_log writes to the unified log alone,
                // where NSLog also writes to stderr -- which is how the loop
                // above would come back.
                os_log(OS_LOG_DEFAULT, "openbounty: %{public}s", line);
                line = nl + 1;
            }
            used = strlen(line);
            memmove(buf, line, used + 1);
            if (used >= sizeof buf - 2) {   // a line longer than the buffer
                os_log(OS_LOG_DEFAULT, "openbounty: %{public}s", buf);
                used = 0;
            }
        }
    }];
}

extern "C" char *plat_ios_documents_dir(void) {
    @autoreleasepool {
        NSArray *dirs = NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES);
        if (dirs.count == 0) return NULL;
        const char *utf8 = [dirs[0] UTF8String];
        if (!utf8) return NULL;
        return strdup(utf8);
    }
}

extern "C" unsigned char *plat_ios_bundle_file(const char *name, int *out_size) {
    if (out_size) *out_size = 0;
    if (!name) return NULL;
    @autoreleasepool {
        NSString *res = [NSString stringWithUTF8String:name];
        NSString *path = [[NSBundle mainBundle] pathForResource:res ofType:nil];
        if (!path) return NULL;
        NSData *data = [NSData dataWithContentsOfFile:path];
        if (!data || data.length == 0) return NULL;
        unsigned char *buf = (unsigned char *)malloc(data.length);
        if (!buf) return NULL;
        memcpy(buf, data.bytes, data.length);
        if (out_size) *out_size = (int)data.length;
        return buf;
    }
}
