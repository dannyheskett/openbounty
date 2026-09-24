#include "map_render.h"
#include "gfx.h"
#include "layout.h"
#include "present.h"
#include "palette.h"
#include "tables.h"     // troop_by_id (flying hero shows the lead troop)
#include "tile_cache.h"
#include "tilevar.h"
#include "tile.h"
#include <stdio.h>
#include <string.h>

// Viewport centering (OpenKB's game.c:1157), legacy: the hero is held
// centred in the viewport except when the camera is clamped at a map edge.
// Half the tile count (2 in the 5x5 original) on each side of the hero is
// visible, plus the hero tile.
#define RADIUS_X  (CL_MAP_TILES_W / 2)
#define RADIUS_Y  (CL_MAP_TILES_H / 2)

// The camera: the map cell drawn at (ox, oy), and the cells drawn round it --
// from x0..x1 and y0..y1 in cells relative to it (inclusive).
//
// Legacy: the whole-tile grid is the pane (5x5), the camera clamps at the
// map's edge.
//
// Modern: the hero's cell is centred across the pane and on the row that
// holds the pane's middle, its rows flush with the columns' tiles at the
// pane's top; every cell the pane shows is drawn, a part of one to the last
// pixel, on every side; and the camera never clamps -- past the world's edge
// the pane is dark, and the hero stays on the centre tile.
typedef struct { int cam_x, cam_y, ox, oy, x0, x1, y0, y1; } MapView;

static MapView map_view(const Game *g, const Map *m) {
    MapView v;
    if (!CL_IS_MODERN) {
        v.ox = CL_MAP_X;
        v.oy = CL_MAP_Y;
        v.cam_x = g->position.x - RADIUS_X;
        v.cam_y = g->position.y - RADIUS_Y;
        if (v.cam_x > m->width  - CL_MAP_TILES_W) v.cam_x = m->width  - CL_MAP_TILES_W;
        if (v.cam_y > m->height - CL_MAP_TILES_H) v.cam_y = m->height - CL_MAP_TILES_H;
        if (v.cam_x < 0) v.cam_x = 0;
        if (v.cam_y < 0) v.cam_y = 0;
        v.x0 = 0; v.x1 = CL_MAP_TILES_W - 1;
        v.y0 = 0; v.y1 = CL_MAP_TILES_H - 1;
        return v;
    }
    int hx = CL_MAP_X + (CL_MAP_W - CL_TILE_W) / 2;           // the hero's cell
    int hr = (CL_MAP_H / 2) / CL_TILE_H;                      // its row
    int hy = CL_MAP_Y + hr * CL_TILE_H;
    v.ox = hx; v.oy = hy;
    v.cam_x = g->position.x;
    v.cam_y = g->position.y;
    v.x0 = -((hx - CL_MAP_X + CL_TILE_W - 1) / CL_TILE_W);
    v.x1 = (CL_MAP_X + CL_MAP_W - (hx + CL_TILE_W) + CL_TILE_W - 1) / CL_TILE_W;
    v.y0 = -hr;
    v.y1 = (CL_MAP_Y + CL_MAP_H - (hy + CL_TILE_H) + CL_TILE_H - 1) / CL_TILE_H;
    return v;
}

// Where the last map_render_draw put the hero's cell (a message about the
// squares beside him outlines them).
static int s_hero_x, s_hero_y;

void map_render_last_hero_cell(int *x, int *y) {
    if (x) *x = s_hero_x;
    if (y) *y = s_hero_y;
}

void map_render_hero_cell(const Game *g, const Map *m, int *x, int *y) {
    if (!g || !m) return;
    MapView v = map_view(g, m);
    if (x) *x = v.ox + (g->position.x - v.cam_x) * CL_TILE_W;
    if (y) *y = v.oy + (g->position.y - v.cam_y) * CL_TILE_H;
}

void map_render_cell(const Map *m, int mx, int my, Rectangle dst) {
    const Tile *t = MapGetTile(m, mx, my);
    if (!t) return;
    // An object tile, or a landmark whose code names its own ground, is drawn
    // over that ground (ART-SPEC section 4): the plain terrain tile first,
    // then the object's art. A code with cosmetic variants draws one of them,
    // chosen per cell (src/tilevar.c); the ground under an object goes
    // through the same pick so it matches its neighbours.
    char va[TILE_ART_NAME_LEN];
    if (t->interactive != INTERACT_NONE || t->ground != t->art) {
        char ga[TILE_ART_NAME_LEN];
        const char *gart = t->ground ? TileGround(m, t)
                           : MapTerrainArt(m, TerrainName(t->terrain), ga, sizeof ga);
        Texture2D ground = tile_cache_get(tilevar_art(gart, mx, my, va, sizeof va));
        if (ground.id)
            gfx_texture_draw(ground, (Rectangle){ 0, 0, (float)ground.width, (float)ground.height },
                             dst, WHITE);
    }
    Texture2D tex = tile_cache_get(tilevar_art(TileArt(m, t), mx, my, va, sizeof va));
    if (tex.id) gfx_texture_draw(tex, (Rectangle){ 0, 0, (float)tex.width, (float)tex.height }, dst, WHITE);
}

// A wandering foe draws the generic wandering-army tile, the same as every
// other placed object. An earlier change (issue #9) drew the foe's lead troop
// sprite instead; reverted 2026-09-06 so the map reads as the original did,
// in every pack.
void map_render_draw(const Game *g, const Map *m, const Fog *f,
                      const Sprites *s) {
    if (!g || !m) return;

    const MapView v = map_view(g, m);
    const int cam_x = v.cam_x, cam_y = v.cam_y;
    s_hero_x = v.ox + (g->position.x - cam_x) * CL_TILE_W;
    s_hero_y = v.oy + (g->position.y - cam_y) * CL_TILE_H;

    // Scissor so partial tiles at the map boundary don't spill.
    // The scissor is in framebuffer pixels, not design pixels: a fixed
    // buffer renders at zoom, so scale the rect (present_get_zoom is 1 else).
    {
        int z = present_get_zoom();
        gfx_clip_begin(CL_MAP_X * z, CL_MAP_Y * z, CL_MAP_W * z, CL_MAP_H * z);
    }

    // Fill unseen tiles as black.
    gfx_rect(CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H, PAL_CLR(BLACK));

    const int ox = v.ox, oy = v.oy;
    for (int ty = v.y0; ty <= v.y1; ty++) {
        for (int tx = v.x0; tx <= v.x1; tx++) {
            int mx = cam_x + tx;
            int my = cam_y + ty;
            if (mx < 0 || my < 0 || mx >= m->width || my >= m->height) continue;
            if (!FogSeen(f, mx, my)) continue;
            Rectangle dst = { (float)(ox + tx * CL_TILE_W), (float)(oy + ty * CL_TILE_H),
                              (float)CL_TILE_W, (float)CL_TILE_H };
            map_render_cell(m, mx, my, dst);
        }
    }

    // Hero (or boat). Centered on the hero's tile within the viewport.
    int hero_vx = g->position.x - cam_x;
    int hero_vy = g->position.y - cam_y;

    // Idle boat on the map (if the hero isn't currently in it). Only draw it
    // when it sits in the zone the hero is currently viewing -- a boat left
    // behind in another zone (e.g. by a gate spell) must not bleed through.
    if (g->boat.has_boat && g->travel_mode == TRAVEL_WALK &&
        (g->boat.zone[0] == '\0' || strcmp(g->boat.zone, g->position.zone) == 0) &&
        FogSeen(f, g->boat.x, g->boat.y)) {
        int bvx = g->boat.x - cam_x;
        int bvy = g->boat.y - cam_y;
        if (bvx >= v.x0 && bvy >= v.y0 && bvx <= v.x1 && bvy <= v.y1) {
            // A boat the hero left behind sits still: frame 0, no facing.
            Texture2D bt = sprites_anim_tex(&s->hero_boat, OB_FACE_SOUTH,
                                            0, NULL);
            if (bt.id) {
                Rectangle bsrc = { 0, 0, (float)bt.width, (float)bt.height };
                Rectangle bdst = {
                    (float)(ox + bvx * CL_TILE_W),
                    (float)(oy + bvy * CL_TILE_H),
                    (float)CL_TILE_W, (float)CL_TILE_H };
                gfx_texture_draw(bt, bsrc, bdst, WHITE);
            }
        }
    }

    // Hero sprite: boat when sailing; the lead troop's sprite when flying (a
    // knight-on-horse gliding over mountains looked wrong -- flight shows what
    // the hero is riding); the walking hero otherwise. Flight falls back to
    // the walking hero when the army is empty or the lead troop has no sprite.
    // anim_frame is a free-running tick; each strip folds it onto its own
    // declared cycle, so a six-frame walk and a four-frame boat coexist.
    //
    // Whether the sprite gets mirrored is the animation's business, not the
    // hero's: a pack that authored four facings is drawn unflipped, while a
    // single-strip pack still mirrors when facing west exactly as before.
    // The hero holds still between steps, so pick the idle set when the pack
    // shipped one and the walk cycle isn't running.
    bool mirror = false;
    const char *cid = g->character.cls.id;
    const SpriteAnim *set = sprites_hero_anim(s, cid, 0);
    if (g->travel_mode != TRAVEL_BOAT && !g->anim_moving &&
        sprites_anim_present(sprites_hero_anim(s, cid, 1))) {
        set = sprites_hero_anim(s, cid, 1);
    }
    if (g->travel_mode == TRAVEL_BOAT) set = sprites_hero_anim(s, cid, 2);
    int tick = g->anim_frame;
    if (CL_IS_MODERN && g->travel_mode != TRAVEL_BOAT && !g->anim_moving)
        tick = sprites_stand(tick);
    Texture2D hsprite = sprites_anim_tex(set, g->position.facing,
                                         tick, &mirror);
    if (g->travel_mode != TRAVEL_BOAT && g->character.mount == MOUNT_FLY) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
            const TroopDef *t = troop_by_id(g->army[i].id);
            if (t && t->index >= 0 && t->index < s->troop_count) {
                Texture2D a =
                    sprites_strip(s->troop_anim[t->index],
                                  s->troop_anim_frames[t->index], tick);
                if (!a.id) a = s->troop_sprite[t->index];
                // Troop sprites are single-strip, so flight goes back to the
                // mirror regardless of what the hero's own art declares.
                if (a.id) { hsprite = a; mirror = (g->position.facing == OB_FACE_WEST); }
            }
            break;   // first non-empty slot only
        }
    }
    if (hsprite.id) {
        Rectangle hsrc = {
            0, 0,
            (float)(mirror ? -hsprite.width : hsprite.width),
            (float)hsprite.height
        };
        Rectangle hdst = {
            (float)(ox + hero_vx * CL_TILE_W),
            (float)(oy + hero_vy * CL_TILE_H),
            (float)CL_TILE_W, (float)CL_TILE_H };
        gfx_texture_draw(hsprite, hsrc, hdst, WHITE);
    }

    // Fog-edge darkening gradient. For each seen tile, check cardinal neighbors
    // and draw fading black strips on edges facing unseen neighbors. Three
    // strips, each the same share of the tile in every pack: 2 px of the
    // original's 48x34 tile (1/24 of its width, 1/17 of its height), so Rome's
    // 96 px tile fades as far in as King's Bounty's does.
    const int bw = CL_TILE_W / 24 > 0 ? CL_TILE_W / 24 : 1;
    const int bh = CL_TILE_H / 17 > 0 ? CL_TILE_H / 17 : 1;
    for (int ty = v.y0; ty <= v.y1; ty++) {
        for (int tx = v.x0; tx <= v.x1; tx++) {
            int mx = cam_x + tx;
            int my = cam_y + ty;
            if (mx < 0 || my < 0 || mx >= m->width || my >= m->height) continue;
            if (!FogSeen(f, mx, my)) continue;
            int px = ox + tx * CL_TILE_W;
            int py = oy + ty * CL_TILE_H;
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
                        sx = px; sy = py + k * bh; sw = CL_TILE_W; sh = bh;
                    } else if (NDY[d] == 1) {
                        sx = px; sy = py + CL_TILE_H - k * bh - bh; sw = CL_TILE_W; sh = bh;
                    } else if (NDX[d] == -1) {
                        sx = px + k * bw; sy = py; sw = bw; sh = CL_TILE_H;
                    } else {
                        sx = px + CL_TILE_W - k * bw - bw; sy = py; sw = bw; sh = CL_TILE_H;
                    }
                    gfx_rect(sx, sy, sw, sh, fog_strip);
                }
            }
        }
    }

    gfx_clip_end();
}
