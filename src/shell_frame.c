// src/shell_frame.c

#include "shell_frame.h"

#include "raylib.h"
#include "layout.h"     // CL_SCREEN_W/H (present-frame blit scaling)
#include "chrome.h"
#include "hud.h"
#include "map_render.h"
#include "overlay.h"

void draw_frame(const Game *game, const Map *map, const Fog *fog,
                const Sprites *sprites) {
    ClearBackground(BLACK);
    chrome_draw(game, sprites);
    map_render_draw(game, map, fog, sprites);
    hud_draw(game, sprites);
    overlay_draw(game, map, fog, sprites);
}

void shell_present_frame(const Game *game, const Map *map, const Fog *fog,
                         const Sprites *sprites, void *render_target) {
    RenderTexture2D *target = (RenderTexture2D *)render_target;
    BeginTextureMode(*target);
    draw_frame(game, map, fog, sprites);
    EndTextureMode();

    // Scale + letterbox blit — identical to the main loop / combat replay.
    BeginDrawing();
    ClearBackground(BLACK);
    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();
    int sx = win_w / CL_SCREEN_W;
    int sy = win_h / CL_SCREEN_H;
    int scale = (sx < sy) ? sx : sy;
    if (scale < 2) scale = 2;
    int dst_w = CL_SCREEN_W * scale;
    int dst_h = CL_SCREEN_H * scale;
    int dst_x = (win_w - dst_w) / 2;
    int dst_y = (win_h - dst_h) / 2;
    Rectangle src = { 0, 0,
                      (float)target->texture.width,
                      -(float)target->texture.height };
    Rectangle dst = { (float)dst_x, (float)dst_y,
                      (float)dst_w, (float)dst_h };
    DrawTexturePro(target->texture, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndDrawing();
}
