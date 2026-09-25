// src/modern/page.h
//
// The page engine (modern only): the one place a modern panel is placed.
//
// The base screen is the frame, the two columns and the map. Everything else
// is a page drawn over it, and every page is one of three kinds, each with
// one size:
//
//   a FULL page    the space inside the smallest screen's frame (a place, a
//                  sheet, the world map, the spells). It floats when the space
//                  inside the frame holds it with its ring and a gap on every
//                  side, and fills that space otherwise: every full page does
//                  the same on the same screen.
//   a MENU page    a full page less a ring and a gap on every side (a menu, a
//                  list, a count, a question with more than two answers). It
//                  floats on every screen.
//   a MESSAGE      the smallest map's width less a ring and a gap on each
//                  side, on the foot of the map (of the battlefield in a
//                  fight), as tall as its words and answers. It floats on
//                  every screen.
//
// A floating page stands in a ring as thick as the frame, and the screen
// behind it is dimmed once. A full page that fills the space has the frame
// for its ring. The sizes come from the pack's declared screen and frame.
//
// Taps: a page is modal -- nothing registered under it takes a tap. A page
// with one action does it on a tap anywhere; on a page with choices a tap on
// nothing inside it does nothing, and a tap outside it -- on the dimmed screen,
// or on the frame round a page that fills -- is its exit. Escape is always the
// exit, Enter the highlighted row.
//
// Legacy never includes this header.

#ifndef OB_MODERN_PAGE_H
#define OB_MODERN_PAGE_H

#include "modern/mlayout.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include "modern/gamemenu.h"
#include "ob_types.h"
#include <stdbool.h>

// ---- the sizes ---------------------------------------------------------------------

int page_ring(void);        // a floating page's ring, and the gap outside it: the frame's thickness
int page_full_w(void);      // a full page: the space inside the smallest screen's frame
int page_full_h(void);
int page_menu_w(void);      // a menu page: a full page less a ring and a gap on every side
// A menu page's height: that, or taller where the screen has room -- as tall
// as PAGE_MENU_ROWS rows and the exit need at the device's row height (the
// World page's seven) -- and never too tall to float. The same for every
// menu page on a screen.
#define PAGE_MENU_ROWS 7
int page_menu_h(void);
int page_msg_w(void);       // a message: the smallest map's width less a ring and a gap each side

typedef enum {
    PAGE_CENTER = 0,   // centred inside the frame
    PAGE_MAP_FOOT,     // on the foot of the map
    PAGE_FIELD_FOOT,   // on the foot of the battlefield
} PageAnchor;

typedef struct {
    ML_Rect r;         // the content
    ML_Rect outer;     // what the page covers: the content and its ring, or,
                       // when it fills, everything inside the frame
    bool    floats;
    PageAnchor anchor;
    int     tap_key, exit_key;   // its taps
} Page;

// ---- the engine ------------------------------------------------------------------

// Every frame starts with nothing open (present_begin calls this).
void    page_frame_begin(void);
// This frame has no frame: the title and class art are full-bleed, and a page
// over them measures from the screen's edges.
void    page_bare(void);
bool    page_is_bare(void);
// The space inside the frame (the whole screen, bare).
ML_Rect page_interior(void);
// A full page floats on this screen. Pure over the layout.
bool    page_full_floats(void);
// The pages open this frame, the lowest first (at most `cap`); for --gallery's
// measurements. Returns how many.
int     page_stack(Page *out, int cap);

// Combat: the battlefield, for PAGE_FIELD_FOOT (page_combat sets it).
bool    page_field(ML_Rect *out);

// Every page's title strip is uk_title (src/modern/uikit.h).

// ---- messages and questions --------------------------------------------------------

// A message: its title in gold, its words, and Continue along its foot (`row`
// NULL: no answer -- the bridge waits for a square and lets the map take
// taps round it). The words are paged, PAGE_MSG_LINES to a page; `page` is
// the one shown. `face` (id 0: none) stands at the left at 1x.
#define PAGE_MSG_LINES 6
int     page_message_text_w(bool face);
int     page_message_pages(const char *body, bool face);
void    page_message(const char *title, const char *body, int page, const char *row,
                     Texture2D face, PageAnchor anchor);

// A question with two answers (Yes and No): its title, its words and the two
// answers along its foot. `face` as for a message.
void    page_question(const char *title, const char *words, int cursor, MlRowFn fn, void *ctx,
                      int touch_list, Texture2D face, PageAnchor anchor);

// ---- places ----------------------------------------------------------------------

// A place is two pages, and each step of it is one or the other by what it
// shows. Both have the title strip, the rows in a column at the left and the
// words beside them; one row is a single action (a tap anywhere).
//
//   the ROOM    the place itself: the backdrop across the top -- the band a
//               figure at 2x stands in, two tiles tall, top-trimmed on a
//               touch device so three rows and the exit fit under it -- with
//               its keeper standing in it, over the rows and the words. Its
//               front, a roll of troops to choose from, and How many.
//   a PERSON    someone speaking: their face at 2x beside what they say, and
//               the rows the whole height of the page. A town's services and
//               each service, a question, and every outcome.
typedef struct { UkScene band; ML_Rect rows; ML_Rect words; } PagePlace;
PagePlace page_place(const char *title, const char *right, Texture2D backdrop, int n_rows);
PagePlace page_person(const char *title, const char *right, int n_rows);

// The foe on the plains: the same title strip, the plains as a band its
// troops stand whole in at 1x, a card per troop under it (intro_y/intro_h)
// and Fight and Evade on the foot. A card holds the troop's face at 1x and
// PAGE_FOE_CARD_LINES lines: how many, the name (two), hit points, damage.
#define PAGE_FOE_CARD_LINES 5
UkScene page_foe(const char *title, const char *right, Texture2D backdrop);

// ---- menus -------------------------------------------------------------------------

// A menu page: the path as its title, the row under the cursor described
// under it, then the rows; the last `p->foot` rows stand on its foot. Rows
// that do not fit scroll.
void    page_menu(const GmPage *p, const char *path, const char *right, int cursor, int touch_list,
                  MlRowFn row_fn, void *row_ctx);
// A menu page with a title strip and a body of its own (Controls, the gate,
// a count, the name): the body under the strip is returned. `tap_key`
// non-zero: its single action; zero: it has choices and Escape is its exit.
ML_Rect page_menu_body(const char *title, const char *right, int tap_key);
// The title menu: its rows over the lower half of the title art. The one page
// with nothing under it to go back to, so a tap outside it does nothing.
void    page_title_menu(const char *const *labels, int n, int cursor, int touch_list);

// ---- pages to read --------------------------------------------------------------

// A full page with choices and a body of its own (the world map, the
// spells): the title strip, and the body under it returned.
ML_Rect page_full_body(const char *title, const char *right);
// A full page to read: the title strip with Close, and a tap anywhere closes
// it (`tap_key`: the key a tap presses -- Escape, or Enter where its one row
// is Continue). Returns the space under the strip.
ML_Rect page_sheet(const char *title, const char *right, int tap_key);
// A full page to read whose picture is as tall as the page (the puzzle's
// grid, `pic_w` wide, at its left): its strip, with Close, spans the words
// beside the picture. Returns the words' space; *picture the picture's.
ML_Rect page_sheet_beside(int pic_w, const char *title, const char *right, ML_Rect *picture);
// The same at a menu page's size (the credits).
ML_Rect page_sheet_small(const char *title, const char *right);

// A caption on full-bleed art (the class picker's): a message's width on the
// screen's foot, its title strip carrying Back (`back`: the key it presses).
// The art is what it is about, so nothing behind it is dimmed. `tap_key`: its
// single action.
Page    page_caption(int h, const char *title, const char *back, int tap_key);

// ---- full-bleed art ---------------------------------------------------------------

// Art that is the whole screen before the game (the logo, the title, the
// class painting): `t` at the largest whole multiple the screen holds,
// centred on the black the screen has been cleared to. Returns where it went
// and *scale its multiple.
ML_Rect page_art(Texture2D t, int *scale);
// The lattice round a rect of full-bleed art already drawn (the cartoon).
void    page_art_margins(ML_Rect art);

// A status the program reports while it works (autoplay, encoding): a
// message with a bar under its words and no answer. Returns the bar's rect.
ML_Rect page_status(const char *title, const char *words);

// ---- the battle -----------------------------------------------------------------

// The battle has one column, two tiles wide, on the right: whose turn it is
// and the commands. The field has the rest of the interior -- flush with its
// top, centred across it where there is room for a band and ground either
// side, flush with the left frame otherwise -- with the castle's back wall
// above it where the map has a tile's more height. Draws the ground and the
// bands; the column and the field are the caller's.
typedef struct { ML_Rect column, field, wall; bool has_wall; } PageCombat;
PageCombat page_combat(bool siege);

// One line on the top edge of the map (of the battlefield, in a fight). Not a
// page: it dims nothing and takes no taps. Drawn last.
void    page_toast(const char *msg);

#endif
