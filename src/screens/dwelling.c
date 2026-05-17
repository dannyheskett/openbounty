#include "dwelling.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "tables.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>

extern void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);
// LocKind values mirroring overlay.c's enum.
#define SCREEN_LOC_PLAINS   3
#define SCREEN_LOC_FOREST   4
#define SCREEN_LOC_HILLCAVE 5
#define SCREEN_LOC_DUNGEON  6

//  does NOT animate the troop —
// line 2967 hardcodes `draw_location(2 + rtype, troop_id, 0)` with
// frame=0. So no SYN-driven frame state here; we always pass 0.
#define DWELLING_FRAME 0

static DwellingKind s_kind = DWELLING_KIND_PLAINS;
static int  s_troop_idx    = -1;          // catalog index of the dwelling's troop
static char s_troop_name[32] = { 0 };     // cached display name
static int  s_pop          = 0;
static int  s_cost         = 0;
static int  s_gold         = 0;
static int  s_cap          = 0;

void screen_dwelling_open(Game *g,
                          DwellingKind kind,
                          const char *troop_id,
                          int dwelling_pop,
                          int recruit_cost,
                          int gold,
                          int cap) {
    if (!g) return;
    s_kind = kind;
    const TroopDef *t = (troop_id && troop_id[0]) ? troop_by_id(troop_id) : NULL;
    s_troop_idx = t ? t->index : -1;
    if (t && t->name[0]) {
        size_t n = 0;
        while (n + 1 < sizeof(s_troop_name) && t->name[n]) {
            s_troop_name[n] = t->name[n]; n++;
        }
        s_troop_name[n] = '\0';
    } else {
        s_troop_name[0] = '\0';
    }
    s_pop  = dwelling_pop;
    s_cost = recruit_cost;
    s_gold = gold;
    s_cap  = cap;
    if (views_active() != VIEW_DWELLING) {
        views_push(VIEW_DWELLING);
    }
}

void screen_dwelling_refresh(int dwelling_pop, int gold, int cap) {
    s_pop  = dwelling_pop;
    s_gold = gold;
    s_cap  = cap;
}

static int loc_kind_for(DwellingKind k) {
    switch (k) {
        case DWELLING_KIND_PLAINS:  return SCREEN_LOC_PLAINS;
        case DWELLING_KIND_FOREST:  return SCREEN_LOC_FOREST;
        case DWELLING_KIND_HILL:    return SCREEN_LOC_HILLCAVE;
        case DWELLING_KIND_DUNGEON: return SCREEN_LOC_DUNGEON;
    }
    return SCREEN_LOC_PLAINS;
}

static const char *dwelling_kind_name(const Game *g, DwellingKind k) {
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    switch (k) {
        case DWELLING_KIND_PLAINS:  return ui ? ui->dwelling_kind_plains  : "Plains";
        case DWELLING_KIND_FOREST:  return ui ? ui->dwelling_kind_forest  : "Forest";
        case DWELLING_KIND_HILL:    return ui ? ui->dwelling_kind_hill    : "Hill";
        case DWELLING_KIND_DUNGEON: return ui ? ui->dwelling_kind_dungeon : "Dungeon";
    }
    return "Dwelling";
}

void screen_dwelling_draw(const Game *g, const Sprites *s) {
    // Source 2967: draw_location(2 + rtype, troop_id, 0) — frame is
    // always 0 (no animation in dwelling visits per the source).
    screens_draw_location_backdrop(g, s, loc_kind_for(s_kind),
                                   s_troop_idx, DWELLING_FRAME);

    // Bottom panel — verbatim  banner.
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

    // Title (centered) + ruler line. :
    //   "           <DwellingName>"
    //   "           --------------"
    const char *title = dwelling_kind_name(g, s_kind);
    bfont_draw_centered(title, x + w / 2, ty, PAL_CLR(WHITE));
    ty += row_h;
    {
        char ruler[32];
        size_t n = strlen(title);
        if (n > 28) n = 28;
        for (size_t i = 0; i < n; i++) ruler[i] = '-';
        ruler[n] = '\0';
        bfont_draw_centered(ruler, x + w / 2, ty, PAL_CLR(WHITE));
    }
    ty += row_h;

    // "<pop> <Troops> are available"
    char line[96];
    snprintf(line, sizeof(line), "%d %s are available",
             s_pop, s_troop_name[0] ? s_troop_name : "Troops");
    bfont_draw(line, tx, ty, PAL_CLR(WHITE));
    ty += row_h;

    // "Cost=<N> each.      GP=<gold>K"
    char cost_part[32];
    char gold_part[24];
    snprintf(cost_part, sizeof(cost_part), "Cost=%d each.", s_cost);
    snprintf(gold_part, sizeof(gold_part), "GP=%dK", s_gold / 1000);
    bfont_draw(cost_part, tx, ty, PAL_CLR(WHITE));
    {
        // Right-align the gold part to column 20 (spec aligns the GP=
        // text to the right side of the inner rect).
        int gp_x = tx + 20 * BFONT_GLYPH_W;
        bfont_draw(gold_part, gp_x, ty, PAL_CLR(WHITE));
    }
    ty += row_h;

    // "You may recruit up to <max>"
    snprintf(line, sizeof(line), "You may recruit up to %d", s_cap);
    bfont_draw(line, tx, ty, PAL_CLR(WHITE));
    ty += row_h;

    // The "Recruit how many" prompt and numeric input are owned by
    // main.c (prompt_text_input_open over this panel). The cursor /
    // typed digits render via prompt_draw on top of this rect.
    {
        const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
        bfont_draw(ui ? ui->dwelling_recruit_how_many : "Recruit how many",
                   tx, ty, PAL_CLR(WHITE));
    }
}
