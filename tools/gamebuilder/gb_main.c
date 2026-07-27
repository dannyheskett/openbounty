// GameBuilder -- application shell: start screen, mode bar, raw-JSON view.
//
// GB-015: takes NO arguments. Everything is chosen by pointing and clicking.
// GB-016: opens on a start screen, never into an undefined state.

#include "raygui.h"

#include "gb.h"

#include "raylib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 1280
#define WIN_H  860
#define BAR_H   34

typedef enum {
    MODE_MAPS = 0, MODE_OBJECTS, MODE_CATALOG, MODE_STRINGS,
    MODE_ART, MODE_PALETTE, MODE_VALIDATE, MODE_PACKAGE, MODE_COUNT
} GbMode;

static const char *MODE_NAME[MODE_COUNT] = {
    "Maps", "Objects", "Catalog", "Strings",
    "Art", "Palette", "Validate", "Package"
};

typedef struct {
    GbWorkspace ws;
    GbRecent    recent;
    GbBrowser  *browser;
    GbMode      mode;
    bool        on_start_screen;

    char        message[512];      // modal error/notice, "" when none
    char        status[GB_STATUS_MAX];
    double      status_at;

    // raw-JSON view (GB-120)
    char       *json_text;
    Vector2     json_scroll;
    bool        json_edit;
} Gb;

static void say(Gb *g, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g->status, sizeof g->status, fmt, ap);
    va_end(ap);
    g->status_at = GetTime();
}

// --- raw JSON ----------------------------------------------------------------

static void json_refresh(Gb *g) {
    if (g->json_text) { cJSON_free(g->json_text); g->json_text = NULL; }
    if (g->ws.open && g->ws.doc) g->json_text = cJSON_Print(g->ws.doc);
}

// --- start screen (GB-016) ----------------------------------------------------

static void draw_start(Gb *g) {
    int cx = GetScreenWidth() / 2;
    DrawText("GameBuilder", cx - MeasureText("GameBuilder", 44) / 2, 90, 44,
             RAYWHITE);
    const char *sub = "OpenBounty game-pack editor";
    DrawText(sub, cx - MeasureText(sub, 16) / 2, 144, 16, GRAY);

    int by = 210;
    if (GuiButton((Rectangle){ (float)(cx - 150), (float)by, 300, 40 },
                  "Open Pack...")) {
        gb_browser_open(g->browser, GB_PICK_PACK,
                        g->recent.count ? g->recent.path[0] : NULL,
                        "Open a pack folder");
    }
    by += 50;
    if (GuiButton((Rectangle){ (float)(cx - 150), (float)by, 300, 40 },
                  "New Pack...  (not yet)")) {
        snprintf(g->message, sizeof g->message,
                 "Creating a pack from scratch is not built yet.\n\n"
                 "For now, open an existing pack folder.");
    }

    by += 74;
    if (g->recent.count) {
        DrawText("Recent", cx - 150, by, 14, LIGHTGRAY);
        by += 22;
        for (int i = 0; i < g->recent.count; i++) {
            Rectangle r = { (float)(cx - 150), (float)by, 300, 26 };
            const char *p = g->recent.path[i];
            const char *shown = strlen(p) > 44 ? p + strlen(p) - 44 : p;
            if (GuiButton(r, TextFormat("%s%s", shown == p ? "" : "...", shown))) {
                char err[512];
                if (gb_workspace_open(&g->ws, p, err, sizeof err)) {
                    gb_recent_add(&g->recent, p);
                    gb_recent_save(&g->recent);
                    json_refresh(g);
                    g->on_start_screen = false;
                    say(g, "Opened %s", p);
                } else {
                    snprintf(g->message, sizeof g->message, "%s", err);
                }
            }
            by += 30;
        }
    }
}

// --- mode bar -----------------------------------------------------------------

static void draw_bar(Gb *g) {
    DrawRectangle(0, 0, GetScreenWidth(), BAR_H, (Color){ 32, 32, 40, 255 });
    int x = 8;
    for (int i = 0; i < MODE_COUNT; i++) {
        int w = MeasureText(MODE_NAME[i], 13) + 22;
        Rectangle r = { (float)x, 4, (float)w, BAR_H - 8 };
        bool on = ((int)g->mode == i);
        if (on) DrawRectangleRec(r, (Color){ 60, 90, 140, 255 });
        else if (CheckCollisionPointRec(GetMousePosition(), r))
            DrawRectangleRec(r, (Color){ 48, 48, 58, 255 });
        DrawText(MODE_NAME[i], x + 11, 11, 13, on ? RAYWHITE : LIGHTGRAY);
        if (CheckCollisionPointRec(GetMousePosition(), r) &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            g->mode = (GbMode)i;                  // GB-100: never discards edits
        }
        x += w + 2;
    }

    const char *id = "";
    if (g->ws.res_valid) id = g->ws.res.pack_name[0] ? g->ws.res.pack_name
                                                     : g->ws.res.pack_id;
    const char *label = TextFormat("%s%s", id, g->ws.dirty ? "  *" : "");
    DrawText(label, GetScreenWidth() - MeasureText(label, 13) - 12, 11, 13,
             g->ws.dirty ? (Color){ 255, 170, 70, 255 } : GRAY);
}

// --- modes --------------------------------------------------------------------

static void draw_placeholder(const char *name, const char *what) {
    int cx = GetScreenWidth() / 2, cy = GetScreenHeight() / 2;
    DrawText(name, cx - MeasureText(name, 28) / 2, cy - 40, 28,
             (Color){ 90, 90, 100, 255 });
    DrawText(what, cx - MeasureText(what, 14) / 2, cy + 4, 14,
             (Color){ 70, 70, 80, 255 });
}

static void draw_json(Gb *g) {
    if (!g->json_text) {
        draw_placeholder("Raw JSON", "nothing open");
        return;
    }
    int w = GetScreenWidth(), h = GetScreenHeight();
    GuiLabel((Rectangle){ 12, (float)BAR_H + 6, 600, 18 },
             "game.json  --  the workspace's source of truth (read-only for now)");

    Rectangle box = { 12, (float)BAR_H + 28, (float)(w - 24), (float)(h - BAR_H - 70) };
    // Line count drives the scroll extent; drawing is clipped to the view.
    int lines = 1;
    for (const char *p = g->json_text; *p; p++) if (*p == '\n') lines++;
    Rectangle view;
    GuiScrollPanel(box, NULL, (Rectangle){ 0, 0, 1400, (float)(lines * 14 + 8) },
                   &g->json_scroll, &view);

    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    const char *p = g->json_text;
    int row = 0;
    char line[512];
    while (*p && row < lines) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n > sizeof line - 1) n = sizeof line - 1;
        memcpy(line, p, n); line[n] = 0;
        float ly = box.y + g->json_scroll.y + 4 + row * 14;
        if (ly > view.y - 14 && ly < view.y + view.height)
            DrawText(line, (int)(box.x + 6 + g->json_scroll.x), (int)ly, 11,
                     (Color){ 190, 200, 190, 255 });
        if (!nl) break;
        p = nl + 1;
        row++;
    }
    EndScissorMode();
}

// --- main ---------------------------------------------------------------------

int main(int argc, char **argv) {
    // GB-015: no arguments. A stray argument is a mistake worth naming rather
    // than ignoring, since someone will try --pack out of habit.
    if (argc > 1) {
        fprintf(stderr,
            "openbounty-gamebuilder takes no arguments.\n"
            "Everything is chosen inside the application: run it and use "
            "Open Pack.\n");
        return 2;
    }
    (void)argv;

    Gb g = {0};
    g.on_start_screen = true;
    gb_recent_load(&g.recent);

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WIN_W, WIN_H, "GameBuilder -- OpenBounty");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 13);
    g.browser = gb_browser_create();

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 18, 18, 22, 255 });

        if (g.on_start_screen) draw_start(&g);
        else {
            draw_bar(&g);
            switch (g.mode) {
                case MODE_CATALOG: draw_json(&g); break;
                default:
                    draw_placeholder(MODE_NAME[g.mode], "not built yet");
                    break;
            }
        }

        // Browser overlays everything while open.
        if (gb_browser_active(g.browser)) {
            char picked[GB_PATH_MAX];
            if (gb_browser_draw(g.browser, GetScreenWidth() / 2 - 320,
                                GetScreenHeight() / 2 - 250, 640, 500,
                                picked, sizeof picked)) {
                char err[512];
                if (gb_workspace_open(&g.ws, picked, err, sizeof err)) {
                    gb_recent_add(&g.recent, picked);
                    gb_recent_save(&g.recent);
                    json_refresh(&g);
                    g.on_start_screen = false;
                    say(&g, "Opened %s", picked);
                } else {
                    snprintf(g.message, sizeof g.message, "%s", err);
                }
            }
        }

        // Modal message. Every failure lands here in words the user can act
        // on, never a bare assert (GB-012).
        if (g.message[0]) {
            int w = 560, h = 220;
            int x = GetScreenWidth() / 2 - w / 2, y = GetScreenHeight() / 2 - h / 2;
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                          (Color){ 0, 0, 0, 180 });
            GuiPanel((Rectangle){ (float)x, (float)y, (float)w, (float)h },
                     "Could not do that");
            // wrap the message by hand: GuiLabel does not
            int ly = y + 40;
            const char *p = g.message;
            char line[80];
            while (*p && ly < y + h - 50) {
                size_t n = 0, last_space = 0;
                while (p[n] && p[n] != '\n' && n < 68) {
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
            if (GuiButton((Rectangle){ (float)(x + w - 100), (float)(y + h - 40),
                                       86, 28 }, "OK") ||
                IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
                g.message[0] = '\0';
            }
        }

        if (g.status[0] && GetTime() - g.status_at < 5.0) {
            DrawText(g.status, 12, GetScreenHeight() - 20, 12,
                     (Color){ 130, 190, 130, 255 });
        }
        EndDrawing();
    }

    gb_browser_destroy(g.browser);
    if (g.json_text) cJSON_free(g.json_text);
    gb_workspace_close(&g.ws);
    CloseWindow();
    return 0;
}
