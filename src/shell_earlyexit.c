// src/shell_earlyexit.c

#include "shell_earlyexit.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "extract.h"
#include "pack.h"
#include "savepath.h"

int shell_run_pack_dir_mode(const char *src, const char *dst) {
    if (!pack_zip_dir(src, dst)) {
        fprintf(stdout, "pack-dir: failed to write %s\n", dst);
        return 1;
    }
    return 0;
}

int shell_run_extract_mode(const char *out_dir) {
    // Inputs come from cwd's legacy/bin/ subdir; if that's missing,
    // look in cwd directly. With --out-dir, emit loose tree.
    // Otherwise zip into <user-data>/openbounty/<pack_id>.openbounty.
    const char *in_dir = "legacy/bin";
    struct stat sst;
    if (stat(in_dir, &sst) != 0) {
        if (stat("KB.EXE", &sst) == 0) {
            in_dir = ".";
        } else {
            fprintf(stdout,
                    "extract: KB.EXE not found. Place your game files "
                    "in legacy/bin/ or in the current directory.\n");
            return 2;
        }
    }
    if (out_dir) {
        int rc = extract_run(in_dir, out_dir);
        return rc == 0 ? 0 : 1;
    }
    char user_dir[PACK_ENTRY_PATH_MAX];
    if (!SavePathGetDir(user_dir, sizeof user_dir)) {
        fprintf(stdout, "extract: cannot resolve user data dir\n");
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
    // Read pack_id from the emitted game.json so the output filename
    // matches what discovery will surface.
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
    snprintf(out_zip, sizeof out_zip, "%s/%s.openbounty", user_dir, pid);
    if (!pack_zip_dir(tmp_dir, out_zip)) {
        fprintf(stdout, "extract: failed to write %s\n", out_zip);
        pack_rmtree(tmp_dir);
        return 1;
    }
    pack_rmtree(tmp_dir);
    fprintf(stdout, "extract: wrote %s\n", out_zip);
    return 0;
}
