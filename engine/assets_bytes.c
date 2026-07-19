// engine/assets_bytes.c -- engine-side asset byte reads.
//
// The texture-loading counterpart (LoadAssetTexture) lives in
// src/assets.c with the rest of the renderer.

#include "assets_bytes.h"
#include "pack.h"

const unsigned char *LoadAssetBytes(const char *path, size_t *out_size) {
    return pack_stack_read(path, out_size);
}

void UnloadAssetBytes(const unsigned char *data) {
    (void)data;  // bytes owned by pack
}
