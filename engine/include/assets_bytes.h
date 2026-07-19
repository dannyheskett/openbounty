// engine/include/assets_bytes.h
//
// Engine-side asset access: byte-level reads from the active pack stack.
// No raylib. Shell code that wants Texture2D loads uses src/assets.h.

#ifndef OB_ENGINE_ASSETS_BYTES_H
#define OB_ENGINE_ASSETS_BYTES_H

#include <stddef.h>

// Returns a borrowed pointer to bytes for `path` and fills *out_size.
// Returns NULL if not present in any active pack (and zeroes *out_size
// when non-NULL). Caller MUST NOT free.
const unsigned char *LoadAssetBytes(const char *path, size_t *out_size);

// No-op: asset bytes are owned by the pack and outlive the caller, so there
// is nothing to free. Call sites keep the paired Load/Unload shape.
void UnloadAssetBytes(const unsigned char *data);

#endif
