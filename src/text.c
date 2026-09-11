#include "text.h"
#include "assets_bytes.h"
#include "resources.h"
#include "layout.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T_FIRST 32
#define T_ASCII 95                       // printable ASCII 32..126
#define T_COUNT (T_ASCII + 4)            // plus the four arrows the legacy control codes name
static const int T_ARROWS[4] = { 0x2193, 0x2191, 0x2192, 0x2190 };   // 0x18 down, 0x19 up, 0x1A right, 0x1B left

static const unsigned char *s_bytes = NULL;
static size_t s_size = 0;
static int    s_px = 0;                  // declared size, design pixels
static int    s_caps = 0;
static char   s_name[256];

// Metrics at zoom 1, design pixels. Every glyph advances by ONE fixed
// width, the widest advance the face has over the printable set: the
// screens lay columns out by character count (menus, tables, the controls
// list), so a proportional advance breaks their alignment. A monospaced
// face gives its own advance; a proportional one gets letter-spaced to its
// widest glyph, which is why packs should declare a monospaced face.
// line_h is the line's ink height plus lead (offsets are line-top relative).
static int    s_adv[T_COUNT];            // all equal: the fixed advance
static int    s_ink_w[T_COUNT];          // each glyph's own ink width, to centre it in the cell
static int    s_ink_x[T_COUNT];          // its left bearing
static int    s_line_h = 0;
static int    s_digit_w = 0;

static Font   s_font;                    // atlas at s_zoom
static bool   s_ready = false;
static int    s_zoom = 1;
static int    s_want_zoom = 1;

static int codepoint(unsigned char ch) {
    switch (ch) {                        // the twirl control codes
        case 0x1D: return '|';
        case 0x05: return '/';
        case 0x1F: return '-';
        case 0x1C: return '\\';
        default: break;
    }
    if (ch >= 0x18 && ch <= 0x1B) return T_FIRST + T_ASCII + (ch - 0x18);   // an arrow slot
    if (s_caps) ch = (unsigned char)toupper(ch);
    if (ch < T_FIRST || ch >= T_FIRST + T_ASCII) return ' ';
    return ch;
}

static GlyphInfo *load_glyphs(int px, int *count) {
    int cps[T_COUNT];
    for (int i = 0; i < T_ASCII; i++) cps[i] = T_FIRST + i;
    for (int i = 0; i < 4; i++) cps[T_ASCII + i] = T_ARROWS[i];
    *count = 0;
    GlyphInfo *g = LoadFontData(s_bytes, (int)s_size, px, cps, T_COUNT, FONT_DEFAULT, count);
    if (g && *count != T_COUNT) { UnloadFontData(g, *count); g = NULL; }
    return g;
}

static unsigned char *s_owned = NULL;    // bytes read from a file (tests, tools)

static bool preload_metrics(const char *name, int size, int caps) {
    s_px = (size > 0) ? size : 16;
    s_caps = caps;
    snprintf(s_name, sizeof s_name, "%s", name);
    int count = 0;
    GlyphInfo *g = load_glyphs(s_px, &count);
    if (!g) { s_bytes = NULL; return false; }
    // Line height: the deepest ink bottom over the printable set is the
    // descent the face actually uses at this size; the line is that plus a
    // little lead. offsetY is measured from the line top, so the tallest
    // glyph's bottom is the line's ink height.
    int deepest = 0, widest = 0;
    for (int i = 0; i < count; i++) {
        // the cell is the widest ADVANCE; a glyph whose ink overhangs its
        // advance by a pixel or two (a wide W in some monos) simply overhangs
        int a = g[i].advanceX > 0 ? g[i].advanceX : g[i].image.width;
        if (a > widest) widest = a;
        s_ink_w[i] = g[i].image.width;
        s_ink_x[i] = g[i].offsetX;
        int b = g[i].offsetY + g[i].image.height;
        if (b > deepest) deepest = b;
    }
    for (int i = 0; i < count; i++) s_adv[i] = widest;
    s_line_h = (deepest > s_px) ? deepest : s_px;
    s_line_h += (s_px + 7) / 8;          // lead: an eighth of the size
    s_digit_w = widest;
    UnloadFontData(g, count);
    (void)name;
    return true;
}

bool text_preload(const struct Resources *res) {
    const Resources *r = (const Resources *)res;
    s_line_h = s_digit_w = 0;
    s_bytes = NULL;
    if (!r || !r->font.file[0]) return false;
    s_bytes = LoadAssetBytes(r->font.file, &s_size);
    if (!s_bytes || !s_size) { s_bytes = NULL; return false; }
    return preload_metrics(r->font.file, r->font.size, r->font.caps);
}

bool text_preload_file(const char *path, int size, int caps) {
    s_line_h = s_digit_w = 0;
    s_bytes = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    free(s_owned);
    s_owned = malloc((size_t)n);
    if (!s_owned || fread(s_owned, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(s_owned); s_owned = NULL; return false; }
    fclose(f);
    s_bytes = s_owned; s_size = (size_t)n;
    return preload_metrics(path, size, caps);
}

static bool build(int zoom) {
    if (!s_bytes) return false;
    int count = 0;
    GlyphInfo *gl = load_glyphs(s_px * zoom, &count);
    if (!gl) return false;
    Font f = { 0 };
    f.baseSize = s_px * zoom;
    f.glyphCount = count;
    f.glyphPadding = 2;
    f.glyphs = gl;
    Image atlas = GenImageFontAtlas(gl, &f.recs, count, f.baseSize, f.glyphPadding, 0);
    f.texture = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    if (f.texture.id == 0) { UnloadFontData(gl, count); return false; }
    SetTextureFilter(f.texture, TEXTURE_FILTER_POINT);
    if (s_ready) UnloadFont(s_font);
    s_font = f;
    s_ready = true;
    s_zoom = zoom;
    return true;
}

bool text_init(void) {
    if (!s_bytes) return false;
    return build(s_want_zoom);
}

void text_shutdown(void) {
    if (s_ready) UnloadFont(s_font);
    s_ready = false;
    free(s_owned); s_owned = NULL;
}

bool text_ready(void) { return s_ready; }

void text_set_zoom(int zoom) {
    if (zoom < 1) zoom = 1;
    s_want_zoom = zoom;
    if (s_ready && zoom != s_zoom) build(zoom);
}

int text_line_h(void)  { return s_line_h > 0 ? s_line_h : 8; }
int text_digit_w(void) { return s_digit_w > 0 ? s_digit_w : 8; }

int text_width(const char *s) {
    if (!s) return 0;
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && *p != '\n'; p++)
        w += s_adv[codepoint(*p) - T_FIRST];
    return w;
}

void text_draw(const char *s, int x, int y, Color c) {
    if (!s_ready || !s) return;
    int cx = x, cy = y;
    const float z = (float)s_zoom;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '\n') { cx = x; cy += text_line_h(); continue; }
        int gi = codepoint(*p) - T_FIRST;
        const GlyphInfo *g = &s_font.glyphs[gi];
        if (g->image.width > 0 && g->image.height > 0) {
            Rectangle src = s_font.recs[gi];
            // centred in the fixed cell: a narrow glyph sits in the middle
            float w = src.width / z;
            float dx = (float)cx + ((float)s_adv[gi] - w) / 2.0f;
            Rectangle dst = { dx, (float)cy + (float)g->offsetY / z, w, src.height / z };
            DrawTexturePro(s_font.texture, src, dst, (Vector2){ 0, 0 }, 0.0f, c);
        }
        cx += s_adv[gi];
    }
}

int text_take_line(const char **p, int max_w, char *out, int cap) {
    const char *s = *p;
    int n = 0, w = 0;
    int last_space = -1, w_at_space = 0;
    const char *src_at_space = NULL;
    while (*s == ' ' || *s == '\t') s++;
    while (*s && *s != '\n' && n + 1 < cap) {
        unsigned char ch = (unsigned char)*s;
        int adv = s_adv[codepoint(ch) - T_FIRST];
        if (w + adv > max_w && n > 0) {
            if (ch != ' ' && last_space >= 0) {   // mid-word: back up to the last space
                n = last_space;
                s = src_at_space;
                w = w_at_space;
            }
            break;                                 // on a space: the line ends here
        }
        if (ch == ' ') { last_space = n; src_at_space = s; w_at_space = w; }
        out[n++] = (char)ch;
        w += adv;
        s++;
    }
    while (n > 0 && out[n - 1] == ' ') n--;       // no trailing space on a line
    while (*s == ' ') s++;                         // nor a leading one on the next
    if (*s == '\n') s++;                          // a newline is a line break, as authored
    out[n] = '\0';
    int consumed = (int)(s - *p);
    *p = s;
    return consumed;
}
