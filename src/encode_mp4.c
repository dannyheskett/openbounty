// H.264 (baseline) + MP4 encoder for openbounty recordings.
// Reads <src_dir>/manifest.ndjson + tick_*.png, emits <out_path>.
//
// minih264 requires width and height to be multiples of 16. Our
// framebuffer is 320x200, so we encode at 320x208 with the bottom 8
// rows of Y/UV padded with limited-range black (Y=16, U=V=128).
// Players show 8 black rows below the gameplay area. Acceptable.

#define _POSIX_C_SOURCE 200809L

#include "encode_mp4.h"
#include "cJSON.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

// minih264 + minimp4 each define internal types/functions with the
// same names (e.g. bs_t, nal_put_esc). Including both
// IMPLEMENTATIONs in one TU collides. Each is compiled in its own
// dedicated TU (encode_mp4_h264.c, encode_mp4_mux.c). Here we only
// pull declarations.
#include "minih264e.h"
#include "minimp4.h"

#define VID_W      320
#define VID_H      208       // padded from 200 to next multiple of 16
#define SRC_H      200       // actual content height
#define TIMESCALE  90000

// ---------- manifest parsing ------------------------------------------------

typedef struct {
    uint64_t  seq;
    uint64_t  ms;
    char     *png_name;
} ManifestEntry;

typedef struct {
    ManifestEntry *items;
    int            count;
    int            cap;
} Manifest;

static void manifest_push(Manifest *m, ManifestEntry *e) {
    if (m->count >= m->cap) {
        int nc = m->cap ? m->cap * 2 : 64;
        ManifestEntry *na = realloc(m->items, (size_t)nc * sizeof *na);
        if (!na) return;
        m->items = na; m->cap = nc;
    }
    m->items[m->count++] = *e;
}

static void manifest_free(Manifest *m) {
    for (int i = 0; i < m->count; i++) free(m->items[i].png_name);
    free(m->items);
    m->items = NULL; m->count = 0; m->cap = 0;
}

static int cmp_seq(const void *a, const void *b) {
    uint64_t sa = ((const ManifestEntry *)a)->seq;
    uint64_t sb = ((const ManifestEntry *)b)->seq;
    return (sa < sb) ? -1 : (sa > sb) ? 1 : 0;
}

static bool read_manifest(const char *dir, Manifest *out,
                          char *err, size_t err_cap) {
    char path[512];
    snprintf(path, sizeof path, "%s/manifest.ndjson", dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, err_cap, "cannot open %s", path);
        return false;
    }
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        cJSON *j = cJSON_Parse(line);
        if (!j) continue;
        cJSON *jseq = cJSON_GetObjectItem(j, "seq");
        cJSON *jms  = cJSON_GetObjectItem(j, "ms");
        cJSON *jpng = cJSON_GetObjectItem(j, "png");
        if (jseq && jms && jpng && cJSON_IsString(jpng) && jpng->valuestring[0]) {
            ManifestEntry e = {
                .seq      = (uint64_t)jseq->valuedouble,
                .ms       = (uint64_t)jms->valuedouble,
                .png_name = strdup(jpng->valuestring),
            };
            manifest_push(out, &e);
        }
        cJSON_Delete(j);
    }
    fclose(f);
    if (out->count == 0) {
        snprintf(err, err_cap, "manifest has no frames");
        return false;
    }
    qsort(out->items, (size_t)out->count, sizeof(ManifestEntry), cmp_seq);
    return true;
}

// ---------- color conversion (RGBA -> I420), padded to VID_H ----------------
//
// BT.601 limited range. Source is 320x200 RGBA from raylib. Y plane is
// 320x208, U/V planes are 160x104. The bottom 8 rows of Y and bottom 4
// rows of U/V are filled with limited-range black.

static void rgba_to_i420_padded(const unsigned char *rgba,
                                unsigned char *y,
                                unsigned char *u,
                                unsigned char *v) {
    const int W = VID_W;
    // Y plane.
    for (int j = 0; j < SRC_H; j++) {
        const unsigned char *row = rgba + (size_t)j * W * 4;
        unsigned char *yrow = y + (size_t)j * W;
        for (int i = 0; i < W; i++) {
            int r = row[4*i + 0], g = row[4*i + 1], b = row[4*i + 2];
            int yv = 16 + ((66*r + 129*g + 25*b + 128) >> 8);
            if (yv < 0) yv = 0; else if (yv > 255) yv = 255;
            yrow[i] = (unsigned char)yv;
        }
    }
    // Y padding rows (limited-range black = 16).
    memset(y + (size_t)SRC_H * W, 16, (size_t)(VID_H - SRC_H) * W);

    // U/V planes (subsampled 2x2). Source rows 0..199 → uv rows 0..99.
    for (int j = 0; j < SRC_H; j += 2) {
        const unsigned char *row0 = rgba + (size_t)j * W * 4;
        const unsigned char *row1 = (j + 1 < SRC_H)
            ? rgba + (size_t)(j + 1) * W * 4 : row0;
        unsigned char *urow = u + (size_t)(j/2) * (W/2);
        unsigned char *vrow = v + (size_t)(j/2) * (W/2);
        for (int i = 0; i < W; i += 2) {
            int r0 = row0[4*i + 0],     g0 = row0[4*i + 1],     b0 = row0[4*i + 2];
            int r1 = row0[4*(i+1) + 0], g1 = row0[4*(i+1) + 1], b1 = row0[4*(i+1) + 2];
            int r2 = row1[4*i + 0],     g2 = row1[4*i + 1],     b2 = row1[4*i + 2];
            int r3 = row1[4*(i+1) + 0], g3 = row1[4*(i+1) + 1], b3 = row1[4*(i+1) + 2];
            int r = (r0 + r1 + r2 + r3) >> 2;
            int g = (g0 + g1 + g2 + g3) >> 2;
            int b = (b0 + b1 + b2 + b3) >> 2;
            int uv = 128 + ((-38*r - 74*g + 112*b + 128) >> 8);
            int vv = 128 + (( 112*r - 94*g - 18*b + 128) >> 8);
            if (uv < 0) uv = 0; else if (uv > 255) uv = 255;
            if (vv < 0) vv = 0; else if (vv > 255) vv = 255;
            urow[i/2] = (unsigned char)uv;
            vrow[i/2] = (unsigned char)vv;
        }
    }
    // U/V padding rows (centered chroma = 128).
    int uv_pad_rows = (VID_H - SRC_H) / 2;   // 4
    memset(u + (size_t)(SRC_H/2) * (W/2), 128, (size_t)uv_pad_rows * (W/2));
    memset(v + (size_t)(SRC_H/2) * (W/2), 128, (size_t)uv_pad_rows * (W/2));
}

// ---------- minimp4 write callback ------------------------------------------

static int mp4_write_cb(int64_t off, const void *buf, size_t sz, void *tok) {
    FILE *f = (FILE *)tok;
    if (fseek(f, (long)off, SEEK_SET) != 0) return 1;
    return fwrite(buf, 1, sz, f) != sz;
}

// ---------- main entry ------------------------------------------------------

bool mp4_encode_dir(const char *src_dir, const char *out_path,
                    encode_progress_fn cb, void *user,
                    char *err_buf, size_t err_cap) {
    if (!err_buf || err_cap == 0) {
        static char dummy[1]; err_buf = dummy; err_cap = 1;
    }
    err_buf[0] = '\0';

    const char *dir = src_dir;
    if (!dir || !dir[0]) {
        snprintf(err_buf, err_cap, "no record dir");
        return false;
    }

    EncodeProgress prog = { 0, 0, 0.0, "Reading manifest" };
    if (cb) cb(&prog, user);

    Manifest mf = { 0 };
    if (!read_manifest(dir, &mf, err_buf, err_cap)) return false;
    prog.total = mf.count;

    // ---- minih264 init ----
    H264E_create_param_t cp = { 0 };
    cp.width  = VID_W;
    cp.height = VID_H;
    cp.gop    = 30;
    cp.vbv_size_bytes = 0;
    cp.const_input_flag = 1;   // don't trash our input buffer

    int persist_size = 0, scratch_size = 0;
    if (H264E_sizeof(&cp, &persist_size, &scratch_size) != 0) {
        snprintf(err_buf, err_cap, "H264E_sizeof failed");
        manifest_free(&mf);
        return false;
    }
    H264E_persist_t *enc = (H264E_persist_t *)malloc((size_t)persist_size);
    H264E_scratch_t *scr = (H264E_scratch_t *)malloc((size_t)scratch_size);
    unsigned char *yuv_buf = (unsigned char *)malloc(
        (size_t)VID_W * VID_H + 2 * (size_t)(VID_W/2) * (VID_H/2));
    if (!enc || !scr || !yuv_buf) {
        snprintf(err_buf, err_cap, "alloc failed");
        free(enc); free(scr); free(yuv_buf);
        manifest_free(&mf);
        return false;
    }
    if (H264E_init(enc, &cp) != 0) {
        snprintf(err_buf, err_cap, "H264E_init failed");
        free(enc); free(scr); free(yuv_buf);
        manifest_free(&mf);
        return false;
    }

    H264E_io_yuv_t yuv;
    yuv.yuv[0] = yuv_buf;
    yuv.yuv[1] = yuv_buf + (size_t)VID_W * VID_H;
    yuv.yuv[2] = yuv.yuv[1] + (size_t)(VID_W/2) * (VID_H/2);
    yuv.stride[0] = VID_W;
    yuv.stride[1] = VID_W / 2;
    yuv.stride[2] = VID_W / 2;

    // ---- minimp4 init ----
    // Output path comes from the caller (recorder_output_path()).
    if (!out_path || !out_path[0]) {
        snprintf(err_buf, err_cap, "no output path");
        free(enc); free(scr); free(yuv_buf);
        manifest_free(&mf);
        return false;
    }
    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        snprintf(err_buf, err_cap, "cannot create %s", out_path);
        free(enc); free(scr); free(yuv_buf);
        manifest_free(&mf);
        return false;
    }
    MP4E_mux_t *mux = MP4E_open(0 /*sequential*/, 0 /*fragmented*/,
                                fout, mp4_write_cb);
    mp4_h26x_writer_t mp4wr;
    if (mp4_h26x_write_init(&mp4wr, mux, VID_W, VID_H, 0 /*is_hevc*/) != 0) {
        snprintf(err_buf, err_cap, "mp4_h26x_write_init failed");
        MP4E_close(mux); fclose(fout);
        free(enc); free(scr); free(yuv_buf);
        manifest_free(&mf);
        return false;
    }

    struct timespec t_start;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    prog.status = "Encoding";

    bool ok = true;
    for (int i = 0; i < mf.count; i++) {
        const ManifestEntry *e = &mf.items[i];
        char png_path[768];
        snprintf(png_path, sizeof png_path, "%s/%s", dir, e->png_name);

        Image im = LoadImage(png_path);
        if (im.data == NULL) {
            snprintf(err_buf, err_cap, "load failed: %s", png_path);
            ok = false; break;
        }
        if (im.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
            ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        }
        if (im.width != VID_W || im.height != SRC_H) {
            UnloadImage(im);
            snprintf(err_buf, err_cap, "frame %s wrong size %dx%d",
                     e->png_name, im.width, im.height);
            ok = false; break;
        }
        rgba_to_i420_padded((const unsigned char *)im.data,
                            yuv.yuv[0], yuv.yuv[1], yuv.yuv[2]);
        UnloadImage(im);

        H264E_run_param_t rp = { 0 };
        rp.encode_speed = H264E_SPEED_BALANCED;
        rp.frame_type   = (i == 0) ? H264E_FRAME_TYPE_KEY
                                   : H264E_FRAME_TYPE_DEFAULT;
        rp.qp_min = 10;
        rp.qp_max = 10;

        unsigned char *coded = NULL;
        int coded_sz = 0;
        if (H264E_encode(enc, scr, &rp, &yuv, &coded, &coded_sz) != 0) {
            snprintf(err_buf, err_cap, "H264E_encode failed at frame %d", i);
            ok = false; break;
        }

        // Frame duration in 90 kHz ticks.
        uint64_t pts = e->ms;
        uint64_t next_ms = (i + 1 < mf.count) ? mf.items[i + 1].ms : (pts + 100);
        uint64_t dur_ms = (next_ms > pts) ? (next_ms - pts) : 33;
        unsigned dur_90k = (unsigned)(dur_ms * 90);

        // mp4_h26x_write_nal handles Annex-B parsing, SPS/PPS extraction
        // and AVCC length-prefix conversion internally.
        if (mp4_h26x_write_nal(&mp4wr, coded, coded_sz, dur_90k) != MP4E_STATUS_OK) {
            snprintf(err_buf, err_cap,
                     "mp4_h26x_write_nal failed at frame %d", i);
            ok = false; break;
        }

        prog.current = i + 1;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        prog.elapsed_s = (double)(now.tv_sec - t_start.tv_sec) +
                        (double)(now.tv_nsec - t_start.tv_nsec) / 1e9;
        if (cb) cb(&prog, user);
    }

    if (ok) {
        prog.status = "Muxing";
        if (cb) cb(&prog, user);
    }

    mp4_h26x_write_close(&mp4wr);
    MP4E_close(mux);
    fclose(fout);
    free(enc);
    free(scr);
    free(yuv_buf);
    manifest_free(&mf);

    if (ok) {
        prog.status = "Done";
        if (cb) cb(&prog, user);
    }
    return ok;
}
