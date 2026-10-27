// src/cli_flags.c -- the command-line flags, listed once (cli_flags.h).

#include "cli_flags.h"
#include "autoplay.h"        // AUTOPLAY_HERO_DIFFICULTY
#include "shell_autoplay.h"  // AUTOPLAY_SPEED_*
#include "version.h"

#include <stdlib.h>
#include <string.h>

const CliFlag cli_flags[] = {
    { "--version",        "-v", NULL,             "print the build number and exit" },
    { "--help",           "-h", NULL,             "print this list and exit" },
    { "--fullscreen",     NULL, NULL,             "start fullscreen" },
    { "--pack",           NULL, "<name|path>",    "play this pack: a discovered pack's name, or a path" },
    { "--lang",           NULL, "<code>",         "load the pack's strings/<code>.json" },
    { "--save-dir",       NULL, "<dir>",          "use <dir> for saves and discovered packs" },
    { "--seed",           NULL, "<0-255>",        "play that catalog world" },
    { "--movie",          NULL, "[<path>]",       "record the session to an MP4" },
    { "--debug",          NULL, NULL,             "add the Debug page of cheats to the game menu" },
    { "--gallery",        NULL, "<dir>",          "capture every modern screen to <dir> and exit" },
    { "--intro-movie",    NULL, "<out.mp4>",      "render the Introduction to an MP4 and exit" },
    { "--puzzle-sweep",   NULL, "<dir>",          "capture the puzzle view of all 256 worlds and exit" },
    { "--window",         NULL, "<WxH>",          "open the window at that size" },
    { "--touch",          NULL, NULL,             "run as a touch device" },
    { "--demo",           NULL, NULL,             "the demo player plays the game" },
    { "--autoplay",       NULL, NULL,             "the winnability oracle plays the game" },
    { "--autoplay-hero",  NULL, "=<class>",       "the class autoplay and --validate-pack play" },
    { "--autoplay-level", NULL, "=<easy|normal|hard|impossible>", "the difficulty they play" },
    { "--autoplay-speed", NULL, "=<slow|normal|fast>", "visible autoplay's replay pace" },
    { "--validate-pack",  NULL, "[LO [HI]]",      "autoplay over catalog worlds LO..HI, as a report" },
    { "--headless",       NULL, NULL,             "with --demo or --autoplay: no window" },
    { "--verbose",        NULL, NULL,             "with --demo or --autoplay: diagnostics" },
    { "--extract",        NULL, NULL,             "build a pack from KB.EXE and exit" },
    { "--out-dir",        NULL, "<dir>",          "with --extract: write a loose tree to <dir>" },
    { "--pack-dir",       NULL, "<src> <dst>",    "zip the asset tree <src> into the pack <dst> and exit" },
};
const int cli_flag_count = (int)(sizeof cli_flags / sizeof cli_flags[0]);

void cli_flags_print_help(FILE *out, const char *version) {
    fprintf(out, "openbounty build %s\nUsage: openbounty [flags]\n\n", version);
    for (int i = 0; i < cli_flag_count; i++) {
        const CliFlag *f = &cli_flags[i];
        char left[80];
        snprintf(left, sizeof left, "%s%s%s%s%s",
                 f->alias ? f->alias : "", f->alias ? ", " : "", f->flag,
                 f->arg && f->arg[0] != '=' ? " " : "", f->arg ? f->arg : "");
        if (strlen(left) > 30) fprintf(out, "  %s\n  %-30s %s\n", left, "", f->help);
        else                   fprintf(out, "  %-30s %s\n", left, f->help);
    }
}

void cli_options_init(CliOptions *o) {
    memset(o, 0, sizeof *o);
    o->seed_index = -1;
    o->autoplay_level = AUTOPLAY_HERO_DIFFICULTY;
    o->autoplay_speed = AUTOPLAY_SPEED_NORMAL;
    o->vp_lo = 0;
    o->vp_hi = 255;
}

int cli_parse(int argc, char **argv, CliOptions *o) {
    cli_options_init(o);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            printf("openbounty build %s\n", OPENBOUNTY_VERSION);
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            cli_flags_print_help(stdout, OPENBOUNTY_VERSION);
            return 0;
        // Strict processing: an argument that does not make sense stops the
        // program. A flag needing a value with none, a bad value, an unknown
        // flag, or a stray token all print an error to stderr and exit 2 --
        // nothing runs on a misunderstood command line.
        } else if (strcmp(a, "--fullscreen") == 0) {
            o->want_fullscreen = true;
        } else if (strcmp(a, "--pack") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --pack requires <name|path>\n"); return 2; }
            o->pack_arg = argv[++i];
        } else if (strcmp(a, "--lang") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --lang requires <code>\n"); return 2; }
            o->lang_arg = argv[++i];
        } else if (strcmp(a, "--extract") == 0) {
            o->extract_mode = true;
        } else if (strcmp(a, "--out-dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --out-dir requires <dir>\n"); return 2; }
            o->extract_out_dir = argv[++i];
        } else if (strcmp(a, "--debug") == 0) {
            // The Debug page of cheats in the modern game menu. Without this
            // flag no cheat is reachable.
            o->debug_flag = true;
        } else if (strcmp(a, "--movie") == 0) {
            o->movie_requested = true;
            // Optional next-arg path: only consumed if it doesn't look
            // like another flag (no leading "-"). Without an arg, the
            // recorder picks an auto-named timestamp file.
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                o->movie_path_arg = argv[++i];
            }
        } else if (strcmp(a, "--seed") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --seed requires 0-255\n"); return 2; }
            const char *sv = argv[++i];
            char *end = NULL;
            long n = strtol(sv, &end, 10);
            if (end == sv || *end != '\0' || n < 0 || n > 255) {
                fprintf(stderr, "openbounty: --seed '%s' is not in range 0-255\n", sv);
                return 2;
            }
            o->seed_index = (int)n;
        } else if (strcmp(a, "--demo") == 0) {
            o->demo_mode = true;
        } else if (strcmp(a, "--autoplay") == 0) {
            o->autoplay_mode = true;
        } else if (strncmp(a, "--autoplay-hero=", 16) == 0) {
            o->autoplay_hero = a + 16;   // validated against the pack after load
            if (!o->autoplay_hero[0]) { fprintf(stderr, "openbounty: --autoplay-hero requires a class id\n"); return 2; }
        } else if (strncmp(a, "--autoplay-level=", 17) == 0) {
            const char *lv = a + 17;
            if      (strcmp(lv, "easy") == 0)       o->autoplay_level = 0;
            else if (strcmp(lv, "normal") == 0)     o->autoplay_level = 1;
            else if (strcmp(lv, "hard") == 0)       o->autoplay_level = 2;
            else if (strcmp(lv, "impossible") == 0) o->autoplay_level = 3;
            else { fprintf(stderr, "openbounty: --autoplay-level '%s' is not easy|normal|hard|impossible\n", lv); return 2; }
        } else if (strncmp(a, "--autoplay-speed=", 17) == 0) {
            const char *sp = a + 17;
            if      (strcmp(sp, "slow") == 0)   o->autoplay_speed = AUTOPLAY_SPEED_SLOW;
            else if (strcmp(sp, "normal") == 0) o->autoplay_speed = AUTOPLAY_SPEED_NORMAL;
            else if (strcmp(sp, "fast") == 0)   o->autoplay_speed = AUTOPLAY_SPEED_FAST;
            else { fprintf(stderr, "openbounty: --autoplay-speed '%s' is not slow|normal|fast\n", sp); return 2; }
        } else if (strcmp(a, "--validate-pack") == 0) {
            o->validate_pack = true;
            // Optional LO [HI] range (numeric next tokens); default 0..255.
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *e = NULL;
                long lo = strtol(argv[++i], &e, 10);
                long hi = lo;
                if (*e != '\0') { fprintf(stderr, "openbounty: --validate-pack LO must be 0-255\n"); return 2; }
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    char *e2 = NULL;
                    hi = strtol(argv[++i], &e2, 10);
                    if (*e2 != '\0') { fprintf(stderr, "openbounty: --validate-pack HI must be 0-255\n"); return 2; }
                }
                if (lo < 0 || hi > 255 || lo > hi) { fprintf(stderr, "openbounty: --validate-pack range must be 0-255 with LO<=HI\n"); return 2; }
                o->vp_lo = (int)lo;
                o->vp_hi = (int)hi;
            }
        } else if (strcmp(a, "--gallery") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --gallery requires <dir>\n"); return 2; }
            o->gallery_dir = argv[++i];
        } else if (strcmp(a, "--intro-movie") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --intro-movie requires <out.mp4>\n"); return 2; }
            o->intro_movie = argv[++i];
        } else if (strcmp(a, "--puzzle-sweep") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --puzzle-sweep requires <dir>\n"); return 2; }
            o->puzzle_sweep_dir = argv[++i];
        } else if (strcmp(a, "--window") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --window requires <WxH>\n"); return 2; }
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                o->want_win_w = w; o->want_win_h = h;
            } else {
                fprintf(stderr, "openbounty: --window wants WxH, e.g. --window 1125x553\n");
                return 2;
            }
        } else if (strcmp(a, "--touch") == 0) {
            o->force_touch = true;
        } else if (strcmp(a, "--headless") == 0) {
            o->headless_mode = true;
        } else if (strcmp(a, "--verbose") == 0) {
            o->verbose_mode = true;
        } else if (strcmp(a, "--save-dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --save-dir requires <dir>\n"); return 2; }
            o->save_dir = argv[++i];
        } else if (strcmp(a, "--pack-dir") == 0) {
            if (i + 2 >= argc) { fprintf(stderr, "openbounty: --pack-dir requires <src_dir> <out_zip>\n"); return 2; }
            o->pack_dir_src = argv[++i];
            o->pack_dir_dst = argv[++i];
        } else {
            fprintf(stderr, "openbounty: unknown option '%s'\n"
                            "Try --help for usage.\n", a);
            return 2;
        }
    }

    return -1;
}
