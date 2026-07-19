#ifndef OB_ASSETS_H
#define OB_ASSETS_H

#include "raylib.h"
#include "assets_bytes.h"  // engine-side byte access

// Shell-side asset access: texture loading (raylib-typed). The engine
// uses assets_bytes.h above for byte-level reads from the pack stack.
//
// Resolution goes through the global pack stack (src/pack.c). Paths
// are pack-relative (e.g. "art/font/kb-font.png").

Texture2D LoadAssetTexture(const char *path);

#endif
