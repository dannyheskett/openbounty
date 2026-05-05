// Minimal PNG writer for RGBA8 images. Uses miniz for IDAT deflate
// and CRC32. Output is a valid PNG that any decoder accepts; we don't
// chase byte-for-byte parity with whatever zlib/level the existing
// pipeline used (the pre-existing assets are already byte-identical
// targets only at the decoded-pixel level).

#include "extract.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32be(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

// Write a PNG chunk: 4-byte length, 4-byte type, data, 4-byte CRC32 of
// (type + data).
static int write_chunk(FILE *f, const char *type,
                       const unsigned char *data, size_t len) {
    unsigned char hdr[8];
    put32be(hdr, (unsigned long)len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return -1;
    if (len > 0 && fwrite(data, 1, len, f) != len) return -1;
    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, (const unsigned char *)type, 4);
    if (len > 0) crc = mz_crc32(crc, data, len);
    unsigned char tail[4];
    put32be(tail, (unsigned long)crc);
    if (fwrite(tail, 1, 4, f) != 4) return -1;
    return 0;
}

int ex_write_png_rgba(const char *path, int w, int h, const uint8_t *rgba) {
    if (w <= 0 || h <= 0) return -1;

    // Make sure the parent directory exists (mirrors ex_write_file).
    const char *slash = strrchr(path, '/');
    if (slash) {
        char dir[1024];
        size_t dn = (size_t)(slash - path);
        if (dn >= sizeof dir) return -1;
        memcpy(dir, path, dn);
        dir[dn] = '\0';
        if (ex_mkdir_p(dir) != 0) return -1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "extract_png: cannot write %s\n", path);
        return -1;
    }

    // Signature.
    static const unsigned char SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (fwrite(SIG, 1, 8, f) != 8) { fclose(f); return -1; }

    // IHDR: width(4) height(4) bit_depth(1)=8 colour_type(1)=6(RGBA) compression(1)=0
    //       filter(1)=0 interlace(1)=0
    unsigned char ihdr[13];
    put32be(ihdr,     (unsigned long)w);
    put32be(ihdr + 4, (unsigned long)h);
    ihdr[8]  = 8;
    ihdr[9]  = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (write_chunk(f, "IHDR", ihdr, 13) != 0) { fclose(f); return -1; }

    // Build IDAT data: per-row filter byte (0 = None) + RGBA bytes.
    size_t row_bytes = (size_t)w * 4 + 1;
    size_t raw_size  = row_bytes * (size_t)h;
    unsigned char *raw = malloc(raw_size);
    if (!raw) { fclose(f); return -1; }
    for (int y = 0; y < h; y++) {
        unsigned char *row = raw + y * row_bytes;
        row[0] = 0;
        memcpy(row + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }

    mz_ulong z_cap = mz_compressBound(raw_size);
    unsigned char *zbuf = malloc(z_cap);
    if (!zbuf) { free(raw); fclose(f); return -1; }
    if (mz_compress2(zbuf, &z_cap, raw, raw_size,
                     MZ_DEFAULT_COMPRESSION) != MZ_OK) {
        fprintf(stderr, "extract_png: deflate failed\n");
        free(zbuf); free(raw); fclose(f); return -1;
    }
    free(raw);

    if (write_chunk(f, "IDAT", zbuf, (size_t)z_cap) != 0) {
        free(zbuf); fclose(f); return -1;
    }
    free(zbuf);

    if (write_chunk(f, "IEND", NULL, 0) != 0) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
