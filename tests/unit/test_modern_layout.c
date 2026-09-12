// The modern UI's five named layouts (src/modern/mlayout.c, REQ-430j), for a
// pack shaped like Rome: 96 px tiles, a 7x5 viewport, a fixed 800x510 buffer.
//
// The rects are asserted against the map pane rather than as bare numbers,
// because the pane's top edge follows the status band and the status band
// follows the font -- which the test binary does not load. What matters is
// where each layout sits relative to the pane, the HUD and the band.

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

TEST pane_is_what_rome_draws(void) {
    rome();
    ASSERT_EQ(800, CL_SCREEN_W);
    ASSERT_EQ(672, CL_MAP_W);
    ASSERT_EQ(480, CL_MAP_H);
    ASSERT_EQ(16,  CL_MAP_X);
    ASSERT_EQ(96,  CL_SIDEBAR_W);
    PASS();
}

// Small: the whole pane width, one tile tall, sitting on the pane's bottom.
TEST small_is_a_full_width_band_on_the_bottom(void) {
    rome();
    ML_Rect r = ml_small();
    ASSERT_EQ(CL_MAP_X, r.x);
    ASSERT_EQ(672, r.w);
    ASSERT_EQ(96, r.h);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H, r.y + r.h);
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

// Location: the backdrop at the smallest integer scale that covers the pane's
// width, cropped to it; the text area takes everything under it.
TEST location_backdrop_is_integer_3x_and_text_fills_below(void) {
    rome();
    ASSERT_EQ(3, ml_loc_scale());
    ML_Rect b = ml_loc_backdrop();
    ML_Rect t = ml_loc_text();
    ASSERT_EQ(CL_MAP_X, b.x);
    ASSERT_EQ(CL_MAP_Y, b.y);
    ASSERT_EQ(672, b.w);
    ASSERT_EQ(306, b.h);            // 102 x 3
    ASSERT_EQ(b.y + b.h, t.y);      // no gap, no overlap: one shared edge
    ASSERT_EQ(174, t.h);
    ASSERT_EQ(672, t.w);
    ASSERT_EQ(CL_SIDEBAR_X, t.x + t.w);   // the text area reaches the HUD
    ASSERT(inside(b, pane()));
    ASSERT(inside(t, pane()));
    PASS();
}

// Full screen: the pane and the HUD, but never the status band above.
TEST full_covers_the_pane_and_the_hud_not_the_status_band(void) {
    rome();
    ML_Rect r = ml_full();
    ASSERT_EQ(CL_MAP_X, r.x);
    ASSERT_EQ(CL_MAP_Y, r.y);
    ASSERT_EQ(768, r.w);
    ASSERT_EQ(480, r.h);
    ASSERT_EQ(CL_SIDEBAR_X + CL_SIDEBAR_W, r.x + r.w);
    ASSERT(r.y >= CL_STATUS_Y + CL_STATUS_H);
    ASSERT(r.x + r.w <= CL_SCREEN_W - CL_FRAME_RIGHT_W);
    PASS();
}

// Capacity is measured, not declared: it follows the loaded font. At Rome's
// 16 px face and 18 px lines the plan's numbers come out of the same formula.
TEST capacity_follows_the_glyph(void) {
    rome();
    ML_Rect s = ml_small(), l = ml_large(), t = ml_loc_text(), f = ml_full();
    ASSERT_EQ((s.w - 2 * ML_PAD) / BFONT_GLYPH_W, ml_cols(s));
    ASSERT_EQ((f.h - 2 * ML_PAD) / BFONT_GLYPH_H, ml_lines(f));
    // Rome's metrics, stated once so a layout change that starves a panel of
    // text shows up here: small 41 x 4, large 35 x 20, location 41 x 8,
    // full 47 x 25.
    ASSERT_EQ(41, (s.w - 2 * ML_PAD) / 16);  ASSERT_EQ(4,  (s.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(35, (l.w - 2 * ML_PAD) / 16);  ASSERT_EQ(20, (l.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(41, (t.w - 2 * ML_PAD) / 16);  ASSERT_EQ(8,  (t.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(47, (f.w - 2 * ML_PAD) / 16);  ASSERT_EQ(25, (f.h - 2 * ML_PAD) / 18);
    PASS();
}

SUITE(unit_modern_layout_suite) {
    RUN_TEST(pane_is_what_rome_draws);
    RUN_TEST(small_is_a_full_width_band_on_the_bottom);
    RUN_TEST(large_is_six_by_four_tiles_centred);
    RUN_TEST(location_backdrop_is_integer_3x_and_text_fills_below);
    RUN_TEST(full_covers_the_pane_and_the_hud_not_the_status_band);
    RUN_TEST(capacity_follows_the_glyph);
}
