// src/cli_flags.c -- the command-line flags, listed once (cli_flags.h).

#include "cli_flags.h"

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
