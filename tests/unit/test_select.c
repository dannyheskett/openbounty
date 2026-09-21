// The selection helper's pure pieces: cursor wrap and hotkey mapping.
#include "greatest.h"
#include "select.h"
#include "raylib.h"

TEST cursor_wraps_both_ways(void) {
    ASSERT_EQ(1, sel_wrap(0, 1, 5));
    ASSERT_EQ(4, sel_wrap(0, -1, 5));
    ASSERT_EQ(0, sel_wrap(4, 1, 5));
    ASSERT_EQ(0, sel_wrap(3, 1, 0));      // empty list never indexes
    PASS();
}

TEST hotkeys_map_to_rows_inside_the_list(void) {
    ASSERT_EQ(0, sel_hotkey_row(KEY_A, KEY_A, 5));
    ASSERT_EQ(4, sel_hotkey_row(KEY_E, KEY_A, 5));
    ASSERT_EQ(-1, sel_hotkey_row(KEY_F, KEY_A, 5));   // past the list
    ASSERT_EQ(-1, sel_hotkey_row(KEY_A, 0, 5));       // no hotkeys
    ASSERT_EQ(2, sel_hotkey_row(KEY_THREE, KEY_ONE, 7));
    PASS();
}

SUITE(unit_select_suite) {
    RUN_TEST(cursor_wraps_both_ways);
    RUN_TEST(hotkeys_map_to_rows_inside_the_list);
}
