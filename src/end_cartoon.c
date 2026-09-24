#include "end_cartoon.h"
#include "gfx.h"
#include "input_host.h"
#include "game.h"
#include "frame_host.h"
#include "layout.h"
#include "present.h"
#include "screenshot.h"
#include "ui.h"
#include "tables.h"
#include "tile_cache.h"
#include "modern/page.h"
#include "modern/uikit.h"
#include <stdio.h>
#include <string.h>

//  + draw_cartoon_frame
// (OpenKB's game.c:4281). The scene is a grid of CL_TILE_W x CL_TILE_H cells:
//   - grass backdrop across the whole grid
//   - a "carpet" column that grows upward one cell per frame
//   - a mounted hero advancing along the carpet starting at frame 5
//   - troops filling the non-carpet cells with their 2-frame walk cycle
//     (flipped horizontally for cells to the right of the carpet column)
//
// The grid is centered horizontally on the 320-wide display and placed at
// the map viewport's Y origin. Frames advance every `ticks_per_step`
// ticks; any keypress short-circuits the animation.

// Legacy's own any-key test, as the original's cartoon read it; modern reads
// the one any-key check (ui_any_key_pressed), a tap included.
static bool legacy_any_key_pressed(void) {
    int k = input_get_key_pressed();
    while (k != 0) {
        if (k != KEY_LEFT_SHIFT && k != KEY_RIGHT_SHIFT &&
            k != KEY_LEFT_CONTROL && k != KEY_RIGHT_CONTROL &&
            k != KEY_LEFT_ALT && k != KEY_RIGHT_ALT) return true;
        k = input_get_key_pressed();
    }
    return false;
}

// A cell of the grid: one tile (legacy), or -- the cartoon being full-bleed
// art -- the largest whole multiple of one at which the grid fits the screen,
// by the rule every full-bleed picture follows (ui_fit_scale).
static int s_cell_w = 48, s_cell_h = 34;

// Size the cells for this screen and place the grid: legacy centred on the
// map pane as it always was; modern centred on the whole screen, in the
// frame's lattice where the screen is larger.
static void cartoon_place(int gw, int gh, int *origin_x, int *origin_y) {
    s_cell_w = CL_TILE_W;
    s_cell_h = CL_TILE_H;
    if (CL_IS_MODERN) {
        int k = ui_fit_scale(gw * CL_TILE_W, gh * CL_TILE_H, CL_SCREEN_W, CL_SCREEN_H);
        s_cell_w = CL_TILE_W * k;
        s_cell_h = CL_TILE_H * k;
        *origin_x = (CL_SCREEN_W - gw * s_cell_w) / 2;
        *origin_y = (CL_SCREEN_H - gh * s_cell_h) / 2;
        return;
    }
    *origin_x = CL_MAP_X + (CL_MAP_W - gw * CL_TILE_W) / 2;
    *origin_y = CL_MAP_Y + (CL_MAP_H - gh * CL_TILE_H) / 2;
    if (*origin_x < CL_MAP_X) *origin_x = CL_MAP_X;
    if (*origin_y < CL_MAP_Y) *origin_y = CL_MAP_Y;
}

static void draw_tile(Texture2D tex, int gx, int gy, int origin_x, int origin_y,
                      bool flip_h) {
    if (!tex.id) return;
    int dx = origin_x + gx * s_cell_w;
    int dy = origin_y + gy * s_cell_h;
    Rectangle src = {
        0, 0,
        flip_h ? -(float)tex.width : (float)tex.width,
        (float)tex.height
    };
    Rectangle dst = {
        (float)dx, (float)dy,
        (float)s_cell_w, (float)s_cell_h
    };
    gfx_texture_draw(tex, src, dst, WHITE);
}

static void draw_cartoon_frame(const Resources *res, const Sprites *sprites,
                               Texture2D grass, Texture2D hero,
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
            draw_tile(grass, x, y, origin_x, origin_y, false);
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
        draw_tile(hero, carpet_col, y, origin_x, origin_y, false);
    }

    // Troop border. layout: iterate troops, fill x=0..3 then x=5,
    // row by row. Troops in column 5 get their sprites horizontally
    // flipped . Troop frame index
    // is `tick` folded onto whatever cycle length the troop declares.
    if (res->ending.troop_border) {
        // Every troop the pack declares, until the grid runs out of rows.
        int nt = sprites->troop_count;
        int x = 0, y = 0;
        for (int i = 0; i < nt && y < gh; i++) {
            bool flip = (x == gw - 1);
            // Modern: standing at the one idle pace; legacy: the cartoon's tick.
            Texture2D tex = sprites_strip(sprites->troop_anim[i], sprites->troop_anim_frames[i],
                                          CL_IS_MODERN ? sprites_stand((int)(ui_anim_time() * UK_IDLE_FPS))
                                                       : tick);
            if (!tex.id) tex = sprites->troop_sprite[i];
            draw_tile(tex, x, y, origin_x, origin_y, flip);
            x++;
            if (x == carpet_col) x = carpet_col + 1;  // skip the carpet column
            if (x >= gw) { x = 0; y++; }
        }
    }
}

// --gallery: draw one frame of the cartoon (frame 0..frame_count) into rt.
void end_cartoon_gallery_draw(RenderTexture2D *rt, const Resources *res,
                              const Sprites *sprites, const struct Game *game, int frame) {
    Texture2D hero = sprites_end_hero(sprites, game ? game->character.cls.id : NULL);
    Texture2D grass = sprites->end_grass;
    if (!grass.id) grass = tile_cache_get("grass");
    int gw = res->ending.grid_width  > 0 ? res->ending.grid_width  : 6;
    int gh = res->ending.grid_height > 0 ? res->ending.grid_height : 5;
    present_refit(rt);
    int origin_x, origin_y;
    cartoon_place(gw, gh, &origin_x, &origin_y);
    present_begin(rt);
    gfx_clear(BLACK);
    if (CL_IS_MODERN) page_art_margins((ML_Rect){ origin_x, origin_y, gw * s_cell_w, gh * s_cell_h });
    draw_cartoon_frame(res, sprites, grass, hero, origin_x, origin_y, 0, frame);
    present_end();
}

void run_end_cartoon(RenderTexture2D *rt,
                             const Resources *res,
                             const Sprites *sprites,
                             const struct Game *game) {
    if (!rt || !res || !sprites) return;
    // The hero tile is the player's class's own when the pack declares one.
    Texture2D hero = sprites_end_hero(sprites, game ? game->character.cls.id : NULL);
    // The grass backdrop is the pack's ending.grass_tile when declared, else
    // the map's own grass tile, so a pack need not ship the tile twice.
    Texture2D grass = sprites->end_grass;
    if (!grass.id) grass = tile_cache_get("grass");
    // Skip silently if the tile art isn't configured.
    if (!grass.id || !sprites->end_carpet.id || !hero.id) return;

    int gw = res->ending.grid_width  > 0 ? res->ending.grid_width  : 6;
    int gh = res->ending.grid_height > 0 ? res->ending.grid_height : 5;
    int tps = res->ending.ticks_per_step > 0 ? res->ending.ticks_per_step : 2;
    int max_frames = res->ending.frame_count > 0 ? res->ending.frame_count : 10;

    int tick = 0;
    int frame = 0;
    bool done = false;
    double last_advance = ui_anim_time();
    double tick_interval = 0.08;   // ~12 ticks per second -- 
                                   // ~60Hz timer advancing through tick 0..3.

    while (!frame_host_should_close() && !done) {
        // Modern: a tap counts too, as on every other any-key screen.
        if (CL_IS_MODERN ? ui_any_key_pressed() : legacy_any_key_pressed()) { done = true; break; }

        if (ui_anim_time() - last_advance >= tick_interval) {
            last_advance = ui_anim_time();
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

        // Derived per frame, not once: present_refit can resize the screen
        // under us when the window changes, which would leave the origin
        // pointing at the old geometry.
        present_refit(rt);
        int origin_x, origin_y;
        cartoon_place(gw, gh, &origin_x, &origin_y);

        present_begin(rt);
        gfx_clear(BLACK);
        if (CL_IS_MODERN) page_art_margins((ML_Rect){ origin_x, origin_y, gw * s_cell_w, gh * s_cell_h });
        draw_cartoon_frame(res, sprites, grass, hero, origin_x, origin_y, tick, frame);
        present_end();

        present_scaled(*rt);
        frame_host_end_frame();

        screenshot_tick(*rt, "win");
    }
}
