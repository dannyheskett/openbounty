// VIEW_DWELLING — outdoor dwelling recruit.
//
//   Location backdrop selected by dwelling kind (plains / forest /
//   hill-cave / dungeon) replaces the map; bottom panel shows the
//   recruit banner ("<DwellingName> ----- <pop> <Troops> are
//   available / Cost=<N> each.  GP=<gold>K / You may recruit up to
//   <max> / Recruit how many"). On top of the panel a numeric text
//   input takes the count.
//
// Caller: main.c, on opening a dwelling tile. See screen_dwelling_open.
//
// Note: this screen does NOT own the recruit prompt — main.c continues
// to drive prompt_text_input_open with the existing FLOW_RECRUIT
// pipeline. The screen just owns the backdrop and the static info
// panel; the prompt overlays both.

#ifndef SCREENS_DWELLING_H
#define SCREENS_DWELLING_H

#include "../game.h"
#include "../sprites.h"

// Dwelling kind enum mirrors what the screen wants to render.
typedef enum {
    DWELLING_KIND_PLAINS = 0,
    DWELLING_KIND_FOREST,
    DWELLING_KIND_HILL,
    DWELLING_KIND_DUNGEON,
} DwellingKind;

// Open the screen. Caches the dwelling kind, troop id (for the
// animated troop sprite), and the static banner contents (pop / cost
// / gold / cap), so the panel renders without needing to re-fetch
// game state per frame.
void screen_dwelling_open(Game *g,
                          DwellingKind kind,
                          const char *troop_id,
                          int dwelling_pop,
                          int recruit_cost,
                          int gold,
                          int cap);

void screen_dwelling_draw(const Game *g, const Sprites *s);

// Refresh the cached pop/cost/gold/cap for the panel (called after a
// successful purchase so the panel updates).
void screen_dwelling_refresh(int dwelling_pop, int gold, int cap);

#endif
