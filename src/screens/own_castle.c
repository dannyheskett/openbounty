#include "own_castle.h"
#include "gfx.h"
#include "layout.h"
#include "overlay.h"
#include "ui.h"
#include "select.h"
#include "touch.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "player_io.h"   // engine views route through the player-IO queue
#include "tables.h"
#include "resources.h"
#include "ob_types.h"
#include "modern/castle.h"
#include "overlay_impl.h"
#include <stdio.h>
#include <string.h>

extern void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);
#define SCREEN_LOC_CASTLE 1

// SYN-driven backdrop frame (own_castle uses five_choices_and_space in
//  which has its own SYN cadence). Match recruit's
// pattern: SOFT_WAIT-ish ~150ms.
static int s_frame = 0;
static double s_last_tick = 0.0;
#define OWN_CASTLE_TICK 0.150

// Castle the screen is bound to. Cleared on dismiss.
static char s_castle_id[24] = { 0 };

// Mode toggle (: SPACE flips between Garrison and Remove).
// initializes to MODE_REMOVE so a fresh visit opens straight
// into "Remove troops" -- the more common operation when revisiting a
// pre-garrisoned castle.
static bool s_garrison_mode = false;

// Animated troop chosen on open. Stays stable for the visit.
static int s_anim_troop_idx = -1;

// Modern: the selected row. Row 0 is the Garrison / Remove mode, rows 1-5 the
// five slots.
static int s_cursor = 1;

static int pick_castle_troop(const Game *g) {
    int total = troops_count();
    int npool = 0;
    for (int i = 0; i < total; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0) npool++;
    }
    if (npool < 1) return -1;
    unsigned long h = g ? (g->seed ^ 0x0CA571E5u) : 0;
    if (s_castle_id[0]) {
        for (const char *p = s_castle_id; *p; p++) {
            h = h * 131u + (unsigned char)*p;
        }
    }
    // The pick-th castle troop in catalog order.
    int pick = (int)(h % (unsigned long)npool);
    for (int i = 0; i < total; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0 && pick-- == 0) return i;
    }
    return -1;
}

void screen_own_castle_open(Game *g, const char *castle_id) {
    if (!g || !castle_id) return;
    size_t n = 0;
    while (n + 1 < sizeof(s_castle_id) && castle_id[n]) {
        s_castle_id[n] = castle_id[n]; n++;
    }
    s_castle_id[n] = '\0';
    s_garrison_mode = false;   // starts in REMOVE mode.
    s_cursor = 1;
    s_anim_troop_idx = pick_castle_troop(g);
    s_frame = 0;
    s_last_tick = 0.0;
    if (CL_IS_MODERN) modern_castle_open(g, false, castle_id);
    // Enqueue the view (carry the castle id in the request payload too); the
    // shell sync pushes it / autoplay acks it. Context statics above stay as-is.
    PlayerRequest *r = player_io_screen(g, VIEW_OWN_CASTLE, /*replace=*/false,
                                            NULL, NULL);
    if (r) snprintf(r->castle_id, sizeof r->castle_id, "%s", castle_id);
}

int  screen_own_castle_cursor(void) { return s_cursor; }
void screen_own_castle_set_cursor(int r) { s_cursor = (r < 0) ? 0 : (r > 5 ? 5 : r); }

bool screen_own_castle_is_garrison_mode(void) {
    return s_garrison_mode;
}

void screen_own_castle_toggle_mode(void) {
    s_garrison_mode = !s_garrison_mode;
}

const char *screen_own_castle_castle_id(void) {
    return s_castle_id;
}

void screen_own_castle_draw(const Game *g, const Sprites *s) {
    if (CL_IS_MODERN) { modern_overlay_draw_castle(g, s); return; }
    // 1) Castle backdrop. Advance frame at  SYN cadence.
    double now = ui_anim_time();
    if (now - s_last_tick >= OWN_CASTLE_TICK) {
        s_last_tick = now;
        s_frame = ob_anim_tick(s_frame);
    }
    screens_draw_location_backdrop(g, s, SCREEN_LOC_CASTLE,
                                   s_anim_troop_idx, s_frame);

    // 2) Bottom panel.
    int x, y, w, h;
    screens_text_rect(&x, &y, &w, &h);
    gfx_rect(x, y, w, h, PAL_CLR(DBLUE));
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));

    int pad = screens_text_pad();   // legacy 4: the panel holds exactly CL_PANEL_COLS glyphs
    int row_h = BFONT_GLYPH_H + CL_UI;
    int tx = x + pad;
    int ty = y + pad;

    // Castle name title. The catalog's display name (if any) is
    // already y-flipped/canonicalized so we just show it.
    const ResCastle *rc = g ? resources_castle_by_id(g->res, s_castle_id) : NULL;
    char title[64];
    snprintf(title, sizeof(title), "Castle %s",
             (rc && rc->name[0]) ? rc->name : s_castle_id);
    bfont_draw(title, tx, ty, PAL_CLR(WHITE));
    ty += row_h;

    // Mode label. : SPACE toggles between GARRISON (player ->
    // castle) and REMOVE (castle -> player). We surface the active
    // mode as a one-line subtitle so the player knows which list the
    // 5 rows below represent.
    const ResUI *ui = &g->res->ui;
    const char *mode_label = s_garrison_mode
        ? ui->own_castle_mode_garrison
        : ui->own_castle_mode_remove;
    if (CL_IS_MODERN) {
        // A row: choosing it flips the mode (Space still does).
        mode_label = s_garrison_mode ? ui->own_castle_row_garrison : ui->own_castle_row_remove;
        sel_row(x, ty, w, row_h, tx, mode_label, s_cursor == 0,
                PAL_CLR(WHITE), PAL_CLR(DBLUE), TOUCH_LIST_CASTLE, 0);
    } else {
        bfont_draw(mode_label, tx, ty, PAL_CLR(WHITE));
    }
    ty += row_h + 1;   // small gap

    // 5 rows. In GARRISON mode list the player's army (move into
    // castle); in REMOVE mode list the castle's garrison (move into
    // army). Empty slots show "--".
    const CastleRecord *cr = g ? GameFindCastleConst(g, s_castle_id) : NULL;
    for (int i = 0; i < 5; i++) {
        char line[64];
        const char *id = NULL;
        int count = 0;
        if (s_garrison_mode) {
            // Player army: g->army[5]
            if (i < GAME_ARMY_SLOTS && g->army[i].id[0] && g->army[i].count > 0) {
                id    = g->army[i].id;
                count = g->army[i].count;
            }
        } else {
            // Castle garrison.
            if (cr && i < GAME_ARMY_SLOTS &&
                cr->garrison[i].id[0] && cr->garrison[i].count > 0) {
                id    = cr->garrison[i].id;
                count = cr->garrison[i].count;
            }
        }
        const TroopDef *t = id ? troop_by_id(id) : NULL;
        const char *name = id ? ((t && t->name[0]) ? t->name : id) : "(empty)";
        if (CL_IS_MODERN) {
            // No key letters in modern: the rows are the choice.
            if (id) snprintf(line, sizeof(line), "%-14s%d", name, count);
            else    snprintf(line, sizeof(line), "%-14s-", name);
            sel_row(x, ty, w, row_h, tx, line, s_cursor == i + 1,
                    PAL_CLR(WHITE), PAL_CLR(DBLUE), TOUCH_LIST_CASTLE, i + 1);
        } else {
            if (id) snprintf(line, sizeof(line), "%c) %-11s%d", 'A' + i, name, count);
            else    snprintf(line, sizeof(line), "%c) %-11s-", 'A' + i, name);
            bfont_draw(line, tx, ty, PAL_CLR(WHITE));
        }
        ty += row_h;
    }
}
