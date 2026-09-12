// The legacy UI, frozen.
//
// Legacy is a finished reproduction of the DOS original: its behaviour IS the
// spec, so every number here is a value that must never move. The modern UI is
// under active development and shares this code, which is why the freeze is
// written down -- a modern change that shifts a legacy pixel fails here rather
// than shipping.
//
// Everything asserted is geometry or pure logic. The test binary links raylib
// but never opens a window, so nothing below may reach a Draw* call; the
// drawing entry points are deliberately absent.

#include "greatest.h"
#include "layout.h"
#include "present.h"
#include "resources.h"
#include "bfont.h"
#include "overlay.h"
#include "ui.h"
#include "prompt.h"
#include "select.h"
#include "textsel.h"
#include <string.h>

static Resources s_res;

static void legacy(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_LEGACY;
    s_res.render.tile_w = 48;  s_res.render.tile_h = 34;
    s_res.render.tiles_w = 5;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 1;
    layout_init((const struct Resources *)&s_res);
}

static void modern(void) {
    memset(&s_res, 0, sizeof s_res);
    s_res.render.mode = RENDER_MODE_MODERN;
    s_res.render.tile_w = 96;  s_res.render.tile_h = 96;
    s_res.render.tiles_w = 7;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 2;
    s_res.render.native_w = 832; s_res.render.native_h = 540;
    layout_init((const struct Resources *)&s_res);
}

// 1. The screen and the four chrome bands.
TEST legacy_screen_and_chrome(void) {
    legacy();
    ASSERT_FALSE(CL_IS_MODERN);
    ASSERT_FALSE(CL_IS_NATIVE);
    ASSERT_EQ(320, CL_SCREEN_W);
    ASSERT_EQ(200, CL_SCREEN_H);
    ASSERT_EQ(1,  CL_UI);
    ASSERT_EQ(16, CL_FRAME_LEFT_W);
    ASSERT_EQ(16, CL_FRAME_RIGHT_W);
    ASSERT_EQ(8,  CL_FRAME_TOP_H);
    ASSERT_EQ(8,  CL_FRAME_BOTTOM_H);
    PASS();
}

// 2. The status band and the bar strip beneath it. The bar's y was the
// literal 17 * ui_scale for the whole history of the port.
TEST legacy_status_and_bar(void) {
    legacy();
    ASSERT_EQ(16,  CL_STATUS_X);
    ASSERT_EQ(8,   CL_STATUS_Y);
    ASSERT_EQ(288, CL_STATUS_W);
    ASSERT_EQ(9,   CL_STATUS_H);
    ASSERT_EQ(17,  CL_BAR_Y);
    ASSERT_EQ(5,   CL_BAR_H);
    PASS();
}

// 3. The map pane and the purse column beside it.
TEST legacy_map_and_sidebar(void) {
    legacy();
    ASSERT_EQ(16,  CL_MAP_X);
    ASSERT_EQ(22,  CL_MAP_Y);
    ASSERT_EQ(240, CL_MAP_W);
    ASSERT_EQ(170, CL_MAP_H);
    ASSERT_EQ(256, CL_SIDEBAR_X);
    ASSERT_EQ(22,  CL_SIDEBAR_Y);
    ASSERT_EQ(48,  CL_SIDEBAR_W);
    ASSERT_EQ(170, CL_SIDEBAR_H);
    ASSERT_EQ(192, CL_BOTTOM_Y);
    ASSERT_EQ(8,   CL_BOTTOM_H);
    PASS();
}

// 4. The content rect. In legacy it is exactly the pane, so both centring
// offsets are zero and it lands on the historic 16,22.
TEST legacy_content_rect(void) {
    legacy();
    ASSERT_EQ(240, CL_CONTENT_W);
    ASSERT_EQ(170, CL_CONTENT_H);
    ASSERT_EQ(16,  CL_CONTENT_X);
    ASSERT_EQ(22,  CL_CONTENT_Y);
    PASS();
}

// 5. The blue bottom panel, including the one-sided 5px margin legacy
// borrows from the left frame so a full 30-character line fits.
TEST legacy_panel_rect(void) {
    legacy();
    ASSERT_EQ(11,  CL_PANEL_X);
    ASSERT_EQ(245, CL_PANEL_W);
    ASSERT_EQ(68,  CL_PANEL_H);
    ASSERT_EQ(124, CL_PANEL_Y);
    ASSERT_EQ(256, CL_PANEL_X + CL_PANEL_W);   // flush with the sidebar
    PASS();
}

// 6. The panel's text budget.
TEST legacy_panel_text_metrics(void) {
    legacy();
    ASSERT_EQ(30, CL_PANEL_COLS);
    ASSERT_EQ(4,  CL_PANEL_PAD_X);
    ASSERT_EQ(240, CL_PANEL_COLS * BFONT_GLYPH_W);
    PASS();
}

// 7. The tile and the viewport it tiles.
TEST legacy_tiles(void) {
    legacy();
    ASSERT_EQ(48, CL_TILE_W);
    ASSERT_EQ(34, CL_TILE_H);
    ASSERT_EQ(5,  CL_MAP_TILES_W);
    ASSERT_EQ(5,  CL_MAP_TILES_H);
    PASS();
}

// 8. Window scale: auto-fit with the old floor and ceiling, no override path.
TEST legacy_scale(void) {
    legacy();
    ASSERT_EQ(2, CL_SCALE);
    ASSERT_EQ(3, present_scale(1280, 720));
    ASSERT_EQ(2, present_scale(320, 200));      // the CL_SCALE_MIN floor
    ASSERT_EQ(5, present_max_scale(4000, 3000));
    PASS();
}

// 9. The render target is the screen, never zoomed, whatever the window --
// and whatever scale a modern pack last selected.
TEST legacy_target_is_never_zoomed(void) {
    legacy();
    int w, h;
    present_set_scale(3);
    present_target_size(1920, 1080, &w, &h);
    ASSERT_EQ(320, w);
    ASSERT_EQ(200, h);
    present_set_scale(1);
    PASS();
}

// 10. The smallest window the pack can be played in.
TEST legacy_min_window(void) {
    legacy();
    int w, h;
    layout_min_window(&w, &h);
    ASSERT_EQ(320, w);
    ASSERT_EQ(200, h);
    PASS();
}

// 11. Legacy geometry is fixed: the window cannot grow the viewport.
TEST legacy_fit_window_is_a_noop(void) {
    legacy();
    ASSERT_FALSE(layout_fit_window(1920, 1080, 1));
    ASSERT_EQ(320, CL_SCREEN_W);
    ASSERT_EQ(200, CL_SCREEN_H);
    ASSERT_EQ(5,   CL_MAP_TILES_W);
    ASSERT_EQ(240, CL_MAP_W);
    PASS();
}

// 12. The bitmap font's cell.
TEST legacy_glyph_metrics(void) {
    legacy();
    ASSERT_FALSE(bfont_is_modern());
    ASSERT_EQ(8, BFONT_GLYPH_W);
    ASSERT_EQ(8, BFONT_GLYPH_H);
    ASSERT_EQ(8, bfont_line_height());
    PASS();
}

// 13. The wrap: whole characters, breaking at the last space, newlines kept.
TEST legacy_wrap_is_by_character_cell(void) {
    legacy();
    const char *p = "The quick brown fox jumps over the lazy dog";
    char line[128];
    ASSERT(bfont_take_line(&p, CL_PANEL_COLS * BFONT_GLYPH_W, line, sizeof line) > 0);
    ASSERT_STR_EQ("The quick brown fox jumps ", line);
    ASSERT(bfont_take_line(&p, CL_PANEL_COLS * BFONT_GLYPH_W, line, sizeof line) > 0);
    ASSERT_STR_EQ("over the lazy dog", line);
    ASSERT_EQ(0, bfont_take_line(&p, CL_PANEL_COLS * BFONT_GLYPH_W, line, sizeof line));
    PASS();
}

// 14. Pagination counts WRAPPED lines at seven rows a page, so the pager and
// the panel never disagree.
TEST legacy_dialog_page_count(void) {
    legacy();
    open_dialog("H", "one");
    ASSERT_EQ(1, overlay_dialog_page_count());
    // Fifteen wrapped lines -> three pages of seven.
    open_dialog("H", "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\nl13\nl14\nl15");
    ASSERT_EQ(3, overlay_dialog_page_count());
    // A single paragraph with no newlines pages on its WRAPPED length, which
    // is the bug this count was written for: 48 five-character words fill six
    // columns a line, so eight lines, so two pages.
    char body[300];
    body[0] = '\0';
    for (int i = 0; i < 48; i++) strcat(body, "aaaa ");
    open_dialog("H", body);
    ASSERT_EQ(2, overlay_dialog_page_count());
    dialog_dismiss();
    PASS();
}

// 15. The pager walks those pages and stops on the last one.
TEST legacy_dialog_paging_walks_and_stops(void) {
    legacy();
    open_dialog("H", "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\nl13\nl14\nl15");
    ASSERT_EQ(0, dialog_page_current());
    ASSERT(dialog_advance());
    ASSERT_EQ(1, dialog_page_current());
    ASSERT(dialog_advance());
    ASSERT_EQ(2, dialog_page_current());
    ASSERT_FALSE(dialog_advance());        // three pages: no fourth
    ASSERT_EQ(2, dialog_page_current());
    dialog_dismiss();
    ASSERT_EQ(0, dialog_page_current());
    ASSERT_FALSE(dialog_is_active());
    PASS();
}

// 16. The prompt state machine: one prompt at a time, opened and dismissed.
TEST legacy_prompt_state(void) {
    legacy();
    ASSERT_FALSE(prompt_is_active());
    ASSERT_STR_EQ("none", prompt_kind_str());
    prompt_yes_no_open("Quit?", "Are you sure");
    ASSERT(prompt_is_active());
    ASSERT_STR_EQ("yes_no", prompt_kind_str());
    ASSERT_STR_EQ("Quit?", prompt_header_text());
    ASSERT_STR_EQ("Are you sure", prompt_body_text());
    prompt_dismiss();
    ASSERT_FALSE(prompt_is_active());
    ASSERT_STR_EQ("none", prompt_kind_str());
    PASS();
}

// 17. The other prompt kinds report themselves the same way.
TEST legacy_prompt_kinds(void) {
    legacy();
    prompt_numeric_open("Slot", "Which one", 5);
    ASSERT_STR_EQ("numeric", prompt_kind_str());
    prompt_dismiss();
    prompt_ab_open("Reward", "A) gold  B) leadership");
    ASSERT_STR_EQ("ab", prompt_kind_str());
    prompt_dismiss();
    prompt_text_input_open("How many", "Enter a number", 3, 100);
    ASSERT_STR_EQ("text", prompt_kind_str());
    ASSERT_EQ(0, prompt_text_input_value());   // nothing typed yet
    prompt_dismiss();
    ASSERT_FALSE(prompt_is_active());
    PASS();
}

// 18. The cursor selector is modern-only: in legacy it reports nothing, so
// every legacy screen keeps its own key handling. Its pure helpers are
// mode-independent and pinned here too.
TEST legacy_selector_is_inert(void) {
    legacy();
    SelList l = { .count = 5, .cursor = 0 };
    int row = -1;
    ASSERT_EQ(SEL_NONE, sel_input(&l, 0, 0, &row));
    ASSERT_EQ(0, l.cursor);                 // untouched
    ASSERT_EQ(-1, row);
    ASSERT_EQ(1, sel_wrap(0, 1, 5));
    ASSERT_EQ(4, sel_wrap(0, -1, 5));
    ASSERT_EQ(0, sel_wrap(4, 1, 5));
    ASSERT_EQ(-1, sel_hotkey_row('Z', 'A', 5));
    PASS();
}

// 19. The letter selector is modern-only: legacy keeps typed entry and the
// window keyboard.
TEST legacy_letter_selector_is_inert(void) {
    legacy();
    TextSel t = { .cursor = 0, .numeric = false };
    char buf[8] = "";
    int len = 0;
    ASSERT_FALSE(textsel_input(&t, buf, &len, (int)sizeof buf, 0, NULL));
    ASSERT_EQ(0, t.cursor);
    ASSERT_EQ(0, len);
    ASSERT_STR_EQ("", buf);
    PASS();
}

// 20. Nothing a modern pack sets survives into legacy. Loading a modern pack
// and then a legacy one must land back on every number above.
TEST legacy_survives_a_modern_pack(void) {
    modern();
    ASSERT_EQ(832, CL_SCREEN_W);
    ASSERT_EQ(2,   CL_UI);
    legacy();
    ASSERT_EQ(320, CL_SCREEN_W);
    ASSERT_EQ(200, CL_SCREEN_H);
    ASSERT_EQ(1,   CL_UI);
    ASSERT_EQ(16,  CL_FRAME_LEFT_W);
    ASSERT_EQ(8,   CL_FRAME_TOP_H);
    ASSERT_EQ(17,  CL_BAR_Y);
    ASSERT_EQ(22,  CL_MAP_Y);
    ASSERT_EQ(240, CL_MAP_W);
    ASSERT_EQ(240, CL_CONTENT_W);
    ASSERT_EQ(11,  CL_PANEL_X);
    ASSERT_EQ(245, CL_PANEL_W);
    ASSERT_EQ(2,   CL_SCALE);
    PASS();
}

SUITE(unit_legacy_freeze_suite) {
    RUN_TEST(legacy_screen_and_chrome);
    RUN_TEST(legacy_status_and_bar);
    RUN_TEST(legacy_map_and_sidebar);
    RUN_TEST(legacy_content_rect);
    RUN_TEST(legacy_panel_rect);
    RUN_TEST(legacy_panel_text_metrics);
    RUN_TEST(legacy_tiles);
    RUN_TEST(legacy_scale);
    RUN_TEST(legacy_target_is_never_zoomed);
    RUN_TEST(legacy_min_window);
    RUN_TEST(legacy_fit_window_is_a_noop);
    RUN_TEST(legacy_glyph_metrics);
    RUN_TEST(legacy_wrap_is_by_character_cell);
    RUN_TEST(legacy_dialog_page_count);
    RUN_TEST(legacy_dialog_paging_walks_and_stops);
    RUN_TEST(legacy_prompt_state);
    RUN_TEST(legacy_prompt_kinds);
    RUN_TEST(legacy_selector_is_inert);
    RUN_TEST(legacy_letter_selector_is_inert);
    RUN_TEST(legacy_survives_a_modern_pack);
}
