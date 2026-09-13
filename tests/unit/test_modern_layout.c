// The modern screen and its five named layouts (src/layout.c,
// src/modern/mlayout.c, REQ-430j), for a pack shaped like Rome: 96 px tiles,
// a 7x5 viewport, a fixed 800x532 buffer.
//
// Vertical positions are asserted against the map pane rather than as bare
// numbers, because the pane's top edge follows the status band and the status
// band follows the font -- which the test binary does not load.

#include "greatest.h"
#include "layout.h"
#include "resources.h"
#include "bfont.h"
#include "modern/mlayout.h"
#include "views.h"
#include <string.h>

static Resources s_res;

static void rome(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_MODERN;
    s_res.render.tile_w = 96;  s_res.render.tile_h = 96;
    s_res.render.tiles_w = 7;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 1;
    s_res.render.native_w = 800; s_res.render.native_h = 532;
    layout_init((const struct Resources *)&s_res);
}

static int inside(ML_Rect in, ML_Rect out) {
    return in.x >= out.x && in.y >= out.y &&
           in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

static ML_Rect pane(void) {
    ML_Rect r = { CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H };
    return r;
}

// The screen's horizontal spacing is split 3 : 2 : 3: left edge, the band
// between the map pane and the HUD, right edge. 800 = 12 + 672 + 8 + 96 + 12.
TEST spacing_is_three_two_three(void) {
    rome();
    ASSERT_EQ(800, CL_SCREEN_W);
    ASSERT_EQ(12, CL_FRAME_LEFT_W);
    ASSERT_EQ(8,  CL_SIDEBAR_GAP);
    ASSERT_EQ(12, CL_FRAME_RIGHT_W);
    ASSERT_EQ(12, CL_MAP_X);
    ASSERT_EQ(672, CL_MAP_W);
    ASSERT_EQ(692, CL_SIDEBAR_X);
    ASSERT_EQ(96, CL_SIDEBAR_W);
    ASSERT_EQ(CL_SCREEN_W - CL_FRAME_RIGHT_W, CL_SIDEBAR_X + CL_SIDEBAR_W);
    PASS();
}

// The vertical stack mirrors the horizontal one: top edge, status band,
// band, map pane, bottom edge -- the outer edges as thick as the side edges
// and the band under the status line as wide as the band beside the HUD.
// 532 = 12 + 20 + 8 + 480 + 12.
TEST vertical_mirrors_horizontal(void) {
    rome();
    ASSERT_EQ(532, CL_SCREEN_H);
    ASSERT_EQ(CL_FRAME_LEFT_W, CL_FRAME_TOP_H);
    ASSERT_EQ(CL_FRAME_RIGHT_W, CL_FRAME_BOTTOM_H);
    ASSERT_EQ(12, CL_FRAME_TOP_H);
    ASSERT_EQ(20, CL_STATUS_H);
    ASSERT_EQ(CL_SIDEBAR_GAP, CL_BAR_H);
    ASSERT_EQ(8, CL_BAR_H);
    ASSERT_EQ(12, CL_STATUS_Y);
    ASSERT_EQ(32, CL_BAR_Y);
    ASSERT_EQ(40, CL_MAP_Y);
    ASSERT_EQ(CL_SCREEN_H - CL_FRAME_BOTTOM_H, CL_MAP_Y + CL_MAP_H);
    PASS();
}

// A buffer too short to mirror keeps the old frames rather than squeezing the
// status band below a text line.
TEST short_buffer_does_not_mirror(void) {
    rome();
    s_res.render.native_h = 510;
    layout_init((const struct Resources *)&s_res);
    ASSERT_EQ(510, CL_SCREEN_H);
    ASSERT(CL_FRAME_TOP_H < CL_FRAME_LEFT_W);
    ASSERT_EQ(CL_SCREEN_H, CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H + CL_MAP_H + CL_FRAME_BOTTOM_H);
    PASS();
}

// Small: six text lines tall, inset from the pane by the screen's spacing.
TEST small_is_inset_by_the_spacing_on_the_bottom(void) {
    rome();
    int S = ml_space();
    ASSERT_EQ(8, S);
    ML_Rect r = ml_small();
    ASSERT_EQ(CL_MAP_X + S, r.x);
    ASSERT_EQ(CL_MAP_W - 2 * S, r.w);
    ASSERT_EQ(ML_SMALL_LINES * BFONT_GLYPH_H + 2 * ML_PAD, r.h);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H - S, r.y + r.h);
    ASSERT(inside(r, pane()));
    PASS();
}

// Large: six by four tiles, centred in the pane.
TEST large_is_six_by_four_tiles_centred(void) {
    rome();
    ML_Rect r = ml_large();
    ASSERT_EQ(576, r.w);
    ASSERT_EQ(384, r.h);
    ASSERT_EQ(CL_MAP_X + 48, r.x);
    ASSERT_EQ(CL_MAP_Y + 48, r.y);
    ASSERT(inside(r, pane()));
    PASS();
}

// Location: the backdrop inset by the spacing at the smallest integer scale
// that covers its width, cropped to it; the text area shares its bottom edge
// and runs to the spacing above the pane's bottom.
TEST location_backdrop_is_integer_3x_inset_and_text_fills_below(void) {
    rome();
    int S = ml_space();
    ASSERT_EQ(3, ml_loc_scale());
    ML_Rect b = ml_loc_backdrop();
    ML_Rect t = ml_loc_text();
    ASSERT_EQ(CL_MAP_X + S, b.x);
    ASSERT_EQ(CL_MAP_Y + S, b.y);
    ASSERT_EQ(656, b.w);
    ASSERT_EQ(306, b.h);            // 102 x 3
    ASSERT_EQ(b.y + b.h, t.y);      // one shared edge
    ASSERT_EQ(b.x, t.x);
    ASSERT_EQ(656, t.w);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H - S, t.y + t.h);
    ASSERT_EQ(158, t.h);          // 480 - 8 - 306 - 8
    ASSERT(inside(b, pane()));
    ASSERT(inside(t, pane()));
    PASS();
}

// Full screen: the pane, the band and the HUD edge to edge, never the status
// band above.
TEST full_covers_pane_band_and_hud_not_the_status_band(void) {
    rome();
    ML_Rect r = ml_full();
    ASSERT_EQ(CL_MAP_X, r.x);
    ASSERT_EQ(CL_MAP_Y, r.y);
    ASSERT_EQ(776, r.w);            // 672 + 8 + 96
    ASSERT_EQ(480, r.h);
    ASSERT_EQ(CL_SIDEBAR_X + CL_SIDEBAR_W, r.x + r.w);
    ASSERT(r.y >= CL_STATUS_Y + CL_STATUS_H);
    ASSERT_EQ(CL_SCREEN_W - CL_FRAME_RIGHT_W, r.x + r.w);
    PASS();
}

// Capacity is measured, not declared: it follows the loaded font. Rome's
// 16 px face and 18 px lines, stated once so a layout change that starves a
// panel of text shows up here: small 40 x 6, large 35 x 20, location 40 x 7,
// full 47 x 25.
TEST capacity_follows_the_glyph(void) {
    rome();
    ML_Rect s = ml_small(), l = ml_large(), t = ml_loc_text(), f = ml_full();
    ASSERT_EQ((s.w - 2 * ML_PAD) / BFONT_GLYPH_W, ml_cols(s));
    ASSERT_EQ((f.h - 2 * ML_PAD) / BFONT_GLYPH_H, ml_lines(f));
    int rome_small_h = ML_SMALL_LINES * 18 + 2 * ML_PAD;
    ASSERT_EQ(40, (s.w - 2 * ML_PAD) / 16);  ASSERT_EQ(6,  (rome_small_h - 2 * ML_PAD) / 18);
    ASSERT_EQ(35, (l.w - 2 * ML_PAD) / 16);  ASSERT_EQ(20, (l.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(40, (t.w - 2 * ML_PAD) / 16);  ASSERT_EQ(7,  (t.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(47, (f.w - 2 * ML_PAD) / 16);  ASSERT_EQ(25, (f.h - 2 * ML_PAD) / 18);
    PASS();
}


// --debug gates the game menu's Debug page (src/views.c): without the flag the
// modern root has no Debug row, so no cheat is reachable.
static int menu_has_debug_row(bool debug) {
    rome();
    resources_republish(&s_res);
    views_menu_bind(NULL, NULL);
    views_menu_set_debug(debug);
    views_set(VIEW_MENU);
    int found = 0;
    for (int i = 0; i < views_menu_entry_count(); i++) {
        const char *l = views_menu_entry_label(i);
        if (l && strcmp(l, "Debug") == 0) found = 1;
    }
    views_set(VIEW_NONE);
    views_menu_set_debug(false);
    resources_republish(NULL);
    return found;
}

TEST debug_row_only_with_debug_flag(void) {
    ASSERT_EQ(0, menu_has_debug_row(false));
    ASSERT_EQ(1, menu_has_debug_row(true));
    PASS();
}

// The modern root is Screens and Actions pages, then Controls, Save, Load,
// New Game and Exit; no row carries a key letter.
TEST root_is_screens_actions_then_system_rows(void) {
    rome();
    strcpy(s_res.ui.menu_screens, "Screens");  strcpy(s_res.ui.menu_actions, "Actions");
    strcpy(s_res.ui.menu_save, "Save");        strcpy(s_res.ui.menu_load, "Load");
    strcpy(s_res.ui.menu_new_game, "New Game"); strcpy(s_res.ui.menu_exit, "Exit");
    resources_republish(&s_res);
    views_menu_bind(NULL, NULL);
    views_set(VIEW_MENU);
    static const char *want[] = { "Screens", "Actions", "Controls", "Save", "Load", "New Game", "Exit" };
    ASSERT_EQ(7, views_menu_entry_count());
    for (int i = 0; i < 7; i++) ASSERT_STR_EQ(want[i], views_menu_entry_label(i));
    ASSERT(views_menu_entry_is_submenu(0));
    ASSERT(views_menu_entry_is_submenu(1));
    views_set(VIEW_NONE);
    resources_republish(NULL);
    PASS();
}

SUITE(unit_modern_layout_suite) {
    RUN_TEST(spacing_is_three_two_three);
    RUN_TEST(vertical_mirrors_horizontal);
    RUN_TEST(short_buffer_does_not_mirror);
    RUN_TEST(small_is_inset_by_the_spacing_on_the_bottom);
    RUN_TEST(large_is_six_by_four_tiles_centred);
    RUN_TEST(location_backdrop_is_integer_3x_inset_and_text_fills_below);
    RUN_TEST(full_covers_pane_band_and_hud_not_the_status_band);
    RUN_TEST(capacity_follows_the_glyph);
    RUN_TEST(debug_row_only_with_debug_flag);
    RUN_TEST(root_is_screens_actions_then_system_rows);
}
