#include "savepath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define PATHSEP '\\'
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#define PATHSEP '/'
#endif

static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    return MKDIR(path) == 0;
}

static char s_dir_override[512] = {0};

void SavePathSetDirOverride(const char *dir) {
    if (!dir || !*dir) {
        s_dir_override[0] = '\0';
        return;
    }
    snprintf(s_dir_override, sizeof(s_dir_override), "%s", dir);
}

bool SavePathGetDir(char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    if (s_dir_override[0]) {
        if (!ensure_dir(s_dir_override)) {
            fprintf(stderr, "SavePathGetDir: cannot create %s\n",
                    s_dir_override);
            return false;
        }
        size_t n = strlen(s_dir_override);
        if (n + 1 > out_size) return false;
        memcpy(out, s_dir_override, n + 1);
        return true;
    }
    char dir[512] = {0};

#if defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (!appdata || !*appdata) return false;
    snprintf(dir, sizeof(dir), "%s\\OpenBounty", appdata);
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    char lib[512], appsupport[512];
    snprintf(lib, sizeof(lib), "%s/Library", home);
    ensure_dir(lib);
    snprintf(appsupport, sizeof(appsupport), "%s/Library/Application Support", home);
    ensure_dir(appsupport);
    snprintf(dir, sizeof(dir), "%s/Library/Application Support/OpenBounty", home);
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        snprintf(dir, sizeof(dir), "%s/openbounty", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home || !*home) return false;
        char local[512], share[512];
        snprintf(local, sizeof(local), "%s/.local", home);
        ensure_dir(local);
        snprintf(share, sizeof(share), "%s/.local/share", home);
        ensure_dir(share);
        snprintf(dir, sizeof(dir), "%s/.local/share/openbounty", home);
    }
#endif

    if (!ensure_dir(dir)) {
        fprintf(stderr, "SavePathGetDir: cannot create %s\n", dir);
        return false;
    }
    size_t n = strlen(dir);
    if (n + 1 > out_size) return false;
    memcpy(out, dir, n + 1);
    return true;
}

bool SavePathGetPacksDir(char *out, size_t out_size) {
    char base[512];
    if (!SavePathGetDir(base, sizeof base)) return false;
    char full[600];
    snprintf(full, sizeof full, "%s%cpacks", base, PATHSEP);
    if (!ensure_dir(full)) {
        fprintf(stderr, "SavePathGetPacksDir: cannot create %s\n", full);
        return false;
    }
    size_t n = strlen(full);
    if (n + 1 > out_size) return false;
    memcpy(out, full, n + 1);
    return true;
}

bool SavePathGetSlot(int slot, char *out, size_t out_size) {
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return false;
    char dir[512];
    if (!SavePathGetDir(dir, sizeof(dir))) return false;
    snprintf(out, out_size, "%s%csave_%d.dat", dir, PATHSEP, slot);
    return true;
}

bool SavePathGet(char *out, size_t out_size) {
    return SavePathGetSlot(0, out, out_size);
}
