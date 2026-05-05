// Game pack reader. Loads a `.openbounty` ZIP or a loose directory tree
// fully into RAM at open time, then serves byte slices via a hashtable
// keyed by pack-relative path.

#include "pack.h"
#include "savepath.h"
#include "miniz.h"
#include "cJSON.h"

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
#include <unistd.h>
#define PATHSEP '/'
#endif

#define PACK_REL_MAX 256
#define PACK_HASH_SLOTS 2048   // power of two; ~400 files ship today

typedef struct PackEntryNode {
    char rel[PACK_REL_MAX];
    unsigned char *data;
    size_t size;
    struct PackEntryNode *next;
} PackEntryNode;

struct Pack {
    char path[PACK_ENTRY_PATH_MAX];
    char id[64];
    char name[64];
    char kind[16];
    char hash[17];   // FNV1a-64 hex of the zip file bytes; "" for dir packs
    PackEntryNode *slots[PACK_HASH_SLOTS];
    int n_entries;
};

// ---------------------------------------------------------------------------
// Hashtable
// ---------------------------------------------------------------------------

static unsigned int fnv1a(const char *s) {
    unsigned int h = 0x811c9dc5u;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 0x01000193u;
    }
    return h;
}

static void normalize_rel(char *s) {
    // ZIPs and dir-walks both produce forward-slash paths on Linux/macOS,
    // but Windows dir walks use backslashes. Normalize so lookups match
    // the literal pack-relative paths the engine passes in.
    for (; *s; s++) {
        if (*s == '\\') *s = '/';
    }
}

static bool pack_insert(Pack *p, const char *rel, unsigned char *data, size_t size) {
    if (strlen(rel) >= PACK_REL_MAX) {
        fprintf(stderr, "pack: entry path too long: %s\n", rel);
        free(data);
        return false;
    }
    PackEntryNode *node = (PackEntryNode *)calloc(1, sizeof *node);
    if (!node) { free(data); return false; }
    snprintf(node->rel, sizeof node->rel, "%s", rel);
    normalize_rel(node->rel);
    node->data = data;
    node->size = size;

    unsigned int slot = fnv1a(node->rel) & (PACK_HASH_SLOTS - 1);
    node->next = p->slots[slot];
    p->slots[slot] = node;
    p->n_entries++;
    return true;
}

static const PackEntryNode *pack_lookup(const Pack *p, const char *rel) {
    if (!p || !rel) return NULL;
    unsigned int slot = fnv1a(rel) & (PACK_HASH_SLOTS - 1);
    for (const PackEntryNode *n = p->slots[slot]; n; n = n->next) {
        if (strcmp(n->rel, rel) == 0) return n;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Identity from game.json
// ---------------------------------------------------------------------------

static void copy_field(char *dst, size_t cap, const cJSON *root, const char *key) {
    cJSON *v = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(v) && v->valuestring) {
        snprintf(dst, cap, "%s", v->valuestring);
    } else {
        dst[0] = '\0';
    }
}

static void parse_identity(Pack *p) {
    const PackEntryNode *gj = pack_lookup(p, "game.json");
    if (!gj) {
        fprintf(stderr, "pack: missing game.json in %s\n", p->path);
        return;
    }
    char *text = (char *)malloc(gj->size + 1);
    if (!text) return;
    memcpy(text, gj->data, gj->size);
    text[gj->size] = '\0';
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) return;
    copy_field(p->id,   sizeof p->id,   root, "pack_id");
    copy_field(p->name, sizeof p->name, root, "pack_name");
    copy_field(p->kind, sizeof p->kind, root, "pack_kind");
    cJSON_Delete(root);
}

// ---------------------------------------------------------------------------
// Loose directory loader
// ---------------------------------------------------------------------------

static bool ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lp = strlen(suffix);
    if (lp > ls) return false;
    return strcmp(s + ls - lp, suffix) == 0;
}

static bool is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static bool slurp_into(Pack *p, const char *full_path, const char *rel) {
    FILE *f = fopen(full_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return false;
    }
    fclose(f);
    return pack_insert(p, rel, buf, (size_t)sz);
}

// Build a child rel path. Returns false if it would overflow PACK_REL_MAX.
// Uses a generous scratch buffer + length check to dodge gcc's
// -Wformat-truncation when concatenating two unbounded inputs.
static bool build_rel(char *out, const char *prefix, const char *name) {
    char scratch[PACK_REL_MAX * 2];
    int n;
    if (prefix[0]) n = snprintf(scratch, sizeof scratch, "%s/%s", prefix, name);
    else           n = snprintf(scratch, sizeof scratch, "%s", name);
    if (n < 0 || n >= PACK_REL_MAX) return false;
    memcpy(out, scratch, (size_t)n + 1);
    return true;
}

#ifdef _WIN32

static bool walk_dir(Pack *p, const char *root, const char *prefix) {
    char pattern[PACK_ENTRY_PATH_MAX + 4];
    snprintf(pattern, sizeof pattern, "%s\\*", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return true; // empty dir is fine
    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char full[PACK_ENTRY_PATH_MAX];
        char rel[PACK_REL_MAX];
        if ((int)(strlen(root) + 1 + strlen(fd.cFileName) + 1) > (int)sizeof full) {
            fprintf(stderr, "pack: path too long under %s\n", root);
            ok = false; continue;
        }
        snprintf(full, sizeof full, "%s%c%s", root, PATHSEP, fd.cFileName);
        if (!build_rel(rel, prefix, fd.cFileName)) {
            fprintf(stderr, "pack: rel path too long: %s/%s\n", prefix, fd.cFileName);
            ok = false; continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = walk_dir(p, full, rel) && ok;
        } else {
            if (ends_with(fd.cFileName, ".xcf") ||
                ends_with(fd.cFileName, ".psd") ||
                strstr(fd.cFileName, "Zone.Identifier")) continue;
            ok = slurp_into(p, full, rel) && ok;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

#else

static bool walk_dir(Pack *p, const char *root, const char *prefix) {
    DIR *d = opendir(root);
    if (!d) return true;
    bool ok = true;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char full[PACK_ENTRY_PATH_MAX];
        char rel[PACK_REL_MAX];
        if ((int)(strlen(root) + 1 + strlen(de->d_name) + 1) > (int)sizeof full) {
            fprintf(stderr, "pack: path too long under %s\n", root);
            ok = false; continue;
        }
        snprintf(full, sizeof full, "%s/%s", root, de->d_name);
        if (!build_rel(rel, prefix, de->d_name)) {
            fprintf(stderr, "pack: rel path too long: %s/%s\n", prefix, de->d_name);
            ok = false; continue;
        }
        if (is_dir(full)) {
            ok = walk_dir(p, full, rel) && ok;
        } else {
            if (ends_with(de->d_name, ".xcf") ||
                ends_with(de->d_name, ".psd") ||
                strstr(de->d_name, "Zone.Identifier")) continue;
            ok = slurp_into(p, full, rel) && ok;
        }
    }
    closedir(d);
    return ok;
}

#endif

// ---------------------------------------------------------------------------
// ZIP loader
// ---------------------------------------------------------------------------

static bool load_zip(Pack *p, const char *path) {
    mz_zip_archive zip = {0};
    if (!mz_zip_reader_init_file(&zip, path, 0)) {
        fprintf(stderr, "pack: not a valid zip: %s\n", path);
        return false;
    }
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    bool ok = true;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) { ok = false; continue; }
        // Skip directory entries.
        if (st.m_is_directory) continue;
        size_t sz = 0;
        void *blob = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!blob) { ok = false; continue; }
        // miniz uses default malloc; we own the buffer now and will free
        // it via standard free() in pack_close. miniz's MZ_MALLOC is
        // plain malloc by default in this build.
        if (!pack_insert(p, st.m_filename, (unsigned char *)blob, sz)) {
            ok = false;
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

// ---------------------------------------------------------------------------
// Whole-file FNV1a-64 hash of the zip on disk. Streamed in 64KB chunks so
// large packs don't need to be slurped twice. Returns true on success;
// out_hex receives a 16-char lowercase hex string + NUL.
// ---------------------------------------------------------------------------

static bool hash_zip_file(const char *path, char *out_hex /*[17]*/) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned long long h = 0xcbf29ce484222325ull;
    const unsigned long long FNV64_PRIME = 0x100000001b3ull;
    unsigned char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= FNV64_PRIME;
        }
    }
    fclose(f);
    snprintf(out_hex, 17, "%016llx", h);
    return true;
}

// ---------------------------------------------------------------------------
// Public open/close
// ---------------------------------------------------------------------------

Pack *pack_open(const char *path) {
    if (!path || !path[0]) return NULL;
    Pack *p = (Pack *)calloc(1, sizeof *p);
    if (!p) return NULL;
    snprintf(p->path, sizeof p->path, "%s", path);

    bool ok;
    if (is_dir(path)) {
        ok = walk_dir(p, path, "");
        // Loose-directory packs are dev-only; leave hash empty.
        p->hash[0] = '\0';
    } else {
        ok = load_zip(p, path);
        if (ok) hash_zip_file(path, p->hash);
    }
    if (!ok || p->n_entries == 0) {
        fprintf(stderr, "pack: failed to load %s (entries=%d)\n",
                path, p->n_entries);
        pack_close(p);
        return NULL;
    }
    parse_identity(p);
    // Fall back to filename-derived id/name if game.json lacked them.
    if (!p->id[0] || !p->name[0]) {
        const char *base = strrchr(path, PATHSEP);
        base = base ? base + 1 : path;
        char stem[64];
        snprintf(stem, sizeof stem, "%s", base);
        char *dot = strrchr(stem, '.');
        if (dot && strcmp(dot, ".openbounty") == 0) *dot = '\0';
        if (!p->id[0])   snprintf(p->id,   sizeof p->id,   "%s", stem);
        if (!p->name[0]) snprintf(p->name, sizeof p->name, "%s", stem);
    }
    if (!p->kind[0]) snprintf(p->kind, sizeof p->kind, "base");
    return p;
}

void pack_close(Pack *p) {
    if (!p) return;
    for (int i = 0; i < PACK_HASH_SLOTS; i++) {
        PackEntryNode *n = p->slots[i];
        while (n) {
            PackEntryNode *next = n->next;
            free(n->data);
            free(n);
            n = next;
        }
    }
    free(p);
}

const unsigned char *pack_read(const Pack *p, const char *rel, size_t *out_size) {
    const PackEntryNode *n = pack_lookup(p, rel);
    if (!n) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = n->size;
    return n->data;
}

const char *pack_id(const Pack *p)   { return p ? p->id   : ""; }
const char *pack_name(const Pack *p) { return p ? p->name : ""; }
const char *pack_kind(const Pack *p) { return p ? p->kind : ""; }
const char *pack_path(const Pack *p) { return p ? p->path : ""; }
const char *pack_hash(const Pack *p) { return p ? p->hash : ""; }

// ---------------------------------------------------------------------------
// Global pack stack
// ---------------------------------------------------------------------------

#define PACK_STACK_MAX 8
static Pack *g_stack[PACK_STACK_MAX];
static int   g_stack_n = 0;

void pack_stack_push(Pack *p) {
    if (!p) return;
    if (g_stack_n >= PACK_STACK_MAX) {
        fprintf(stderr, "pack: stack full\n");
        return;
    }
    g_stack[g_stack_n++] = p;
}

void pack_stack_pop(void) {
    if (g_stack_n == 0) return;
    g_stack_n--;
    pack_close(g_stack[g_stack_n]);
    g_stack[g_stack_n] = NULL;
}

void pack_stack_clear(void) {
    while (g_stack_n > 0) pack_stack_pop();
}

const unsigned char *pack_stack_read(const char *rel, size_t *out_size) {
    for (int i = g_stack_n - 1; i >= 0; i--) {
        size_t sz = 0;
        const unsigned char *b = pack_read(g_stack[i], rel, &sz);
        if (b) {
            if (out_size) *out_size = sz;
            return b;
        }
    }
    if (out_size) *out_size = 0;
    return NULL;
}

const Pack *pack_stack_top(void) {
    return g_stack_n > 0 ? g_stack[g_stack_n - 1] : NULL;
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

static void strip_extension(char *stem) {
    char *dot = strrchr(stem, '.');
    if (dot && strcmp(dot, ".openbounty") == 0) *dot = '\0';
}

static bool already_listed(const PackEntry *list, int n, const char *name) {
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].name, name) == 0) return true;
    }
    return false;
}

static int scan_one_dir(const char *dir, PackEntry *out, int filled, int cap) {
#ifdef _WIN32
    char pattern[PACK_ENTRY_PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s\\*.openbounty", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return filled;
    do {
        if (filled >= cap) break;
        char stem[PACK_ENTRY_NAME_MAX];
        snprintf(stem, sizeof stem, "%s", fd.cFileName);
        strip_extension(stem);
        if (already_listed(out, filled, stem)) continue;
        snprintf(out[filled].path, sizeof out[filled].path,
                 "%s%c%s", dir, PATHSEP, fd.cFileName);
        snprintf(out[filled].name, sizeof out[filled].name, "%s", stem);
        filled++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return filled;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (filled >= cap) break;
        if (!ends_with(de->d_name, ".openbounty")) continue;
        char stem[PACK_ENTRY_NAME_MAX];
        snprintf(stem, sizeof stem, "%s", de->d_name);
        strip_extension(stem);
        if (already_listed(out, filled, stem)) continue;
        snprintf(out[filled].path, sizeof out[filled].path,
                 "%s/%s", dir, de->d_name);
        snprintf(out[filled].name, sizeof out[filled].name, "%s", stem);
        filled++;
    }
    closedir(d);
#endif
    return filled;
}

int pack_discover(PackEntry *out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = 0;
    n = scan_one_dir(".", out, n, cap);
    char user_dir[PACK_ENTRY_PATH_MAX];
    if (SavePathGetPacksDir(user_dir, sizeof user_dir)) {
        n = scan_one_dir(user_dir, out, n, cap);
    }
    return n;
}

// ---------------------------------------------------------------------------
// pack_zip_dir / pack_rmtree — used by --extract to zip the extractor's
// loose-tree output into a single .openbounty archive.
// ---------------------------------------------------------------------------

static bool zip_walk(mz_zip_archive *zw, const char *root, const char *prefix) {
#ifdef _WIN32
    char pat[PACK_ENTRY_PATH_MAX + 4];
    snprintf(pat, sizeof pat, "%s\\*", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (ends_with(fd.cFileName, ".xcf") ||
            ends_with(fd.cFileName, ".psd") ||
            strstr(fd.cFileName, "Zone.Identifier")) continue;
        char full[PACK_ENTRY_PATH_MAX], rel[PACK_REL_MAX];
        snprintf(full, sizeof full, "%s%c%s", root, PATHSEP, fd.cFileName);
        if (!build_rel(rel, prefix, fd.cFileName)) { ok = false; continue; }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = zip_walk(zw, full, rel) && ok;
        } else {
            FILE *f = fopen(full, "rb");
            if (!f) { ok = false; continue; }
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            if (sz < 0) { fclose(f); ok = false; continue; }
            void *buf = malloc((size_t)sz);
            if (!buf) { fclose(f); ok = false; continue; }
            if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
                free(buf); fclose(f); ok = false; continue;
            }
            fclose(f);
            if (!mz_zip_writer_add_mem(zw, rel, buf, (size_t)sz, MZ_DEFAULT_COMPRESSION)) {
                ok = false;
            }
            free(buf);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
#else
    DIR *d = opendir(root);
    if (!d) return true;
    bool ok = true;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (ends_with(de->d_name, ".xcf") ||
            ends_with(de->d_name, ".psd") ||
            strstr(de->d_name, "Zone.Identifier")) continue;
        char full[PACK_ENTRY_PATH_MAX], rel[PACK_REL_MAX];
        snprintf(full, sizeof full, "%s/%s", root, de->d_name);
        if (!build_rel(rel, prefix, de->d_name)) { ok = false; continue; }
        if (is_dir(full)) {
            ok = zip_walk(zw, full, rel) && ok;
        } else {
            FILE *f = fopen(full, "rb");
            if (!f) { ok = false; continue; }
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            if (sz < 0) { fclose(f); ok = false; continue; }
            void *buf = malloc((size_t)sz);
            if (!buf) { fclose(f); ok = false; continue; }
            if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
                free(buf); fclose(f); ok = false; continue;
            }
            fclose(f);
            if (!mz_zip_writer_add_mem(zw, rel, buf, (size_t)sz, MZ_DEFAULT_COMPRESSION)) {
                ok = false;
            }
            free(buf);
        }
    }
    closedir(d);
    return ok;
#endif
}

bool pack_zip_dir(const char *src_dir, const char *out_zip) {
    if (!src_dir || !out_zip) return false;
    remove(out_zip);
    mz_zip_archive zw = {0};
    if (!mz_zip_writer_init_file(&zw, out_zip, 0)) {
        fprintf(stderr, "pack: cannot create %s\n", out_zip);
        return false;
    }
    bool ok = zip_walk(&zw, src_dir, "");
    if (ok) ok = mz_zip_writer_finalize_archive(&zw) ? true : false;
    mz_zip_writer_end(&zw);
    if (!ok) remove(out_zip);
    return ok;
}

bool pack_rmtree(const char *path) {
    if (!path || !path[0]) return false;
    if (!is_dir(path)) {
        return remove(path) == 0;
    }
#ifdef _WIN32
    char pat[PACK_ENTRY_PATH_MAX + 4];
    snprintf(pat, sizeof pat, "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return RemoveDirectoryA(path) ? true : false;
    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char full[PACK_ENTRY_PATH_MAX];
        snprintf(full, sizeof full, "%s%c%s", path, PATHSEP, fd.cFileName);
        ok = pack_rmtree(full) && ok;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (!RemoveDirectoryA(path)) ok = false;
    return ok;
#else
    DIR *d = opendir(path);
    if (!d) return rmdir(path) == 0;
    bool ok = true;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char full[PACK_ENTRY_PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", path, de->d_name);
        ok = pack_rmtree(full) && ok;
    }
    closedir(d);
    if (rmdir(path) != 0) ok = false;
    return ok;
#endif
}

bool pack_resolve_arg(const char *arg, char *out, size_t cap) {
    if (!arg || !arg[0] || !out || cap == 0) return false;

    bool has_sep = strchr(arg, '/') || strchr(arg, '\\');
    bool has_ext = ends_with(arg, ".openbounty");
    if (has_sep || has_ext) {
        struct stat st;
        if (stat(arg, &st) != 0) {
            fprintf(stderr, "pack: not found: %s\n", arg);
            return false;
        }
        snprintf(out, cap, "%s", arg);
        return true;
    }

    // Bare name. Search cwd then user dir, with-or-without extension.
    const char *roots[2] = { ".", NULL };
    char user_dir[PACK_ENTRY_PATH_MAX];
    if (SavePathGetPacksDir(user_dir, sizeof user_dir)) {
        roots[1] = user_dir;
    }
    for (int i = 0; i < 2; i++) {
        if (!roots[i]) continue;
        const char *suffixes[2] = { ".openbounty", "" };
        for (int j = 0; j < 2; j++) {
            char candidate[PACK_ENTRY_PATH_MAX];
            snprintf(candidate, sizeof candidate, "%s%c%s%s",
                     roots[i], PATHSEP, arg, suffixes[j]);
            struct stat st;
            if (stat(candidate, &st) == 0) {
                snprintf(out, cap, "%s", candidate);
                return true;
            }
        }
    }
    fprintf(stderr, "pack: '%s' not found in cwd or user packs dir\n", arg);
    return false;
}
