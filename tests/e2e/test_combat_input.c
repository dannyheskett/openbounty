// Combat input contract tests. Exercises the building blocks the
// combat loop relies on (give-up confirm prompt, view stack, controls
// cycling) without spinning up a real raylib window. The full
// input-dispatch path lives behind harness_key_pressed, which the
// scenario harness covers end-to-end.

#include "greatest.h"
#include "prompt.h"
#include "views.h"
#include "resources.h"
#include "game.h"
#include "fixtures.h"

#include <string.h>
#include <stdlib.h>

// ---- Give-up confirm prompt ------------------------------------------------
//
// The combat loop opens a yes/no prompt seeded from
// res.banners.combat_give_up_{header,body}. On PROMPT_RESULT_YES it sets
// c.result = 2; NO/CANCEL resumes combat. These tests pin the resource
// text (so future game.json edits don't silently break the give-up flow)
// and the prompt API contract the loop depends on.

TEST give_up_banner_strings_match_expected(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);

    // Header is the "Press 'ESC' to exit" hint.
    ASSERT_STR_EQ("Press ESC to exit",
                  res->banners.combat_give_up_header);

    // Body must contain the expected phrasing. We don't pin exact
    // whitespace because the prompt renderer adds "(y/n)?" itself.
    ASSERT(strstr(res->banners.combat_give_up_body,
                  "Giving up will forfeit your") != NULL);
    ASSERT(strstr(res->banners.combat_give_up_body,
                  "armies and send you back") != NULL);
    ASSERT(strstr(res->banners.combat_give_up_body,
                  "the King") != NULL);

    resources_free(res);
    free(res);
    PASS();
}

TEST give_up_prompt_opens_and_dismisses(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);

    // Sanity: no prompt active at suite start.
    prompt_dismiss();
    ASSERT_FALSEm("test stale: prompt was active before opening",
                  prompt_is_active());

    prompt_yes_no_open(res->banners.combat_give_up_header,
                       res->banners.combat_give_up_body);
    ASSERTm("yes_no_open did not flip prompt active", prompt_is_active());
    ASSERT_STR_EQ("yes_no", prompt_kind_str());

    prompt_dismiss();
    ASSERT_FALSEm("dismiss left prompt active", prompt_is_active());

    resources_free(res);
    free(res);
    PASS();
}

// ---- View stack: Options ↔ Controls swap ----------------------------------
//
// The combat loop watches for O/C while a view is open and calls
// views_set to swap directly. views_set should replace the stack top
// (not push) so the swap is a single-frame transition and ESC dismisses
// to "no view" rather than to the previous menu.

TEST view_swap_replaces_top(void) {
    views_set(VIEW_NONE);     // reset
    ASSERT_EQ(VIEW_NONE, views_active());

    views_set(VIEW_OPTIONS);
    ASSERT_EQ(VIEW_OPTIONS, views_active());

    views_set(VIEW_CONTROLS);
    ASSERT_EQ(VIEW_CONTROLS, views_active());

    views_set(VIEW_OPTIONS);
    ASSERT_EQ(VIEW_OPTIONS, views_active());

    // One dismiss should fully close — confirms we didn't accidentally
    // push a stack of 3 views during the swap.
    views_dismiss();
    ASSERT_EQ(VIEW_NONE, views_active());

    PASS();
}

// ---- Controls row cycling --------------------------------------------------
//
// Combat dispatches number keys 1..N inside VIEW_CONTROLS to
// views_controls_advance, which cycles stats.options[row] within the
// row's declared range (wrap-on-overflow).

TEST controls_advance_cycles_within_range(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    ASSERTm("game.json is missing the Controls schema",
            res->controls.count > 0);

    Game g = {0};
    g.res = res;
    // Drive the values to known state.
    for (int i = 0; i < res->controls.count; i++) g.stats.options[i] = 0;

    // Skip audio rows: views_controls_advance is a no-op on them when
    // audio_is_available() returns false, which is the case in the
    // test binary (no raylib audio context).
    int delay_row = -1, bool_row = -1;
    for (int i = 0; i < res->controls.count; i++) {
        if (views_controls_row_disabled(&g, i)) continue;
        if (delay_row < 0 && res->controls.items[i].range > 2) delay_row = i;
        if (bool_row  < 0 && res->controls.items[i].range == 2) bool_row  = i;
    }
    ASSERTm("no numeric (range>2) controls row in schema", delay_row >= 0);
    ASSERTm("no enabled bool controls row in schema",      bool_row  >= 0);

    int range = res->controls.items[delay_row].range;
    for (int i = 0; i < range; i++) {
        views_controls_advance(&g, delay_row);
    }
    ASSERT_EQm("Controls row did not wrap to zero after a full cycle",
               0, g.stats.options[delay_row]);

    // Bool rows toggle 0 <-> 1.
    g.stats.options[bool_row] = 0;
    views_controls_advance(&g, bool_row);
    ASSERT_EQ(1, g.stats.options[bool_row]);
    views_controls_advance(&g, bool_row);
    ASSERT_EQ(0, g.stats.options[bool_row]);

    resources_free(res);
    free(res);
    PASS();
}

TEST controls_advance_rejects_out_of_range_row(void) {
    Resources *res = fx_load_resources();
    ASSERT(res);
    Game g = {0};
    g.res = res;
    // Sentinel pattern in every byte so a bad mutation is visible.
    memset(g.stats.options, 0xAA, sizeof g.stats.options);
    int before = g.stats.options[0];

    views_controls_advance(&g, -1);
    views_controls_advance(&g, res->controls.count);
    views_controls_advance(&g, 9999);

    ASSERT_EQm("controls_advance mutated state for an out-of-range row",
               before, g.stats.options[0]);

    resources_free(res);
    free(res);
    PASS();
}

SUITE(e2e_combat_input_suite) {
    RUN_TEST(give_up_banner_strings_match_expected);
    RUN_TEST(give_up_prompt_opens_and_dismisses);
    RUN_TEST(view_swap_replaces_top);
    RUN_TEST(controls_advance_cycles_within_range);
    RUN_TEST(controls_advance_rejects_out_of_range_row);
}
