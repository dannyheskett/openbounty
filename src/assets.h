#ifndef ASSETS_H
#define ASSETS_H

#include <stddef.h>
#include "raylib.h"

// Asset access — all paths are pack-relative (e.g. "art/font/kb-font.png").
// Resolution goes through the global pack stack (src/pack.c). Callers
// MUST NOT free pointers returned by LoadAssetBytes — they are borrowed
// from the active pack and stay valid until the pack is closed.

Texture2D LoadAssetTexture(const char *path);

// Returns a borrowed pointer to bytes for `path` and fills *out_size.
// Returns NULL if not present in any active pack (and zeroes *out_size
// when non-NULL).
const unsigned char *LoadAssetBytes(const char *path, size_t *out_size);

// Deprecated no-op kept so existing call sites still compile. Bytes are
// owned by the pack now.
void UnloadAssetBytes(const unsigned char *data);

#endif
