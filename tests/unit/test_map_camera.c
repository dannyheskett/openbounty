// The overworld camera (src/map_render.c map_view), read through the hero's
// cell. Modern (#161): the hero is centred except near the world's edge, where
// the camera stops with the map's edge on the pane's; a map smaller than the
// pane sits centred in it. Legacy keeps its whole-tile clamp.

#include "greatest.h"
#include "layout.h"
#include "resources.h"
#include "map_render.h"
#include <string.h>

static Resources s_res;
static Game s_g;
static Map s_m;

// The Glory of Rome's render block: 96 px tiles, five by five whole tiles, 800 x 504.
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

// The hero's cell, standing at (x, y) on a w x h map.
static void cell(int w, int h, int x, int y, int *px, int *py) {
    memset(&s_g, 0, sizeof s_g);
    memset(&s_m, 0, sizeof s_m);
    s_m.width = w;  s_m.height = h;
    s_g.position.x = x;  s_g.position.y = y;
    map_render_hero_cell(&s_g, &s_m, px, py);
}

// The centred camera's cell (DSGN-0017).
static int centre_x(void) { return CL_MAP_X + (CL_MAP_W - CL_TILE_W) / 2; }
static int centre_y(void) { return CL_MAP_Y + ((CL_MAP_H / 2) / CL_TILE_H) * CL_TILE_H; }

TEST the_hero_is_centred_away_from_the_edges(void) {
    rome();
    int x, y;
    cell(64, 64, 32, 32, &x, &y);
    ASSERT_EQ(centre_x(), x);
    ASSERT_EQ(centre_y(), y);
    PASS();
}

TEST the_camera_stops_at_the_top_left(void) {
    rome();
    int x, y;
    cell(64, 64, 0, 0, &x, &y);
    ASSERT_EQ(CL_MAP_X, x);
    ASSERT_EQ(CL_MAP_Y, y);
    cell(64, 64, 1, 1, &x, &y);                        // one in: still clamped
    ASSERT_EQ(CL_MAP_X + CL_TILE_W, x);
    ASSERT_EQ(CL_MAP_Y + CL_TILE_H, y);
    PASS();
}

TEST the_camera_stops_at_the_bottom_right(void) {
    rome();
    int x, y;
    cell(64, 28, 63, 27, &x, &y);
    ASSERT_EQ(CL_MAP_X + CL_MAP_W, x + CL_TILE_W);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H, y + CL_TILE_H);
    PASS();
}

TEST one_axis_clamps_alone(void) {
    rome();
    int x, y;
    cell(64, 64, 0, 32, &x, &y);
    ASSERT_EQ(CL_MAP_X, x);
    ASSERT_EQ(centre_y(), y);
    cell(64, 64, 32, 63, &x, &y);
    ASSERT_EQ(centre_x(), x);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H, y + CL_TILE_H);
    PASS();
}

TEST a_map_smaller_than_the_pane_is_centred(void) {
    rome();
    int x0, y0, x1, y1;
    cell(3, 3, 0, 0, &x0, &y0);                        // the map's first cell
    cell(3, 3, 2, 2, &x1, &y1);                        // and its last
    ASSERT_EQ(x0 + 2 * CL_TILE_W, x1);                 // one map, wherever the hero is
    ASSERT_EQ(y0 + 2 * CL_TILE_H, y1);
    int span_w = 3 * CL_TILE_W, span_h = 3 * CL_TILE_H;
    ASSERT_EQ(CL_MAP_X + (CL_MAP_W - span_w) / 2, x0);
    ASSERT_EQ(CL_MAP_Y + (CL_MAP_H - span_h) / 2, y0);
    PASS();
}

TEST legacy_is_unchanged(void) {
    legacy();
    int x, y;
    cell(64, 64, 32, 32, &x, &y);                      // the centre tile of the 5x5
    ASSERT_EQ(CL_MAP_X + 2 * CL_TILE_W, x);
    ASSERT_EQ(CL_MAP_Y + 2 * CL_TILE_H, y);
    cell(64, 64, 0, 0, &x, &y);
    ASSERT_EQ(CL_MAP_X, x);
    ASSERT_EQ(CL_MAP_Y, y);
    cell(64, 64, 63, 63, &x, &y);
    ASSERT_EQ(CL_MAP_X + 4 * CL_TILE_W, x);
    ASSERT_EQ(CL_MAP_Y + 4 * CL_TILE_H, y);
    PASS();
}

SUITE(unit_map_camera_suite) {
    RUN_TEST(the_hero_is_centred_away_from_the_edges);
    RUN_TEST(the_camera_stops_at_the_top_left);
    RUN_TEST(the_camera_stops_at_the_bottom_right);
    RUN_TEST(one_axis_clamps_alone);
    RUN_TEST(a_map_smaller_than_the_pane_is_centred);
    RUN_TEST(legacy_is_unchanged);
}
