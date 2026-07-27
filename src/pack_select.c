// Pre-game pack selector. Runs before any pack is loaded, so it
// deliberately uses only raylib built-ins (default font, plain colors).

#include "frame_host.h"
#include "input_host.h"
#include "pack_select.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

void pack_select_step(PackSelectState *st, const PackSelectInput *in, int n) {
    if (!st || !in || n <= 0) return;

    // A dismissed window is a cancel, not a selection. Without this the loop
    // fell out with quit unset and the caller booted whatever row the cursor
    // happened to rest on.
    if (in->close || in->cancel) {
        st->quit = true;
        st->done = true;
        return;
    }

    if (in->up)   st->cursor = (st->cursor - 1 + n) % n;
    if (in->down) st->cursor = (st->cursor + 1) % n;
    if (in->digit >= 0 && in->digit < n) st->cursor = in->digit;

    if (in->confirm) st->done = true;
}

bool pack_select_flow(const PackEntry *list, int n, int *chosen) {
    if (!list || n <= 0 || !chosen) return false;

    const int W = 640;
    const int H = 400;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(W, H, "OpenBounty, select pack");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    PackSelectState st = { 0, false, false };

    while (!st.done) {

        PackSelectInput in;
        in.close   = frame_host_should_close();
        in.cancel  = input_key_pressed(KEY_ESCAPE);
        in.up      = input_key_pressed(KEY_UP)   || input_key_pressed(KEY_KP_8);
        in.down    = input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2);
        in.confirm = input_key_pressed(KEY_ENTER) ||
                     input_key_pressed(KEY_KP_ENTER) ||
                     input_key_pressed(KEY_SPACE);
        in.digit   = -1;
        for (int i = 0; i < n && i < 9; i++) {
            if (input_key_pressed(KEY_ONE + i)) in.digit = i;
        }

        pack_select_step(&st, &in, n);

        int cursor = st.cursor;

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
        // Yield AFTER the extra poll, not before it -- see frame_host.h.
        // (This loop polls twice, so frame_host_end_frame() would put the
        // yield on the wrong side of the second poll.)
        frame_host_yield();
    }

    CloseWindow();
    if (st.quit) return false;
    *chosen = st.cursor;
    return true;
}
