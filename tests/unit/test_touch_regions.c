// Touch region precedence: a tap takes the FIRST region registered that
// contains it (`resolve_tap` / `touch_last_hit`, src/touch.c). Screens that
// draw a panel on top of a bigger region therefore have to register the panel's
// rows BEFORE the region underneath, or the panel is untappable.
//
// The class picker is the case this pins (REQ-532, issue #47): its four column
// regions span the whole painting, and the picked class's description panel --
// Continue and Cancel -- sits inside it. Registered the other way round, every
// tap on those rows re-picked a class and touch could not leave the screen.

#include "greatest.h"
#include "touch.h"

// Rome's geometry: the 256x164 picker at 3x in an 800x532 buffer, and the
// description panel along the foot.
#define ART_X   16
#define ART_Y   20
#define ART_W  768
#define ART_H  492
#define COLS     4
#define PANEL_X  50
#define PANEL_Y 348
#define PANEL_W 700
#define PANEL_H 168

static void columns(void) {
    for (int k = 0; k < COLS; k++)
        touch_region_row(ART_X + k * (ART_W / COLS), ART_Y, ART_W / COLS, ART_H,
                         TOUCH_LIST_CLASS, k);
}

static void confirm_rows(void) {
    int rh = PANEL_H / 2;
    for (int i = 0; i < 2; i++)
        touch_region_row(PANEL_X, PANEL_Y + i * rh, PANEL_W, rh,
                         TOUCH_LIST_CLASS_CONFIRM, i);
}

// The panel really does lie over the painting; without that overlap the rest of
// this file proves nothing. (It hangs a few pixels below the art's foot, so
// this is an overlap, not containment.)
TEST the_panel_lies_over_the_painting(void) {
    ASSERT(PANEL_X < ART_X + ART_W && ART_X < PANEL_X + PANEL_W);
    ASSERT(PANEL_Y < ART_Y + ART_H && ART_Y < PANEL_Y + PANEL_H);
    PASS();
}

TEST rows_first_wins_the_tap(void) {
    confirm_rows();
    columns();
    touch_frame();

    int list = 0, row = -1, key = 0;
    // Continue, then Cancel: the tap reaches the row, not the painting.
    ASSERT(touch_last_hit(PANEL_X + 10, PANEL_Y + 10, &list, &row, &key));
    ASSERT_EQ(TOUCH_LIST_CLASS_CONFIRM, list);
    ASSERT_EQ(0, row);
    ASSERT(touch_last_hit(PANEL_X + 10, PANEL_Y + PANEL_H / 2 + 10, &list, &row, &key));
    ASSERT_EQ(TOUCH_LIST_CLASS_CONFIRM, list);
    ASSERT_EQ(1, row);
    PASS();
}

TEST the_painting_still_picks_a_class_outside_the_panel(void) {
    confirm_rows();
    columns();
    touch_frame();

    int list = 0, row = -1, key = 0;
    // Above the panel, over the third column.
    int x = ART_X + 2 * (ART_W / COLS) + 10;
    ASSERT(touch_last_hit(x, ART_Y + 10, &list, &row, &key));
    ASSERT_EQ(TOUCH_LIST_CLASS, list);
    ASSERT_EQ(2, row);
    PASS();
}

// The failure mode itself, stated as a rule: register the columns first and the
// panel's rows become unreachable.
TEST columns_first_shadow_the_rows(void) {
    columns();
    confirm_rows();
    touch_frame();

    int list = 0, row = -1, key = 0;
    ASSERT(touch_last_hit(PANEL_X + 10, PANEL_Y + 10, &list, &row, &key));
    ASSERT_EQ(TOUCH_LIST_CLASS, list);
    PASS();
}

SUITE(unit_touch_regions_suite) {
    RUN_TEST(the_panel_lies_over_the_painting);
    RUN_TEST(rows_first_wins_the_tap);
    RUN_TEST(the_painting_still_picks_a_class_outside_the_panel);
    RUN_TEST(columns_first_shadow_the_rows);
}
