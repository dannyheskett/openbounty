// Winnability, via the autoplay oracle (GB-310..GB-313).
//
// Run OUT OF PROCESS against the game binary, never linked in. A sweep can run
// for hours and the oracle is doing a deep search; a hang or a crash in there
// must not take the editor's unsaved work with it. That is the whole reason for
// the pipe.
//
// Results are advisory (GB-320). They are stamped with the pack's state when
// measured, so a stale result cannot be mistaken for a fresh one.

// popen/fileno are POSIX, not C99, and the project builds with -std=c99.
#define _POSIX_C_SOURCE 200809L

#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define gb_popen  _popen
#  define gb_pclose _pclose
#else
#  include <fcntl.h>
#  include <unistd.h>
#  define gb_popen  popen
#  define gb_pclose pclose
#endif

struct GbOracle {
    FILE *pipe;
    bool  running;
    char  line[512];
    int   at;
    GbOracleResult res;
};

static GbOracle O;

// The game binary sits next to the editor in a build tree, and next to it in a
// release archive. Try both rather than making the user find it.
static bool find_game_binary(char *out, size_t n) {
    const char *candidates[] = {
        "./build/release/openbounty",
        "./build/openbounty",
        "./openbounty",
        "./openbounty.exe",
    };
    for (unsigned i = 0; i < sizeof candidates / sizeof *candidates; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) { fclose(f); snprintf(out, n, "%s", candidates[i]); return true; }
    }
    return false;
}

bool gb_oracle_start(const char *pack_root, int lo, int hi,
                     char *err, size_t errsz) {
    if (O.running) {
        snprintf(err, errsz, "A check is already running.");
        return false;
    }
    char bin[GB_PATH_MAX];
    if (!find_game_binary(bin, sizeof bin)) {
        snprintf(err, errsz,
                 "Could not find the openbounty game binary.\n\n"
                 "The winnability check runs the game's own oracle, so the "
                 "game needs to be next to GameBuilder.");
        return false;
    }
    memset(&O.res, 0, sizeof O.res);
    O.res.lo = lo;
    O.res.hi = hi;

    char cmd[GB_PATH_MAX * 2];
    snprintf(cmd, sizeof cmd, "\"%s\" --validate-pack %d %d --pack \"%s\" 2>&1",
             bin, lo, hi, pack_root);
    O.pipe = gb_popen(cmd, "r");
    if (!O.pipe) {
        snprintf(err, errsz, "Could not start the winnability check.");
        return false;
    }
#ifndef _WIN32
    // Non-blocking, so the editor keeps drawing while the sweep runs. A sweep
    // can take hours; freezing the UI for it would be unusable.
    int fd = fileno(O.pipe);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    O.running = true;
    O.at = 0;
    return true;
}

// Parse one finalized row of the sweep table. Seeds report as they finish, so
// progress is real rather than a spinner.
static void consume_line(const char *s) {
    if (strstr(s, "SOLVED") || strstr(s, "NOT-SOLVED")) {
        if (O.res.rows < GB_ORACLE_MAX_ROWS) {
            GbOracleRow *r = &O.res.row[O.res.rows++];
            r->solved = (strstr(s, "NOT-SOLVED") == NULL);
            snprintf(r->text, sizeof r->text, "%s", s);
            if (r->solved) O.res.solved++; else O.res.failed++;
        }
    }
    if (strstr(s, "PASS")) O.res.verdict = 1;
    if (strstr(s, "FAIL")) O.res.verdict = 2;
}

bool gb_oracle_poll(void) {
    if (!O.running) return false;
    for (;;) {
        int c = fgetc(O.pipe);
        if (c == EOF) break;
        if (c == '\n' || c == '\r') {
            if (O.at > 0) {
                O.line[O.at] = 0;
                consume_line(O.line);
                O.at = 0;
            }
            continue;
        }
        if (O.at < (int)sizeof O.line - 1) O.line[O.at++] = (char)c;
    }
    if (feof(O.pipe)) {
        gb_pclose(O.pipe);
        O.pipe = NULL;
        O.running = false;
        O.res.finished = true;
    }
    clearerr(O.pipe ? O.pipe : stdin);
    return O.running;
}

void gb_oracle_stop(void) {
    if (O.pipe) { gb_pclose(O.pipe); O.pipe = NULL; }
    O.running = false;
}

bool gb_oracle_running(void) { return O.running; }
const GbOracleResult *gb_oracle_result(void) { return &O.res; }
