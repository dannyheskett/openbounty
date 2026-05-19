#include "end_game.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "raylib.h"
#include <string.h>
#include <stdio.h>

// Cached state for the active end screen.
static bool s_won = false;
static char s_body[1024] = { 0 };

void screen_end_game_open(bool won, const char *body) {
    s_won = won;
    s_body[0] = '\0';
    if (body) {
        size_t n = 0;
        while (n + 1 < sizeof(s_body) && body[n]) {
            s_body[n] = body[n]; n++;
        }
        s_body[n] = '\0';
    }
    if (won) {
        if (views_active() != VIEW_WIN) views_push(VIEW_WIN);
    } else {
        if (views_active() != VIEW_LOSE) views_push(VIEW_LOSE);
    }
}

void screen_end_game_draw(const Game *g, const Sprites *s) {
    (void)g;

    // Fullscreen layout: left side gets the rendered text, right side
    // gets a half-scale ending image. The text area is intentionally
    // wider than 50% (216px vs 72px for the image): the win/lose body
    // strings in game.json average ~25 chars per line, which doesn't
    // fit cleanly in a 144px half. Shrinking the image to half-scale
    // (72x85, centered vertically in the right column) buys the text
    // enough horizontal room to render without aggressive wrapping
    // and truncation off the bottom.
    int total_left  = CL_MAP_X;             // chrome's left frame stays
    int total_top   = CL_MAP_Y;             // status bar + bar strip stay
    int total_w     = CL_SCREEN_W - CL_MAP_X - CL_FRAME_RIGHT_W;
    int total_h     = CL_SCREEN_H - CL_MAP_Y - CL_FRAME_BOTTOM_H;

    // Paint full area DBLUE first (CS_ENDING background).
    DrawRectangle(total_left, total_top, total_w, total_h, PAL_CLR(DBLUE));

    // Right column: the ending image at half-scale (72x85), centered
    // vertically in an 80-px-wide reservation. The original asset is
    // 144x170 — half-scale preserves aspect ratio.
    int img_col_w   = 80;
    int img_col_x   = total_left + total_w - img_col_w;
    Texture2D img = s_won ? s->ending_win : s->ending_lose;
    if (img.id && img.width > 0) {
        int dst_w = img.width  / 2;
        int dst_h = img.height / 2;
        int dst_x = img_col_x + (img_col_w - dst_w) / 2;
        int dst_y = total_top + (total_h - dst_h) / 2;
        Rectangle src = { 0, 0, (float)img.width, (float)img.height };
        Rectangle dst = { (float)dst_x, (float)dst_y,
                          (float)dst_w, (float)dst_h };
        DrawTexturePro(img, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }

    // Text column: the remaining width on the left. 27 chars/line at
    // 8px font with 4px padding is enough to render the standard win
    // body without truncating the closing lines.
    int text_col_w = total_w - img_col_w;
    int pad     = 4;
    int line_h  = BFONT_GLYPH_H + 1;
    int tx      = total_left + pad;
    int ty      = total_top + pad;
    int floor_y = total_top + total_h - pad;

    int max_chars = (text_col_w - 2 * pad) / BFONT_GLYPH_W;
    if (max_chars < 1) max_chars = 1;

    const char *p = s_body;
    char line[160];
    while (*p && ty + line_h <= floor_y) {
        int n = 0;
        // Read one logical line (up to '\n' or NUL).
        while (*p && *p != '\n' && n + 1 < (int)sizeof(line)) {
            line[n++] = *p++;
        }
        line[n] = '\0';
        if (*p == '\n') p++;

        // Word-wrap if longer than max_chars. Break at the last space
        // that still fits; if no space, hard-break.
        char *cursor = line;
        while ((int)strlen(cursor) > max_chars && ty + line_h <= floor_y) {
            int cut = max_chars;
            while (cut > 0 && cursor[cut] != ' ') cut--;
            if (cut <= 0) cut = max_chars;
            char saved = cursor[cut];
            cursor[cut] = '\0';
            bfont_draw(cursor, tx, ty, PAL_CLR(WHITE));
            ty += line_h;
            cursor[cut] = saved;
            cursor += cut;
            while (*cursor == ' ') cursor++;
        }
        if (*cursor && ty + line_h <= floor_y) {
            bfont_draw(cursor, tx, ty, PAL_CLR(WHITE));
            ty += line_h;
        }
    }
}
