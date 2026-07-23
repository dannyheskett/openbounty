// localtime_r needs POSIX 199506+; 200809L covers everything we use.
#define _POSIX_C_SOURCE 200809L

#include "frame_host.h"
#include "input_host.h"
#include "shell_demo.h"
#include "demo.h"
#include "shell_autoplay.h"
#include "autoplay.h"
#include "diag.h"
#include "shell_run.h"
#include "raylib.h"
#include "recorder.h"
#include "audio.h"
#include "encode_dialog.h"
#include "map.h"
#include "fog.h"
#include "savegame.h"
#include "savepath.h"
#include "game.h"
#include "bfont.h"
#include "sprites.h"
#include "tile_cache.h"
#include "adventure.h"
#include "views.h"
#include "ui.h"
#include "resources.h"
#include "screenshot.h"
#include "pack.h"
#include "pack_select.h"
#include "extract.h"
#include "version.h"
#include "fatal.h"

#include <sys/stat.h>
#include "layout.h"
#include "present.h"
#include "palette.h"
#include "chrome.h"
#include "hud.h"
#include "map_render.h"
#include "overlay.h"
#include "input.h"
#include "prompt.h"
#include "startup.h"
#include "end_cartoon.h"
#include "screens/home_castle.h"
#include "screens/recruit_soldiers.h"
#include "screens/own_castle.h"
#include "screens/dwelling.h"
#include "screens/alcove.h"
#include "screens/end_game.h"
#include "combat.h"
#include "combat_loop.h"
#include <time.h>
#include "views_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Prompt-flow scratch state lives in pending.{c,h} so step.c and the
// main-loop dispatch share the same buffers.
#include "pending.h"
#include "step.h"
#include "flows.h"

// Fast-quit (Ctrl+Q) status-bar prompt lives in shell_fastquit.{c,h}.
// main_fast_quit_active() is provided there for chrome.c to query.
#include "shell_fastquit.h"

// F10 debug cheat menu lives in shell_cheats.{c,h}.
#include "shell_cheats.h"
#include "shell_ctx.h"
#include "shell_promptdispatch.h"
#include "shell_actions.h"
#include "shell_earlyexit.h"

// Adventure spell casting (cast_*, dispatch_adventure_spell, bridge/gate
// continuation state) lives in spells_adventure.{c,h}.
#include "spells_adventure.h"

// Game-menu (Esc / O) callbacks (Save/Load/New/Quit) moved to
// src/shell_menu.{c,h}.
#include "shell_menu.h"

// perform_temp_death() moved to src/shell_tempdeath.{c,h}.
#include "shell_tempdeath.h"

// End-of-week two-screen sequence (astrology -> budget). Implementation
// in src/shell_weekend.{c,h}. Call schedule_week_end() whenever a week
// boundary is crossed; the main loop calls pump_week_end_dialog to pop
// each pending screen before processing input.
#include "shell_weekend.h"


// run_audience_dialog() (King Maximus audience flow) moved to
// src/shell_audience.{c,h}.
#include "shell_audience.h"

// Per-frame draw_frame() dispatcher moved to src/shell_frame.{c,h}.
#include "shell_frame.h"

// Town/Castle Gate destination picker (player_io_message + letter dispatch,
// like the F10 debug menu). Implementation in src/shell_gate.{c,h}.
#include "shell_gate.h"

// ===========================================================================
// --validate-pack: systematic pack winnability report
// ===========================================================================
//
// Runs the headless oracle over a seed range, one seed at a time, and renders
// a terminal table -- a live status line while each seed resolves, one finalized
// row per seed, and a recap. Built for pack AUTHORS: a NOT-SOLVED row names the
// first objective the oracle could not clear and why. No window opens; all the
// oracle's own [AUTOPLAY]/[SEARCH]/[VERDICT] chatter is silenced (ob_diag_quiet)
// so only this table shows.

static double vp_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

// "m:ss" for a millisecond duration.
static void vp_fmt_time(double ms, char *out, int cap) {
    int secs = (int)(ms / 1000.0 + 0.5);
    snprintf(out, (size_t)cap, "%d:%02d", secs / 60, secs % 60);
}

static int    vp_cur_seed;
static double vp_seed_start_ms;

// Progress hook: overwrite one ephemeral status line while a seed resolves.
// Same column widths as the finalized row (below), minus the trailing newline,
// so the line snaps into place when the seed completes.
static bool vp_progress_cb(int done, int total, void *ud) {
    (void)ud;
    char t[16], dn[16], seedc[8];
    vp_fmt_time(vp_now_ms() - vp_seed_start_ms, t, sizeof t);
    snprintf(dn, sizeof dn, "%d/%d", done, total);
    snprintf(seedc, sizeof seedc, "%d", vp_cur_seed);
    printf("\r\033[K%4s  %-11s  %-7s  %4s  %7s  %6s  %-6s",
           seedc, "searching", dn, "", "", "", t);
    fflush(stdout);
    return true;   // no cancel; Ctrl-C aborts the sweep
}

static int validate_pack_run(const char *pack_dir, int lo, int hi,
                             const char *hero, int level) {
    ob_diag_set_quiet(true);
    autoplay_set_progress(vp_progress_cb, NULL);

    // One column layout, shared by the header, divider, data rows, and the
    // in-progress line so every cell sits in the same character positions.
    #define VP_ROW_FMT "%4s  %-11s  %-7s  %4s  %7s  %6s  %-6s  %s\n"
    printf("Validating pack '%s' - seeds %d..%d\n\n", pack_dir, lo, hi);
    printf(VP_ROW_FMT, "seed", "verdict", "done", "days", "score", "moves",
           "time", "first blocker");
    printf(VP_ROW_FMT, "----", "-----------", "-------", "----", "-------",
           "------", "------", "-------------");
    fflush(stdout);

    int solved = 0, total = 0;
    double sum_ms = 0.0;
    long   sum_days = 0, sum_score = 0;

    for (int s = lo; s <= hi; s++) {
        vp_cur_seed = s;
        vp_seed_start_ms = vp_now_ms();
        AutoplayConfig cfg = { s, pack_dir, hero, level };
        AutoplayResult r;
        bool ok = autoplay_run(&cfg, &r);
        double elapsed = vp_now_ms() - vp_seed_start_ms;
        char t[16];
        vp_fmt_time(elapsed, t, sizeof t);
        printf("\r\033[K");   // clear the ephemeral status line
        if (!ok) {
            // Setup failure is seed-independent (bad pack, unknown hero, out
            // of memory): it would repeat for every seed, so stop the sweep.
            // autoplay_run has already printed the specific reason to stderr.
            fprintf(stderr, "openbounty: --validate-pack aborted at seed %d "
                            "(run setup failed)\n", s);
            autoplay_set_progress(NULL, NULL);
            recsink_free();
            return 2;
        }
        total++;
        if (r.solved) solved++;
        sum_ms += elapsed;
        sum_days += r.days_used;
        sum_score += r.score;
        char done[16], seedc[8], daysc[12], scorec[12], movesc[12], blk[192];
        snprintf(done,   sizeof done,   "%d/%d", r.best_done, r.obj_total);
        snprintf(seedc,  sizeof seedc,  "%d", s);
        snprintf(daysc,  sizeof daysc,  "%d", r.days_used);
        snprintf(scorec, sizeof scorec, "%d", r.score);
        snprintf(movesc, sizeof movesc, "%d", r.moves);
        if (r.solved)
            blk[0] = '\0';
        else if (r.unmet_cause[0])
            snprintf(blk, sizeof blk, "%s (%s)", r.unmet_label, r.unmet_cause);
        else
            snprintf(blk, sizeof blk, "%s", r.unmet_label);
        printf(VP_ROW_FMT, seedc, r.solved ? "SOLVED" : "NOT-SOLVED",
               done, daysc, scorec, movesc, t, blk);
        fflush(stdout);
    }

    autoplay_set_progress(NULL, NULL);
    recsink_free();

    // Close the table with the divider again, then a totals row in the same
    // columns: PASS/FAIL (the validation verdict, tied to the exit code), the
    // solved ratio, per-seed AVERAGE days/score/time, and the total wall time.
    printf(VP_ROW_FMT, "----", "-----------", "-------", "----", "-------",
           "------", "------", "-------------");
    char ratio[16], mdays[12], mscore[12], avgt[16], tot[16], leg[24];
    snprintf(ratio,  sizeof ratio,  "%d/%d", solved, total);
    snprintf(mdays,  sizeof mdays,  "%ld", total ? sum_days  / total : 0);
    snprintf(mscore, sizeof mscore, "%ld", total ? sum_score / total : 0);
    vp_fmt_time(total ? sum_ms / total : 0.0, avgt, sizeof avgt);
    vp_fmt_time(sum_ms, tot, sizeof tot);
    snprintf(leg, sizeof leg, "total %s", tot);
    printf(VP_ROW_FMT, "all", (solved == total) ? "PASS" : "FAIL",
           ratio, mdays, mscore, "", avgt, leg);
    #undef VP_ROW_FMT
    return (total > 0 && solved == total) ? 0 : 1;
}

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char **argv) {
    return shell_run_game(argc, argv);
}

int shell_run_game(int argc, char **argv) {
    // SINGLE UNIFIED OUTPUT: the whole game/autoplay log goes to stdout (nothing to
    // stderr), so a piped run is one correctly-ordered stream. Line-buffer stdout so it
    // keeps stderr's old promptness -- every line flushes immediately, and an abort/crash
    // path (e.g. nav_fail) cannot lose its dump to an unflushed block buffer.
    setvbuf(stdout, NULL, _IOLBF, 0);

    // Minimal CLI parsing.
    bool want_fullscreen = false;
    const char *pack_arg = NULL;     // --pack <name|path>
    bool extract_mode = false;        // --extract: build pack from KB.EXE then exit
    const char *extract_out_dir = NULL; // --out-dir <dir>: extract to loose tree
    const char *pack_dir_src = NULL;  // --pack-dir <src> <dst>: zip a loose asset tree
    const char *pack_dir_dst = NULL;
    // --movie [path]: record gameplay to an MP4. With no arg, defaults
    // to <user-data>/openbounty/movie-<timestamp>.mp4.
    bool        movie_requested = false;
    const char *movie_path_arg  = NULL;
    // --seed N: pick catalog world N (0..255) for a reproducible run. -1 means
    // "not asked for" -- the world is derived from time + name + class instead.
    int seed_index = -1;
    bool headless_mode = false;
    // --demo: DEMO MODE -- the human-like player agent (demo/). The agent plays
    // the LIVE game forward under a player's constraints (fog, prompts, one
    // committed timeline with no rollback) and can win, lose, or get stuck.
    // It does weigh a fight before entering it, by simulating it on a
    // discarded copy of what it can already see (DEMO-SPEC.md DM-013).
    //   --demo            -> VISIBLE: window opens, the agent plays at a
    //                        watchable pace, hands off on completion.
    //   --demo --headless -> HEADLESS: no window; plays to an ending, prints
    //                        the [DEMO OVER] report, exits.
    bool demo_mode = false;
    // --autoplay: the headless automated player / pack-winnability oracle
    // (autoplay/, docs/AUTOPLAY-SPECS.md).
    //   --autoplay --headless -> HEADLESS: no window; drives the whole game to
    //                            its verdict, prints [VERDICT READY], exits
    //                            0=SOLVED 1=NOT-SOLVED 2=setup failure.
    //   --autoplay            -> VISIBLE: the run is resolved headlessly first,
    //                            then replayed on the live world at a watchable
    //                            pace (AP-024).
    bool autoplay_mode = false;
    // --autoplay-hero=<class> / --autoplay-level=<easy|normal|hard|impossible>:
    // the class and difficulty the oracle plays; the level sets the day budget
    // via the pack. Defaults: knight / normal.
    const char *autoplay_hero = NULL;   // NULL => AUTOPLAY_HERO_CLASS
    int autoplay_level = AUTOPLAY_HERO_DIFFICULTY;
    // --autoplay-speed=<slow|normal|fast>: visible replay pacing.
    int autoplay_speed = AUTOPLAY_SPEED_NORMAL;
    // --validate-pack [LO [HI]]: systematic winnability report over a seed
    // range (default the whole catalog, 0..255).
    bool validate_pack = false;
    int  vp_lo = 0, vp_hi = 255;
    // --verbose: turn on the agent diagnostic channels for the run.
    // Observation-only: unset leaves the run bit-for-bit identical.
    bool verbose_mode = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            printf("openbounty build %s\n", OPENBOUNTY_VERSION);
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            printf("openbounty build %s\n"
                   "Usage: %s [--fullscreen] [--pack <name|path>] [--save-dir <dir>]\n"
                   "       %*s [--movie [<path>]] [--seed 0-255] [--version]\n"
                   "       %s --demo [--headless] [--seed 0-255] [--verbose] [--movie [<path>]]\n"
                   "       %s --autoplay [--headless] [--seed 0-255] [--verbose]\n"
                   "       %*s [--autoplay-hero=<class>] [--autoplay-level=<easy|normal|hard|impossible>]\n"
                   "       %*s [--autoplay-speed=<slow|normal|fast>]\n"
                   "       %s --validate-pack [LO [HI]] [--pack <name|path>]\n"
                   "       %s --extract [--out-dir <dir>]\n"
                   "       %s --pack-dir <src_dir> <out_zip>\n",
                   OPENBOUNTY_VERSION, argv[0],
                   (int)strlen(argv[0]), "",
                   argv[0], argv[0],
                   (int)strlen(argv[0]), "",
                   (int)strlen(argv[0]), "",
                   argv[0], argv[0], argv[0]);
            return 0;
        // Strict processing: an argument that does not make sense stops the
        // program. A flag needing a value with none, a bad value, an unknown
        // flag, or a stray token all print an error to stderr and exit 2 --
        // nothing runs on a misunderstood command line.
        } else if (strcmp(a, "--fullscreen") == 0) {
            want_fullscreen = true;
        } else if (strcmp(a, "--pack") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --pack requires <name|path>\n"); return 2; }
            pack_arg = argv[++i];
        } else if (strcmp(a, "--extract") == 0) {
            extract_mode = true;
        } else if (strcmp(a, "--out-dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --out-dir requires <dir>\n"); return 2; }
            extract_out_dir = argv[++i];
        } else if (strcmp(a, "--movie") == 0) {
            movie_requested = true;
            // Optional next-arg path: only consumed if it doesn't look
            // like another flag (no leading "-"). Without an arg, the
            // recorder picks an auto-named timestamp file.
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                movie_path_arg = argv[++i];
            }
        } else if (strcmp(a, "--seed") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --seed requires 0-255\n"); return 2; }
            const char *sv = argv[++i];
            char *end = NULL;
            long n = strtol(sv, &end, 0);
            if (end == sv || *end != '\0' || n < 0 || n > 255) {
                fprintf(stderr, "openbounty: --seed '%s' is not in range 0-255\n", sv);
                return 2;
            }
            seed_index = (int)n;
        } else if (strcmp(a, "--demo") == 0) {
            demo_mode = true;
        } else if (strcmp(a, "--autoplay") == 0) {
            autoplay_mode = true;
        } else if (strncmp(a, "--autoplay-hero=", 16) == 0) {
            autoplay_hero = a + 16;   // validated against the pack after load
            if (!autoplay_hero[0]) { fprintf(stderr, "openbounty: --autoplay-hero requires a class id\n"); return 2; }
        } else if (strncmp(a, "--autoplay-level=", 17) == 0) {
            const char *lv = a + 17;
            if      (strcmp(lv, "easy") == 0)       autoplay_level = 0;
            else if (strcmp(lv, "normal") == 0)     autoplay_level = 1;
            else if (strcmp(lv, "hard") == 0)       autoplay_level = 2;
            else if (strcmp(lv, "impossible") == 0) autoplay_level = 3;
            else { fprintf(stderr, "openbounty: --autoplay-level '%s' is not easy|normal|hard|impossible\n", lv); return 2; }
        } else if (strncmp(a, "--autoplay-speed=", 17) == 0) {
            const char *sp = a + 17;
            if      (strcmp(sp, "slow") == 0)   autoplay_speed = AUTOPLAY_SPEED_SLOW;
            else if (strcmp(sp, "normal") == 0) autoplay_speed = AUTOPLAY_SPEED_NORMAL;
            else if (strcmp(sp, "fast") == 0)   autoplay_speed = AUTOPLAY_SPEED_FAST;
            else { fprintf(stderr, "openbounty: --autoplay-speed '%s' is not slow|normal|fast\n", sp); return 2; }
        } else if (strcmp(a, "--validate-pack") == 0) {
            validate_pack = true;
            // Optional LO [HI] range (numeric next tokens); default 0..255.
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *e = NULL;
                long lo = strtol(argv[++i], &e, 0);
                long hi = lo;
                if (*e != '\0') { fprintf(stderr, "openbounty: --validate-pack LO must be 0-255\n"); return 2; }
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    char *e2 = NULL;
                    hi = strtol(argv[++i], &e2, 0);
                    if (*e2 != '\0') { fprintf(stderr, "openbounty: --validate-pack HI must be 0-255\n"); return 2; }
                }
                if (lo < 0 || hi > 255 || lo > hi) { fprintf(stderr, "openbounty: --validate-pack range must be 0-255 with LO<=HI\n"); return 2; }
                vp_lo = (int)lo;
                vp_hi = (int)hi;
            }
        } else if (strcmp(a, "--headless") == 0) {
            headless_mode = true;
        } else if (strcmp(a, "--verbose") == 0) {
            verbose_mode = true;
        } else if (strcmp(a, "--save-dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "openbounty: --save-dir requires <dir>\n"); return 2; }
            SavePathSetDirOverride(argv[++i]);
        } else if (strcmp(a, "--pack-dir") == 0) {
            if (i + 2 >= argc) { fprintf(stderr, "openbounty: --pack-dir requires <src_dir> <out_zip>\n"); return 2; }
            pack_dir_src = argv[++i];
            pack_dir_dst = argv[++i];
        } else {
            fprintf(stderr, "openbounty: unknown option '%s'\n"
                            "Try --help for usage.\n", a);
            return 2;
        }
    }

    // Set the agent diagnostic gates for the whole process, once, before any
    // agent path runs. Other modes never touch a gated hook, so this is inert.
    demo_set_verbose(verbose_mode);
    ob_diag_set_verbose(verbose_mode);

    // Early-exit CLI modes (--pack-dir and --extract). Both run to
    // completion and return; no window opens. Implementations live in
    // shell_earlyexit.{c,h}.
    if (pack_dir_src) return shell_run_pack_dir_mode(pack_dir_src, pack_dir_dst);
    if (extract_mode) return shell_run_extract_mode(extract_out_dir);

    // --validate-pack: the pack-author winnability report (no window).
    if (validate_pack) {
        char vp_pack_path[PACK_ENTRY_PATH_MAX];
        const char *vp_pack_dir = "assets/kings-bounty";
        if (pack_arg && pack_arg[0]) {
            if (!pack_resolve_arg(pack_arg, vp_pack_path, sizeof vp_pack_path)) {
                fprintf(stderr, "openbounty: --pack '%s' not found\n", pack_arg);
                return 2;
            }
            vp_pack_dir = vp_pack_path;
        }
        return validate_pack_run(vp_pack_dir, vp_lo, vp_hi,
                                 autoplay_hero, autoplay_level);
    }

    // Resolve the pack once for the headless agent mode (it boots its own
    // engine + pack, the engine-only consumer pattern; no window opens).
    if (autoplay_mode && headless_mode) {
        char ap_pack_path[PACK_ENTRY_PATH_MAX];
        const char *ap_pack_dir = "assets/kings-bounty";
        if (pack_arg && pack_arg[0]) {
            if (!pack_resolve_arg(pack_arg, ap_pack_path,
                                  sizeof ap_pack_path)) {
                fprintf(stdout, "--autoplay: --pack '%s' not found\n",
                        pack_arg);
                return 2;
            }
            ap_pack_dir = ap_pack_path;
        }
        // --autoplay --headless: one seed to a verdict (AP-010).
        AutoplayConfig cfg = {
            seed_index >= 0 ? seed_index : AUTOPLAY_DEFAULT_SEED_INDEX,
            ap_pack_dir,
            autoplay_hero,
            autoplay_level,
        };
        AutoplayResult r;
        if (!autoplay_run(&cfg, &r)) {
            fprintf(stdout, "--autoplay --headless: run setup failed\n");
            return 2;
        }
        recsink_free();
        return r.solved ? 0 : 1;
    }

    // Headless demo: --demo --headless plays the agent to an ending with NO
    // window and exits. Dispatched here, before InitWindow. demo_run boots its
    // own engine + pack (the engine-only consumer pattern), so no shell/window
    // state is needed.
    if (demo_mode && headless_mode) {
        char dm_pack_path[PACK_ENTRY_PATH_MAX];
        const char *dm_pack_dir = "assets/kings-bounty";
        if (pack_arg && pack_arg[0]) {
            if (!pack_resolve_arg(pack_arg, dm_pack_path, sizeof dm_pack_path)) {
                fprintf(stdout, "--demo --headless: --pack '%s' not found\n",
                        pack_arg);
                return 2;
            }
            dm_pack_dir = dm_pack_path;
        }
        DemoConfig dcfg = {
            .seed_index = seed_index >= 0 ? seed_index
                                          : DEMO_DEFAULT_SEED_INDEX,
            .pack_dir = dm_pack_dir,
        };
        DemoResult dr;
        if (!demo_run(&dcfg, &dr)) {
            fprintf(stdout, "--demo --headless: run setup failed\n");
            return 2;
        }
        // Exit-code contract: 0 = WON, 1 = any other ending (lost / stuck).
        return dr.won ? 0 : 1;
    }

    // Resolve --pack <name|path>, or auto-discover. Discovery walks (in
    // order): cwd zips, <user-data>/openbounty zips, <exe>/assets zips,
    // <exe>/assets/<sub>/game.json loose trees. If nothing is found we
    // try a first-run KB.EXE extraction in cwd; failing that, error out
    // with a platform-specific dialog explaining the install steps.
    char pack_path[PACK_ENTRY_PATH_MAX];
    if (pack_arg && pack_arg[0]) {
        if (!pack_resolve_arg(pack_arg, pack_path, sizeof pack_path)) {
            char body[1024];
            snprintf(body, sizeof body,
                "Could not find a game pack at:\n\n    %s\n\n"
                "Pass a path to a *.openbounty file, or to a directory "
                "containing game.json.",
                pack_arg);
            fatal_user_error("OpenBounty: --pack not found", body);
            return 1;
        }
    } else {
        PackEntry entries[PACK_DISCOVER_MAX];
        int n = pack_discover(entries, PACK_DISCOVER_MAX);
        if (n == 0) {
            // Final fallback: a fresh first-run KB.EXE extract from cwd.
            // Output goes to <user-data>/openbounty/<id>.openbounty so
            // future launches will find it via discovery step 2.
            struct stat st;
            const char *in_dir = NULL;
            if (stat("legacy/bin", &st) == 0 && S_ISDIR(st.st_mode))
                in_dir = "legacy/bin";
            else if (stat("KB.EXE", &st) == 0)
                in_dir = ".";
            if (!in_dir) {
                // Build a platform-specific message. Tell the user
                // (a) why nothing happened, (b) where the engine
                // looks for packs, (c) how to make one from their
                // own KB.EXE. The body has to stand alone -- on
                // Windows GUI builds this dialog is the only thing
                // the user ever sees.
                char user_dir[PACK_ENTRY_PATH_MAX];
                if (!SavePathGetDir(user_dir, sizeof user_dir)) {
                    user_dir[0] = '\0';
                }
                char body[2048];
#ifdef _WIN32
                // Windows MessageBox does its own word-wrapping; keep
                // prose as single lines and use \n only for paragraph
                // breaks and list items. Hard-wrapped prose otherwise
                // produces ragged short lines because MessageBox
                // honors the literal newlines.
                snprintf(body, sizeof body,
                    "OpenBounty cannot start because no game pack was found.\n\n"
                    "OpenBounty is a reimplementation of King's Bounty (1990) and ships without game data. To play, you must supply your own asset pack derived from a legally-owned copy of the original game.\n\n"
                    "How to fix this:\n\n"
                    "1. Place a *.openbounty pack file in either of these folders:\n"
                    "     %s\n"
                    "     (or the folder containing openbounty.exe)\n\n"
                    "2. Or, place KB.EXE next to openbounty.exe and re-run; the engine will extract a pack on first launch.\n\n"
                    "3. Or, from a command prompt:\n"
                    "     openbounty.exe --pack <path-to-pack-or-KB.EXE-folder>\n\n"
                    "See README.txt for the full instructions.",
                    user_dir[0] ? user_dir : "(your AppData\\OpenBounty folder)");
#elif defined(__APPLE__)
                snprintf(body, sizeof body,
                    "OpenBounty cannot start because no game pack was found.\n\n"
                    "OpenBounty is a reimplementation of King's Bounty (1990) and "
                    "ships without game data. To play, you must supply your own "
                    "asset pack derived from a legally-owned copy of the original game.\n\n"
                    "How to fix this:\n\n"
                    "1. Place a *.openbounty pack file in:\n"
                    "     %s\n"
                    "   or in the folder containing the openbounty binary.\n\n"
                    "2. Or, from a Terminal, run:\n"
                    "     ./openbounty --extract /path/to/KB.EXE\n"
                    "   to generate a pack from your own copy of the game.\n\n"
                    "See README.txt for the full instructions.",
                    user_dir[0] ? user_dir : "~/Library/Application Support/OpenBounty");
#else
                snprintf(body, sizeof body,
                    "OpenBounty cannot start because no game pack was found.\n\n"
                    "OpenBounty is a reimplementation of King's Bounty (1990) and "
                    "ships without game data. To play, you must supply your own "
                    "asset pack derived from a legally-owned copy of the original game.\n\n"
                    "How to fix this:\n\n"
                    "1. Place a *.openbounty pack file in:\n"
                    "     %s\n"
                    "   or in the directory you run openbounty from.\n\n"
                    "2. Or, run:\n"
                    "     ./openbounty --extract /path/to/KB.EXE\n"
                    "   to generate a pack from your own copy of the game.\n\n"
                    "See README.txt for the full instructions.",
                    user_dir[0] ? user_dir : "$XDG_DATA_HOME/openbounty (default ~/.local/share/openbounty)");
#endif
                fatal_user_error("OpenBounty: no game pack found", body);
                return 1;
            }
            fprintf(stdout, "[extract] no pack found; running first-run extraction from %s\n", in_dir);
            char user_dir[PACK_ENTRY_PATH_MAX];
            if (!SavePathGetDir(user_dir, sizeof user_dir)) return 1;
            char tmp_dir[PACK_ENTRY_PATH_MAX + 32];
            snprintf(tmp_dir, sizeof tmp_dir, "%s/.tmp-extract", user_dir);
            pack_rmtree(tmp_dir);
            if (extract_run(in_dir, tmp_dir) != 0) {
                pack_rmtree(tmp_dir);
                return 1;
            }
            char pid[64] = "kings-bounty";
            {
                Pack *p = pack_open(tmp_dir);
                if (p) {
                    const char *id = pack_id(p);
                    if (id && id[0]) snprintf(pid, sizeof pid, "%s", id);
                    pack_close(p);
                }
            }
            char wide_zip[PACK_ENTRY_PATH_MAX + 96];
            snprintf(wide_zip, sizeof wide_zip,
                     "%s/%s.openbounty", user_dir, pid);
            if (!pack_zip_dir(tmp_dir, wide_zip)) {
                pack_rmtree(tmp_dir);
                return 1;
            }
            pack_rmtree(tmp_dir);
            fprintf(stdout, "[extract] wrote %s\n", wide_zip);
            size_t wzn = strlen(wide_zip);
            if (wzn >= sizeof pack_path) wzn = sizeof pack_path - 1;
            memcpy(pack_path, wide_zip, wzn);
            pack_path[wzn] = '\0';
        } else if (n == 1) {
            snprintf(pack_path, sizeof pack_path, "%s", entries[0].path);
        } else {
            int chosen = 0;
            if (!pack_select_flow(entries, n, &chosen)) {
                // User pressed ESC.
                return 0;
            }
            snprintf(pack_path, sizeof pack_path, "%s", entries[chosen].path);
        }
    }

    Pack *pack = pack_open(pack_path);
    if (!pack) {
        char body[1024];
        snprintf(body, sizeof body,
            "Failed to open the game pack at:\n\n    %s\n\n"
            "The file may be corrupt, the wrong format, or unreadable. "
            "Try replacing it with a fresh extraction:\n\n"
            "    openbounty --extract /path/to/KB.EXE",
            pack_path);
        fatal_user_error("OpenBounty: cannot open game pack", body);
        return 1;
    }
    pack_stack_push(pack);

    // Silence raylib's per-asset INFO chatter; keep warnings + errors.
    SetTraceLogLevel(LOG_WARNING);

    Resources res;
    if (!resources_load(&res, "game.json")) {
        fprintf(stdout, "Failed to load game resources from %s.\n", pack_path);
        pack_stack_clear();
        return 1;
    }

    // Strict: an --autoplay-hero the pack does not define stops the program
    // (validated here, where the catalog is loaded but no window is open yet).
    if (autoplay_mode && autoplay_hero && !class_by_id(autoplay_hero)) {
        fprintf(stderr, "openbounty: unknown --autoplay-hero '%s'\n",
                autoplay_hero);
        resources_free(&res);
        pack_stack_clear();
        return 2;
    }

    int base_w = CL_WINDOW_W;    // 640
    int base_h = CL_WINDOW_H;    // 400

    unsigned int window_flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
    SetConfigFlags(window_flags);
    InitWindow(base_w, base_h, res.title[0] ? res.title : "OpenBounty");
    SetWindowMinSize(320, 200);
    if (want_fullscreen) ToggleFullscreen();
    // Demo mode paces itself via per-beat holds in shell_demo.c; the frame rate
    // stays at the human 60fps cap. Human play is 60fps too.
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    // Bitmap font + 256-color palette have fixed pack-relative paths
    // that the extractor produces. No engine-side configuration knob.
    bfont_init("art/font/kb-font.png");
    palette_init("palettes/palette.bin");

    Sprites sprites;
    sprites_load(&sprites, &res);
    tile_cache_attach(&res);

    // Allocate the render target early so startup screens can
    // draw into it.
    RenderTexture2D render_target_startup =
        LoadRenderTexture(CL_SCREEN_W, CL_SCREEN_H);
    SetTextureFilter(render_target_startup.texture, TEXTURE_FILTER_POINT);

    // Pre-game flow: pick slot + new-game wizard. --demo / --autoplay bypass
    // the wizard and synthesize a deterministic new game: the agent plays it,
    // so there's no human to run the menus. Seed defaults to the mode default.
    StartupChoice choice = { 0 };
    if (demo_mode || autoplay_mode) {
        if (seed_index < 0)
            seed_index = autoplay_mode ? AUTOPLAY_DEFAULT_SEED_INDEX
                                       : DEMO_DEFAULT_SEED_INDEX;
        choice.action = STARTUP_NEW;
        choice.slot = 0;
        // Autoplay's class/level come from --autoplay-hero / --autoplay-level
        // (validated against the loaded pack; an unknown class warns and falls
        // back to the default). Demo keeps its fixed profile.
        const char *hero = (autoplay_mode && autoplay_hero && autoplay_hero[0])
                               ? autoplay_hero : AUTOPLAY_HERO_CLASS;
        if (autoplay_mode && autoplay_hero && autoplay_hero[0] &&
            !class_by_id(autoplay_hero)) {
            fprintf(stdout, "[main] --autoplay-hero '%s' unknown; using %s\n",
                    autoplay_hero, AUTOPLAY_HERO_CLASS);
            hero = AUTOPLAY_HERO_CLASS;
        }
        snprintf(choice.class_id, sizeof choice.class_id, "%s",
                 autoplay_mode ? hero : "");
        snprintf(choice.name, sizeof choice.name, "%s",
                 autoplay_mode ? AUTOPLAY_HERO_NAME : DEMO_HERO_NAME);
        choice.difficulty = autoplay_mode ? autoplay_level
                                          : DEMO_HERO_DIFFICULTY;
    } else if (!startup_flow(&res, &sprites,
                             &render_target_startup, &choice)) {
        // User quit before choosing.
        UnloadRenderTexture(render_target_startup);
        sprites_unload(&sprites);
        bfont_shutdown();
        CloseWindow();
        resources_free(&res);
        pack_stack_clear();
        return 0;
    }

    Map map;
    Fog fog;
    FogInit(&fog);

    Game game;
    game.res = &res;
    if (choice.action == STARTUP_NEW) {
        // Class id -> pclass index comes straight from the ClassDef catalog.
        // Unknown ids fall back to class 0 so the game still starts.
        const ClassDef *cd = class_by_id(choice.class_id);
        int pclass = (cd && cd->index >= 0) ? cd->index : 0;

        // --seed picks the catalog world (villain placements, dwellings,
        // scepter, salt). seed_index < 0 means none was asked for, and
        // GameInitSeeded derives one from time + name + class instead.
        GameInitSeeded(&game, choice.name, pclass, choice.difficulty, NULL,
                       seed_index);
        fprintf(stdout, "[main] seed: %d%s\n", game.seed_index,
                seed_index >= 0 ? "" : " (derived)");

        //  -- post-create_game informational modal.
        char body[256];
        snprintf(body, sizeof(body),
                 "%s the %s,\n\n"
                 "A new game is being created. "
                 "Please wait while I perform "
                 "godlike actions to make this "
                 "game playable.",
                 game.character.name,
                 game.character.cls.rank_title);
        player_io_message(&game, NULL, body);
    } else {
        // LOAD: hydrate Game from the chosen slot. GameInit first with
        // defaults so all fields have sane values the loader can overwrite.
        GameInit(&game, "Hero", 0, DIFFICULTY_NORMAL, NULL);
        char path[512];
        const char *pid = res.pack_id[0] ? res.pack_id : NULL;
        if (SavePathGetSlot(pid, choice.slot, path, sizeof(path))) {
            SaveResult r = SaveGameRead(path, &game, &map, &fog);
            if (r != SAVE_OK) {
                fprintf(stdout, "Load slot %d failed: %s\n",
                        choice.slot, SaveResultText(r));
            }
        }

        //  -- post-load_game informational modal.
        char body[256];
        snprintf(body, sizeof(body),
                 "%s the %s,\n\n"
                 "Please wait while I prepare "
                 "a suitable environment for "
                 "your bountying enjoyment!",
                 game.character.name,
                 game.character.cls.rank_title);
        player_io_message(&game, NULL, body);
    }

    // Load the starting zone now that placements are populated. For NEW
    // games, GameInit chose the zone marked is_home (falling back to
    // res.world.starting_zone); for LOAD, Game.position.zone was restored
    // from the save.
    const char *load_zone = game.position.zone;
    if (!load_zone[0]) load_zone = res.world.starting_zone;
    if (!MapLoadZoneWithPlacements(&map, &res, load_zone, &game)) {
        UnloadRenderTexture(render_target_startup);
        sprites_unload(&sprites);
        bfont_shutdown();
        CloseWindow();
        resources_free(&res);
        pack_stack_clear();
        return 1;
    }
    // Apply consumed-tile mutations (artifact pickups, removed alcoves, etc.)
    // for the freshly-loaded zone. GameSwitchZone does this on later loads;
    // this is the initial-load equivalent.
    GameApplyTileMutations(&game, &map, load_zone);
    game.position.last_x = game.position.x;
    game.position.last_y = game.position.y;
    game.hud_visible = true;

    // spawn_game calls clear_fog() in the NEW path only; load_game
    // trusts the fog bytes stored in the save. Match that behavior -- for
    // LOAD, the Fog struct was populated by SaveGameRead above.
    if (choice.action == STARTUP_NEW) {
        FogReveal(&fog, &map, game.position.x, game.position.y,
                  res.world.fog_sight);
    }

    bool quit_requested = false;
    // Set when a demo run WON (scepter recovered): the win cartoon + win
    // screen play as the ending, then control is handed to the human on the
    // cleared world. The engine's show_win_game sets game_over (the real
    // game's terminal state); we clear it once the player dismisses the win
    // view so the hand-off is a LIVE board, not a frozen one.
    bool won_handoff = false;
    const int spawn_x = game.position.x;
    const int spawn_y = game.position.y;
    MenuCtx menu_ctx = {
        .game = &game, .map = &map, .fog = &fog, .res = &res,
        .spawn_x = spawn_x, .spawn_y = spawn_y,
        .quit_flag = &quit_requested,
        .hud_pref = game.hud_visible,
    };
    MenuCallbacks menu_cbs = {
        .on_save = menu_save, .on_load = menu_load,
        .on_new  = menu_new,  .on_quit = menu_quit,
    };

    // Render target was allocated above (render_target_startup)
    // so the pre-game flow can draw into it; reuse here.
    RenderTexture2D render_target = render_target_startup;

    // Recorder: when --movie was passed, capture state + framebuffer
    // PNGs on logical-tick mutations into a hidden temp dir, then mux
    // to one .mp4 at shutdown. Off when --movie wasn't passed, in
    // which case every recorder_capture() call is a free no-op.
    if (movie_requested) {
        char movie_path[1024];
        if (movie_path_arg) {
            snprintf(movie_path, sizeof movie_path, "%s", movie_path_arg);
        } else {
            // Auto-name: <user-data>/openbounty/movie-YYYYMMDD-HHMMSS.mp4
            char user_dir[PACK_ENTRY_PATH_MAX];
            if (!SavePathGetDir(user_dir, sizeof user_dir)) {
                fprintf(stdout, "--movie: cannot resolve user data dir\n");
                movie_requested = false;
            } else {
                time_t t = time(NULL);
                struct tm tmv;
                localtime_r(&t, &tmv);
                snprintf(movie_path, sizeof movie_path,
                         "%s/movie-%04d%02d%02d-%02d%02d%02d.mp4",
                         user_dir,
                         tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                         tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
            }
        }
        if (movie_requested) {
            recorder_init(movie_path);
            recorder_attach_state(&game, &map, &fog);
            recorder_attach_render_target(&render_target);
            fprintf(stdout, "--movie: recording to %s\n", movie_path);
        }
    }

    // Audio: open device, load music streams, start the openworld
    // track. Honors the user's saved Sounds + Music toggles.
    audio_init(&res);
    if (!audio_is_available()) {
        // No playback device: pin Sounds/Music/Volume to 0 so the
        // controls panel and the live audio push agree. The rows are
        // also rendered grayed-out and ignore input.
        game.stats.options[1] = 0;  // Sounds
        game.stats.options[5] = 0;  // Music
        game.stats.options[6] = 0;  // Volume
    }
    audio_set_sounds_enabled(game.stats.options[1] != 0);
    audio_set_music_enabled (game.stats.options[5] != 0);
    audio_set_master_volume (game.stats.options[6]);       // Volume 0..9
    audio_set_track(AUDIO_TRACK_OPENWORLD);

    double hero_anim_next = 0.0;
    double last_step_time = 0.0;   // classic: only animate shortly after a step
    bool prev_overlay = false;

    if (demo_mode) {
        // Visible demo mode: the player agent drives the LIVE game; the shell
        // paces it. No plan, no replay -- the run unfolds as it is decided.
        ShellCtx dctx = {
            .game = &game, .map = &map, .fog = &fog, .res = &res,
            .sprites = &sprites, .render_target = &render_target,
            .quit_requested = &quit_requested,
        };
        if (!shell_demo_begin(&dctx)) {
            fprintf(stdout, "--demo: agent init failed; playing manually\n");
            demo_mode = false;
        }
    }

    if (autoplay_mode) {
        // Visible autoplay (AP-024): resolve headlessly on the oracle's own
        // world, then replay the recording on this identical live world.
        // Mark the live game as an oracle session so the replay's on-capture
        // rank promotions match the headless resolve (issue #13 gates that
        // promotion to real, non-oracle play only).
        game.oracle_mode = true;
        ShellCtx actx = {
            .game = &game, .map = &map, .fog = &fog, .res = &res,
            .sprites = &sprites, .render_target = &render_target,
            .quit_requested = &quit_requested,
        };
        if (!shell_autoplay_begin(&actx, seed_index, pack_path,
                                  autoplay_hero, autoplay_level,
                                  autoplay_speed)) {
            if (shell_autoplay_cancelled()) {
                // ESC / window-close during the resolve: drop out of the game.
                quit_requested = true;
            } else {
                fprintf(stdout,
                        "--autoplay: resolution failed; playing manually\n");
                autoplay_mode = false;
            }
        }
    }

    while (!frame_host_should_close() && !quit_requested) {
        // Audio: drive music streaming + react to live toggle changes.
        audio_set_sounds_enabled(game.stats.options[1] != 0);
        audio_set_music_enabled (game.stats.options[5] != 0);
        audio_set_master_volume (game.stats.options[6]);
        audio_tick();
        if ((input_key_down(KEY_LEFT_ALT) || input_key_down(KEY_RIGHT_ALT)) &&
            input_key_pressed(KEY_ENTER)) {
            ToggleFullscreen();
        }

        // F10 -> debug cheat menu (implementation in shell_cheats.{c,h}).
        // W/L cheats short-circuit normal per-frame logic.
        if (cheat_menu_tick(&game, &map, &fog, &res, &sprites,
                            &render_target) == CHEAT_DISPATCHED_TERMINAL) {
            continue;
        }

        bool overlay = (views_active() != VIEW_NONE) || dialog_is_active() ||
                       prompt_is_active();
        if (overlay && !prev_overlay) {
            menu_ctx.hud_pref = game.hud_visible;
            game.hud_visible = false;
        } else if (!overlay && prev_overlay) {
            game.hud_visible = menu_ctx.hud_pref;
        }
        prev_overlay = overlay;

        // Pop pending week-end screens (astrology -> budget) before input.
        pump_week_end_dialog(&game);

        // Drain engine player-IO MESSAGES (chest results, pickups, sign-posts,
        // captures, etc.) into the shell dialog so the engine's uniform
        // messages render through the existing dialog UI. One per frame when the
        // dialog slot is free; the human dismisses with any key as before.
        shell_pump_player_io_message(&game);

        // Sync engine VIEWS (town / home-castle / own-castle / alcove / dwelling /
        // win / lose) from the queue onto the shell view stack. The human
        // dismisses them with ESC as before. Autoplay never reaches here (it
        // owns the frame and acks REQ_VIEWs directly), so it never accumulates a
        // view -- the views_push stack-overflow defect is gone.
        shell_pump_player_io_view(&game);

        // ==== Input ====

        // Fast-quit (Ctrl+Q) status-bar prompt.
        if (fast_quit_is_active()) {
            if (fast_quit_tick()) quit_requested = true;
            goto end_input;   // swallow any other input this frame
        }

        // Bottom-frame prompt-result dispatcher (search, dismiss-army,
        // siege, attack-foe, chest, alcove, recruit, accept-friendly,
        // navigate). Returns true while a prompt is up.
        ShellCtx sctx = {
            .game = &game, .map = &map, .fog = &fog, .res = &res,
            .sprites = &sprites, .render_target = &render_target,
            .quit_requested = &quit_requested,
        };

        // Visible demo mode owns the frame: the agent answers its
        // own prompts and takes one paced action; we skip human input.
        if (demo_mode) {
            bool dm_done = false;
            shell_demo_tick(&sctx, frame_host_time(), &dm_done);
            if (dm_done) {
                char body[240];
                shell_demo_summary(&sctx, body, sizeof body);
                fprintf(stdout, "--demo: %s\n", body);
                shell_demo_end();
                demo_mode = false;
                if (game.stats.won) {
                    // The agent found the scepter: play the ending a human
                    // victory gets, then hand off the cleared board.
                    run_end_cartoon(&render_target, &res, &sprites);
                    show_win_game(&game, &res);
                    won_handoff = true;
                } else {
                    open_dialog("Demo", body);
                }
            }
            goto end_input;
        }

        // Visible autoplay owns the frame the same way: one recorded
        // primitive per paced beat, applied through the one replay applier.
        if (autoplay_mode) {
            // Interrupt: ESC or SPACE during the visible replay hands control
            // to the human. There is no resume -- once taken over, autoplay
            // stays off for the rest of the session (s_active cleared by end).
            if (input_key_pressed(KEY_ESCAPE) || input_key_pressed(KEY_SPACE)) {
                shell_autoplay_end();
                autoplay_mode = false;
                // Clean handoff: the replay may be mid-prim with a prompt /
                // view / pending flow open. Clear it all, or a stale prompt
                // eats input (prompt_dispatch_tick runs before the dialog
                // handler) and the message below would never dismiss.
                player_io_reset(&game);
                pending_reset();
                views_set(VIEW_NONE);
                if (prompt_is_active()) prompt_dismiss();
                open_dialog("Autoplay",
                            "Autoplay interrupted.\n\nYou now have control.");
                goto end_input;
            }
            bool ap_done = false;
            shell_autoplay_tick(&sctx, frame_host_time(), &ap_done);
            if (ap_done) {
                char body[240];
                shell_autoplay_summary(&sctx, body, sizeof body);
                fprintf(stdout, "--autoplay: %s\n", body);
                shell_autoplay_end();
                autoplay_mode = false;
                if (game.stats.won) {
                    run_end_cartoon(&render_target, &res, &sprites);
                    show_win_game(&game, &res);
                    won_handoff = true;
                } else {
                    open_dialog("Autoplay", body);
                }
            }
            goto end_input;
        }

        // Town/Castle Gate destination picker (shell_gate.{c,h}): when a gate
        // spell armed gate_state, gate_menu_tick opens VIEW_GATE. Selection/ESC
        // input then lives in the VIEW_GATE branch of the if/else chain below,
        // NOT in gate_menu_tick. So only short-circuit the frame on which the
        // picker is freshly opened; while it is already showing, fall through
        // so its VIEW_GATE branch actually receives keys. (Blanket-skipping
        // every frame the picker is up starved that branch -> a dead,
        // unresponsive picker that could only be escaped by rebooting.)
        bool gate_already_open = (views_active() == VIEW_GATE);
        if (gate_menu_tick(&game, &map, &fog) == GATE_MENU_ACTIVE &&
            !gate_already_open) {
            goto end_input;
        }

        if (prompt_dispatch_tick(&sctx)) {
            // prompt is up (or just resolved); skip the rest of input
        } else if (views_active() == VIEW_MENU) {
            views_menu_update(&menu_cbs, &menu_ctx);
        } else if (views_active() == VIEW_TOWN) {
            views_town_update(&game);
        } else if (views_active() == VIEW_CONTROLS) {
            // Navigate rows with Up/Down; digit keys 1..N jump to and
            // advance the matching row; ESC / any unhandled key closes.
            int count = 0;
            int vis_map[8] = { 0 };
            int vis = 0;
            if (res.controls.count > 0) {
                for (int i = 0; i < res.controls.count && vis < 8; i++) {
                    if (res.controls.items[i].hidden) continue;
                    vis_map[vis++] = i;
                }
                count = vis;
            }
            int cur = views_controls_cursor();
            if (count > 0) {
                if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8)) {
                    cur = (cur - 1 + count) % count;
                    views_controls_set_cursor(cur);
                } else if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2)) {
                    cur = (cur + 1) % count;
                    views_controls_set_cursor(cur);
                } else if (input_key_pressed(KEY_ENTER) ||
                           input_key_pressed(KEY_KP_ENTER) ||
                           input_key_pressed(KEY_SPACE)) {
                    // Advance the value of the selected setting.
                    views_controls_advance(&game, vis_map[cur]);
                } else if (input_key_pressed(KEY_ESCAPE) ||
                           input_key_pressed(KEY_C) ||
                           gamepad_pressed_cancel()) {
                    views_dismiss();
                } else {
                    // Digit 1..count selects and advances that row.
                    for (int k = 0; k < count && k < 9; k++) {
                        if (input_key_pressed(KEY_ONE + k)) {
                            views_controls_set_cursor(k);
                            views_controls_advance(&game, vis_map[k]);
                            break;
                        }
                    }
                }
            } else if (ui_any_key_pressed()) {
                views_dismiss();
            }
        } else if (views_active() == VIEW_SPELLS) {
            // Spell casting with Left/Right to switch columns, A-G to cast.
            if (views_spells_update()) {
                int spell_idx = views_spells_chosen();
                if (spell_idx >= 0) {
                    dispatch_adventure_spell(&game, spell_idx);
                }
            } else if (ui_any_key_pressed()) {
                views_dismiss();
            }
        } else if (views_active() == VIEW_GATE) {
            // Gate destination picker: arrows/letter to select, Enter to go,
            // ESC to cancel (handled inside views_gate_update). On a confirmed
            // choice the engine performs the boat-aware teleport + charge spend.
            if (views_gate_update()) {
                int idx = views_gate_chosen();
                const GateDestination *d = views_gate_dest(idx);
                if (d) {
                    GameGateTeleport(&game, &map, &fog, d,
                                     views_gate_is_town() ? "town_gate"
                                                          : "castle_gate");
                }
            }
        } else if (views_active() == VIEW_HOME_CASTLE && !dialog_is_active()) {
            //  /  +
            // : throne_room_or_barracks gamestate accepts A and B
            // to enter sub-flows; ESC pops back to the overworld.
            //
            // Gated on !dialog_is_active() so the audience modal popup
            // (run_audience_dialog -> open_dialog) is handled by the
            // downstream dialog branch instead -- dialog has its own
            // SPACE-to-advance flow over the persistent backdrop.
            if (input_key_pressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                views_dismiss();
                pending_castle_id[0] = '\0';
            } else if (input_key_pressed(KEY_A)) {
                // A) Recruit Soldiers -- push the dedicated recruit
                // sub-screen (5 troops + gold + key hint).
                screen_recruit_soldiers_open(&game);
            } else if (input_key_pressed(KEY_B)) {
                // B) Audience with the King -- modal popup over the
                // castle backdrop. Run after panel render so it overlays.
                const ResCastle *rc2 =
                    resources_castle_by_id(&res, pending_castle_id);
                run_audience_dialog(&game, rc2);
            }
        } else if (views_active() == VIEW_RECRUIT_SOLDIERS && !dialog_is_active()) {
            // recruit_soldiers -- the screen owns
            // its full input loop (state machine `whom`, inline numeric
            // input). Main.c just forwards each frame and dismisses on
            // ESC-at-idle.
            if (screen_recruit_soldiers_update(&game)) {
                views_dismiss();
            }
        } else if (views_active() == VIEW_OWN_CASTLE && !dialog_is_active()) {
            //  / :
            // SPACE toggles GARRISON/REMOVE; A..E moves the chosen
            // slot via GameGarrisonTroop / GameUngarrisonTroop. ESC
            // returns to the overworld.
            if (input_key_pressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                views_dismiss();
                pending_castle_id[0] = '\0';
            } else if (input_key_pressed(KEY_SPACE)) {
                screen_own_castle_toggle_mode();
            } else {
                for (int k = 0; k < 5; k++) {
                    if (!input_key_pressed(KEY_A + k)) continue;
                    const char *cid = screen_own_castle_castle_id();
                    int rc;
                    if (screen_own_castle_is_garrison_mode()) {
                        rc = GameGarrisonTroop(&game, cid, k);
                        if (rc == 2) {
                            player_io_message(&game, NULL,
                                game.res->banners.cannot_garrison_last);
                        } else if (rc == 1) {
                            player_io_message(&game, NULL,
                                game.res->banners.no_troop_slots);
                        }
                    } else {
                        rc = GameUngarrisonTroop(&game, cid, k);
                        if (rc == 1) {
                            player_io_message(&game, NULL,
                                game.res->banners.no_troop_slots);
                        }
                    }
                    break;
                }
            }
        } else if (views_active() != VIEW_NONE && !dialog_is_active()) {
            // VIEW_WORLDMAP: SPACE toggles fog-only vs whole-map reveal
            // when the player owns the crystal orb for this continent
            // . Without the orb
            // SPACE dismisses the view like any other key.
            //
            // Gated on !dialog_is_active() so a dialog opened on top of
            // a persistent view (e.g. audience-with-king over
            // VIEW_HOME_CASTLE) doesn't have its dismiss key also tear
            // down the underlying view.
            if (views_active() == VIEW_WORLDMAP && input_key_pressed(KEY_SPACE)) {
                int zi = -1;
                for (int i = 0; i < res.zone_count; i++) {
                    if (strcmp(res.zones[i].id, game.position.zone) == 0) {
                        zi = i; break;
                    }
                }
                bool has_orb = (zi >= 0 && zi < GAME_CONTINENTS &&
                                game.world.orbs_found[zi]);
                if (has_orb) {
                    views_render_worldmap_toggle_hero_only();
                } else {
                    views_dismiss();
                }
            } else if (ui_any_key_pressed()) {
                ViewKind dismissing = views_active();
                views_dismiss();
                // WIN HAND-OFF: dismissing the agent's win screen returns
                // control to the human on the cleared world. show_win_game set
                // game_over (the real terminal ending); clear it here so the
                // next frame is a LIVE adventure board the player drives, not a
                // frozen one that the GameIsOver branch would quit to menu.
                if (won_handoff && dismissing == VIEW_WIN) {
                    game.stats.game_over = false;
                    won_handoff = false;
                }
            }
        } else if (dialog_is_active()) {
            // Handle bridge direction input if waiting for it
            if (bridge_state == BRIDGE_STATE_DIRECTION) {
                InputState in = input_poll();
                if (in.dx != 0 || in.dy != 0) {
                    int built = try_build_bridge(&game, &map, in.dx, in.dy);
                    bridge_state = BRIDGE_STATE_NONE;
                    const ResBanners *bn = &game.res->banners;
                    char msg[RES_BANNER_LEN];
                    if (built > 0) {
                        int bidx = spell_index_by_id("bridge");
                        if (bidx >= 0) game.spells.counts[bidx]--;
                        char cbuf[16];
                        snprintf(cbuf, sizeof cbuf, "%d", built);
                        ResTemplateVar vars[] = { { "COUNT", cbuf } };
                        resources_format_template(msg, sizeof msg,
                                                  bn->spell_bridge_built,
                                                  vars, 1);
                        dialog_dismiss();
                        player_io_message(&game, spell_header("bridge", "Bridge"), msg);
                    } else {
                        resources_format_template(msg, sizeof msg,
                                                  bn->spell_bridge_invalid,
                                                  NULL, 0);
                        dialog_dismiss();
                        player_io_message(&game, spell_header("bridge", "Bridge"), msg);
                    }
                } else if (input_key_pressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                    bridge_state = BRIDGE_STATE_NONE;
                    dialog_dismiss();
                }
            } else {
                // ask_quit dialog handling: Ctrl-Q exits, any other key
                // advances page or dismisses. Since Ctrl-Q is the only post-dialog action, we
                // check it here for all dialogs (harmless elsewhere).
                bool ctrl = input_key_down(KEY_LEFT_CONTROL) || input_key_down(KEY_RIGHT_CONTROL);
                if (ctrl && input_key_pressed(KEY_Q)) {
                    dialog_dismiss();
                    quit_requested = true;
                } else if (ui_any_key_pressed()) {
                    // Advance to next page or dismiss if on last page.
                    if (!dialog_advance()) {
                        dialog_dismiss();
                        // Audience two-step: if a king's message
                        // is pending, open it as page 2 right after the
                        // fanfare dialog dismisses. 
                        // KB_BottomBox(message, "", MSG_PAUSE) at
                        // game.c:2118, the king's words are passed as
                        // the HEADER (rendered yellow) with empty body.
                        // No separate "Castle of King Maximus" title
                        // -- that's a deviation that's now removed.
                        if (pending_audience_message[0]) {
                            char body[700];
                            snprintf(body, sizeof(body), "%s",
                                     pending_audience_message);
                            pending_audience_message[0] = '\0';
                            player_io_message(&game, body, "");
                        }
                    }
                }
            }
        } else if (GameIsOver(&game)) {
            // Game over: any key returns to menu.
            if (ui_any_key_pressed()) quit_requested = true;
        } else {
            // Standard adventure-mode bindings. No ESC->menu,
            // no TAB, no Space->HUD.
            InputState in = input_poll();
            shell_dispatch_action(&sctx, &in);
            if (in.action == INPUT_ACTION_NONE && (in.dx || in.dy)) {
                if (GameStep(&game, &map, &fog, &res, in.dx, in.dy)) {
                    last_step_time = frame_host_time();
                }
            }
        }

        // Animate hero sprite -- only advance frames during
        // or just after a step. options[3] = 1 means Animation On
        // ( "Animation = On" with value 1; classic
        // controls_menu displays "On" when val==1).
        if (frame_host_time() >= hero_anim_next) {
            bool anim_enabled = (game.stats.options[3] != 0);
            bool animating = anim_enabled && (frame_host_time() - last_step_time < 0.4);
            if (animating) {
                game.anim_frame = (game.anim_frame + 1) % 4;
            } else {
                game.anim_frame = 0;   // idle pose
            }
            // Delay option: 0.05 + options[0] * 0.05 (range 0-5 = 0.05-0.30)
            double interval = 0.05 + game.stats.options[0] * 0.05;
            hero_anim_next = frame_host_time() + interval;
        }

        end_input:;

        // ==== Draw ====
        // Render into the 320x200 offscreen target.
        BeginTextureMode(render_target);
        draw_frame(&game, &map, &fog, &sprites);
        EndTextureMode();

        // Blit centered + letterboxed at the largest integer scale that fits,
        // within the bounds in layout.h (see present.c).
        present_scaled(render_target);
        frame_host_end_frame();

        // Screenshots (dev builds only; see screenshot.c):
        //   - backtick (`) -> "shot" prefix, on demand
        //   - VIEW_CHARACTER rising edge -> "char" prefix (auto, for layout diffs)
        screenshot_tick(render_target, "shot");
        {
            static bool prev_char_view = false;
            bool cur_char_view = (views_active() == VIEW_CHARACTER);
            if (cur_char_view && !prev_char_view) {
                screenshot_save(render_target, "char");
            }
            prev_char_view = cur_char_view;
        }

    }

    // Encode --movie session to its MP4. Runs AFTER the main loop exits,
    // BEFORE recorder_shutdown so the temp dir is still on disk. The
    // dialog drives its own draw loop on render_target.
    if (recorder_active() && recorder_temp_dir() && recorder_output_path()) {
        encode_dialog_session(&render_target,
                              recorder_temp_dir(),
                              recorder_output_path());
    }

    audio_shutdown();
    recorder_shutdown();
    UnloadRenderTexture(render_target);
    bfont_shutdown();
    sprites_unload(&sprites);
    CloseWindow();
    resources_free(&res);
    pack_stack_clear();
    return 0;
}
