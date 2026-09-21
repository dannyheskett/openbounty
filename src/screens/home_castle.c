// 
//
// Source structure preserved:
//   - home_troops[5] filtered, random_troop = home_troops[rand()%5]
//   - input via throne_room_or_barracks gamestate (A=1, B=2, SYN=3,
//     ESC=0xFF), SYN tick at SOFT_WAIT (150ms)
//   - per-screen `frame` (0..3) advanced on each SYN tick
//   - menu rendering: "Castle %s\n", "\n\n", "A) Recruit...", "B) Audience..."
//   - draw_location(0, random_troop, frame) -- castle backdrop
//
// Sub-flows (recruit, audience) are nested calls; the screen reappears
// when they return. OpenBounty models that with the view stack:
// VIEW_HOME_CASTLE stays on the stack while VIEW_RECRUIT_SOLDIERS or the
// audience modal popup runs on top of it.

#include "home_castle.h"
#include "gfx.h"
#include "layout.h"
#include "overlay.h"
#include "ui.h"
#include "palette.h"
#include "select.h"
#include "touch.h"
#include "bfont.h"
#include "views.h"
#include "player_io.h"   // engine views route through the player-IO queue
#include "tables.h"
#include "resources.h"
#include "raylib.h"
#include "modern/castle.h"
#include "overlay_impl.h"
#include "pending.h"
#include <stdio.h>
#include <string.h>

extern void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);
#define SCREEN_LOC_CASTLE 1   // matches LOC_CASTLE in overlay.c

// random_troop, chosen once at entry (one of the 5 castle-class troops).
// OpenBounty draws it from the deterministic world seed.
static int s_anim_troop_idx = -1;

// Animation frame, advanced on each tick and wrapped.
static int s_frame = 0;

// SYN cadence in seconds. throne_room_or_barracks uses SOFT_WAIT=150ms.
static double s_last_tick = 0.0;
#define HOME_CASTLE_TICK 0.150

static int s_cursor = 0;   // modern: the selected row
int  screen_home_castle_cursor(void) { return s_cursor; }
void screen_home_castle_set_cursor(int r) { s_cursor = (r < 0) ? 0 : (r > 1 ? 1 : r); }

static int pick_castle_troop(const Game *g) {
    int total = troops_count();
    int npool = 0;
    for (int i = 0; i < total; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0) npool++;
    }
    if (npool < 1) return -1;
    unsigned long h = g ? (g->seed ^ 0xC451E11Au) : 0;
    h ^= (unsigned long)g->stats.days_left;
    h = h * 2654435761u + 0x9E3779B9u;
    // The pick-th castle troop in catalog order.
    int pick = (int)(h % (unsigned long)npool);
    for (int i = 0; i < total; i++) {
        const TroopDef *t = troop_by_index(i);
        if (t && strcmp(t->dwelling, "castle") == 0 && pick-- == 0) return i;
    }
    return -1;
}

void screen_home_castle_open(Game *g) {
    if (!g) return;
    s_anim_troop_idx = pick_castle_troop(g);
    s_frame = 0;
    s_last_tick = 0.0;
    s_cursor = 0;
    if (CL_IS_MODERN) modern_castle_open(g, true, pending_castle_id);
    // Enqueue the view; the shell's per-frame sync pushes it (human play) or
    // autoplay acks it (no UI, never accumulates a stack). Context statics above
    // stay as-is.
    player_io_screen(g, VIEW_HOME_CASTLE, /*replace=*/false, NULL, NULL);
}

void screen_home_castle_draw(const Game *g, const Sprites *s) {
    if (CL_IS_MODERN) { modern_overlay_draw_castle(g, s); return; }
    // Source 2285-2286: frame++ on each SYN tick (throne_room_or_barracks
    // SOFT_WAIT cadence, 150ms). We drive it from real time at the same
    // cadence so the backdrop animates regardless of render fps.
    double now = ui_anim_time();
    if (now - s_last_tick >= HOME_CASTLE_TICK) {
        s_last_tick = now;
        s_frame++;
        if (s_frame > 3) s_frame = 0;
    }

    // Source 2309: draw_location(0, random_troop, frame) -- castle
    // backdrop with the chosen troop animated at our owned `s_frame`.
    screens_draw_location_backdrop(g, s, SCREEN_LOC_CASTLE,
                                   s_anim_troop_idx, s_frame);

    // Bottom panel:  verbatim. Source 2295-2304 prints:
    //   "Castle %s\n"           <- title row at text->y - fs->h/4
    //   "\n\n"                  <- then 2 blank lines
    //   "A) Recruit Soldiers      \n"
    //   "B) Audience with the King\n"
    // We use res.banners.body_home_castle so localization stays
    // possible; default fallback matches the verbatim source layout.
    int x, y, w, h;
    screens_text_rect(&x, &y, &w, &h);
    gfx_rect(x, y, w, h, PAL_CLR(DBLUE));
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));

    int pad = screens_text_pad();   // legacy 4: the panel holds exactly CL_PANEL_COLS glyphs
    int row_h = BFONT_GLYPH_H + CL_UI;

    const char *body = g->res->banners.body_home_castle;

    int tx = x + pad;
    int ty = y + pad;
    const char *p = body;
    char line[96];
    if (CL_IS_MODERN) {
        // The body's first line is the castle's name; the choices are rows,
        // with no key letters (A and B still answer as shortcuts).
        if (bfont_take_line(&p, w - 2 * pad, line, (int)sizeof line) > 0)
            bfont_draw(line, tx, ty, PAL_CLR(WHITE));
        ty += 2 * row_h;
        const ResUI *ui = &g->res->ui;
        const char *rows[2] = { ui->home_castle_recruit, ui->home_castle_audience };
        for (int i = 0; i < 2; i++) {
            sel_row(x, ty, w, row_h, tx, rows[i], s_cursor == i,
                    PAL_CLR(WHITE), PAL_CLR(DBLUE), TOUCH_LIST_CASTLE, i);
            ty += row_h;
        }
        return;
    }
    while (*p && ty + row_h <= y + h - pad) {
        int n = 0;
        while (*p && *p != '\n' && n + 1 < (int)sizeof(line)) {
            line[n++] = *p++;
        }
        line[n] = '\0';
        if (*p == '\n') p++;
        bfont_draw(line, tx, ty, PAL_CLR(WHITE));
        ty += row_h;
    }
}
