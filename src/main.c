// localtime_r needs POSIX 199506+; 200809L covers everything we use.
#define _POSIX_C_SOURCE 200809L

#include "frame_host.h"
#include "input_host.h"
#include "shell_autoplay.h"
#include "autoplay.h"
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

// Town/Castle Gate destination picker (player_io_message + letter dispatch,
// like the F10 debug menu). Implementation in src/shell_gate.{c,h}.
#include "shell_gate.h"

// Re-boot a fresh NEW-game world for a given seed, in place. Used by the demo
// (attract) loop to restart from a clean board each cycle — it mirrors the
// new-game boot path below (GameInit + zone load + tile mutations + fog reveal)
// but skips the human-facing "please wait" modal. Returns false if the zone
// failed to load (the caller should stop looping). `name` is the class id ("" =
// default class 0, as autoplay uses).
static bool demo_reboot_world(Game *game, Map *map, Fog *fog, Resources *res,
                              unsigned long seed) {
    FogInit(fog);
    game->res = res;
    game->seed = seed;                 // deterministic world for this cycle
    GameInit(game, "Demo", /*pclass=*/0, DIFFICULTY_NORMAL, NULL);

    const char *load_zone = game->position.zone;
    if (!load_zone[0]) load_zone = res->world.starting_zone;
    if (!MapLoadZoneWithPlacements(map, res, load_zone, game)) return false;
    GameApplyTileMutations(game, map, load_zone);
    game->position.last_x = game->position.x;
    game->position.last_y = game->position.y;
    game->hud_visible = true;
    FogReveal(fog, map, game->position.x, game->position.y, res->world.fog_sight);
    return true;
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
    // keeps stderr's old promptness — every line flushes immediately, and an abort/crash
    // path (e.g. nav_fail) cannot lose its dump to an unflushed block buffer.
    setvbuf(stdout, NULL, _IOLBF, 0);

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
    // --autoplay: drive the game with the autoplay planner.
    //   --autoplay            -> VISIBLE: opens the window, plays at game speed,
    //                            hands off to the player on termination.
    //   --autoplay --headless -> HEADLESS: no window; runs to a verdict and
    //                            exits (early-exit mode, like --extract). For CI
    //                            and bulk seed sweeps. Seed defaults to 1.
    bool autoplay_mode = false;
    bool headless_mode = false;
    // --demo: curated attract mode. Visible only — builds a SHORT zone-0 plan
    // and replays it at a watchable pace, then LOOPS (re-boots + replays) forever
    // until the window closes. Implemented as autoplay-with-a-DemoCap; shares the
    // whole visible-replay path. Pairs with --seed (fixed showcase) and --movie.
    bool demo_mode = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            printf("openbounty build %s\n", OPENBOUNTY_VERSION);
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            printf("openbounty build %s\n"
                   "Usage: %s [--fullscreen] [--pack <name|path>] [--save-dir <dir>]\n"
                   "       %*s [--movie [<path>]] [--seed N] [--version]\n"
                   "       %s --autoplay [--headless] [--seed N]\n"
                   "       %s --demo [--seed N] [--movie [<path>]]\n"
                   "       %s --extract [--out-dir <dir>]\n"
                   "       %s --pack-dir <src_dir> <out_zip>\n",
                   OPENBOUNTY_VERSION, argv[0],
                   (int)strlen(argv[0]), "",
                   argv[0], argv[0], argv[0], argv[0]);
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
        } else if (strcmp(a, "--autoplay") == 0) {
            autoplay_mode = true;
        } else if (strcmp(a, "--demo") == 0) {
            // Demo runs ON the autoplay machinery (build + visible replay), so it
            // enables autoplay_mode too; the demo flag selects the curated,
            // paced, looping behavior at begin/handoff time.
            demo_mode = true;
            autoplay_mode = true;
        } else if (strcmp(a, "--headless") == 0) {
            headless_mode = true;
        } else if (strcmp(a, "--save-dir") == 0 && i + 1 < argc) {
            SavePathSetDirOverride(argv[++i]);
        } else if (strcmp(a, "--pack-dir") == 0 && i + 2 < argc) {
            pack_dir_src = argv[++i];
            pack_dir_dst = argv[++i];
        }
    }

    // Early-exit CLI modes (--pack-dir and --extract). Both run to
    // completion and return; no window opens. Implementations live in
    // shell_earlyexit.{c,h}.
    if (pack_dir_src) return shell_run_pack_dir_mode(pack_dir_src, pack_dir_dst);
    if (extract_mode) return shell_run_extract_mode(extract_out_dir);

    // --demo is an inherently VISIBLE attract loop: --headless is meaningless
    // for it, so drop headless rather than fall into the headless oracle below.
    if (demo_mode && headless_mode) {
        fprintf(stdout, "--demo is visible-only; ignoring --headless\n");
        headless_mode = false;
    }

    // Headless autoplay: --autoplay --headless runs the pack-winnability
    // oracle with NO window and exits with the verdict. Dispatched here, before
    // InitWindow, like --extract. The visible path (--autoplay alone) is wired
    // later in the main loop. autoplay() boots its own engine + pack (the
    // engine-only consumer pattern), so no shell/window state is needed.
    if (autoplay_mode && headless_mode) {
        // The structured decision trace (diag.h) is ALWAYS ON at FULL verbosity
        // in headless: it goes to stdout alongside the verdict line (the "aptrace"
        // prefix keeps it filterable) and is observation-only, so it never changes
        // the run. No env knobs
        // or flags — headless always emits the complete trace (level 2: STEP/
        // PREDICT/CAND/INTERV/RUNCAP plus NAV/RECRUIT detail), in greppable
        // key=val text.
        AutoplayConfig cfg = {
            .seed = forced_seed ? forced_seed : 1,
            .pack_dir = NULL,           // default pack (assets/kings-bounty)
            .trace = true,              // structured trace to stdout (always-on)
            .trace_level = 2,           // full detail (includes NAV/RECRUIT)
            .trace_json = false,        // greppable key=val text
            .zone_scope = -1,           // all zones — the binary's one behavior
                                        // (the zone-0 regression baseline lives
                                        // in the test suite, not the CLI)
        };
        AutoplayResult ar;
        if (!autoplay(&cfg, &ar)) {
            fprintf(stdout, "--autoplay --headless: run setup failed\n");
            return 2;
        }
        printf("autoplay: seed=%llu verdict=%s objectives=%d/%d turns=%d\n",
               (unsigned long long)ar.seed, autoplay_verdict_str(ar.verdict),
               ar.objectives_done, ar.objectives_total, ar.turns);
        return ar.verdict == AUTOPLAY_VERDICT_FAILED ? 1 : 0;
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

    int base_w = CL_WINDOW_W;    // 640
    int base_h = CL_WINDOW_H;    // 400

    unsigned int window_flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
    SetConfigFlags(window_flags);
    InitWindow(base_w, base_h, res.title[0] ? res.title : "OpenBounty");
    SetWindowMinSize(320, 200);
    if (want_fullscreen) ToggleFullscreen();
    // Autoplay is a watched REPLAY, not interactive play: run the frame loop as
    // fast as the machine allows (no 60fps cap) so the recorded plan plays back
    // quickly. The step/dwell pacing lives in shell_autoplay.c (0 = full speed for
    // autoplay; a tuned watchable pace for demo mode); the frame rate itself
    // should never throttle it. Human play stays at 60fps.
    SetTargetFPS(autoplay_mode ? 0 : 60);
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

    // Pre-game flow: pick slot + new-game wizard. --autoplay bypasses the
    // wizard and synthesizes a deterministic new game: the planner
    // drives it, so there's no human to run the menus. Seed defaults to 1.
    StartupChoice choice = { 0 };
    if (autoplay_mode) {
        if (forced_seed == 0) forced_seed = 1;
        choice.action = STARTUP_NEW;
        choice.slot = 0;
        snprintf(choice.class_id, sizeof choice.class_id, "%s", "");
        snprintf(choice.name, sizeof choice.name, "%s", "Autoplay");
        choice.difficulty = DIFFICULTY_NORMAL;
        choice.seed = (unsigned long)forced_seed;
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
        // Class id → pclass index comes straight from the ClassDef catalog.
        // Unknown ids fall back to class 0 so the game still starts.
        const ClassDef *cd = class_by_id(choice.class_id);
        int pclass = (cd && cd->index >= 0) ? cd->index : 0;

        // --seed forces a deterministic per-game world (villain
        // placements, dwellings, scepter, salt). GameInit honors any
        // pre-set g->seed; zero falls back to time+name+class.
        if (forced_seed != 0) {
            game.seed = forced_seed;
            fprintf(stdout, "[main] forced seed: %llu\n",
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

        //  — post-load_game informational modal.
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
    // trusts the fog bytes stored in the save. Match that behavior — for
    // LOAD, the Fog struct was populated by SaveGameRead above.
    if (choice.action == STARTUP_NEW) {
        FogReveal(&fog, &map, game.position.x, game.position.y,
                  res.world.fog_sight);
    }

    bool quit_requested = false;
    // Set when an autoplay run WON (scepter recovered): the win cartoon + win
    // screen play as the ending, then control is handed to the human on the
    // cleared world. The engine's show_win_game sets game_over (the real
    // game's terminal state); we clear it once the player dismisses the win
    // view so the hand-off is a LIVE board, not a frozen one.
    bool autoplay_won_handoff = false;
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
        game.stats.options[6] = 0;  // Music
        game.stats.options[7] = 0;  // Volume
    }
    // Visible autoplay replays silently: while it owns the frames, sounds and
    // music are suppressed WITHOUT touching the player's saved options, so the
    // per-frame sync below restores the player's own settings at hand-off.
    audio_set_sounds_enabled(!autoplay_mode && game.stats.options[1] != 0);
    audio_set_music_enabled (!autoplay_mode && game.stats.options[6] != 0);
    audio_set_master_volume (game.stats.options[7]);       // Volume 0..9
    audio_set_track(AUDIO_TRACK_OPENWORLD);

    double hero_anim_next = 0.0;
    double last_step_time = 0.0;   // classic: only animate shortly after a step
    bool prev_overlay = false;

    // Visible autoplay: engage the planner against the live game. From
    // here the main loop hands each adventure frame to shell_autoplay_tick
    // instead of human input. Visible autoplay is a REPLAY: it plays back as fast
    // as the machine allows (step_delay 0 = advance every frame, FPS uncapped
    // above). The watchable, slowed-down pace is demo mode's job, not autoplay's.
    if (autoplay_mode) {
        ShellCtx actx = {
            .game = &game, .map = &map, .fog = &fog, .res = &res,
            .sprites = &sprites, .render_target = &render_target,
            .quit_requested = &quit_requested,
        };
        if (!shell_autoplay_begin_ex(&actx, /*demo=*/demo_mode)) {
            fprintf(stdout, "%s: planner init failed; playing manually\n",
                    demo_mode ? "--demo" : "--autoplay");
            autoplay_mode = false;
            demo_mode = false;
        }
    }

    while (!frame_host_should_close() && !quit_requested) {
        // Audio: drive music streaming + react to live toggle changes. While
        // visible autoplay owns the frame the run is silent (suppressed here,
        // not in the saved options); the frame after hand-off this same sync
        // re-enables audio from the player's own settings.
        audio_set_sounds_enabled(!autoplay_mode && game.stats.options[1] != 0);
        audio_set_music_enabled (!autoplay_mode && game.stats.options[6] != 0);
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

        // Drain engine player-IO MESSAGES (chest results, pickups, sign-posts,
        // captures, etc.) into the shell dialog (M3) so the engine's uniform
        // messages render through the existing dialog UI. One per frame when the
        // dialog slot is free; the human dismisses with any key as before.
        shell_pump_player_io_message(&game);

        // Sync engine VIEWS (town / home-castle / own-castle / alcove / dwelling /
        // win / lose) from the queue onto the shell view stack (M4). The human
        // dismisses them with ESC as before. Autoplay never reaches here (it
        // owns the frame and acks REQ_VIEWs directly), so it never accumulates a
        // view — the views_push stack-overflow defect is gone.
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

        // Visible autoplay owns the frame: the committed-plan executor answers
        // prompts / dismisses dialogs / takes one step, and we skip all
        // human-input branches. Visible mode builds the plan once then
        // replays it via the SAME plan_build + plan_exec_step path the headless
        // runner uses, so the rendered run matches it for a given seed.
        if (autoplay_mode) {
            bool ap_done = false;
            shell_autoplay_tick(&sctx, frame_host_time(), &ap_done);
            if (ap_done && demo_mode && !frame_host_should_close()) {
                // DEMO (attract) LOOP: the curated showcase finished — re-boot a
                // fresh board and replay it, forever, until the window is closed.
                // (A window-close abort mid-replay also sets ap_done; the guard
                // above lets the while-loop exit instead of restarting.)
                // A demo plan never sets game.stats.won (the scepter is excluded),
                // so there is no win cartoon to play; just restart cleanly. Keep
                // the SAME seed each cycle for a repeatable, hand-tuned showcase.
                //
                // MOVIE EXCEPTION: --demo --movie wants ONE clean arc, not an
                // ever-growing file. When recording, the first completed loop
                // ends the run so the recorder finalizes a single-arc MP4 at
                // shutdown (below). Without --movie the demo loops forever.
                if (recorder_active()) {
                    fprintf(stdout, "--demo --movie: first arc complete — "
                            "finalizing movie and exiting\n");
                    shell_autoplay_end();
                    quit_requested = true;
                    goto end_input;
                }
                shell_autoplay_end();
                gate_state = GATE_STATE_NONE;   // clear any armed gate picker
                if (!demo_reboot_world(&game, &map, &fog, &res,
                                       (unsigned long)forced_seed) ||
                    !shell_autoplay_begin_ex(&sctx, /*demo=*/true)) {
                    fprintf(stdout, "--demo: restart failed; handing off\n");
                    autoplay_mode = false;
                    demo_mode = false;
                }
                goto end_input;   // this frame rendered; next frame resumes the demo
            }
            if (ap_done) {
                char body[160];
                shell_autoplay_summary(&sctx, body, sizeof body);
                fprintf(stdout, "--autoplay: %s\n", body);
                shell_autoplay_end();
                autoplay_mode = false;
                demo_mode = false;
                // The executor replays gate teleports via the engine directly
                // and never runs the shell's gate picker, so a gate spell cast
                // during the run can leave gate_state armed (GATE_STATE_SELECT).
                // Left set, the per-frame gate_menu_tick pops the gate picker
                // on top of the win/handoff view the very next frame, and that
                // picker swallows every keypress without dismissing — the hang.
                // Clear it so the hand-off view owns the frame.
                gate_state = GATE_STATE_NONE;
                if (game.stats.won) {
                    // The scepter was recovered: play the win cartoon and the
                    // win screen, exactly as a human victory does — the
                    // autoplay executor applies the dig directly (not through
                    // player_io_answer), so the cartoon must be driven here
                    // rather than via the prompt-dispatch won_game path.
                    // show_win_game sets game_over (the real ending); flag the
                    // run so that once the player DISMISSES the win view we
                    // clear game_over and hand off the LIVE cleared board.
                    run_end_cartoon(&render_target, &res, &sprites);
                    show_win_game(&game, &res);
                    autoplay_won_handoff = true;
                } else {
                    // PARTIAL / aborted run: HAND OFF to the human. Open the
                    // summary directly as a shell dialog (NOT via the engine
                    // player-IO queue) so the standard dialog-dismissal branch
                    // owns it immediately — routing it through the queue left
                    // it stuck on screen once the autoplay driver was torn
                    // down, with nothing to drive the dwell/ack handshake.
                    open_dialog("Autoplay", body);
                }
                // This frame already rendered; control returns next frame.
            }
            goto end_input;
        }

        // Town/Castle Gate destination picker (shell_gate.{c,h}): when a gate
        // spell armed gate_state, show the visited-destination list in the
        // message box and teleport on a letter press. Same shape as the F10
        // debug menu (player_io_message + GetKeyPressed letter dispatch). When
        // the picker is up it consumes input, so skip the rest of the frame.
        if (gate_menu_tick(&game, &map, &fog) == GATE_MENU_ACTIVE) {
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
                // WIN HAND-OFF: dismissing the autoplay win screen returns
                // control to the human on the cleared world. show_win_game set
                // game_over (the real terminal ending); clear it here so the
                // next frame is a LIVE adventure board the player drives, not a
                // frozen one that the GameIsOver branch would quit to menu.
                if (autoplay_won_handoff && dismissing == VIEW_WIN) {
                    game.stats.game_over = false;
                    autoplay_won_handoff = false;
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
                        // — that's a deviation that's now removed.
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
            // Standard adventure-mode bindings. No ESC→menu,
            // no TAB, no Space→HUD.
            InputState in = input_poll();
            shell_dispatch_action(&sctx, &in);
            if (in.action == INPUT_ACTION_NONE && (in.dx || in.dy)) {
                if (GameStep(&game, &map, &fog, &res, in.dx, in.dy)) {
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

    shell_autoplay_end();   // frees the planner if --autoplay engaged
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
