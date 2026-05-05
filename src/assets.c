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
        fprintf(stderr, "LoadAssetTexture: not in pack: %s\n", path);
        return (Texture2D){0};
    }
    Image img = LoadImageFromMemory(path_ext(path), bytes, (int)sz);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

const unsigned char *LoadAssetBytes(const char *path, size_t *out_size) {
    return pack_stack_read(path, out_size);
}

void UnloadAssetBytes(const unsigned char *data) {
    (void)data;  // bytes owned by pack
}
