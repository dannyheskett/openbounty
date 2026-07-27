// Workspace: opening, saving and re-projecting a pack.
//
// The DOM is the truth (see gb.h). Two properties matter and are tested:
//
//   GB-121  unknown keys survive a round-trip, so a pack using engine features
//           newer than this build is not silently damaged.
//   GB-114  an unchanged workspace saves byte-identically, so a pack under
//           version control produces empty diffs when nothing changed.
//
// cJSON gives us both: it keeps object members in document order and reprints
// numbers from their source text, so parse -> print is stable.

#include "gb.h"

#include "pack.h"
#include "savepath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#  define GB_SEP '\\'
#else
#  define GB_SEP '/'
#endif

static bool is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *read_whole(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = (long)rd;
    return buf;
}

bool gb_workspace_open(GbWorkspace *ws, const char *path,
                       char *err, size_t errsz) {
    if (!ws || !path || !*path) {
        snprintf(err, errsz, "No path given.");
        return false;
    }
    if (!is_dir(path)) {
        // GB-111: an archive is extracted to a working directory; the archive
        // itself is never edited in place. Not yet implemented -- the message
        // has to tell the user what to do instead, not just refuse.
        snprintf(err, errsz,
                 "'%s' is not a directory.\n\n"
                 "Opening a .openbounty archive is not supported yet. "
                 "Extract it first, then open the folder.", path);
        return false;
    }

    char manifest[GB_PATH_MAX + 16];
    snprintf(manifest, sizeof manifest, "%s%cgame.json", path, GB_SEP);
    long len = 0;
    char *text = read_whole(manifest, &len);
    if (!text) {
        snprintf(err, errsz,
                 "No game.json in:\n%s\n\n"
                 "A pack folder must contain game.json. Use New Pack to "
                 "create one.", path);
        return false;
    }

    const char *parse_end = NULL;
    cJSON *doc = cJSON_ParseWithOpts(text, &parse_end, 1);
    if (!doc) {
        // Report where it broke -- "invalid JSON" alone is useless (GB-012).
        long off = parse_end ? (long)(parse_end - text) : -1;
        int line = 1, col = 1;
        for (long i = 0; i < off && i < len; i++) {
            if (text[i] == '\n') { line++; col = 1; } else { col++; }
        }
        if (off >= 0) {
            snprintf(err, errsz,
                     "game.json could not be parsed.\n\n"
                     "The problem is at line %d, column %d.", line, col);
        } else {
            snprintf(err, errsz, "game.json could not be parsed.");
        }
        free(text);
        return false;
    }
    free(text);

    gb_workspace_close(ws);
    memset(ws, 0, sizeof *ws);
    ws->doc = doc;
    snprintf(ws->root, sizeof ws->root, "%s", path);
    ws->open = true;
    ws->dirty = false;
    gb_workspace_reproject(ws);
    return true;
}

bool gb_workspace_save(GbWorkspace *ws, char *err, size_t errsz) {
    if (!ws || !ws->open || !ws->doc) {
        snprintf(err, errsz, "Nothing is open.");
        return false;
    }
    char *out = cJSON_Print(ws->doc);
    if (!out) {
        snprintf(err, errsz, "Ran out of memory writing game.json.");
        return false;
    }

    char manifest[GB_PATH_MAX + 16], tmp[GB_PATH_MAX + 32];
    snprintf(manifest, sizeof manifest, "%s%cgame.json", ws->root, GB_SEP);
    snprintf(tmp, sizeof tmp, "%s.tmp", manifest);

    // Write to a temporary and rename, so an interrupted save cannot leave a
    // half-written manifest where a working pack used to be (GB-012).
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        snprintf(err, errsz, "Cannot write to:\n%s\n\nIs the folder writable?",
                 tmp);
        cJSON_free(out);
        return false;
    }
    size_t n = strlen(out);
    bool ok = fwrite(out, 1, n, f) == n && fputc('\n', f) != EOF;
    ok = (fclose(f) == 0) && ok;
    cJSON_free(out);
    if (!ok) {
        remove(tmp);
        snprintf(err, errsz, "Failed while writing:\n%s\n\nDisk full?", tmp);
        return false;
    }
    remove(manifest);              // Windows rename() will not overwrite
    if (rename(tmp, manifest) != 0) {
        snprintf(err, errsz, "Could not replace:\n%s", manifest);
        return false;
    }
    ws->dirty = false;
    return true;
}

void gb_workspace_close(GbWorkspace *ws) {
    if (!ws) return;
    if (ws->doc) cJSON_Delete(ws->doc);
    ws->doc = NULL;
    ws->open = false;
    ws->res_valid = false;
}

bool gb_workspace_reproject(GbWorkspace *ws) {
    // Resources is derived for preview only. It is loaded through the pack
    // stack, which is what the game does, so previews match the game exactly.
    if (!ws || !ws->open) return false;
    Pack *p = pack_open(ws->root);
    if (!p) { ws->res_valid = false; return false; }
    pack_stack_push(p);
    ws->res_valid = resources_load(&ws->res, "game.json");
    return ws->res_valid;
}

// --- recent packs -------------------------------------------------------------

static void recent_path(char *out, size_t n) {
    char dir[GB_PATH_MAX];
    if (!SavePathGetDir(dir, sizeof dir)) { out[0] = '\0'; return; }
    snprintf(out, n, "%s%cgamebuilder-recent.txt", dir, GB_SEP);
}

void gb_recent_load(GbRecent *r) {
    memset(r, 0, sizeof *r);
    char path[GB_PATH_MAX + 32];
    recent_path(path, sizeof path);
    if (!path[0]) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[GB_PATH_MAX];
    while (r->count < GB_RECENT_MAX && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n || !is_dir(line)) continue;   // drop entries that moved away
        snprintf(r->path[r->count++], GB_PATH_MAX, "%s", line);
    }
    fclose(f);
}

void gb_recent_add(GbRecent *r, const char *path) {
    for (int i = 0; i < r->count; i++) {
        if (strcmp(r->path[i], path) == 0) {          // move to front
            for (int j = i; j > 0; j--)
                memcpy(r->path[j], r->path[j - 1], GB_PATH_MAX);
            snprintf(r->path[0], GB_PATH_MAX, "%s", path);
            return;
        }
    }
    if (r->count < GB_RECENT_MAX) r->count++;
    for (int j = r->count - 1; j > 0; j--)
        memcpy(r->path[j], r->path[j - 1], GB_PATH_MAX);
    snprintf(r->path[0], GB_PATH_MAX, "%s", path);
}

void gb_recent_save(const GbRecent *r) {
    char path[GB_PATH_MAX + 32];
    recent_path(path, sizeof path);
    if (!path[0]) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < r->count; i++) fprintf(f, "%s\n", r->path[i]);
    fclose(f);
}
