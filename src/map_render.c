#include "map_render.h"
#include "layout.h"
#include "palette.h"
#include "tile_cache.h"
#include <stdio.h>

//  (game.c:1157): the hero is held centered in the
// 5x5 viewport except when the camera is clamped at a map edge. 2 tiles
// on each side of the hero are visible, plus the hero tile.
#define RADIUS  (CL_MAP_TILES_W / 2)   // 2

void map_render_draw(const Game *g, const Map *m, const Fog *f,
                      const Sprites *s) {
    if (!g || !m) return;

    // Compute the top-left visible tile (camera anchor). Clamp at map edges.
    int cam_x = g->position.x - RADIUS;
    int cam_y = g->position.y - RADIUS;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
    if (cam_x > m->width  - CL_MAP_TILES_W) cam_x = m->width  - CL_MAP_TILES_W;
    if (cam_y > m->height - CL_MAP_TILES_H) cam_y = m->height - CL_MAP_TILES_H;

    // Scissor so partial tiles at the map boundary don't spill.
    BeginScissorMode(CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H);

    // Fill unseen tiles as black.
    DrawRectangle(CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H, PAL_CLR(BLACK));

    for (int ty = 0; ty < CL_MAP_TILES_H; ty++) {
        for (int tx = 0; tx < CL_MAP_TILES_W; tx++) {
            int mx = cam_x + tx;
            int my = cam_y + ty;
            if (mx < 0 || my < 0 || mx >= m->width || my >= m->height) continue;
            if (!FogSeen(f, mx, my)) continue;
            const Tile *t = MapGetTile(m, mx, my);
            if (!t) continue;
            Texture2D tex = tile_cache_get(t->art);
            if (tex.id == 0) continue;
            Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
            int px = CL_MAP_X + tx * CL_TILE_W;
            int py = CL_MAP_Y + ty * CL_TILE_H;
            Rectangle dst = { (float)px, (float)py,
                              (float)CL_TILE_W, (float)CL_TILE_H };
            DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }

    // Hero (or boat). Centered on the hero's tile within the viewport.
    int hero_vx = g->position.x - cam_x;
    int hero_vy = g->position.y - cam_y;

    // Idle boat on the map (if the hero isn't currently in it).
    if (g->boat.has_boat && g->travel_mode == TRAVEL_WALK &&
        FogSeen(f, g->boat.x, g->boat.y)) {
        int bvx = g->boat.x - cam_x;
        int bvy = g->boat.y - cam_y;
        if (bvx >= 0 && bvy >= 0 &&
            bvx < CL_MAP_TILES_W && bvy < CL_MAP_TILES_H) {
            Texture2D bt = s->hero_boat[0];
            if (bt.id) {
                Rectangle bsrc = { 0, 0, (float)bt.width, (float)bt.height };
                Rectangle bdst = {
                    (float)(CL_MAP_X + bvx * CL_TILE_W),
                    (float)(CL_MAP_Y + bvy * CL_TILE_H),
                    (float)CL_TILE_W, (float)CL_TILE_H };
                DrawTexturePro(bt, bsrc, bdst, (Vector2){ 0, 0 }, 0.0f, WHITE);
            }
        }
    }

    // Hero sprite.
    Texture2D hsprite = (g->travel_mode == TRAVEL_BOAT)
        ? s->hero_boat[g->anim_frame & 3]
        : s->hero_walk[g->anim_frame & 3];
    if (hsprite.id) {
        Rectangle hsrc = {
            0, 0,
            (float)(g->position.facing_left ? -hsprite.width : hsprite.width),
            (float)hsprite.height
        };
        Rectangle hdst = {
            (float)(CL_MAP_X + hero_vx * CL_TILE_W),
            (float)(CL_MAP_Y + hero_vy * CL_TILE_H),
            (float)CL_TILE_W, (float)CL_TILE_H };
        DrawTexturePro(hsprite, hsrc, hdst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }

    // Fog-edge darkening gradient. For each seen tile, check cardinal neighbors
    // and draw fading black strips on edges facing unseen neighbors.
    for (int ty = 0; ty < CL_MAP_TILES_H; ty++) {
        for (int tx = 0; tx < CL_MAP_TILES_W; tx++) {
            int mx = cam_x + tx;
            int my = cam_y + ty;
            if (mx < 0 || my < 0 || mx >= m->width || my >= m->height) continue;
            if (!FogSeen(f, mx, my)) continue;
            int px = CL_MAP_X + tx * CL_TILE_W;
            int py = CL_MAP_Y + ty * CL_TILE_H;
            static const int NDX[4] = { 0, 0,-1, 1 };
            static const int NDY[4] = {-1, 1, 0, 0 };
            for (int d = 0; d < 4; d++) {
                int nx = mx + NDX[d];
                int ny = my + NDY[d];
                if (FogSeen(f, nx, ny)) continue;
                for (int k = 0; k < 3; k++) {
                    unsigned char alpha = (unsigned char)(128 >> k);
                    Color fog_strip = { 0, 0, 0, alpha };
                    int sx, sy, sw, sh;
                    if (NDY[d] == -1) {
                        sx = px; sy = py + k * 2; sw = CL_TILE_W; sh = 2;
                    } else if (NDY[d] == 1) {
                        sx = px; sy = py + CL_TILE_H - k * 2 - 2; sw = CL_TILE_W; sh = 2;
                    } else if (NDX[d] == -1) {
                        sx = px + k * 2; sy = py; sw = 2; sh = CL_TILE_H;
                    } else {
                        sx = px + CL_TILE_W - k * 2 - 2; sy = py; sw = 2; sh = CL_TILE_H;
                    }
                    DrawRectangle(sx, sy, sw, sh, fog_strip);
                }
            }
        }
    }

    EndScissorMode();
}
