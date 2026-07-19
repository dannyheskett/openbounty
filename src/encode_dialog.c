// Progress dialog for the --movie shutdown path. Drives mp4_encode_dir
// synchronously, rendering an "Encoding video..." panel each time the
// encoder calls back. Renders OVER the existing framebuffer (no clear)
// so the last in-game frame stays visible behind the panel.

#include "encode_dialog.h"
#include "encode_mp4.h"
#include "bfont.h"
#include "palette.h"
#include "layout.h"
#include "present.h"
#include "raylib.h"
#include "frame_host.h"
#include <stdio.h>
#include <string.h>

#define GW BFONT_GLYPH_W
#define GH BFONT_GLYPH_H

// We do NOT clear the render target. Whatever the previous frame left
// is the backdrop the modal panel sits on top of.
static void frame_begin(RenderTexture2D *rt) {
    BeginTextureMode(*rt);
}

static void frame_end(RenderTexture2D *rt) {
    EndTextureMode();
    present_scaled(*rt);
    frame_host_end_frame();
}

// Centered modal panel with a yellow border on dark blue. Repainted
// solid each frame so prior text is overwritten cleanly.
//
//   +------------------------------+
//   |       Encoding Video         |
//   |                              |
//   |  Frame 124 of 380       32%  |
//   |  +------------------------+  |
//   |  |#########               |  |   <- drawn rect, not glyphs
//   |  +------------------------+  |
//   |  Elapsed: 14.3s              |
//   |                              |
//   |  status: Encoding            |
//   +------------------------------+
//
// Width: 36 chars. Height: 11 rows.
static void draw_panel(const EncodeProgress *p, const char *footer) {
    // 36-char inner width, 8-px padding all round. The 320x200 screen
    // can fit 36 cols comfortably (288 px + 16 px padding = 304 px).
    int cols = 36, rows = 11;
    int pad  = 8;
    int w = cols * GW + pad * 2;
    int h = rows * GH + pad * 2;
    int x = (CL_SCREEN_W - w) / 2;
    int y = (CL_SCREEN_H - h) / 2;

    // Solid panel background covers anything from the previous frame.
    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    DrawRectangleLines(x, y, w, h, PAL_CLR(YELLOW));

    int tx = x + pad;
    int ty = y + pad;

    // Title row, centered.
    bfont_draw_centered("Encoding Video",
                        x + w / 2, ty, PAL_CLR(YELLOW));
    ty += GH * 2;

    // Frame counter (left) + percentage (right) on the same row.
    char line[64];
    snprintf(line, sizeof line, "Frame %d of %d", p->current, p->total);
    bfont_draw(line, tx, ty, PAL_CLR(WHITE));
    int pct = (p->total > 0) ? (p->current * 100 / p->total) : 0;
    if (pct > 100) pct = 100;
    char pctbuf[16];
    snprintf(pctbuf, sizeof pctbuf, "%3d%%", pct);
    int pctw = (int)bfont_measure(pctbuf).x;
    bfont_draw(pctbuf, x + w - pad - pctw, ty, PAL_CLR(WHITE));
    ty += GH + 2;

    // Drawn progress bar: dark trough + yellow fill + yellow border.
    int bar_x = tx;
    int bar_y = ty;
    int bar_w = cols * GW;                // full inner width
    int bar_h = GH + 2;
    DrawRectangle(bar_x, bar_y, bar_w, bar_h, PAL_CLR(BLACK));
    int filled = (p->total > 0)
        ? (int)((long long)bar_w * p->current / p->total) : 0;
    if (filled < 0) filled = 0;
    if (filled > bar_w) filled = bar_w;
    if (filled > 0) {
        DrawRectangle(bar_x + 1, bar_y + 1,
                      filled - 2 > 0 ? filled - 2 : 0,
                      bar_h - 2, PAL_CLR(YELLOW));
    }
    DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, PAL_CLR(YELLOW));
    ty += bar_h + 2;

    // Elapsed.
    snprintf(line, sizeof line, "Elapsed: %5.1fs", p->elapsed_s);
    bfont_draw(line, tx, ty, PAL_CLR(WHITE));
    ty += GH * 2;

    // Status. Ellipsize to the inner width if it would overrun (the status can
    // carry a full output PATH on the encoder error path, e.g. "cannot create
    // /long/path/movie.mp4", which otherwise spills past the box). Trailing "..."
    // makes the truncation read as intentional rather than a clipped glyph.
    {
        const char *s = p->status ? p->status : "";
        char buf[160];
        snprintf(buf, sizeof buf, "status: %s", s);
        int max_chars = cols;
        if (max_chars >= (int)sizeof buf) max_chars = (int)sizeof buf - 1;
        if ((int)strlen(buf) > max_chars) {
            if (max_chars >= 3) {
                buf[max_chars - 3] = '.';
                buf[max_chars - 2] = '.';
                buf[max_chars - 1] = '.';
            }
            buf[max_chars] = '\0';
        }
        bfont_draw(buf, tx, ty, PAL_CLR(GREY));
    }
    ty += GH;

    // Optional footer (e.g. "Press any key to exit").
    if (footer && footer[0]) {
        ty += GH / 2;
        bfont_draw_centered(footer, x + w / 2, ty, PAL_CLR(YELLOW));
    }
}

// State threaded through the encoder callback. We remember the last
// progress payload so the final "press any key" panel can show the
// real frame count and elapsed time, not 1-of-1.
typedef struct {
    RenderTexture2D *rt;
    int    last_total;
    double last_elapsed_s;
} CallbackCtx;

static void on_progress(const EncodeProgress *p, void *user) {
    CallbackCtx *cx = (CallbackCtx *)user;
    if (p->total > 0)        cx->last_total     = p->total;
    if (p->elapsed_s > 0.0)  cx->last_elapsed_s = p->elapsed_s;
    if (frame_host_should_close()) return;
    frame_begin(cx->rt);
    draw_panel(p, NULL);
    frame_end(cx->rt);
}

bool encode_dialog_session(RenderTexture2D *rt, const char *src_dir,
                           const char *out_path) {
    if (!rt || !src_dir || !out_path) return false;

    // Initial frame: dialog appears immediately, before the encoder
    // spends time scanning the manifest.
    EncodeProgress init = { 0, 0, 0.0, "Reading manifest" };
    CallbackCtx cx = { rt, 0, 0.0 };
    on_progress(&init, &cx);

    char err[256] = { 0 };
    bool ok = mp4_encode_dir(src_dir, out_path, on_progress, &cx,
                             err, sizeof err);

    // Final panel: shows the real frame count + elapsed time captured
    // during encode. Held until the user presses any key.
    EncodeProgress final_state = {
        .current   = ok ? cx.last_total : 0,
        .total     = cx.last_total > 0 ? cx.last_total : 1,
        .elapsed_s = cx.last_elapsed_s,
        .status    = ok ? "Done. movie.mp4 written."
                        : (err[0] ? err : "Failed"),
    };
    // Hold the "Done" panel briefly, then AUTO-EXIT -- so an unattended recording
    // (notably `--demo --movie`, which ends its arc on its own) finalizes and
    // quits without a human keypress. An interactive user can still press any key
    // to dismiss it immediately; otherwise it closes after AUTO_EXIT_S.
    const double AUTO_EXIT_S = 2.5;
    const char *footer = "Press any key to exit";
    double until = GetTime() + AUTO_EXIT_S;

    while (!frame_host_should_close()) {
        if (GetTime() >= until) break;   // auto-exit: no keypress required
        // Drain queued key edges. raylib polls fresh input inside
        // frame_end's BeginDrawing, so a key pressed during any frame is
        // detected on the next iteration.
        bool got_key = false;
        int k;
        while ((k = GetKeyPressed()) != 0) {
            // Ignore pure-modifier keys so accidental shift/ctrl don't
            // dismiss the dialog before the user reads it.
            if (k != KEY_LEFT_SHIFT && k != KEY_RIGHT_SHIFT &&
                k != KEY_LEFT_CONTROL && k != KEY_RIGHT_CONTROL &&
                k != KEY_LEFT_ALT && k != KEY_RIGHT_ALT &&
                k != KEY_LEFT_SUPER && k != KEY_RIGHT_SUPER &&
                k != KEY_CAPS_LOCK && k != KEY_NUM_LOCK &&
                k != KEY_SCROLL_LOCK) {
                got_key = true;
            }
        }
        if (got_key) break;
        frame_begin(rt);
        draw_panel(&final_state, footer);
        frame_end(rt);
    }

    if (!ok) {
        fprintf(stdout, "[encode-movie] %s\n", err[0] ? err : "failed");
    }
    return ok;
}
