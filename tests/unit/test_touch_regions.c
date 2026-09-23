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

// The left rail and the map are neighbours, not rivals: the map region is
// registered first (the main loop registers it before the frame is drawn), so
// the two must not overlap -- CL_MAP_X starts after the rail's column. These
// pin the arithmetic with Rome's numbers: a 96 px rail at the frame's edge,
// an 8 px gap, then the map.
#define RAIL_X   12
#define RAIL_Y   73
#define RAIL_W   96
#define TILE     96
#define MAP_X   (RAIL_X + RAIL_W + 8)
#define MAP_W   (7 * TILE)
#define MAP_H   (5 * TILE)

static void rail_rows(void) {
    for (int i = 0; i < 5; i++)
        touch_region_row(RAIL_X, RAIL_Y + i * TILE, RAIL_W, TILE, TOUCH_LIST_RAIL, i);
}

TEST the_map_is_registered_first_and_the_rail_still_takes_its_column(void) {
    touch_region_map(MAP_X, RAIL_Y, MAP_W, MAP_H, TILE, TILE, 3, 2, 0);
    rail_rows();
    touch_frame();

    int list = 0, row = -1, key = 0;
    // Third tile down the rail.
    ASSERT(touch_last_hit(RAIL_X + 10, RAIL_Y + 2 * TILE + 10, &list, &row, &key));
    ASSERT_EQ(TOUCH_LIST_RAIL, list);
    ASSERT_EQ(2, row);

    // And the map is untouched by the rail: a tap left of centre still steps.
    list = 0; row = -1; key = 0;
    ASSERT(touch_last_hit(MAP_X + 10, RAIL_Y + 2 * TILE + 10, &list, &row, &key));
    ASSERT_EQ(0, list);
    ASSERT(key != 0);
    PASS();
}

// A page owns every tap while it is open: rail_draw registers nothing then,
// which is this -- the rows simply are not there, so the tap falls through to
// whatever the page put down.
TEST with_no_rail_rows_registered_the_column_falls_through(void) {
    touch_region_row(RAIL_X, RAIL_Y, 700, 300, TOUCH_LIST_MENU, 0);   // a page
    touch_frame();

    int list = 0, row = -1, key = 0;
    ASSERT(touch_last_hit(RAIL_X + 10, RAIL_Y + 10, &list, &row, &key));
    ASSERT_EQ(TOUCH_LIST_MENU, list);
    PASS();
}

SUITE(unit_touch_regions_suite) {
    RUN_TEST(the_panel_lies_over_the_painting);
    RUN_TEST(rows_first_wins_the_tap);
    RUN_TEST(the_painting_still_picks_a_class_outside_the_panel);
    RUN_TEST(columns_first_shadow_the_rows);
    RUN_TEST(the_map_is_registered_first_and_the_rail_still_takes_its_column);
    RUN_TEST(with_no_rail_rows_registered_the_column_falls_through);
}
