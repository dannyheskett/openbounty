// The standard modern select list's scroll window (ml_list_first): the cursor
// row is always in view, the window never runs past either end, and a list
// that fits shows from its first row.

#include "greatest.h"
#include "modern/mlist.h"

TEST list_that_fits_starts_at_zero(void) {
    ASSERT_EQ(0, ml_list_first(5, 4, 5));
    ASSERT_EQ(0, ml_list_first(3, 2, 8));
    ASSERT_EQ(0, ml_list_first(0, 0, 4));
    PASS();
}

TEST cursor_stays_in_view(void) {
    for (int count = 1; count <= 12; count++)
        for (int vis = 1; vis <= 6; vis++)
            for (int cur = 0; cur < count; cur++) {
                int first = ml_list_first(count, cur, vis);
                ASSERT(first >= 0);
                ASSERT(cur >= first && cur < first + vis);
                if (count > vis) ASSERT(first + vis <= count);
            }
    PASS();
}

TEST scrolls_only_as_far_as_needed(void) {
    ASSERT_EQ(0, ml_list_first(10, 4, 5));   // row 4 is the last of the first window
    ASSERT_EQ(1, ml_list_first(10, 5, 5));
    ASSERT_EQ(5, ml_list_first(10, 9, 5));   // the window ends at the last row
    PASS();
}

SUITE(unit_mlist_suite) {
    RUN_TEST(list_that_fits_starts_at_zero);
    RUN_TEST(cursor_stays_in_view);
    RUN_TEST(scrolls_only_as_far_as_needed);
}
