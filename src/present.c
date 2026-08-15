#include "present.h"
#include "layout.h"

// Runtime display scale. 0 = auto-fit the window, which is the startup state.
// Deliberately not persisted: it is a property of the machine looking at the
// game, not of the pack and not of the save. The project writes no config file
// and stats.options[] is serialized into saves, so neither is a home for it.
static int s_scale_override = 0;

void present_set_scale(int scale) {
    s_scale_override = (scale > 0) ? scale : 0;
}

int present_get_scale_override(void) {
    return s_scale_override;
}

int present_scale(int win_w, int win_h) {
    int sx = win_w / CL_SCREEN_W;
    int sy = win_h / CL_SCREEN_H;
    int scale = (sx < sy) ? sx : sy;

    // An explicit choice wins, but never past what the window can actually
    // show -- a 4x pick in a small window would crop rather than letterbox.
    if (s_scale_override > 0) {
        int fit = (sx < sy) ? sx : sy;
        scale = (s_scale_override < fit) ? s_scale_override : fit;
    }

    // Floor first, then ceiling. Order matters only if the two ever cross,
    // which would be a misconfiguration; clamping low-then-high means the
    // ceiling wins in that case rather than the window dictating the size.
    // The floor keeps a small screen legible, but it belongs to the geometry:
    // 320x200 needs 2x to be usable, whereas a modern pack is already large and
    // must be allowed to sit at 1:1. CL_SCALE is the pack's own starting scale.
    int floor_scale = CL_SCALE;
    if (scale < floor_scale) scale = floor_scale;
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
