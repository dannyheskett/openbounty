// The TrueType font route: the fit and advance arithmetic (pure), and the
// shipped Rome face rasterised through raylib's CPU-side loader at the
// declared size, to show it fits the 16 px cell the layout gives it.

#include "greatest.h"
#include "bfont.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>

TEST fits_when_ink_is_inside_the_cell(void) {
    int w[3] = { 10, 0, 12 }, top[3] = { 2, 0, 3 }, bottom[3] = { 14, 0, 16 };
    ASSERT(bfont_fits(w, top, bottom, 3, 16, 16));      // span 2..16 = 14, width 12
    int w2[2] = { 17, 8 }, t2[2] = { 0, 0 }, b2[2] = { 10, 10 };
    ASSERT_FALSE(bfont_fits(w2, t2, b2, 2, 16, 16));    // too wide
    int w3[2] = { 8, 8 }, t3[2] = { -2, 0 }, b3[2] = { 10, 15 };
    ASSERT_FALSE(bfont_fits(w3, t3, b3, 2, 16, 16));    // span -2..15 = 17, too tall
    int w4[1] = { 0 }, t4[1] = { 0 }, b4[1] = { 0 };
    ASSERT_FALSE(bfont_fits(w4, t4, b4, 1, 16, 16));    // nothing but spaces
    PASS();
}

TEST advance_is_widest_ink_plus_one_capped_at_the_cell(void) {
    ASSERT_EQ(11, bfont_advance_for(10, 16));
    ASSERT_EQ(16, bfont_advance_for(15, 16));
    ASSERT_EQ(16, bfont_advance_for(40, 16));
    ASSERT_EQ(1, bfont_advance_for(0, 16));
    PASS();
}

static unsigned char *read_file(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)*n);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); fclose(f); return NULL; }
    fclose(f);
    return b;
}

TEST rome_face_fits_its_cell_at_the_declared_size(void) {
    long n = 0;
    unsigned char *bytes = read_file("assets/glory-of-rome/art/font/Cinzel-Bold.ttf", &n);
    ASSERT(bytes != NULL);
    int cps[95];
    for (int i = 0; i < 95; i++) cps[i] = 32 + i;
    int count = 0;
    GlyphInfo *g = LoadFontData(bytes, (int)n, 15, cps, 95, FONT_DEFAULT, &count);
    ASSERT(g != NULL);
    ASSERT_EQ(95, count);
    int w[95], top[95], bottom[95], mw = 0;
    for (int i = 0; i < 95; i++) {
        w[i] = g[i].image.width; top[i] = g[i].offsetY; bottom[i] = g[i].offsetY + g[i].image.height;
        if (w[i] > mw) mw = w[i];
    }
    ASSERT(bfont_fits(w, top, bottom, 95, 16, 16));
    int adv = bfont_advance_for(mw, 16);
    ASSERT(adv >= 8 && adv <= 16);
    UnloadFontData(g, count);
    free(bytes);
    PASS();
}

SUITE(unit_bfont_suite) {
    RUN_TEST(fits_when_ink_is_inside_the_cell);
    RUN_TEST(advance_is_widest_ink_plus_one_capped_at_the_cell);
    RUN_TEST(rome_face_fits_its_cell_at_the_declared_size);
}
