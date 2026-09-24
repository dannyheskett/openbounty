// The mobile presentation decision, and the tap mapping that has to undo it.
//
// On a phone the frame is blitted at the largest whole-number multiple that
// fits the safe area, centred. A tap arrives in device pixels and has to come
// back as a pack pixel, which is present_window_to_screen dividing by the
// scale that was actually used. If the two ever disagree -- the frame drawn
// at 2x and the tap divided by 1 -- every touch lands in the wrong place, and
// nothing on a desktop would notice.
//
// Both halves are plain arithmetic over what the platform reported, so they
// are tested here rather than on a device: what a device still has to prove
// is that UIKit/Android hand over the contact at all.

#include "greatest.h"
#include "present.h"

TEST multiple_is_the_largest_whole_fit(void) {
    // Rome's 800x504 on the phones this ships to.
    ASSERT_EQ(2, present_fit_multiple(800, 504, 2400, 1080));  // Pixel 6, landscape
    ASSERT_EQ(2, present_fit_multiple(800, 504, 2622, 1206));  // iPhone 16 Simulator
    ASSERT_EQ(4, present_fit_multiple(800, 504, 3200, 2400));  // a tablet
    // Height is the binding dimension on a wide phone: 2400/800 = 3, but
    // 1079/504 = 2, and the smaller wins or the frame would not fit.
    ASSERT_EQ(2, present_fit_multiple(800, 504, 2400, 1079));
    PASS();
}

TEST multiple_never_shrinks_below_one(void) {
    // A screen smaller than the frame: the answer is 1 and the frame is
    // centred and clipped, never scaled down to a blur.
    ASSERT_EQ(1, present_fit_multiple(800, 504, 640, 320));
    ASSERT_EQ(1, present_fit_multiple(800, 504, 0, 0));
    // A degenerate frame cannot divide by zero.
    ASSERT_EQ(1, present_fit_multiple(0, 0, 2400, 1080));
    PASS();
}

TEST a_tap_comes_back_as_the_pack_pixel_under_it(void) {
    // 800x504 at 2x is 1600x1008, centred in 2400x1080: x from 400, y from 36.
    int fit = present_fit_multiple(800, 504, 2400, 1080);
    int w = 800 * fit, h = 504 * fit;
    int x = (2400 - w) / 2, y = (1080 - h) / 2;
    present_store_dst(x, y, w, h, fit);

    int sx = 0, sy = 0;
    ASSERT(present_window_to_screen(x, y, &sx, &sy));
    ASSERT_EQ(0, sx); ASSERT_EQ(0, sy);

    // The middle of the frame is the middle of the pack.
    ASSERT(present_window_to_screen(x + w / 2, y + h / 2, &sx, &sy));
    ASSERT_EQ(400, sx); ASSERT_EQ(252, sy);

    // The last device pixel inside the frame is the last pack pixel.
    ASSERT(present_window_to_screen(x + w - 1, y + h - 1, &sx, &sy));
    ASSERT_EQ(799, sx); ASSERT_EQ(503, sy);

    // Both device pixels of a 2x block answer the same pack pixel.
    int ax = 0, ay = 0, bx = 0, by = 0;
    ASSERT(present_window_to_screen(x + 10, y + 10, &ax, &ay));
    ASSERT(present_window_to_screen(x + 11, y + 11, &bx, &by));
    ASSERT_EQ(ax, bx); ASSERT_EQ(ay, by);
    PASS();
}

TEST a_tap_in_the_letterbox_is_not_in_the_frame(void) {
    int fit = present_fit_multiple(800, 504, 2400, 1080);
    int w = 800 * fit, h = 504 * fit;
    int x = (2400 - w) / 2, y = (1080 - h) / 2;
    present_store_dst(x, y, w, h, fit);

    int sx = 0, sy = 0;
    ASSERT_FALSE(present_window_to_screen(x - 1, y, &sx, &sy));      // left band
    ASSERT_FALSE(present_window_to_screen(x, y - 1, &sx, &sy));      // above
    ASSERT_FALSE(present_window_to_screen(x + w, y, &sx, &sy));      // right band
    ASSERT_FALSE(present_window_to_screen(x, y + h, &sx, &sy));      // below
    PASS();
}

SUITE(unit_present_mobile_suite) {
    RUN_TEST(multiple_is_the_largest_whole_fit);
    RUN_TEST(multiple_never_shrinks_below_one);
    RUN_TEST(a_tap_comes_back_as_the_pack_pixel_under_it);
    RUN_TEST(a_tap_in_the_letterbox_is_not_in_the_frame);
}
