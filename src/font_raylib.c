// raylib backend for src/font_backend.h: LoadFontData rasterises the glyph
// set, GenImageFontAtlas packs it, and the result is copied into the seam's
// own structs so src/text.c holds no raylib type. iOS compiles its stb_truetype
// equivalent instead and never builds this file.
//
// The packing arguments -- 2px of padding, method 0 (the skyline packer) --
// are what text.c passed before the seam existed, and the atlas it produces is
// what the glyph source rects are measured against, so they are not free
// parameters.

#include "font_backend.h"

#if !defined(PLATFORM_IOS)

#include "raylib.h"
#include <stdlib.h>
#include <string.h>

#define FONT_PAD 2

static void copy_metrics(const GlyphInfo *g, FontGlyph *out) {
    // A glyph with no ink (space) reports a zero-size image; advanceX can also
    // be zero on a face that leans on the image width, which is what text.c's
    // widest-advance scan already compensates for.
    out->advance = g->advanceX;
    out->off_x   = g->offsetX;
    out->off_y   = g->offsetY;
    out->ink_w   = g->image.width;
    out->ink_h   = g->image.height;
    out->src     = (Rectangle){ 0, 0, 0, 0 };
}

bool font_backend_measure(const unsigned char *ttf, int ttf_size, int px,
                          const int *codepoints, int count, FontGlyph *out) {
    if (!ttf || ttf_size <= 0 || !codepoints || count <= 0 || !out) return false;
    int got = 0;
    GlyphInfo *g = LoadFontData(ttf, ttf_size, px, (int *)codepoints, count,
                                FONT_DEFAULT, &got);
    if (!g) return false;
    if (got != count) { UnloadFontData(g, got); return false; }
    for (int i = 0; i < count; i++) copy_metrics(&g[i], &out[i]);
    UnloadFontData(g, got);
    return true;
}

bool font_backend_bake(const unsigned char *ttf, int ttf_size, int px,
                       const int *codepoints, int count, FontAtlas *out) {
    if (!ttf || ttf_size <= 0 || !codepoints || count <= 0 || !out) return false;
    int got = 0;
    GlyphInfo *g = LoadFontData(ttf, ttf_size, px, (int *)codepoints, count,
                                FONT_DEFAULT, &got);
    if (!g) return false;
    if (got != count) { UnloadFontData(g, got); return false; }

    Rectangle *recs = NULL;
    Image atlas = GenImageFontAtlas(g, &recs, count, px, FONT_PAD, 0);
    Texture2D tex = gfx_texture_from_image(atlas);
    gfx_image_free(atlas);
    if (tex.id == 0) {
        free(recs);
        UnloadFontData(g, got);
        return false;
    }
    gfx_texture_point(tex);

    FontGlyph *glyphs = (FontGlyph *)calloc((size_t)count, sizeof *glyphs);
    if (!glyphs) {
        gfx_texture_free(tex);
        free(recs);
        UnloadFontData(g, got);
        return false;
    }
    for (int i = 0; i < count; i++) {
        copy_metrics(&g[i], &glyphs[i]);
        glyphs[i].src = recs[i];
    }

    // raylib allocated recs with its own malloc; the GlyphInfo array owns the
    // per-glyph images. Both are finished with once the metrics are copied and
    // the atlas is uploaded.
    free(recs);
    UnloadFontData(g, got);

    out->texture   = tex;
    out->glyphs    = glyphs;
    out->count     = count;
    out->base_size = px;
    return true;
}

void font_backend_free(FontAtlas *f) {
    if (!f) return;
    if (f->texture.id) gfx_texture_free(f->texture);
    free(f->glyphs);
    memset(f, 0, sizeof *f);
}

#endif // !PLATFORM_IOS
