#include "screenshot.h"
#include "raylib.h"
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

void screenshot_save(RenderTexture2D rt, const char *prefix) {
    if (!prefix || !prefix[0]) prefix = "shot";
    // The folder is gitignored, so a fresh checkout has none; make it rather
    // than let ExportImage fail with a FILEIO warning.
    if (!DirectoryExists("screenshots")) MKDIR("screenshots");
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    int seq = 0;
    char path[256];
    for (;;) {
        snprintf(path, sizeof(path),
                 "screenshots/%s_%04d.png", prefix, seq);
        if (!FileExists(path)) break;
        seq++;
        if (seq > 9999) break;
    }
    ExportImage(img, path);
    UnloadImage(img);
}

void screenshot_tick(RenderTexture2D rt, const char *prefix) {
    if (!IsKeyPressed(KEY_GRAVE)) return;
    screenshot_save(rt, prefix ? prefix : "shot");
}
