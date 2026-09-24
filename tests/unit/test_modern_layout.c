// The modern game menu's pages and the prompt's answer rows (src/modern/
// gamemenu.c, src/prompt.c), for a pack shaped like Rome. The screen's
// geometry is tests/unit/test_layout.c's.

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
    s_res.render.tiles_w = 5;  s_res.render.tiles_h = 5;
    s_res.render.ui_scale = 1;
    s_res.render.native_w = 800; s_res.render.native_h = 504;
    layout_init((const struct Resources *)&s_res);
}

// The modern game menu (src/modern/gamemenu.c): drill-down pages. The top
// level is Hero, World, Game, Close and -- on its foot, and nowhere else --
// Exit; every other page ends with Back; Debug is first on the Game page and
// only with --debug; a row that does not apply is greyed with its reason as
// the description.
static void menu_page(bool debug, bool troops, GmPageId id, GmPage *p) {
    rome();
    strcpy(s_res.ui.gm_debug, "Debug");
    strcpy(s_res.ui.gm_exit, "Exit");
    strcpy(s_res.ui.gm_close, "Close");
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
    ASSERT_EQ(GM_ACT_BACK, p.item[p.n - 1].key);          // Back last
    for (int i = 0; i < p.n; i++) ASSERT(strcmp(p.item[i].label, "Exit") != 0);   // Exit is not here
    PASS();
}

TEST menu_pages_drill_down(void) {
    GmPage p;
    menu_page(false, true, GM_PAGE_ROOT, &p);
    ASSERT_EQ(5, p.n);                                   // Hero, World, Game, Close, Exit
    ASSERT_EQ(GM_ACT_PAGE + GM_PAGE_HERO, p.item[0].key);
    ASSERT_STR_EQ("Close", p.item[3].label);
    ASSERT_EQ(GM_ACT_BACK, p.item[3].key);               // Close closes the menu
    ASSERT_STR_EQ("Exit", p.item[4].label);              // Exit last ...
    ASSERT_EQ(2, p.foot);                                // ... on the page's foot, with Close
    menu_page(false, true, GM_PAGE_HERO, &p);
    ASSERT_STR_EQ("Army", p.item[0].label);
    ASSERT_EQ(GM_ACT_BACK, p.item[p.n - 1].key);          // Back last
    ASSERT(p.item[4].enabled);                            // troops: Dismiss applies
    menu_page(false, false, GM_PAGE_HERO, &p);
    ASSERT_FALSE(p.item[4].enabled);                      // none: greyed, with why
    ASSERT_STR_EQ("No troops.", p.item[4].desc);
    menu_page(false, true, GM_PAGE_SAVE, &p);
    ASSERT_EQ(6, p.n);                                    // five slots and Back
    PASS();
}

// Numeric and A/B prompts answer by rows: the rows the opener names, and the
// words it names with them (prompt_set_choices, prompt_set_lead). Nothing is
// read out of the body: without named rows the answers themselves are the
// rows and the body is the words.
TEST prompt_rows_are_named_not_parsed(void) {
    prompt_ab_open("", "You may:\nA) Take the gold.\nB) Give it away.");
    const PromptView *v = prompt_view();
    ASSERT_EQ(2, v->choice_n);
    ASSERT_STR_EQ("A", v->choices[0]);                    // the body is not parsed
    ASSERT_STR_EQ("You may:\nA) Take the gold.\nB) Give it away.", v->lead);
    const char *labels[2] = { "Take the gold.", "Give it away." };
    const int values[2] = { 1, 2 };
    prompt_set_choices(labels, values, 2);
    prompt_set_lead("You may:");
    v = prompt_view();
    ASSERT_STR_EQ("Give it away.", v->choices[1]);
    ASSERT_STR_EQ("You may:", v->lead);
    prompt_dismiss();

    prompt_numeric_open("", "Which?", 3);
    v = prompt_view();
    ASSERT_EQ(3, v->choice_n);
    ASSERT_STR_EQ("3", v->choices[2]);
    prompt_dismiss();
    PASS();
}

SUITE(unit_modern_layout_suite) {
    RUN_TEST(debug_row_only_with_debug_flag);
    RUN_TEST(menu_pages_drill_down);
    RUN_TEST(prompt_rows_are_named_not_parsed);
}
