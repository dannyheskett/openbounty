// localtime_r needs POSIX 199506+; 200809L covers everything we use.
#define _POSIX_C_SOURCE 200809L

#include "cli_flags.h"
#include "shell_pack.h"
#include "frame_host.h"
#include "gfx.h"
#include "input_host.h"
#include "shell_demo.h"
#include "demo.h"
#include "shell_autoplay.h"
#include "autoplay.h"
#include "diag.h"
#include "shell_run.h"
#include "ob_types.h"
#include "recorder.h"
#include "audio.h"
#include "encode_dialog.h"
#include "map.h"
#include "fog.h"
#include "savegame.h"
#include "savepath.h"
#include "game.h"
#include "bfont.h"
#include "select.h"
#include "sprites.h"
#include "tile_cache.h"
#include "tilevar.h"
#include "adventure.h"
#include "views.h"
#include "ui.h"
#include "resources.h"
#include "screenshot.h"
#include "pack.h"
#include "pack_select.h"
#include "plat_android.h"
#include "plat_ios.h"

#include "boot_trace.h"
#include "extract.h"
#include "version.h"
#include "fatal.h"

#include <sys/stat.h>
#include "layout.h"
#include "lattice.h"
#include "present.h"
#include "palette.h"
#include "chrome.h"
#include "hud.h"
#include "map_render.h"
#include "overlay.h"
#include "input.h"
#include "touch.h"
#include "uitouch.h"
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
#include "shell_goto.h"
#include "views_render_impl.h"
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
#include "modern/rail.h"
#include "shell_earlyexit.h"

// Adventure spell casting (cast_*, dispatch_adventure_spell, bridge/gate
// continuation state) lives in spells_adventure.{c,h}.
#include "spells_adventure.h"

// Game-menu (Esc / O) callbacks: Save, Load, New, Quit.
#include "shell_menu.h"

// perform_temp_death(): a lost fight sends the hero home.
#include "shell_tempdeath.h"

// End-of-week two-screen sequence (astrology -> budget). Implementation
// in src/shell_weekend.{c,h}. Call schedule_week_end() whenever a week
// boundary is crossed; the main loop calls pump_week_end_dialog to pop
// each pending screen before processing input.
#include "shell_weekend.h"


// run_audience_dialog(): the audience with the king.
#include "shell_audience.h"
#include "modern/castle.h"
#include "modern/gamemenu.h"
#include "modern/mlist.h"
#include "modern/location.h"
#include "shell_gallery.h"

// draw_frame(): the per-frame draw.
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

// The cell a tap on the map steps from. Modern: the hero's own cell, wherever
// the camera put it. Legacy: the viewport's centre tile, as it always was.
static void map_tap_cell(const Game *g, const Map *m, int *x, int *y) {
    if (CL_IS_MODERN) { map_render_hero_cell(g, m, x, y); return; }
    *x = CL_MAP_X + (CL_MAP_TILES_W / 2) * CL_TILE_W;
    *y = CL_MAP_Y + (CL_MAP_TILES_H / 2) * CL_TILE_H;
}

// ===========================================================================
// main
// ===========================================================================

// iOS has its own entry point: UIApplicationMain in ios/ios_main.mm, which
// starts the UI and then runs shell_run_game on the game thread. Defining
// main() here as well would be a duplicate symbol.
#if !defined(PLATFORM_IOS)
int main(int argc, char **argv) {
    return shell_run_game(argc, argv);
}
#endif

int shell_run_game(int argc, char **argv) {
    // SINGLE UNIFIED OUTPUT: the whole game/autoplay log goes to stdout (nothing to
    // stderr), so a piped run is one correctly-ordered stream. Line-buffer stdout so it
    // keeps stderr's old promptness -- every line flushes immediately, and an abort/crash
    // path (e.g. nav_fail) cannot lose its dump to an unflushed block buffer.
    setvbuf(stdout, NULL, _IOLBF, 0);

    // Android has no command line and no writable working directory: the save
    // root is resolved from the activity before anything can read a slot.
    // A no-op everywhere else.
    BOOT_TRACE("[boot] entered\n");
    plat_android_boot();
    plat_ios_boot();
    BOOT_TRACE("[boot] save path resolved\n");

    // The command line (src/cli_flags.c): strict, and nothing runs on a
    // misunderstood one.
    CliOptions cli;
    {
        int rc = cli_parse(argc, argv, &cli);
        if (rc >= 0) return rc;
    }
    if (cli.save_dir) SavePathSetDirOverride(cli.save_dir);
    bool want_fullscreen = cli.want_fullscreen;
    const char *pack_arg = cli.pack_arg;
    const char *lang_arg = cli.lang_arg;
    bool extract_mode = cli.extract_mode;
    const char *extract_out_dir = cli.extract_out_dir;
    const char *pack_dir_src = cli.pack_dir_src;
    const char *pack_dir_dst = cli.pack_dir_dst;
    bool debug_flag = cli.debug_flag;
    bool movie_requested = cli.movie_requested;
    const char *movie_path_arg = cli.movie_path_arg;
    const char *gallery_dir = cli.gallery_dir;
    const char *intro_movie = cli.intro_movie;
    const char *puzzle_sweep_dir = cli.puzzle_sweep_dir;
    int want_win_w = cli.want_win_w;
    int want_win_h = cli.want_win_h;
    bool force_touch = cli.force_touch;
    int seed_index = cli.seed_index;
    bool headless_mode = cli.headless_mode;
    bool demo_mode = cli.demo_mode;
    bool autoplay_mode = cli.autoplay_mode;
    const char *autoplay_hero = cli.autoplay_hero;
    int autoplay_level = cli.autoplay_level;
    int autoplay_speed = cli.autoplay_speed;
    bool validate_pack = cli.validate_pack;
    int vp_lo = cli.vp_lo;
    int vp_hi = cli.vp_hi;
    bool verbose_mode = cli.verbose_mode;

    // Set the agent diagnostic gates for the whole process, once, before any
    // agent path runs. Other modes never touch a gated hook, so this is inert.
    demo_set_verbose(verbose_mode);
    ob_diag_set_verbose(verbose_mode);

    // Apply the --lang locale override once, before any mode loads resources
    // (validate-pack, autoplay, demo, and the normal game all share it).
    resources_set_locale(lang_arg);

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

    // Which pack: the one bundled on Android and iOS, --pack, discovery or
    // the picker, or a first-run extraction (src/shell_pack.c).
    char pack_path[PACK_ENTRY_PATH_MAX];
    Pack *pack = NULL;
    {
        int rc = shell_open_game_pack(pack_arg, &pack, pack_path, sizeof pack_path);
        if (rc >= 0) return rc;
    }
    pack_stack_push(pack);

    // Silence raylib's per-asset INFO chatter; keep warnings + errors.
    frame_host_quiet_log();   // the shell reports its own conditions

    BOOT_TRACE("[boot] loading resources\n");
    Resources res;
    if (!resources_load(&res, "game.json")) {
        // Reported through fatal_user_error, not a bare printf: on Windows the
        // console may not be visible, and a pack rejected for declaring no
        // render.mode would otherwise look like the game vanishing silently.
        char body[1024];
        snprintf(body, sizeof body,
                 "Failed to load game resources from:\n  %s\n\n"
                 "A pack must declare a render mode, for example:\n"
                 "  \"render\": { \"mode\": \"legacy\" }\n\n"
                 "See the console output for the specific reason.",
                 pack_path);
        fatal_user_error("OpenBounty: cannot load game pack", body);
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

    // A modern pack's TrueType font decides the line height the layout reads
    // (status band, dialog panel), so its metrics are computed first, CPU
    // only; the texture comes after the window.
    bfont_preload_metrics((const struct Resources *)&res);

    // Geometry comes from the pack, so it must be resolved before the window
    // and the render target are sized. resources_load already rejected a pack
    // that declared no render.mode.
    layout_init((const struct Resources *)&res);

    // The window opens at the smallest screen and stays there: the zoom
    // rises only when the player maximises it or goes full screen
    // (present.c held_zoom). --window WxH wins: it is the whole point of
    // the flag.
    int base_w = CL_WINDOW_W;
    int base_h = CL_WINDOW_H;
    if (want_win_w > 0 && want_win_h > 0) { base_w = want_win_w; base_h = want_win_h; }

    // The window: resizable, no cursor, no exit key, 60fps -- all of that is
    // frame_host_window_open's, so every platform opens it the same way.
    // Demo mode paces itself via per-beat holds in shell_demo.c; the frame rate
    // stays at the human 60fps cap. Human play is 60fps too.
    BOOT_TRACE("[boot] resources loaded, opening the window\n");
    frame_host_window_open(base_w, base_h,
                           res.title[0] ? res.title : "OpenBounty");
    // --touch: after the window, because the host clears its input state as it
    // opens. Everything a finger changes now draws on this desk.
    if (force_touch) input_host_force_touch();
    {
        int dw = 0, dh = 0;
        frame_host_display_size(&dw, &dh);
        BOOT_TRACE("[boot] window %dx%d display %dx%d\n",
                   frame_host_window_width(), frame_host_window_height(), dw, dh);
    }
    int min_w, min_h;
    layout_min_window(&min_w, &min_h);
    frame_host_window_min_size(min_w, min_h);
    if (want_fullscreen) frame_host_window_fullscreen_toggle();

    // Font strip and palette come from the manifest. They were compiled in
    // here, which meant every pack had to ship a file named for the game the
    // extractor was written against.
    BOOT_TRACE("[boot] baking the font\n");
    bfont_init((const struct Resources *)&res);
    BOOT_TRACE("[boot] font ready\n");
    palette_init(res.sprites.palette);
    ui_set_panel_frame(res.sprites.panel_frame);

    BOOT_TRACE("[boot] loading sprites\n");
    Sprites sprites;
    sprites_load(&sprites, &res);
    BOOT_TRACE("[boot] sprites loaded\n");
    tile_cache_attach(&res);
    // Cosmetic tile variants (draw-time only): picked from the game's own
    // seed, which the frame hands tilevar each time it draws the map.
    tilevar_init((const struct Resources *)&res, 0);

    // Fit the layout to the window before anything allocates a target. In
    // modern the buffer is the window divided by the scale; without this the
    // startup screens get a target sized from the pack's declared viewport
    // rather than the actual window, and clip.
    layout_fit_window(frame_host_window_width(), frame_host_window_height(),
                      present_scale(frame_host_window_width(),
                                    frame_host_window_height()));

    // Allocate the render target early so startup screens can
    // draw into it.
    BOOT_TRACE("[boot] creating the frame buffer\n");
    RenderTexture2D render_target_startup =
        gfx_target_create(CL_SCREEN_W, CL_SCREEN_H);
    BOOT_TRACE("[boot] frame buffer id=%u\n", render_target_startup.id);
    gfx_texture_point(render_target_startup.texture);

    // Pre-game flow: pick slot + new-game wizard. --demo / --autoplay bypass
    // the wizard and synthesize a deterministic new game: the agent plays it,
    // so there's no human to run the menus. Seed defaults to the mode default.
    // Modern's in-game New Game comes back here, past the splashes, to the
    // title menu (back_to_title).
    bool back_to_title = false;
    bool audio_started = false;
title:;
    StartupChoice choice = { 0 };
    if (demo_mode || autoplay_mode || gallery_dir || puzzle_sweep_dir || intro_movie) {
        if (seed_index < 0)
            seed_index = autoplay_mode ? AUTOPLAY_DEFAULT_SEED_INDEX
                                       : DEMO_DEFAULT_SEED_INDEX;
        choice.action = STARTUP_NEW;
        choice.slot = 0;
        // Autoplay's class/level come from --autoplay-hero / --autoplay-level
        // (an unknown class has already stopped the program). Demo keeps its
        // fixed profile.
        const char *hero = (autoplay_mode && autoplay_hero && autoplay_hero[0])
                               ? autoplay_hero : AUTOPLAY_HERO_CLASS;
        snprintf(choice.class_id, sizeof choice.class_id, "%s",
                 autoplay_mode ? hero : "");
        snprintf(choice.name, sizeof choice.name, "%s",
                 autoplay_mode ? AUTOPLAY_HERO_NAME : DEMO_HERO_NAME);
        choice.difficulty = autoplay_mode ? autoplay_level
                                          : DEMO_HERO_DIFFICULTY;
    } else if (!startup_flow(&res, &sprites,
                             &render_target_startup, &choice, back_to_title)) {
        // User quit before choosing.
        audio_shutdown();
        recorder_shutdown();
        gfx_target_free(render_target_startup);
        sprites_unload(&sprites);
        bfont_shutdown();
        lattice_shutdown();
        tile_cache_shutdown();
        frame_host_window_close();
        resources_free(&res);
        pack_stack_clear();
        return 0;
    }

    back_to_title = false;

    Map map = { 0 };
    Fog fog = { 0 };

    Game game = { 0 };
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
#ifndef NDEBUG
        fprintf(stdout, "[main] seed: %d%s\n", game.seed_index,
                seed_index >= 0 ? "" : " (derived)");
#endif

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
        // Modern goes straight into the game.
        if (!CL_IS_MODERN) player_io_note(&game, NULL, body);
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
        player_io_note(&game, NULL, body);
    }

    // Load the starting zone now that placements are populated. For NEW
    // games, GameInit chose the zone marked is_home (falling back to
    // res.world.starting_zone); for LOAD, Game.position.zone was restored
    // from the save.
    const char *load_zone = game.position.zone;
    if (!load_zone[0]) load_zone = res.world.starting_zone;
    if (!GameReloadZoneMap(&game, &map, load_zone)) {
        gfx_target_free(render_target_startup);
        sprites_unload(&sprites);
        bfont_shutdown();
        lattice_shutdown();
        tile_cache_shutdown();
        frame_host_window_close();
        resources_free(&res);
        pack_stack_clear();
        return 1;
    }
    game.position.last_x = game.position.x;
    game.position.last_y = game.position.y;
    game.hud_visible = true;

    // spawn_game calls clear_fog() in the NEW path only; load_game
    // trusts the fog bytes stored in the save. Match that behavior -- for
    // LOAD, the Fog struct was populated by SaveGameRead above.
    if (choice.action == STARTUP_NEW) {
        FogRevealFor(&res, &fog, &map, game.position.x, game.position.y);
    }

    bool quit_requested = false;
    bool menu_asking = false;          // modern game menu: Yes/No before exit, load or overwrite
    bool castle_asking = false;        // modern home castle: Yes/No before a tribute
    bool town_asking = false;          // modern town: a Yes/No before an action is up
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
    views_menu_bind(&menu_cbs, &menu_ctx);
    views_menu_set_debug(debug_flag);

    // Render target was allocated above (render_target_startup)
    // so the pre-game flow can draw into it; reuse here.
    RenderTexture2D render_target = render_target_startup;
    if (puzzle_sweep_dir) {
        // The puzzle view of every catalog world, then quit (#108).
        int rc = gallery_puzzle_sweep(&game, &map, &fog, &res, &sprites, &render_target, puzzle_sweep_dir);
        gfx_target_free(render_target);
        sprites_unload(&sprites);
        frame_host_window_close();
        resources_free(&res);
        return rc;
    }
    if (intro_movie) {
        // The Introduction rendered to a silent video, then quit (#154).
        int rc = gallery_intro_movie(&res, &render_target, intro_movie);
        gfx_target_free(render_target);
        sprites_unload(&sprites);
        frame_host_window_close();
        resources_free(&res);
        return rc;
    }
    if (gallery_dir) {
        // Layout audit: capture every modern screen, then quit.
        int rc = gallery_run(&game, &map, &fog, &res, &sprites, &render_target, gallery_dir);
        gfx_target_free(render_target);
        sprites_unload(&sprites);
        frame_host_window_close();
        resources_free(&res);
        return rc;
    }

    // Recorder: when --movie was passed, capture state + framebuffer
    // PNGs on logical-tick mutations into /tmp/openbounty-movie-<pid>, then mux
    // to one .mp4 at shutdown. Off when --movie wasn't passed, in
    // which case every recorder_capture() call is a free no-op.
    if (movie_requested && !recorder_active()) {
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

    // Audio: open the device, load the music streams, start the openworld
    // track, honouring the saved Sounds + Music toggles. Opening the device
    // can block for tens of seconds (a slow sound server under WSL); done here
    // on the main thread it froze the window on the last startup frame between
    // choosing a character and the game appearing. Interactive play opens it
    // in the background and the game starts at once; --autoplay and --demo
    // keep the synchronous open, because game options are part of their
    // byte-exact state and a no-device fallback landing mid-run would change
    // it.
    if (audio_started)              { /* back from the title menu: already open */ }
    else if (demo_mode || autoplay_mode) audio_init_blocking(&res);
    else                            audio_init(&res);
    audio_started = true;
    // No playback device: pin Sounds/Music/Volume to 0 so the controls panel
    // and the live audio push agree (the rows are also greyed out and ignore
    // input). Checked again each frame below until the open resolves.
    bool audio_pinned = false;
    if (audio_status() == AUDIO_UNAVAILABLE) {
        game.stats.options[1] = 0;  // Sounds
        game.stats.options[5] = 0;  // Music
        game.stats.options[6] = 0;  // Volume
        audio_pinned = true;
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

    // The load is over: keys pressed while it ran must not answer the first
    // dialog or step the hero.
    input_host_flush(0.3);

    while (!frame_host_should_close() && !quit_requested) {
        // Audio: drive music streaming + react to live toggle changes.
        audio_set_sounds_enabled(game.stats.options[1] != 0);
        audio_set_music_enabled (game.stats.options[5] != 0);
        audio_set_master_volume (game.stats.options[6]);
        audio_tick();
        if (!audio_pinned && audio_status() == AUDIO_UNAVAILABLE) {
            // The background open finished without a device.
            game.stats.options[1] = 0;
            game.stats.options[5] = 0;
            game.stats.options[6] = 0;
            audio_pinned = true;
        }
        if ((input_key_down(KEY_LEFT_ALT) || input_key_down(KEY_RIGHT_ALT)) &&
            input_key_pressed(KEY_ENTER)) {
            frame_host_window_fullscreen_toggle();
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
        shell_pump_note(&game);

        // Sync engine VIEWS (town / home-castle / own-castle / alcove / dwelling /
        // win / lose) from the queue onto the shell view stack. The human
        // dismisses them with ESC as before. Autoplay never reaches here (it
        // owns the frame and acks REQ_VIEWs directly), so it never accumulates a
        // view -- the views_push stack-overflow defect is gone.
        shell_pump_player_io_view(&game);

        // ==== Input ====

        // Modern town: an action waits on its Yes/No.
        if (town_asking) {
            PromptResult r = prompt_update();
            if (r != PROMPT_RESULT_NONE) town_asking = false;
            if (r == PROMPT_RESULT_YES) views_town_confirm_yes(&game);
            goto end_input;
        }

        if (menu_asking) {
            PromptResult r = prompt_update();
            if (r != PROMPT_RESULT_NONE) menu_asking = false;
            if (r == PROMPT_RESULT_YES) modern_gamemenu_confirm_yes();
            goto end_input;
        }
        if (castle_asking) {
            PromptResult r = prompt_update();
            if (r != PROMPT_RESULT_NONE) castle_asking = false;
            if (r == PROMPT_RESULT_YES) modern_castle_confirm_yes(&game);
            goto end_input;
        }

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
                    run_end_cartoon(&render_target, &res, &sprites, &game);
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
                    run_end_cartoon(&render_target, &res, &sprites, &game);
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

        // Modern temple and dwelling screens stay up through their answer; once
        // no prompt, dialog or queued request is left, they close.
        if (CL_IS_MODERN && (views_active() == VIEW_ALCOVE || views_active() == VIEW_DWELLING)) {
            // A message raised over the screen (the Augur's reply, a refusal)
            // becomes the in-lay's text rather than a dialog box over the scene.
            if (dialog_is_active()) {
                loc_deal_absorb(dialog_body_text());
                dialog_dismiss();
            }
            if (loc_deal_pending() && !prompt_is_active() && !loc_deal_revealed()) {
                // The place first: its action row brings up the outcome, Leave exits.
                MlList l = { 2, *loc_deal_cursor(), NULL, NULL };
                int row = -1;
                MlEvent ev = ml_list_input(&l, TOUCH_LIST_PROMPT, &row);
                *loc_deal_cursor() = l.cursor;
                if (ev == ML_EV_BACK || (ev == ML_EV_ACT && row == 1)) { loc_deal_clear(); views_dismiss(); }
                else if (ev == ML_EV_ACT) loc_deal_reveal();
                goto end_input;
            }
            if (loc_deal_pending() && !prompt_is_active()) {
                // The outcome is on show: Continue -- any key, or a tap -- closes
                // it, as every outcome does.
                if (ui_any_key_pressed() || touch_tapped_row(TOUCH_LIST_PROMPT) == 0) {
                    loc_deal_clear();
                    views_dismiss();
                }
                goto end_input;
            }
            if (!prompt_is_active() && !dialog_is_active() && !player_io_front(&game)) {
                loc_deal_clear();
                views_dismiss();
            }
        }

        // prompt_dispatch_tick returns false while a message dialog is up, so
        // the chain falls through to the dialog branch below and the message is
        // dismissed before the prompt is answered (issue #19).
        if (prompt_dispatch_tick(&sctx)) {
            // prompt is up (or just resolved); skip the rest of input
        } else if (views_active() == VIEW_MENU) {
            if (CL_IS_MODERN) {
                modern_gamemenu_update(&game);
                char ask[RES_BANNER_LEN];
                if (modern_gamemenu_take_confirm(&game, ask, sizeof ask)) {
                    prompt_yes_no_open(NULL, ask);
                    prompt_set_req_kind(PIO_ASK_IN_PLACE);   // the menu asks in its own page
                    menu_asking = true;
                }
                int slot = 0;
                switch (modern_gamemenu_take_action(&slot)) {
                    case GM_DO_SAVE: {
                        menu_ctx.slot = slot;
                        menu_save(&menu_ctx);
                        views_dismiss();
                        char body[RES_BANNER_LEN], sb[12], db[12];
                        snprintf(sb, sizeof sb, "%d", slot + 1);
                        snprintf(db, sizeof db, "%d", game.stats.days_left);
                        ResTemplateVar sv[] = { { "SLOT", sb }, { "NAME", game.character.name },
                                                { "RANK", game.character.cls.rank_title }, { "DAYS", db } };
                        resources_format_template(body, sizeof body, res.banners.save_done, sv, 4);
                        player_io_note(&game, res.banners.save_done_title, body);
                        break;
                    }
                    case GM_DO_LOAD: menu_ctx.slot = slot; if (menu_load(&menu_ctx)) views_dismiss(); break;
                    // New Game was answered Yes in its page: back to the title.
                    case GM_DO_NEW:  views_dismiss(); back_to_title = true; quit_requested = true; break;
                    case GM_DO_EXIT: menu_quit(&menu_ctx); break;
                    case GM_DO_NONE: break;
                }
            } else {
                views_menu_update(&menu_cbs, &menu_ctx);
            }
            // A Debug row (--debug only) closes the menu and names a cheat.
            int cheat = views_menu_take_cheat();
            if (cheat >= 0 &&
                cheat_apply((CheatAction)cheat, &game, &map, &fog, &res, &sprites,
                            &render_target) == CHEAT_DISPATCHED_TERMINAL) {
                continue;
            }
        } else if (views_active() == VIEW_TOWN) {
            views_town_update(&game);
            char ask[RES_BANNER_LEN];
            if (views_town_take_confirm(&game, ask, sizeof ask) != TOWN_CONFIRM_NONE) {
                prompt_yes_no_open(NULL, ask);
                prompt_set_req_kind(PIO_ASK_IN_PLACE);   // the town asks in its own panel
                town_asking = true;
            }
        } else if (views_active() == VIEW_CONTROLS && CL_IS_MODERN) {
            views_controls_input(&game);      // the one Controls input (views.c)
        } else if (views_active() == VIEW_CONTROLS) {
            // Navigate rows with Up/Down; digit keys 1..N jump to and
            // advance the matching row; ESC / any unhandled key closes.
            touch_request(TOUCH_CHROME_BACK);
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
            // Modern: a Back row after the pack's settings, as on every menu
            // page. There is no Scale row: the scale follows the surface.
            int back_row = CL_IS_MODERN ? count : -1;
            if (CL_IS_MODERN) count += 1;
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
                    if (cur == back_row) views_dismiss();
                    else                 views_controls_advance(&game, vis_map[cur]);
                } else if (input_key_pressed(KEY_ESCAPE) ||
                           input_key_pressed(KEY_C) ||
                           gamepad_pressed_cancel()) {
                    views_dismiss();
                } else {
                    // Digit 1..count selects and advances that row.
                    for (int k = 0; k < count && k < 9; k++) {
                        if (input_key_pressed(KEY_ONE + k)) {
                            views_controls_set_cursor(k);
                            if (k == back_row) views_dismiss();
                            else               views_controls_advance(&game, vis_map[k]);
                            break;
                        }
                    }
                }
            } else if (ui_any_key_pressed()) {
                views_dismiss();
            }
        } else if (views_active() == VIEW_SPELLS && CL_IS_MODERN) {
            // The one spells page (views_spells_input); a chosen spell is cast.
            if (views_spells_update_modern(&game)) {
                int spell_idx = views_spells_chosen();
                if (spell_idx >= 0) dispatch_adventure_spell(&game, spell_idx);
            }
        } else if (views_active() == VIEW_SPELLS) {
            // Spell casting with Left/Right to switch columns, A-G to cast.
            if (views_spells_update()) {
                int spell_idx = views_spells_chosen();
                if (spell_idx >= 0) {
                    dispatch_adventure_spell(&game, spell_idx);
                }
            } else if ((!CL_IS_MODERN || !views_spells_casting()) && ui_any_key_pressed()) {
                // Legacy, or only looking: any other key closes. Modern casting
                // keeps its keys -- the arrows move the cursor and Escape closes
                // (views_spells_update), so an arrow never closes the list.
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
        } else if ((views_active() == VIEW_HOME_CASTLE || views_active() == VIEW_OWN_CASTLE) &&
                   !dialog_is_active() && CL_IS_MODERN) {
            // Modern castles: the town-style screen owns its input.
            if (modern_castle_update(&game)) {
                views_dismiss();
                pending_castle_id[0] = '\0';
            }
            char ask[RES_BANNER_LEN];
            if (views_active() == VIEW_HOME_CASTLE && modern_castle_take_confirm(&game, ask, sizeof ask)) {
                prompt_yes_no_open(NULL, ask);
                prompt_set_req_kind(PIO_ASK_IN_PLACE);   // the castle asks in its own scene
                castle_asking = true;
            }
        } else if (views_active() == VIEW_HOME_CASTLE && !dialog_is_active()) {
            //  /  +
            // throne_room_or_barracks gamestate accepts A and B
            // to enter sub-flows; ESC pops back to the overworld.
            //
            // Gated on !dialog_is_active() so the audience modal popup
            // (run_audience_dialog -> open_dialog) is handled by the
            // downstream dialog branch instead -- dialog has its own
            // SPACE-to-advance flow over the persistent backdrop.
            // Modern: up/down and Enter or a tap pick a row; A and B act
            // directly in both modes.
            int pick = -1;
            {
                SelList l = { 2, screen_home_castle_cursor() };
                int row = -1;
                if (sel_input(&l, TOUCH_LIST_CASTLE, 0, &row) == SEL_CONFIRM) pick = row;
                screen_home_castle_set_cursor(l.cursor);
            }
            if (input_key_pressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                views_dismiss();
                pending_castle_id[0] = '\0';
            } else if (pick == 0 || input_key_pressed(KEY_A)) {
                // A) Recruit Soldiers -- push the dedicated recruit
                // sub-screen (5 troops + gold + key hint).
                screen_recruit_soldiers_open(&game);
            } else if (pick == 1 || input_key_pressed(KEY_B)) {
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
                // Modern: up/down move the slot cursor and Enter or a tap
                // acts on it; the letters act directly in both modes.
                // Row 0 is the Garrison / Remove mode, rows 1-5 the slots.
                int chosen = -1;
                {
                    SelList l = { 6, screen_own_castle_cursor() };
                    int row = -1;
                    SelEvent ev = sel_input(&l, TOUCH_LIST_CASTLE, 0, &row);
                    screen_own_castle_set_cursor(l.cursor);
                    if (ev == SEL_CONFIRM) {
                        if (row == 0) screen_own_castle_toggle_mode();
                        else          chosen = row - 1;
                    }
                }
                for (int k = 0; k < 5; k++) {
                    if (k != chosen && !input_key_pressed(KEY_A + k)) continue;
                    const char *cid = screen_own_castle_castle_id();
                    int rc;
                    if (screen_own_castle_is_garrison_mode()) {
                        rc = GameGarrisonTroop(&game, cid, k);
                        if (rc == 2) {
                            player_io_note(&game, NULL,
                                game.res->banners.cannot_garrison_last);
                        } else if (rc == 1) {
                            player_io_note(&game, NULL,
                                game.res->banners.no_troop_slots);
                        }
                    } else {
                        rc = GameUngarrisonTroop(&game, cid, k);
                        if (rc == 1) {
                            player_io_note(&game, NULL,
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
            //
            // Modern draws the toggle as a row under the map (with the orb):
            // Enter, Space or a tap on it swaps the map.
            bool worldmap_row = false;
            bool has_orb = false;
            bool wm_modern = CL_IS_MODERN && views_active() == VIEW_WORLDMAP && modern_worldmap_input(&game, &map, &fog);
            if (wm_modern) {
                // Modern: the places list owns the keys (src/modern/views_render.c).
            } else if (views_active() == VIEW_WORLDMAP) {
                int zi = -1;
                for (int i = 0; i < res.zone_count; i++) {
                    if (strcmp(res.zones[i].id, game.position.zone) == 0) {
                        zi = i; break;
                    }
                }
                has_orb = (zi >= 0 && zi < game.world.zone_count && game.world.orbs_found[zi]);
                if (has_orb && CL_IS_MODERN) {
                    SelList l = { 1, 0 };
                    worldmap_row = sel_input(&l, TOUCH_LIST_PROMPT, 0, NULL) == SEL_CONFIRM;
                }
            }
            if (wm_modern) {
                // handled above
            } else if (worldmap_row) {
                views_render_worldmap_toggle_hero_only();
            } else if (views_active() == VIEW_WORLDMAP && input_key_pressed(KEY_SPACE)) {
                if (has_orb) {
                    views_render_worldmap_toggle_hero_only();
                } else {
                    views_dismiss();
                }
            } else if (CL_IS_MODERN ? ui_any_key_pressed_ex(false)
                                    : ui_any_key_pressed_ex(views_closes_on_tap(views_active()))) {
                // Legacy: one rule for every page (views_closes_on_tap): a
                // sheet or a picture goes away under a finger; a page with
                // rows waits for a row. Modern: the page itself decides a tap
                // (src/modern/page.c) and presses a key, which lands here.
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
                // Touch: tap the target tile; ESC chrome cancels.
                int cx, cy;
                map_tap_cell(&game, &map, &cx, &cy);
                ui_map(CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H, cx, cy,
                       CL_TILE_W, CL_TILE_H, 0);
                touch_request(TOUCH_CHROME_BACK);
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
                        player_io_note(&game, spell_header("bridge", "Bridge"), msg);
                    } else {
                        resources_format_template(msg, sizeof msg,
                                                  bn->spell_bridge_invalid,
                                                  NULL, 0);
                        dialog_dismiss();
                        player_io_note(&game, spell_header("bridge", "Bridge"), msg);
                    }
                } else if (input_key_pressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                    bridge_state = BRIDGE_STATE_NONE;
                    dialog_dismiss();
                }
            } else {
                // Ctrl-Q on the legacy Q save message quits, as its words
                // say; on any other message it closes the message and asks
                // the map's "Quit without saving?" question. Any other key
                // advances the page or dismisses.
                bool ctrl = input_key_down(KEY_LEFT_CONTROL) || input_key_down(KEY_RIGHT_CONTROL);
                if (ctrl && input_key_pressed(KEY_Q)) {
                    dialog_dismiss();
                    if (fast_quit_save_message_open()) quit_requested = true;
                    else fast_quit_open();
                    fast_quit_set_save_message(false);
                } else if (ui_any_key_pressed()) {
                    fast_quit_set_save_message(false);
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
                            player_io_note(&game, body, "");
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
            //
            // Touch: the hero is always the centre tile of the viewport, so
            // a tap picks its direction relative to centre -- one tap, one
            // injected direction key, one step. The verbs are the left rail
            // and the game menu; there is no action bar.
            {
                int cx, cy;
                map_tap_cell(&game, &map, &cx, &cy);
                ui_map(CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H, cx, cy,
                       CL_TILE_W, CL_TILE_H, 0);
            }
            InputState in = input_poll();
            // The rail's tap, if any, is the frame's action: it fires the
            // same case the key would (src/modern/rail.c).
            InputAction ra = rail_tapped();
            if (ra == INPUT_ACTION_NONE) ra = hud_tapped();   // the other column
            if (ra != INPUT_ACTION_NONE) in.action = ra;
            // Goto (#70): a walk under way takes one step a beat; any key or
            // tap of the player's stops it, and does nothing else.
            bool walking = false;
            if (shell_goto_active()) {
                if (in.action != INPUT_ACTION_NONE || in.dx || in.dy) {
                    shell_goto_cancel();
                    in = (InputState){ 0, 0, INPUT_ACTION_NONE };
                } else {
                    int gdx, gdy;
                    if (shell_goto_next(&game, frame_host_time(), &gdx, &gdy)) {
                        in.dx = gdx; in.dy = gdy;
                        walking = true;
                    }
                }
            }
            shell_dispatch_action(&sctx, &in);
            if (in.action == INPUT_ACTION_NONE && (in.dx || in.dy)) {
                bool moved = GameStep(&game, &map, &fog, &res, in.dx, in.dy);
                if (moved) last_step_time = frame_host_time();
                if (walking) shell_goto_after_step(&game, moved);
            }
        }

        // Animate hero sprite -- only advance frames during
        // or just after a step. options[3] = 1 means Animation On
        // ( "Animation = On" with value 1; classic
        // controls_menu displays "On" when val==1).
        if (frame_host_time() >= hero_anim_next) {
            bool anim_enabled = (game.stats.options[3] != 0);
            bool animating = anim_enabled && (frame_host_time() - last_step_time < 0.4);
            game.anim_moving = animating;
            // A pack that ships an idle animation keeps ticking while the hero
            // stands still, so he breathes instead of freezing. Without one,
            // standing still snaps back to frame 0 as it always has.
            if (animating || sprites_anim_present(&sprites.hero_idle)) {
                // Free-running tick: each sprite strip folds this onto its
                // own declared cycle length at draw time, so the counter
                // must not assume any particular frame count here.
                game.anim_frame = ob_anim_tick(game.anim_frame);
            } else {
                game.anim_frame = 0;   // idle pose
            }
            // Delay option: 0.05 + options[0] * 0.05 (range 0-5 = 0.05-0.30)
            double interval = 0.05 + game.stats.options[0] * 0.05;
            hero_anim_next = frame_host_time() + interval;
        }

        end_input:;

        // ==== Draw ====
        // Modern takes whatever the window gives, so the render target changes
        // size when the window does. Legacy is fixed and this is a no-op.
        present_refit(&render_target);

        // Render into the offscreen target.
        present_begin(&render_target);
        draw_frame(&game, &map, &fog, &sprites);
        present_end();

        // Blit centered + letterboxed at the largest integer scale that fits,
        // within the bounds in layout.h (see present.c).
        present_scaled(render_target);
        frame_host_end_frame();

        // Screenshot on demand: backtick (`) -> screenshots/shot_NNNN.png.
        screenshot_tick(render_target, "shot");

    }

    // New Game from the in-game menu: clear what the session left on screen
    // and go back to the title menu. The window, audio and render target stay.
    if (back_to_title && !frame_host_should_close()) {
        views_set(VIEW_NONE);
        dialog_dismiss();
        prompt_dismiss();
        pending_reset();
        MapFree(&map);
        FogFree(&fog);
        GameFree(&game);
        goto title;
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
    gfx_target_free(render_target);
    bfont_shutdown();
    sprites_unload(&sprites);
    lattice_shutdown();
    tile_cache_shutdown();
    MapFree(&map);
    FogFree(&fog);
    GameFree(&game);
    frame_host_window_close();
    resources_free(&res);
    pack_stack_clear();
    return 0;
}
