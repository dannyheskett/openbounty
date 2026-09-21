// raylib backend for the gfx primitive layer (src/gfx.h): every entry point is
// a thin wrapper over the raylib call it replaces, so desktop, web and Android
// rendering is identical to what it was before the seam existed. iOS compiles
// ios/gfx_metal.mm instead and never builds this file.
//
// Nothing here adds behaviour. Where raylib takes floats and the shell has
// ints, the cast is the one the call sites were already doing inline.

#include "gfx.h"

#if !defined(PLATFORM_IOS)

#include "raylib.h"

void gfx_frame_begin(void) { BeginDrawing(); }
void gfx_frame_end(void)   { EndDrawing(); }
void gfx_clear(Color color) { ClearBackground(color); }

void gfx_rect(int x, int y, int w, int h, Color color) {
    DrawRectangle(x, y, w, h, color);
}

void gfx_rect_lines(int x, int y, int w, int h, Color color) {
    DrawRectangleLines(x, y, w, h, color);
}

void gfx_rect_rounded(int x, int y, int w, int h, float roundness,
                      int segments, Color color) {
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, (float)h },
                         roundness, segments, color);
}

void gfx_rect_rounded_lines(int x, int y, int w, int h, float roundness,
                            int segments, Color color) {
    DrawRectangleRoundedLines((Rectangle){ (float)x, (float)y,
                                           (float)w, (float)h },
                              roundness, segments, color);
}

void gfx_triangle(Vector2 a, Vector2 b, Vector2 c, Color color) {
    DrawTriangle(a, b, c, color);
}

void gfx_circle(int cx, int cy, float radius, Color color) {
    DrawCircle(cx, cy, radius, color);
}

void gfx_label(const char *text, int x, int y, int size, Color color) {
    DrawText(text, x, y, size, color);
}

int gfx_label_width(const char *text, int size) {
    return MeasureText(text, size);
}

void gfx_clip_begin(int x, int y, int w, int h) {
    BeginScissorMode(x, y, w, h);
}

void gfx_clip_end(void) { EndScissorMode(); }

Texture2D gfx_texture_from_image(Image img) {
    return LoadTextureFromImage(img);
}

void gfx_texture_free(Texture2D t) { UnloadTexture(t); }

void gfx_texture_point(Texture2D t) {
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
}

void gfx_texture_point_clamp(Texture2D t) {
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
}

Image gfx_image_from_memory(const char *ext, const unsigned char *bytes, int size) {
    return LoadImageFromMemory(ext, bytes, size);
}

Image gfx_image_solid(int w, int h, Color color) {
    return GenImageColor(w, h, color);
}

void gfx_image_free(Image img) { UnloadImage(img); }

void gfx_image_fill_rect(Image *dst, int x, int y, int w, int h, Color color) {
    ImageDrawRectangle(dst, x, y, w, h, color);
}

void gfx_image_blit(Image *dst, Image src, Rectangle src_rect,
                    Rectangle dst_rect, Color tint) {
    ImageDraw(dst, src, src_rect, dst_rect, tint);
}

// A whole-frame integer zoom. raylib expresses it as a 2D camera; the offset
// and target stay at the origin, so this is a pure scale about (0,0).
void gfx_zoom_begin(float zoom) {
    Camera2D cam = { 0 };
    cam.zoom = zoom;
    BeginMode2D(cam);
}

void gfx_zoom_end(void) { EndMode2D(); }

void gfx_texture_draw(Texture2D t, Rectangle src, Rectangle dst, Color tint) {
    DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0.0f, tint);
}

RenderTexture2D gfx_target_create(int w, int h) {
    return LoadRenderTexture(w, h);
}

void gfx_target_free(RenderTexture2D rt) { UnloadRenderTexture(rt); }
void gfx_target_begin(RenderTexture2D rt) { BeginTextureMode(rt); }
void gfx_target_end(void) { EndTextureMode(); }

#endif // !PLATFORM_IOS
