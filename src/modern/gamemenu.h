// src/modern/gamemenu.h
//
// The modern game menu and combat menu (REQ-430s): traditional drill-down
// menus. Each page is one column of standard rows ending in Back (on the Game
// page Back sits above Exit, which is always last); a row that opens a page
// shows ">". The title strip shows the path ("Menu > Game > Save"); the panel
// beside the rows describes the row under the cursor, or says why a greyed row
// does not apply. Legacy never includes this header; its menus are unchanged.

#ifndef OB_MODERN_GAMEMENU_H
#define OB_MODERN_GAMEMENU_H

#include "game.h"
#include "modern/mlist.h"
#include "raylib.h"
#include <stdbool.h>

#define GM_ROWS_MAX 16
#define GM_DEPTH_MAX 4

typedef struct {
    const char *label;
    const char *desc;      // what it does, or why it is greyed
    const char *shortcut;  // key name shown at the right with a keyboard ("" = none)
    int         key;       // the key it presses, or an internal action (GM_ACT_*)
    bool        enabled;
} GmItem;

typedef struct {
    const char *title;
    int         n;
    GmItem      item[GM_ROWS_MAX];
} GmPage;

// Internal actions carried in GmItem.key; real keys are raylib codes, far below.
enum {
    GM_ACT_BACK = 100000,
    GM_ACT_PAGE = 100100,     // + page id: open that page
    GM_ACT_USER = 100200,     // + n: the owner's own actions
};

typedef enum { GM_EV_NONE = 0, GM_EV_ACT, GM_EV_BACK } GmEvent;

// One frame of list input: Up/Down move (wrapping), Enter or a tap on an
// enabled row acts, Escape goes back. The cursor may rest on a greyed row.
GmEvent gm_page_input(const GmPage *p, int *cursor, int touch_list);

// Draw a page in (x, y, w, h): the path in a title strip, the rows in a column
// `list_w` wide (scrolling to the cursor), the description beside them.
// `row_fn` / `row_ctx` may give the rows their own labels (NULL: the items').
void gm_draw_page(const GmPage *p, const char *path, const char *right_title,
                  int x, int y, int w, int h, int list_w, int cursor, int touch_list,
                  MlRowFn row_fn, void *row_ctx);

// ---- the game menu ----------------------------------------------------------------
void modern_gamemenu_open(bool debug);
void modern_gamemenu_update(Game *g);    // input while VIEW_MENU is up
void modern_gamemenu_draw(const Game *g);

// A Yes/No waits: true once, writing the question. On Yes the main loop calls
// modern_gamemenu_confirm_yes.
bool modern_gamemenu_take_confirm(const Game *g, char *body, int cap);
void modern_gamemenu_confirm_yes(void);

typedef enum { GM_DO_NONE = 0, GM_DO_SAVE, GM_DO_LOAD, GM_DO_NEW, GM_DO_EXIT } GmDo;
// What the main loop must carry out now (*slot for Save and Load).
GmDo modern_gamemenu_take_action(int *slot);
// A Debug row's cheat (--debug), or -1.
int  modern_gamemenu_take_cheat(void);

// Page ids, for tests and for opening a page directly.
typedef enum { GM_PAGE_ROOT = 0, GM_PAGE_HERO, GM_PAGE_WORLD, GM_PAGE_GAME,
               GM_PAGE_SAVE, GM_PAGE_LOAD, GM_PAGE_DEBUG } GmPageId;
// The rows of a page as they stand now (for tests).
void modern_gamemenu_page(const Game *g, GmPageId id, GmPage *out);

#endif
