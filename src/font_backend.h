// src/font_backend.h
//
// Rasterising the pack's TrueType face into an atlas. src/text.c owns every
// decision above this line -- which codepoints, what size, the fixed advance
// it derives, the line height, how a glyph is centred in its cell -- and this
// is only "turn these codepoints into pixels and metrics".
//
// Two backends:
//   src/font_raylib.c   raylib's LoadFontData + GenImageFontAtlas
//   ios/font_ios.c      stb_truetype (iOS, links no raylib)
//
// The face is a PACK asset (game.json's font.file), so it cannot be baked
// ahead of time into the binary the way a game with one built-in face would:
// every pack picks its own, and the layout reads the metrics back. That is why
// this is a real seam and not a pre-generated atlas.

#ifndef OB_FONT_BACKEND_H
#define OB_FONT_BACKEND_H

#include "gfx.h"
#include <stdbool.h>

typedef struct {
    int advance;        // pen movement for this glyph, in pixels
    int off_x, off_y;   // ink bearings, measured from the line top
    int ink_w, ink_h;   // the ink's own size; 0 for a blank such as space
    Rectangle src;      // where the ink sits in the atlas texture
} FontGlyph;

typedef struct {
    Texture2D  texture;   // the atlas, already uploaded
    FontGlyph *glyphs;    // `count` entries, in the order the codepoints came
    int        count;
    int        base_size; // the pixel size it was baked at
} FontAtlas;

// Metrics only, no texture and no GL: this runs before the window exists, so
// the layout can size itself from the face. `out` takes `count` entries.
bool font_backend_measure(const unsigned char *ttf, int ttf_size, int px,
                          const int *codepoints, int count, FontGlyph *out);

// Rasterise and upload. Returns false and leaves `out` untouched on failure.
bool font_backend_bake(const unsigned char *ttf, int ttf_size, int px,
                       const int *codepoints, int count, FontAtlas *out);

void font_backend_free(FontAtlas *f);

#endif // OB_FONT_BACKEND_H
