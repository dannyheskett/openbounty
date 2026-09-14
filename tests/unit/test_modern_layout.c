// The modern screen and its five named layouts (src/layout.c,
// src/modern/mlayout.c, REQ-430j), for a pack shaped like Rome: 96 px tiles,
// a 7x5 viewport, a fixed 800x532 buffer.
//
// Vertical positions are asserted against the map pane rather than as bare
// numbers, because the pane's top edge follows the status band and the status
// band follows the font -- which the test binary does not load.

#include "greatest.h"
#include "layout.h"
#include "resources.h"
#include "bfont.h"
#include "modern/mlayout.h"
#include "views.h"
#include "modern/gamemenu.h"
#include "prompt.h"
#include "prompt_impl.h"
#include <string.h>

static Resources s_res;

static void rome(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_MODERN;
    s_res.render.tile_w = 96;  s_res.render.tile_h = 96;
    s_res.render.tiles_w = 7;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 1;
    s_res.render.native_w = 800; s_res.render.native_h = 532;
    layout_init((const struct Resources *)&s_res);
}

static int inside(ML_Rect in, ML_Rect out) {
    return in.x >= out.x && in.y >= out.y &&
           in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

static ML_Rect pane(void) {
    ML_Rect r = { CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H };
    return r;
}

// The screen's horizontal spacing is split 3 : 2 : 3: left edge, the band
// between the map pane and the HUD, right edge. 800 = 12 + 672 + 8 + 96 + 12.
TEST spacing_is_three_two_three(void) {
    rome();
    ASSERT_EQ(800, CL_SCREEN_W);
    ASSERT_EQ(12, CL_FRAME_LEFT_W);
    ASSERT_EQ(8,  CL_SIDEBAR_GAP);
    ASSERT_EQ(12, CL_FRAME_RIGHT_W);
    ASSERT_EQ(12, CL_MAP_X);
    ASSERT_EQ(672, CL_MAP_W);
    ASSERT_EQ(692, CL_SIDEBAR_X);
    ASSERT_EQ(96, CL_SIDEBAR_W);
    ASSERT_EQ(CL_SCREEN_W - CL_FRAME_RIGHT_W, CL_SIDEBAR_X + CL_SIDEBAR_W);
    PASS();
}

// The vertical stack mirrors the horizontal one: top edge, status band,
// band, map pane, bottom edge -- the outer edges as thick as the side edges
// and the band under the status line as wide as the band beside the HUD.
// 532 = 12 + 20 + 8 + 480 + 12.
TEST vertical_mirrors_horizontal(void) {
    rome();
    ASSERT_EQ(532, CL_SCREEN_H);
    ASSERT_EQ(CL_FRAME_LEFT_W, CL_FRAME_TOP_H);
    ASSERT_EQ(CL_FRAME_RIGHT_W, CL_FRAME_BOTTOM_H);
    ASSERT_EQ(12, CL_FRAME_TOP_H);
    ASSERT_EQ(20, CL_STATUS_H);
    ASSERT_EQ(CL_SIDEBAR_GAP, CL_BAR_H);
    ASSERT_EQ(8, CL_BAR_H);
    ASSERT_EQ(12, CL_STATUS_Y);
    ASSERT_EQ(32, CL_BAR_Y);
    ASSERT_EQ(40, CL_MAP_Y);
    ASSERT_EQ(CL_SCREEN_H - CL_FRAME_BOTTOM_H, CL_MAP_Y + CL_MAP_H);
    PASS();
}

// A buffer too short to mirror keeps the old frames rather than squeezing the
// status band below a text line.
TEST short_buffer_does_not_mirror(void) {
    rome();
    s_res.render.native_h = 510;
    layout_init((const struct Resources *)&s_res);
    ASSERT_EQ(510, CL_SCREEN_H);
    ASSERT(CL_FRAME_TOP_H < CL_FRAME_LEFT_W);
    ASSERT_EQ(CL_SCREEN_H, CL_FRAME_TOP_H + CL_STATUS_H + CL_BAR_H + CL_MAP_H + CL_FRAME_BOTTOM_H);
    PASS();
}

// Small: six text lines tall, inset from the pane by the screen's spacing.
TEST small_is_inset_by_the_spacing_on_the_bottom(void) {
    rome();
    int S = ml_space();
    ASSERT_EQ(8, S);
    ML_Rect r = ml_small();
    ASSERT_EQ(CL_MAP_X + S, r.x);
    ASSERT_EQ(CL_MAP_W - 2 * S, r.w);
    ASSERT_EQ(ML_SMALL_LINES * BFONT_GLYPH_H + 2 * ML_PAD, r.h);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H - S, r.y + r.h);
    ASSERT(inside(r, pane()));
    PASS();
}

// Large: six by four tiles, centred in the pane.
TEST large_is_six_by_four_tiles_centred(void) {
    rome();
    ML_Rect r = ml_large();
    ASSERT_EQ(576, r.w);
    ASSERT_EQ(384, r.h);
    ASSERT_EQ(CL_MAP_X + 48, r.x);
    ASSERT_EQ(CL_MAP_Y + 48, r.y);
    ASSERT(inside(r, pane()));
    PASS();
}

// Location: the backdrop inset by the spacing at the smallest integer scale
// that covers its width, cropped to it; the text area shares its bottom edge
// and runs to the spacing above the pane's bottom.
TEST location_backdrop_is_integer_3x_inset_and_text_fills_below(void) {
    rome();
    int S = ml_space();
    ASSERT_EQ(3, ml_loc_scale());
    ML_Rect b = ml_loc_backdrop();
    ML_Rect t = ml_loc_text();
    ASSERT_EQ(CL_MAP_X + S, b.x);
    ASSERT_EQ(CL_MAP_Y + S, b.y);
    ASSERT_EQ(656, b.w);
    ASSERT_EQ(306, b.h);            // 102 x 3
    ASSERT_EQ(b.y + b.h, t.y);      // one shared edge
    ASSERT_EQ(b.x, t.x);
    ASSERT_EQ(656, t.w);
    ASSERT_EQ(CL_MAP_Y + CL_MAP_H - S, t.y + t.h);
    ASSERT_EQ(158, t.h);          // 480 - 8 - 306 - 8
    ASSERT(inside(b, pane()));
    ASSERT(inside(t, pane()));
    PASS();
}

// Full screen: the pane, the band and the HUD edge to edge, never the status
// band above.
TEST full_covers_pane_band_and_hud_not_the_status_band(void) {
    rome();
    ML_Rect r = ml_full();
    ASSERT_EQ(CL_MAP_X, r.x);
    ASSERT_EQ(CL_MAP_Y, r.y);
    ASSERT_EQ(776, r.w);            // 672 + 8 + 96
    ASSERT_EQ(480, r.h);
    ASSERT_EQ(CL_SIDEBAR_X + CL_SIDEBAR_W, r.x + r.w);
    ASSERT(r.y >= CL_STATUS_Y + CL_STATUS_H);
    ASSERT_EQ(CL_SCREEN_W - CL_FRAME_RIGHT_W, r.x + r.w);
    PASS();
}

// Capacity is measured, not declared: it follows the loaded font. Rome's
// 16 px face and 18 px lines, stated once so a layout change that starves a
// panel of text shows up here: small 40 x 6, large 35 x 20, location 40 x 7,
// full 47 x 25.
TEST capacity_follows_the_glyph(void) {
    rome();
    ML_Rect s = ml_small(), l = ml_large(), t = ml_loc_text(), f = ml_full();
    ASSERT_EQ((s.w - 2 * ML_PAD) / BFONT_GLYPH_W, ml_cols(s));
    ASSERT_EQ((f.h - 2 * ML_PAD) / BFONT_GLYPH_H, ml_lines(f));
    int rome_small_h = ML_SMALL_LINES * 18 + 2 * ML_PAD;
    ASSERT_EQ(40, (s.w - 2 * ML_PAD) / 16);  ASSERT_EQ(6,  (rome_small_h - 2 * ML_PAD) / 18);
    ASSERT_EQ(35, (l.w - 2 * ML_PAD) / 16);  ASSERT_EQ(20, (l.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(40, (t.w - 2 * ML_PAD) / 16);  ASSERT_EQ(7,  (t.h - 2 * ML_PAD) / 18);
    ASSERT_EQ(47, (f.w - 2 * ML_PAD) / 16);  ASSERT_EQ(25, (f.h - 2 * ML_PAD) / 18);
    PASS();
}


// The modern game menu (src/modern/gamemenu.c): drill-down pages. The top
// level is Hero, World, Game and Back; Debug is first on the Game page and only
// with --debug; Exit is always last; a row that does not apply is greyed with
// its reason as the description.
static void menu_page(bool debug, bool troops, GmPageId id, GmPage *p) {
    rome();
    strcpy(s_res.ui.gm_debug, "Debug");
    strcpy(s_res.ui.gm_exit, "Exit");
    strcpy(s_res.ui.gm_army, "Army");
    strcpy(s_res.banners.gmr_no_troops, "No troops.");
    resources_republish(&s_res);
    static Game g;
    memset(&g, 0, sizeof g);
    g.res = &s_res;
    if (troops) { strcpy(g.army[0].id, "militia"); g.army[0].count = 5; }
    modern_gamemenu_open(debug);
    modern_gamemenu_page(&g, id, p);
    resources_republish(NULL);
}

TEST debug_row_only_with_debug_flag(void) {
    GmPage p;
    menu_page(false, true, GM_PAGE_GAME, &p);
    for (int i = 0; i < p.n; i++) ASSERT(strcmp(p.item[i].label, "Debug") != 0);
    menu_page(true, true, GM_PAGE_GAME, &p);
    ASSERT_STR_EQ("Debug", p.item[0].label);             // first
    ASSERT_STR_EQ("Exit", p.item[p.n - 1].label);        // Exit last
    PASS();
}

TEST menu_pages_drill_down(void) {
    GmPage p;
    menu_page(false, true, GM_PAGE_ROOT, &p);
    ASSERT_EQ(4, p.n);                                   // Hero, World, Game, Back
    ASSERT_EQ(GM_ACT_PAGE + GM_PAGE_HERO, p.item[0].key);
    ASSERT_EQ(GM_ACT_BACK, p.item[3].key);
    menu_page(false, true, GM_PAGE_HERO, &p);
    ASSERT_STR_EQ("Army", p.item[0].label);
    ASSERT_EQ(GM_ACT_BACK, p.item[p.n - 1].key);          // Back last
    ASSERT(p.item[4].enabled);                            // troops: Dismiss applies
    menu_page(false, false, GM_PAGE_HERO, &p);
    ASSERT_FALSE(p.item[4].enabled);                      // none: greyed, with why
    ASSERT_STR_EQ("No troops.", p.item[4].desc);
    menu_page(false, true, GM_PAGE_SAVE, &p);
    ASSERT_EQ(11, p.n);                                   // ten slots and Back
    PASS();
}

// Numeric and A/B prompts answer by rows: the body's own choice lines become
// the rows (a line without a prefix continues the choice above it), and the
// rest of the body is the lead text.
TEST prompt_choice_lines_become_rows(void) {
    prompt_ab_open("", "You may:\nA) Take the gold.\nB) Give it to the\npeasants.");
    const PromptView *v = prompt_view();
    ASSERT_STR_EQ("You may:\n", v->lead);
    ASSERT_EQ(2, v->choice_n);
    ASSERT_STR_EQ("Take the gold.", v->choices[0]);
    ASSERT_STR_EQ("Give it to the peasants.", v->choices[1]);
    prompt_dismiss();

    prompt_numeric_open("Go to which continent?", "1. Italia\n2. Gallia\n", 2);
    v = prompt_view();
    ASSERT_EQ(2, v->choice_n);
    ASSERT_STR_EQ("Gallia", v->choices[1]);
    prompt_dismiss();

    // No choice lines: the answers are the rows.
    prompt_numeric_open("", "Which?", 3);
    v = prompt_view();
    ASSERT_EQ(3, v->choice_n);
    ASSERT_STR_EQ("3", v->choices[2]);
    prompt_dismiss();
    PASS();
}

SUITE(unit_modern_layout_suite) {
    RUN_TEST(spacing_is_three_two_three);
    RUN_TEST(vertical_mirrors_horizontal);
    RUN_TEST(short_buffer_does_not_mirror);
    RUN_TEST(small_is_inset_by_the_spacing_on_the_bottom);
    RUN_TEST(large_is_six_by_four_tiles_centred);
    RUN_TEST(location_backdrop_is_integer_3x_inset_and_text_fills_below);
    RUN_TEST(full_covers_pane_band_and_hud_not_the_status_band);
    RUN_TEST(capacity_follows_the_glyph);
    RUN_TEST(debug_row_only_with_debug_flag);
    RUN_TEST(menu_pages_drill_down);
    RUN_TEST(prompt_choice_lines_become_rows);
}
