// Tests for src/pack_select.c -- the multi-pack selector's decision logic.
//
// pack_select_flow() itself needs a window, so the loop body was factored
// into pack_select_step(), which is pure and testable. These cover the
// cancel paths in particular: a picker the user dismissed must not report a
// selection, or the game boots a pack nobody chose.

#include "greatest.h"
#include "pack_select.h"

static PackSelectInput no_input(void) {
    PackSelectInput in;
    in.close   = false;
    in.cancel  = false;
    in.up      = false;
    in.down    = false;
    in.digit   = -1;
    in.confirm = false;
    return in;
}

// ---------------------------------------------------------------------------
// Cancel paths: closing the window and ESC both quit without selecting.
// ---------------------------------------------------------------------------

TEST close_window_quits_without_selecting(void) {
    PackSelectState st = { 0, false, false };

    // Move the cursor off row 0 first, so a leaked selection would be
    // visible as a specific wrong pack rather than the harmless default.
    PackSelectInput down = no_input();
    down.down = true;
    pack_select_step(&st, &down, 3);
    ASSERT_EQ(1, st.cursor);
    ASSERT(!st.done);

    PackSelectInput close = no_input();
    close.close = true;
    pack_select_step(&st, &close, 3);

    ASSERT(st.done);
    ASSERT(st.quit);  // the regression: quit stayed false and the caller loaded st.cursor
    PASS();
}

TEST escape_quits_without_selecting(void) {
    PackSelectState st = { 0, false, false };
    PackSelectInput esc = no_input();
    esc.cancel = true;
    pack_select_step(&st, &esc, 3);
    ASSERT(st.done);
    ASSERT(st.quit);
    PASS();
}

// A cancel outranks a confirm arriving in the same frame.
TEST close_beats_confirm_in_same_frame(void) {
    PackSelectState st = { 0, false, false };
    PackSelectInput in = no_input();
    in.close   = true;
    in.confirm = true;
    pack_select_step(&st, &in, 3);
    ASSERT(st.done);
    ASSERT(st.quit);
    PASS();
}

// ---------------------------------------------------------------------------
// Confirm path: ends the loop WITH a selection.
// ---------------------------------------------------------------------------

TEST confirm_selects_current_row(void) {
    PackSelectState st = { 0, false, false };

    PackSelectInput up = no_input();
    up.up = true;
    pack_select_step(&st, &up, 3);
    ASSERT_EQ(2, st.cursor);  // wraps backwards off row 0

    PackSelectInput enter = no_input();
    enter.confirm = true;
    pack_select_step(&st, &enter, 3);

    ASSERT(st.done);
    ASSERT(!st.quit);
    ASSERT_EQ(2, st.cursor);
    PASS();
}

// ---------------------------------------------------------------------------
// Cursor movement.
// ---------------------------------------------------------------------------

TEST cursor_wraps_both_directions(void) {
    PackSelectState st = { 0, false, false };
    PackSelectInput down = no_input();
    down.down = true;

    for (int i = 0; i < 4; i++) pack_select_step(&st, &down, 4);
    ASSERT_EQ(0, st.cursor);  // full loop back to the top
    ASSERT(!st.done);

    PackSelectInput up = no_input();
    up.up = true;
    pack_select_step(&st, &up, 4);
    ASSERT_EQ(3, st.cursor);
    PASS();
}

TEST digit_jumps_to_row(void) {
    PackSelectState st = { 0, false, false };
    PackSelectInput d = no_input();
    d.digit = 2;
    pack_select_step(&st, &d, 5);
    ASSERT_EQ(2, st.cursor);
    ASSERT(!st.done);
    PASS();
}

// A digit past the end of the list is ignored rather than selecting a pack
// that isn't there.
TEST digit_past_end_is_ignored(void) {
    PackSelectState st = { 1, false, false };
    PackSelectInput d = no_input();
    d.digit = 7;
    pack_select_step(&st, &d, 3);
    ASSERT_EQ(1, st.cursor);
    PASS();
}

// ---------------------------------------------------------------------------
// Defensive.
// ---------------------------------------------------------------------------

TEST empty_list_is_inert(void) {
    PackSelectState st = { 0, false, false };
    PackSelectInput in = no_input();
    in.down    = true;
    in.confirm = true;
    pack_select_step(&st, &in, 0);
    ASSERT_EQ(0, st.cursor);
    ASSERT(!st.done);
    PASS();
}

TEST null_args_do_not_crash(void) {
    PackSelectState st = { 0, false, false };
    PackSelectInput in = no_input();
    pack_select_step(NULL, &in, 3);
    pack_select_step(&st, NULL, 3);
    ASSERT_EQ(0, st.cursor);
    ASSERT(!st.done);
    PASS();
}

SUITE(unit_pack_select_suite) {
    RUN_TEST(close_window_quits_without_selecting);
    RUN_TEST(escape_quits_without_selecting);
    RUN_TEST(close_beats_confirm_in_same_frame);
    RUN_TEST(confirm_selects_current_row);
    RUN_TEST(cursor_wraps_both_directions);
    RUN_TEST(digit_jumps_to_row);
    RUN_TEST(digit_past_end_is_ignored);
    RUN_TEST(empty_list_is_inert);
    RUN_TEST(null_args_do_not_crash);
}
