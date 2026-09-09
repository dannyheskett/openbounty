#include "bfont.h"
#include "assets.h"
#include "layout.h"
#include "resources.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static Texture2D g_font_tex;
static bool      g_ready = false;

// Measured off the strip at load: width / BFONT_GLYPHS. 8 for the original
// 1024x8 font, 32 for a strip authored at ui_scale 4. Zero until bfont_init
// runs, so the accessors fall back to 8 rather than divide by nothing.
static int g_src_w = 0;
static int g_src_h = 0;

int bfont_src_glyph_w(void) { return g_src_w > 0 ? g_src_w : 8; }
int bfont_src_glyph_h(void) { return g_src_h > 0 ? g_src_h : 8; }

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
        Rectangle src = { (float)(pairs[i].src_code * BFONT_SRC_GLYPH_W), 0.0f,
                          (float)BFONT_SRC_GLYPH_W, (float)BFONT_SRC_GLYPH_H };
        Rectangle dst = { (float)(pairs[i].dst_code * BFONT_SRC_GLYPH_W), 0.0f,
                          (float)BFONT_SRC_GLYPH_W, (float)BFONT_SRC_GLYPH_H };
        ImageDraw(img, *img, src, dst, WHITE);
    }
}

static bool bfont_init_strip(const char *png_path) {
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
    // Measure the glyph before patching -- the patch copies glyph-sized
    // rectangles around inside the strip and needs the size to do it.
    g_src_w = (img.width > 0) ? img.width / BFONT_GLYPHS : 0;
    g_src_h = img.height;
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

// ---- Two routes -------------------------------------------------------------
//
// Legacy, and any pack without a "font" block: the strip above, in the
// 8 * ui_scale cell, exactly as it always was. Modern with a "font" block:
// the proportional TrueType backend in text.c, at the declared size, on its
// own metrics. Every caller uses the bfont_* names; the switch is here.

static bool g_modern = false;

bool bfont_preload_metrics(const struct Resources *res) {
    g_modern = text_preload(res);
    return g_modern;
}

bool bfont_init(const struct Resources *res) {
    const Resources *r = (const Resources *)res;
    if (g_modern) {
        if (text_init()) { g_ready = true; return true; }
        fprintf(stdout, "bfont: TrueType route failed, using the strip\n");
        g_modern = false;
    }
    return bfont_init_strip(r ? r->sprites.font : NULL);
}

bool bfont_is_modern(void) { return g_modern; }

void bfont_set_zoom(int zoom) { text_set_zoom(zoom); }

void bfont_shutdown(void) {
    if (g_modern) text_shutdown();
    else if (g_ready) UnloadTexture(g_font_tex);
    g_ready = false;
}

bool bfont_ready(void) { return g_ready; }

// The on-screen glyph is 8 DESIGN UNITS times ui_scale -- deliberately not the
// source size. The layout is measured in 8px units throughout, so this has to
// stay put however the pack authors its strip; a higher-resolution source buys
// sharpness, not bigger text.
int bfont_glyph_w(void) { return g_modern ? text_digit_w() : 8 * g_layout.ui_scale; }
int bfont_glyph_h(void) { return g_modern ? text_line_h() : 8 * g_layout.ui_scale; }

int bfont_line_height(void) { return BFONT_GLYPH_H; }

void bfont_draw(const char *text, int x, int y, Color c) {
    if (!g_ready || !text) return;
    if (g_modern) { text_draw(text, x, y, c); return; }
    int cx = x;
    int cy = y;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            cx = x;
            cy += BFONT_GLYPH_H;
            continue;
        }
        unsigned char ch = (unsigned char)*p;
        if (ch >= BFONT_GLYPHS) ch = ' ';
        Rectangle src = { (float)(ch * BFONT_SRC_GLYPH_W), 0.0f,
                          (float)BFONT_SRC_GLYPH_W, (float)BFONT_SRC_GLYPH_H };
        Rectangle dst = { (float)cx, (float)cy,
                          (float)BFONT_GLYPH_W, (float)BFONT_GLYPH_H };
        DrawTexturePro(g_font_tex, src, dst, (Vector2){ 0, 0 }, 0.0f, c);
        cx += BFONT_GLYPH_W;
    }
}

Vector2 bfont_measure(const char *text) {
    Vector2 v = { 0.0f, (float)BFONT_GLYPH_H };
    if (!text) return v;
    if (g_modern) {
        int w_max = 0, h = BFONT_GLYPH_H;
        for (const char *p = text; *p; ) {
            int w = text_width(p);
            if (w > w_max) w_max = w;
            while (*p && *p != '\n') p++;
            if (*p == '\n') { p++; h += BFONT_GLYPH_H; }
        }
        v.x = (float)w_max; v.y = (float)h;
        return v;
    }
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

int bfont_text_width(const char *text) { return (int)bfont_measure(text).x; }

void bfont_draw_right(const char *text, int x_right, int y, Color c) {
    bfont_draw(text, x_right - bfont_text_width(text), y, c);
}

// The legacy wrap: at most max_chars characters per line, breaking at the
// last space, every '\n' a line break. This is the word-wrap the dialog and
// prompt panels carried as private copies; their behaviour is unchanged.
static int wrap_chars(const char **p, int max_chars, char *out, int out_sz) {
    int n = 0;
    while (**p == ' ' || **p == '\t') (*p)++;
    while (**p && **p != '\n' && n + 1 < out_sz && n < max_chars) {
        out[n++] = **p; (*p)++;
    }
    if (**p && **p != '\n' && n >= max_chars) {
        int back = n;
        while (back > 0 && out[back - 1] != ' ') back--;
        if (back > 0) {
            int over = n - back;
            *p -= over;
            n = back;
        }
    }
    out[n] = '\0';
    if (**p == '\n') (*p)++;
    return n + 1;
}

int bfont_take_line(const char **p, int max_w, char *out, int cap) {
    if (!p || !*p || !**p) { if (out && cap) out[0] = '\0'; return 0; }
    if (g_modern) return text_take_line(p, max_w, out, cap);
    int max_chars = max_w / BFONT_GLYPH_W;
    if (max_chars < 1) max_chars = 1;
    return wrap_chars(p, max_chars, out, cap);
}
