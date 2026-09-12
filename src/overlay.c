// src/overlay.c -- the overlay dispatcher.
//
// Whatever modal or overlay state is active gets drawn on top of the map and
// chrome: dialogs, menus, the town menu, the detail views, toasts. WHICH of
// those is drawn is decided here and is the same in both render modes; HOW
// each one looks is not, so every draw goes to one of the two implementations
// behind overlay_impl.h:
//
//   src/legacy/overlay.c  -- the DOS original, frozen.
//   src/modern/overlay.c  -- modern UI work.
//
// This file holds no state and draws nothing itself.

#include "overlay.h"
#include "overlay_impl.h"
#include "layout.h"
#include "views.h"
#include "prompt.h"
#include "ui.h"
#include "views_render.h"
#include "resources.h"
#include "screens/home_castle.h"
#include "screens/recruit_soldiers.h"
#include "screens/own_castle.h"
#include "screens/dwelling.h"
#include "screens/alcove.h"
#include "screens/end_game.h"

// ---------------------------------------------------------------------------
// The public entry points, each one mode's draw.
// ---------------------------------------------------------------------------

void overlay_draw_dialog(void) {
    if (CL_IS_MODERN) modern_overlay_draw_dialog();
    else              legacy_overlay_draw_dialog();
}

void overlay_draw_dialog_centered(void) {
    if (CL_IS_MODERN) modern_overlay_draw_dialog_centered();
    else              legacy_overlay_draw_dialog_centered();
}

int overlay_dialog_page_count(void) {
    return CL_IS_MODERN ? modern_overlay_dialog_page_count()
                        : legacy_overlay_dialog_page_count();
}

// Public bridge for screen modules in src/screens/. Takes an int for loc_kind
// so the LocKind enum can stay private to the two implementations.
// Constants (must match LocKind enum order):
//   1 = LOC_CASTLE  2 = LOC_TOWN     3 = LOC_PLAINS
//   4 = LOC_FOREST  5 = LOC_HILLCAVE 6 = LOC_DUNGEON
//
// `troop_frame` is the 0..3 animation frame the caller owns. The screens
// advance their own frame from SYN ticks.
void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                    int loc_kind, int troop_idx,
                                    int troop_frame) {
    if (CL_IS_MODERN)
        modern_overlay_draw_location_backdrop(g, s, loc_kind, troop_idx, troop_frame);
    else
        legacy_overlay_draw_location_backdrop(g, s, loc_kind, troop_idx, troop_frame);
}

// The alpha byte for a dim percent, clamped to 0..100. Pure, and the same
// arithmetic whoever asks.
int overlay_dim_alpha(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return percent * 255 / 100;
}

// Modern only (REQ-430g): legacy has no dim and never did.
void overlay_dim_scene(void) {
    if (!CL_IS_MODERN) return;
    modern_overlay_dim_scene();
}

// ---------------------------------------------------------------------------
// Top-level dispatcher. The order of the layers is mode-independent.
// ---------------------------------------------------------------------------

static void draw_menu(void) {
    if (CL_IS_MODERN) modern_overlay_draw_menu();
    else              legacy_overlay_draw_menu();
}

static void draw_town(const Game *g, const Sprites *s) {
    if (CL_IS_MODERN) modern_overlay_draw_town(g, s);
    else              legacy_overlay_draw_town(g, s);
}

static void draw_options(const Game *g) {
    if (CL_IS_MODERN) modern_overlay_draw_options(g);
    else              legacy_overlay_draw_options(g);
}

static void draw_controls(const Game *g) {
    if (CL_IS_MODERN) modern_overlay_draw_controls(g);
    else              legacy_overlay_draw_controls(g);
}

static void draw_toast(void) {
    if (CL_IS_MODERN) modern_overlay_draw_toast();
    else              legacy_overlay_draw_toast();
}

void overlay_draw(const Game *g, const Map *m, const Fog *f,
                          const Sprites *s) {
    ViewKind v = views_active();

    // Modern: a detail view, a prompt or a dialog sits on a dimmed scene, so
    // the panel is what the eye lands on. The toast alone does not dim.
    if (v != VIEW_NONE || prompt_is_active() || dialog_is_active())
        overlay_dim_scene();

    if (v == VIEW_OPTIONS) {
        draw_options(g);
    } else if (v == VIEW_CONTROLS) {
        draw_controls(g);
    } else if (v == VIEW_MENU) {
        draw_menu();
    } else if (v == VIEW_TOWN) {
        draw_town(g, s);
    } else if (v == VIEW_HOME_CASTLE) {
        screen_home_castle_draw(g, s);
    } else if (v == VIEW_RECRUIT_SOLDIERS) {
        screen_recruit_soldiers_draw(g, s);
    } else if (v == VIEW_OWN_CASTLE) {
        screen_own_castle_draw(g, s);
    } else if (v == VIEW_DWELLING) {
        screen_dwelling_draw(g, s);
    } else if (v == VIEW_ALCOVE) {
        screen_alcove_draw(g, s);
    } else if (v == VIEW_WIN || v == VIEW_LOSE) {
        screen_end_game_draw(g, s);
    } else if (v == VIEW_ARMY      || v == VIEW_CHARACTER ||
               v == VIEW_CONTRACT  || v == VIEW_PUZZLE    ||
               v == VIEW_WORLDMAP  || v == VIEW_SPELLS) {
        views_render_draw(g, m, f, s);
    }

    // Modal prompt (yes/no, numeric picker): replaces the bottom frame.
    if (prompt_is_active()) {
        prompt_draw();
    }

    // Dialog LAST, so it covers a prompt that is up at the same time. Both are
    // opaque bottom-frame panels, and the dialog is the one holding input while
    // it is open (see the prompt_dispatch_tick gate in main.c, issue #19) -- the
    // visible modal has to be the one the next key talks to. Once the dialog is
    // dismissed the prompt underneath is revealed and answers as usual.
    if (dialog_is_active()) {
        overlay_draw_dialog();
    }

    // Toast always last so it floats above other layers.
    draw_toast();
}
