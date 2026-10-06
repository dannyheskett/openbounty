// src/cli_flags.h
//
// The command-line flags, listed once. `--help` prints this table, and a test
// (tests/unit/test_cli_flags.c) checks that README §3's flag table and
// OPENBOUNTY-SPEC REQ-480 name exactly these flags. cli_parse reads each one
// into a CliOptions.

#ifndef OB_CLI_FLAGS_H
#define OB_CLI_FLAGS_H

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char *flag;    // "--seed"
    const char *alias;   // "-v", or NULL
    const char *arg;     // "<0-255>", or NULL for none
    const char *help;    // one line for --help
} CliFlag;

extern const CliFlag cli_flags[];
extern const int     cli_flag_count;

// Print the build line and every flag, one to a line, to `out`.
void cli_flags_print_help(FILE *out, const char *version);

// What the command line asked for. cli_parse fills it; unset fields keep the
// defaults cli_options_init gives.
typedef struct {
    bool        want_fullscreen;   // --fullscreen
    const char *pack_arg;          // --pack <name|path>
    const char *lang_arg;          // --lang <code>: strings/<code>.json
    const char *save_dir;          // --save-dir <dir>
    bool        extract_mode;      // --extract: build the pack from KB.EXE, exit
    const char *extract_out_dir;   // --out-dir <dir>: extract to a loose tree
    const char *pack_dir_src;      // --pack-dir <src> <dst>: zip a loose tree
    const char *pack_dir_dst;
    bool        debug_flag;        // --debug: the Debug page of cheats
    bool        movie_requested;   // --movie [<path>]: record to an MP4
    const char *movie_path_arg;    //   its path; NULL picks <user-data>/movie-<ts>.mp4
    const char *gallery_dir;       // --gallery <dir>: capture every modern screen
    const char *intro_movie;       // --intro-movie <out.mp4>: the Introduction to video
    const char *puzzle_sweep_dir;  // --puzzle-sweep <dir>: the puzzle view of all 256 worlds
    // --window WxH / --touch: a device's geometry, on the desk. The window size
    // drives everything (present_refit derives the buffer from it), and --touch
    // turns on what only a finger turns on.
    int         want_win_w, want_win_h;
    bool        force_touch;
    int         seed_index;        // --seed N: catalog world N; -1 = not asked for
    bool        headless_mode;     // --headless, with --demo or --autoplay
    // --demo: the human-like player plays the live game (demo/, DEMO-SPEC);
    // with --headless, to an ending, printing the [DEMO OVER] report.
    bool        demo_mode;
    // --autoplay: the winnability oracle (autoplay/, AUTOPLAY-SPECS); headless,
    // it drives to its verdict and exits 0 SOLVED, 1 NOT-SOLVED, 2 setup
    // failure; visible, it resolves headlessly and replays on the live world.
    bool        autoplay_mode;
    const char *autoplay_hero;     // --autoplay-hero=<class>; NULL = AUTOPLAY_HERO_CLASS
    int         autoplay_level;    // --autoplay-level=...: the difficulty, and so the day budget
    int         autoplay_speed;    // --autoplay-speed=...: visible replay pace
    bool        validate_pack;     // --validate-pack [LO [HI]]: the winnability report
    int         vp_lo, vp_hi;      //   its catalog range, 0..255 by default
    bool        verbose_mode;      // --verbose: the agent diagnostic channels
} CliOptions;

// The defaults: nothing asked for, seed -1, autoplay at its default level and
// speed, the whole catalog for --validate-pack.
void cli_options_init(CliOptions *o);

// Parse argv into `o` (initialised first). Returns -1 to run the program, or
// an exit code: 0 after --version or --help, 2 after a flag needing a value
// with none, a bad value, an unknown flag or a stray token, each with its
// reason on stderr. Parsing is strict: nothing runs on a misunderstood line.
int cli_parse(int argc, char **argv, CliOptions *o);

#endif
