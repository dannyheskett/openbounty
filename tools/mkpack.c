// mkpack: zip a directory tree into a single .openbounty file.
// Used by the Makefile to package each assets/<game>/ into
// build/<game>.openbounty for distribution. Not shipped.
//
//   ./mkpack <src_dir> <out_zip>
//
// Skips editor scratch files (.xcf, .psd, *Zone.Identifier).

#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#define PATHSEP '\\'
#else
#include <dirent.h>
#define PATHSEP '/'
#endif

static int g_added = 0;

static int has_suffix(const char *s, const char *suffix) {
    size_t ls = strlen(s), lp = strlen(suffix);
    if (lp > ls) return 0;
    return strcmp(s + ls - lp, suffix) == 0;
}

static int should_skip(const char *name) {
    if (has_suffix(name, ".xcf")) return 1;
    if (has_suffix(name, ".psd")) return 1;
    if (strstr(name, "Zone.Identifier")) return 1;
    return 0;
}

static int add_file(mz_zip_archive *zw, const char *full, const char *rel) {
    FILE *f = fopen(full, "rb");
    if (!f) {
        fprintf(stderr, "mkpack: cannot open %s\n", full);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return 0; }
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return 0;
    }
    fclose(f);

    if (!mz_zip_writer_add_mem(zw, rel, buf, (size_t)sz, MZ_DEFAULT_COMPRESSION)) {
        fprintf(stderr, "mkpack: zip add failed for %s\n", rel);
        free(buf);
        return 0;
    }
    free(buf);
    g_added++;
    return 1;
}

#ifdef _WIN32

static int walk(mz_zip_archive *zw, const char *root, const char *prefix) {
    char pat[4096];
    snprintf(pat, sizeof pat, "%s\\*", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;
    int ok = 1;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char full[4096], rel[4096];
        snprintf(full, sizeof full, "%s%c%s", root, PATHSEP, fd.cFileName);
        if (prefix[0]) snprintf(rel, sizeof rel, "%s/%s", prefix, fd.cFileName);
        else           snprintf(rel, sizeof rel, "%s",     fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = walk(zw, full, rel) && ok;
        } else if (!should_skip(fd.cFileName)) {
            ok = add_file(zw, full, rel) && ok;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

#else

static int is_dir(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int walk(mz_zip_archive *zw, const char *root, const char *prefix) {
    DIR *d = opendir(root);
    if (!d) return 1;
    int ok = 1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char full[4096], rel[4096];
        snprintf(full, sizeof full, "%s/%s", root, de->d_name);
        if (prefix[0]) snprintf(rel, sizeof rel, "%s/%s", prefix, de->d_name);
        else           snprintf(rel, sizeof rel, "%s",     de->d_name);
        if (is_dir(full)) {
            ok = walk(zw, full, rel) && ok;
        } else if (!should_skip(de->d_name)) {
            ok = add_file(zw, full, rel) && ok;
        }
    }
    closedir(d);
    return ok;
}

#endif

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <src_dir> <out_zip>\n", argv[0]);
        return 2;
    }
    const char *src = argv[1];
    const char *out = argv[2];

    // Overwrite cleanly. miniz_init_file refuses to truncate an
    // existing zip, so remove first.
    remove(out);

    mz_zip_archive zw = {0};
    if (!mz_zip_writer_init_file(&zw, out, 0)) {
        fprintf(stderr, "mkpack: cannot create %s\n", out);
        return 1;
    }
    if (!walk(&zw, src, "")) {
        mz_zip_writer_end(&zw);
        remove(out);
        return 1;
    }
    if (!mz_zip_writer_finalize_archive(&zw)) {
        fprintf(stderr, "mkpack: finalize failed\n");
        mz_zip_writer_end(&zw);
        remove(out);
        return 1;
    }
    mz_zip_writer_end(&zw);
    fprintf(stderr, "mkpack: wrote %s (%d files)\n", out, g_added);
    return 0;
}
