// The letter selector's grid: cell characters and cursor movement.
#include "greatest.h"
#include "textsel.h"

TEST alpha_grid_cells(void) {
    ASSERT_EQ(29, textsel_count(false));
    ASSERT_EQ(6, textsel_cols(false));
    ASSERT_EQ('A', textsel_char(0, false));
    ASSERT_EQ('Z', textsel_char(25, false));
    ASSERT_EQ(' ', textsel_char(26, false));
    ASSERT_EQ(TEXTSEL_DEL, textsel_char(27, false));
    ASSERT_EQ(TEXTSEL_OK, textsel_char(28, false));
    ASSERT_EQ(0, textsel_char(29, false));
    PASS();
}

TEST numeric_grid_cells(void) {
    ASSERT_EQ(12, textsel_count(true));
    ASSERT_EQ('7', textsel_char(0, true));
    ASSERT_EQ(TEXTSEL_DEL, textsel_char(3, true));
    ASSERT_EQ(TEXTSEL_OK, textsel_char(7, true));
    ASSERT_EQ('0', textsel_char(11, true));
    PASS();
}

TEST cursor_moves_and_wraps(void) {
    ASSERT_EQ(1, textsel_move(0, 1, 0, false));
    ASSERT_EQ(5, textsel_move(0, -1, 0, false));      // wraps within the row
    ASSERT_EQ(6, textsel_move(0, 0, 1, false));       // down a row
    ASSERT_EQ(24, textsel_move(0, 0, -1, false));     // up from the top lands on the last row
    ASSERT_EQ(28, textsel_move(27, 1, 0, false));     // last row has 5 cells
    ASSERT_EQ(24, textsel_move(28, 1, 0, false));     // and wraps within them
    ASSERT_EQ(28, textsel_move(23, 0, 1, false));     // down onto a missing cell clamps to OK
    ASSERT_EQ(0, textsel_move(24, 0, 1, false));      // down from the last row wraps to the top
    PASS();
}

SUITE(unit_textsel_suite) {
    RUN_TEST(alpha_grid_cells);
    RUN_TEST(numeric_grid_cells);
    RUN_TEST(cursor_moves_and_wraps);
}
