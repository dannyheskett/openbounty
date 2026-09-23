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

// Rail order, top to bottom. Every action is an existing case in
// shell_dispatch_action, so the rail adds no game logic of its own; each row
// does exactly what its key does, legality included -- Cast pops the
// "you know no magic" banner here just as it does from the keyboard.
typedef struct {
    Texture2D   (*art)(const Sprites *s);
    InputAction   action;
} RailRow;

static Texture2D art_menu  (const Sprites *s) { return s->rail_menu;   }
static Texture2D art_map   (const Sprites *s) { return s->rail_map;    }
static Texture2D art_army  (const Sprites *s) { return s->rail_army;   }
static Texture2D art_search(const Sprites *s) { return s->rail_search; }
static Texture2D art_cast  (const Sprites *s) { return s->rail_cast;   }

static const RailRow ROWS[] = {
    { art_menu,   INPUT_ACTION_GAME_MENU    },
    { art_map,    INPUT_ACTION_VIEW_MAP     },
    { art_army,   INPUT_ACTION_VIEW_ARMY    },
    { art_search, INPUT_ACTION_SEARCH       },
    { art_cast,   INPUT_ACTION_CAST_SPELL   },
};
#define RAIL_ROWS ((int)(sizeof ROWS / sizeof ROWS[0]))

// A page of its own is up: it owns the screen and every tap on it. The rail
// still draws (it is part of the frame behind), but registers nothing.
static bool a_page_is_open(void) {
    return views_active() != VIEW_NONE || dialog_is_active() || prompt_is_active();
}

void rail_draw(const Game *g, const Sprites *s) {
    (void)g;
    if (!s || CL_RAIL_W <= 0) return;
    bool page = a_page_is_open();
    int x = CL_RAIL_X, y = CL_RAIL_Y;
    for (int i = 0; i < RAIL_ROWS; i++) {
        if (y + CL_TILE_H > CL_RAIL_Y + CL_RAIL_H) break;   // a short pane
        Texture2D t = ROWS[i].art(s);
        if (t.id) {
            // The HUD panel's two calls: the art filling the tile, then the
            // shell's own frame around it (hud.c, blit_panel).
            Rectangle src = { 0, 0, (float)t.width, (float)t.height };
            Rectangle dst = { (float)x, (float)y, (float)CL_RAIL_W, (float)CL_TILE_H };
            gfx_texture_draw(t, src, dst, WHITE);
        }
        ui_panel_frame(x, y, CL_RAIL_W, CL_TILE_H);
        if (!page) ui_tile_row(x, y, CL_RAIL_W, CL_TILE_H, TOUCH_LIST_RAIL, i);
        y += CL_TILE_H;
    }
}

InputAction rail_tapped(void) {
    if (CL_RAIL_W <= 0) return INPUT_ACTION_NONE;
    int row = touch_tapped_row(TOUCH_LIST_RAIL);
    if (row < 0 || row >= RAIL_ROWS) return INPUT_ACTION_NONE;
    return ROWS[row].action;
}
