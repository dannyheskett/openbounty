// Pixel editor (GB-250..GB-255).
//
// Deliberately NOT a general image editor. It is scoped to pack sprites: the
// image's own dimensions, the pack's palette as the only selectable colours,
// and binary alpha. No layers, no filters, no arbitrary canvas sizes. Competing
// with Aseprite is a losing fight; being the fastest way to fix a tile that
// looks wrong in context is winnable.
//
// The thing that makes it worth having at all is the context preview: a tile
// that looks right alone routinely looks wrong tiled, and the only way to see
// that is to draw it against its neighbours.

#include "gb_ui.h"

#include "raygui.h"
#include "assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PX_MAX_W 128
#define PX_MAX_H 128
#define PX_FRAMES 4
#define PX_UNDO 64

typedef struct {
    bool     open;
    char     path[GB_PATH_MAX];       // pack-relative, of frame 0
    char     stem[128];               // "peasants" for peasants_00.png
    bool     animated;                // a _NN frame set rather than one image
    int      w, h;
    int      frames;
    int      frame;                   // frame being edited
    Color    px[PX_FRAMES][PX_MAX_H][PX_MAX_W];
    bool     dirty;

    int      zoom;
    Vector2  pan;
    int      color;                   // palette index
    int      tool;                    // 0 pencil 1 eraser 2 fill 3 line 4 rect 5 pick
    bool     onion;
    bool     playing;
    double   play_at;
    int      play_frame;

    Vector2  drag_from;
    bool     dragging;

    // Per-frame undo ring. Small and whole-frame: an image is at most 128x128
    // and the simplicity is worth more here than the memory.
    Color   *undo_buf[PX_UNDO];
    int      undo_frame[PX_UNDO];
    int      undo_count, undo_at;
} GbPixel;

static GbPixel P;

extern Color PAL[];

// --- io -----------------------------------------------------------------------

static void split_frame_name(const char *path, char *stem, size_t n,
                             bool *animated) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    snprintf(stem, n, "%s", base);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    size_t len = strlen(stem);
    // "<name>_00" is one frame of a set; anything else is a single image.
    *animated = (len > 3 && stem[len - 3] == '_' &&
                 stem[len - 2] >= '0' && stem[len - 2] <= '9' &&
                 stem[len - 1] >= '0' && stem[len - 1] <= '9');
    if (*animated) stem[len - 3] = 0;
}

bool gb_pixel_open(const char *pack_rel_path) {
    memset(&P, 0, sizeof P);
    snprintf(P.path, sizeof P.path, "%s", pack_rel_path);
    split_frame_name(pack_rel_path, P.stem, sizeof P.stem, &P.animated);

    char dir[GB_PATH_MAX];
    snprintf(dir, sizeof dir, "%s", pack_rel_path);
    char *slash = strrchr(dir, '/');
    if (slash) *(slash + 1) = 0; else dir[0] = 0;

    P.frames = P.animated ? PX_FRAMES : 1;
    for (int f = 0; f < P.frames; f++) {
        char rel[GB_PATH_MAX * 2];
        if (P.animated) snprintf(rel, sizeof rel, "%s%s_%02d.png", dir, P.stem, f);
        else            snprintf(rel, sizeof rel, "%s", pack_rel_path);

        Texture2D t = LoadAssetTexture(rel);
        if (!t.id) {
            if (f == 0) return false;
            P.frames = f;                 // a short set is still editable
            break;
        }
        Image img = LoadImageFromTexture(t);
        if (img.width > PX_MAX_W || img.height > PX_MAX_H) {
            UnloadImage(img);
            return false;
        }
        P.w = img.width;
        P.h = img.height;
        for (int y = 0; y < img.height; y++)
            for (int x = 0; x < img.width; x++)
                P.px[f][y][x] = GetImageColor(img, x, y);
        UnloadImage(img);
    }
    P.open = true;
    P.zoom = 8;
    P.color = 15;
    P.onion = true;
    return true;
}

// Writing goes straight to the pack directory, alongside everything else the
// editor saves. Alpha is forced binary: the pack's renderer treats it that way
// and a half-transparent edge reads as dirt at 48x34.
bool gb_pixel_save(const char *pack_root) {
    if (!P.open) return false;
    char dir[GB_PATH_MAX];
    snprintf(dir, sizeof dir, "%s", P.path);
    char *slash = strrchr(dir, '/');
    if (slash) *(slash + 1) = 0; else dir[0] = 0;

    for (int f = 0; f < P.frames; f++) {
        Image img = GenImageColor(P.w, P.h, BLANK);
        for (int y = 0; y < P.h; y++)
            for (int x = 0; x < P.w; x++) {
                Color c = P.px[f][y][x];
                c.a = c.a >= 128 ? 255 : 0;
                ImageDrawPixel(&img, x, y, c);
            }
        char out[GB_PATH_MAX * 3];
        if (P.animated)
            snprintf(out, sizeof out, "%s/%s%s_%02d.png", pack_root, dir, P.stem, f);
        else
            snprintf(out, sizeof out, "%s/%s", pack_root, P.path);
        ExportImage(img, out);
        UnloadImage(img);
    }
    P.dirty = false;
    return true;
}

void gb_pixel_close(void) {
    for (int i = 0; i < P.undo_count; i++) free(P.undo_buf[i]);
    memset(&P, 0, sizeof P);
}

bool gb_pixel_is_open(void) { return P.open; }
bool gb_pixel_dirty(void) { return P.dirty; }

// --- undo ---------------------------------------------------------------------

static void px_snapshot(void) {
    for (int i = P.undo_at; i < P.undo_count; i++) free(P.undo_buf[i]);
    P.undo_count = P.undo_at;
    if (P.undo_count == PX_UNDO) {
        free(P.undo_buf[0]);
        memmove(P.undo_buf, P.undo_buf + 1, sizeof(Color *) * (PX_UNDO - 1));
        memmove(P.undo_frame, P.undo_frame + 1, sizeof(int) * (PX_UNDO - 1));
        P.undo_count--; P.undo_at--;
    }
    size_t n = (size_t)P.w * P.h * sizeof(Color);
    Color *buf = malloc(n);
    for (int y = 0; y < P.h; y++)
        memcpy(buf + y * P.w, P.px[P.frame][y], (size_t)P.w * sizeof(Color));
    P.undo_buf[P.undo_count] = buf;
    P.undo_frame[P.undo_count] = P.frame;
    P.undo_count++;
    P.undo_at = P.undo_count;
}

static void px_undo(void) {
    if (P.undo_at <= 0) return;
    P.undo_at--;
    int f = P.undo_frame[P.undo_at];
    Color *buf = P.undo_buf[P.undo_at];
    for (int y = 0; y < P.h; y++)
        memcpy(P.px[f][y], buf + y * P.w, (size_t)P.w * sizeof(Color));
    P.frame = f;
    P.dirty = true;
}

// --- drawing tools ------------------------------------------------------------

static void put(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= P.w || y >= P.h) return;
    P.px[P.frame][y][x] = c;
    P.dirty = true;
}

static void line(int x0, int y0, int x1, int y1, Color c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static bool same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void fill(int sx, int sy, Color c) {
    if (sx < 0 || sy < 0 || sx >= P.w || sy >= P.h) return;
    Color from = P.px[P.frame][sy][sx];
    if (same(from, c)) return;
    static int st[PX_MAX_W * PX_MAX_H][2];
    int top = 0;
    st[top][0] = sx; st[top][1] = sy; top++;
    while (top > 0) {
        top--;
        int x = st[top][0], y = st[top][1];
        if (x < 0 || y < 0 || x >= P.w || y >= P.h) continue;
        if (!same(P.px[P.frame][y][x], from)) continue;
        P.px[P.frame][y][x] = c;
        const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4 && top < PX_MAX_W * PX_MAX_H; i++) {
            st[top][0] = x + dx[i]; st[top][1] = y + dy[i]; top++;
        }
    }
    P.dirty = true;
}

// --- frame ---------------------------------------------------------------------

void gb_pixel_frame(int top, const char *pack_root, char *status,
                    size_t status_sz) {
    if (!P.open) return;
    int W = GetScreenWidth(), H = GetScreenHeight();
    int panel = 190;

    Rectangle canvas = { (float)panel, (float)top,
                         (float)(W - panel - 220), (float)(H - top) };
    Vector2 org = { canvas.x + P.pan.x + 20, canvas.y + P.pan.y + 20 };

    // zoom / pan
    if (CheckCollisionPointRec(GetMousePosition(), canvas)) {
        float wh = GetMouseWheelMove();
        if (wh > 0 && P.zoom < 32) P.zoom++;
        if (wh < 0 && P.zoom > 1) P.zoom--;
    }
    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON)) {
        Vector2 d = GetMouseDelta();
        P.pan.x += d.x; P.pan.y += d.y;
    }

    // checkerboard, so transparency is visible rather than guessed at
    BeginScissorMode((int)canvas.x, (int)canvas.y, (int)canvas.width,
                     (int)canvas.height);
    for (int y = 0; y < P.h; y++)
        for (int x = 0; x < P.w; x++) {
            Rectangle r = { org.x + x * P.zoom, org.y + y * P.zoom,
                            (float)P.zoom, (float)P.zoom };
            DrawRectangleRec(r, ((x + y) & 1) ? (Color){ 60, 60, 66, 255 }
                                              : (Color){ 44, 44, 50, 255 });
        }
    // onion skin of the previous frame (GB-253)
    if (P.onion && P.animated && P.frames > 1) {
        int prev = (P.frame + P.frames - 1) % P.frames;
        for (int y = 0; y < P.h; y++)
            for (int x = 0; x < P.w; x++) {
                Color c = P.px[prev][y][x];
                if (c.a < 128) continue;
                c.a = 60;
                DrawRectangle((int)(org.x + x * P.zoom), (int)(org.y + y * P.zoom),
                              P.zoom, P.zoom, c);
            }
    }
    for (int y = 0; y < P.h; y++)
        for (int x = 0; x < P.w; x++) {
            Color c = P.px[P.frame][y][x];
            if (c.a < 128) continue;
            DrawRectangle((int)(org.x + x * P.zoom), (int)(org.y + y * P.zoom),
                          P.zoom, P.zoom, c);
        }
    if (P.zoom >= 6) {
        Color g = { 255, 255, 255, 24 };
        for (int x = 0; x <= P.w; x++)
            DrawLine((int)(org.x + x * P.zoom), (int)org.y,
                     (int)(org.x + x * P.zoom), (int)(org.y + P.h * P.zoom), g);
        for (int y = 0; y <= P.h; y++)
            DrawLine((int)org.x, (int)(org.y + y * P.zoom),
                     (int)(org.x + P.w * P.zoom), (int)(org.y + y * P.zoom), g);
    }
    EndScissorMode();

    // painting
    Vector2 m = GetMousePosition();
    int px = (int)((m.x - org.x) / P.zoom), py = (int)((m.y - org.y) / P.zoom);
    bool inside = CheckCollisionPointRec(m, canvas) &&
                  px >= 0 && py >= 0 && px < P.w && py < P.h;
    Color ink = PAL[P.color];
    if (inside) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            px_snapshot();
            P.drag_from = (Vector2){ (float)px, (float)py };
            P.dragging = true;
            if (P.tool == 2) fill(px, py, ink);
            if (P.tool == 5) {
                // eyedropper: match to the nearest palette entry, since only
                // palette colours are selectable
                Color c = P.px[P.frame][py][px];
                int best = 0; long bd = 1L << 40;
                for (int i = 0; i < 256; i++) {
                    long d = (long)(PAL[i].r - c.r) * (PAL[i].r - c.r) +
                             (long)(PAL[i].g - c.g) * (PAL[i].g - c.g) +
                             (long)(PAL[i].b - c.b) * (PAL[i].b - c.b);
                    if (d < bd) { bd = d; best = i; }
                }
                P.color = best;
                snprintf(status, status_sz, "Picked palette #%d", best);
            }
        }
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            if (P.tool == 0) put(px, py, ink);
            if (P.tool == 1) put(px, py, BLANK);
        }
        if (P.dragging && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (P.tool == 3) line((int)P.drag_from.x, (int)P.drag_from.y,
                                  px, py, ink);
            if (P.tool == 4) {
                int x0 = (int)fminf(P.drag_from.x, (float)px);
                int x1 = (int)fmaxf(P.drag_from.x, (float)px);
                int y0 = (int)fminf(P.drag_from.y, (float)py);
                int y1 = (int)fmaxf(P.drag_from.y, (float)py);
                for (int x = x0; x <= x1; x++) { put(x, y0, ink); put(x, y1, ink); }
                for (int y = y0; y <= y1; y++) { put(x0, y, ink); put(x1, y, ink); }
            }
            P.dragging = false;
        }
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z)) px_undo();
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S)) {
        gb_pixel_save(pack_root);
        snprintf(status, status_sz, "Saved %s", P.stem);
    }

    // --- left panel: tools + palette ---
    DrawRectangle(0, top, panel, H - top, (Color){ 28, 28, 34, 255 });
    int y = top + 8;
    DrawText(P.stem, 10, y, 14, RAYWHITE); y += 18;
    DrawText(TextFormat("%dx%d  %d frame%s", P.w, P.h, P.frames,
                        P.frames == 1 ? "" : "s"), 10, y, 11, GRAY);
    y += 20;
    static const char *TOOLS[] = { "Pencil", "Eraser", "Fill", "Line", "Rect",
                                   "Pick" };
    for (int i = 0; i < 6; i++) {
        Rectangle r = { 10.0f + (i % 2) * 88, (float)(y + (i / 2) * 26), 84, 22 };
        if (GuiButton(r, TOOLS[i])) P.tool = i;
        if (P.tool == i) DrawRectangleLinesEx(r, 2, YELLOW);
    }
    y += 88;
    DrawText(TextFormat("Zoom %dx", P.zoom), 10, y, 11, GRAY); y += 18;

    if (P.animated) {
        DrawText("FRAMES", 10, y, 12, LIGHTGRAY); y += 16;
        for (int f = 0; f < P.frames; f++) {
            Rectangle r = { 10.0f + f * 44, (float)y, 40, 22 };
            if (GuiButton(r, TextFormat("%d", f))) P.frame = f;
            if (P.frame == f) DrawRectangleLinesEx(r, 2, YELLOW);
        }
        y += 28;
        GuiCheckBox((Rectangle){ 10, (float)y, 16, 16 }, "Onion skin", &P.onion);
        y += 22;
        if (GuiButton((Rectangle){ 10, (float)y, 84, 22 },
                      P.playing ? "Stop" : "Play")) P.playing = !P.playing;
        y += 30;
    }

    DrawText("PALETTE", 10, y, 12, LIGHTGRAY); y += 16;
    for (int i = 0; i < 256; i++) {
        int col = i % 16, row = i / 16;
        Rectangle r = { 10.0f + col * 11, (float)(y + row * 11), 10, 10 };
        DrawRectangleRec(r, PAL[i]);
        if (i == P.color) DrawRectangleLinesEx(r, 2, WHITE);
        if (CheckCollisionPointRec(m, r) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            P.color = i;
    }

    // --- right panel: context preview (GB-254) ---
    int rx = W - 210;
    DrawRectangle(rx, top, 210, H - top, (Color){ 28, 28, 34, 255 });
    DrawText("IN CONTEXT", rx + 10, top + 8, 12, LIGHTGRAY);
    DrawText("A tile that looks right", rx + 10, top + 26, 10, GRAY);
    DrawText("alone often looks wrong", rx + 10, top + 38, 10, GRAY);
    DrawText("tiled. Check it here.", rx + 10, top + 50, 10, GRAY);

    // 3x3 of the edited image, at true pixel size
    int cw = P.w, ch = P.h;
    for (int ty = 0; ty < 3; ty++)
        for (int tx = 0; tx < 3; tx++)
            for (int yy = 0; yy < ch; yy++)
                for (int xx = 0; xx < cw; xx++) {
                    Color c = P.px[P.frame][yy][xx];
                    if (c.a < 128) continue;
                    DrawPixel(rx + 20 + tx * cw + xx, top + 76 + ty * ch + yy, c);
                }

    // animation playback at the in-game interval
    if (P.playing && P.animated) {
        if (GetTime() - P.play_at > 0.15) {
            P.play_frame = (P.play_frame + 1) % P.frames;
            P.play_at = GetTime();
        }
        int ay = top + 76 + 3 * ch + 20;
        DrawText("PLAYBACK", rx + 10, ay - 16, 11, LIGHTGRAY);
        for (int yy = 0; yy < ch; yy++)
            for (int xx = 0; xx < cw; xx++) {
                Color c = P.px[P.play_frame][yy][xx];
                if (c.a < 128) continue;
                DrawRectangle(rx + 20 + xx * 2, ay + yy * 2, 2, 2, c);
            }
    }

    if (inside)
        snprintf(status, status_sz, "%d, %d   palette #%d%s", px, py, P.color,
                 P.dirty ? "   (unsaved)" : "");
}
