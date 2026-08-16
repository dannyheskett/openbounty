#ifndef OB_BFONT_H
#define OB_BFONT_H

#include "raylib.h"

// bitmap font. 8x8 glyphs in a 128-glyph strip, ASCII 0..127.
// The strip PNG is white glyphs on transparent; draw tints the glyphs
// with the given color.

// The strip is 8x8 on disk. What these report is the glyph's size ON SCREEN,
// which the pack scales with render.ui_scale -- an 8px glyph in a buffer three
// times the size of the original design space is unreadable. Every caller
// measures text with these, so scaling here scales all text layout with it.
#define BFONT_SRC_GLYPH_W  8
#define BFONT_SRC_GLYPH_H  8
int bfont_glyph_w(void);
int bfont_glyph_h(void);
#define BFONT_GLYPH_W  (bfont_glyph_w())
#define BFONT_GLYPH_H  (bfont_glyph_h())

bool    bfont_init(const char *png_path);
void    bfont_shutdown(void);
bool    bfont_ready(void);

// `text` may contain '\n'; newlines advance y by BFONT_GLYPH_H.
// Out-of-range bytes are rendered as spaces.
void    bfont_draw(const char *text, int x, int y, Color c);
void    bfont_draw_centered(const char *text, int cx, int y, Color c);
Vector2 bfont_measure(const char *text);
int     bfont_line_height(void);

#endif
