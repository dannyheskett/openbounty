#include "alcove.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "modern/location.h"
#include "player_io.h"   // engine views route through the player-IO queue
#include "tables.h"
#include "raylib.h"
#include "ui.h"
#include <string.h>

extern void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);
#define SCREEN_LOC_ALCOVE 7

// The figure on the backdrop is the pack's `sprites.ui.alcove_figure` when it
// declares one. Without it the screen falls back to animating a troop, which
// is all it could do before: the original hard-coded Gnomes here even though
// the alcove is the archmage's home, and a pack that names no figure of its
// own keeps that behaviour exactly.
static int s_fallback_troop_idx = -1;

// frame counter advanced on each SYN tick of the yes/no prompt
// (SHORT_WAIT = 50ms cadence).
static int s_frame = 0;
static double s_last_tick = 0.0;
#define ALCOVE_TICK 0.050

void screen_alcove_open(Game *g) {
    if (!g) return;
    loc_deal_clear();   // modern: a fresh visit shows no old deal
    const TroopDef *t = troop_by_id("gnomes");
    s_fallback_troop_idx = t ? t->index : -1;
    s_frame = 0;
    s_last_tick = 0.0;
    // Enqueue the view; shell sync pushes / autoplay acks. Statics stay.
    player_io_screen(g, VIEW_ALCOVE, /*replace=*/false, NULL, NULL);
}

void screen_alcove_draw(const Game *g, const Sprites *s) {
    // Source 2890: draw_location(2 + DWELLING_HILLCAVE, creature, frame)
    // -- frame advances on each yes_no_interactive SYN tick (50ms).
    double now = ui_anim_time();
    if (now - s_last_tick >= ALCOVE_TICK) {
        s_last_tick = now;
        s_frame = ob_anim_tick(s_frame);
    }
    // A declared figure may set its own pace. The screen's tick is 50 ms --
    // right for the original's jittering gnome, far too fast for a figure
    // that performs a gesture, which at that rate would loop several times a
    // second. Held frames come from the clock, so the pace is real time
    // whatever the render rate.
    int frame = s_frame;
    const Resources *r = (g && g->res) ? g->res : NULL;
    if (r && s && s->alcove_figure.id && r->sprites.alcove_figure_frame_ms > 0)
        frame = (int)(ui_anim_time() * 1000.0 / r->sprites.alcove_figure_frame_ms);
    screens_draw_location_backdrop(g, s, SCREEN_LOC_ALCOVE,
                                   s_fallback_troop_idx, frame);

    // The greeting banner is rendered by the yes/no prompt overlay
    // (prompt_yes_no_open with res.banners.alcove_offer body, opened
    // from engine/step.c). The screen itself just owns the backdrop --
    // nothing else needs to live inside the panel since the prompt
    // covers it.
}
