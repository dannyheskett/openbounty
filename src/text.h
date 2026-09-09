// src/text.h
//
// The modern text backend: a proportional, anti-aliased TrueType face at
// the size the pack declares, drawn on its own baseline with the font's own
// advances. bfont delegates to this when a modern pack carries a "font"
// block; legacy packs never reach it and keep the bitmap strip and its cell.
//
// Metrics (line height, the advance of '0') are needed by the layout before
// the window exists, so they are computed CPU-side from the font bytes by
// text_preload; the atlas texture is built later by text_init.

#ifndef OB_TEXT_H
#define OB_TEXT_H

#include "raylib.h"
#include <stdbool.h>

struct Resources;

// Read the TTF from the pack and compute the metrics at the declared size.
// No GL. Returns false when the pack has no usable font block.
bool text_preload(const struct Resources *res);

// The same from a file on disk, for tests and tools. The bytes are kept
// until text_shutdown.
bool text_preload_file(const char *path, int size, int caps);

// Build the atlas (needs GL). Returns false on failure; then bfont falls
// back to the strip.
bool text_init(void);
void text_shutdown(void);
bool text_ready(void);

// Rebuild the atlas at cell times `zoom` so a frame rendered at that zoom
// draws sharp glyphs; the metrics in design units do not change.
void text_set_zoom(int zoom);

int  text_line_h(void);          // ascent + descent at the declared size
int  text_digit_w(void);         // advance of '0', for code that still budgets columns
int  text_width(const char *s);  // width of one line in design pixels (stops at '\n')

void text_draw(const char *s, int x, int y, Color c);

// Pixel-width word wrap. Copies one line of at most `max_w` pixels into
// `out`, breaking at the last space, and advances *p. A single '\n' in the
// source is a space; a blank line is a paragraph break and ends the line.
// Returns the number of characters consumed, 0 when *p is exhausted.
int  text_take_line(const char **p, int max_w, char *out, int cap);

#endif
