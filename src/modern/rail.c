// src/modern/rail.c -- the left rail (see rail.h).

#include "modern/rail.h"
#include "gfx.h"
#include "layout.h"
#include "sprites.h"
#include "touch.h"
#include "uitouch.h"
#include "ui.h"
#include "views.h"
#include "overlay.h"
#include "prompt_impl.h"
#include "hud.h"
#include "modern/page.h"
#include "resources.h"

// Rail order, top to bottom. Every action is an existing case in
// shell_dispatch_action, so the rail adds no game logic of its own; each row
// does exactly what its key does, legality included.
typedef struct {
    Texture2D   (*art)(const Sprites *s);
    InputAction   action;
    const char   *key;      // the key the row's screen answers to (src/input.c);
                            // NULL: the pack's name for Escape
} RailRow;

static Texture2D art_menu  (const Sprites *s) { return s->rail_menu;   }
static Texture2D art_map   (const Sprites *s) { return s->rail_map;    }
static Texture2D art_army  (const Sprites *s) { return s->rail_army;   }
static Texture2D art_search(const Sprites *s) { return s->rail_search; }

static const RailRow ROWS[] = {
    { art_menu,   INPUT_ACTION_GAME_MENU,   NULL },
    { art_map,    INPUT_ACTION_VIEW_MAP,    "M"  },
    { art_army,   INPUT_ACTION_VIEW_ARMY,   "A"  },
    { art_search, INPUT_ACTION_SEARCH,      "S"  },
    { NULL,       INPUT_ACTION_VIEW_PUZZLE, "P"  },   // the puzzle, drawn by the HUD's code
};
#define RAIL_ROWS ((int)(sizeof ROWS / sizeof ROWS[0]))

// A page of its own is up: it owns the screen, every key and every tap. The
// rail still draws (it is part of the base screen behind), but shows no keys
// and registers nothing.
static bool a_page_is_open(void) {
    return views_active() != VIEW_NONE || dialog_is_active() || prompt_is_active();
}

void rail_draw(const Game *g, const Sprites *s) {
    if (!s || CL_RAIL_W <= 0) return;
    bool page = a_page_is_open();
    const Resources *res = resources_current();
    int x = CL_RAIL_X, y = CL_RAIL_Y;
    for (int i = 0; i < RAIL_ROWS; i++) {
        int ty = y + i * CL_TILE_H;
        if (ROWS[i].art) {
            Texture2D t = ROWS[i].art(s);
            if (t.id) {
                Rectangle src = { 0, 0, (float)t.width, (float)t.height };
                Rectangle dst = { (float)x, (float)ty, (float)CL_RAIL_W, (float)CL_TILE_H };
                gfx_texture_draw(t, src, dst, WHITE);
            }
        } else {
            hud_draw_puzzle_tile(g, s, x, ty, false);
        }
    }
    hud_column_finish(x, y, CL_RAIL_W, CL_RAIL_H, RAIL_ROWS);
    for (int i = 0; i < RAIL_ROWS && !page; i++) {
        int ty = y + i * CL_TILE_H;
        hud_key_hint(x, ty, ROWS[i].key ? ROWS[i].key : (res ? res->ui.key_esc : "Esc"));
        ui_tile_row(x, ty, CL_RAIL_W, CL_TILE_H, TOUCH_LIST_RAIL, i);
    }
}

InputAction rail_tapped(void) {
    if (CL_RAIL_W <= 0) return INPUT_ACTION_NONE;
    int row = touch_tapped_row(TOUCH_LIST_RAIL);
    if (row < 0 || row >= RAIL_ROWS) return INPUT_ACTION_NONE;
    return ROWS[row].action;
}
