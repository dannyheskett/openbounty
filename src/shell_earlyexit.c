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

const char *shell_extract_input_dir(void) {
    struct stat st;
    if (stat("legacy/bin/KB.EXE", &st) == 0) return "legacy/bin";
    if (stat("KB.EXE", &st) == 0) return ".";
    return NULL;
}

bool shell_extract_to_user_dir(const char *in_dir, char *out_zip, size_t cap) {
    char user_dir[PACK_ENTRY_PATH_MAX];
    if (!SavePathGetDir(user_dir, sizeof user_dir)) {
        fprintf(stdout, "extract: cannot resolve user data dir\n");
        return false;
    }
    char tmp_dir[PACK_ENTRY_PATH_MAX + 32];
    snprintf(tmp_dir, sizeof tmp_dir, "%s/.tmp-extract", user_dir);
    pack_rmtree(tmp_dir);
    if (extract_run(in_dir, tmp_dir) != 0) {
        pack_rmtree(tmp_dir);
        return false;
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
    char zip[PACK_ENTRY_PATH_MAX + 96];
    snprintf(zip, sizeof zip, "%s/%s.openbounty", user_dir, pid);
    if (!pack_zip_dir(tmp_dir, zip)) {
        fprintf(stdout, "extract: failed to write %s\n", zip);
        pack_rmtree(tmp_dir);
        return false;
    }
    pack_rmtree(tmp_dir);
    fprintf(stdout, "extract: wrote %s\n", zip);
    if (out_zip && cap > 0) {
        size_t n = strlen(zip);
        if (n >= cap) n = cap - 1;
        memcpy(out_zip, zip, n);
        out_zip[n] = '\0';
    }
    return true;
}

int shell_run_extract_mode(const char *out_dir) {
    // Inputs come from legacy/bin/ when it holds KB.EXE, else from the
    // current directory. With --out-dir, emit a loose tree; otherwise zip
    // into <user-data>/<pack_id>.openbounty.
    const char *in_dir = shell_extract_input_dir();
    if (!in_dir) {
        fprintf(stdout,
                "extract: KB.EXE not found. Place your game files "
                "in legacy/bin/ or in the current directory.\n");
        return 2;
    }
    if (out_dir) {
        int rc = extract_run(in_dir, out_dir);
        return rc == 0 ? 0 : 1;
    }
    return shell_extract_to_user_dir(in_dir, NULL, 0) ? 0 : 1;
}
