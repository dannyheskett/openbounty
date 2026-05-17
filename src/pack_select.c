// Pre-game pack selector. Runs before any pack is loaded, so it
// deliberately uses only raylib built-ins (default font, plain colors).

#include "pack_select.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

bool pack_select_flow(const PackEntry *list, int n, int *chosen) {
    if (!list || n <= 0 || !chosen) return false;

    const int W = 640;
    const int H = 400;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(W, H, "OpenBounty — select pack");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    int cursor = 0;
    bool done   = false;
    bool quit   = false;

    while (!done && !WindowShouldClose()) {

        if (IsKeyPressed(KEY_ESCAPE)) { quit = true; done = true; }
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_KP_8)) {
            cursor = (cursor - 1 + n) % n;
        }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_KP_2)) {
            cursor = (cursor + 1) % n;
        }
        for (int i = 0; i < n && i < 9; i++) {
            if (IsKeyPressed(KEY_ONE + i)) cursor = i;
        }
        if (IsKeyPressed(KEY_ENTER) ||
            IsKeyPressed(KEY_KP_ENTER) ||
            IsKeyPressed(KEY_SPACE)) {
            done = true;
        }

        BeginDrawing();
        ClearBackground((Color){ 16, 16, 32, 255 });

        const char *title = "Select Game Pack";
        int tw = MeasureText(title, 24);
        DrawText(title, (W - tw) / 2, 32, 24, RAYWHITE);

        int row_h = 24;
        int list_h = n * row_h;
        int top = (H - list_h) / 2 - 8;
        for (int i = 0; i < n; i++) {
            char line[96];
            snprintf(line, sizeof line, " %d. %s", i + 1, list[i].name);
            Color fg = (i == cursor) ? YELLOW : RAYWHITE;
            int x = W / 2 - 200;
            int y = top + i * row_h;
            if (i == cursor) {
                DrawRectangle(x - 6, y - 2, 412, row_h, (Color){ 40, 40, 70, 255 });
            }
            DrawText(line, x, y, 20, fg);
        }

        const char *hint = "UP/DN select   ENTER load   ESC quit";
        int hw = MeasureText(hint, 16);
        DrawText(hint, (W - hw) / 2, H - 40, 16, GRAY);

        EndDrawing();
        PollInputEvents();
    }

    CloseWindow();
    if (quit) return false;
    *chosen = cursor;
    return true;
}
