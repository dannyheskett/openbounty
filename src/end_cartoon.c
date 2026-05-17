#include "end_cartoon.h"
#include "layout.h"
#include "screenshot.h"
#include "ui.h"
#include "tables.h"
#include <stdio.h>
#include <string.h>

//  + draw_cartoon_frame
// (game.c:4281). The scene is a grid of CL_TILE_W x CL_TILE_H cells:
//   - grass backdrop across the whole grid
//   - a "carpet" column that grows upward one cell per frame
//   - a mounted hero advancing along the carpet starting at frame 5
//   - troops filling the non-carpet cells with their 2-frame walk cycle
//     (flipped horizontally for cells to the right of the carpet column)
//
// The grid is centered horizontally on the 320-wide display and placed at
// the map viewport's Y origin. Frames advance every `ticks_per_step`
// ticks; any keypress short-circuits the animation.

static bool any_key_pressed(void) {
    int k = GetKeyPressed();
    while (k != 0) {
        if (k != KEY_LEFT_SHIFT && k != KEY_RIGHT_SHIFT &&
            k != KEY_LEFT_CONTROL && k != KEY_RIGHT_CONTROL &&
            k != KEY_LEFT_ALT && k != KEY_RIGHT_ALT) return true;
        k = GetKeyPressed();
    }
    return false;
}

static void draw_tile(Texture2D tex, int gx, int gy, int origin_x, int origin_y,
                      bool flip_h) {
    if (!tex.id) return;
    int dx = origin_x + gx * CL_TILE_W;
    int dy = origin_y + gy * CL_TILE_H;
    Rectangle src = {
        0, 0,
        flip_h ? -(float)tex.width : (float)tex.width,
        (float)tex.height
    };
    Rectangle dst = {
        (float)dx, (float)dy,
        (float)CL_TILE_W, (float)CL_TILE_H
    };
    DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

static void draw_cartoon_frame(const Resources *res, const Sprites *sprites,
                               int origin_x, int origin_y,
                               int tick, int frame) {
    int gw = res->ending.grid_width;
    int gh = res->ending.grid_height;
    int carpet_col = res->ending.carpet_column;
    int carpet_max = res->ending.carpet_length;

    // draw_cartoon_frame:4291-4294. Carpet length = frame (capped
    // at carpet_length), hero progress = frame - 5 (capped at 4).
    int bridge_len = frame;
    int hero_prog  = frame - 5;
    if (bridge_len > carpet_max) bridge_len = carpet_max;
    if (hero_prog > gh - 1) hero_prog = gh - 1;

    // Grass across the whole grid.
    for (int y = 0; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            draw_tile(sprites->end_grass, x, y, origin_x, origin_y, false);
        }
    }

    // Carpet column, grown from the bottom up.
    for (int i = 0; i < bridge_len; i++) {
        int y = (gh - 1) - i;
        draw_tile(sprites->end_carpet, carpet_col, y,
                  origin_x, origin_y, false);
    }

    // Hero. Draws once hero_prog >= 0 (i.e. from frame 5 onward).
    if (hero_prog >= 0) {
        int y = (gh - 1) - hero_prog;
        draw_tile(sprites->end_hero, carpet_col, y,
                  origin_x, origin_y, false);
    }

    // Troop border. layout: iterate troops, fill x=0..3 then x=5,
    // row by row. Troops in column 5 get their sprites horizontally
    // flipped . Troop frame index
    // is just `tick` — a 0..3 cycle.
    if (res->ending.troop_border) {
        int nt = troops_count();
        if (nt > 25) nt = 25;
        int x = 0, y = 0;
        for (int i = 0; i < nt && y < gh; i++) {
            bool flip = (x == gw - 1);
            int frame_idx = tick % 4;
            Texture2D tex = sprites->troop_anim[i][frame_idx];
            if (!tex.id) tex = sprites->troop_sprite[i];
            draw_tile(tex, x, y, origin_x, origin_y, flip);
            x++;
            if (x == carpet_col) x = carpet_col + 1;  // skip the carpet column
            if (x >= gw) { x = 0; y++; }
        }
    }
}

void run_end_cartoon(RenderTexture2D *rt,
                             const Resources *res,
                             const Sprites *sprites) {
    if (!rt || !res || !sprites) return;
    // Skip silently if the tile art isn't configured.
    if (!sprites->end_grass.id || !sprites->end_carpet.id ||
        !sprites->end_hero.id) return;

    int gw = res->ending.grid_width  > 0 ? res->ending.grid_width  : 6;
    int gh = res->ending.grid_height > 0 ? res->ending.grid_height : 5;
    int tps = res->ending.ticks_per_step > 0 ? res->ending.ticks_per_step : 2;
    int max_frames = res->ending.frame_count > 0 ? res->ending.frame_count : 10;

    // Center the grid horizontally; anchor at the map viewport's Y.
    int total_w = gw * CL_TILE_W;
    int origin_x = (CL_SCREEN_W - total_w) / 2;
    if (origin_x < 0) origin_x = 0;
    int origin_y = CL_MAP_Y;
    // If the grid overflows the map viewport vertically, still anchor at
    // the top — renders within the map area but openbounty' map is
    // 170 tall vs a native 5×34 = 170 grid that fits exactly at default.
    (void)gh;

    int tick = 0;
    int frame = 0;
    bool done = false;
    double last_advance = GetTime();
    double tick_interval = 0.08;   // ~12 ticks per second — 
                                   // ~60Hz timer advancing through tick 0..3.

    while (!WindowShouldClose() && !done) {
        if (any_key_pressed()) { done = true; break; }

        if (GetTime() - last_advance >= tick_interval) {
            last_advance = GetTime();
            tick++;
            // advances the animation frame every 2nd and 4th tick
            // of a 4-tick cycle (draw_cartoon_frame:4384). Emulate by
            // stepping every `tps` ticks.
            if (tick % tps == 0) {
                frame++;
                if (frame > max_frames) { done = true; break; }
            }
            if (tick > 3) tick = 0;
        }

        BeginTextureMode(*rt);
        ClearBackground(BLACK);
        draw_cartoon_frame(res, sprites, origin_x, origin_y, tick, frame);
        EndTextureMode();

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
                          (float)rt->texture.width,
                          -(float)rt->texture.height };
        Rectangle dst = { (float)dst_x, (float)dst_y,
                          (float)dst_w, (float)dst_h };
        DrawTexturePro(rt->texture, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

        screenshot_tick(*rt, "win");
    }
}
