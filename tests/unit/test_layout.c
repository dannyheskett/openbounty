// Layout and presentation for a pack that fixes its buffer size
// (render.native_w/native_h): the screen is the buffer, the viewport is the
// declared tile count, the spare space becomes chrome bands, and the buffer
// is shown at 1x, 2x or 3x only. Legacy geometry must be exactly what it was.

#include "greatest.h"
#include "layout.h"
#include "present.h"
#include "resources.h"
#include <string.h>

static Resources s_res;

static void rome_like(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_MODERN;
    s_res.render.tile_w = 96;  s_res.render.tile_h = 96;
    s_res.render.tiles_w = 7;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 2;
    s_res.render.native_w = 960; s_res.render.native_h = 540;
    layout_init((const struct Resources *)&s_res);
}

static void legacy(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_LEGACY;
    s_res.render.tile_w = 48;  s_res.render.tile_h = 34;
    s_res.render.tiles_w = 5;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 1;
    layout_init((const struct Resources *)&s_res);
}

TEST native_buffer_fixes_the_screen_and_widens_the_bands(void) {
    rome_like();
    ASSERT(CL_IS_MODERN);
    ASSERT(CL_IS_NATIVE);
    ASSERT_EQ(960, CL_SCREEN_W);
    ASSERT_EQ(540, CL_SCREEN_H);
    ASSERT_EQ(7, CL_MAP_TILES_W);
    ASSERT_EQ(5, CL_MAP_TILES_H);
    ASSERT_EQ(672, CL_MAP_W);
    ASSERT_EQ(480, CL_MAP_H);
    // 960 - 672 map - 96 sidebar = 192 spare, split 96 and 96.
    ASSERT_EQ(96, CL_FRAME_LEFT_W);
    ASSERT_EQ(96, CL_FRAME_RIGHT_W);
    // 540 - 18 status - 10 bar - 480 map = 32 spare, split 16 and 16.
    ASSERT_EQ(16, CL_FRAME_TOP_H);
    ASSERT_EQ(16, CL_FRAME_BOTTOM_H);
    ASSERT_EQ(CL_SCREEN_W, CL_FRAME_LEFT_W + CL_MAP_W + CL_SIDEBAR_W + CL_FRAME_RIGHT_W);
    ASSERT_EQ(CL_SCREEN_H, CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H + CL_MAP_H + CL_FRAME_BOTTOM_H);
    ASSERT_EQ(1, CL_SCALE);
    PASS();
}

TEST native_buffer_ignores_the_window(void) {
    rome_like();
    ASSERT_FALSE(layout_fit_window(1920, 1080, 1));
    ASSERT_EQ(960, CL_SCREEN_W);
    ASSERT_EQ(7, CL_MAP_TILES_W);
    int w, h;
    layout_min_window(&w, &h);
    ASSERT_EQ(960, w);
    ASSERT_EQ(540, h);
    PASS();
}

TEST native_scale_is_one_two_or_three(void) {
    rome_like();
    ASSERT_EQ(1, present_max_scale(960, 540));
    ASSERT_EQ(1, present_max_scale(1919, 1080));
    ASSERT_EQ(2, present_max_scale(1920, 1080));
    ASSERT_EQ(3, present_max_scale(2880, 1620));
    ASSERT_EQ(3, present_max_scale(7680, 4320));   // capped, never 4x
    present_set_scale(3);
    ASSERT_EQ(2, present_scale(1920, 1080));        // clamped to the window
    ASSERT_EQ(3, present_scale(2880, 1620));
    present_set_scale(1);
    ASSERT_EQ(1, present_scale(2880, 1620));        // the choice, not the fit
    PASS();
}

TEST legacy_geometry_is_unchanged(void) {
    legacy();
    ASSERT_FALSE(CL_IS_MODERN);
    ASSERT_FALSE(CL_IS_NATIVE);
    ASSERT_EQ(320, CL_SCREEN_W);
    ASSERT_EQ(200, CL_SCREEN_H);
    ASSERT_EQ(16, CL_FRAME_LEFT_W);
    ASSERT_EQ(16, CL_FRAME_RIGHT_W);
    ASSERT_EQ(8,  CL_FRAME_TOP_H);
    ASSERT_EQ(8,  CL_FRAME_BOTTOM_H);
    ASSERT_EQ(22, CL_MAP_Y);
    ASSERT_EQ(240, CL_MAP_W);
    ASSERT_EQ(170, CL_MAP_H);
    ASSERT_EQ(2, CL_SCALE);
    ASSERT_EQ(3, present_scale(1280, 720));
    ASSERT_EQ(5, present_max_scale(4000, 3000));
    int w, h;
    layout_min_window(&w, &h);
    ASSERT_EQ(320, w);
    ASSERT_EQ(200, h);
    PASS();
}

TEST modern_without_native_still_follows_the_window(void) {
    rome_like();
    s_res.render.native_w = 0; s_res.render.native_h = 0;
    layout_init((const struct Resources *)&s_res);
    ASSERT_FALSE(CL_IS_NATIVE);
    ASSERT_EQ(32, CL_FRAME_LEFT_W);
    ASSERT_EQ(16, CL_FRAME_TOP_H);
    ASSERT(layout_fit_window(1920, 1080, 1));
    ASSERT_EQ(1920, CL_SCREEN_W);
    PASS();
}

SUITE(unit_layout_suite) {
    RUN_TEST(native_buffer_fixes_the_screen_and_widens_the_bands);
    RUN_TEST(native_buffer_ignores_the_window);
    RUN_TEST(native_scale_is_one_two_or_three);
    RUN_TEST(legacy_geometry_is_unchanged);
    RUN_TEST(modern_without_native_still_follows_the_window);
    // Leave the layout as the fixture pack expects it.
    legacy();
}
