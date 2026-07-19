// src/shell_frame.c

#include "shell_frame.h"

#include "raylib.h"
#include "frame_host.h"
#include "layout.h"
#include "present.h"     // CL_SCREEN_W/H (present-frame blit scaling)
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

    present_scaled(*target);
    frame_host_end_frame();
}
