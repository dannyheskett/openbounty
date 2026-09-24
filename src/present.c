#include <stdio.h>
#include "present.h"
#include "frame_host.h"
#include "gfx.h"
#include "safe_area.h"
#include "layout.h"
#include "touch.h"
#include "bfont.h"
#include "modern/page.h"

// The blit rect of the last present_scaled, in window pixels, and the
// design-space size it showed: what turns a tap's window position back into
// design-space pixels. The touch chrome lays itself out around the rect.
static int s_dst_x, s_dst_y, s_dst_w, s_dst_h;
static int s_des_w, s_des_h;

// Runtime display scale, in whole pixels. 1 means one buffer pixel is one
// screen pixel, which is the startup state and what a modern pack is authored
// for. Deliberately not persisted: it is a property of the machine looking at
// the game, not of the pack and not of the save. The project writes no config
// file and stats.options[] is serialized into saves, so neither is a home.
static int s_scale = 1;

// Zoom the current target is rendered at (fixed buffers only; else 1).
static int s_zoom = 1;
#if !defined(PLATFORM_IOS) && !defined(PLATFORM_ANDROID) && !defined(__EMSCRIPTEN__)
static bool s_zoomed;          // a zoom has been chosen
static int  s_zoom_state;      // the window's maximised / full-screen state then
#endif
static bool s_layout_changed;  // the last refit changed the screen
static bool s_smooth;          // the target is filtered smoothly (fitted down)

bool present_layout_changed(void) { return s_layout_changed; }

// The zoom a surface gets, `fit` being the largest that fits it. On a desktop
// window the zoom never rises while an edge is dragged: it rises only when
// the window is the size the game gave it, or has just been maximised,
// restored or made full screen. It falls whenever the surface can no longer
// hold it. A phone, and the browser's canvas, take the largest that fits.
static int held_zoom(int fit) {
#if defined(PLATFORM_IOS) || defined(PLATFORM_ANDROID) || defined(__EMSCRIPTEN__)
    return fit;
#else
    int state = (frame_host_window_maximized() ? 1 : 0) | (frame_host_window_fullscreen() ? 2 : 0);
    bool free_rise = !s_zoomed || state != s_zoom_state || frame_host_window_at_set_size();
    s_zoomed = true;
    s_zoom_state = state;
    return (free_rise || fit < s_zoom) ? fit : s_zoom;
#endif
}

int present_get_zoom(void) { return s_zoom; }

void present_target_size(int win_w, int win_h, int *w, int *h) {
    int z = CL_IS_NATIVE ? present_scale(win_w, win_h) : 1;
    if (w) *w = CL_SCREEN_W * z;
    if (h) *h = CL_SCREEN_H * z;
}

void present_begin(RenderTexture2D *rt) {
    // A frame starts with no page open (modern pages: src/modern/page.c).
    page_frame_begin();
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
    // A declared buffer is shown at the largest whole scale that fits. The
    // measure is the DECLARED size, not the current buffer: the map pane grows
    // to fill what is left, and measuring against the grown buffer would feed
    // back on itself (bigger pane -> smaller fit -> smaller pane).
    if (CL_IS_NATIVE) {
        int floor_w = g_layout.native_w > 0 ? g_layout.native_w : CL_SCREEN_W;
        int floor_h = g_layout.native_h > 0 ? g_layout.native_h : CL_SCREEN_H;
        int fx = win_w / floor_w;
        int fy = win_h / floor_h;
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
    int floor_w = (CL_IS_NATIVE && g_layout.native_w > 0) ? g_layout.native_w : CL_SCREEN_W;
    int floor_h = (CL_IS_NATIVE && g_layout.native_h > 0) ? g_layout.native_h : CL_SCREEN_H;
    int sx = win_w / floor_w;
    int sy = win_h / floor_h;
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
    // There is no zoom setting. The scale is the largest whole number the
    // surface can show, everywhere: the desktop window opens at the declared
    // buffer times that scale, and mobile and the web canvas take whatever
    // surface they are given. What the scale leaves over goes to the map pane
    // (layout_grow_native), so a bigger screen buys more world rather than
    // bigger pixels or wider black bars.
    return present_max_scale(win_w, win_h);
}

// The part of the window the game may draw in: all of it, less any display
// cutout or gesture-bar insets (safe_area.c). Every inset is zero on desktop,
// web and iOS (whose window already IS its safe area); on Android it is what
// the camera notch and the navigation bar leave. A degenerate inset (wider
// than the window) is ignored.
static void safe_rect(int *x, int *y, int *w, int *h) {
    int win_w = frame_host_window_width();
    int win_h = frame_host_window_height();
    SafeArea sa = safe_area_get();
    int sx = sa.left, sy = sa.top;
    int sw = win_w - sa.left - sa.right;
    int sh = win_h - sa.top  - sa.bottom;
    if (sw <= 0) { sx = 0; sw = win_w; }
    if (sh <= 0) { sy = 0; sh = win_h; }
    if (x) *x = sx;
    if (y) *y = sy;
    if (w) *w = sw;
    if (h) *h = sh;
}

bool present_refit(RenderTexture2D *rt) {
    if (!rt) return false;
    s_layout_changed = false;
    if (CL_IS_NATIVE) {
        // A declared buffer takes the whole surface, on every screen: the
        // zoom is the largest whole number (3 at most) at which the declared
        // buffer fits -- held while a window edge is dragged (held_zoom) --
        // and the screen is the surface at that zoom (layout_grow_native).
        // The target is the screen times the zoom.
        int sw, sh;
        safe_rect(NULL, NULL, &sw, &sh);
        int z = held_zoom(present_scale(sw, sh));
        bool grown = layout_grow_native(sw, sh, z);
        int w = CL_SCREEN_W * z, h = CL_SCREEN_H * z;
        bool changed = grown || (rt->texture.width != w || rt->texture.height != h);
        if (changed) {
            gfx_target_free(*rt);
            *rt = gfx_target_create(w, h);
            gfx_texture_point(rt->texture);
            s_smooth = false;
        }
        if (z != s_zoom) { s_zoom = z; bfont_set_zoom(z); }
        s_layout_changed = changed;
        return changed;
    }
    int win_w = frame_host_window_width();
    int win_h = frame_host_window_height();
    if (!layout_fit_window(win_w, win_h, present_scale(win_w, win_h)))
        return false;
    gfx_target_free(*rt);
    *rt = gfx_target_create(CL_SCREEN_W, CL_SCREEN_H);
    gfx_texture_point(rt->texture);
    s_layout_changed = true;
    return true;
}

// The largest whole number of times a dst_w x dst_h frame fits inside the
// safe area, never less than 1.
//
// Only the mobile paths multiply by this: a phone has no window to resize and
// no scale control, so the desktop's 1x would leave an 800x504 buffer as a
// small panel in the middle of a 2400x1080 screen. A whole number keeps every
// pack pixel square and the art hard-edged. Compiled everywhere so it can be
// tested anywhere; on a screen smaller than the frame it answers 1 and the
// frame is centred and clipped rather than shrunk.
int present_fit_multiple(int dst_w, int dst_h, int safe_w, int safe_h) {
    if (dst_w <= 0 || dst_h <= 0) return 1;
    int fit_x = safe_w / dst_w;
    int fit_y = safe_h / dst_h;
    int fit = (fit_x < fit_y) ? fit_x : fit_y;
    return (fit < 1) ? 1 : fit;
}

bool present_fit_down(int w, int h, int room_w, int room_h, int *out_w, int *out_h) {
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (w <= 0 || h <= 0 || room_w <= 0 || room_h <= 0) return false;
    if (w <= room_w && h <= room_h) return false;
    if ((long)w * room_h > (long)h * room_w) {
        if (out_h) *out_h = (int)((long)h * room_w / w);
        if (out_w) *out_w = room_w;
    } else {
        if (out_w) *out_w = (int)((long)w * room_h / h);
        if (out_h) *out_h = room_h;
    }
    return true;
}

void present_scaled(RenderTexture2D rt) {
    gfx_frame_begin();
    gfx_clear(BLACK);

    // The game is fitted and centred inside the safe area, never under a
    // notch or a gesture bar.
    int safe_x, safe_y, safe_w, safe_h;
    safe_rect(&safe_x, &safe_y, &safe_w, &safe_h);

    int scale = present_scale(safe_w, safe_h);

    int dst_w = CL_SCREEN_W * scale;
    int dst_h = CL_SCREEN_H * scale;
    if (CL_IS_NATIVE) {
        // A declared buffer was rendered at the zoom already: blit it 1:1.
        dst_w = rt.texture.width; dst_h = rt.texture.height; scale = s_zoom;
        // Below the smallest screen -- a browser window can be -- the frame
        // is fitted down to the window rather than cut off, smoothly, so no
        // row or column of pixels is dropped.
        bool down = present_fit_down(dst_w, dst_h, safe_w, safe_h, &dst_w, &dst_h);
        if (down) scale = 1;
        if (down != s_smooth) {
            s_smooth = down;
            if (down) gfx_texture_smooth(rt.texture);
            else      gfx_texture_point(rt.texture);
        }
    }

#if defined(PLATFORM_IOS) || defined(PLATFORM_ANDROID)
    // Mobile shows the frame at the largest whole-number multiple of whatever
    // the paragraphs above decided. See present_fit_multiple: `scale` is what
    // present_window_to_screen divides a tap by, so it takes the multiple too.
    {
        int fit = present_fit_multiple(dst_w, dst_h, safe_w, safe_h);
        dst_w *= fit;
        dst_h *= fit;
        scale *= fit;
    }
#endif

    // A RenderTexture2D is stored y-flipped, hence the negative src height.
    Rectangle src = { 0, 0,
                      (float)rt.texture.width,
                      -(float)rt.texture.height };
    Rectangle dst = { (float)(safe_x + (safe_w - dst_w) / 2),
                      (float)(safe_y + (safe_h - dst_h) / 2),
                      (float)dst_w, (float)dst_h };
    gfx_texture_draw(rt.texture, src, dst, WHITE);

    s_dst_x = (int)dst.x; s_dst_y = (int)dst.y;
    s_dst_w = dst_w;      s_dst_h = dst_h;
    s_des_w = CL_SCREEN_W; s_des_h = CL_SCREEN_H;

#if defined(PLATFORM_IOS) || defined(PLATFORM_ANDROID)
    // The first frame's arithmetic, once, into the platform log: a frame in the
    // wrong place on a phone cannot be read off a screenshot alone.
    {
        static bool said = false;
        if (!said) {
            said = true;
            printf("[present] window %dx%d safe %d,%d %dx%d buffer %dx%d -> dst %d,%d %dx%d scale %d\n",
                   frame_host_window_width(), frame_host_window_height(),
                   safe_x, safe_y, safe_w, safe_h,
                   rt.texture.width, rt.texture.height,
                   (int)dst.x, (int)dst.y, dst_w, dst_h, scale);
            fflush(stdout);
        }
    }
#endif

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
    s_des_w = scale > 0 ? w / scale : 0;
    s_des_h = scale > 0 ? h / scale : 0;
}

bool present_window_to_screen(int wx, int wy, int *sx, int *sy) {
    if (s_dst_w <= 0 || s_dst_h <= 0 || s_des_w <= 0 || s_des_h <= 0) return false;
    if (wx < s_dst_x || wy < s_dst_y ||
        wx >= s_dst_x + s_dst_w || wy >= s_dst_y + s_dst_h) return false;
    if (sx) *sx = (int)((long)(wx - s_dst_x) * s_des_w / s_dst_w);
    if (sy) *sy = (int)((long)(wy - s_dst_y) * s_des_h / s_dst_h);
    return true;
}

int present_window_len_to_design(int len) {
    if (s_dst_w <= 0 || s_des_w <= 0) return len;
    return (int)((long)len * s_des_w / s_dst_w);
}


void present_last_dst(int *x, int *y, int *w, int *h) {
    if (x) *x = s_dst_x;
    if (y) *y = s_dst_y;
    if (w) *w = s_dst_w;
    if (h) *h = s_dst_h;
}
