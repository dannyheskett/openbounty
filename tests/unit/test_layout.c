// Layout and presentation for a pack that declares its buffer
// (render.native_w/native_h): the declared buffer is the smallest screen; on a
// bigger surface the screen is the surface at the largest whole zoom (3 at
// most) the declared buffer fits, and the map takes everything between the two
// columns. Legacy geometry must be exactly what it was.

#include "greatest.h"
#include "layout.h"
#include "present.h"
#include "resources.h"
#include "modern/page.h"
#include <string.h>

static Resources s_res;

// Glory of Rome's render block: 96 px tiles, five by five whole tiles, 800 x 504.
static void rome(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_MODERN;
    s_res.render.tile_w = 96;  s_res.render.tile_h = 96;
    s_res.render.tiles_w = 5;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 1;
    s_res.render.native_w = 800; s_res.render.native_h = 504;
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

// The screen a surface gets: its zoom, then the layout at that zoom.
static int fit(int surface_w, int surface_h) {
    int z = present_max_scale(surface_w, surface_h);
    layout_grow_native(surface_w, surface_h, z);
    return z;
}

// 800 = 12 + 96 + 4 + 576 + 4 + 96 + 12 across; 504 = 12 + 480 + 12 down.
TEST declared_buffer_is_the_smallest_screen(void) {
    rome();
    ASSERT(CL_IS_MODERN);
    ASSERT(CL_IS_NATIVE);
    ASSERT_EQ(800, CL_SCREEN_W);
    ASSERT_EQ(504, CL_SCREEN_H);
    ASSERT_EQ(12, CL_FRAME_LEFT_W);
    ASSERT_EQ(12, CL_FRAME_RIGHT_W);
    ASSERT_EQ(12, CL_FRAME_TOP_H);
    ASSERT_EQ(12, CL_FRAME_BOTTOM_H);
    ASSERT_EQ(12, CL_RAIL_X);
    ASSERT_EQ(96, CL_RAIL_W);
    ASSERT_EQ(4, CL_SIDEBAR_GAP);
    ASSERT_EQ(112, CL_MAP_X);
    ASSERT_EQ(576, CL_MAP_W);
    ASSERT_EQ(692, CL_SIDEBAR_X);
    ASSERT_EQ(96, CL_SIDEBAR_W);
    ASSERT_EQ(CL_SCREEN_W - CL_FRAME_RIGHT_W, CL_SIDEBAR_X + CL_SIDEBAR_W);
    // No band: the map starts at the frame and runs to it.
    ASSERT_EQ(0, CL_STATUS_H);
    ASSERT_EQ(0, CL_BAR_H);
    ASSERT_EQ(12, CL_MAP_Y);
    ASSERT_EQ(480, CL_MAP_H);
    ASSERT_EQ(CL_SCREEN_H - CL_FRAME_BOTTOM_H, CL_MAP_Y + CL_MAP_H);
    // Both columns run the map's full height.
    ASSERT_EQ(CL_MAP_Y, CL_RAIL_Y);
    ASSERT_EQ(CL_MAP_H, CL_RAIL_H);
    ASSERT_EQ(CL_MAP_Y, CL_SIDEBAR_Y);
    ASSERT_EQ(CL_MAP_H, CL_SIDEBAR_H);
    // Five whole tiles across and down; the 96 px left across are a half tile
    // either side.
    ASSERT_EQ(5, CL_MAP_TILES_W);
    ASSERT_EQ(5, CL_MAP_TILES_H);
    ASSERT_EQ(96, CL_MAP_W - CL_MAP_TILES_W * CL_TILE_W);
    ASSERT_EQ(0, CL_MAP_H - CL_MAP_TILES_H * CL_TILE_H);
    int w, h;
    layout_min_window(&w, &h);
    ASSERT_EQ(800, w);
    ASSERT_EQ(504, h);
    PASS();
}

TEST zoom_is_the_largest_whole_fit_of_the_declared_buffer(void) {
    rome();
    ASSERT_EQ(1, present_max_scale(800, 504));
    ASSERT_EQ(1, present_max_scale(1599, 1008));
    ASSERT_EQ(1, present_max_scale(1600, 1007));
    ASSERT_EQ(2, present_max_scale(1600, 1008));
    ASSERT_EQ(3, present_max_scale(2400, 1512));
    ASSERT_EQ(3, present_max_scale(7680, 4320));   // capped, never 4x
    ASSERT_EQ(1, present_max_scale(640, 400));     // below the smallest screen
    PASS();
}

// Every surface the game is played on: its zoom, the screen, and the whole
// tiles the map shows.
TEST the_surfaces(void) {
    static const struct { int sw, sh, z, w, h, tw, th; } T[] = {
        {  800,  504, 1,  800, 504,  5, 5 },   // desktop, the smallest window
        { 1600, 1008, 2,  800, 504,  5, 5 },   // 1080p desktop, as opened
        { 1920, 1017, 2,  960, 508,  7, 5 },   // 1080p desktop, maximised
        { 2560, 1380, 2, 1280, 690, 11, 5 },   // 1440p, maximised
        { 3840, 2100, 3, 1280, 700, 11, 7 },   // 4K
        { 1920,  969, 1, 1920, 969, 17, 9 },   // a browser, 1920 x 969 viewport
        { 2250, 1107, 2, 1125, 553,  9, 5 },   // iPhone 12 safe area
        { 2040, 1017, 2, 1020, 508,  7, 5 },   // iPhone mini safe area
        { 2400, 1080, 2, 1200, 540,  9, 5 },   // Android 2400 x 1080
        { 2360, 1600, 2, 1180, 800,  9, 7 },   // iPad landscape
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        rome();
        ASSERT_EQ(T[i].z, fit(T[i].sw, T[i].sh));
        ASSERT_EQ(T[i].w, CL_SCREEN_W);
        ASSERT_EQ(T[i].h, CL_SCREEN_H);
        ASSERT_EQ(T[i].tw, CL_MAP_TILES_W);
        ASSERT_EQ(T[i].th, CL_MAP_TILES_H);
        // The frame, the columns and the gaps never change; the map is
        // everything between the columns, and at least the whole tiles.
        ASSERT_EQ(12, CL_FRAME_LEFT_W);
        ASSERT_EQ(12, CL_FRAME_TOP_H);
        ASSERT_EQ(96, CL_RAIL_W);
        ASSERT_EQ(112, CL_MAP_X);
        ASSERT_EQ(CL_SCREEN_W - 224, CL_MAP_W);
        ASSERT_EQ(CL_SCREEN_H - 24, CL_MAP_H);
        ASSERT_EQ(1, CL_MAP_TILES_W % 2);                 // odd: the hero centres
        ASSERT_EQ(1, CL_MAP_TILES_H % 2);
        ASSERT(CL_MAP_TILES_W * CL_TILE_W <= CL_MAP_W);
        // What the whole tiles leave is less than a tile each side, so one
        // part tile either side fills it.
        ASSERT(CL_MAP_W - CL_MAP_TILES_W * CL_TILE_W < 2 * CL_TILE_W);
        ASSERT(CL_MAP_H - CL_MAP_TILES_H * CL_TILE_H < 2 * CL_TILE_H);
        // The surface is used whole: less than one zoom step is left over.
        ASSERT(T[i].sw - CL_SCREEN_W * T[i].z < T[i].z);
        ASSERT(T[i].sh - CL_SCREEN_H * T[i].z < T[i].z);
    }
    PASS();
}

TEST growth_depends_on_the_surface_alone(void) {
    rome();
    ASSERT_FALSE(layout_grow_native(800, 504, 1));        // the declared buffer
    ASSERT(layout_grow_native(2400, 1080, 2));
    ASSERT_FALSE(layout_grow_native(2400, 1080, 2));      // idempotent
    ASSERT(layout_grow_native(800, 504, 1));              // and back
    ASSERT_EQ(800, CL_SCREEN_W);
    ASSERT_EQ(504, CL_SCREEN_H);
    ASSERT_EQ(5, CL_MAP_TILES_W);
    PASS();
}

// A surface under the declared buffer (only a browser window can be) keeps
// the declared buffer; present_scaled fits it down to the window.
TEST below_the_smallest_screen_the_screen_stays_declared(void) {
    rome();
    ASSERT_EQ(1, fit(640, 400));
    ASSERT_EQ(800, CL_SCREEN_W);
    ASSERT_EQ(504, CL_SCREEN_H);
    ASSERT_EQ(1, fit(1000, 480));
    ASSERT_EQ(1000, CL_SCREEN_W);
    ASSERT_EQ(504, CL_SCREEN_H);
    PASS();
}

TEST target_is_the_screen_times_the_zoom(void) {
    rome();
    int w, h;
    present_target_size(1600, 1008, &w, &h);
    ASSERT_EQ(800 * 2, w);
    ASSERT_EQ(504 * 2, h);
    present_target_size(800, 504, &w, &h);
    ASSERT_EQ(800, w);
    ASSERT_EQ(504, h);
    present_target_size(2400, 1512, &w, &h);
    ASSERT_EQ(800 * 3, w);
    legacy();
    present_target_size(1920, 1080, &w, &h);     // legacy: the screen, never zoomed
    ASSERT_EQ(320, w);
    ASSERT_EQ(200, h);
    PASS();
}

// A tap maps back through the blit rect.
TEST taps_map_through_the_blit_rect(void) {
    int sx, sy;
    present_store_dst(0, 0, 1600, 1008, 2);
    ASSERT(present_window_to_screen(1599, 1007, &sx, &sy));
    ASSERT_EQ(799, sx);
    ASSERT_EQ(503, sy);
    ASSERT_EQ(22, present_window_len_to_design(44));
    present_store_dst(0, 0, 0, 0, 0);
    ASSERT_FALSE(present_window_to_screen(5, 5, &sx, &sy));
    ASSERT_EQ(44, present_window_len_to_design(44));
    PASS();
}

// Below the smallest screen the frame is fitted down, its shape kept.
TEST a_frame_too_big_is_fitted_down(void) {
    int w, h;
    ASSERT_FALSE(present_fit_down(800, 504, 800, 504, &w, &h));
    ASSERT_EQ(800, w);
    ASSERT_EQ(504, h);
    ASSERT(present_fit_down(800, 504, 640, 400, &w, &h));   // height-bound
    ASSERT_EQ(634, w);
    ASSERT_EQ(400, h);
    ASSERT(present_fit_down(800, 504, 400, 900, &w, &h));   // width-bound
    ASSERT_EQ(400, w);
    ASSERT_EQ(252, h);
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
    ASSERT_EQ(9,  CL_STATUS_H);
    ASSERT_EQ(5,  CL_BAR_H);
    ASSERT_EQ(22, CL_MAP_Y);
    ASSERT_EQ(16, CL_MAP_X);
    ASSERT_EQ(240, CL_MAP_W);
    ASSERT_EQ(170, CL_MAP_H);
    ASSERT_EQ(0, CL_RAIL_W);
    ASSERT_EQ(2, CL_SCALE);
    ASSERT_EQ(3, present_scale(1280, 720));
    ASSERT_EQ(5, present_max_scale(4000, 3000));
    ASSERT_FALSE(layout_grow_native(2400, 1080, 2));   // never grows
    ASSERT_EQ(320, CL_SCREEN_W);
    int w, h;
    layout_min_window(&w, &h);
    ASSERT_EQ(320, w);
    ASSERT_EQ(200, h);
    PASS();
}

TEST modern_without_native_still_follows_the_window(void) {
    rome();
    s_res.render.native_w = 0; s_res.render.native_h = 0;
    layout_init((const struct Resources *)&s_res);
    ASSERT_FALSE(CL_IS_NATIVE);
    ASSERT_EQ(16, CL_FRAME_LEFT_W);
    ASSERT_EQ(8, CL_FRAME_TOP_H);
    ASSERT_EQ(0, CL_RAIL_W);
    ASSERT(layout_fit_window(1920, 1080, 1));
    ASSERT_EQ(1920, CL_SCREEN_W);
    PASS();
}

// A full page (the smallest screen's interior, 776 x 480) floats once the
// space inside the frame holds it, its 12 px ring and a 12 px gap on every
// side: 848 x 552 and not a pixel less. Every full page floats or fills
// together; a menu page (a full page less a ring and a gap on every side)
// and a message always float.
TEST a_full_page_floats_from_848_by_552(void) {
    rome();
    page_frame_begin();
    ASSERT_EQ(776, page_full_w());
    ASSERT_EQ(480, page_full_h());
    ASSERT_EQ(728, page_menu_w());
    ASSERT_EQ(432, page_menu_h());
    ASSERT_EQ(528, page_msg_w());
    fit(848, 552);
    ASSERT_EQ(848, CL_SCREEN_W);
    ASSERT(page_full_floats());
    fit(847, 552);
    ASSERT_FALSE(page_full_floats());
    fit(848, 551);
    ASSERT_FALSE(page_full_floats());
    // The smallest screen: a full page fills it.
    fit(800, 504);
    ASSERT_FALSE(page_full_floats());
    // Across the surfaces: the phones at their widest and the tablet float, the
    // mini and a maximised 1080p window fill.
    fit(2250, 1107); ASSERT(page_full_floats());
    fit(2040, 1017); ASSERT_FALSE(page_full_floats());
    fit(1920, 1017); ASSERT_FALSE(page_full_floats());
    fit(2400, 1080); ASSERT_FALSE(page_full_floats());
    fit(2360, 1600); ASSERT(page_full_floats());
    fit(1920, 969);  ASSERT(page_full_floats());
    PASS();
}

SUITE(unit_layout_suite) {
    RUN_TEST(declared_buffer_is_the_smallest_screen);
    RUN_TEST(zoom_is_the_largest_whole_fit_of_the_declared_buffer);
    RUN_TEST(the_surfaces);
    RUN_TEST(growth_depends_on_the_surface_alone);
    RUN_TEST(below_the_smallest_screen_the_screen_stays_declared);
    RUN_TEST(target_is_the_screen_times_the_zoom);
    RUN_TEST(taps_map_through_the_blit_rect);
    RUN_TEST(a_frame_too_big_is_fitted_down);
    RUN_TEST(legacy_geometry_is_unchanged);
    RUN_TEST(modern_without_native_still_follows_the_window);
    RUN_TEST(a_full_page_floats_from_848_by_552);
    // Leave the layout as the fixture pack expects it.
    legacy();
}
