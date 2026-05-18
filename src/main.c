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

// ===========================================================================
// Render path. Draws into the 320x200 target at 1x.
// ===========================================================================

static void draw_frame(const Game *game, const Map *map, const Fog *fog,
                               const Sprites *sprites) {
    ClearBackground(BLACK);
    chrome_draw(game, sprites);
    map_render_draw(game, map, fog, sprites);
    hud_draw(game, sprites);
    overlay_draw(game, map, fog, sprites);
}

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char **argv) {
    // Minimal CLI parsing ( /  read_cmd_config equivalent).
    bool want_fullscreen = false;
    const char *pack_arg = NULL;     // --pack <name|path>
    bool extract_mode = false;        // --extract: build pack from KB.EXE then exit
    const char *extract_out_dir = NULL; // --out-dir <dir>: extract to loose tree
    const char *pack_dir_src = NULL;  // --pack-dir <src> <dst>: zip a loose asset tree
    const char *pack_dir_dst = NULL;
    int record_cap = 0;          // 0 → recorder_init uses default
    const char *record_dir = NULL;
    bool encode_movie = false;
    // 0 means "derive from time + name + class" (default). Non-zero
    // forces a deterministic per-game seed for reproducible runs.
    uint64_t forced_seed = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            printf("openbounty build %s\n", OPENBOUNTY_VERSION);
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            printf("openbounty build %s\n"
                   "Usage: %s [--fullscreen] [--pack <name|path>] [--save-dir <dir>] [--extract [--out-dir <dir>]]\n"
                   "       %*s [--record <dir>] [--record-cap N] [--encode-movie] [--seed N] [--version]\n"
                   "       %s --pack-dir <src_dir> <out_zip>\n",
                   OPENBOUNTY_VERSION, argv[0], (int)strlen(argv[0]), "", argv[0]);
            return 0;
        } else if (strcmp(a, "--fullscreen") == 0) {
            want_fullscreen = true;
        } else if (strcmp(a, "--pack") == 0 && i + 1 < argc) {
            pack_arg = argv[++i];
        } else if (strcmp(a, "--extract") == 0) {
            extract_mode = true;
        } else if (strcmp(a, "--out-dir") == 0 && i + 1 < argc) {
            extract_out_dir = argv[++i];
        } else if (strcmp(a, "--record") == 0 && i + 1 < argc) {
            record_dir = argv[++i];
        } else if (strcmp(a, "--record-cap") == 0 && i + 1 < argc) {
            record_cap = atoi(argv[++i]);
        } else if (strcmp(a, "--encode-movie") == 0) {
            encode_movie = true;
        } else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
            forced_seed = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(a, "--save-dir") == 0 && i + 1 < argc) {
            SavePathSetDirOverride(argv[++i]);
        } else if (strcmp(a, "--pack-dir") == 0 && i + 2 < argc) {
            pack_dir_src = argv[++i];
            pack_dir_dst = argv[++i];
        }
    }

    // --pack-dir <src> <dst>: zip a pre-extracted asset tree into a
    // .openbounty archive. Used by the Makefile to build the shipped
    // pack from assets/<name>/. No window.
    if (pack_dir_src) {
        if (!pack_zip_dir(pack_dir_src, pack_dir_dst)) {
            fprintf(stderr, "pack-dir: failed to write %s\n", pack_dir_dst);
            return 1;
        }
        return 0;
    }

    // --extract: produce a .openbounty pack (or, with --out-dir, a
    // loose tree) from the user's KB.EXE distribution. Inputs come from
    // cwd's legacy/bin/ subdir to match the standalone tool's contract;
    // if that's missing, the engine looks in cwd directly. Output is
    // <user-data>/openbounty/<pack_id>.openbounty, unless --out-dir is
    // given.
    if (extract_mode) {
        const char *in_dir = "legacy/bin";
        struct stat sst;
        if (stat(in_dir, &sst) != 0) {
            // Fall back to cwd if KB.EXE sits there directly.
            if (stat("KB.EXE", &sst) == 0) in_dir = ".";
            else {
                fprintf(stderr,
                        "extract: KB.EXE not found. Place your game files "
                        "in legacy/bin/ or in the current directory.\n");
                return 2;
            }
        }
        if (extract_out_dir) {
            int rc = extract_run(in_dir, extract_out_dir);
            return rc == 0 ? 0 : 1;
        }
        char user_dir[PACK_ENTRY_PATH_MAX];
        if (!SavePathGetDir(user_dir, sizeof user_dir)) {
            fprintf(stderr, "extract: cannot resolve user data dir\n");
            return 1;
        }
        char tmp_dir[PACK_ENTRY_PATH_MAX + 32];
        snprintf(tmp_dir, sizeof tmp_dir, "%s/.tmp-extract", user_dir);
        pack_rmtree(tmp_dir);
        int rc = extract_run(in_dir, tmp_dir);
        if (rc != 0) {
            pack_rmtree(tmp_dir);
            return 1;
        }
        // Read pack_id from the just-emitted game.json so the output
        // filename matches what discovery will surface.
        char pid[64] = "kings-bounty";
        {
            Pack *p = pack_open(tmp_dir);
            if (p) {
                const char *id = pack_id(p);
                if (id && id[0]) snprintf(pid, sizeof pid, "%s", id);
                pack_close(p);
            }
        }
        char out_zip[PACK_ENTRY_PATH_MAX + 96];
        snprintf(out_zip, sizeof out_zip, "%s/%s.openbounty",
                 user_dir, pid);
        if (!pack_zip_dir(tmp_dir, out_zip)) {
            fprintf(stderr, "extract: failed to write %s\n", out_zip);
            pack_rmtree(tmp_dir);
            return 1;
        }
        pack_rmtree(tmp_dir);
        fprintf(stderr, "extract: wrote %s\n", out_zip);
        return 0;
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

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(base_w, base_h, res.title[0] ? res.title : "OpenBounty");
    SetWindowMinSize(320, 200);
    if (want_fullscreen) ToggleFullscreen();
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

    // Pre-game flow: pick slot + new-game wizard.
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

    // Recorder: in-memory ring of (state JSON + framebuffer PNG)
    // snapshots, captured on logical-tick mutations (steps, combat
    // actions, dialogs, view changes). Disk dump is opt-in via
    // --record <dir>.
    recorder_init(record_cap, 0);
    recorder_attach_state(&game, &map, &fog);
    recorder_attach_render_target(&render_target);
    if (record_dir) recorder_set_record_dir(record_dir);
    recorder_capture("init");

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
    audio_set_sounds_enabled(game.stats.options[1] != 0);  // Sounds toggle
    audio_set_music_enabled (game.stats.options[6] != 0);  // Music toggle
    audio_set_master_volume (game.stats.options[7]);       // Volume 0..9
    audio_set_track(AUDIO_TRACK_OPENWORLD);

    double hero_anim_next = 0.0;
    double last_step_time = 0.0;   // classic: only animate shortly after a step
    bool prev_overlay = false;

    while (!WindowShouldClose() && !quit_requested) {
        // Audio: drive music streaming + react to live toggle changes.
        audio_set_sounds_enabled(game.stats.options[1] != 0);
        audio_set_music_enabled (game.stats.options[6] != 0);
        audio_set_master_volume (game.stats.options[7]);
        audio_tick();
        if ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) &&
            IsKeyPressed(KEY_ENTER)) {
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

        if (prompt_is_active()) {
            // Bottom-frame prompt (yes/no or numeric). When it
            // returns a result, dispatch based on pending_flow.
            PromptResult r = prompt_update();
            if (r != PROMPT_RESULT_NONE) {
                switch (pending_flow) {
                    case FLOW_SEARCH:
                        if (r == PROMPT_RESULT_YES) {
                            // :
                            // scepter tile → win_game; otherwise spend
                            // 10 days and show the "revealed nothing"
                            // banner.
                            bool on_scepter =
                                (strcmp(game.scepter.zone, game.position.zone) == 0 &&
                                 game.scepter.x == game.position.x &&
                                 game.scepter.y == game.position.y);
                            if (on_scepter) {
                                run_end_cartoon(&render_target, &res, &sprites);
                                show_win_game(&game, &res);
                            } else {
                                //  spends
                                // 10 days. If the period crosses a week
                                // boundary, the adventure loop's
                                // end_of_week runs — so we must queue our
                                // schedule_week_end for parity.
                                int paid = 0;
                                int weeks = GameSpendDays(&game,
                                    res.tuning.search_cost_days, &paid);
                                if (game.stats.game_over) {
                                    show_lose_game(&game, &res);
                                } else {
                                    if (weeks > 0) {
                                        schedule_week_end(&game, paid);
                                    }
                                    open_dialog(NULL,
                                        "Your search of this area has\n"
                                        "revealed nothing.");
                                }
                            }
                        }
                        break;
                    case FLOW_DISMISS_ARMY:
                        if (r >= PROMPT_RESULT_1 && r <= PROMPT_RESULT_5) {
                            int slot = r - PROMPT_RESULT_1;
                            if (slot < 0 || slot >= GAME_ARMY_SLOTS) break;
                            // : if
                            // this is the LAST non-empty stack, prompt
                            // "sent back to King in disgrace" before
                            // triggering temp_death.
                            int occupied = 0;
                            for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                                if (game.army[i].id[0] && game.army[i].count > 0) {
                                    occupied++;
                                }
                            }
                            if (occupied <= 1) {
                                // Remember which slot to dismiss (always
                                // slot 0 in practice, but keep explicit).
                                pending_castle_id[0] = (char)('0' + slot);
                                pending_castle_id[1] = '\0';
                                pending_flow = FLOW_DISMISS_LAST;
                                {
                                    char body[RES_BANNER_LEN];
                                    resources_format_template(body, sizeof body,
                                                              game.res->banners.body_dismiss_last,
                                                              NULL, 0);
                                    prompt_yes_no_open(
                                        game.res->ui.dt_dismiss_last, body);
                                }
                                goto prompt_chained;
                            }
                            // Non-last dismissal: zero the slot, then
                            // compact so the army view has no gaps.
                            game.army[slot].id[0] = '\0';
                            game.army[slot].count = 0;
                            GameCompactArmy(&game);
                        }
                        break;
                    case FLOW_DISMISS_LAST: {
                        if (r == PROMPT_RESULT_YES) {
                            perform_temp_death(&game, &map, &fog, &res);
                        }
                        pending_castle_id[0] = '\0';
                        break;
                    }
                    case FLOW_SIEGE_MONSTER: {
                        // lay_siege monster branch:
                        // yes → run combat, win transfers castle to player;
                        // loss → temp_death.
                        if (r == PROMPT_RESULT_YES) {
                            CastleRecord *cr =
                                GameFindCastle(&game, pending_castle_id);
                            const ResCastle *rc = resources_castle_by_id(
                                &res, pending_castle_id);
                            CombatTarget tgt = { 0 };
                            tgt.name = rc && rc->name[0]
                                ? rc->name : pending_castle_id;
                            if (cr) {
                                tgt.garrison = cr->garrison;
                                tgt.garrison_slots = GAME_ARMY_SLOTS;
                            }
                            CombatResult outcome =
                                RunCombat(&game, &sprites, &render_target, COMBAT_MODE_CASTLE, &tgt);
                            if (outcome == COMBAT_RESULT_WIN && cr) {
                                cr->owner_kind = CASTLE_OWNER_PLAYER;
                                cr->visited = true;
                                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                                    cr->garrison[s].id[0] = '\0';
                                    cr->garrison[s].count = 0;
                                }
                            } else if (outcome == COMBAT_RESULT_LOSS) {
                                perform_temp_death(&game, &map, &fog, &res);
                            }
                        }
                        pending_castle_id[0] = '\0';
                        break;
                    }
                    case FLOW_SIEGE_VILLAIN: {
                        // lay_siege villain branch:
                        // yes → run combat; win fulfills the contract
                        // (reward + mark caught + cycle rotation + rank-up),
                        // castle becomes player-owned. Loss → temp_death.
                        if (r == PROMPT_RESULT_YES) {
                            CastleRecord *cr =
                                GameFindCastle(&game, pending_castle_id);
                            const ResCastle *rc = resources_castle_by_id(
                                &res, pending_castle_id);
                            const VillainDef *v = (cr && cr->villain_id[0])
                                ? villain_by_id(cr->villain_id) : NULL;
                            CombatTarget tgt = { 0 };
                            tgt.name = v && v->name[0] ? v->name
                                     : (rc && rc->name[0] ? rc->name
                                                          : pending_castle_id);
                            if (cr) {
                                tgt.garrison = cr->garrison;
                                tgt.garrison_slots = GAME_ARMY_SLOTS;
                            }
                            CombatResult outcome =
                                RunCombat(&game, &sprites, &render_target, COMBAT_MODE_CASTLE, &tgt);
                            if (outcome == COMBAT_RESULT_WIN && cr) {
                                // Snapshot the villain's id and name BEFORE
                                // we clear the castle's villain_id field —
                                // we need them for the post-combat dialog
                                // regardless of whether the player held a
                                // contract on this villain.
                                char caught_vid[24];
                                size_t k = 0;
                                while (k + 1 < sizeof(caught_vid) &&
                                       cr->villain_id[k]) {
                                    caught_vid[k] = cr->villain_id[k]; k++;
                                }
                                caught_vid[k] = '\0';
                                const VillainDef *captured =
                                    caught_vid[0] ? villain_by_id(caught_vid)
                                                  : NULL;
                                const char *vname =
                                    (captured && captured->name[0])
                                        ? captured->name : caught_vid;

                                // Castle becomes player-owned and the
                                // garrison clears, regardless of contract
                                // match. Combat already credited spoils.
                                cr->owner_kind = CASTLE_OWNER_PLAYER;
                                cr->visited = true;
                                cr->villain_id[0] = '\0';
                                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                                    cr->garrison[s].id[0] = '\0';
                                    cr->garrison[s].count = 0;
                                }

                                // Contract semantics: only fulfill when
                                // the active contract
                                // matches the captured villain. Without a
                                // matching contract, no bounty is paid and
                                // the villain is "set free" — castle becomes
                                // player-owned (already done above), but the
                                // villain is not added to villains_caught[]
                                // and the cycle is not rotated.
                                bool contract_match = false;
                                if (caught_vid[0] &&
                                    game.contract.active_id[0] &&
                                    strcmp(caught_vid,
                                           game.contract.active_id) == 0) {
                                    contract_match = true;
                                }

                                int prev_rank = game.character.cls.rank_index;
                                int reward_gold = 0;
                                bool ranked_up = false;
                                if (contract_match) {
                                    if (captured) reward_gold = captured->reward;
                                    GameFulfillContract(&game, caught_vid);
                                    GameMaybeRankUp(&game);
                                    ranked_up = (game.character.cls.rank_index
                                                 != prev_rank);
                                }

                                // Surface the capture (and bounty / rank-up,
                                // when applicable) to the player. The combat
                                // layer's own "Victory!" dialog already
                                // dismissed before RunCombat returned, so
                                // open_dialog here queues a fresh bottom-
                                // screen dialog the main loop will pump on
                                // the next frame.
                                if (caught_vid[0]) {
                                    char body[640];
                                    if (contract_match) {
                                        int n = snprintf(body, sizeof body,
                                            "...and the capture of %s.\n\n"
                                            "For fulfilling your contract\n"
                                            "you receive an additional\n"
                                            "%d gold as bounty...\n"
                                            "and a piece of the map to\n"
                                            "the stolen scepter.",
                                            vname, reward_gold);
                                        if (ranked_up && n > 0 &&
                                            n < (int)sizeof body) {
                                            snprintf(body + n,
                                                     sizeof body - (size_t)n,
                                                     "\n\nYou are promoted "
                                                     "to %s!",
                                                     game.character.cls
                                                         .rank_title);
                                        }
                                    } else {
                                        snprintf(body, sizeof body,
                                            "...and the capture of %s.\n\n"
                                            "Since you did not have the\n"
                                            "proper contract, the Lord\n"
                                            "has been set free.",
                                            vname);
                                    }
                                    open_dialog("Capture", body);
                                }
                            } else if (outcome == COMBAT_RESULT_LOSS) {
                                perform_temp_death(&game, &map, &fog, &res);
                            }
                        }
                        pending_castle_id[0] = '\0';
                        break;
                    }
                    case FLOW_ATTACK_FOE: {
                        // : yes → combat;
                        // win clears the foe tile and removes the placement.
                        // Loss → temp_death.
                        if (r == PROMPT_RESULT_YES && pending_foe_id[0]) {
                            FoeState *f = GameFindFoe(&game, pending_foe_id);
                            CombatTarget tgt = { 0 };
                            tgt.name = "Hostile band";
                            if (f) {
                                tgt.garrison = f->garrison;
                                tgt.garrison_slots = GAME_ARMY_SLOTS;
                            }
                            CombatResult outcome =
                                RunCombat(&game, &sprites, &render_target, COMBAT_MODE_FOE, &tgt);
                            if (outcome == COMBAT_RESULT_WIN) {
                                MapClearInteractive(&map,
                                                    pending_foe_x,
                                                    pending_foe_y);
                                if (f) f->alive = false;
                            } else if (outcome == COMBAT_RESULT_LOSS) {
                                perform_temp_death(&game, &map, &fog, &res);
                            }
                        }
                        pending_foe_id[0] = '\0';
                        pending_foe_x = pending_foe_y = -1;
                        break;
                    }
                    case FLOW_CHEST_CHOICE: {
                        // gold_or_leadership (
                        // ): A=gold, B=distribute to peasants
                        // for a leadership bump. prompt_ab_open returns
                        // PROMPT_RESULT_1 for A and PROMPT_RESULT_2 for B.
                        if (r == PROMPT_RESULT_1) {
                            GameAcceptChestGold(&game, pending_chest_gold);
                        } else if (r == PROMPT_RESULT_2) {
                            GameAcceptChestLeadership(&game,
                                                      pending_chest_leadership);
                        }
                        pending_chest_gold = 0;
                        pending_chest_leadership = 0;
                        break;
                    }
                    case FLOW_ALCOVE: {
                        // : on yes,
                        // deduct alcove_cost, set knows_magic, consume tile.
                        if (r == PROMPT_RESULT_YES) {
                            const ResBanners *bn = &res.banners;
                            char msg[RES_BANNER_LEN];
                            if (game.stats.gold < res.economy.alcove_cost) {
                                char cbuf[16];
                                snprintf(cbuf, sizeof cbuf, "%d",
                                         res.economy.alcove_cost);
                                ResTemplateVar vars[] = { { "COST", cbuf } };
                                resources_format_template(msg, sizeof msg,
                                                          bn->alcove_no_gold,
                                                          vars, 1);
                                open_dialog(res.ui.dt_alcove_result, msg);
                            } else {
                                game.stats.gold -= res.economy.alcove_cost;
                                game.stats.knows_magic = true;
                                // Remove the alcove tile.
                                MapClearInteractive(&map,
                                                    game.position.x,
                                                    game.position.y);
                                GameAddConsumed(&game, game.position.zone,
                                                game.position.x,
                                                game.position.y);
                                resources_format_template(msg, sizeof msg,
                                                          bn->alcove_taught,
                                                          NULL, 0);
                                open_dialog(res.ui.dt_alcove_result, msg);
                            }
                        }
                        // Pop VIEW_ALCOVE — the hillcave backdrop is
                        // gone; player is back in the overworld with
                        // an open dialog (taught / no-gold / no
                        // dialog at all if cancelled).
                        if (views_active() == VIEW_ALCOVE) {
                            views_dismiss();
                        }
                        break;
                    }
                    case FLOW_RECRUIT:
                        if (r == PROMPT_RESULT_YES && pending_dwelling_troop[0]) {
                            int n = prompt_text_input_value();
                            //  and
                            // visit_dwelling silently clamp to `max`
                            // before invoking buy_troop.  lists
                            // only error codes 1 (no gold) and 2 (no
                            // slot); rc==3 ("army cannot handle") is a
                            // openbounty-only path that we suppress
                            // here by capping `n` first.
                            int max = GameMaxRecruitable(&game,
                                pending_dwelling_troop);
                            if (max < 0) max = 0;
                            if (n > max) n = max;
                            if (n > 0) {
                                int rc = GameBuyTroop(&game,
                                                      pending_dwelling_troop, n);
                                if (rc == 1) {
                                    // : "no gold" banner is
                                    // padded with 3 leading newlines.
                                    // MSG_FLAG_PADDED owns that layout
                                    // decision in one place.
                                    open_dialog_flags(NULL,
                                        game.res->banners.town_no_gold,
                                        MSG_FLAG_PADDED);
                                } else if (rc == 2) {
                                    // : "No troop slots
                                    // left!" — banner verbatim, no
                                    // leading-blank decoration.
                                    open_dialog(NULL,
                                        game.res->banners.no_troop_slots);
                                } else {
                                    // Success: reduce dwelling population.
                                    for (int i = 0; i < game.dwelling_count; i++) {
                                        DwellingState *d = &game.dwellings[i];
                                        if (d->x == pending_dwelling_x &&
                                            d->y == pending_dwelling_y &&
                                            strcmp(d->zone,
                                                   pending_dwelling_zone) == 0) {
                                            d->count -= n;
                                            if (d->count < 0) d->count = 0;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        pending_dwelling_troop[0] = '\0';
                        pending_dwelling_zone[0]  = '\0';
                        pending_dwelling_x = pending_dwelling_y = -1;
                        // If we entered via VIEW_DWELLING (the location-
                        // backdrop screen), pop it back to the overworld
                        // now that the prompt resolved. Home-castle
                        // recruit lives under VIEW_RECRUIT_SOLDIERS so
                        // the dismiss here only fires for actual
                        // outdoor-dwelling visits.
                        if (views_active() == VIEW_DWELLING) {
                            views_dismiss();
                        }
                        break;
                    case FLOW_ACCEPT_FRIENDLY:
                        if (r == PROMPT_RESULT_YES &&
                            pending_dwelling_troop[0] &&
                            pending_friendly_count > 0) {
                            GameAddTroop(&game,
                                         pending_dwelling_troop,
                                         pending_friendly_count);
                        }
                        // Always consume the foe (yes or no).
                        if (pending_dwelling_zone[0]) {
                            MapClearInteractive(&map,
                                                pending_dwelling_x,
                                                pending_dwelling_y);
                            GameAddConsumed(&game, pending_dwelling_zone,
                                            pending_dwelling_x,
                                            pending_dwelling_y);
                        }
                        if (pending_friendly_foe_id[0]) {
                            FoeState *f = GameFindFoe(&game,
                                                      pending_friendly_foe_id);
                            if (f) f->alive = false;
                        }
                        pending_dwelling_troop[0] = '\0';
                        pending_dwelling_zone[0]  = '\0';
                        pending_friendly_foe_id[0] = '\0';
                        pending_friendly_count = 0;
                        pending_dwelling_x = pending_dwelling_y = -1;
                        break;
                    case FLOW_NAVIGATE:
                        if (r >= PROMPT_RESULT_1 && r <= PROMPT_RESULT_5) {
                            int idx = r - PROMPT_RESULT_1;
                            if (idx >= 0 && idx < pending_nav_count) {
                                const char *target = pending_nav_zones[idx];
                                if (!GameSwitchZone(&game, &map, &fog, target)) {
                                    open_dialog(NULL,
                                        "Cannot reach that continent.");
                                } else {
                                    // spend a full week on arrival.
                                    int paid = 0;
                                    GameSpendWeek(&game, &paid);
                                    if (paid > 0) schedule_week_end(&game, paid);
                                }
                            }
                        }
                        pending_nav_count = 0;
                        break;
                    case FLOW_NONE: default: break;
                }
                pending_flow = FLOW_NONE;
                prompt_chained: ;   // chained-prompt target: skip reset
            }
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
                if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_KP_8)) {
                    cur = (cur - 1 + count) % count;
                    views_controls_set_cursor(cur);
                } else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_KP_2)) {
                    cur = (cur + 1) % count;
                    views_controls_set_cursor(cur);
                } else if (IsKeyPressed(KEY_ENTER) ||
                           IsKeyPressed(KEY_KP_ENTER) ||
                           IsKeyPressed(KEY_SPACE)) {
                    // Advance the value of the selected setting.
                    views_controls_advance(&game, vis_map[cur]);
                } else if (IsKeyPressed(KEY_ESCAPE) ||
                           IsKeyPressed(KEY_C) ||
                           gamepad_pressed_cancel()) {
                    views_dismiss();
                } else {
                    // Digit 1..count selects and advances that row.
                    for (int k = 0; k < count && k < 9; k++) {
                        if (IsKeyPressed(KEY_ONE + k)) {
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
            if (IsKeyPressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                views_dismiss();
                pending_castle_id[0] = '\0';
            } else if (IsKeyPressed(KEY_A)) {
                // A) Recruit Soldiers — push the dedicated recruit
                // sub-screen (5 troops + gold + key hint).
                screen_recruit_soldiers_open(&game);
            } else if (IsKeyPressed(KEY_B)) {
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
            if (IsKeyPressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                views_dismiss();
                pending_castle_id[0] = '\0';
            } else if (IsKeyPressed(KEY_SPACE)) {
                screen_own_castle_toggle_mode();
            } else {
                for (int k = 0; k < 5; k++) {
                    if (!IsKeyPressed(KEY_A + k)) continue;
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
            if (views_active() == VIEW_WORLDMAP && IsKeyPressed(KEY_SPACE)) {
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
                } else if (IsKeyPressed(KEY_ESCAPE) || gamepad_pressed_cancel()) {
                    bridge_state = BRIDGE_STATE_NONE;
                    dialog_dismiss();
                }
            } else if (gate_state == GATE_STATE_SELECT) {
                // Handle gate destination selection (A-Z or ESC).
                // matches the typed letter to the destination's first
                // letter (not its catalog index).
                int key = GetKeyPressed();
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
                        for (int ci = 0; ci < GAME_CASTLES; ci++) {
                            if (!game.castles[ci].visited) continue;
                            if (ci >= game.res->castle_count) continue;
                            const ResCastle *rc = &game.res->castles[ci];
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
                bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
                if (ctrl && IsKeyPressed(KEY_Q)) {
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
            switch (in.action) {
                case INPUT_ACTION_VIEW_ARMY:       views_set(VIEW_ARMY);      break;
                case INPUT_ACTION_VIEW_CHARACTER:  views_set(VIEW_CHARACTER); break;
                case INPUT_ACTION_VIEW_CONTRACT:   views_set(VIEW_CONTRACT);  break;
                case INPUT_ACTION_VIEW_PUZZLE:     views_set(VIEW_PUZZLE);    break;
                case INPUT_ACTION_VIEW_MAP:        views_set(VIEW_WORLDMAP);  break;
                case INPUT_ACTION_CAST_SPELL:
                    // no_spell_banner pops if the
                    // player doesn't know magic yet. Substitute the
                    // alcove location from data so the banner stays in
                    // sync with whatever zone hosts it.
                    if (!game.stats.knows_magic) {
                        char body[RES_BANNER_LEN];
                        const ResZone *az = NULL;
                        for (int zi = 0; zi < res.zone_count; zi++) {
                            if (res.zones[zi].magic_alcove_x >= 0 &&
                                res.zones[zi].magic_alcove_y >= 0) {
                                az = &res.zones[zi];
                                break;
                            }
                        }
                        char xs[16], ys[16];
                        snprintf(xs, sizeof xs, "%d", az ? az->magic_alcove_x : 0);
                        snprintf(ys, sizeof ys, "%d", az ? az->magic_alcove_y : 0);
                        ResTemplateVar vars[3] = {
                            { "ZONE", az ? (az->name[0] ? az->name : az->id) : "" },
                            { "X",    xs },
                            { "Y",    ys },
                        };
                        resources_format_template(body, sizeof body,
                                                  res.banners.no_spell_banner,
                                                  vars, 3);
                        open_dialog(NULL, body);
                    } else {
                        views_set(VIEW_SPELLS);
                        views_spells_set_mode(true);
                    }
                    break;
                case INPUT_ACTION_OPTIONS_MENU:    views_set(VIEW_OPTIONS);   break;
                case INPUT_ACTION_SAVE_QUIT: {
                    // : Q saves
                    // unconditionally, then displays a "Press Ctrl-Q to
                    // Quit / any other key to continue" dialog. The
                    // dialog handler at the bottom of the main loop
                    // already watches Ctrl-Q to exit.
                    char path[1024];
                    const char *pid = res.pack_id[0] ? res.pack_id : NULL;
                    if (SavePathGetSlot(pid, 0, path, sizeof(path))) {
                        SaveGameWrite(path, &game, &map, &fog);
                    }
                    {
                        char body[RES_BANNER_LEN];
                        resources_format_template(body, sizeof body,
                                                  res.banners.body_save_confirm,
                                                  NULL, 0);
                        // : a single
                        // body block with no title bar. The full message
                        // lives in body_save_confirm; pass NULL for title.
                        open_dialog(NULL, body);
                    }
                    break;
                }
                case INPUT_ACTION_FAST_QUIT:
                    // Status-bar prompt, NOT a bottom dialog. chrome.c
                    // queries fast_quit_is_active() to substitute the
                    // prompt string for the "Days Left:N" status.
                    fast_quit_open();
                    break;

                case INPUT_ACTION_END_WEEK: {
                    // spend_week: advance to next week boundary.
                    // After the advance, schedule_week_end() queues the
                    // astrology + budget dialogs (end_of_week, game.c:3673).
                    int paid = 0;
                    GameSpendWeek(&game, &paid);
                    if (paid > 0) {
                        schedule_week_end(&game, paid);
                    }
                    if (game.stats.game_over) {
                        show_lose_game(&game, &res);
                    }
                    break;
                }
                case INPUT_ACTION_SEARCH: {
                    // : "It will take 10
                    // days to do a search of this area. Search (y/n)?"
                    pending_flow = FLOW_SEARCH;
                    {
                        char body[RES_BANNER_LEN], dbuf[12];
                        snprintf(dbuf, sizeof dbuf, "%d",
                                 res.tuning.search_cost_days);
                        ResTemplateVar v[] = { { "DAYS", dbuf } };
                        resources_format_template(body, sizeof body,
                                                  res.banners.body_search,
                                                  v, 1);
                        prompt_yes_no_open(res.ui.dt_search, body);
                    }
                    break;
                }
                case INPUT_ACTION_DISMISS_ARMY: {
                    // pick a slot (1..5) to dismiss one troop stack.
                    int n_slots = 0;
                    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                        if (game.army[i].id[0] && game.army[i].count > 0) n_slots++;
                    }
                    if (n_slots <= 0) break;
                    pending_flow = FLOW_DISMISS_ARMY;
                    {
                        char body[RES_BANNER_LEN];
                        resources_format_template(body, sizeof body,
                                                  res.banners.body_dismiss_pick,
                                                  NULL, 0);
                        prompt_numeric_open(res.ui.dt_dismiss_army, body,
                                            GAME_ARMY_SLOTS);
                    }
                    break;
                }
                case INPUT_ACTION_FLY:
                    // allowed only from RIDE; every
                    // army stack must contain a flying troop with skill_
                    // level >= 2 (player_can_fly). Silent on failure.
                    if (game.character.mount == MOUNT_RIDE &&
                        GamePlayerCanFly(&game)) {
                        game.character.mount = MOUNT_FLY;
                    }
                    break;
                case INPUT_ACTION_LAND: {
                    // from FLY, land only on plain
                    // grass (map byte 0x00 — terrain=GRASS, no interactive).
                    if (game.character.mount != MOUNT_FLY) break;
                    const Tile *cur = MapGetTile(&map,
                                                 game.position.x,
                                                 game.position.y);
                    if (cur &&
                        cur->terrain == TERRAIN_GRASS &&
                        cur->interactive == INTERACT_NONE &&
                        !cur->blocks_foot) {
                        game.character.mount = MOUNT_RIDE;
                    }
                    break;
                }
                case INPUT_ACTION_NEW_CONTINENT: {
                    // : on water,
                    // show "Go to which continent?" with the discovered
                    // continents numbered 1..N; player picks a digit and
                    // spend_week runs afterward.
                    const ResBanners *bn = &res.banners;
                    if (game.travel_mode != TRAVEL_BOAT) {
                        open_dialog(NULL, bn->body_must_be_sailing);
                        break;
                    }
                    const ResZone *cur =
                        resources_zone_by_id(&res, game.position.zone);
                    if (!cur || cur->neighbor_count == 0) {
                        open_dialog(NULL, bn->body_no_continents);
                        break;
                    }
                    // Build numbered body listing each neighbor. Cap at 5
                    // (matches our prompt_numeric_open max).
                    pending_nav_count = 0;
                    char body[256];
                    int bo = 0;
                    for (int ni = 0; ni < cur->neighbor_count &&
                                     pending_nav_count < 5; ni++) {
                        const ResZone *nz =
                            resources_zone_by_id(&res, cur->neighbors[ni]);
                        if (!nz) continue;
                        size_t k = 0;
                        while (k + 1 < sizeof(pending_nav_zones[0]) &&
                               nz->id[k]) {
                            pending_nav_zones[pending_nav_count][k] = nz->id[k];
                            k++;
                        }
                        pending_nav_zones[pending_nav_count][k] = '\0';
                        char ibuf[12];
                        snprintf(ibuf, sizeof ibuf, "%d", pending_nav_count + 1);
                        ResTemplateVar v[] = {
                            { "INDEX", ibuf },
                            { "ZONE",  nz->name[0] ? nz->name : nz->id },
                        };
                        char frag[96];
                        resources_format_template(frag, sizeof frag,
                                                  bn->body_navigate_row, v, 2);
                        int n = snprintf(body + bo, sizeof(body) - bo,
                                         "%s", frag);
                        if (n < 0 || n >= (int)(sizeof(body) - bo)) break;
                        bo += n;
                        pending_nav_count++;
                    }
                    if (pending_nav_count == 0) {
                        open_dialog(NULL, bn->body_no_continents);
                        break;
                    }
                    pending_flow = FLOW_NAVIGATE;
                    prompt_numeric_open(res.ui.dt_navigate,
                                        body, pending_nav_count);
                    break;
                }
                case INPUT_ACTION_VIEW_CONTROLS:
                    // controls_menu.
                    views_set(VIEW_CONTROLS);
                    break;
                case INPUT_ACTION_REST: {
                    // numpad 5 rests one day (one step worth) in
                    // place. Drives day/week tick the same way a real step
                    // does.
                    bool day_end = false, week_end = false;
                    int paid = 0;
                    GameOnStep(&game, false, &day_end, &week_end, &paid);
                    (void)day_end;
                    if (week_end) schedule_week_end(&game, paid);
                    if (game.stats.game_over) show_lose_game(&game, &res);
                    break;
                }
                case INPUT_ACTION_NONE:
                default: break;
            }
            if (in.action == INPUT_ACTION_NONE && (in.dx || in.dy)) {
                if (step_try(&game, &map, &fog, &res, in.dx, in.dy)) {
                    last_step_time = GetTime();
                }
            }
        }

        // Animate hero sprite — only advance frames during
        // or just after a step. options[3] = 1 means Animation On
        // ( "Animation = On" with value 1; classic
        // controls_menu displays "On" when val==1).
        if (GetTime() >= hero_anim_next) {
            bool anim_enabled = (game.stats.options[3] != 0);
            bool animating = anim_enabled && (GetTime() - last_step_time < 0.4);
            if (animating) {
                game.anim_frame = (game.anim_frame + 1) % 4;
            } else {
                game.anim_frame = 0;   // idle pose
            }
            // Delay option: 0.05 + options[0] * 0.05 (range 0-5 = 0.05-0.30)
            double interval = 0.05 + game.stats.options[0] * 0.05;
            hero_anim_next = GetTime() + interval;
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

    // Encode session to movie.webm if --encode-movie was given. Runs
    // AFTER the main loop exits, BEFORE recorder_shutdown so the disk
    // record dir is still untouched. The dialog drives its own draw
    // loop on render_target.
    if (encode_movie) {
        if (record_dir) {
            encode_dialog_session(&render_target, record_dir);
        } else {
            fprintf(stderr,
                    "[encode-movie] requires --record <dir>; skipping\n");
        }
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
