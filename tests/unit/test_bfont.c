// Text wrapping and the metrics the layout reads, on both routes.
//
// Legacy: bfont_take_line wraps by max_w / 8 characters and keeps every
// newline, which is the word-wrap the dialog and prompt panels carried as
// private copies. Modern: text_take_line wraps by the face's real advances,
// treats a single newline as a space and a blank line as a paragraph break.

#include "greatest.h"
#include "bfont.h"
#include "text.h"
#include "layout.h"
#include "resources.h"
#include <string.h>

static void legacy_layout(void) {
    Resources r;
    memset(&r, 0, sizeof r);
    r.render.mode = RENDER_MODE_LEGACY;
    r.render.tile_w = 48; r.render.tile_h = 34; r.render.tiles_w = 5; r.render.tiles_h = 5; r.render.ui_scale = 1;
    layout_init((const struct Resources *)&r);
}

TEST legacy_wrap_is_thirty_columns_breaking_at_spaces(void) {
    legacy_layout();
    ASSERT_EQ(8, BFONT_GLYPH_W);
    const char *p = "The quick brown fox jumps over the lazy dog and keeps running";
    char line[128];
    // The old wrap backs up to the last space whenever it fills all 30
    // cells, even when the next character is itself a space: 25, not 30.
    ASSERT(bfont_take_line(&p, 30 * 8, line, sizeof line) > 0);
    // ... and it leaves that space on the end of the line.
    ASSERT_STR_EQ("The quick brown fox jumps ", line);
    ASSERT(bfont_take_line(&p, 30 * 8, line, sizeof line) > 0);
    ASSERT_STR_EQ("over the lazy dog and keeps ", line);
    ASSERT(bfont_take_line(&p, 30 * 8, line, sizeof line) > 0);
    ASSERT_STR_EQ("running", line);
    ASSERT_EQ(0, bfont_take_line(&p, 30 * 8, line, sizeof line));
    PASS();
}

TEST legacy_wrap_keeps_every_newline(void) {
    legacy_layout();
    const char *p = "one\ntwo\n\nfour";
    char line[64];
    bfont_take_line(&p, 240, line, sizeof line); ASSERT_STR_EQ("one", line);
    bfont_take_line(&p, 240, line, sizeof line); ASSERT_STR_EQ("two", line);
    bfont_take_line(&p, 240, line, sizeof line); ASSERT_STR_EQ("", line);      // the blank line stays
    bfont_take_line(&p, 240, line, sizeof line); ASSERT_STR_EQ("four", line);
    PASS();
}

TEST legacy_metrics_are_the_old_literals(void) {
    legacy_layout();
    ASSERT_EQ(9, CL_STATUS_H);
    ASSERT_EQ(68, CL_PANEL_H);
    ASSERT_EQ(8, bfont_line_height());
    PASS();
}

TEST modern_wrap_uses_the_face_and_keeps_newlines(void) {
    ASSERT(text_preload_file("assets/glory-of-rome/art/font/SpaceMono-Bold.ttf", 20, 0));
    ASSERT(text_line_h() >= 20);
    ASSERT(text_digit_w() > 0);
    // A width that holds either line but not "THE QUICK BROWN"
    int w_a = text_width("The quick"), w_b = text_width("brown fox");
    int max_w = (w_a > w_b ? w_a : w_b) + 2;
    ASSERT(text_width("The quick brown") > max_w);
    const char *p = "The quick brown fox\n\nSecond paragraph";
    char line[64];
    ASSERT(text_take_line(&p, max_w, line, sizeof line) > 0);
    ASSERT_STR_EQ("The quick", line);            // wrapped by the face's cell width
    ASSERT(text_take_line(&p, max_w, line, sizeof line) > 0);
    ASSERT_STR_EQ("brown fox", line);            // then the authored line break
    ASSERT(text_take_line(&p, 10000, line, sizeof line) > 0);
    ASSERT_STR_EQ("", line);                     // the blank line stays a blank line
    ASSERT(text_take_line(&p, 10000, line, sizeof line) > 0);
    ASSERT_STR_EQ("Second paragraph", line);
    ASSERT_EQ(0, text_take_line(&p, 10000, line, sizeof line));
    text_shutdown();
    PASS();
}

SUITE(unit_bfont_suite) {
    RUN_TEST(legacy_wrap_is_thirty_columns_breaking_at_spaces);
    RUN_TEST(legacy_wrap_keeps_every_newline);
    RUN_TEST(legacy_metrics_are_the_old_literals);
    RUN_TEST(modern_wrap_uses_the_face_and_keeps_newlines);
}
