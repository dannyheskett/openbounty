// Cleanroom procedural chrome_overworld.png generator.
//
// Background: openbounty ships a 320x200 chrome frame (yellow border
// with rope-twist motif, speckled gray trim, transparent interior) that
// the engine overlays on the overworld view. The shipped PNG is a
// screenshot composite from a long-ago port pass — not extractable
// from KB.EXE/256.CC and ownership is murky. This generator produces
// a frame with the same visual character using only:
//
//   - the VGA palette (MCGA.DRV @ 0x032D, expanded 6→8)
//   - a deterministic LCG for speckle
//   - hand-tuned but original geometric recipes
//
// No pixels from the original PNG are referenced. The output is similar
// in spirit (yellow rope band between two dark ridges, speckled trim,
// rounded corner ornament) but does not match byte-for-byte.

#define _POSIX_C_SOURCE 200809L
#include "extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 200

// Pull RGB out of the 6-bit palette and bit-replicate to 8.
static inline void pal_rgb(const uint8_t *pal, int idx, uint8_t out[3]) {
    unsigned r6 = pal[idx*3 + 0] & 0x3Fu;
    unsigned g6 = pal[idx*3 + 1] & 0x3Fu;
    unsigned b6 = pal[idx*3 + 2] & 0x3Fu;
    out[0] = (uint8_t)((r6 << 2) | (r6 >> 4));
    out[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    out[2] = (uint8_t)((b6 << 2) | (b6 >> 4));
}

static inline void put(uint8_t *rgba, int x, int y,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    int o = (y * W + x) * 4;
    rgba[o+0] = r; rgba[o+1] = g; rgba[o+2] = b; rgba[o+3] = a;
}

// Deterministic LCG (numerical recipes). Same input → same noise → same
// PNG byte-for-byte across runs. Seed picked so the speckle pattern is
// pleasing; no significance.
static uint32_t lcg(uint32_t *s) {
    *s = (*s) * 1664525u + 1013904223u;
    return *s;
}

// The original (port-shipped) chrome has asymmetric thickness: 16px
// on left/right but only 8px on top/bottom. The engine's HUD layout
// expects that exact transparent window (anything thicker eats game
// content). Scale the conceptual 0..14 distance band into the actual
// horizontal/vertical thickness.
#define BORDER_X 16
#define BORDER_Y 8

// Returns a "logical" border distance 0..(BORDER_THICK-1), or
// BORDER_THICK if we're inside the transparent interior. Computed
// by taking the smaller of the X-axis and Y-axis fractional depths
// and mapping back to the 0..14 band the pixel recipe expects.
static int border_dist(int x, int y) {
    int dx = x < W - 1 - x ? x : W - 1 - x;
    int dy = y < H - 1 - y ? y : H - 1 - y;
    if (dx >= BORDER_X && dy >= BORDER_Y) return 1000; // interior
    // Normalize each axis depth to a 0..14 scale, then take the deeper.
    int lx = (dx * 15) / BORDER_X;
    int ly = (dy * 15) / BORDER_Y;
    if (lx > 14) lx = 14;
    if (ly > 14) ly = 14;
    return lx < ly ? lx : ly;
}

// The chrome frame is 15 pixels thick on each side. Layout (from outer
// edge inward, by border_dist):
//   d=0..1   outer speckled trim (gray noise + occasional gold speck)
//   d=2..4   inner speckled trim (slightly brighter gray)
//   d=5      black/dark-gold ridge line (palette idx 13 in original,
//            here a dark ochre)
//   d=6..9   yellow rope band (bright yellow with cross-hatch twist)
//   d=10     second ridge line
//   d=11..14 inner trim (lighter speckle, fading to transparent)
//   d>=15    transparent
//
// Corners: the rope band rounds off via a Manhattan-distance check —
// pixels whose chebyshev corner distance is <5 get treated as outer
// trim instead of rope, giving the slightly softened corner look.

#define BORDER_THICK 15  // logical thickness band (axes scale into this)

// MCGA.DRV palette layout (discovered by scanning the palette for
// grayscale runs and yellow hues):
//   idx 17..30  : grayscale ramp, bright (idx 17 = #ef) → dark (idx 30 = #2c)
//   idx 69..73  : yellow ramp,    bright (idx 69 = #ff f7 41) → dark (#cf c7 00)
//   idx  0      : black
// We pick indices from these ranges so the chrome stays inside the
// game's color world without any custom palette table.

#define PAL_BLACK       0
#define PAL_GRAY_BRT1  18  // df df df
#define PAL_GRAY_BRT2  19  // d3 d3 d3
#define PAL_GRAY_MID1  20  // c3 c3 c3
#define PAL_GRAY_MID2  21  // b2 b2 b2
#define PAL_GRAY_DRK1  23  // 96
#define PAL_GRAY_DRK2  25  // 79
#define PAL_GRAY_DRK3  27  // 59
#define PAL_YEL_BRT    69  // ff f7 41
#define PAL_YEL_MID1   70  // ff f7 20
#define PAL_YEL_MID2   71  // ff f7 00
#define PAL_YEL_DRK1   72  // e7 db 00
#define PAL_YEL_DRK2   73  // cf c7 00

// Generate one pixel given its border distance and a noise sample.
// Returns palette index (or -1 for transparent).
static int pixel_palette_idx(int x, int y, int d, uint32_t *rng,
                             int corner_softness) {
    if (d >= BORDER_THICK) return -1; // transparent interior

    uint32_t n = lcg(rng);

    // Corner softening: within `corner_softness` pixels of a corner,
    // replace the rope band with mid trim so the corner doesn't jut.
    int near_corner = (x < corner_softness || x >= W - corner_softness) &&
                      (y < corner_softness || y >= H - corner_softness);

    if (d <= 1) {
        // Outer speckle: mostly mid gray, occasional yellow speck for
        // worn-metal feel.
        int r = (int)(n & 0x3F);
        if (r < 3)  return PAL_YEL_DRK1;
        if (r < 6)  return PAL_YEL_DRK2;
        if (r < 30) return PAL_GRAY_MID1;
        return PAL_GRAY_MID2;
    }
    if (d <= 4) {
        // Inner trim: slightly cleaner gray with sparse darker dots.
        int r = (int)(n & 0x1F);
        if (r < 2)  return PAL_GRAY_DRK1;
        if (r < 4)  return PAL_GRAY_MID2;
        if (r < 18) return PAL_GRAY_MID1;
        return PAL_GRAY_BRT1;
    }
    if (d == 5 || d == 10) {
        // Dark ridge line bordering the rope band.
        return ((n & 0xF) == 0) ? PAL_YEL_DRK2 : PAL_BLACK;
    }
    if (d >= 6 && d <= 9) {
        // Yellow rope band. (x+y) parity + perpendicular offset fakes
        // the rope twist. Outer rope rows brighter; inner rows pick up
        // dark accents to suggest shadow on the underside.
        if (near_corner) return PAL_GRAY_MID1;
        int twist = ((x + y) >> 1) & 3;
        int rb = border_dist(x, y);
        if (rb == 6 || rb == 9) {
            if (twist == 0 && (n & 0x7) == 0) return PAL_YEL_MID2;
            return PAL_YEL_BRT;
        }
        switch (twist) {
            case 0: return PAL_YEL_BRT;
            case 1: return PAL_YEL_MID1;
            case 2: return PAL_YEL_DRK1;
            case 3: return PAL_YEL_MID2;
        }
    }
    if (d >= 11 && d <= 13) {
        // Inner trim fading toward transparency. Indices walk darker as
        // d increases; sparse brighter pixels add texture.
        int r = (int)(n & 0x7);
        if (d == 11) {
            if (r == 0) return PAL_GRAY_BRT1;
            return PAL_GRAY_MID1;
        }
        if (d == 12) {
            if (r == 0) return PAL_GRAY_MID1;
            if (r == 1) return PAL_GRAY_DRK1;
            return PAL_GRAY_MID2;
        }
        if (r == 0) return PAL_GRAY_DRK2;
        return PAL_GRAY_DRK1;
    }
    if (d == 14) {
        int r = (int)(n & 0x7);
        if (r == 0) return PAL_GRAY_DRK3;
        return PAL_GRAY_DRK2;
    }
    return -1;
}

// Build a single chrome PNG at out_path.
static int build_chrome(const uint8_t *pal, const char *out_path) {
    uint8_t *rgba = (uint8_t *)calloc((size_t)W * H * 4, 1);
    if (!rgba) return -1;

    uint32_t rng = 0xC0FFEEu;
    const int corner_softness = 5;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int d = border_dist(x, y);
            int idx = pixel_palette_idx(x, y, d, &rng, corner_softness);
            if (idx < 0) {
                // Transparent interior.
                put(rgba, x, y, 0, 0, 0, 0);
                continue;
            }
            uint8_t rgb[3];
            pal_rgb(pal, idx, rgb);
            put(rgba, x, y, rgb[0], rgb[1], rgb[2], 255);
        }
    }

    // Inner stitch: bright-yellow dashes just outside the transparent
    // window. Asymmetric — uses BORDER_X and BORDER_Y because the
    // transparent area is wider than it is tall.
    uint8_t yel[3]; pal_rgb(pal, PAL_YEL_BRT, yel);
    for (int x = BORDER_X; x < W - BORDER_X; x++) {
        if ((x & 7) == 0) {
            put(rgba, x, BORDER_Y - 1,  yel[0], yel[1], yel[2], 255);
            put(rgba, x, H - BORDER_Y,  yel[0], yel[1], yel[2], 255);
        }
    }
    for (int y = BORDER_Y; y < H - BORDER_Y; y++) {
        if ((y & 7) == 0) {
            put(rgba, BORDER_X - 1, y,  yel[0], yel[1], yel[2], 255);
            put(rgba, W - BORDER_X, y,  yel[0], yel[1], yel[2], 255);
        }
    }

    int rc = ex_write_png_rgba(out_path, W, H, rgba);
    free(rgba);
    return rc;
}

// Public entry: hooks into the synth stage from extract.c.
int ex_emit_chrome(const CcArchive *cc, const char *out_dir) {
    const CcEntry *mcga = ex_cc_find(cc, "MCGA.DRV");
    if (!mcga) {
        fprintf(stderr, "extract: MCGA.DRV missing for chrome synth\n");
        return -1;
    }
    const uint8_t *pal = mcga->data + 0x032D;
    char path[512];
    snprintf(path, sizeof path, "%s/art/ui/chrome_overworld.png", out_dir);
    if (build_chrome(pal, path) != 0) return -1;
    fprintf(stderr, "extract: wrote chrome_overworld (320x200 synthesized)\n");
    return 0;
}
