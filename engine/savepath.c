// readlink, _NSGetExecutablePath etc. need POSIX/BSD feature flags
// before the system headers are pulled in.
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "savepath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#define PATHSEP '\\'
#elif defined(__APPLE__)
#include <unistd.h>
#include <mach-o/dyld.h>
#define MKDIR(p) mkdir((p), 0755)
#define PATHSEP '/'
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

// Make every parent of `path` (and `path` itself) exist. Treats both
// '/' and '\\' as separators so the same code works on every OS.
static bool ensure_dir_recursive(const char *path) {
    if (!path || !*path) return false;
    char buf[1024];
    size_t n = strlen(path);
    if (n + 1 > sizeof buf) return false;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = '\0';
            if (!ensure_dir(buf)) return false;
            buf[i] = saved;
        }
    }
    return ensure_dir(buf);
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
            fprintf(stdout, "SavePathGetDir: cannot create %s\n",
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
        fprintf(stdout, "SavePathGetDir: cannot create %s\n", dir);
        return false;
    }
    size_t n = strlen(dir);
    if (n + 1 > out_size) return false;
    memcpy(out, dir, n + 1);
    return true;
}

bool SavePathGetSlot(const char *pack_id, int slot, char *out, size_t out_size) {
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return false;
    if (!out || out_size == 0) return false;

    // --save-dir override is flat: caller picked the exact dir, so we
    // don't interpolate pack_id under it. Useful for harness/tests.
    if (s_dir_override[0]) {
        if (!ensure_dir(s_dir_override)) return false;
        snprintf(out, out_size, "%s%csave_%d.dat",
                 s_dir_override, PATHSEP, slot);
        return true;
    }

    char base[512];
    if (!SavePathGetDir(base, sizeof base)) return false;

    // Default layout: <user-data>/openbounty/saves/<pack_id>/save_N.dat.
    // Empty pack_id is treated as "unknown"; saves still need *some*
    // bucket so they don't collide with packed games. This shouldn't
    // happen in practice — a pack must be loaded before saving.
    const char *pid = (pack_id && pack_id[0]) ? pack_id : "_unknown";
    char dir[768];
    snprintf(dir, sizeof dir, "%s%csaves%c%s",
             base, PATHSEP, PATHSEP, pid);
    if (!ensure_dir_recursive(dir)) {
        fprintf(stdout, "SavePathGetSlot: cannot create %s\n", dir);
        return false;
    }
    snprintf(out, out_size, "%s%csave_%d.dat", dir, PATHSEP, slot);
    return true;
}

bool SavePathGetExeDir(char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    char path[1024] = {0};

#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    if (n == 0 || n >= sizeof path) return false;
#elif defined(__APPLE__)
    uint32_t sz = sizeof path;
    if (_NSGetExecutablePath(path, &sz) != 0) return false;
#else
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) return false;
    path[n] = '\0';
#endif

    char *sep = strrchr(path, PATHSEP);
#if !defined(_WIN32)
    // On POSIX a backslash is a legal filename character — only '/'
    // separates components. On Windows strrchr already finds '\\', so
    // we don't need a second pass.
#else
    char *fwd = strrchr(path, '/');
    if (fwd && (!sep || fwd > sep)) sep = fwd;
#endif
    if (!sep) return false;
    *sep = '\0';
    size_t n2 = strlen(path);
    if (n2 + 1 > out_size) return false;
    memcpy(out, path, n2 + 1);
    return true;
}
