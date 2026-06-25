#include "own_castle.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "player_io.h"   // M4: engine views route through the player-IO queue
#include "tables.h"
#include "resources.h"
#include "raylib.h"
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
// into "Remove troops" — the more common operation when revisiting a
// pre-garrisoned castle.
static bool s_garrison_mode = false;

// Animated troop chosen on open. Stays stable for the visit.
static int s_anim_troop_idx = -1;

static int pick_castle_troop(const Game *g) {
    int total = troops_count();
    int pool[8];
    int npool = 0;
    for (int i = 0; i < total && npool < 8; i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t) continue;
        if (strcmp(t->dwelling, "castle") == 0) pool[npool++] = i;
    }
    if (npool < 1) return -1;
    unsigned long h = g ? (g->seed ^ 0x0CA571E5u) : 0;
    if (s_castle_id[0]) {
        for (const char *p = s_castle_id; *p; p++) {
            h = h * 131u + (unsigned char)*p;
        }
    }
    return pool[h % (unsigned long)npool];
}

void screen_own_castle_open(Game *g, const char *castle_id) {
    if (!g || !castle_id) return;
    size_t n = 0;
    while (n + 1 < sizeof(s_castle_id) && castle_id[n]) {
        s_castle_id[n] = castle_id[n]; n++;
    }
    s_castle_id[n] = '\0';
    s_garrison_mode = false;   // starts in REMOVE mode.
    s_anim_troop_idx = pick_castle_troop(g);
    s_frame = 0;
    s_last_tick = 0.0;
    // M4: enqueue the view (carry the castle id in the request payload too); the
    // shell sync pushes it / autoplay acks it. Context statics above stay as-is.
    PlayerRequest *r = player_io_raise_view(g, VIEW_OWN_CASTLE, /*replace=*/false,
                                            NULL, NULL);
    if (r) snprintf(r->castle_id, sizeof r->castle_id, "%s", castle_id);
}

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
    // 1) Castle backdrop. Advance frame at  SYN cadence.
    double now = GetTime();
    if (now - s_last_tick >= OWN_CASTLE_TICK) {
        s_last_tick = now;
        s_frame = (s_frame + 1) & 3;
    }
    screens_draw_location_backdrop(g, s, SCREEN_LOC_CASTLE,
                                   s_anim_troop_idx, s_frame);

    // 2) Bottom panel.
    int x = CL_PANEL_X;
    int y = CL_PANEL_Y;
    int w = CL_PANEL_W;
    int h = CL_PANEL_H;
    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    DrawRectangleLines(x, y, w, h, PAL_CLR(YELLOW));

    int pad = 4;
    int row_h = BFONT_GLYPH_H + 1;
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

    // Mode label. : SPACE toggles between GARRISON (player →
    // castle) and REMOVE (castle → player). We surface the active
    // mode as a one-line subtitle so the player knows which list the
    // 5 rows below represent.
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    const char *mode_label = s_garrison_mode
        ? (ui ? ui->own_castle_mode_garrison : "Garrison troops (Space=Remove)")
        : (ui ? ui->own_castle_mode_remove   : "Remove troops (Space=Garrison)");
    bfont_draw(mode_label, tx, ty, PAL_CLR(WHITE));
    ty += row_h + 1;   // small gap

    // 5 rows. In GARRISON mode list the player's army (move into
    // castle); in REMOVE mode list the castle's garrison (move into
    // army). Empty slots show "—".
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
        if (id) {
            const TroopDef *t = troop_by_id(id);
            const char *name = (t && t->name[0]) ? t->name : id;
            snprintf(line, sizeof(line), "%c) %-11s%d", 'A' + i, name, count);
        } else {
            snprintf(line, sizeof(line), "%c) %-11s—", 'A' + i, "(empty)");
        }
        bfont_draw(line, tx, ty, PAL_CLR(WHITE));
        ty += row_h;
    }
}
