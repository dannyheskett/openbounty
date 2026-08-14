// Animation cycle lengths come from the pack, not from a compiled-in 4.
//
// Two things are guarded here. First, that a pack CAN declare any cycle
// length up to OB_ANIM_FRAMES_MAX and the parsed count reflects it. Second,
// and more important, that kings-bounty is completely unaffected: every one
// of its animations declares exactly four frames, so it keeps the four-frame
// cycle it has always had.

#include "greatest.h"
#include "tables.h"
#include "resources.h"
#include "pack.h"
#include "fixtures.h"

#include <stdlib.h>
#include <string.h>

#define ANIM_FIXTURE_DIR "tests/fixtures/animpack"

// resources_load resolves its manifest through the pack stack, so the fixture
// has to be mounted as a pack rather than handed over as a path -- otherwise
// the read falls through to whichever pack is already on the stack and the
// test silently asserts against kings-bounty's manifest instead.
//
// The pack is opened fresh per test and never cached: pack_stack_pop() takes
// ownership and closes what it pops, so a cached handle would dangle after
// the first teardown. Pushing on top of the suite's kings-bounty pack leaves
// that one in place underneath for the tests that want it.
static Resources *load_anim_fixture(void) {
    Pack *p = pack_open(ANIM_FIXTURE_DIR);
    if (!p) return NULL;
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    if (!r || !resources_load(r, "game.json")) {
        free(r); pack_stack_pop(); return NULL;
    }
    return r;
}

static void free_anim_fixture(Resources *r) {
    resources_free(r); free(r);
    pack_stack_pop();   // closes the fixture pack
}

// ---- Pack-declared cycles --------------------------------------------------

TEST hero_cycles_come_from_the_pack(void) {
    Resources *r = load_anim_fixture();
    ASSERT(r);
    // Six walk frames and two boat frames, from one manifest. These used to
    // share a single out-count, so the walk value was overwritten by the boat
    // value and both were discarded.
    ASSERT_EQ(6, r->sprites.hero_walk_count);
    ASSERT_EQ(2, r->sprites.hero_boat_count);
    ASSERT_STR_EQ("h_05.png", r->sprites.hero_walk[5]);
    ASSERT_STR_EQ("b_01.png", r->sprites.hero_boat[1]);
    free_anim_fixture(r);
    PASS();
}

TEST hud_cycle_exceeds_four_and_absent_is_zero(void) {
    Resources *r = load_anim_fixture();
    ASSERT(r);
    ASSERT_EQ(8, r->sprites.hud_siege_animation_count);
    ASSERT_STR_EQ("s_07.png", r->sprites.hud_siege_animation[7]);
    // The fixture declares no magic animation at all. Zero means "nothing
    // declared", which is what makes the consumer fall back to a still image.
    ASSERT_EQ(0, r->sprites.hud_magic_animation_count);
    free_anim_fixture(r);
    PASS();
}

TEST troop_anim_skips_non_strings_without_leaving_a_hole(void) {
    Resources *r = load_anim_fixture();
    ASSERT(r);
    const TroopDef *t = troop_by_id("grunt");
    ASSERT(t);
    // The manifest array has 8 entries, one of which is the number 7. The
    // old hand-rolled loop advanced its index outside the type check, so it
    // both capped at 4 and left slot 1 empty. Seven contiguous frames now.
    ASSERT_EQ(7, t->anim_count);
    for (int i = 0; i < t->anim_count; i++)
        ASSERT(t->anim[i][0] != '\0');
    ASSERT_STR_EQ("g_00.png", t->anim[0]);
    ASSERT_STR_EQ("g_01.png", t->anim[1]);
    ASSERT_STR_EQ("g_06.png", t->anim[6]);
    free_anim_fixture(r);
    PASS();
}

TEST villain_anim_is_declarable_like_a_troop(void) {
    Resources *r = load_anim_fixture();
    ASSERT(r);
    const VillainDef *v = villain_by_id("boss");
    ASSERT(v);
    ASSERT_EQ(5, v->anim_count);
    ASSERT_STR_EQ("p_04.png", v->anim[4]);
    free_anim_fixture(r);
    PASS();
}

// ---- kings-bounty is unchanged ---------------------------------------------

TEST kings_bounty_keeps_four_frame_cycles(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    ASSERT_EQ(OB_ANIM_FRAMES_DEFAULT, r->sprites.hero_walk_count);
    ASSERT_EQ(OB_ANIM_FRAMES_DEFAULT, r->sprites.hero_boat_count);
    ASSERT_EQ(OB_ANIM_FRAMES_DEFAULT, r->sprites.hud_siege_animation_count);
    ASSERT_EQ(OB_ANIM_FRAMES_DEFAULT, r->sprites.hud_magic_animation_count);
    resources_free(r); free(r);
    PASS();
}

TEST kings_bounty_troops_all_declare_four_frames(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    int n = troops_count();
    ASSERT(n > 0);
    for (int i = 0; i < n; i++) {
        const TroopDef *t = troop_by_index(i);
        ASSERT(t);
        ASSERT_EQ(OB_ANIM_FRAMES_DEFAULT, t->anim_count);
    }
    resources_free(r); free(r);
    PASS();
}

TEST kings_bounty_villains_use_the_stem_fallback(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    // kings-bounty declares no villain anim arrays -- its frames are found by
    // deriving <portrait-stem>_NN siblings. Count 0 is what selects that path,
    // so this asserts the fallback is still the one being taken.
    int n = villains_count();
    ASSERT(n > 0);
    for (int i = 0; i < n; i++) {
        const VillainDef *v = villain_by_index(i);
        ASSERT(v);
        ASSERT_EQ(0, v->anim_count);
    }
    resources_free(r); free(r);
    PASS();
}

// ---- The storage ceiling holds ---------------------------------------------

TEST tick_wrap_divides_every_supported_cycle(void) {
    // Counters wrap at OB_ANIM_TICK_WRAP. If any cycle length from 1 to
    // OB_ANIM_FRAMES_MAX failed to divide it, that cycle would skip frames at
    // the rollover -- a glitch appearing once every few hours of play.
    for (int n = 1; n <= OB_ANIM_FRAMES_MAX; n++)
        ASSERT_EQ_FMT(0, OB_ANIM_TICK_WRAP % n, "%d");
    PASS();
}

SUITE(unit_anim_frames_suite) {
    RUN_TEST(hero_cycles_come_from_the_pack);
    RUN_TEST(hud_cycle_exceeds_four_and_absent_is_zero);
    RUN_TEST(troop_anim_skips_non_strings_without_leaving_a_hole);
    RUN_TEST(villain_anim_is_declarable_like_a_troop);
    RUN_TEST(kings_bounty_keeps_four_frame_cycles);
    RUN_TEST(kings_bounty_troops_all_declare_four_frames);
    RUN_TEST(kings_bounty_villains_use_the_stem_fallback);
    RUN_TEST(tick_wrap_divides_every_supported_cycle);
}
