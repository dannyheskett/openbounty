// src/modern/uikit.h
//
// The modern screens' content builders: one look for every title strip,
// scene, picture, column of words and row, so the art is what the eye lands
// on and the interface is the same everywhere. Where a page goes is the page
// engine's alone (src/modern/page.h); these draw inside the rect it returns.
//
//   title     the strip across a page's top: its name at the left, what it is
//             about (the purse, the hero) at the right, a lattice band under it
//   scene     a place: the backdrop at 3x between its columns, the words under
//             it, and the rows along the foot
//   doc       paragraphs of words beside a picture, paged
//
// Words: every block of text is inset UK_INSET from its panel's edges, lines
// are uk_line_h() apart, and text that does not fit is cut with ".." -- a
// line at its end, a block on its last line. Colours: labels and titles gold,
// values and words white, what cannot be chosen dark grey.
//
// Legacy never includes this header.

#ifndef OB_MODERN_UIKIT_H
#define OB_MODERN_UIKIT_H

#include "modern/mlayout.h"
#include "modern/mlist.h"
#include "ob_types.h"
#include <stdbool.h>

struct Game;

#define UK_BAND   4     // the lattice band between parts, and above a page's rows
#define UK_INSET  12    // text inset inside a panel, on every side
// Every figure and troop standing still, out of a fight and in one: a frame
// every 150 ms, the combat tick's pace.
#define UK_IDLE_FPS (1000.0 / 150.0)
// A face that talks (a portrait, a villain's): two frames a second.
#define UK_FACE_FPS 2.0

// The colours every modern page uses.
Color uk_fill(void);      // a panel's fill
Color uk_ink(void);       // a lit row's text (solid)
Color uk_edge(void);      // a picture's thin gold edge
Color uk_edge_dim(void);  // the edge of an icon still to find
Color uk_ghost(void);     // an icon still to find (a tint)
Color uk_shade(void);     // laid over what cannot be used now
Color uk_hint_bg(void);   // behind a key name on a tile
Color uk_button(void);    // a count button's edge, and its bar
Color uk_bar_edge(void);  // the count bar's frame

int   uk_title_h(void);   // the title strip, without its band
int   uk_line_h(void);    // a text line's pitch

// The title strip at (x, y, w): `left` gold at the left, `right` gold at the
// right, and `close` (NULL: none) after it -- a button for Escape. When the
// strip cannot hold everything, Close gives up its key name, then `right`
// goes; only then is `left` cut, with "..". Returns the y under its band.
int  uk_title(int x, int y, int w, const char *left, const char *right, const char *close);
// "Gold 12000", for a place screen's title strip (the HUD is hidden there).
void uk_gold_text(const struct Game *g, char *out, int cap);
// A picture in a black square with a thin gold edge, at w x h (a whole
// multiple of the art).
void uk_picture(Texture2D t, int x, int y, int w, int h);
// The same at the art's own size, cut to w x h from its top left: a picture
// whose place is smaller than it loses its foot, never its scale.
void uk_picture_cut(Texture2D t, int x, int y, int w, int h);
// A figure: `t` at `scale` (a whole multiple) with its foot on `foot_y`,
// from `x`. Where it would pass `top_y` its top is cut in whole pixels of the
// art -- never squashed.
void uk_figure(Texture2D t, int x, int foot_y, int scale, int top_y, bool mirror);

// One line of text at (x, y), cut with ".." to `w`.
void uk_line(const char *text, int x, int y, int w, Color fg);
// Make `buf` end with ".." inside `w` (a line that has more after it).
void uk_mark_cut(char *buf, int cap, int w);
// `text` wrapped to `w` from (x, y), at most `max_lines` lines; a cut is
// marked on the last. Returns the y under the last line.
int  uk_lines_draw(const char *text, int x, int y, int w, int max_lines, Color fg);
// Words wrapped from (x, y) across w; while a line's top is above `pic_b` it
// starts at `pic_r` instead (beside a picture), then runs the full width.
// Stops before `max_y`, marking a cut. Returns the y under the last line.
int  uk_flow(int x, int y, int w, int pic_r, int pic_b, int max_y, const char *text, Color fg);
// Lines `text` wraps to at width w.
int  uk_lines(const char *text, int w);
// `text` wrapped to `w` and centred on `cx`, at most `max_lines` lines, a cut
// marked. Returns the y under the last line.
int  uk_words_centred(const char *text, int cx, int y, int w, int max_lines, Color fg);

// `n` full-width rows along the foot of `body` with a band above them; returns
// the y of the band (the words above stop there).
int  uk_foot_rows(ML_Rect body, int n, int cursor, MlRowFn fn, void *ctx, int touch_list);


// ---- the place scene ---------------------------------------------------------------

typedef struct {
    ML_Rect full;
    ML_Rect scene;      // the backdrop as drawn
    int     scale;
    int     trim;       // screen pixels cut off the backdrop's top
    int     intro_y, intro_h;
    int     rows_y;
    int     rows;
} UkScene;

// The backdrop as a band across `r` from `top`, `band_h` tall, at the largest
// whole scale (3 at most) its width allows: its top trimmed in whole source
// pixels, a column in each bar beside it (or the lattice), and a lattice
// divider under it.
UkScene uk_scene_band(ML_Rect r, int top, Texture2D backdrop, int band_h);
// The same band at a scale the caller has chosen (1..3, never wider than r):
// a scene note picks the largest at which its picture stands whole.
UkScene uk_scene_band_at(ML_Rect r, int top, Texture2D backdrop, int band_h, int scale);
// A figure standing on the backdrop's bottom edge at 2x, `x` screen pixels
// in from the backdrop's left.
void    uk_scene_figure(const UkScene *L, Texture2D t, int x);

// A row source over a fixed list of labels. `esc` is one more than the row
// Escape presses (0: none): that row shows the key while the keyboard is in
// use.
typedef struct { const char *label[8]; bool enabled[8]; int esc; } UkRows;
bool uk_rows_fn(void *ctx, int i, char *label, char *right, int cap);

// ---- paged words beside a picture ------------------------------------------------

// Paragraphs of words for an in-lay's body: each may start with a gap, and its
// first `label` bytes may be gold before a value.
#define UK_DOC_PARAS 32
typedef struct {
    int   off[UK_DOC_PARAS];
    int   label[UK_DOC_PARAS];
    Color fg[UK_DOC_PARAS];
    bool  gap[UK_DOC_PARAS];
    int   n;
    char  pool[4096];
    int   used;
    bool  next_gap;
} UkDoc;

void uk_doc_add(UkDoc *d, const char *text, Color fg);
// The next paragraph starts after a gap.
void uk_doc_gap(UkDoc *d);
// "Label: value" from a template holding %VALUE%: the part before it gold.
void uk_doc_labeled(UkDoc *d, const char *tmpl, const char *value);
// Lay the words out in `area`, beside a pic_w x pic_h picture at its top left
// (0: none), and draw page `page` with a "1/2" pager at the foot when there are
// more (page < 0: the first page, no pager, and a cut marked). Returns the
// page count (draw false: count only).
int  uk_doc_draw(const UkDoc *d, ML_Rect area, int pic_w, int pic_h, int page, bool draw);

// ---- How many ----------------------------------------------------------------------

// The one count: `heading` gold, `sub` white, `cost` gold (NULL: none), then
// the count's buttons across `a`, from its top. Returns the y under it.
int  uk_count(ML_Rect a, const char *heading, const char *sub, const char *cost, int value, int max);

#endif
