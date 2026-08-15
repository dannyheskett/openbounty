#include "present.h"
#include "layout.h"

// Runtime display scale. 0 = auto-fit the window, which is the startup state.
// Deliberately not persisted: it is a property of the machine looking at the
// game, not of the pack and not of the save. The project writes no config file
// and stats.options[] is serialized into saves, so neither is a home for it.
static int s_scale_override = 0;

void present_set_scale(int scale) {
    s_scale_override = (scale > 0) ? scale : 0;
    // Resizing is the point of the control. Clamping the choice to whatever the
    // window already fits made every option collapse to the current scale, so
    // the setting appeared to do nothing. Grow the window to match instead.
    if (s_scale_override > 0 && IsWindowReady() && !IsWindowFullscreen()) {
        SetWindowSize(CL_SCREEN_W * s_scale_override,
                      CL_SCREEN_H * s_scale_override);
    }
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

    // An explicit choice is honoured as given. The floor below is a default-
    // sizing rule, not a veto on what the player asked for, and set_scale has
    // already grown the window to match so the fit clamp normally agrees.
    if (s_scale_override > 0) {
        int fit = (sx < sy) ? sx : sy;
        if (fit < 1) fit = 1;
        scale = (s_scale_override < fit) ? s_scale_override : fit;
    } else {
        // Auto-fit. Floor first, then ceiling. The floor keeps a small screen
        // legible and belongs to the geometry: 320x200 needs 2x to be usable,
        // whereas a modern pack is already large and sits at 1:1. CL_SCALE is
        // the pack's own starting scale.
        int floor_scale = CL_SCALE;
        if (scale < floor_scale) scale = floor_scale;
    }
#if defined(__EMSCRIPTEN__)
    if (scale > CL_SCALE_MAX_WEB) scale = CL_SCALE_MAX_WEB;
#else
    if (scale > CL_SCALE_MAX) scale = CL_SCALE_MAX;
#endif
    return scale;
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
