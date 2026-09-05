// Maps and Objects modes (GB-200..GB-215).
//
// One canvas serves both: terrain is painted underneath, objects are dragged on
// top, and each mode decides which of the two the mouse is talking to. Keeping
// them on one canvas is deliberate -- placement is a spatial decision made
// against the terrain, so hiding one while editing the other would be wrong.
//
// Terrain edits go through the furnish pass on every change (REQ-229), so the
// canvas always shows the finished map rather than raw base terrain.

#include "gb_ui.h"

#include "raygui.h"
#include "tile_cache.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const int TW = 48, TH = 34;

static const Color TERRAIN_COL[TERRAIN_COUNT] = {
    {  72, 132,  48, 255 }, {  28,  78,  32, 255 }, { 120, 108,  96, 255 },
    {  36,  68, 140, 255 }, { 198, 176, 104, 255 },
};
static const char *TERRAIN_NAME[TERRAIN_COUNT] = {
    "Grass", "Forest", "Mountain", "Water", "Desert"
};
static const Color OBJ_COL[GB_OBJ_KINDS] = {
    { 240, 220,  80, 255 },  // town
    { 230,  90,  70, 255 },  // castle
    { 250, 250, 250, 255 },  // chest
    { 170, 140,  90, 255 },  // sign
    { 210, 120, 210, 255 },  // dwelling
    { 255,  40,  40, 255 },  // army
    {  90, 220, 255, 255 },  // hero spawn
    {  90, 160, 255, 255 },  // home spawn
    { 190, 120, 255, 255 },  // alcove
};

// --- helpers ------------------------------------------------------------------

static Vector2 screen_to_tile(const GbMapView *v, Vector2 s) {
    return (Vector2){ (s.x - v->origin.x - v->pan.x) / (TW * v->zoom),
                      (s.y - v->origin.y - v->pan.y) / (TH * v->zoom) };
}

static bool in_grid(const MapGrid *g, int x, int y) {
    return x >= 0 && y >= 0 && x < g->w && y < g->h;
}

// Capture a rectangle so an edit can be undone. Painting captures the brush
// footprint; bulk tools capture the whole grid, which is why the undo layer
// distinguishes the two.
static void snap_rect(const MapGrid *g, int x, int y, int w, int h,
                      MapCell *out) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int gx = x + i, gy = y + j;
            out[j * w + i] = in_grid(g, gx, gy) ? g->cell[gy][gx]
                                                : (MapCell){ TERRAIN_GRASS, -1, 0 };
        }
}

// --- terrain tools ------------------------------------------------------------

// A whole stroke is ONE undo step. Pushing per frame meant Ctrl+Z rubbed out
// a few tiles of a drag rather than the drag, which is not what anyone means
// by undo. The grid is captured when the button goes down and the single
// entry is committed when it comes up.
static MapCell stroke_before[MAPEDIT_MAX_W * MAPEDIT_MAX_H];
static bool    stroke_open;

static void stroke_begin(GbMapView *v, const MapGrid *g) {
    (void)v;
    snap_rect(g, 0, 0, g->w, g->h, stroke_before);
    stroke_open = true;
}

static void stroke_commit(GbMapView *v, const MapGrid *g, GbUndo *undo,
                          const char *label) {
    if (!stroke_open) return;
    stroke_open = false;
    static MapCell after[MAPEDIT_MAX_W * MAPEDIT_MAX_H];
    snap_rect(g, 0, 0, g->w, g->h, after);
    // Nothing actually changed: do not leave an empty step in the history.
    if (memcmp(stroke_before, after,
               (size_t)g->w * g->h * sizeof(MapCell)) == 0) return;
    gb_undo_push_tiles(undo, label, v->zone, 0, 0, g->w, g->h,
                       stroke_before, after);
}

static void paint_at(GbMapView *v, MapGrid *g, int cx, int cy) {
    int r = v->brush / 2;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            int gx = cx + i, gy = cy + j;
            if (!in_grid(g, gx, gy)) continue;
            if (g->cell[gy][gx].terrain == v->brush_terrain &&
                !g->cell[gy][gx].decor) continue;
            g->cell[gy][gx].terrain = v->brush_terrain;
            g->cell[gy][gx].decor = 0;      // painting clears a decorative tile
            g->dirty = true;
        }
}

static void flood_at(GbMapView *v, MapGrid *g, int sx, int sy) {
    if (!in_grid(g, sx, sy)) return;
    Terrain from = g->cell[sy][sx].terrain;
    if (from == v->brush_terrain) return;

    static int stack[MAPEDIT_MAX_W * MAPEDIT_MAX_H][2];
    int top = 0;
    stack[top][0] = sx; stack[top][1] = sy; top++;
    while (top > 0) {
        top--;
        int x = stack[top][0], y = stack[top][1];
        if (!in_grid(g, x, y) || g->cell[y][x].terrain != from) continue;
        g->cell[y][x].terrain = v->brush_terrain;
        g->cell[y][x].decor = 0;
        const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4 && top < MAPEDIT_MAX_W * MAPEDIT_MAX_H; i++) {
            stack[top][0] = x + dx[i]; stack[top][1] = y + dy[i]; top++;
        }
    }
    g->dirty = true;
}

static void rect_fill(GbMapView *v, MapGrid *g, int ax, int ay, int bx, int by) {
    int x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax;
    int y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w <= 0 || h <= 0) return;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (in_grid(g, x, y)) {
                g->cell[y][x].terrain = v->brush_terrain;
                g->cell[y][x].decor = 0;
            }
    g->dirty = true;
}

// --- drawing ------------------------------------------------------------------

static void draw_terrain(GbMapView *v, const MapGrid *g) {
    float tw = TW * v->zoom, th = TH * v->zoom;
    // Only the visible window: a 64x128 zone must cost nothing to pan.
    int x0 = (int)((-v->pan.x) / tw) - 1;
    int y0 = (int)((-v->pan.y) / th) - 1;
    int x1 = x0 + (int)(v->size.x / tw) + 3;
    int y1 = y0 + (int)(v->size.y / th) + 3;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const MapCell *c = &g->cell[y][x];
            Rectangle dst = { v->origin.x + v->pan.x + x * tw,
                              v->origin.y + v->pan.y + y * th, tw + 1, th + 1 };
            if (v->flat_view) {
                DrawRectangleRec(dst, TERRAIN_COL[c->terrain]);
                continue;
            }
            // Terrain art lives under the zone's tile set folder when it
            // declares one, exactly as engine/map.c MapTerrainArt resolves it.
            char art[64];
            snprintf(art, sizeof art, "%s%s%s", g->tile_set,
                     g->tile_set[0] ? "/" : "",
                     mapedit_art_for(c->terrain, c->variant));
            Texture2D t = tile_cache_get(art);
            if (t.id) {
                Rectangle src = { 0, 0, (float)t.width, (float)t.height };
                DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0, WHITE);
            } else {
                DrawRectangleRec(dst, TERRAIN_COL[c->terrain]);
            }
        }
    }
    if (v->show_grid && v->zoom > 0.35f) {
        Color gc = { 255, 255, 255, 36 };
        for (int x = x0; x <= x1; x++)
            DrawLineV((Vector2){ v->origin.x + v->pan.x + x * tw,
                                 v->origin.y + v->pan.y + y0 * th },
                      (Vector2){ v->origin.x + v->pan.x + x * tw,
                                 v->origin.y + v->pan.y + y1 * th }, gc);
        for (int y = y0; y <= y1; y++)
            DrawLineV((Vector2){ v->origin.x + v->pan.x + x0 * tw,
                                 v->origin.y + v->pan.y + y * th },
                      (Vector2){ v->origin.x + v->pan.x + x1 * tw,
                                 v->origin.y + v->pan.y + y * th }, gc);
    }
}

static void draw_objects(GbMapView *v, const GbObjectList *L, int selected) {
    float tw = TW * v->zoom, th = TH * v->zoom;
    for (int i = 0; i < L->count; i++) {
        const GbObject *o = &L->item[i];
        float px = v->origin.x + v->pan.x + o->x * tw;
        float py = v->origin.y + v->pan.y + o->y * th;
        if (px < v->origin.x - tw || py < v->origin.y - th ||
            px > v->origin.x + v->size.x || py > v->origin.y + v->size.y) continue;

        if (o->kind == GB_OBJ_CASTLE) {
            // Show the real footprint, not a dot: a castle that does not fit
            // is the commonest placement mistake.
            int fx, fy, fw, fh;
            gb_castle_footprint(gb_castle_is_single(o->node), o->x, o->y,
                                &fx, &fy, &fw, &fh);
            Rectangle r = { v->origin.x + v->pan.x + fx * tw,
                            v->origin.y + v->pan.y + fy * th,
                            fw * tw, fh * th };
            DrawRectangleRec(r, (Color){ 230, 90, 70, 60 });
            DrawRectangleLinesEx(r, 1, OBJ_COL[GB_OBJ_CASTLE]);
        }
        Rectangle dot = { px + tw * 0.25f, py + th * 0.25f, tw * 0.5f, th * 0.5f };
        DrawRectangleRec(dot, OBJ_COL[o->kind]);
        DrawRectangleLinesEx(dot, 1, BLACK);
        // A field of unlabelled dots tells the author nothing about which is
        // which, so name them once there is room to read it.
        if (v->zoom > 0.6f && o->kind <= GB_OBJ_CASTLE)
            DrawText(o->label, (int)px, (int)(py + th * 0.8f), 9,
                     (Color){ 230, 230, 240, 220 });
        if (i == selected) {
            DrawRectangleLinesEx((Rectangle){ px, py, tw, th }, 2, YELLOW);
            if (v->zoom > 0.4f)
                DrawText(o->label, (int)px + 2, (int)(py - 12), 10, YELLOW);
        }
    }
}

static void draw_minimap(GbMapView *v, const MapGrid *g, Rectangle box) {
    // A tall zone (64x128) is half as wide as the box, so centre the drawn
    // area inside it rather than leaving a black slab beside the map.
    float s = box.width / g->w;
    float sy = box.height / g->h;
    if (sy < s) s = sy;
    float dw = g->w * s, dh = g->h * s;
    Rectangle inner = { box.x + (box.width - dw) / 2,
                        box.y + (box.height - dh) / 2, dw, dh };
    DrawRectangleRec(inner, (Color){ 10, 10, 14, 255 });
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            DrawRectangle((int)(inner.x + x * s), (int)(inner.y + y * s),
                          (int)(s + 1), (int)(s + 1),
                          TERRAIN_COL[g->cell[y][x].terrain]);

    // Viewport rectangle, clamped to the map so it cannot spill outside.
    float tw = TW * v->zoom, th = TH * v->zoom;
    float vx = (-v->pan.x / tw) * s, vy = (-v->pan.y / th) * s;
    float vw = (v->size.x / tw) * s, vh = (v->size.y / th) * s;
    if (vx < 0) { vw += vx; vx = 0; }
    if (vy < 0) { vh += vy; vy = 0; }
    if (vx + vw > dw) vw = dw - vx;
    if (vy + vh > dh) vh = dh - vy;
    if (vw > 0 && vh > 0)
        DrawRectangleLinesEx((Rectangle){ inner.x + vx, inner.y + vy, vw, vh },
                             1, YELLOW);
    DrawRectangleLinesEx(inner, 1, (Color){ 70, 70, 80, 255 });
    box = inner;

    if (CheckCollisionPointRec(GetMousePosition(), box) &&
        IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        Vector2 m = GetMousePosition();
        float tx = (m.x - box.x) / s, ty = (m.y - box.y) / s;
        v->pan.x = -(tx * tw) + v->size.x / 2;
        v->pan.y = -(ty * th) + v->size.y / 2;
    }
}

// --- public -------------------------------------------------------------------

void gb_mapview_init(GbMapView *v) {
    memset(v, 0, sizeof *v);
    v->zoom = 0.5f;
    v->brush = 1;
    v->brush_terrain = TERRAIN_WATER;
    v->show_grid = true;
    v->show_objects = true;
    v->selected = -1;
    v->tool = GB_TOOL_VIEW;      // open in a mode that cannot change anything
    v->inspect_x = v->inspect_y = -1;
}

// Gather everything about one tile. Reads the grid and the object list rather
// than keeping its own copy, so the inspector cannot disagree with the map.
void gb_inspect_tile(GbTileInfo *out, const MapGrid *g, const GbObjectList *L,
                     const Resources *res, const char *zone, int x, int y) {
    memset(out, 0, sizeof *out);
    if (!g || x < 0 || y < 0 || x >= g->w || y >= g->h) return;
    const MapCell *c = &g->cell[y][x];
    out->valid   = true;
    out->x = x; out->y = y;
    out->zone    = zone;
    out->terrain = c->terrain;
    out->variant = c->variant;
    out->decorative = (c->decor != 0);
    out->art     = mapedit_art_for(c->terrain, c->variant);
    out->code    = c->decor ? c->decor : mapedit_code_for(res, c->terrain,
                                                          c->variant);
    // Walkability comes from the pack's own tile_codes entry, not from a
    // guess about the terrain -- a bridge is grass that water rules ignore.
    unsigned char b = (unsigned char)out->code;
    if (b < RES_TILE_CODE_COUNT && res->tile_codes[b].present) {
        out->blocks_foot = res->tile_codes[b].blocks_foot;
        out->is_bridge   = res->tile_codes[b].is_bridge;
    }
    if (L) {
        for (int i = 0; i < L->count && out->objects < GB_INSPECT_MAX_OBJ; i++) {
            if (L->item[i].x != x || L->item[i].y != y) continue;
            out->obj[out->objects++] = &L->item[i];
        }
        // A 3x2 castle has five wall tiles besides its gate, so a tile can be
        // inside one without being its gate. Report that too, or the author
        // wonders why the tile is blocked. A 1x1 castle has no such tiles.
        for (int i = 0; i < L->count && out->objects < GB_INSPECT_MAX_OBJ; i++) {
            if (L->item[i].kind != GB_OBJ_CASTLE) continue;
            if (L->item[i].x == x && L->item[i].y == y) continue;
            int fx, fy, fw, fh;
            gb_castle_footprint(gb_castle_is_single(L->item[i].node),
                                L->item[i].x, L->item[i].y, &fx, &fy, &fw, &fh);
            if (x >= fx && x < fx + fw && y >= fy && y < fy + fh)
                out->obj[out->objects++] = &L->item[i];
        }
    }
}

void gb_mapview_frame(GbMapView *v, MapGrid *g, GbObjectList *objs,
                      GbUndo *undo, bool objects_mode, cJSON *doc,
                      const char *zone_id, char *status, size_t status_sz) {
    Vector2 m = GetMousePosition();
    bool over = CheckCollisionPointRec(m, (Rectangle){ v->origin.x, v->origin.y,
                                                       v->size.x, v->size.y });

    // zoom about the cursor
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && over) {
        float old = v->zoom;
        v->zoom += wheel * 0.1f;
        if (v->zoom < 0.12f) v->zoom = 0.12f;
        if (v->zoom > 2.0f)  v->zoom = 2.0f;
        v->pan.x = m.x - v->origin.x - (m.x - v->origin.x - v->pan.x) * (v->zoom / old);
        v->pan.y = m.y - v->origin.y - (m.y - v->origin.y - v->pan.y) * (v->zoom / old);
    }
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 d = GetMouseDelta();
        v->pan.x += d.x; v->pan.y += d.y;
    }

    Vector2 t = screen_to_tile(v, m);
    int cx = (int)floorf(t.x), cy = (int)floorf(t.y);

    BeginScissorMode((int)v->origin.x, (int)v->origin.y,
                     (int)v->size.x, (int)v->size.y);
    draw_terrain(v, g);

    if (!objects_mode) {
        // --- terrain editing ---
        if (over && in_grid(g, cx, cy)) {
            if (v->tool == GB_TOOL_VIEW && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                v->inspect_x = cx;
                v->inspect_y = cy;
                // Select any object here too, so the two panels agree.
                v->selected = -1;
                for (int i = objs->count - 1; i >= 0; i--)
                    if (objs->item[i].x == cx && objs->item[i].y == cy) {
                        v->selected = i;
                        break;
                    }
            } else if (v->tool == GB_TOOL_PAINT && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) stroke_begin(v, g);
                paint_at(v, g, cx, cy);
                mapedit_despeckle(g);
                mapedit_furnish(g, NULL);
            } else if (v->tool == GB_TOOL_FILL &&
                       IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                stroke_begin(v, g);
                flood_at(v, g, cx, cy);
                mapedit_despeckle(g);
                mapedit_furnish(g, NULL);
                stroke_commit(v, g, undo, "flood fill");
            } else if (v->tool == GB_TOOL_RECT) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    v->drag_from = (Vector2){ (float)cx, (float)cy };
                    v->dragging = true;
                    stroke_begin(v, g);
                } else if (v->dragging && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                    rect_fill(v, g, (int)v->drag_from.x, (int)v->drag_from.y,
                              cx, cy);
                    mapedit_despeckle(g);
                    mapedit_furnish(g, NULL);
                    stroke_commit(v, g, undo, "rectangle");
                    v->dragging = false;
                }
            } else if (v->tool == GB_TOOL_PICK &&
                       IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                v->brush_terrain = g->cell[cy][cx].terrain;
                snprintf(status, status_sz, "Picked %s",
                         TERRAIN_NAME[v->brush_terrain]);
            }
        }
        if (v->tool == GB_TOOL_PAINT && IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
            stroke_commit(v, g, undo, "paint");

        if (v->dragging && v->tool == GB_TOOL_RECT) {
            float tw = TW * v->zoom, th = TH * v->zoom;
            int x0 = (int)fminf(v->drag_from.x, (float)cx);
            int y0 = (int)fminf(v->drag_from.y, (float)cy);
            int x1 = (int)fmaxf(v->drag_from.x, (float)cx);
            int y1 = (int)fmaxf(v->drag_from.y, (float)cy);
            DrawRectangleLinesEx((Rectangle){ v->origin.x + v->pan.x + x0 * tw,
                                              v->origin.y + v->pan.y + y0 * th,
                                              (x1 - x0 + 1) * tw,
                                              (y1 - y0 + 1) * th }, 2, YELLOW);
        }
    }

    if (v->show_objects || objects_mode) draw_objects(v, objs, v->selected);

    if (objects_mode && over) {
        // Click selects the nearest object on the tile; drag moves it.
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            v->selected = -1;
            for (int i = objs->count - 1; i >= 0; i--) {
                if (objs->item[i].x == cx && objs->item[i].y == cy) {
                    v->selected = i;
                    v->dragging = true;
                    break;
                }
            }
        }
        if (v->dragging && v->selected >= 0 &&
            IsMouseButtonDown(MOUSE_LEFT_BUTTON) && in_grid(g, cx, cy)) {
            GbObject *o = &objs->item[v->selected];
            if (o->x != cx || o->y != cy) {
                cJSON *before = cJSON_Duplicate(o->node, 1);
                gb_object_move(o, cx, cy);
                // The whole node is recorded, not just x/y: a castle move
                // touches four fields and undo must put all of them back.
                gb_undo_push_json(undo, "move object", o->owner ? o->owner : o->node,
                                  NULL, o->owner ? o->index : -1, before, o->node);
                cJSON_Delete(before);
                snprintf(status, status_sz, "%s -> (%d,%d)", o->label, cx, cy);
            }
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) v->dragging = false;

        if (v->selected >= 0 && IsKeyPressed(KEY_DELETE)) {
            GbObject *o = &objs->item[v->selected];
            if (gb_object_delete(o)) {
                gb_objects_collect(objs, doc, zone_id);
                v->selected = -1;
                snprintf(status, status_sz, "Deleted");
            } else {
                snprintf(status, status_sz,
                         "Spawn points cannot be deleted, only moved");
            }
        }
    }

    // The inspected tile keeps a marker after the mouse moves on.
    if (v->inspect_x >= 0 && in_grid(g, v->inspect_x, v->inspect_y)) {
        float tw = TW * v->zoom, th = TH * v->zoom;
        Rectangle r = { v->origin.x + v->pan.x + v->inspect_x * tw,
                        v->origin.y + v->pan.y + v->inspect_y * th, tw, th };
        DrawRectangleRec(r, (Color){ 120, 200, 255, 50 });
        DrawRectangleLinesEx(r, 2, (Color){ 120, 200, 255, 255 });
    }

    // brush / hover cursor
    if (over && in_grid(g, cx, cy)) {
        float tw = TW * v->zoom, th = TH * v->zoom;
        int r = (objects_mode || v->tool == GB_TOOL_VIEW) ? 0 : v->brush / 2;
        DrawRectangleLinesEx((Rectangle){ v->origin.x + v->pan.x + (cx - r) * tw,
                                          v->origin.y + v->pan.y + (cy - r) * th,
                                          tw * (r * 2 + 1), th * (r * 2 + 1) },
                             2, YELLOW);
    }
    EndScissorMode();

    // minimap, bottom-right of the canvas
    Rectangle mm = { v->origin.x + v->size.x - 150, v->origin.y + v->size.y - 150,
                     140, 140 };
    draw_minimap(v, g, mm);

    if (over && in_grid(g, cx, cy)) {
        // Sits above the app's status line, which owns the bottom edge.
        DrawText(TextFormat("%d, %d  %s", cx, cy,
                            TERRAIN_NAME[g->cell[cy][cx].terrain]),
                 (int)v->origin.x + 8, (int)(v->origin.y + v->size.y - 38), 12,
                 (Color){ 200, 200, 210, 255 });
    }
}

const char *gb_terrain_name(int t) {
    return (t >= 0 && t < TERRAIN_COUNT) ? TERRAIN_NAME[t] : "?";
}
Color gb_terrain_color(int t) {
    return (t >= 0 && t < TERRAIN_COUNT) ? TERRAIN_COL[t] : MAGENTA;
}
Color gb_object_color(int k) {
    return (k >= 0 && k < GB_OBJ_KINDS) ? OBJ_COL[k] : MAGENTA;
}
