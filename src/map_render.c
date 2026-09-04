#include "map_render.h"
#include "layout.h"
#include "palette.h"
#include "tables.h"     // troop_by_id (flying hero shows the lead troop)
#include "tile_cache.h"
#include <stdio.h>
#include <string.h>

// Viewport centering (OpenKB's game.c:1157): the hero is held centered in the
// 5x5 viewport except when the camera is clamped at a map edge. 2 tiles
// on each side of the hero are visible, plus the hero tile.
#define RADIUS  (CL_MAP_TILES_W / 2)   // 2

// The world-map sprite for a wandering foe: its lead troop (first non-empty
// garrison stack), animated, so a stack of ogres/skeletons/archers shows that
// creature instead of a single generic footman (issue #9). Returns {0} when
// the foe or its troop can't be resolved, so the caller falls back to the
// generic wandering-army art.
static Texture2D foe_map_sprite(const Game *g, const Sprites *s,
                                const char *foe_id, int frame) {
    if (!g || !foe_id || !foe_id[0]) return (Texture2D){ 0 };
    const FoeState *f = GameFindFoeConst(g, foe_id);
    if (!f) return (Texture2D){ 0 };
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!f->garrison[i].id[0] || f->garrison[i].count <= 0) continue;
        const TroopDef *t = troop_by_id(f->garrison[i].id);
        if (!t || t->index < 0 || t->index >= 25) return (Texture2D){ 0 };
        Texture2D a =
            s->troop_anim[t->index][sprites_frame(frame,
                                                 s->troop_anim_frames[t->index])];
        if (!a.id) a = s->troop_sprite[t->index];
        return a;
    }
    return (Texture2D){ 0 };
}

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
    // A modern viewport grows with the window and can end up wider than the
    // map itself, which makes the clamp above negative. Pin it back to the
    // origin: the surplus tiles fall outside the map and are simply not drawn.
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;

    // Scissor so partial tiles at the map boundary don't spill.
    BeginScissorMode(CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H);

    // Fill unseen tiles as black. This also blacks out the sub-tile slack: in
    // modern mode the pane is the whole interior of the frame, which is rarely
    // an exact multiple of the tile, and a partial tile is never drawn.
    DrawRectangle(CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H, PAL_CLR(BLACK));

    // Centre the whole-tile grid in the pane, so the leftover splits evenly
    // either side and the hero still sits at the middle of the window.
    const int ox = CL_MAP_X + (CL_MAP_W - CL_MAP_TILES_W * CL_TILE_W) / 2;
    const int oy = CL_MAP_Y + (CL_MAP_H - CL_MAP_TILES_H * CL_TILE_H) / 2;

    for (int ty = 0; ty < CL_MAP_TILES_H; ty++) {
        for (int tx = 0; tx < CL_MAP_TILES_W; tx++) {
            int mx = cam_x + tx;
            int my = cam_y + ty;
            if (mx < 0 || my < 0 || mx >= m->width || my >= m->height) continue;
            if (!FogSeen(f, mx, my)) continue;
            const Tile *t = MapGetTile(m, mx, my);
            if (!t) continue;
            int px = ox + tx * CL_TILE_W;
            int py = oy + ty * CL_TILE_H;
            Rectangle dst = { (float)px, (float)py,
                              (float)CL_TILE_W, (float)CL_TILE_H };
            // An object tile is drawn over its ground (ART-SPEC section 4):
            // the plain terrain tile first, then the object's art. A
            // transparent object -- a troop-override foe sprite (issue #9),
            // the 1x1 castle -- then stands on the ground it occupies instead
            // of the black map fill; an opaque object covers the ground
            // completely, so nothing that drew before this draws differently.
            if (t->interactive != INTERACT_NONE) {
                Texture2D ground = tile_cache_get(TerrainName(t->terrain));
                if (ground.id) {
                    Rectangle gsrc = { 0, 0, (float)ground.width,
                                       (float)ground.height };
                    DrawTexturePro(ground, gsrc, dst, (Vector2){ 0, 0 },
                                   0.0f, WHITE);
                }
            }
            Texture2D tex = (Texture2D){ 0 };
            if (t->interactive == INTERACT_FOE)
                tex = foe_map_sprite(g, s, t->id, g->anim_frame);
            if (tex.id == 0) tex = tile_cache_get(t->art);
            if (tex.id == 0) continue;
            Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
            DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
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
        if (bvx >= 0 && bvy >= 0 &&
            bvx < CL_MAP_TILES_W && bvy < CL_MAP_TILES_H) {
            // A boat the hero left behind sits still: frame 0, no facing.
            Texture2D bt = sprites_anim_tex(&s->hero_boat, OB_FACE_SOUTH,
                                            0, NULL);
            if (bt.id) {
                Rectangle bsrc = { 0, 0, (float)bt.width, (float)bt.height };
                Rectangle bdst = {
                    (float)(ox + bvx * CL_TILE_W),
                    (float)(oy + bvy * CL_TILE_H),
                    (float)CL_TILE_W, (float)CL_TILE_H };
                DrawTexturePro(bt, bsrc, bdst, (Vector2){ 0, 0 }, 0.0f, WHITE);
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
    const SpriteAnim *set = &s->hero_walk;
    if (g->travel_mode != TRAVEL_BOAT && !g->anim_moving &&
        sprites_anim_present(&s->hero_idle)) {
        set = &s->hero_idle;
    }
    if (g->travel_mode == TRAVEL_BOAT) set = &s->hero_boat;
    Texture2D hsprite = sprites_anim_tex(set, g->position.facing,
                                         g->anim_frame, &mirror);
    if (g->travel_mode != TRAVEL_BOAT && g->character.mount == MOUNT_FLY) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
            const TroopDef *t = troop_by_id(g->army[i].id);
            if (t && t->index >= 0 && t->index < 25) {
                Texture2D a =
                    s->troop_anim[t->index]
                                 [sprites_frame(g->anim_frame,
                                                s->troop_anim_frames[t->index])];
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
