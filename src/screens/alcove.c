#include "alcove.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "player_io.h"   // engine views route through the player-IO queue
#include "tables.h"
#include "raylib.h"
#include <string.h>

extern void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);
#define SCREEN_LOC_HILLCAVE 5

// Backdrop animation is hard-coded to Gnomes (troop id "gnomes")
// even though the alcove is Aurange's home.
static int s_gnomes_idx = -1;

// frame counter advanced on each SYN tick of the yes/no prompt
// (SHORT_WAIT = 50ms cadence).
static int s_frame = 0;
static double s_last_tick = 0.0;
#define ALCOVE_TICK 0.050

void screen_alcove_open(Game *g) {
    if (!g) return;
    const TroopDef *t = troop_by_id("gnomes");
    s_gnomes_idx = t ? t->index : -1;
    s_frame = 0;
    s_last_tick = 0.0;
    // Enqueue the view; shell sync pushes / autoplay acks. Statics stay.
    player_io_raise_view(g, VIEW_ALCOVE, /*replace=*/false, NULL, NULL);
}

void screen_alcove_draw(const Game *g, const Sprites *s) {
    // Source 2890: draw_location(2 + DWELLING_HILLCAVE, creature, frame)
    // -- frame advances on each yes_no_interactive SYN tick (50ms).
    double now = GetTime();
    if (now - s_last_tick >= ALCOVE_TICK) {
        s_last_tick = now;
        s_frame = (s_frame + 1) & 3;
    }
    screens_draw_location_backdrop(g, s, SCREEN_LOC_HILLCAVE,
                                   s_gnomes_idx, s_frame);

    // The greeting banner is rendered by the yes/no prompt overlay
    // (prompt_yes_no_open with res.banners.alcove_offer body, opened
    // from engine/step.c). The screen itself just owns the backdrop --
    // nothing else needs to live inside the panel since the prompt
    // covers it.
}
