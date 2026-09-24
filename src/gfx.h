// src/gfx.h
//
// The shell's entire drawing surface, as an immediate-mode primitive layer.
// Two backends implement it with identical behaviour:
//
//   src/gfx_raylib.c   wraps raylib          (desktop / web / Android)
//   ios/gfx_metal.mm   one Metal pipeline    (iOS, links no raylib)
//
// Drawing code calls these instead of raylib directly, so all of it is shared
// and only the primitives are swapped per platform. docs/IOS-BACKEND.md
// has the full inventory this was derived from: 85 raylib functions across 429
// call sites, of which the drawing ones are what live here.
//
// Deliberately NOT in this layer:
//   * Text. The shell has two text systems of its own -- src/bfont.c (a bitmap
//     strip from the pack) and src/text.c (an atlas baked from the pack's own
//     TrueType font). Both end up drawing textured quads through gfx_texture_*,
//     so they need no primitive of their own.
//   * Window, input, audio and file access. Those are platform seams, not
//     drawing: see src/plat_android.c and (to come) src/plat_ios.c.

#ifndef OB_GFX_H
#define OB_GFX_H

#include "ob_types.h"
#include "gfx.h"

// ---- frame ----------------------------------------------------------------

void gfx_frame_begin(void);
void gfx_frame_end(void);
void gfx_clear(Color color);

// ---- primitives -----------------------------------------------------------

void gfx_rect(int x, int y, int w, int h, Color color);
void gfx_rect_lines(int x, int y, int w, int h, Color color);
// `roundness` is raylib's: the corner radius as a fraction (0..1) of the
// shorter side, so a panel keeps the same visual corner at any size.
void gfx_rect_rounded(int x, int y, int w, int h, float roundness,
                      int segments, Color color);
void gfx_rect_rounded_lines(int x, int y, int w, int h, float roundness,
                            int segments, Color color);
void gfx_triangle(Vector2 a, Vector2 b, Vector2 c, Color color);
void gfx_circle(int cx, int cy, float radius, Color color);

// ---- chrome labels --------------------------------------------------------
//
// The ONE piece of text this layer draws, and deliberately the only one: the
// touch chrome's button captions ("Esc", "Y", "N"), which are painted in
// window pixels over the letterbox, outside the game buffer, at a size derived
// from the button. The game's own text goes through src/bfont.c and
// src/text.c, which draw the pack's font as textured quads and need nothing
// here.
//
// The backend's built-in UI font: raylib's DrawText, and on iOS a small baked
// atlas. Sizes are in window pixels.
void gfx_label(const char *text, int x, int y, int size, Color color);
int  gfx_label_width(const char *text, int size);

// ---- clipping -------------------------------------------------------------
//
// Rectangular scissor in window pixels. Nested regions are not supported (no
// backend here keeps a stack); the shell only ever uses one at a time.
void gfx_clip_begin(int x, int y, int w, int h);
void gfx_clip_end(void);

// ---- textures -------------------------------------------------------------

// Upload decoded pixels. The Image is not retained: the caller still owns and
// frees it. A failed upload returns a texture with id 0, which draws nothing.
Texture2D gfx_texture_from_image(Image img);
void      gfx_texture_free(Texture2D t);
// Point filtering: the pack is pixel art, and every texture in the game is
// sampled this way. The one exception is gfx_texture_smooth.
void      gfx_texture_point(Texture2D t);
// Smooth filtering, for the whole frame fitted DOWN to a window smaller than
// the smallest screen (present.c): shrunk by a fraction, it keeps every pixel
// row and column instead of dropping some. Point filtering is put back when
// the window is large enough again.
void      gfx_texture_smooth(Texture2D t);
// Clamped edges as well, which is what stops a neighbouring tile bleeding in
// at a sub-pixel scroll position. Only the map tiles need it.
void      gfx_texture_point_clamp(Texture2D t);

// The workhorse: a source rect of `t` into a destination rect, tinted. A
// negative src height means the source is stored bottom-up (what a render
// target is), matching raylib.
void gfx_texture_draw(Texture2D t, Rectangle src, Rectangle dst, Color tint);

// ---- CPU images -----------------------------------------------------------
//
// Decoded pixels, before upload. `ext` is the file extension including the dot
// (".png"), as raylib takes it: the decoder picks the format from it.
Image gfx_image_from_memory(const char *ext, const unsigned char *bytes, int size);
Image gfx_image_solid(int w, int h, Color color);
void  gfx_image_free(Image img);
void  gfx_image_fill_rect(Image *dst, int x, int y, int w, int h, Color color);
// Blit a source rect of `src` into `dst` at `dst_rect`, tinted.
void  gfx_image_blit(Image *dst, Image src, Rectangle src_rect,
                     Rectangle dst_rect, Color tint);

// ---- camera ---------------------------------------------------------------
//
// A whole-frame integer zoom, used only when a pack declares a fixed buffer
// larger than its own design size (src/present.c). Nothing else transforms.
void gfx_zoom_begin(float zoom);
void gfx_zoom_end(void);

// ---- offscreen frame buffer ----------------------------------------------
//
// src/present.c renders the whole frame into one of these at the pack's buffer
// size, then blits it once, integer-scaled, into the window.
RenderTexture2D gfx_target_create(int w, int h);
void            gfx_target_free(RenderTexture2D rt);
void            gfx_target_begin(RenderTexture2D rt);
void            gfx_target_end(void);

#endif // OB_GFX_H
