#include "bfont.h"
#include "assets.h"
#include <stdio.h>

static Texture2D g_font_tex;
static bool      g_ready = false;

// places special glyph codepoints in the control-char range
// ():
//   \x1D pipe (twirl |)
//   \x05 slash arrow (twirl /)
//   \x1F dash (twirl -)
//   \x1C backslash (twirl \)
//
// The source font's glyphs at those slots are not exported by the
// pack's font PNG. Patch the texture in-place by copying the printable
// '|', '/', '-', '\\' glyphs into the control-char slots so any string
// that uses those codepoints renders the right shape.
static void bfont_patch_twirl_glyphs(Image *img) {
    struct { int dst_code; int src_code; } pairs[] = {
        { 0x1D, '|' },   // pipe
        { 0x05, '/' },   // slash
        { 0x1F, '-' },   // dash
        { 0x1C, '\\' },  // backslash
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        Rectangle src = { (float)(pairs[i].src_code * BFONT_GLYPH_W), 0.0f,
                          (float)BFONT_GLYPH_W, (float)BFONT_GLYPH_H };
        Rectangle dst = { (float)(pairs[i].dst_code * BFONT_GLYPH_W), 0.0f,
                          (float)BFONT_GLYPH_W, (float)BFONT_GLYPH_H };
        ImageDraw(img, *img, src, dst, WHITE);
    }
}

bool bfont_init(const char *png_path) {
    // Load via LoadAssetBytes so this works for both embedded and
    // on-disk builds (same path LoadAssetTexture takes internally).
    size_t sz = 0;
    const unsigned char *data = LoadAssetBytes(png_path, &sz);
    if (!data || sz == 0) {
        fprintf(stdout, "bfont: failed to read %s\n", png_path);
        g_ready = false;
        return false;
    }
    Image img = LoadImageFromMemory(".png", data, (int)sz);
    if (img.data == NULL) {
        fprintf(stdout, "bfont: failed to decode %s\n", png_path);
        g_ready = false;
        return false;
    }
    bfont_patch_twirl_glyphs(&img);
    g_font_tex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (g_font_tex.id == 0) {
        fprintf(stdout, "bfont: failed to upload %s\n", png_path);
        g_ready = false;
        return false;
    }
    SetTextureFilter(g_font_tex, TEXTURE_FILTER_POINT);
    g_ready = true;
    return true;
}

void bfont_shutdown(void) {
    if (g_ready) UnloadTexture(g_font_tex);
    g_ready = false;
}

bool bfont_ready(void) { return g_ready; }

int bfont_line_height(void) { return BFONT_GLYPH_H; }

void bfont_draw(const char *text, int x, int y, Color c) {
    if (!g_ready || !text) return;
    int cx = x;
    int cy = y;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            cx = x;
            cy += BFONT_GLYPH_H;
            continue;
        }
        unsigned char ch = (unsigned char)*p;
        if (ch >= 128) ch = ' ';
        Rectangle src = { (float)(ch * BFONT_GLYPH_W), 0.0f,
                          (float)BFONT_GLYPH_W, (float)BFONT_GLYPH_H };
        Rectangle dst = { (float)cx, (float)cy,
                          (float)BFONT_GLYPH_W, (float)BFONT_GLYPH_H };
        DrawTexturePro(g_font_tex, src, dst, (Vector2){ 0, 0 }, 0.0f, c);
        cx += BFONT_GLYPH_W;
    }
}

Vector2 bfont_measure(const char *text) {
    Vector2 v = { 0.0f, (float)BFONT_GLYPH_H };
    if (!text) return v;
    int w_line = 0, w_max = 0, h = BFONT_GLYPH_H;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            if (w_line > w_max) w_max = w_line;
            w_line = 0;
            h += BFONT_GLYPH_H;
        } else {
            w_line += BFONT_GLYPH_W;
        }
    }
    if (w_line > w_max) w_max = w_line;
    v.x = (float)w_max;
    v.y = (float)h;
    return v;
}

void bfont_draw_centered(const char *text, int cx, int y, Color c) {
    Vector2 m = bfont_measure(text);
    bfont_draw(text, cx - (int)m.x / 2, y, c);
}
