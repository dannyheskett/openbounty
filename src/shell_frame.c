// src/shell_frame.c

#include "shell_frame.h"

#include "raylib.h"
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
