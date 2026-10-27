// src/shell_pack.c -- which pack the game plays (shell_pack.h).

#include "shell_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fatal.h"
#include "pack_select.h"
#include "plat_android.h"
#include "plat_ios.h"
#include "savepath.h"
#include "shell_earlyexit.h"
#include "boot_trace.h"

// No pack anywhere and no KB.EXE to build one from. Tell the user (a) why
// nothing happened, (b) where the engine looks for packs, (c) how to make one
// from their own KB.EXE. The body has to stand alone -- on Windows GUI builds
// this dialog is the only thing the user ever sees.
static void report_no_pack(void) {
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
        "The release archive carries the King's Bounty pack in its assets folder, next to openbounty.exe; it may have been moved or deleted.\n\n"
        "How to fix this:\n\n"
        "1. Put the assets folder back, or place a *.openbounty pack file in this folder:\n"
        "     %s\n"
        "     or in the folder you start openbounty.exe from.\n\n"
        "2. Or, place your KB.EXE and its game files in the folder you start openbounty.exe from and re-run; the engine extracts a pack on first launch.\n\n"
        "3. Or, from a command prompt in the folder that holds KB.EXE:\n"
        "     openbounty.exe --extract\n\n"
        "See README.txt for the full instructions.",
        user_dir[0] ? user_dir : "(your AppData\\OpenBounty folder)");
#elif defined(__APPLE__)
    snprintf(body, sizeof body,
        "OpenBounty cannot start because no game pack was found.\n\n"
        "The release archive carries the King's Bounty pack in its "
        "assets folder, next to openbounty; it may have been moved or "
        "deleted.\n\n"
        "How to fix this:\n\n"
        "1. Put the assets folder back, or place a *.openbounty pack file in:\n"
        "     %s\n"
        "   or in the folder you run openbounty from.\n\n"
        "2. Or, from a Terminal in the folder that holds KB.EXE, run:\n"
        "     ./openbounty --extract\n"
        "   to generate a pack from your own copy of the game.\n\n"
        "See README.txt for the full instructions.",
        user_dir[0] ? user_dir : "~/Library/Application Support/OpenBounty");
#else
    snprintf(body, sizeof body,
        "OpenBounty cannot start because no game pack was found.\n\n"
        "The release archive carries the King's Bounty pack in its "
        "assets folder, next to openbounty; it may have been moved or "
        "deleted.\n\n"
        "How to fix this:\n\n"
        "1. Put the assets folder back, or place a *.openbounty pack file in:\n"
        "     %s\n"
        "   or in the directory you run openbounty from.\n\n"
        "2. Or, in the directory that holds KB.EXE, run:\n"
        "     ./openbounty --extract\n"
        "   to generate a pack from your own copy of the game.\n\n"
        "See README.txt for the full instructions.",
        user_dir[0] ? user_dir : "$XDG_DATA_HOME/openbounty (default ~/.local/share/openbounty)");
#endif
    fatal_user_error("OpenBounty: no game pack found", body);
}

int shell_open_game_pack(const char *pack_arg, Pack **out, char *pack_path, size_t cap) {
    // Android ships exactly one pack, inside the APK: no discovery, no picker,
    // no CLI. Opened here so the resolve-and-open block below is skipped whole.
    Pack *pack = plat_android_open_pack();
    if (pack) snprintf(pack_path, cap, "%s", ANDROID_PACK_ASSET);
    if (!pack) {
        BOOT_TRACE("[boot] opening the bundled pack\n");
        pack = plat_ios_open_pack();
        BOOT_TRACE("[boot] pack %s\n", pack ? "opened" : "FAILED");
        if (pack) snprintf(pack_path, cap, "%s", IOS_PACK_RESOURCE);
    }

    // Resolve --pack <name|path>, or auto-discover. Discovery walks (in
    // order): cwd zips, <user-data>/openbounty zips, <exe>/assets zips,
    // <exe>/assets/<sub>/game.json loose trees. If nothing is found we
    // try a first-run KB.EXE extraction in cwd; failing that, error out
    // with a platform-specific dialog explaining the install steps.
    if (!pack) {
    if (pack_arg && pack_arg[0]) {
        if (!pack_resolve_arg(pack_arg, pack_path, cap)) {
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
        PackEntry *entries = NULL;
        int n = pack_discover(&entries);
        if (n == 0) {
            // Final fallback: a fresh first-run KB.EXE extract from cwd.
            // Output goes to <user-data>/<id>.openbounty so
            // future launches will find it via discovery step 2.
            const char *in_dir = shell_extract_input_dir();
            if (!in_dir) {
                report_no_pack();
                return 1;
            }
            fprintf(stdout, "[extract] no pack found; running first-run extraction from %s\n", in_dir);
            if (!shell_extract_to_user_dir(in_dir, pack_path, cap)) return 1;
        } else if (n == 1) {
            snprintf(pack_path, cap, "%s", entries[0].path);
        } else {
            int chosen = 0;
            if (!pack_select_flow(entries, n, &chosen)) {
                // User pressed ESC.
                free(entries);
                return 0;
            }
            snprintf(pack_path, cap, "%s", entries[chosen].path);
        }
        free(entries);
    }

    pack = pack_open(pack_path);
    }   // !pack (non-Android)
    if (!pack) {
        char body[1024];
        snprintf(body, sizeof body,
            "Failed to open the game pack at:\n\n    %s\n\n"
            "The file may be corrupt, the wrong format, or unreadable. "
            "Try replacing it with a fresh extraction, run in the "
            "folder that holds KB.EXE:\n\n"
            "    openbounty --extract",
            pack_path);
        fatal_user_error("OpenBounty: cannot open game pack", body);
        return 1;
    }
    *out = pack;
    return -1;
}
