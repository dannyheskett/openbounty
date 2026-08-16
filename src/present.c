#include "present.h"
#include "layout.h"

// Runtime display scale. 0 = auto-fit the window, which is the startup state.
// Deliberately not persisted: it is a property of the machine looking at the
// game, not of the pack and not of the save. The project writes no config file
// and stats.options[] is serialized into saves, so neither is a home for it.
static int s_scale_override = 0;

void present_set_scale(int scale) {
    // Never touches the window. Scale is pixel size, not window size: the
    // buffer is drawn larger and present_scaled centres and letterboxes it.
    // A 4K or 8K display is the case this exists for -- at 1x a 96px tile is
    // tiny on such a panel, and 2x/3x make it comfortable without changing
    // how much map is visible.
    s_scale_override = (scale > 0) ? scale : 0;
}

int present_get_scale_override(void) {
    return s_scale_override;
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

    // Modern: Auto is the largest scale that still shows the pack's declared
    // viewport. Staying at 1x would leave the chrome, the status font and the
    // sidebar at 320x200 proportions on a 1920x1200 buffer -- a hairline frame
    // and an 8px font around giant tiles. The viewport then grows into whatever
    // the scale leaves over.
    scale = layout_auto_scale(win_w, win_h);
    if (s_scale_override > 0) {
        // Clamp against the SMALLEST viewport the layout will shrink to, not
        // against the current screen size. The current size is derived from the
        // scale, so measuring against it would feed back on itself: a larger
        // scale shrinks the viewport, which shrinks the screen, which permits a
        // larger scale. The minimum is fixed, so this terminates.
        int min_w = CL_FRAME_LEFT_W + CL_TILE_W * CL_TILES_MIN
                  + CL_SIDEBAR_W + CL_FRAME_RIGHT_W;
        int min_h = CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H
                  + CL_TILE_H * CL_TILES_MIN + CL_FRAME_BOTTOM_H;
        int fx = win_w / min_w, fy = win_h / min_h;
        int fit = (fx < fy) ? fx : fy;
        if (fit < 1) fit = 1;
        scale = (s_scale_override < fit) ? s_scale_override : fit;
    }
#if defined(__EMSCRIPTEN__)
    if (scale > CL_SCALE_MAX_WEB) scale = CL_SCALE_MAX_WEB;
#else
    if (scale > CL_SCALE_MAX) scale = CL_SCALE_MAX;
#endif
    return scale;
}

bool present_refit(RenderTexture2D *rt) {
    if (!rt) return false;
    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();
    if (!layout_fit_window(win_w, win_h, present_scale(win_w, win_h)))
        return false;
    UnloadRenderTexture(*rt);
    *rt = LoadRenderTexture(CL_SCREEN_W, CL_SCREEN_H);
    SetTextureFilter(rt->texture, TEXTURE_FILTER_POINT);
    return true;
}

void present_scaled(RenderTexture2D rt) {
    BeginDrawing();
    ClearBackground(BLACK);

    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();
    int scale = present_scale(win_w, win_h);

    int dst_w = CL_SCREEN_W * scale;
    int dst_h = CL_SCREEN_H * scale;

    // A RenderTexture2D is stored y-flipped, hence the negative src height.
    Rectangle src = { 0, 0,
                      (float)rt.texture.width,
                      -(float)rt.texture.height };
    Rectangle dst = { (float)((win_w - dst_w) / 2),
                      (float)((win_h - dst_h) / 2),
                      (float)dst_w, (float)dst_h };
    DrawTexturePro(rt.texture, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
}
