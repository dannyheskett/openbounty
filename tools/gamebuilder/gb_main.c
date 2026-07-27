// GameBuilder -- application shell.
//
// GB-015: takes NO arguments. Everything is chosen by pointing and clicking.
// GB-016: opens on a start screen, never into an undefined state.

#include "gb_ui.h"

#include "raygui.h"
#include "tile_cache.h"
#include "pack.h"
#include "palette.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR_H  34
#define SIDE_W 210

typedef enum {
    MODE_MAPS = 0, MODE_OBJECTS, MODE_CATALOG, MODE_STRINGS,
    MODE_ART, MODE_PALETTE, MODE_VALIDATE, MODE_PACKAGE, MODE_COUNT
} GbMode;

static const char *MODE_NAME[MODE_COUNT] = {
    "Maps", "Objects", "Catalog", "Strings",
    "Art", "Palette", "Validate", "Package"
};

typedef struct {
    GbWorkspace  ws;
    GbRecent     recent;
    GbBrowser   *browser;
    GbUndo      *undo;
    GbMode       mode;
    bool         start_screen;

    MapGrid      grid[GB_MAX_ZONES];
    bool         grid_loaded[GB_MAX_ZONES];
    GbObjectList objs;
    GbMapView    view;

    char         message[600];
    char         status[GB_STATUS_MAX];
    double       status_at;
    double       last_autosave;
    bool         confirm_quit;
} Gb;

static Gb G;

// Validate mode needs the loaded grids; hand them over explicitly rather than
// letting that file reach into this one's state.
void gb_collect_grids(MapGrid ***g, bool **loaded, int *n) {
    static MapGrid *ptr[GB_MAX_ZONES];
    for (int i = 0; i < GB_MAX_ZONES; i++) ptr[i] = &G.grid[i];
    *g = ptr;
    *loaded = G.grid_loaded;
    *n = GB_MAX_ZONES;
}

static void say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(G.status, sizeof G.status, fmt, ap);
    va_end(ap);
    G.status_at = GetTime();
}

// --- zone plumbing ------------------------------------------------------------

static const char *cur_zone_id(void) {
    return G.ws.open ? gb_zone_id_at(G.ws.doc, G.view.zone) : NULL;
}

static void load_zone(int index) {
    const char *zid = gb_zone_id_at(G.ws.doc, index);
    if (!zid || index < 0 || index >= GB_MAX_ZONES) return;
    if (!G.grid_loaded[index]) {
        if (!G.ws.res_valid) gb_workspace_reproject(&G.ws);
        if (mapedit_load(&G.grid[index], &G.ws.res, zid)) {
            mapedit_furnish(&G.grid[index], NULL);
            G.grid_loaded[index] = true;
        } else {
            snprintf(G.message, sizeof G.message,
                     "Could not read the map for zone '%s'.\n\n"
                     "Check that its `map` file exists in the pack.", zid);
            return;
        }
    }
    G.view.zone = index;
    G.view.selected = -1;
    gb_objects_collect(&G.objs, G.ws.doc, zid);
}

// Undo needs to reach the grids without knowing how we store them.
static MapGrid *grid_for(void *user, int zone) {
    (void)user;
    return (zone >= 0 && zone < GB_MAX_ZONES && G.grid_loaded[zone])
           ? &G.grid[zone] : NULL;
}
static void after_tiles(void *user, int zone) {
    (void)user;
    if (zone >= 0 && zone < GB_MAX_ZONES && G.grid_loaded[zone])
        mapedit_furnish(&G.grid[zone], NULL);
}

static void open_path(const char *path) {
    char err[512];
    if (!gb_workspace_open(&G.ws, path, err, sizeof err)) {
        snprintf(G.message, sizeof G.message, "%s", err);
        return;
    }
    memset(G.grid_loaded, 0, sizeof G.grid_loaded);
    gb_undo_clear(G.undo);
    gb_recent_add(&G.recent, path);
    gb_recent_save(&G.recent);
    tile_cache_attach(&G.ws.res);
    palette_init("palettes/palette.bin");
    gb_mapview_init(&G.view);
    load_zone(0);
    G.start_screen = false;
    say("Opened %s", G.ws.root);

    if (gb_autosave_exists(G.ws.root)) {
        snprintf(G.message, sizeof G.message,
                 "Unsaved work from a previous session was found for this "
                 "pack.\n\nPress R to recover it, or Esc to discard it and "
                 "keep the version on disk.");
    }
}

static void do_save(void) {
    char err[512];
    if (!gb_workspace_save(&G.ws, err, sizeof err)) {
        snprintf(G.message, sizeof G.message, "%s", err);
        return;
    }
    int written = 0;
    for (int i = 0; i < GB_MAX_ZONES; i++) {
        if (G.grid_loaded[i] && G.grid[i].dirty) {
            if (mapedit_save(&G.grid[i], &G.ws.res)) written++;
        }
    }
    gb_autosave_discard(&G.ws);
    say("Saved game.json%s", written ? TextFormat(" and %d map(s)", written) : "");
}

static bool anything_dirty(void) {
    if (G.ws.dirty) return true;
    for (int i = 0; i < GB_MAX_ZONES; i++)
        if (G.grid_loaded[i] && G.grid[i].dirty) return true;
    return false;
}

// --- start screen -------------------------------------------------------------

static void draw_start(void) {
    int cx = GetScreenWidth() / 2;
    DrawText("GameBuilder", cx - MeasureText("GameBuilder", 44) / 2, 80, 44,
             RAYWHITE);
    const char *sub = "OpenBounty game-pack editor";
    DrawText(sub, cx - MeasureText(sub, 16) / 2, 134, 16, GRAY);

    int by = 200;
    if (GuiButton((Rectangle){ (float)(cx - 150), (float)by, 300, 40 },
                  "Open Pack...")) {
        gb_browser_open(G.browser, GB_PICK_PACK,
                        G.recent.count ? G.recent.path[0] : NULL,
                        "Open a pack folder or .openbounty");
    }
    by += 48;
    if (GuiButton((Rectangle){ (float)(cx - 150), (float)by, 300, 40 },
                  "New Pack...")) {
        gb_browser_open(G.browser, GB_PICK_DIR, NULL,
                        "Choose an empty folder for the new pack");
        G.view.selected = -2;          // marks the browser result as "new"
    }

    by += 70;
    if (G.recent.count) {
        DrawText("Recent", cx - 150, by, 14, LIGHTGRAY);
        by += 22;
        for (int i = 0; i < G.recent.count; i++) {
            const char *p = G.recent.path[i];
            const char *shown = strlen(p) > 44 ? p + strlen(p) - 44 : p;
            if (GuiButton((Rectangle){ (float)(cx - 150), (float)by, 300, 26 },
                          TextFormat("%s%s", shown == p ? "" : "...", shown)))
                open_path(p);
            by += 30;
        }
    }
}

// --- chrome -------------------------------------------------------------------

static void draw_bar(void) {
    DrawRectangle(0, 0, GetScreenWidth(), BAR_H, (Color){ 32, 32, 40, 255 });
    int x = 8;
    for (int i = 0; i < MODE_COUNT; i++) {
        int w = MeasureText(MODE_NAME[i], 13) + 20;
        Rectangle r = { (float)x, 4, (float)w, BAR_H - 8 };
        bool on = ((int)G.mode == i);
        if (on) DrawRectangleRec(r, (Color){ 60, 90, 140, 255 });
        else if (CheckCollisionPointRec(GetMousePosition(), r))
            DrawRectangleRec(r, (Color){ 48, 48, 58, 255 });
        DrawText(MODE_NAME[i], x + 10, 11, 13, on ? RAYWHITE : LIGHTGRAY);
        if (CheckCollisionPointRec(GetMousePosition(), r) &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            G.mode = (GbMode)i;                // GB-100: never discards edits
        x += w + 2;
    }
    const char *id = G.ws.res_valid
        ? (G.ws.res.pack_name[0] ? G.ws.res.pack_name : G.ws.res.pack_id) : "";
    const char *label = TextFormat("%s%s", id, anything_dirty() ? "  *" : "");
    DrawText(label, GetScreenWidth() - MeasureText(label, 13) - 12, 11, 13,
             anything_dirty() ? (Color){ 255, 170, 70, 255 } : GRAY);
}

static void draw_map_side(bool objects_mode) {
    int x = GetScreenWidth() - SIDE_W, y = BAR_H + 8;
    DrawRectangle(x, BAR_H, SIDE_W, GetScreenHeight() - BAR_H,
                  (Color){ 28, 28, 34, 255 });

    // zone picker
    DrawText("ZONE", x + 10, y, 12, LIGHTGRAY); y += 18;
    int zn = gb_zone_count(G.ws.doc);
    for (int i = 0; i < zn && i < GB_MAX_ZONES; i++) {
        const char *zid = gb_zone_id_at(G.ws.doc, i);
        Rectangle r = { (float)(x + 10), (float)y, SIDE_W - 20, 22 };
        bool on = (G.view.zone == i);
        if (on) DrawRectangleRec(r, (Color){ 60, 90, 140, 255 });
        else if (CheckCollisionPointRec(GetMousePosition(), r))
            DrawRectangleRec(r, (Color){ 44, 44, 54, 255 });
        DrawText(zid ? zid : "?", x + 16, y + 5, 12, on ? RAYWHITE : LIGHTGRAY);
        if (CheckCollisionPointRec(GetMousePosition(), r) &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && i != G.view.zone)
            load_zone(i);
        y += 24;
    }
    y += 10;

    if (!objects_mode) {
        DrawText("TERRAIN  (1-5)", x + 10, y, 12, LIGHTGRAY); y += 18;
        for (int i = 0; i < TERRAIN_COUNT; i++) {
            Rectangle sw = { (float)(x + 10), (float)y, 22, 16 };
            DrawRectangleRec(sw, gb_terrain_color(i));
            if ((int)G.view.brush_terrain == i)
                DrawRectangleLinesEx(sw, 2, YELLOW);
            DrawText(TextFormat("%d %s", i + 1, gb_terrain_name(i)),
                     x + 40, y + 2, 12,
                     (int)G.view.brush_terrain == i ? YELLOW : RAYWHITE);
            if (CheckCollisionPointRec(GetMousePosition(),
                    (Rectangle){ (float)(x + 10), (float)y, SIDE_W - 20, 16 }) &&
                IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                G.view.brush_terrain = (Terrain)i;
            y += 20;
        }
        y += 8;
        static const char *TOOL[GB_TOOL_COUNT] = { "Paint", "Fill", "Rect", "Pick" };
        DrawText("TOOL", x + 10, y, 12, LIGHTGRAY); y += 18;
        for (int i = 0; i < GB_TOOL_COUNT; i++) {
            Rectangle r = { (float)(x + 10 + (i % 2) * 92), (float)(y + (i / 2) * 26),
                            88, 22 };
            if (GuiButton(r, TOOL[i])) G.view.tool = (GbTool)i;
            if ((int)G.view.tool == i) DrawRectangleLinesEx(r, 2, YELLOW);
        }
        y += 60;
        DrawText(TextFormat("Brush  %d   [ ]", G.view.brush), x + 10, y, 12,
                 RAYWHITE);
        y += 24;
    } else {
        DrawText("PLACE", x + 10, y, 12, LIGHTGRAY); y += 18;
        static const GbObjKind PLACEABLE[] = {
            GB_OBJ_CHEST, GB_OBJ_SIGN, GB_OBJ_DWELLING, GB_OBJ_ARMY,
            GB_OBJ_TOWN, GB_OBJ_CASTLE
        };
        for (unsigned i = 0; i < sizeof PLACEABLE / sizeof *PLACEABLE; i++) {
            Rectangle r = { (float)(x + 10 + (i % 2) * 92),
                            (float)(y + (i / 2) * 26), 88, 22 };
            DrawRectangle((int)r.x, (int)r.y, 4, (int)r.height,
                          gb_object_color(PLACEABLE[i]));
            if (GuiButton(r, GB_OBJ_NAME[PLACEABLE[i]])) {
                // Drop it in the middle of the current view, then the author
                // drags it: placing at the mouse would need a mode change.
                float tw = 48 * G.view.zoom, th = 34 * G.view.zoom;
                int tx = (int)((-G.view.pan.x + G.view.size.x / 2) / tw);
                int ty = (int)((-G.view.pan.y + G.view.size.y / 2) / th);
                cJSON *n = gb_object_create(G.ws.doc, cur_zone_id(),
                                            PLACEABLE[i], tx, ty);
                if (n) {
                    G.ws.dirty = true;
                    gb_objects_collect(&G.objs, G.ws.doc, cur_zone_id());
                    G.view.selected = G.objs.count - 1;
                    say("Placed %s at (%d,%d) -- drag to position",
                        GB_OBJ_NAME[PLACEABLE[i]], tx, ty);
                }
            }
        }
        y += 90;
        DrawText(TextFormat("%d objects", G.objs.count), x + 10, y, 12, RAYWHITE);
        y += 20;
        if (G.view.selected >= 0 && G.view.selected < G.objs.count) {
            const GbObject *o = &G.objs.item[G.view.selected];
            DrawText("SELECTED", x + 10, y, 12, LIGHTGRAY); y += 18;
            DrawText(o->label, x + 10, y, 12, YELLOW); y += 16;
            DrawText(TextFormat("%s  (%d, %d)", GB_OBJ_NAME[o->kind], o->x, o->y),
                     x + 10, y, 11, GRAY);
            y += 22;
            DrawText("Del removes it", x + 10, y, 11, GRAY);
        }
    }

    y = GetScreenHeight() - 96;
    DrawText(TextFormat("Undo: %s", gb_undo_can_undo(G.undo)
                        ? gb_undo_label(G.undo, false) : "-"), x + 10, y, 11, GRAY);
    y += 14;
    DrawText(TextFormat("Redo: %s", gb_undo_can_redo(G.undo)
                        ? gb_undo_label(G.undo, true) : "-"), x + 10, y, 11, GRAY);
    y += 20;
    DrawText("Ctrl+Z / Ctrl+Y / Ctrl+S", x + 10, y, 10, DARKGRAY);
    y += 14;
    DrawText("G grid   T flat   O objects", x + 10, y, 10, DARKGRAY);
}

// --- main ---------------------------------------------------------------------

int main(int argc, char **argv) {
    if (argc > 1) {
        fprintf(stderr,
            "openbounty-gamebuilder takes no arguments.\n"
            "Everything is chosen inside the application: run it and use "
            "Open Pack.\n");
        return 2;
    }
    (void)argv;

    G.start_screen = true;
    gb_recent_load(&G.recent);

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1400, 900, "GameBuilder -- OpenBounty");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 13);
    G.browser = gb_browser_create();
    G.undo = gb_undo_create();
    gb_mapview_init(&G.view);

    bool quit = false;
    while (!quit) {
        if (WindowShouldClose()) {
            if (anything_dirty() && !G.confirm_quit) {
                G.confirm_quit = true;
                snprintf(G.message, sizeof G.message,
                         "This pack has unsaved changes.\n\n"
                         "Press S to save and quit, Q to quit without saving, "
                         "or Esc to keep working.");
            } else if (!anything_dirty()) {
                quit = true;
            }
        }

        // GB-103: autosave on an interval, never into the pack itself.
        if (G.ws.open && GetTime() - G.last_autosave > 30.0) {
            gb_autosave_write(&G.ws);
            G.last_autosave = GetTime();
        }

        bool modal = G.message[0] || gb_browser_active(G.browser);
        if (!modal && !G.start_screen) {
            bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            GbUndoApply ctx = { NULL, grid_for, after_tiles };
            if (ctrl && IsKeyPressed(KEY_Z)) {
                if (gb_undo_undo(G.undo, &ctx)) {
                    G.ws.dirty = true;
                    gb_objects_collect(&G.objs, G.ws.doc, cur_zone_id());
                    say("Undo");
                } else say("Nothing to undo");
            }
            if (ctrl && (IsKeyPressed(KEY_Y) ||
                         (IsKeyPressed(KEY_Z) && IsKeyDown(KEY_LEFT_SHIFT)))) {
                if (gb_undo_redo(G.undo, &ctx)) {
                    G.ws.dirty = true;
                    gb_objects_collect(&G.objs, G.ws.doc, cur_zone_id());
                    say("Redo");
                } else say("Nothing to redo");
            }
            if (ctrl && IsKeyPressed(KEY_S)) do_save();
            if (IsKeyPressed(KEY_G)) G.view.show_grid = !G.view.show_grid;
            if (IsKeyPressed(KEY_T)) G.view.flat_view = !G.view.flat_view;
            if (IsKeyPressed(KEY_O)) G.view.show_objects = !G.view.show_objects;
            for (int i = 0; i < TERRAIN_COUNT; i++)
                if (IsKeyPressed(KEY_ONE + i)) G.view.brush_terrain = (Terrain)i;
            if (IsKeyPressed(KEY_LEFT_BRACKET) && G.view.brush > 1) G.view.brush -= 2;
            if (IsKeyPressed(KEY_RIGHT_BRACKET) && G.view.brush < 15) G.view.brush += 2;
        }

        BeginDrawing();
        ClearBackground((Color){ 18, 18, 22, 255 });

        if (G.start_screen) {
            draw_start();
        } else {
            draw_bar();
            switch (G.mode) {
            case MODE_MAPS:
            case MODE_OBJECTS: {
                bool om = (G.mode == MODE_OBJECTS);
                G.view.origin = (Vector2){ 0, BAR_H };
                G.view.size = (Vector2){ (float)(GetScreenWidth() - SIDE_W),
                                         (float)(GetScreenHeight() - BAR_H) };
                if (G.grid_loaded[G.view.zone]) {
                    gb_mapview_frame(&G.view, &G.grid[G.view.zone], &G.objs,
                                     G.undo, om, G.ws.doc, cur_zone_id(),
                                     G.status, sizeof G.status);
                    if (G.status[0]) G.status_at = GetTime();
                }
                draw_map_side(om);
                break;
            }
            default:
                gb_mode_draw(G.mode, &G.ws, G.undo, BAR_H, G.status,
                             sizeof G.status);
                break;
            }
        }

        if (gb_browser_active(G.browser)) {
            char picked[GB_PATH_MAX];
            if (gb_browser_draw(G.browser, GetScreenWidth() / 2 - 320,
                                GetScreenHeight() / 2 - 250, 640, 500,
                                picked, sizeof picked)) {
                if (G.view.selected == -2) {          // New Pack flow
                    G.view.selected = -1;
                    char err[512];
                    if (gb_newpack_create(picked, err, sizeof err)) open_path(picked);
                    else snprintf(G.message, sizeof G.message, "%s", err);
                } else {
                    open_path(picked);
                }
            }
        }

        if (G.message[0]) {
            int w = 600, h = 240;
            int x = GetScreenWidth() / 2 - w / 2, y = GetScreenHeight() / 2 - h / 2;
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                          (Color){ 0, 0, 0, 180 });
            GuiPanel((Rectangle){ (float)x, (float)y, (float)w, (float)h },
                     G.confirm_quit ? "Unsaved changes" : "Notice");
            int ly = y + 38;
            const char *p = G.message;
            char line[96];
            while (*p && ly < y + h - 46) {
                size_t n = 0, last_space = 0;
                while (p[n] && p[n] != '\n' && n < 72) {
                    if (p[n] == ' ') last_space = n;
                    n++;
                }
                if (p[n] && p[n] != '\n' && last_space) n = last_space;
                memcpy(line, p, n); line[n] = 0;
                DrawText(line, x + 16, ly, 13, RAYWHITE);
                ly += 17;
                p += n;
                while (*p == ' ' || *p == '\n') p++;
            }
            bool dismiss = GuiButton((Rectangle){ (float)(x + w - 100),
                                                  (float)(y + h - 38), 86, 26 },
                                     "OK");
            if (G.confirm_quit) {
                if (IsKeyPressed(KEY_S)) { do_save(); quit = true; }
                if (IsKeyPressed(KEY_Q)) quit = true;
                if (IsKeyPressed(KEY_ESCAPE) || dismiss) {
                    G.message[0] = 0; G.confirm_quit = false;
                }
            } else {
                // Recovery offer uses the same modal.
                if (IsKeyPressed(KEY_R) && gb_autosave_exists(G.ws.root)) {
                    char err[512];
                    if (gb_autosave_recover(&G.ws, err, sizeof err)) {
                        memset(G.grid_loaded, 0, sizeof G.grid_loaded);
                        load_zone(G.view.zone);
                        say("Recovered unsaved work");
                    } else say("%s", err);
                    G.message[0] = 0;
                } else if (dismiss || IsKeyPressed(KEY_ENTER) ||
                           IsKeyPressed(KEY_ESCAPE)) {
                    if (G.ws.open && gb_autosave_exists(G.ws.root))
                        gb_autosave_discard(&G.ws);
                    G.message[0] = 0;
                }
            }
        }

        if (G.status[0] && GetTime() - G.status_at < 5.0)
            DrawText(G.status, 12, GetScreenHeight() - 18, 12,
                     (Color){ 130, 190, 130, 255 });
        EndDrawing();
    }

    gb_browser_destroy(G.browser);
    gb_undo_destroy(G.undo);
    gb_workspace_close(&G.ws);
    tile_cache_shutdown();
    CloseWindow();
    return 0;
}
