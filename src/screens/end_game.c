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

    // Layout matches openkb's win_game / lose_game (src/game.c:4431):
    //   full = the map+sidebar area minus right chrome
    //        = { CL_MAP_X, CL_MAP_Y, 288, 170 }  in our units
    //   right-align the ending image inside `full` at its NATIVE size
    //        = 144x170 PNG, no scaling
    //   text rectangle = `full` with image width subtracted on the right
    //        = { CL_MAP_X, CL_MAP_Y, 144, 170 }
    //   text rendered character-cell at (text.x, text.y), 18 cols x
    //   21 rows of 8x8 glyphs, NO padding, NO inter-line gap. The
    //   body strings in game.json are authored pre-wrapped to 18
    //   chars per line, matching the original DOS layout (see
    //   openkb/data/free/endwin.txt for the canonical formatting).
    int total_left  = CL_MAP_X;
    int total_top   = CL_MAP_Y;
    int total_w     = CL_SCREEN_W - CL_MAP_X - CL_FRAME_RIGHT_W;
    int total_h     = CL_SCREEN_H - CL_MAP_Y - CL_FRAME_BOTTOM_H;

    // Paint full area DBLUE first (CS_ENDING background).
    DrawRectangle(total_left, total_top, total_w, total_h, PAL_CLR(DBLUE));

    // Right side: the ending image at native size, right-aligned.
    Texture2D img = s_won ? s->ending_win : s->ending_lose;
    int img_w = (img.id && img.width  > 0) ? img.width  : 0;
    int img_h = (img.id && img.height > 0) ? img.height : 0;
    if (img_w > 0 && img_h > 0) {
        Rectangle src = { 0, 0, (float)img_w, (float)img_h };
        Rectangle dst = { (float)(total_left + total_w - img_w),
                          (float)total_top,
                          (float)img_w,
                          (float)img_h };
        DrawTexturePro(img, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }

    // Text rectangle = `full` minus the image's width.
    int text_w  = total_w - img_w;
    int line_h  = BFONT_GLYPH_H;   // openkb-parity: no inter-line gap
    int tx      = total_left;
    int ty      = total_top;
    int floor_y = total_top + total_h;

    int max_chars = text_w / BFONT_GLYPH_W;
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

        // Defensive word-wrap: the JSON body should already be authored
        // to fit max_chars, but if a translated/edited string sneaks in
        // longer we'd rather wrap than draw past the rect into the
        // image. Break at the last space that still fits; if no space,
        // hard-break.
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
        if (ty + line_h <= floor_y) {
            // Render even empty lines so author-controlled paragraph
            // breaks (blank lines in the JSON body) come through.
            bfont_draw(cursor, tx, ty, PAL_CLR(WHITE));
            ty += line_h;
        }
    }
}
