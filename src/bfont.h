#ifndef OB_BFONT_H
#define OB_BFONT_H

#include "raylib.h"

// bitmap font. A horizontal strip of BFONT_GLYPHS glyphs, ASCII 0..127,
// white on transparent; draw tints them with the given colour.
//
// Two sizes, and they are not the same thing:
//
//   SOURCE size -- what the pack authored, measured off the strip itself
//   (width / BFONT_GLYPHS). A pack may ship 8x8 as the original did, or a
//   higher-resolution strip. Nothing but the source rectangle uses this.
//
//   ON-SCREEN size -- always 8 * render.ui_scale, and deliberately NOT tied to
//   the source. The whole layout is measured in 8px design units (CL_STATUS_H,
//   the panel column budgets, every row height), so this is a contract: change
//   it and the layout moves. A pack that ships a source glyph equal to the
//   on-screen size gets a crisp 1:1 blit; a smaller source is upscaled, which
//   is what an 8x8 strip does at ui_scale > 1.
#define BFONT_GLYPHS 128
int bfont_src_glyph_w(void);
int bfont_src_glyph_h(void);
int bfont_glyph_w(void);
int bfont_glyph_h(void);
#define BFONT_SRC_GLYPH_W  (bfont_src_glyph_w())
#define BFONT_SRC_GLYPH_H  (bfont_src_glyph_h())
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
