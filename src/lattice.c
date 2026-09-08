#include "lattice.h"
#include "layout.h"
#include "raylib.h"

// Colours are the lattice's own, not the pack palette: a modern pack's
// palette is whatever its font strip needs, and the chrome has to read as one
// object whatever the pack chose.
static const Color C_WOOD   = {  46,  30,  16, 255 };   // dark walnut ground
static const Color C_GOLD   = { 206, 160,  42, 255 };   // the lattice strands
static const Color C_BRIGHT = { 250, 222, 104, 255 };   // where strands cross
static const Color C_SHADE  = { 118,  82,  22, 255 };   // the strand's shadow
static const Color C_RAIL   = { 232, 186,  60, 255 };   // edge rails
static const Color C_INK    = {  14,  10,   6, 255 };   // line inside the rail

static Texture2D s_tex;    // one repeat of the pattern
static int       s_unit;   // the ui_scale it was built at

// One repeat of the pattern in unit cells: strand A runs down-right on the
// main diagonal, strand B down-left, so they cross at (0,0) and (4,4) of an
// 8-cell repeat. Each strand carries a one-cell shadow below it, which is
// what makes it read as woven rather than drawn.
static Color cell_colour(int ux, int uy) {
    const int P = LATTICE_PITCH;
    int a  = ((ux - uy) % P + P) % P;   // 0 on strand A
    int b  = (ux + uy) % P;             // 0 on strand B
    if (a == 0 && b == 0) return C_BRIGHT;
    if (a == 0 || b == 0) return C_GOLD;
    if (a == 1 || b == 1) return C_SHADE;
    return C_WOOD;
}

static void build(void) {
    int u = CL_UI;
    if (u < 1) u = 1;
    if (s_tex.id && s_unit == u) return;
    if (s_tex.id) UnloadTexture(s_tex);
    const int P = LATTICE_PITCH;
    Image img = GenImageColor(P * u, P * u, C_WOOD);
    for (int uy = 0; uy < P; uy++)
        for (int ux = 0; ux < P; ux++)
            ImageDrawRectangle(&img, ux * u, uy * u, u, u, cell_colour(ux, uy));
    s_tex = LoadTextureFromImage(img);
    SetTextureFilter(s_tex, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    s_unit = u;
}

// Tile the pattern over the rect, aligned to the screen origin so every band
// on the screen shares one grid. Partial tiles are cropped in the source
// rather than clipped with a scissor, so this works inside any render target.
static void tile(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    build();
    const int P = s_tex.width;
    for (int ty = y - ((y % P) + P) % P; ty < y + h; ty += P) {
        for (int tx = x - ((x % P) + P) % P; tx < x + w; tx += P) {
            int sx = (tx < x) ? x - tx : 0;
            int sy = (ty < y) ? y - ty : 0;
            int ex = (tx + P > x + w) ? (x + w) - tx : P;
            int ey = (ty + P > y + h) ? (y + h) - ty : P;
            if (ex <= sx || ey <= sy) continue;
            Rectangle src = { (float)sx, (float)sy, (float)(ex - sx), (float)(ey - sy) };
            Rectangle dst = { (float)(tx + sx), (float)(ty + sy), src.width, src.height };
            DrawTexturePro(s_tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }
}

// A rail round the inside of the rect: one unit of gold at the edge, one unit
// of ink inside it.
static void rail(int x, int y, int w, int h) {
    int u = CL_UI;
    if (w < 2 * u || h < 2 * u) { DrawRectangle(x, y, w, h, C_RAIL); return; }
    DrawRectangle(x, y, w, u, C_RAIL);
    DrawRectangle(x, y + h - u, w, u, C_RAIL);
    DrawRectangle(x, y, u, h, C_RAIL);
    DrawRectangle(x + w - u, y, u, h, C_RAIL);
    if (w < 4 * u || h < 4 * u) return;
    DrawRectangle(x + u, y + u, w - 2 * u, u, C_INK);
    DrawRectangle(x + u, y + h - 2 * u, w - 2 * u, u, C_INK);
    DrawRectangle(x + u, y + u, u, h - 2 * u, C_INK);
    DrawRectangle(x + w - 2 * u, y + u, u, h - 2 * u, C_INK);
}

void lattice_fill(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    tile(x, y, w, h);
    rail(x, y, w, h);
}

void lattice_ring(int x, int y, int w, int h, int l, int r, int t, int b) {
    if (w <= 0 || h <= 0) return;
    if (l + r >= w || t + b >= h) { lattice_fill(x, y, w, h); return; }
    tile(x,         y,         w, t);
    tile(x,         y + h - b, w, b);
    tile(x,         y + t,     l, h - t - b);
    tile(x + w - r, y + t,     r, h - t - b);
    int u = CL_UI;
    int thin = (l < 6 * u || r < 6 * u || t < 6 * u || b < 6 * u);
    // Outer rail: gold at the edge, and ink inside it when the band has room.
    DrawRectangle(x, y, w, u, C_RAIL);
    DrawRectangle(x, y + h - u, w, u, C_RAIL);
    DrawRectangle(x, y, u, h, C_RAIL);
    DrawRectangle(x + w - u, y, u, h, C_RAIL);
    if (!thin) {
        DrawRectangle(x + u, y + u, w - 2 * u, u, C_INK);
        DrawRectangle(x + u, y + h - 2 * u, w - 2 * u, u, C_INK);
        DrawRectangle(x + u, y + u, u, h - 2 * u, C_INK);
        DrawRectangle(x + w - 2 * u, y + u, u, h - 2 * u, C_INK);
    }
    // Inner rail: gold against the content, ink against the pattern.
    int ix = x + l, iy = y + t, iw = w - l - r, ih = h - t - b;
    if (!thin) {
        DrawRectangle(ix - 2 * u, iy - 2 * u, iw + 4 * u, u, C_INK);
        DrawRectangle(ix - 2 * u, iy + ih + u, iw + 4 * u, u, C_INK);
        DrawRectangle(ix - 2 * u, iy - 2 * u, u, ih + 4 * u, C_INK);
        DrawRectangle(ix + iw + u, iy - 2 * u, u, ih + 4 * u, C_INK);
    }
    DrawRectangle(ix - u, iy - u, iw + 2 * u, u, C_RAIL);
    DrawRectangle(ix - u, iy + ih, iw + 2 * u, u, C_RAIL);
    DrawRectangle(ix - u, iy - u, u, ih + 2 * u, C_RAIL);
    DrawRectangle(ix + iw, iy - u, u, ih + 2 * u, C_RAIL);
}

void lattice_shutdown(void) {
    if (s_tex.id) UnloadTexture(s_tex);
    s_tex = (Texture2D){ 0 };
    s_unit = 0;
}
