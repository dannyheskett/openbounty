#include "present.h"
#include "gfx.h"
#include "safe_area.h"
#include "layout.h"
#include "touch.h"
#include "bfont.h"

// The blit rect of the last present_scaled, in window pixels, and the scale
// it used. This is what turns a tap's window position back into design-space
// pixels, and what the touch chrome lays itself out around.
static int s_dst_x, s_dst_y, s_dst_w, s_dst_h, s_dst_scale = 1;

// Runtime display scale, in whole pixels. 1 means one buffer pixel is one
// screen pixel, which is the startup state and what a modern pack is authored
// for. Deliberately not persisted: it is a property of the machine looking at
// the game, not of the pack and not of the save. The project writes no config
// file and stats.options[] is serialized into saves, so neither is a home.
static int s_scale = 1;

// Zoom the current target is rendered at (fixed buffers only; else 1).
static int s_zoom = 1;

int present_get_zoom(void) { return s_zoom; }

void present_target_size(int win_w, int win_h, int *w, int *h) {
    int z = CL_IS_NATIVE ? present_scale(win_w, win_h) : 1;
    if (w) *w = CL_SCREEN_W * z;
    if (h) *h = CL_SCREEN_H * z;
}

void present_begin(RenderTexture2D *rt) {
    gfx_target_begin(*rt);
    if (CL_IS_NATIVE && s_zoom > 1) {
        gfx_zoom_begin((float)s_zoom);
    }
}

void present_end(void) {
    if (CL_IS_NATIVE && s_zoom > 1) gfx_zoom_end();
    gfx_target_end();
}

void present_set_scale(int scale) {
    // Never touches the window. Scale is pixel size, not window size: a higher
    // scale makes each pixel bigger and the viewport correspondingly smaller.
    // A 4K or 8K display is the case this exists for -- at 1:1 a 96px tile is
    // small on such a panel.
    s_scale = (scale > 0) ? scale : 1;
}

int present_get_scale(void) {
    return s_scale;
}

int present_max_scale(int win_w, int win_h) {
    if (!CL_IS_MODERN) return CL_SCALE_MAX;
    // A fixed buffer is shown at 1x, 2x or 3x: the largest that fits.
    if (CL_IS_NATIVE) {
        int fx = win_w / CL_SCREEN_W;
        int fy = win_h / CL_SCREEN_H;
        int fit = (fx < fy) ? fx : fy;
        if (fit < 1) fit = 1;
        if (fit > CL_SCALE_MAX_NATIVE) fit = CL_SCALE_MAX_NATIVE;
        return fit;
    }
    // Measured against the SMALLEST viewport the layout will shrink to, never
    // against the current screen size. That size is itself derived from the
    // scale, so measuring against it feeds back on itself: a larger scale
    // shrinks the viewport, which shrinks the screen, which permits a larger
    // scale. The minimum is fixed, so this terminates.
    int min_w = CL_FRAME_LEFT_W + CL_TILE_W * CL_TILES_MIN
              + CL_SIDEBAR_W + CL_FRAME_RIGHT_W;
    int min_h = CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H
              + CL_TILE_H * CL_TILES_MIN + CL_FRAME_BOTTOM_H;
    int fx = (min_w > 0) ? win_w / min_w : 1;
    int fy = (min_h > 0) ? win_h / min_h : 1;
    int fit = (fx < fy) ? fx : fy;
    if (fit < 1) fit = 1;
#if defined(__EMSCRIPTEN__)
    if (fit > CL_SCALE_MAX_WEB) fit = CL_SCALE_MAX_WEB;
#else
    if (fit > CL_SCALE_MAX) fit = CL_SCALE_MAX;
#endif
    return fit;
}

int present_scale(int win_w, int win_h) {
    int sx = win_w / CL_SCREEN_W;
    int sy = win_h / CL_SCREEN_H;
    int scale = (sx < sy) ? sx : sy;

    // Legacy is left exactly as it was before render modes existed: auto-fit
    // with the CL_SCALE_MIN floor, no override path at all. The scale control
    // is a modern-only feature and must not alter legacy behaviour in any way.
    if (!CL_IS_MODERN) {
        if (scale < CL_SCALE_MIN) scale = CL_SCALE_MIN;
#if defined(__EMSCRIPTEN__)
        if (scale > CL_SCALE_MAX_WEB) scale = CL_SCALE_MAX_WEB;
#else
        if (scale > CL_SCALE_MAX) scale = CL_SCALE_MAX;
#endif
        return scale;
    }

    // Modern renders pixel for pixel: at 1x one buffer pixel is one screen
    // pixel, and it is the VIEWPORT that grows to fill a bigger window, not the
    // pixels. There is no auto-fit -- a pack already sizes its own art and
    // furniture through tile_w/tile_h and ui_scale, so scaling the buffer on
    // top of that would enlarge everything twice.
    //
    // The setting is whatever the player chose, clamped to what the window can
    // actually show so the menu's label never disagrees with the picture.
    //
    // A fixed buffer (render.native_w/native_h) is the other way about: the
    // buffer never changes, so the window is what the zoom sizes. The blit is
    // still the chosen scale clamped to the window, which is what keeps a
    // fullscreen or hand-resized window letterboxed rather than cropped.
    int fit = present_max_scale(win_w, win_h);
    return (s_scale < fit) ? s_scale : fit;
}

void present_zoom_window(int scale) {
    if (!CL_IS_NATIVE) return;
    if (scale < 1) scale = 1;
    if (scale > CL_SCALE_MAX_NATIVE) scale = CL_SCALE_MAX_NATIVE;
    if (IsWindowFullscreen() || IsWindowMaximized()) return;
    SetWindowSize(CL_SCREEN_W * scale, CL_SCREEN_H * scale);
}

bool present_refit(RenderTexture2D *rt) {
    if (!rt) return false;
    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();
    if (CL_IS_NATIVE) {
        // The buffer never follows the window, but the zoom does: the target
        // is the buffer times the zoom, reallocated when the zoom changes.
        int z = present_scale(win_w, win_h);
        int w, h;
        present_target_size(win_w, win_h, &w, &h);
        bool changed = (rt->texture.width != w || rt->texture.height != h);
        if (changed) {
            gfx_target_free(*rt);
            *rt = gfx_target_create(w, h);
            gfx_texture_point(rt->texture);
        }
        if (z != s_zoom) { s_zoom = z; bfont_set_zoom(z); }
        return changed;
    }
    if (!layout_fit_window(win_w, win_h, present_scale(win_w, win_h)))
        return false;
    gfx_target_free(*rt);
    *rt = gfx_target_create(CL_SCREEN_W, CL_SCREEN_H);
    gfx_texture_point(rt->texture);
    return true;
}

void present_scaled(RenderTexture2D rt) {
    gfx_frame_begin();
    gfx_clear(BLACK);

    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();

    // The window minus any display cutout / gesture-bar insets (safe_area.c).
    // Every inset is zero on desktop, web and iOS, so this is the whole window
    // there; on Android it is what the camera notch and the navigation bar
    // leave, and the game is fitted and centred inside it rather than under
    // them. A degenerate inset (wider than the window) is ignored.
    SafeArea sa = safe_area_get();
    int safe_x = sa.left, safe_y = sa.top;
    int safe_w = win_w - sa.left - sa.right;
    int safe_h = win_h - sa.top  - sa.bottom;
    if (safe_w <= 0) { safe_x = 0; safe_w = win_w; }
    if (safe_h <= 0) { safe_y = 0; safe_h = win_h; }

    int scale = present_scale(safe_w, safe_h);

    int dst_w = CL_SCREEN_W * scale;
    int dst_h = CL_SCREEN_H * scale;
    // A fixed buffer was rendered at the zoom already: blit it 1:1.
    if (CL_IS_NATIVE) { dst_w = rt.texture.width; dst_h = rt.texture.height; scale = s_zoom; }

    // A RenderTexture2D is stored y-flipped, hence the negative src height.
    Rectangle src = { 0, 0,
                      (float)rt.texture.width,
                      -(float)rt.texture.height };
    Rectangle dst = { (float)(safe_x + (safe_w - dst_w) / 2),
                      (float)(safe_y + (safe_h - dst_h) / 2),
                      (float)dst_w, (float)dst_h };
    gfx_texture_draw(rt.texture, src, dst, WHITE);

    present_store_dst((int)dst.x, (int)dst.y, dst_w, dst_h, scale);

    // Touch chrome draws over the letterbox, in window pixels, after the
    // game's frame. It renders nothing unless a screen requested chrome
    // this frame AND a touch has been seen, so every present_scaled caller
    // (main loop, startup, combat, cartoon, autoplay, encode) gets it
    // without change and desktop output is untouched.
    touch_draw_chrome();
}

void present_store_dst(int x, int y, int w, int h, int scale) {
    s_dst_x = x; s_dst_y = y;
    s_dst_w = w; s_dst_h = h;
    s_dst_scale = scale;
}

bool present_window_to_screen(int wx, int wy, int *sx, int *sy) {
    if (s_dst_w <= 0 || s_dst_h <= 0 || s_dst_scale <= 0) return false;
    if (wx < s_dst_x || wy < s_dst_y ||
        wx >= s_dst_x + s_dst_w || wy >= s_dst_y + s_dst_h) return false;
    if (sx) *sx = (wx - s_dst_x) / s_dst_scale;
    if (sy) *sy = (wy - s_dst_y) / s_dst_scale;
    return true;
}

void present_last_dst(int *x, int *y, int *w, int *h) {
    if (x) *x = s_dst_x;
    if (y) *y = s_dst_y;
    if (w) *w = s_dst_w;
    if (h) *h = s_dst_h;
}
