// openbounty-mapedit -- GUI map editor for pack zones.
//
//   ./build/openbounty-mapedit --pack <dir> --zone <id>
//
// Paints base terrain onto a zone's .dat. Edge variants are NOT painted: the
// furnish pass derives them continuously so the canvas always shows the
// finished map, and bakes them on save (REQ-229).
//
// Rendering goes through the game's own tile_cache, so the canvas is drawn by
// exactly the code that draws the game. An earlier standalone renderer got the
// art path convention wrong and silently produced a black image; reusing the
// engine's loader makes that class of bug impossible.

#include "mapedit.h"

#include "assets.h"
#include "pack.h"
#include "tile_cache.h"

#include "raylib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_W 190
#define WIN_W  1280
#define WIN_H   860

typedef struct {
    MapGrid   grid;
    Resources res;
    Terrain   brush;
    int       brush_size;
    float     zoom;
    Vector2   pan;
    bool      show_grid;
    bool      show_terrain;      // flat colours instead of art
    char      status[256];
    double    status_at;
} Editor;

static const char *TERRAIN_NAME[TERRAIN_COUNT] = {
    "Grass", "Forest", "Mountain", "Water", "Desert"
};
static const Color TERRAIN_COL[TERRAIN_COUNT] = {
    {  72, 132,  48, 255 }, {  28,  78,  32, 255 }, { 120, 108,  96, 255 },
    {  36,  68, 140, 255 }, { 198, 176, 104, 255 },
};

static void DrawTextBoxed(const char *s, int x, int y);

static void status(Editor *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->status, sizeof e->status, fmt, ap);
    va_end(ap);
    e->status_at = GetTime();
}

// --- canvas ------------------------------------------------------------------

static const int TW = 48, TH = 34;

static Vector2 tile_at(const Editor *e, Vector2 screen) {
    return (Vector2){
        (screen.x - PANEL_W - e->pan.x) / (TW * e->zoom),
        (screen.y - e->pan.y) / (TH * e->zoom),
    };
}

static void paint(Editor *e, int cx, int cy) {
    int r = e->brush_size / 2;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= e->grid.w || y >= e->grid.h) continue;
            if (e->grid.cell[y][x].terrain == e->brush &&
                !e->grid.cell[y][x].decor) continue;
            e->grid.cell[y][x].terrain = e->brush;
            e->grid.cell[y][x].decor = 0;   // painting clears a decorative tile
            e->grid.dirty = true;
        }
    }
}

// Flood fill the contiguous region of one terrain, 4-connected.
static void fill(Editor *e, int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= e->grid.w || sy >= e->grid.h) return;
    Terrain from = e->grid.cell[sy][sx].terrain;
    if (from == e->brush) return;

    static int stack[MAPEDIT_MAX_W * MAPEDIT_MAX_H][2];
    int top = 0;
    stack[top][0] = sx; stack[top][1] = sy; top++;
    while (top > 0) {
        top--;
        int x = stack[top][0], y = stack[top][1];
        if (x < 0 || y < 0 || x >= e->grid.w || y >= e->grid.h) continue;
        if (e->grid.cell[y][x].terrain != from) continue;
        e->grid.cell[y][x].terrain = e->brush;
        e->grid.cell[y][x].decor = 0;
        const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4 && top < MAPEDIT_MAX_W * MAPEDIT_MAX_H; i++) {
            stack[top][0] = x + dx[i]; stack[top][1] = y + dy[i]; top++;
        }
    }
    e->grid.dirty = true;
}

static void draw_canvas(Editor *e) {
    BeginScissorMode(PANEL_W, 0, WIN_W - PANEL_W, WIN_H);
    float tw = TW * e->zoom, th = TH * e->zoom;

    // Only the visible window of tiles, so a 64x128 zone costs nothing to pan.
    int x0 = (int)((-e->pan.x) / tw) - 1, x1 = x0 + (int)((WIN_W - PANEL_W) / tw) + 3;
    int y0 = (int)((-e->pan.y) / th) - 1, y1 = y0 + (int)(WIN_H / th) + 3;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > e->grid.w) x1 = e->grid.w;
    if (y1 > e->grid.h) y1 = e->grid.h;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const MapCell *c = &e->grid.cell[y][x];
            Rectangle dst = { PANEL_W + e->pan.x + x * tw, e->pan.y + y * th,
                              tw + 1, th + 1 };
            if (e->show_terrain) {
                DrawRectangleRec(dst, TERRAIN_COL[c->terrain]);
            } else {
                Texture2D t = tile_cache_get(mapedit_art_for(c->terrain,
                                                             c->variant));
                if (t.id) {
                    Rectangle src = { 0, 0, (float)t.width, (float)t.height };
                    DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0, WHITE);
                } else {
                    DrawRectangleRec(dst, TERRAIN_COL[c->terrain]);
                }
            }
        }
    }

    if (e->show_grid && e->zoom > 0.35f) {
        Color g = { 255, 255, 255, 40 };
        for (int x = x0; x <= x1; x++)
            DrawLineV((Vector2){ PANEL_W + e->pan.x + x * tw, e->pan.y + y0 * th },
                      (Vector2){ PANEL_W + e->pan.x + x * tw, e->pan.y + y1 * th }, g);
        for (int y = y0; y <= y1; y++)
            DrawLineV((Vector2){ PANEL_W + e->pan.x + x0 * tw, e->pan.y + y * th },
                      (Vector2){ PANEL_W + e->pan.x + x1 * tw, e->pan.y + y * th }, g);
    }

    // Brush cursor
    Vector2 t = tile_at(e, GetMousePosition());
    int cx = (int)t.x, cy = (int)t.y;
    if (t.x >= 0 && t.y >= 0 && cx < e->grid.w && cy < e->grid.h) {
        int r = e->brush_size / 2;
        Rectangle b = { PANEL_W + e->pan.x + (cx - r) * tw,
                        e->pan.y + (cy - r) * th,
                        tw * e->brush_size, th * e->brush_size };
        DrawRectangleLinesEx(b, 2, YELLOW);
    }
    EndScissorMode();
}

// --- panel -------------------------------------------------------------------

static void draw_panel(Editor *e) {
    DrawRectangle(0, 0, PANEL_W, WIN_H, (Color){ 28, 28, 34, 255 });
    int y = 12;
    DrawText("openbounty-mapedit", 12, y, 16, RAYWHITE); y += 26;
    DrawText(TextFormat("%s  %dx%d", e->grid.zone_id, e->grid.w, e->grid.h),
             12, y, 12, GRAY); y += 24;

    DrawText("TERRAIN  (1-5)", 12, y, 12, LIGHTGRAY); y += 18;
    for (int i = 0; i < TERRAIN_COUNT; i++) {
        Rectangle sw = { 12, (float)y, 22, 16 };
        DrawRectangleRec(sw, TERRAIN_COL[i]);
        if ((int)e->brush == i) DrawRectangleLinesEx(sw, 2, YELLOW);
        DrawText(TextFormat("%d %s", i + 1, TERRAIN_NAME[i]), 42, y + 2, 12,
                 (int)e->brush == i ? YELLOW : RAYWHITE);
        if (CheckCollisionPointRec(GetMousePosition(),
                                   (Rectangle){ 12, (float)y, 160, 16 }) &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            e->brush = (Terrain)i;
        }
        y += 20;
    }

    y += 10;
    DrawText(TextFormat("Brush  %d  ([ ])", e->brush_size), 12, y, 12, RAYWHITE);
    y += 22;
    DrawText(TextFormat("Zoom   %.0f%%", e->zoom * 100), 12, y, 12, RAYWHITE);
    y += 28;

    DrawText("KEYS", 12, y, 12, LIGHTGRAY); y += 18;
    const char *keys[] = {
        "LMB    paint",
        "RMB    pan",
        "F      flood fill",
        "G      grid",
        "T      flat/art view",
        "wheel  zoom",
        "S      save .dat",
        "R      reload",
    };
    for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) {
        DrawText(keys[i], 12, y, 11, GRAY); y += 15;
    }

    y = WIN_H - 74;
    if (e->grid.dirty) {
        DrawText("UNSAVED", 12, y, 14, (Color){ 255, 140, 60, 255 });
    }
    y += 20;
    if (e->status[0] && GetTime() - e->status_at < 6.0) {
        DrawTextBoxed(e->status, 12, y);
    }
}

// Small helper: wrap the status line inside the panel.
static void DrawTextBoxed(const char *s, int x, int y) {
    char line[64];
    int len = (int)strlen(s), at = 0, row = 0;
    while (at < len && row < 4) {
        int n = len - at > 26 ? 26 : len - at;
        memcpy(line, s + at, n); line[n] = 0;
        DrawText(line, x, y + row * 13, 11, (Color){ 150, 220, 150, 255 });
        at += n; row++;
    }
}

// --- main --------------------------------------------------------------------

static void usage(void) {
    fprintf(stderr,
        "usage: openbounty-mapedit --pack <dir> --zone <id>\n"
        "\n"
        "Paints base terrain for a pack zone. Edge variants are derived by the\n"
        "furnish pass and baked into the .dat on save; they are never painted\n"
        "by hand. The pack must be a loose directory, not a .openbounty file.\n");
}

int main(int argc, char **argv) {
    const char *pack_arg = NULL, *zone_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pack") && i + 1 < argc) pack_arg = argv[++i];
        else if (!strcmp(argv[i], "--zone") && i + 1 < argc) zone_arg = argv[++i];
        else { usage(); return 2; }
    }
    if (!pack_arg || !zone_arg) { usage(); return 2; }

    Pack *pack = pack_open(pack_arg);
    if (!pack) {
        fprintf(stderr, "mapedit: cannot open pack '%s'\n", pack_arg);
        return 1;
    }
    pack_stack_push(pack);

    Editor e = {0};
    if (!resources_load(&e.res, "game.json")) {
        fprintf(stderr, "mapedit: cannot load game.json from '%s'\n", pack_arg);
        return 1;
    }
    if (!mapedit_load(&e.grid, &e.res, zone_arg)) return 1;

    e.brush = TERRAIN_WATER;
    e.brush_size = 1;
    e.zoom = 0.5f;
    e.pan = (Vector2){ 20, 20 };
    e.show_grid = true;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WIN_W, WIN_H, TextFormat("openbounty-mapedit -- %s", zone_arg));
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    tile_cache_attach(&e.res);

    // Furnish once up front so the canvas opens showing the finished map.
    mapedit_furnish(&e.grid, NULL);

    bool running = true;
    while (running && !WindowShouldClose()) {
        Vector2 mp = GetMousePosition();
        bool on_canvas = mp.x > PANEL_W;

        for (int i = 0; i < TERRAIN_COUNT; i++)
            if (IsKeyPressed(KEY_ONE + i)) e.brush = (Terrain)i;
        if (IsKeyPressed(KEY_G)) e.show_grid = !e.show_grid;
        if (IsKeyPressed(KEY_T)) e.show_terrain = !e.show_terrain;
        if (IsKeyPressed(KEY_LEFT_BRACKET) && e.brush_size > 1) e.brush_size -= 2;
        if (IsKeyPressed(KEY_RIGHT_BRACKET) && e.brush_size < 15) e.brush_size += 2;

        float wheel = GetMouseWheelMove();
        if (wheel != 0 && on_canvas) {
            float old = e.zoom;
            e.zoom += wheel * 0.1f;
            if (e.zoom < 0.15f) e.zoom = 0.15f;
            if (e.zoom > 2.0f) e.zoom = 2.0f;
            // keep the tile under the cursor put
            e.pan.x = mp.x - PANEL_W - (mp.x - PANEL_W - e.pan.x) * (e.zoom / old);
            e.pan.y = mp.y - (mp.y - e.pan.y) * (e.zoom / old);
        }
        if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
            Vector2 d = GetMouseDelta();
            e.pan.x += d.x; e.pan.y += d.y;
        }

        bool edited = false;
        if (on_canvas && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            Vector2 t = tile_at(&e, mp);
            if (t.x >= 0 && t.y >= 0) { paint(&e, (int)t.x, (int)t.y); edited = true; }
        }
        if (on_canvas && IsKeyPressed(KEY_F)) {
            Vector2 t = tile_at(&e, mp);
            fill(&e, (int)t.x, (int)t.y);
            edited = true;
        }
        // Re-derive variants after any edit: the canvas always shows the
        // finished map, never raw base terrain.
        if (edited) mapedit_furnish(&e.grid, NULL);

        if (IsKeyPressed(KEY_S)) {
            if (mapedit_save(&e.grid, &e.res)) {
                status(&e, "saved %s", e.grid.dat_path);
            } else {
                status(&e, "SAVE FAILED - see terminal");
            }
        }
        if (IsKeyPressed(KEY_R)) {
            if (mapedit_load(&e.grid, &e.res, zone_arg)) {
                mapedit_furnish(&e.grid, NULL);
                status(&e, "reloaded");
            }
        }

        BeginDrawing();
        ClearBackground((Color){ 12, 12, 16, 255 });
        draw_canvas(&e);
        draw_panel(&e);
        EndDrawing();
    }

    tile_cache_shutdown();
    CloseWindow();
    pack_stack_clear();
    return 0;
}
