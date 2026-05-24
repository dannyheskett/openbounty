// localtime_r needs POSIX 199506+; 200809L covers everything we use.
#define _POSIX_C_SOURCE 200809L

#include "frame_host.h"
#include "input_host.h"
#include "autoplay/core.h"
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

// End-of-week two-screen sequence (astrology → budget). Implementation
// in src/shell_weekend.{c,h}. Call schedule_week_end() whenever a week
// boundary is crossed; the main loop calls pump_week_end_dialog to pop
// each pending screen before processing input.
#include "shell_weekend.h"


// run_audience_dialog() (King Maximus audience flow) moved to
// src/shell_audience.{c,h}.
#include "shell_audience.h"

// Per-frame draw_frame() dispatcher moved to src/shell_frame.{c,h}.
#include "shell_frame.h"

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char **argv) {
    return shell_run_game(argc, argv, NULL);
}

int shell_run_game(int argc, char **argv, ShellRunHooks *hooks) {
    // Minimal CLI parsing ( /  read_cmd_config equivalent).
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
    // 0 means "derive from time + name + class" (default). Non-zero
    // forces a deterministic per-game seed for reproducible runs.
    uint64_t forced_seed = 0;
    // --autoplay: drive the game through a scripted input sequence
    // (via input_host/frame_host shims) and assert on the end-state.
    // Exits with the autoplay routine's return code. See
    // src/autoplay.h.
    bool autoplay_mode = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            printf("openbounty build %s\n", OPENBOUNTY_VERSION);
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            printf("openbounty build %s\n"
                   "Usage: %s [--fullscreen] [--pack <name|path>] [--save-dir <dir>]\n"
                   "       %*s [--movie [<path>]] [--seed N] [--version]\n"
                   "       %s --extract [--out-dir <dir>]\n"
                   "       %s --pack-dir <src_dir> <out_zip>\n"
                   "       %s --autoplay [--visible] [--uncapped]\n",
                   OPENBOUNTY_VERSION, argv[0],
                   (int)strlen(argv[0]), "",
                   argv[0], argv[0], argv[0]);
            return 0;
        } else if (strcmp(a, "--fullscreen") == 0) {
            want_fullscreen = true;
        } else if (strcmp(a, "--pack") == 0 && i + 1 < argc) {
            pack_arg = argv[++i];
        } else if (strcmp(a, "--extract") == 0) {
            extract_mode = true;
        } else if (strcmp(a, "--out-dir") == 0 && i + 1 < argc) {
            extract_out_dir = argv[++i];
        } else if (strcmp(a, "--movie") == 0) {
            movie_requested = true;
            // Optional next-arg path: only consumed if it doesn't look
            // like another flag (no leading "-"). Without an arg, the
            // recorder picks an auto-named timestamp file.
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                movie_path_arg = argv[++i];
            }
        } else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
            forced_seed = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(a, "--save-dir") == 0 && i + 1 < argc) {
            SavePathSetDirOverride(argv[++i]);
        } else if (strcmp(a, "--pack-dir") == 0 && i + 2 < argc) {
            pack_dir_src = argv[++i];
            pack_dir_dst = argv[++i];
        } else if (strcmp(a, "--autoplay") == 0) {
            autoplay_mode = true;
        }
    }

    // Early-exit CLI modes (--pack-dir and --extract). Both run to
    // completion and return; no window opens. Implementations live in
    // shell_earlyexit.{c,h}.
    if (pack_dir_src) return shell_run_pack_dir_mode(pack_dir_src, pack_dir_dst);
    if (extract_mode) return shell_run_extract_mode(extract_out_dir);

    // --autoplay dispatch happens only when called from entry-point
    // main() (hooks==NULL). The autoplay routine itself calls
    // shell_run_game with hooks set; recursing into the dispatch
    // would loop forever.
    if (!hooks && autoplay_mode) {
        return autoplay_run(argc, argv);
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
                // own KB.EXE. The body has to stand alone — on
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
            fprintf(stderr, "[extract] no pack found; running first-run extraction from %s\n", in_dir);
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
            fprintf(stderr, "[extract] wrote %s\n", wide_zip);
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
        fprintf(stderr, "Failed to load game resources from %s.\n", pack_path);
        pack_stack_clear();
        return 1;
    }

    int base_w = CL_WINDOW_W;    // 640
    int base_h = CL_WINDOW_H;    // 400

    unsigned int window_flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
    bool autoplay_visible  = false;
    bool autoplay_handoff  = false;
    bool autoplay_uncapped = false;
    if (hooks) {
        // In autoplay mode the window stays hidden by default — CI
        // shouldn't pop a window and we don't need vsync-paced
        // frames. --visible opens the window AND throttles the
        // simulation to wall-clock 30fps so a human can follow it.
        // --handoff (implied by --visible) hands control to the
        // keyboard when the autoplay flow finishes successfully,
        // instead of exiting the process. --uncapped removes the
        // 30fps wall-clock throttle (and skips raylib's SetTargetFPS
        // post-handoff) so visible autoplay runs as fast as the
        // host can render.
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--visible") == 0) {
                autoplay_visible = true;
                autoplay_handoff = true;
            } else if (strcmp(argv[i], "--handoff") == 0) {
                autoplay_handoff = true;
            } else if (strcmp(argv[i], "--uncapped") == 0) {
                autoplay_uncapped = true;
            }
        }
        if (!autoplay_visible) window_flags |= FLAG_WINDOW_HIDDEN;
    }
    SetConfigFlags(window_flags);
    InitWindow(base_w, base_h, res.title[0] ? res.title : "OpenBounty");
    SetWindowMinSize(320, 200);
    if (want_fullscreen) ToggleFullscreen();
    // In autoplay mode we don't want SetTargetFPS's vsync — the test
    // clock advances per loop iteration. Without --visible, leaving
    // FPS uncapped lets autoplay run as fast as possible. With
    // --visible (and no --uncapped), throttle the test clock to 30fps
    // so the simulation tracks wall time and is actually watchable.
    if (!hooks) SetTargetFPS(60);
    if (hooks && autoplay_visible && !autoplay_uncapped) {
        // Wall-clock pace = 30 fps in visible autoplay — half the
        // native 60fps. Slow enough to follow each tick's logical
        // action (one walk step or one combat decision) without
        // feeling sluggish.
        frame_host_set_test_fps(30);
    }
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

    // Pre-game flow: pick slot + new-game wizard.
    if (hooks && hooks->before_startup) hooks->before_startup(hooks);
    StartupChoice choice = { 0 };
    if (!startup_flow(&res, &sprites,
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
        // Class id → pclass index comes straight from the ClassDef catalog.
        // Unknown ids fall back to class 0 so the game still starts.
        const ClassDef *cd = class_by_id(choice.class_id);
        int pclass = (cd && cd->index >= 0) ? cd->index : 0;

        // --seed forces a deterministic per-game world (villain
        // placements, dwellings, scepter, salt). GameInit honors any
        // pre-set g->seed; zero falls back to time+name+class.
        if (forced_seed != 0) {
            game.seed = forced_seed;
            fprintf(stderr, "[main] forced seed: %llu\n",
                    (unsigned long long)forced_seed);
        }
        GameInit(&game, choice.name, pclass, choice.difficulty, NULL);

        //  — post-create_game informational modal.
        char body[256];
        snprintf(body, sizeof(body),
                 "%s the %s,\n\n"
                 "A new game is being created. "
                 "Please wait while I perform "
                 "godlike actions to make this "
                 "game playable.",
                 game.character.name,
                 game.character.cls.rank_title);
        open_dialog(NULL, body);
    } else {
        // LOAD: hydrate Game from the chosen slot. GameInit first with
        // defaults so all fields have sane values the loader can overwrite.
        GameInit(&game, "Hero", 0, DIFFICULTY_NORMAL, NULL);
        char path[512];
        const char *pid = res.pack_id[0] ? res.pack_id : NULL;
        if (SavePathGetSlot(pid, choice.slot, path, sizeof(path))) {
            SaveResult r = SaveGameRead(path, &game, &map, &fog);
            if (r != SAVE_OK) {
                fprintf(stderr, "Load slot %d failed: %s\n",
                        choice.slot, SaveResultText(r));
            }
        }

        //  — post-load_game informational modal.
        char body[256];
        snprintf(body, sizeof(body),
                 "%s the %s,\n\n"
                 "Please wait while I prepare "
                 "a suitable environment for "
                 "your bountying enjoyment!",
                 game.character.name,
                 game.character.cls.rank_title);
        open_dialog(NULL, body);
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
    // trusts the fog bytes stored in the save. Match that behavior — for
    // LOAD, the Fog struct was populated by SaveGameRead above.
    if (choice.action == STARTUP_NEW) {
        FogReveal(&fog, &map, game.position.x, game.position.y,
                  res.world.fog_sight);
    }

    bool quit_requested = false;
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
                fprintf(stderr, "--movie: cannot resolve user data dir\n");
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
            fprintf(stderr, "--movie: recording to %s\n", movie_path);
        }
    }

    // Audio: open device, load music streams, start the openworld
    // track. Honors the user's saved Sounds + Music toggles.
    // Autoplay headless (no --visible) skips audio entirely — opening
    // the device on a headless box is noisy and the test doesn't
    // listen anyway.
    // Autoplay (headless OR visible) skips audio entirely: opening the
    // device can hang for several seconds on systems without working
    // audio (headless boxes, WSL2 without pulse, CI runners), and the
    // autoplay isn't going to listen anyway.
    if (!hooks) {
        audio_init(&res);
    }
    if (!audio_is_available()) {
        // No playback device: pin Sounds/Music/Volume to 0 so the
        // controls panel and the live audio push agree. The rows are
        // also rendered grayed-out and ignore input.
        game.stats.options[1] = 0;  // Sounds
        game.stats.options[6] = 0;  // Music
        game.stats.options[7] = 0;  // Volume
    }
    audio_set_sounds_enabled(game.stats.options[1] != 0);  // Sounds toggle
    audio_set_music_enabled (game.stats.options[6] != 0);  // Music toggle
    audio_set_master_volume (game.stats.options[7]);       // Volume 0..9
    audio_set_track(AUDIO_TRACK_OPENWORLD);

    double hero_anim_next = 0.0;
    double last_step_time = 0.0;   // classic: only animate shortly after a step
    bool prev_overlay = false;

    if (hooks && hooks->after_init) hooks->after_init(hooks, &game, &map, &fog, &res);

    // frame_host_should_close itself advances the input/frame test
    // clock (one tick per check) so we don't need explicit tick
    // calls here. The per-frame hook is called right after the
    // tick fires for this iteration so the hook sees the freshly
    // promoted active key and can queue follow-up input.
    int gp_frame_no = 0;
    int gp_exit_code = 0;
    while (!frame_host_should_close() && !quit_requested) {
        if (hooks && hooks->per_frame) {
            ShellRunVerdict v = hooks->per_frame(hooks, &game, &map, &fog, &res, gp_frame_no);
            if (v == SHELL_RUN_EXIT_PASS) {
                if (autoplay_handoff) {
                    // Hand over to the human: unhook autoplay, switch
                    // input shim back to raylib, switch frame host to
                    // raylib (60fps vsync — or uncapped if --uncapped
                    // was passed), open audio if it wasn't already,
                    // and continue the loop without per_frame.
                    hooks->per_frame = NULL;
                    input_host_use_raylib();
                    frame_host_use_raylib();
                    if (!autoplay_uncapped) SetTargetFPS(60);
                    audio_init(&res);
                    audio_set_sounds_enabled(game.stats.options[1] != 0);
                    audio_set_music_enabled (game.stats.options[6] != 0);
                    audio_set_master_volume (game.stats.options[7]);
                    audio_set_track(AUDIO_TRACK_OPENWORLD);
                    fprintf(stderr, "[main] autoplay complete — handing off to keyboard\n");
                    continue;
                }
                gp_exit_code = 0;
                break;
            }
            if (v == SHELL_RUN_EXIT_FAIL) { gp_exit_code = 1; break; }
        }
        gp_frame_no++;
        // Audio: drive music streaming + react to live toggle changes.
        audio_set_sounds_enabled(game.stats.options[1] != 0);
        audio_set_music_enabled (game.stats.options[6] != 0);
        audio_set_master_volume (game.stats.options[7]);
        audio_tick();
        if ((input_key_down(KEY_LEFT_ALT) || input_key_down(KEY_RIGHT_ALT)) &&
            input_key_pressed(KEY_ENTER)) {
            ToggleFullscreen();
        }

        // F10 → debug cheat menu (implementation in shell_cheats.{c,h}).
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

        // Pop pending week-end screens (astrology → budget) before input.
        pump_week_end_dialog(&game);

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
        } else if (views_active() == VIEW_HOME_CASTLE && !dialog_is_active()) {
            //  /  +
            // : throne_room_or_barracks gamestate accepts A and B
            // to enter sub-flows; ESC pops back to the overworld.
            //
            // Gated on !dialog_is_active() so the audience modal popup
            // (run_audience_dialog → open_dialog) is handled by the
            // downstream dialog branch instead — dialog has its own
            // SPACE-to-advance flow over the persistent backdrop.
            if (input_key_pressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                views_dismiss();
                pending_castle_id[0] = '\0';
            } else if (input_key_pressed(KEY_A)) {
                // A) Recruit Soldiers — push the dedicated recruit
                // sub-screen (5 troops + gold + key hint).
                screen_recruit_soldiers_open(&game);
            } else if (input_key_pressed(KEY_B)) {
                // B) Audience with the King — modal popup over the
                // castle backdrop. Run after panel render so it overlays.
                const ResCastle *rc2 =
                    resources_castle_by_id(&res, pending_castle_id);
                run_audience_dialog(&game, rc2);
            }
        } else if (views_active() == VIEW_RECRUIT_SOLDIERS && !dialog_is_active()) {
            // recruit_soldiers — the screen owns
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
            // returns to the overworld. Errors per .
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
                            open_dialog(NULL,
                                game.res->banners.cannot_garrison_last);
                        } else if (rc == 1) {
                            open_dialog(NULL,
                                game.res->banners.no_troop_slots);
                        }
                    } else {
                        rc = GameUngarrisonTroop(&game, cid, k);
                        if (rc == 1) {
                            open_dialog(NULL,
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
                views_dismiss();
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
                        open_dialog(spell_header("bridge", "Bridge"), msg);
                    } else {
                        resources_format_template(msg, sizeof msg,
                                                  bn->spell_bridge_invalid,
                                                  NULL, 0);
                        dialog_dismiss();
                        open_dialog(spell_header("bridge", "Bridge"), msg);
                    }
                } else if (input_key_pressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                    bridge_state = BRIDGE_STATE_NONE;
                    dialog_dismiss();
                }
            } else if (gate_state == GATE_STATE_SELECT) {
                // Handle gate destination selection (A-Z or ESC).
                // matches the typed letter to the destination's first
                // letter (not its catalog index).
                int key = input_get_key_pressed();
                if (key == 0) {
                    // No key pressed
                } else if (key == KEY_ESCAPE) {
                    gate_state = GATE_STATE_NONE;
                    dialog_dismiss();
                } else if (key >= KEY_A && key <= KEY_Z) {
                    char letter = (char)('a' + (key - KEY_A));
                    bool valid = false;
                    const char *dest_zone = NULL;
                    int dest_x = -1, dest_y = -1;
                    int spell_idx = spell_index_by_id(
                        gate_mode == 0 ? "castle_gate" : "town_gate");

                    if (gate_mode == 0) {
                        // Castle gate: find visited castle whose name
                        // (or id, falling back) starts with `letter`.
                        // Skip the home castle (King Maximus's) — per
                        // spec, the spell can't teleport there; it
                        // would also collide on 'O' with "Ophiraund"
                        // since the home castle's display name is
                        // "of King Maximus".
                        for (int ci = 0; ci < GAME_CASTLES; ci++) {
                            if (!game.castles[ci].visited) continue;
                            if (ci >= game.res->castle_count) continue;
                            const ResCastle *rc = &game.res->castles[ci];
                            if (strcmp(rc->special.flow, "audience") == 0)
                                continue;
                            char first = (char)tolower((unsigned char)
                                (rc->name[0] ? rc->name[0] : rc->id[0]));
                            if (first != letter) continue;
                            if (!rc->zone[0]) continue;
                            valid = true;
                            dest_zone = rc->zone;
                            dest_x = rc->gate_x >= 0 ? rc->gate_x : rc->x;
                            dest_y = rc->gate_y >= 0 ? rc->gate_y : rc->y;
                            break;
                        }
                    } else {
                        // Town gate: find visited town whose name starts
                        // with `letter`. Towns track visited via
                        // game.towns[idx].visited keyed by catalog index.
                        int tn = game.res->town_count;
                        if (tn > GAME_TOWNS) tn = GAME_TOWNS;
                        for (int ti = 0; ti < tn; ti++) {
                            if (!game.towns[ti].visited) continue;
                            const ResTown *rt = &game.res->towns[ti];
                            char first = (char)tolower((unsigned char)
                                (rt->name[0] ? rt->name[0] : rt->id[0]));
                            if (first != letter) continue;
                            if (!rt->zone[0]) continue;
                            valid = true;
                            dest_zone = rt->zone;
                            dest_x = rt->gate_x >= 0 ? rt->gate_x : rt->x;
                            dest_y = rt->gate_y >= 0 ? rt->gate_y : rt->y;
                            break;
                        }
                    }

                    const ResBanners *bn = &game.res->banners;
                    const char *header = spell_header(
                        gate_mode == 0 ? "castle_gate" : "town_gate",
                        gate_mode == 0 ? "Castle Gate" : "Town Gate");
                    char msg[RES_BANNER_LEN];
                    if (valid && dest_zone &&
                        GameSwitchZone(&game, &map, &fog, dest_zone)) {
                        if (spell_idx >= 0) game.spells.counts[spell_idx]--;
                        if (dest_x >= 0 && dest_y >= 0) {
                            game.position.x = dest_x;
                            game.position.y = dest_y;
                            game.position.last_x = dest_x;
                            game.position.last_y = dest_y;
                        }
                        resources_format_template(msg, sizeof msg,
                                                  bn->spell_gate_teleported,
                                                  NULL, 0);
                        dialog_dismiss();
                        gate_state = GATE_STATE_NONE;
                        open_dialog(header, msg);
                    } else {
                        resources_format_template(msg, sizeof msg,
                                                  bn->spell_gate_invalid,
                                                  NULL, 0);
                        dialog_dismiss();
                        gate_state = GATE_STATE_NONE;
                        open_dialog(header, msg);
                    }
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
                        // — that's a deviation that's now removed.
                        if (pending_audience_message[0]) {
                            char body[700];
                            snprintf(body, sizeof(body), "%s",
                                     pending_audience_message);
                            pending_audience_message[0] = '\0';
                            open_dialog(body, "");
                        }
                    }
                }
            }
        } else if (GameIsOver(&game)) {
            // Game over: any key returns to menu.
            if (ui_any_key_pressed()) quit_requested = true;
        } else {
            // Standard adventure-mode bindings. No ESC→menu,
            // no TAB, no Space→HUD.
            InputState in = input_poll();
            shell_dispatch_action(&sctx, &in);
            if (in.action == INPUT_ACTION_NONE && (in.dx || in.dy)) {
                if (step_try(&game, &map, &fog, &res, in.dx, in.dy)) {
                    last_step_time = frame_host_time();
                }
            }
        }

        // Animate hero sprite — only advance frames during
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

        // Blit centered + letterboxed. Pick the largest integer scale
        // that fits the current window (or display, in fullscreen) so
        // fullscreen actually uses the extra pixels. Minimum 2x.
        BeginDrawing();
        ClearBackground(BLACK);
        int win_w = GetScreenWidth();
        int win_h = GetScreenHeight();
        int sx = win_w / CL_SCREEN_W;
        int sy = win_h / CL_SCREEN_H;
        int scale = (sx < sy) ? sx : sy;
        if (scale < 2) scale = 2;
        int dst_w = CL_SCREEN_W * scale;
        int dst_h = CL_SCREEN_H * scale;
        int dst_x = (win_w - dst_w) / 2;
        int dst_y = (win_h - dst_h) / 2;
        // RenderTexture2D is y-flipped; negate the src height.
        Rectangle src = { 0, 0,
                          (float)render_target.texture.width,
                          -(float)render_target.texture.height };
        Rectangle dst = { (float)dst_x, (float)dst_y,
                          (float)dst_w, (float)dst_h };
        DrawTexturePro(render_target.texture, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

        // Screenshots (dev builds only; see screenshot.c):
        //   - backtick (`) → "shot" prefix, on demand
        //   - VIEW_CHARACTER rising edge → "char" prefix (auto, for layout diffs)
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
    return hooks ? gp_exit_code : 0;
}
