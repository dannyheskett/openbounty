#include "plat_android.h"

#if defined(PLATFORM_ANDROID)

#include "raylib.h"
#include "savepath.h"

#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Exported by raylib's Android backend (rcore_android.c) with default
// visibility. Declared here rather than included: raylib.h does not carry it,
// and this is the only thing the shell needs from the activity.
extern struct android_app *GetAndroidApp(void);

// The game reports everything it does on stdout -- the pack it opened, the
// seed, every fatal. On Android stdout is /dev/null, so it is piped into
// logcat one line at a time, exactly as the iOS build pipes it into os_log.
// Without this the only diagnosis available for a black screen is guesswork.
static void *log_pump(void *arg) {
    int rd = (int)(long)arg;
    char buf[1024];
    size_t used = 0;
    for (;;) {
        ssize_t n = read(rd, buf + used, sizeof buf - used - 1);
        if (n <= 0) break;
        used += (size_t)n;
        buf[used] = '\0';
        char *line = buf, *nl;
        while ((nl = strchr(line, '\n')) != NULL) {
            *nl = '\0';
            __android_log_write(ANDROID_LOG_INFO, "openbounty", line);
            line = nl + 1;
        }
        used = strlen(line);
        memmove(buf, line, used + 1);
        if (used >= sizeof buf - 2) {      // a line longer than the buffer
            __android_log_write(ANDROID_LOG_INFO, "openbounty", buf);
            used = 0;
        }
    }
    return NULL;
}

static void log_stdout(void) {
    int fds[2];
    if (pipe(fds) != 0) return;
    dup2(fds[1], STDOUT_FILENO);
    setvbuf(stdout, NULL, _IOLBF, 0);
    pthread_t t;
    if (pthread_create(&t, NULL, log_pump, (void *)(long)fds[0]) == 0)
        pthread_detach(t);
}

void plat_android_boot(void) {
    log_stdout();
    struct android_app *app = GetAndroidApp();
    if (!app || !app->activity || !app->activity->internalDataPath) return;
    // The app's private directory: the only place the process may write, and
    // it is removed when the app is uninstalled, which is the behaviour
    // Android users expect of save data.
    char dir[512];
    snprintf(dir, sizeof dir, "%s/saves", app->activity->internalDataPath);
    SavePathSetDirOverride(dir);
}

Pack *plat_android_open_pack(void) {
    // Read the whole pack out of the APK and open it from memory rather than
    // letting pack_open() reach for the file.
    //
    // raylib's Android backend wraps fopen (-Wl,--wrap=fopen) so a relative
    // open falls through to the asset manager, but miniz does not always call
    // fopen: with __USE_LARGEFILE64 it calls fopen64, which the wrap does not
    // intercept, and the pack would silently fail to open. LoadFileData goes
    // straight to AAssetManager, so this path does not depend on which variant
    // bionic selects.
    int size = 0;
    unsigned char *bytes = LoadFileData(ANDROID_PACK_ASSET, &size);
    if (!bytes || size <= 0) {
        fprintf(stdout, "android: cannot read %s from the APK\n",
                ANDROID_PACK_ASSET);
        if (bytes) UnloadFileData(bytes);
        return NULL;
    }
    Pack *p = pack_open_mem(bytes, (size_t)size, ANDROID_PACK_ASSET);
    // pack_open_mem copies every entry out, so the APK blob is done with here.
    UnloadFileData(bytes);
    return p;
}

#else   // every other platform

void plat_android_boot(void) {}

Pack *plat_android_open_pack(void) { return NULL; }

#endif
