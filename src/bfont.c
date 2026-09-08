#include "bfont.h"
#include "assets.h"
#include "layout.h"
#include "resources.h"
#include <ctype.h>
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

// ---- TrueType route ----------------------------------------------------------

#define TT_FIRST 32
#define TT_COUNT 95                      // printable ASCII 32..126

static Font  g_tt;                       // atlas + glyph info, when g_tt_ready
static bool  g_tt_ready = false;
static int   g_tt_adv = 0;               // advance in DESIGN pixels
static int   g_tt_px = 0;                // fitted pixel size, at zoom 1
static int   g_tt_top = 0;               // topmost ink over all glyphs (line-top relative), at the built zoom
static int   g_tt_ink_h = 0;             // ink span top..bottom, at the built zoom
static int   g_tt_zoom = 1;              // zoom the atlas was built at
static int   g_want_zoom = 1;
static int   g_caps = 0;
static const unsigned char *g_tt_bytes = NULL;
static size_t g_tt_size = 0;
static char  g_tt_name[256];
static int   g_tt_req = 0;               // requested size, 0 = largest that fits

bool bfont_fits(const int *w, const int *top, const int *bottom, int n,
                int cell_w, int cell_h) {
    int lo = 1 << 30, hi = -(1 << 30), mw = 0;
    for (int i = 0; i < n; i++) {
        if (w[i] <= 0) continue;         // space
        if (w[i] > mw) mw = w[i];
        if (top[i] < lo) lo = top[i];
        if (bottom[i] > hi) hi = bottom[i];
    }
    if (mw == 0) return false;
    return mw <= cell_w && (hi - lo) <= cell_h;
}

int bfont_advance_for(int max_ink_w, int cell_w) {
    int a = max_ink_w + 1;
    return (a > cell_w) ? cell_w : (a < 1 ? 1 : a);
}

// Measure the glyph images stb_truetype produced: each is a tight grayscale
// bitmap, so its size is the ink box and offsetY is where it sits below the
// line top.
static void measure(const GlyphInfo *g, int n, int *w, int *top, int *bottom) {
    for (int i = 0; i < n; i++) {
        w[i] = g[i].image.width;
        top[i] = g[i].offsetY;
        bottom[i] = g[i].offsetY + g[i].image.height;
    }
}

static void tt_unload(void) {
    if (g_tt_ready) UnloadFont(g_tt);
    g_tt_ready = false;
}

// Build the atlas at `zoom`: fit the size to the zoomed cell, then rasterise.
static bool tt_build(int zoom) {
    int cell_w = 8 * g_layout.ui_scale * zoom;
    int cell_h = 8 * g_layout.ui_scale * zoom;
    int codepoints[TT_COUNT];
    for (int i = 0; i < TT_COUNT; i++) codepoints[i] = TT_FIRST + i;
    // At a higher zoom start from the zoom-1 fit scaled up, so the face keeps
    // the same size and the same advance at every zoom (only sharper).
    int start = (zoom > 1 && g_tt_px > 0) ? g_tt_px * zoom
              : (g_tt_req > 0) ? g_tt_req * zoom : 64 * zoom;
    for (int px = start; px >= 4; px--) {
        int count = 0;
        GlyphInfo *gl = LoadFontData(g_tt_bytes, (int)g_tt_size, px, codepoints, TT_COUNT, FONT_DEFAULT, &count);
        if (!gl || count != TT_COUNT) { if (gl) UnloadFontData(gl, count); return false; }
        int w[TT_COUNT], top[TT_COUNT], bottom[TT_COUNT];
        measure(gl, count, w, top, bottom);
        if (!bfont_fits(w, top, bottom, count, cell_w, cell_h)) { UnloadFontData(gl, count); continue; }
        int lo = 1 << 30, hi = -(1 << 30), mw = 0;
        for (int i = 0; i < count; i++) {
            if (w[i] <= 0) continue;
            if (w[i] > mw) mw = w[i];
            if (top[i] < lo) lo = top[i];
            if (bottom[i] > hi) hi = bottom[i];
        }
        Font f = { 0 };
        f.baseSize = px;
        f.glyphCount = count;
        f.glyphPadding = 2;
        f.glyphs = gl;
        Image atlas = GenImageFontAtlas(gl, &f.recs, count, px, f.glyphPadding, 0);
        f.texture = LoadTextureFromImage(atlas);
        UnloadImage(atlas);
        // the glyph images stay attached to the Font (UnloadFont frees them)
        if (f.texture.id == 0) { UnloadFontData(gl, count); return false; }
        SetTextureFilter(f.texture, TEXTURE_FILTER_POINT);
        tt_unload();
        g_tt = f;
        g_tt_ready = true;
        g_tt_zoom = zoom;
        g_tt_top = lo;
        g_tt_ink_h = hi - lo;
        if (zoom == 1 || g_tt_adv == 0)
            g_tt_adv = bfont_advance_for((mw + zoom - 1) / zoom, 8 * g_layout.ui_scale);
        if (zoom == 1) g_tt_px = px;
        return true;
    }
    return false;
}

bool bfont_init(const struct Resources *res) {
    const Resources *r = (const Resources *)res;
    g_ready = false;
    g_caps = 0;
    if (r && r->font.file[0]) {
        g_tt_bytes = LoadAssetBytes(r->font.file, &g_tt_size);
        g_tt_req = r->font.size;
        g_caps = r->font.caps;
        snprintf(g_tt_name, sizeof g_tt_name, "%s", r->font.file);
        if (g_tt_bytes && g_tt_size && tt_build(g_want_zoom)) {
            fprintf(stdout, "bfont: %s fitted at %dpx, advance %d in a %d cell%s\n",
                    r->font.file, g_tt_px, g_tt_adv, 8 * g_layout.ui_scale,
                    g_caps ? ", caps" : "");
            g_ready = true;
            return true;
        }
        fprintf(stdout, "bfont: could not use %s, falling back to the strip\n", r->font.file);
        g_tt_bytes = NULL;
    }
    return bfont_init_strip(r ? r->sprites.font : NULL);
}

void bfont_set_zoom(int zoom) {
    if (zoom < 1) zoom = 1;
    g_want_zoom = zoom;
    if (g_tt_ready && g_tt_bytes && zoom != g_tt_zoom) tt_build(zoom);
}

void bfont_shutdown(void) {
    if (g_ready && !g_tt_ready) UnloadTexture(g_font_tex);
    tt_unload();
    g_ready = false;
}

bool bfont_ready(void) { return g_ready; }

// The on-screen glyph is 8 DESIGN UNITS times ui_scale -- deliberately not the
// source size. The layout is measured in 8px units throughout, so this has to
// stay put however the pack authors its strip; a higher-resolution source buys
// sharpness, not bigger text.
int bfont_glyph_w(void) { return (g_tt_ready && g_tt_adv > 0) ? g_tt_adv : 8 * g_layout.ui_scale; }
int bfont_glyph_h(void) { return 8 * g_layout.ui_scale; }

int bfont_line_height(void) { return BFONT_GLYPH_H; }

// The strip patches the twirl control codes into its texture; the TrueType
// route maps them at draw time instead.
static int tt_codepoint(unsigned char ch) {
    switch (ch) {
        case 0x1D: return '|';
        case 0x05: return '/';
        case 0x1F: return '-';
        case 0x1C: return '\\';
        default: break;
    }
    if (g_caps) ch = (unsigned char)toupper(ch);
    if (ch < TT_FIRST || ch >= TT_FIRST + TT_COUNT) return ' ';
    return ch;
}

static void tt_draw(const char *text, int x, int y, Color c) {
    int cx = x, cy = y;
    const int cell_h = BFONT_GLYPH_H;
    const float z = (float)g_tt_zoom;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') { cx = x; cy += cell_h; continue; }
        int cp = tt_codepoint((unsigned char)*p);
        int gi = cp - TT_FIRST;
        const GlyphInfo *g = &g_tt.glyphs[gi];
        if (g->image.width > 0 && g->image.height > 0) {
            Rectangle src = g_tt.recs[gi];
            // ink centred in the advance; every glyph on one baseline: the
            // ink span of the whole face is centred in the cell
            float w = src.width / z, h = src.height / z;
            float dx = (float)cx + ((float)g_tt_adv - w) / 2.0f;
            float dy = (float)cy + ((float)cell_h - (float)g_tt_ink_h / z) / 2.0f
                     + (float)(g->offsetY - g_tt_top) / z;
            Rectangle dst = { dx, dy, w, h };
            DrawTexturePro(g_tt.texture, src, dst, (Vector2){ 0, 0 }, 0.0f, c);
        }
        cx += g_tt_adv;
    }
}

void bfont_draw(const char *text, int x, int y, Color c) {
    if (!g_ready || !text) return;
    if (g_tt_ready) { tt_draw(text, x, y, c); return; }
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
