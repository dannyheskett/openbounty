// src/modern/uikit.h
//
// The modern screens' shared kit: one look for every panel, title strip,
// scene, in-lay and picture, so the art is what the eye lands on and the
// interface is the same everywhere.
//
//   panel     a dark, nearly opaque fill ringed by the lattice -- never a flat
//             blue box
//   title     the strip across a screen's top: its name at the left, what it
//             is about (a zone, the purse) at the right, a lattice band under it
//   scene     a place screen: the backdrop whole at 3x (720x306) framed by the
//             lattice at the sides, a two-line introduction under it, and two
//             full-width rows -- or three, with the backdrop's top trimmed
//   in-lay    a framed panel centred over the dimmed screen: a title, a picture
//             at 2x with the words flowing beside it and then beneath, and its
//             rows along the foot
//
// On Rome's 776x480: scene 36 + 306 + 36 + 2 + 100, or 36 + 255 + 37 + 2 + 150.
// Legacy never includes this header.

#ifndef OB_MODERN_UIKIT_H
#define OB_MODERN_UIKIT_H

#include "modern/mlayout.h"
#include "modern/mlist.h"
#include "raylib.h"
#include <stdbool.h>

struct Game;

#define UK_BAND   4     // the lattice band between parts
#define UK_INSET  12    // text inset inside a panel

Color uk_fill(void);    // a panel's fill
Color uk_ink(void);     // a selected row's text (solid)
int   uk_title_h(void); // the title strip, without its band
int   uk_line_h(void);  // a text line's pitch

// A framed panel: the fill and the lattice ring outside the rect.
void uk_panel(int x, int y, int w, int h);
// The fill alone over the full-screen rect (the chrome frames it).
void uk_sheet(void);
// The title strip at (x, y, w): `left` yellow, `right` at the right edge.
// Returns the y under its band.
int  uk_title(int x, int y, int w, const char *left, const char *right, Color right_c);
// "Gold 12000", for a place screen's title strip (the HUD is hidden there).
void uk_gold_text(const struct Game *g, char *out, int cap);
// Darken everything under an in-lay.
void uk_dim(void);

// An in-lay w x h centred on ml_area(): dims, draws the panel and its title,
// and returns the body under the title band.
ML_Rect uk_inlay(int w, int h, const char *title, const char *right);
// The in-lay sizes: standard, tall, wide.
#define UK_INLAY_W   576
#define UK_INLAY_H   384
#define UK_TALL_H    444
#define UK_WIDE_W    720

// A picture in a black square with a thin gold edge.
void uk_picture(Texture2D t, int x, int y, int w, int h);

// Words wrapped from (x, y) across w; while a line's top is above `pic_b` it
// starts at `pic_r` instead (beside a picture), then runs the full width.
// Stops before `max_y`. Returns the y under the last line.
int  uk_flow(int x, int y, int w, int pic_r, int pic_b, int max_y, const char *text, Color fg);
// Lines `text` wraps to at width w.
int  uk_lines(const char *text, int w);

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

// Draw the frame, title and backdrop for a scene with `rows` answers (2 or 3).
UkScene uk_scene(const char *title, const char *right, Texture2D backdrop, int rows);
// As uk_scene with a band of `band_h` between the backdrop and the rows (the
// foe's cards), the backdrop's top trimmed to make room.
UkScene uk_scene_ex(const char *title, const char *right, Texture2D backdrop, int rows, int band_h);
// As uk_scene, with room for the whole introduction (up to three lines).
UkScene uk_scene_for(const char *title, const char *right, Texture2D backdrop, int rows, const char *intro);
// Blit art placed in the backdrop's own 240x102 units.
void    uk_scene_blit(const UkScene *L, Texture2D t, int bx, int by, int bw, int bh);
// A figure standing on the backdrop's bottom edge, `x` screen pixels in, at 2x.
void    uk_scene_figure(const UkScene *L, Texture2D t, int x);
void    uk_scene_intro(const UkScene *L, const char *text);
void    uk_scene_rows(const UkScene *L, int n, int cursor, MlRowFn fn, void *ctx, int touch_list);


// A row source over a fixed list of labels.
typedef struct { const char *label[8]; bool enabled[8]; } UkRows;
bool uk_rows_fn(void *ctx, int i, char *label, char *right, int cap);

// The How-many in-lay: the troop's picture at 2x, `lines` beside it, the count
// buttons, and "Recruit 20" / Cancel as rows (tapped as rows 0 and 1 of
// touch_list).
void uk_count_inlay(const char *title, Texture2D face, const char *lines[], Color colors[], int nlines,
                    int value, int max, const char *act_label, const char *cancel_label, int touch_list);

// A confirmation in-lay: title, picture at 2x (none: the words take the width),
// the words, and one row (Continue).
void uk_result_inlay(const char *title, Texture2D face, const char *text, const char *row_label,
                     int touch_list);


// ---- paged words beside a picture ------------------------------------------------

// Paragraphs of words for an in-lay's body: each may start with a gap, and its
// first `label` bytes may be yellow before a value.
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
// "Label: value" from a template holding %VALUE%: the part before it yellow.
void uk_doc_labeled(UkDoc *d, const char *tmpl, const char *value);
// Lay the words out in `area`, beside a pic_w x pic_h picture at its top left
// (0: none), and draw page `page` with a "1/2" pager at the foot when there are
// more (page < 0: the first page and no pager). Returns the page count (draw
// false: count only).
int  uk_doc_draw(const UkDoc *d, ML_Rect area, int pic_w, int pic_h, int page, bool draw);
// The height the words take in one page at area's width (beside the picture).
int  uk_doc_height(const UkDoc *d, ML_Rect area, int pic_w, int pic_h);

// ---- the card: the one panel model -------------------------------------------------
//
// A framed panel sized to what it holds: an optional title strip; an optional
// picture, always at 2x, with the words in one column beside it; optionally a
// block the full width of the card under them (the count row); the answers as
// full-width rows stacked along the foot. The narrowest column that fits
// beside the picture is chosen, so short words make a narrow card and long
// words a wide one.

#define UK_CARD_ANSWERS 4
typedef struct {
    const char  *title, *right;               // title strip (NULL: none)
    Texture2D    face;                        // 2x picture (id 0: none)
    const UkDoc *doc;                         // the words (may be NULL)
    const char  *answers[UK_CARD_ANSWERS];    // fixed answers, as rows
    bool         disabled[UK_CARD_ANSWERS];
    int          n_answers, cursor, touch_list;
    int          touch_base;                  // the first answer's row in touch_list
    int          extra_h;                     // full-width block under the body (0: none)
    int          min_w;                       // the card at least this wide
    bool         at_foot;                     // on the area's foot (the map), else centred
    bool         no_dim;
} UkCard;

typedef struct { ML_Rect card; ML_Rect extra; } UkCardOut;

// Lay out and draw the card; out (may be NULL) gives the extra block's rect.
void uk_card(const UkCard *c, UkCardOut *out);

#endif
