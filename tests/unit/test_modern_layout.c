// The modern screen and its five named layouts (src/layout.c,
// src/modern/mlayout.c, REQ-430j), for a pack shaped like Rome: 96 px tiles,
// a 7x5 viewport, a fixed 800x510 buffer.
//
// Vertical positions are asserted against the map pane rather than as bare
// numbers, because the pane's top edge follows the status band and the status
// band follows the font -- which the test binary does not load.

#include "greatest.h"
#include "layout.h"
#include "resources.h"
#include "bfont.h"
#include "modern/mlayout.h"
#include <string.h>

static Resources s_res;

static void rome(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_MODERN;
    s_res.render.tile_w = 96;  s_res.render.tile_h = 96;
    s_res.render.tiles_w = 7;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 1;
    s_res.render.native_w = 800; s_res.render.native_h = 510;
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

// The screen's horizontal spacing is split three ways: left edge, the band
// between the map pane and the HUD, right edge. 800 = 11 + 672 + 10 + 96 + 11.
TEST spacing_is_equal_left_middle_right(void) {
    rome();
    ASSERT_EQ(800, CL_SCREEN_W);
    ASSERT_EQ(11, CL_FRAME_LEFT_W);
    ASSERT_EQ(10, CL_SIDEBAR_GAP);
    ASSERT_EQ(11, CL_FRAME_RIGHT_W);
    ASSERT_EQ(11, CL_MAP_X);
    ASSERT_EQ(672, CL_MAP_W);
    ASSERT_EQ(693, CL_SIDEBAR_X);
    ASSERT_EQ(96, CL_SIDEBAR_W);
    ASSERT_EQ(CL_SCREEN_W - CL_FRAME_RIGHT_W, CL_SIDEBAR_X + CL_SIDEBAR_W);
    // No two of the three differ by more than the one pixel 32 cannot share.
    ASSERT(CL_FRAME_LEFT_W - CL_SIDEBAR_GAP <= 1 && CL_FRAME_RIGHT_W - CL_SIDEBAR_GAP <= 1);
    PASS();
}

// Small: six text lines tall, inset from the pane by the screen's spacing.
TEST small_is_inset_by_the_spacing_on_the_bottom(void) {
    rome();
    int S = ml_space();
    ASSERT_EQ(10, S);
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
    ASSERT_EQ(652, b.w);
    ASSERT_EQ(306, b.h);            // 102 x 3
    ASSERT_EQ(b.y + b.h, t.y);      // one shared edge
    ASSERT_EQ(b.x, t.x);
    ASSERT_EQ(652, t.w);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H - S, t.y + t.h);
    ASSERT_EQ(154, t.h);
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
    ASSERT_EQ(778, r.w);            // 672 + 10 + 96
    ASSERT_EQ(480, r.h);
    ASSERT_EQ(CL_SIDEBAR_X + CL_SIDEBAR_W, r.x + r.w);
    ASSERT(r.y >= CL_STATUS_Y + CL_STATUS_H);
    ASSERT_EQ(CL_SCREEN_W - CL_FRAME_RIGHT_W, r.x + r.w);
    PASS();
}

// Capacity is measured, not declared: it follows the loaded font. Rome's
// 16 px face and 18 px lines, stated once so a layout change that starves a
// panel of text shows up here: small 39 x 6, large 35 x 20, location 39 x 7,
// full 47 x 25.
TEST capacity_follows_the_glyph(void) {
    rome();
    ML_Rect s = ml_small(), l = ml_large(), t = ml_loc_text(), f = ml_full();
    ASSERT_EQ((s.w - 2 * ML_PAD) / BFONT_GLYPH_W, ml_cols(s));
    ASSERT_EQ((f.h - 2 * ML_PAD) / BFONT_GLYPH_H, ml_lines(f));
    int rome_small_h = ML_SMALL_LINES * 18 + 2 * ML_PAD;
    ASSERT_EQ(39, (s.w - 2 * ML_PAD) / 16);  ASSERT_EQ(6,  (rome_small_h - 2 * ML_PAD) / 18);
    ASSERT_EQ(35, (l.w - 2 * ML_PAD) / 16);  ASSERT_EQ(20, (l.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(39, (t.w - 2 * ML_PAD) / 16);  ASSERT_EQ(7,  (t.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(47, (f.w - 2 * ML_PAD) / 16);  ASSERT_EQ(25, (f.h - 2 * ML_PAD) / 18);
    PASS();
}

SUITE(unit_modern_layout_suite) {
    RUN_TEST(spacing_is_equal_left_middle_right);
    RUN_TEST(small_is_inset_by_the_spacing_on_the_bottom);
    RUN_TEST(large_is_six_by_four_tiles_centred);
    RUN_TEST(location_backdrop_is_integer_3x_inset_and_text_fills_below);
    RUN_TEST(full_covers_pane_band_and_hud_not_the_status_band);
    RUN_TEST(capacity_follows_the_glyph);
}
