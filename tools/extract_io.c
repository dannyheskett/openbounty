// File-IO helpers for extract.c. Split into a TU so test/utility tools
// can link against just these without pulling in the main pipeline.

#define _POSIX_C_SOURCE 200809L
#include "extract.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define EX_MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define EX_MKDIR(p) mkdir((p), 0755)
#endif

uint8_t *ex_read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "extract: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    if (out_size) *out_size = (size_t)n;
    return buf;
}

int ex_mkdir_p(const char *path) {
    if (!path || !*path) return -1;
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    size_t n = strlen(buf);
    for (size_t i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char saved = buf[i];
            buf[i] = '\0';
            if (EX_MKDIR(buf) != 0 && errno != EEXIST) {
                fprintf(stderr, "extract: mkdir %s: %s\n", buf, strerror(errno));
                return -1;
            }
            buf[i] = saved;
        }
    }
    return 0;
}

int ex_write_file(const char *path, const uint8_t *data, size_t len) {
    const char *slash = strrchr(path, '/');
    if (slash) {
        char dir[1024];
        size_t dn = (size_t)(slash - path);
        if (dn >= sizeof dir) return -1;
        memcpy(dir, path, dn);
        dir[dn] = '\0';
        if (ex_mkdir_p(dir) != 0) return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "extract: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

const CcEntry *ex_cc_find(const CcArchive *cc, const char *name) {
    if (!cc || !name) return NULL;
    for (int i = 0; i < cc->count; i++) {
        const char *a = cc->entries[i].name;
        const char *b = name;
        while (*a && *b) {
            char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
            char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*a && !*b) return &cc->entries[i];
    }
    return NULL;
}

void ex_cc_free(CcArchive *cc) {
    if (!cc) return;
    for (int i = 0; i < cc->count; i++) free(cc->entries[i].data);
    free(cc->entries);
    cc->entries = NULL;
    cc->count = 0;
}
