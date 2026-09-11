// The scene dim under detail views: the alpha for a pack's percent.
#include "greatest.h"
#include "overlay.h"

TEST dim_alpha_maps_percent_and_clamps(void) {
    ASSERT_EQ(0, overlay_dim_alpha(0));
    ASSERT_EQ(255, overlay_dim_alpha(100));
    ASSERT_EQ(140, overlay_dim_alpha(55));
    ASSERT_EQ(0, overlay_dim_alpha(-10));
    ASSERT_EQ(255, overlay_dim_alpha(400));
    PASS();
}

SUITE(unit_overlay_dim_suite) {
    RUN_TEST(dim_alpha_maps_percent_and_clamps);
}
