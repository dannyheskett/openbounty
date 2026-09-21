// The CPU-image half of src/gfx.h for iOS: decode, allocate, and the two
// blits the shell does before anything reaches the GPU. Plain C -- Metal is
// not involved, so none of this belongs in gfx_metal.mm.
//
// stb_image is the same decoder raylib uses internally, at the same version
// (third_party/raylib/src/external/), so a PNG decodes to the same pixels on
// every platform.
//
// Every image here is 8-bit RGBA, which is what gfx_texture_from_image uploads
// and what the two blits assume.

#include "gfx.h"

#if defined(PLATFORM_IOS)

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO          // every asset arrives as bytes from the pack
#include "stb_image.h"

#include <stdlib.h>
#include <string.h>

Image gfx_image_from_memory(const char *ext, const unsigned char *bytes, int size) {
    Image img;
    memset(&img, 0, sizeof img);
    (void)ext;   // the pack ships PNG only; stb sniffs the header regardless
    if (!bytes || size <= 0) return img;
    int w = 0, h = 0, comp = 0;
    unsigned char *px = stbi_load_from_memory(bytes, size, &w, &h, &comp, 4);
    if (!px) return img;
    img.data = px;
    img.width = w;
    img.height = h;
    return img;
}

Image gfx_image_solid(int w, int h, Color color) {
    Image img;
    memset(&img, 0, sizeof img);
    if (w <= 0 || h <= 0) return img;
    unsigned char *px = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
    if (!px) return img;
    for (int i = 0; i < w * h; i++) {
        px[i * 4 + 0] = color.r;
        px[i * 4 + 1] = color.g;
        px[i * 4 + 2] = color.b;
        px[i * 4 + 3] = color.a;
    }
    img.data = px;
    img.width = w;
    img.height = h;
    return img;
}

void gfx_image_free(Image img) { free(img.data); }

void gfx_image_fill_rect(Image *dst, int x, int y, int w, int h, Color color) {
    if (!dst || !dst->data) return;
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= dst->height) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= dst->width) continue;
            unsigned char *p = (unsigned char *)dst->data + ((size_t)yy * dst->width + xx) * 4;
            p[0] = color.r; p[1] = color.g; p[2] = color.b; p[3] = color.a;
        }
    }
}

// Nearest-neighbour, because every use in the shell is a 1:1 copy of a glyph
// cell or a tile: src/bfont.c patches four control-code slots from printable
// ones, and nothing scales. A tint of white leaves the pixels alone, which is
// the only tint any caller passes.
void gfx_image_blit(Image *dst, Image src, Rectangle src_rect,
                    Rectangle dst_rect, Color tint) {
    if (!dst || !dst->data || !src.data) return;
    if (dst_rect.width <= 0 || dst_rect.height <= 0) return;
    if (src_rect.width <= 0 || src_rect.height <= 0) return;

    for (int j = 0; j < (int)dst_rect.height; j++) {
        int dy = (int)dst_rect.y + j;
        if (dy < 0 || dy >= dst->height) continue;
        int sy = (int)(src_rect.y + src_rect.height * ((float)j / dst_rect.height));
        if (sy < 0 || sy >= src.height) continue;
        for (int i = 0; i < (int)dst_rect.width; i++) {
            int dx = (int)dst_rect.x + i;
            if (dx < 0 || dx >= dst->width) continue;
            int sx = (int)(src_rect.x + src_rect.width * ((float)i / dst_rect.width));
            if (sx < 0 || sx >= src.width) continue;

            const unsigned char *s =
                (const unsigned char *)src.data + ((size_t)sy * src.width + sx) * 4;
            unsigned char *d =
                (unsigned char *)dst->data + ((size_t)dy * dst->width + dx) * 4;
            unsigned a = (unsigned)s[3] * tint.a / 255u;
            if (a == 0) continue;
            // Source-over, which is what ImageDraw does.
            for (int k = 0; k < 3; k++) {
                unsigned sc = (unsigned)s[k] * ((k == 0) ? tint.r : (k == 1) ? tint.g : tint.b) / 255u;
                d[k] = (unsigned char)((sc * a + d[k] * (255u - a)) / 255u);
            }
            d[3] = (unsigned char)(a + d[3] * (255u - a) / 255u);
        }
    }
}

#endif // PLATFORM_IOS
