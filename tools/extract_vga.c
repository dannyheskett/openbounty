// VGA graphics-file parser + 8bpp renderer. Ported from
// legacy/scripts/extract_assets.py:57-165.
//
// Each `*.256` graphics file in the CC archive is a multi-image atlas:
//   u16 LE   image count
//   per-image directory (image_count × 4 bytes):
//       u16 LE   pixel-data offset
//       u16 LE   mask offset (0 = auto-derive, see below)
//   per-image header:
//       u16 LE   width
//       u16 LE   height
//       w*h bytes of 8bpp palette indices
//       (color-key byte either at mask_off or at end-of-pixels)
//
// The "mask" is a single palette index used as the transparent color
// key. When mask_off==0, the original DOS routine reads the byte right
// after the pixel data (offset + 4 + w*h). _detect_real_bg sanity-
// checks the claimed key against the actual border color of the
// sprite — DOS source data sometimes lies about the key. Border
// dominance ≥50% wins.
//
// The renderer expands the 6-bit palette to 8-bit via bit replication:
//   r8 = (r6 << 2) | (r6 >> 4)
// (Not r6*255/63; both upscalings are common but openbounty' golden
// PNGs use bit-replication — verifiable: 0x82, 0x92 etc. only appear
// in bit-rep output.)

#include "extract.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned read_u16_le(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

int ex_vga_frame_count(const uint8_t *data, size_t len) {
    if (len < 2) return 0;
    int count = (int)read_u16_le(data);
    if (count <= 0 || count > 1024) return 0;
    return count;
}

// Border dominance heuristic. If ≥50% of the 1-pixel border is one
// color AND that color differs from the claimed key, the claimed key
// is wrong (DOS source anomaly) — return the border color instead.
// Tie-break for "most common" is FIRST-SEEN wins, mirroring Python's
// Counter.most_common(1).
static int detect_real_bg(int w, int h, const uint8_t *pixels, int claimed_key) {
    int counts[256] = { 0 };
    int first_seen[256];
    for (int i = 0; i < 256; i++) first_seen[i] = INT32_MAX;
    int border_size = 0;
    int seq = 0;
    for (int x = 0; x < w; x++) {
        unsigned char p = pixels[x];                    // top row
        if (counts[p]++ == 0) first_seen[p] = seq;
        seq++; border_size++;
    }
    for (int x = 0; x < w; x++) {
        unsigned char p = pixels[(h - 1) * w + x];      // bottom row
        if (counts[p]++ == 0) first_seen[p] = seq;
        seq++; border_size++;
    }
    for (int y = 1; y < h - 1; y++) {
        unsigned char p = pixels[y * w];                // left col
        if (counts[p]++ == 0) first_seen[p] = seq;
        seq++; border_size++;
        p = pixels[y * w + (w - 1)];                    // right col
        if (counts[p]++ == 0) first_seen[p] = seq;
        seq++; border_size++;
    }
    if (border_size == 0) return claimed_key;

    // Find max count, tie-break by min first_seen.
    int best_idx = -1, best_count = 0, best_first = INT32_MAX;
    for (int i = 0; i < 256; i++) {
        if (counts[i] > best_count ||
            (counts[i] == best_count && first_seen[i] < best_first)) {
            best_count = counts[i];
            best_idx = i;
            best_first = first_seen[i];
        }
    }
    if (best_count * 100 / border_size >= 50 && best_idx != claimed_key) {
        return best_idx;
    }
    return claimed_key;
}

int ex_vga_render_frame(const uint8_t *data, size_t len,
                        int frame_idx,
                        const uint8_t *palette_768,
                        int apply_color_key,
                        uint8_t **out_rgba, int *out_w, int *out_h) {
    *out_rgba = NULL; *out_w = *out_h = 0;
    if (len < 2) return -1;
    int count = (int)read_u16_le(data);
    if (count <= 0 || frame_idx < 0 || frame_idx >= count) return -1;

    size_t dir_off = 2 + (size_t)frame_idx * 4;
    if (dir_off + 4 > len) return -1;
    unsigned off      = read_u16_le(data + dir_off);
    unsigned mask_off = read_u16_le(data + dir_off + 2);

    if (off + 4 > len) return -1;
    int w = (int)read_u16_le(data + off);
    int h = (int)read_u16_le(data + off + 2);
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return -1;

    size_t need = (size_t)w * h;
    if (off + 4 + need > len) return -1;
    const uint8_t *pixels = data + off + 4;

    int effective_key = -1;
    if (apply_color_key) {
        // Color key: mask_off if set, else byte right after the pixel data.
        int color_key = -1;
        if (mask_off != 0 && mask_off < len) {
            color_key = data[mask_off];
        } else {
            size_t auto_off = off + 4 + need;
            if (auto_off < len) color_key = data[auto_off];
        }
        effective_key = (color_key >= 0)
            ? detect_real_bg(w, h, pixels, color_key)
            : -1;
    }

    uint8_t *rgba = malloc(need * 4);
    if (!rgba) return -1;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = pixels[y * w + x];
            size_t o = ((size_t)y * w + x) * 4;
            if (effective_key >= 0 && idx == effective_key) {
                rgba[o + 0] = 0; rgba[o + 1] = 0; rgba[o + 2] = 0; rgba[o + 3] = 0;
            } else {
                // Palette is 6-bit per channel; expand via bit replication.
                unsigned r6 = palette_768[idx * 3 + 0] & 0x3Fu;
                unsigned g6 = palette_768[idx * 3 + 1] & 0x3Fu;
                unsigned b6 = palette_768[idx * 3 + 2] & 0x3Fu;
                rgba[o + 0] = (uint8_t)((r6 << 2) | (r6 >> 4));
                rgba[o + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
                rgba[o + 2] = (uint8_t)((b6 << 2) | (b6 >> 4));
                rgba[o + 3] = 0xFF;
            }
        }
    }

    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 0;
}
