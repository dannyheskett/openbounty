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

// Load the pack's font. Two routes behind one set of names:
//
//   legacy, or any pack without a "font" block: the bitmap strip in
//   sprites.font, drawn into the 8 * ui_scale cell, unchanged;
//
//   modern with a "font" block: the proportional TrueType backend (text.c)
//   at the size the pack declares, anti-aliased, on its own metrics.
//   BFONT_GLYPH_H is then the face's line height and BFONT_GLYPH_W the
//   advance of '0', so layout that counts rows and columns follows the font.
//
// bfont_preload_metrics runs BEFORE layout_init (no GL: it reads the font
// bytes and computes the metrics the layout needs); bfont_init builds the
// texture once the window exists.
struct Resources;
bool    bfont_preload_metrics(const struct Resources *res);
bool    bfont_init(const struct Resources *res);
void    bfont_shutdown(void);
bool    bfont_is_modern(void);

// Rasterisation zoom for the TrueType route (see present.c); the strip
// route ignores it.
void    bfont_set_zoom(int zoom);

// Word wrap to a pixel width: copies one line into `out`, breaking at the
// last space, advances *p, returns the characters consumed (0 at the end).
// Legacy wraps by max_w / BFONT_GLYPH_W characters and keeps every '\n' as a
// line break, as the dialog and prompt always did. Modern wraps by the real
// glyph cell and keeps every '\n' too, so authored menus and tables hold.
int     bfont_take_line(const char **p, int max_w, char *out, int cap);

int     bfont_text_width(const char *text);
void    bfont_draw_right(const char *text, int x_right, int y, Color c);
bool    bfont_ready(void);

// `text` may contain '\n'; newlines advance y by BFONT_GLYPH_H.
// Out-of-range bytes are rendered as spaces.
void    bfont_draw(const char *text, int x, int y, Color c);
void    bfont_draw_centered(const char *text, int cx, int y, Color c);
Vector2 bfont_measure(const char *text);
int     bfont_line_height(void);

#endif
