// src/overlay_impl.h
//
// The overlay's two draw paths. src/overlay.c is a dispatcher: it owns the
// public entry points in overlay.h and the state-free pieces, and sends every
// actual draw to one of these two implementations.
//
//   src/legacy/overlay.c  -- FROZEN. The DOS original's overlay, pixel for
//                            pixel. Its behaviour is the spec; do not change
//                            it to serve a modern need.
//   src/modern/overlay.c  -- where modern UI work happens.
//
// Neither file is called from anywhere but the dispatcher, and nothing outside
// this trio includes this header.

#ifndef OB_OVERLAY_IMPL_H
#define OB_OVERLAY_IMPL_H

#include "game.h"
#include "sprites.h"

// The location-backdrop kind, as the screens/ modules pass it. Values match
// the SCREEN_LOC_* constants declared beside screens_draw_location_backdrop.
//   1 = castle  2 = town  3 = plains  4 = forest  5 = hillcave  6 = dungeon

void legacy_overlay_draw_dialog(void);
void legacy_overlay_draw_dialog_centered(void);
int  legacy_overlay_dialog_page_count(void);
void legacy_overlay_draw_menu(void);
void legacy_overlay_draw_town(const Game *g, const Sprites *s);
void legacy_overlay_draw_options(const Game *g);
void legacy_overlay_draw_controls(const Game *g);
void legacy_overlay_draw_toast(void);
void legacy_overlay_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);

void modern_overlay_draw_dialog(void);
void modern_overlay_draw_dialog_centered(void);
int  modern_overlay_dialog_page_count(void);
void modern_overlay_draw_menu(void);
void modern_overlay_draw_town(const Game *g, const Sprites *s);
void modern_overlay_draw_options(const Game *g);
void modern_overlay_draw_controls(const Game *g);
void modern_overlay_draw_toast(void);
void modern_overlay_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);
// Modern only: no legacy counterpart. The dimmed scene under a detail view,
// prompt or dialog (REQ-430g).
void modern_overlay_dim_scene(void);

#endif
