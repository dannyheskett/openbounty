// Hero facing: derived from movement, and carried through a save.
//
// Movement is 8-way but there are only four facings, so every step has to
// resolve to one of them. Horizontal wins on a diagonal, which keeps facing
// agreeing with facing_left, the flag that still drives the mirror for packs
// that ship a single sprite strip.

#include "greatest.h"
#include "game.h"
#include "map.h"
#include "fog.h"
#include "savegame.h"
#include "fixtures.h"
#include "sprites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FACING_SAVE_PATH "build/test_facing.sav"

// A hero standing in the middle of an empty grass map, so a step is never
// rejected by terrain. calloc zeroes tiles to TERRAIN_GRASS / INTERACT_NONE
// and travel_mode to TRAVEL_WALK, the same zero-asset trick test_foe_follow
// uses. Using the real GameStep keeps the tie-break under test the shipping one.
typedef struct { Resources *res; Game *g; Map *m; Fog *f; } Field;

static bool field_open(Field *fd) {
    fd->res = fx_load_resources();
    fd->g = calloc(1, sizeof *fd->g);
    fd->m = calloc(1, sizeof *fd->m);
    fd->f = calloc(1, sizeof *fd->f);
    if (!fd->res || !fd->g || !fd->m || !fd->f) return false;
    // Init through the fixture so day/step accounting is populated: a bare
    // calloc'd Game has zero steps left, so the first step would roll the day
    // over and end_day would walk state this stripped-down world lacks.
    fx_init_game(fd->g, fd->res, FIXTURE_SEED);
    fd->m->width = 64; fd->m->height = 64;
    strcpy(fd->g->position.zone, "continentia");
    fd->g->position.x = 32; fd->g->position.y = 32;
    return true;
}

static void field_close(Field *fd) {
    free(fd->f); free(fd->m); free(fd->g);
    if (fd->res) { resources_free(fd->res); free(fd->res); }
}

static int step_facing(int dx, int dy) {
    Field fd;
    if (!field_open(&fd)) { field_close(&fd); return -1; }
    GameStep(fd.g, fd.m, fd.f, fd.res, dx, dy);
    int facing = fd.g->position.facing;
    field_close(&fd);
    return facing;
}

TEST cardinal_steps_set_their_own_facing(void) {
    ASSERT_EQ(OB_FACE_EAST,  step_facing( 1,  0));
    ASSERT_EQ(OB_FACE_WEST,  step_facing(-1,  0));
    ASSERT_EQ(OB_FACE_SOUTH, step_facing( 0,  1));
    ASSERT_EQ(OB_FACE_NORTH, step_facing( 0, -1));
    PASS();
}

TEST diagonal_steps_resolve_horizontally(void) {
    // All four diagonals must land on the horizontal facing, so that facing
    // and facing_left can never disagree about which way the hero looks.
    ASSERT_EQ(OB_FACE_EAST, step_facing( 1, -1));
    ASSERT_EQ(OB_FACE_EAST, step_facing( 1,  1));
    ASSERT_EQ(OB_FACE_WEST, step_facing(-1, -1));
    ASSERT_EQ(OB_FACE_WEST, step_facing(-1,  1));
    PASS();
}

TEST facing_and_facing_left_stay_consistent(void) {
    Field fd;
    ASSERT(field_open(&fd));

    GameStep(fd.g, fd.m, fd.f, fd.res, -1, 0);
    ASSERT(fd.g->position.facing_left);
    ASSERT_EQ(OB_FACE_WEST, fd.g->position.facing);

    GameStep(fd.g, fd.m, fd.f, fd.res, 1, 0);
    ASSERT_FALSE(fd.g->position.facing_left);
    ASSERT_EQ(OB_FACE_EAST, fd.g->position.facing);

    // A vertical step leaves the mirror flag alone (there is nothing to
    // mirror) but does move the facing.
    bool before = fd.g->position.facing_left;
    GameStep(fd.g, fd.m, fd.f, fd.res, 0, -1);
    ASSERT_EQ(before, fd.g->position.facing_left);
    ASSERT_EQ(OB_FACE_NORTH, fd.g->position.facing);

    field_close(&fd);
    PASS();
}

TEST facing_survives_a_save_round_trip(void) {
    Resources *res1; Game *g1; Map *m1; Fog *f1;
    ASSERT(fx_init_game_full(&res1, &g1, &m1, &f1, NULL, FIXTURE_SEED));
    g1->position.facing = OB_FACE_NORTH;
    ASSERT_EQ(SAVE_OK, SaveGameWrite(FACING_SAVE_PATH, g1, m1, f1));
    fx_free_game_full(res1, g1, m1, f1);

    Resources *res2; Game *g2; Map *m2; Fog *f2;
    ASSERT(fx_init_game_full(&res2, &g2, &m2, &f2, NULL, FIXTURE_SEED));
    ASSERT_EQ(SAVE_OK, SaveGameRead(FACING_SAVE_PATH, g2, m2, f2));
    ASSERT_EQ(OB_FACE_NORTH, g2->position.facing);
    fx_free_game_full(res2, g2, m2, f2);

    remove(FACING_SAVE_PATH);
    PASS();
}

TEST old_saves_derive_facing_from_the_mirror_flag(void) {
    // Saves written before facings existed carry facing_left and no facing.
    // Rather than bump SAVE_VERSION and orphan them, the loader derives one
    // from the other. Simulated by stripping the key back out of a real save.
    Resources *res1; Game *g1; Map *m1; Fog *f1;
    ASSERT(fx_init_game_full(&res1, &g1, &m1, &f1, NULL, FIXTURE_SEED));
    g1->position.facing_left = true;
    g1->position.facing = OB_FACE_NORTH;   // deliberately NOT west
    ASSERT_EQ(SAVE_OK, SaveGameWrite(FACING_SAVE_PATH, g1, m1, f1));
    fx_free_game_full(res1, g1, m1, f1);

    // Rewrite the key name so the loader cannot see it, leaving the file
    // otherwise byte-identical and the same length.
    FILE *fp = fopen(FACING_SAVE_PATH, "r+b");
    ASSERT(fp);
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    ASSERT(len > 0);
    char *buf = malloc((size_t)len + 1);
    ASSERT(buf);
    fseek(fp, 0, SEEK_SET);
    ASSERT_EQ((size_t)len, fread(buf, 1, (size_t)len, fp));
    buf[len] = '\0';
    char *key = strstr(buf, "\"facing\"");
    ASSERT(key);
    memcpy(key, "\"xacing\"", 8);
    fseek(fp, 0, SEEK_SET);
    ASSERT_EQ((size_t)len, fwrite(buf, 1, (size_t)len, fp));
    fclose(fp);
    free(buf);

    Resources *res2; Game *g2; Map *m2; Fog *f2;
    ASSERT(fx_init_game_full(&res2, &g2, &m2, &f2, NULL, FIXTURE_SEED));
    ASSERT_EQ(SAVE_OK, SaveGameRead(FACING_SAVE_PATH, g2, m2, f2));
    // facing_left was true, so the derived facing is west, not the north the
    // save happened to be holding before the key was hidden.
    ASSERT(g2->position.facing_left);
    ASSERT_EQ(OB_FACE_WEST, g2->position.facing);
    fx_free_game_full(res2, g2, m2, f2);

    remove(FACING_SAVE_PATH);
    PASS();
}

// ---- Renderer selection ----------------------------------------------------
//
// sprites_anim_tex is a pure function over SpriteAnim, so it can be driven
// with synthetic texture ids and no GPU. id is encoded facing*100 + frame.

static void fill_anim(SpriteAnim *a, bool directional, const int *counts) {
    memset(a, 0, sizeof *a);
    a->directional = directional;
    for (int f = 0; f < OB_FACE_COUNT; f++) {
        a->frames[f] = counts[f];
        for (int i = 0; i < counts[f]; i++)
            a->tex[f][i].id = (unsigned)(f * 100 + i + 1);
    }
}

TEST single_strip_mirrors_west_and_never_turns(void) {
    SpriteAnim a;
    const int counts[OB_FACE_COUNT] = { 4, 0, 0, 0 };
    fill_anim(&a, false, counts);

    bool mirror = true;
    // Every facing draws the one strip; only west asks for the flip. This is
    // exactly what kings-bounty did before facings existed.
    ASSERT_EQ(1u, sprites_anim_tex(&a, OB_FACE_SOUTH, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    ASSERT_EQ(1u, sprites_anim_tex(&a, OB_FACE_NORTH, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    ASSERT_EQ(1u, sprites_anim_tex(&a, OB_FACE_EAST, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    ASSERT_EQ(1u, sprites_anim_tex(&a, OB_FACE_WEST, 0, &mirror).id);
    ASSERT(mirror);
    PASS();
}

TEST directional_selects_its_facing_and_never_mirrors(void) {
    SpriteAnim a;
    const int counts[OB_FACE_COUNT] = { 4, 4, 4, 4 };
    fill_anim(&a, true, counts);

    bool mirror = true;
    ASSERT_EQ(1u,   sprites_anim_tex(&a, OB_FACE_SOUTH, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    ASSERT_EQ(101u, sprites_anim_tex(&a, OB_FACE_EAST, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    // West is authored, so it is drawn as authored rather than flipped from
    // east -- the whole point of declaring four facings.
    ASSERT_EQ(201u, sprites_anim_tex(&a, OB_FACE_WEST, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    ASSERT_EQ(301u, sprites_anim_tex(&a, OB_FACE_NORTH, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    PASS();
}

TEST directional_falls_back_to_south_for_a_missing_facing(void) {
    SpriteAnim a;
    const int counts[OB_FACE_COUNT] = { 4, 4, 4, 0 };   // no north
    fill_anim(&a, true, counts);

    bool mirror = true;
    // Walking north must draw something rather than nothing.
    ASSERT_EQ(1u, sprites_anim_tex(&a, OB_FACE_NORTH, 0, &mirror).id);
    ASSERT_FALSE(mirror);
    PASS();
}

TEST each_facing_folds_the_tick_onto_its_own_cycle(void) {
    SpriteAnim a;
    const int counts[OB_FACE_COUNT] = { 2, 6, 4, 4 };   // uneven on purpose
    fill_anim(&a, true, counts);

    // Tick 7: south has 2 frames (7 % 2 = 1), east has 6 (7 % 6 = 1).
    ASSERT_EQ(2u,   sprites_anim_tex(&a, OB_FACE_SOUTH, 7, NULL).id);
    ASSERT_EQ(102u, sprites_anim_tex(&a, OB_FACE_EAST, 7, NULL).id);
    // Tick 4: south wraps to 0, east is still mid-cycle at 4.
    ASSERT_EQ(1u,   sprites_anim_tex(&a, OB_FACE_SOUTH, 4, NULL).id);
    ASSERT_EQ(105u, sprites_anim_tex(&a, OB_FACE_EAST, 4, NULL).id);
    PASS();
}

TEST anim_present_reports_whether_anything_was_declared(void) {
    SpriteAnim a;
    const int none[OB_FACE_COUNT] = { 0, 0, 0, 0 };
    const int some[OB_FACE_COUNT] = { 0, 0, 3, 0 };
    fill_anim(&a, true, none);
    ASSERT_FALSE(sprites_anim_present(&a));
    fill_anim(&a, true, some);
    ASSERT(sprites_anim_present(&a));
    PASS();
}

SUITE(unit_facing_suite) {
    RUN_TEST(cardinal_steps_set_their_own_facing);
    RUN_TEST(diagonal_steps_resolve_horizontally);
    RUN_TEST(facing_and_facing_left_stay_consistent);
    RUN_TEST(facing_survives_a_save_round_trip);
    RUN_TEST(old_saves_derive_facing_from_the_mirror_flag);
    RUN_TEST(single_strip_mirrors_west_and_never_turns);
    RUN_TEST(directional_selects_its_facing_and_never_mirrors);
    RUN_TEST(directional_falls_back_to_south_for_a_missing_facing);
    RUN_TEST(each_facing_folds_the_tick_onto_its_own_cycle);
    RUN_TEST(anim_present_reports_whether_anything_was_declared);
}
