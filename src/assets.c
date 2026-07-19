#include "assets.h"
#include "pack.h"
#include <stdio.h>
#include <string.h>

static const char *path_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot ? dot : "";
}

Texture2D LoadAssetTexture(const char *path) {
    size_t sz = 0;
    const unsigned char *bytes = pack_stack_read(path, &sz);
    if (!bytes || sz == 0) {
        fprintf(stdout, "LoadAssetTexture: not in pack: %s\n", path);
        return (Texture2D){0};
    }
    Image img = LoadImageFromMemory(path_ext(path), bytes, (int)sz);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

// LoadAssetBytes / UnloadAssetBytes moved to engine/assets_bytes.c.
