// Opening a .openbounty archive (GB-111), and autosave (GB-103).
//
// An archive is never edited in place: it is extracted to a working directory
// beside the user's data, and that directory is the workspace. Saving writes
// there; the original archive is only replaced when the user packages.

#include "gb.h"

#include "pack.h"
#include "savepath.h"

#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define GB_SEP '\\'
#  define gb_mkdir(p) _mkdir(p)
#else
#  include <unistd.h>
#  define GB_SEP '/'
#  define gb_mkdir(p) mkdir((p), 0777)
#endif

// Create every directory along a path. Archive entries arrive in arbitrary
// order, so a file's parent may not exist yet.
static void mkdirs(const char *path) {
    char tmp[GB_PATH_MAX * 2];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
            gb_mkdir(tmp);
            *p = c;
        }
    }
    gb_mkdir(tmp);
}

static void basename_noext(const char *path, char *out, size_t n) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (bs > slash) slash = bs;
#endif
    const char *base = slash ? slash + 1 : path;
    snprintf(out, n, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot && strcmp(dot, ".openbounty") == 0) *dot = '\0';
}

bool gb_archive_extract(const char *archive, char *out_dir, size_t out_sz,
                        char *err, size_t errsz) {
    char user[GB_PATH_MAX];
    if (!SavePathGetDir(user, sizeof user)) {
        snprintf(err, errsz, "Could not find your user data folder.");
        return false;
    }
    char stem[128];
    basename_noext(archive, stem, sizeof stem);

    char work[GB_PATH_MAX * 2];
    snprintf(work, sizeof work, "%s%cgamebuilder", user, GB_SEP);
    mkdirs(work);
    snprintf(work, sizeof work, "%s%cgamebuilder%c%s", user, GB_SEP, GB_SEP, stem);

    struct stat st;
    if (stat(work, &st) == 0) {
        // A previous extraction of the same archive is still here. Reusing it
        // silently could hand back stale edits, so it is removed rather than
        // merged -- merging two versions of a pack is worse than either.
        pack_rmtree(work);
    }
    mkdirs(work);

    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, archive, 0)) {
        snprintf(err, errsz,
                 "Could not read:\n%s\n\nIt may not be a valid .openbounty "
                 "archive.", archive);
        return false;
    }
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; i++) {
        mz_zip_archive_file_stat fs;
        if (!mz_zip_reader_file_stat(&zip, i, &fs)) { ok = false; break; }
        char dst[GB_PATH_MAX * 3];
        snprintf(dst, sizeof dst, "%s%c%s", work, GB_SEP, fs.m_filename);
        for (char *p = dst; *p; p++) if (*p == '/') *p = GB_SEP;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) { mkdirs(dst); continue; }

        char parent[GB_PATH_MAX * 3];
        snprintf(parent, sizeof parent, "%s", dst);
        char *slash = strrchr(parent, GB_SEP);
        if (slash) { *slash = '\0'; mkdirs(parent); }
        if (!mz_zip_reader_extract_to_file(&zip, i, dst, 0)) {
            snprintf(err, errsz, "Could not write:\n%s", dst);
            ok = false;
        }
    }
    mz_zip_reader_end(&zip);
    if (!ok) return false;

    char manifest[GB_PATH_MAX * 3];
    snprintf(manifest, sizeof manifest, "%s%cgame.json", work, GB_SEP);
    if (stat(manifest, &st) != 0) {
        snprintf(err, errsz,
                 "That archive has no game.json at its root, so it is not a "
                 "pack.");
        return false;
    }
    snprintf(out_dir, out_sz, "%s", work);
    return true;
}

// --- autosave (GB-103) --------------------------------------------------------
//
// Autosave never writes to the pack. It writes the manifest to a sibling file
// so a crash loses at most one interval of work, and recovery is an explicit
// choice rather than something that happens behind the user's back.

static void autosave_path(const GbWorkspace *ws, char *out, size_t n) {
    snprintf(out, n, "%s%c.gamebuilder-autosave.json", ws->root, GB_SEP);
}

bool gb_autosave_write(const GbWorkspace *ws) {
    if (!ws || !ws->open || !ws->doc || !ws->dirty) return false;
    char path[GB_PATH_MAX * 2];
    autosave_path(ws, path, sizeof path);
    char *text = cJSON_Print(ws->doc);
    if (!text) return false;
    FILE *f = fopen(path, "wb");
    if (!f) { cJSON_free(text); return false; }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    cJSON_free(text);
    return true;
}

bool gb_autosave_exists(const char *root) {
    char path[GB_PATH_MAX * 2];
    snprintf(path, sizeof path, "%s%c.gamebuilder-autosave.json", root, GB_SEP);
    struct stat st;
    return stat(path, &st) == 0;
}

bool gb_autosave_recover(GbWorkspace *ws, char *err, size_t errsz) {
    char path[GB_PATH_MAX * 2];
    autosave_path(ws, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "No recovery file found."); return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t rd = fread(buf, 1, (size_t)n, f);
    buf[rd] = '\0';
    fclose(f);
    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (!doc) {
        snprintf(err, errsz, "The recovery file is damaged and cannot be used.");
        return false;
    }
    if (ws->doc) cJSON_Delete(ws->doc);
    ws->doc = doc;
    ws->dirty = true;              // recovered work is unsaved by definition
    gb_workspace_reproject(ws);
    return true;
}

void gb_autosave_discard(const GbWorkspace *ws) {
    char path[GB_PATH_MAX * 2];
    autosave_path(ws, path, sizeof path);
    remove(path);
}
