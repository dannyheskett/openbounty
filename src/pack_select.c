// Pre-game pack selector. Runs before any pack is loaded, so it carries its
// own face: Liberation Sans, embedded (src/font_sans.inc, SIL OFL 1.1).

#include "frame_host.h"
#include "input_host.h"
#include "pack_select.h"
#include "pack.h"
#include "raylib.h"
#include "font_sans.inc"

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
    HideCursor();   // no mouse support: rows answer to keys and touch

    PackSelectState st = { 0, false, false };

    // The packs' own titles from their manifests ("The Glory of Rome"), the
    // file name when a pack has none. Rows are touch-sized.
    char titles[16][96];
    for (int i = 0; i < n && i < 16; i++) {
        Pack *pk = pack_open(list[i].path);
        const char *t = pk ? pack_name(pk) : "";
        snprintf(titles[i], sizeof titles[i], "%s", (t && t[0]) ? t : list[i].name);
        if (pk) pack_close(pk);
    }
    const int ROW_H = 48, ROW_W = 440, FONT = 28;
    // Both sizes loaded at their own pixel size, so the glyphs are drawn 1:1.
    Font face = LoadFontFromMemory(".ttf", FONT_SANS, FONT_SANS_LEN, FONT, NULL, 0);
    Font small = LoadFontFromMemory(".ttf", FONT_SANS, FONT_SANS_LEN, 18, NULL, 0);
    SetTextureFilter(face.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(small.texture, TEXTURE_FILTER_BILINEAR);

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

        // Touch: this screen never goes through the touch layer (it draws
        // straight to the window, no render target), so it samples the contact
        // itself and hit-tests the row rects drawn below in window pixels. A
        // tap selects and confirms in one go.
        {
            int mx, my;
            input_touch_sample();
            if (input_touch_pressed(&mx, &my)) {
                int top = (H - n * ROW_H) / 2;
                int x = (W - ROW_W) / 2;
                for (int i = 0; i < n; i++) {
                    int y = top + i * ROW_H;
                    if (mx >= x && mx < x + ROW_W && my >= y && my < y + ROW_H) {
                        in.digit = i;
                        in.confirm = true;
                        break;
                    }
                }
            }
        }

        pack_select_step(&st, &in, n);

        int cursor = st.cursor;

        BeginDrawing();
        ClearBackground((Color){ 16, 16, 32, 255 });

        const char *title = "Select Game Pack";
        int tw = (int)MeasureTextEx(face, title, FONT, 0).x;
        DrawTextEx(face, title, (Vector2){ (float)((W - tw) / 2), 32 }, FONT, 0, RAYWHITE);

        int top = (H - n * ROW_H) / 2;
        // A framed panel like the game's own: a gold edge, a rule under each
        // row, the chosen row lit gold with dark words.
        {
            int x = (W - ROW_W) / 2;
            DrawRectangle(x - 6, top - 6, ROW_W + 12, n * ROW_H + 12, (Color){ 12, 14, 30, 255 });
            DrawRectangleLinesEx((Rectangle){ (float)(x - 6), (float)(top - 6), (float)(ROW_W + 12),
                                              (float)(n * ROW_H + 12) }, 3, (Color){ 200, 160, 60, 255 });
        }
        for (int i = 0; i < n; i++) {
            const char *line = i < 16 ? titles[i] : list[i].name;
            Color fg = (i == cursor) ? (Color){ 16, 18, 36, 255 } : RAYWHITE;
            int x = (W - ROW_W) / 2;
            int y = top + i * ROW_H;
            if (i == cursor) DrawRectangle(x, y, ROW_W, ROW_H - 2, YELLOW);
            if (i + 1 < n) DrawRectangle(x, y + ROW_H - 2, ROW_W, 2, (Color){ 120, 96, 40, 255 });
            int lw = (int)MeasureTextEx(face, line, FONT, 0).x;
            DrawTextEx(face, line, (Vector2){ (float)(x + (ROW_W - lw) / 2), (float)(y + (ROW_H - FONT) / 2) },
                       FONT, 0, fg);
        }

        const char *hint = "Tap a pack, or choose with the arrows and Enter";
        int hw = (int)MeasureTextEx(small, hint, 18, 0).x;
        DrawTextEx(small, hint, (Vector2){ (float)((W - hw) / 2), (float)(H - 40) }, 18, 0, GRAY);

        EndDrawing();
        PollInputEvents();
        // Yield AFTER the extra poll, not before it -- see frame_host.h.
        // (This loop polls twice, so frame_host_end_frame() would put the
        // yield on the wrong side of the second poll.)
        frame_host_yield();
    }

    UnloadFont(face);
    UnloadFont(small);
    CloseWindow();
    if (st.quit) return false;
    *chosen = st.cursor;
    return true;
}
