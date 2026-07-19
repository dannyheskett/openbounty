#include "present.h"
#include "layout.h"

int present_scale(int win_w, int win_h) {
    int sx = win_w / CL_SCREEN_W;
    int sy = win_h / CL_SCREEN_H;
    int scale = (sx < sy) ? sx : sy;

    // Floor first, then ceiling. Order matters only if the two ever cross,
    // which would be a misconfiguration; clamping low-then-high means the
    // ceiling wins in that case rather than the window dictating the size.
    if (scale < CL_SCALE_MIN) scale = CL_SCALE_MIN;
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
