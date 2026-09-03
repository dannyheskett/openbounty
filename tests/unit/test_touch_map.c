// Touch coordinate mapping: window pixels back to design-space pixels.
//
// present_scaled blits the render target letterboxed and integer-scaled;
// present_window_to_screen inverts that for a tap. The store/read split
// (present_store_dst) exists exactly so this math is testable without a
// window: feed a known blit rect, check the inverse.

#include "greatest.h"
#include "present.h"

// A 320x200 target at 3x, centred in a 1280x720 window:
// dst = { (1280-960)/2, (720-600)/2, 960, 600 } = { 160, 60, 960, 600 }.
static void store_legacy_3x(void) {
    present_store_dst(160, 60, 960, 600, 3);
}

TEST corners_map_to_corner_pixels(void) {
    store_legacy_3x();
    int sx, sy;
    ASSERT(present_window_to_screen(160, 60, &sx, &sy));
    ASSERT_EQ(0, sx);
    ASSERT_EQ(0, sy);
    // Last window pixel inside the blit -> last design pixel.
    ASSERT(present_window_to_screen(160 + 960 - 1, 60 + 600 - 1, &sx, &sy));
    ASSERT_EQ(319, sx);
    ASSERT_EQ(199, sy);
    PASS();
}

TEST every_window_pixel_of_a_cell_maps_to_that_cell(void) {
    store_legacy_3x();
    // Design pixel (10, 20) covers window pixels [190..192] x [120..122].
    for (int wy = 120; wy < 123; wy++) {
        for (int wx = 190; wx < 193; wx++) {
            int sx, sy;
            ASSERT(present_window_to_screen(wx, wy, &sx, &sy));
            ASSERT_EQ(10, sx);
            ASSERT_EQ(20, sy);
        }
    }
    PASS();
}

TEST letterbox_is_outside(void) {
    store_legacy_3x();
    ASSERT_FALSE(present_window_to_screen(0, 0, NULL, NULL));
    ASSERT_FALSE(present_window_to_screen(159, 300, NULL, NULL));   // left bar
    ASSERT_FALSE(present_window_to_screen(640, 59, NULL, NULL));    // top bar
    ASSERT_FALSE(present_window_to_screen(160 + 960, 300, NULL, NULL));
    ASSERT_FALSE(present_window_to_screen(640, 60 + 600, NULL, NULL));
    PASS();
}

TEST unstored_mapping_rejects_everything(void) {
    present_store_dst(0, 0, 0, 0, 0);
    ASSERT_FALSE(present_window_to_screen(100, 100, NULL, NULL));
    PASS();
}

TEST last_dst_reports_what_was_stored(void) {
    store_legacy_3x();
    int x, y, w, h;
    present_last_dst(&x, &y, &w, &h);
    ASSERT_EQ(160, x);
    ASSERT_EQ(60, y);
    ASSERT_EQ(960, w);
    ASSERT_EQ(600, h);
    PASS();
}

SUITE(unit_touch_map_suite) {
    RUN_TEST(corners_map_to_corner_pixels);
    RUN_TEST(every_window_pixel_of_a_cell_maps_to_that_cell);
    RUN_TEST(letterbox_is_outside);
    RUN_TEST(unstored_mapping_rejects_everything);
    RUN_TEST(last_dst_reports_what_was_stored);
}
