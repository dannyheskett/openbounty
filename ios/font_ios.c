// src/font_backend.h for iOS: rasterise the pack's own TrueType face with
// stb_truetype and pack it into one atlas.
//
// The metrics MUST match what raylib's LoadFontData reports on desktop,
// because src/text.c derives the layout from them -- the fixed advance, the
// line height, every glyph's ink box. raylib's rtext.c uses stb_truetype
// itself with these same calls (stbtt_ScaleForPixelHeight, GetCodepointBitmap,
// GetCodepointHMetrics), so this is the same arithmetic in the same order, not
// a lookalike.
//
// The atlas packer is simpler than raylib's skyline: a left-to-right row
// packer with 2px padding. It only has to agree with ITSELF -- text.c reads
// back each glyph's rect -- and a fixed glyph set of 132 cells fits any
// sensible texture either way.

#include "font_backend.h"

#if defined(PLATFORM_IOS)

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <stdlib.h>
#include <string.h>

#define FONT_PAD 2

// Shared by measure and bake: scale, ascent, and each glyph's metrics.
typedef struct {
    stbtt_fontinfo info;
    float scale;
    int   ascent;
} Face;

static bool face_open(Face *f, const unsigned char *ttf, int px) {
    if (!stbtt_InitFont(&f->info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)))
        return false;
    f->scale = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    int desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &desc, &gap);
    return true;
}

// One glyph's metrics, in the same terms raylib reports: advance in pixels,
// bearings measured from the LINE TOP (not the baseline), and the ink size.
static void glyph_metrics(Face *f, int cp, FontGlyph *out, int *ox, int *oy,
                          int *iw, int *ih) {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBox(&f->info, cp, f->scale, f->scale,
                                &x0, &y0, &x1, &y1);
    out->advance = (int)(adv * f->scale + 0.5f);
    // y0 is relative to the baseline (negative above it); raylib's offsetY is
    // from the line top, so add the scaled ascent.
    out->off_x = x0;
    out->off_y = (int)(f->ascent * f->scale) + y0;
    out->ink_w = x1 - x0;
    out->ink_h = y1 - y0;
    out->src   = (Rectangle){ 0, 0, 0, 0 };
    if (ox) *ox = x0;
    if (oy) *oy = y0;
    if (iw) *iw = x1 - x0;
    if (ih) *ih = y1 - y0;
}

bool font_backend_measure(const unsigned char *ttf, int ttf_size, int px,
                          const int *codepoints, int count, FontGlyph *out) {
    if (!ttf || ttf_size <= 0 || !codepoints || count <= 0 || !out) return false;
    Face f;
    if (!face_open(&f, ttf, px)) return false;
    for (int i = 0; i < count; i++)
        glyph_metrics(&f, codepoints[i], &out[i], NULL, NULL, NULL, NULL);
    return true;
}

bool font_backend_bake(const unsigned char *ttf, int ttf_size, int px,
                       const int *codepoints, int count, FontAtlas *out) {
    if (!ttf || ttf_size <= 0 || !codepoints || count <= 0 || !out) return false;
    Face f;
    if (!face_open(&f, ttf, px)) return false;

    FontGlyph *glyphs = (FontGlyph *)calloc((size_t)count, sizeof *glyphs);
    if (!glyphs) return false;

    // Pass one: metrics, and a width wide enough for a square-ish atlas.
    int total = 0, tallest = 0;
    for (int i = 0; i < count; i++) {
        glyph_metrics(&f, codepoints[i], &glyphs[i], NULL, NULL, NULL, NULL);
        total += glyphs[i].ink_w + FONT_PAD;
        if (glyphs[i].ink_h > tallest) tallest = glyphs[i].ink_h;
    }
    int atlas_w = 128;
    while (atlas_w * atlas_w < total * (tallest + FONT_PAD) && atlas_w < 4096)
        atlas_w *= 2;

    // Pass two: place each glyph, wrapping to a new row when it will not fit.
    int pen_x = FONT_PAD, pen_y = FONT_PAD, row_h = 0;
    for (int i = 0; i < count; i++) {
        int gw = glyphs[i].ink_w, gh = glyphs[i].ink_h;
        if (pen_x + gw + FONT_PAD > atlas_w) {
            pen_x = FONT_PAD;
            pen_y += row_h + FONT_PAD;
            row_h = 0;
        }
        glyphs[i].src = (Rectangle){ (float)pen_x, (float)pen_y,
                                     (float)gw, (float)gh };
        pen_x += gw + FONT_PAD;
        if (gh > row_h) row_h = gh;
    }
    int atlas_h = pen_y + row_h + FONT_PAD;
    if (atlas_h < 1) atlas_h = 1;

    // Pass three: rasterise into an RGBA buffer. stb gives 8-bit coverage;
    // the atlas is white with coverage in alpha, which is what the shader
    // tints -- the same shape raylib's font atlas has.
    unsigned char *px_rgba =
        (unsigned char *)calloc((size_t)atlas_w * (size_t)atlas_h, 4);
    if (!px_rgba) { free(glyphs); return false; }

    unsigned char *mono = (unsigned char *)malloc((size_t)atlas_w * (size_t)atlas_h);
    if (!mono) { free(px_rgba); free(glyphs); return false; }
    memset(mono, 0, (size_t)atlas_w * (size_t)atlas_h);

    for (int i = 0; i < count; i++) {
        int gw = glyphs[i].ink_w, gh = glyphs[i].ink_h;
        if (gw <= 0 || gh <= 0) continue;
        int ox = (int)glyphs[i].src.x, oy = (int)glyphs[i].src.y;
        stbtt_MakeCodepointBitmap(&f.info, mono + (size_t)oy * atlas_w + ox,
                                  gw, gh, atlas_w, f.scale, f.scale,
                                  codepoints[i]);
    }
    for (int i = 0; i < atlas_w * atlas_h; i++) {
        px_rgba[i * 4 + 0] = 255;
        px_rgba[i * 4 + 1] = 255;
        px_rgba[i * 4 + 2] = 255;
        px_rgba[i * 4 + 3] = mono[i];
    }
    free(mono);

    Image img;
    memset(&img, 0, sizeof img);
    img.data = px_rgba;
    img.width = atlas_w;
    img.height = atlas_h;
    Texture2D tex = gfx_texture_from_image(img);
    free(px_rgba);
    if (tex.id == 0) { free(glyphs); return false; }
    gfx_texture_point(tex);

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

#endif // PLATFORM_IOS
