// A guardian's traits through a save: static, requires_troop, scene_index and
// scene_title are written with every foe and read back, and a save without
// them takes them from the zone army the foe was spawned from.

#include "greatest.h"
#include "game.h"
#include "map.h"
#include "fog.h"
#include "savegame.h"
#include "fixtures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOES_SAVE_PATH "build/test_save_foes.sav"

// The first foe spawned from a zone army, or -1.
static int first_army_foe(const Game *g) {
    for (int i = 0; i < g->foe_count; i++)
        if (GameFoeArmy(g, &g->foes[i])) return i;
    return -1;
}

// Rename every occurrence of `key` in the save so the loader cannot see it,
// keeping the file the same length.
static bool hide_key(const char *path, const char *key, const char *hidden) {
    FILE *fp = fopen(path, "r+b");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(fp); return false; }
    fseek(fp, 0, SEEK_SET);
    bool ok = fread(buf, 1, (size_t)len, fp) == (size_t)len;
    buf[len] = '\0';
    size_t kl = strlen(key);
    for (char *k = strstr(buf, key); k; k = strstr(k + kl, key))
        memcpy(k, hidden, kl);
    fseek(fp, 0, SEEK_SET);
    ok = ok && fwrite(buf, 1, (size_t)len, fp) == (size_t)len;
    fclose(fp);
    free(buf);
    return ok;
}

TEST guardian_traits_survive_a_save_round_trip(void) {
    Resources *res1; Game *g1; Map *m1; Fog *f1;
    ASSERT(fx_init_game_full(&res1, &g1, &m1, &f1, NULL, FIXTURE_SEED));
    int i = first_army_foe(g1);
    ASSERT(i >= 0);
    FoeState *f = &g1->foes[i];
    f->is_static = true;
    strcpy(f->requires_troop, "dragons");
    f->scene_index = 3;
    strcpy(f->scene_title, "The Gate");
    ASSERT_EQ(SAVE_OK, SaveGameWrite(FOES_SAVE_PATH, g1, m1, f1));
    fx_free_game_full(res1, g1, m1, f1);

    Resources *res2; Game *g2; Map *m2; Fog *f2;
    ASSERT(fx_init_game_full(&res2, &g2, &m2, &f2, NULL, FIXTURE_SEED));
    ASSERT_EQ(SAVE_OK, SaveGameRead(FOES_SAVE_PATH, g2, m2, f2));
    ASSERT(i < g2->foe_count);
    const FoeState *r = &g2->foes[i];
    ASSERT(r->is_static);
    ASSERT_STR_EQ("dragons", r->requires_troop);
    ASSERT_EQ(3, r->scene_index);
    ASSERT_STR_EQ("The Gate", r->scene_title);
    fx_free_game_full(res2, g2, m2, f2);

    remove(FOES_SAVE_PATH);
    PASS();
}

TEST old_saves_take_guardian_traits_from_the_pack(void) {
    Resources *res1; Game *g1; Map *m1; Fog *f1;
    ASSERT(fx_init_game_full(&res1, &g1, &m1, &f1, NULL, FIXTURE_SEED));
    int i = first_army_foe(g1);
    ASSERT(i >= 0);
    ASSERT_EQ(SAVE_OK, SaveGameWrite(FOES_SAVE_PATH, g1, m1, f1));
    fx_free_game_full(res1, g1, m1, f1);
    ASSERT(hide_key(FOES_SAVE_PATH, "\"static\"", "\"xtatic\""));
    ASSERT(hide_key(FOES_SAVE_PATH, "\"requires_troop\"", "\"xequires_troop\""));
    ASSERT(hide_key(FOES_SAVE_PATH, "\"scene_index\"", "\"xcene_index\""));
    ASSERT(hide_key(FOES_SAVE_PATH, "\"scene_title\"", "\"xcene_title\""));

    // The loading game's pack makes that army a guardian, so the traits the
    // loader fills in can only have come from the pack.
    Resources *res2; Game *g2; Map *m2; Fog *f2;
    ASSERT(fx_init_game_full(&res2, &g2, &m2, &f2, NULL, FIXTURE_SEED));
    ResZoneArmy *za = (ResZoneArmy *)GameFoeArmy(g2, &g2->foes[i]);
    ASSERT(za);
    za->is_static = true;
    strcpy(za->requires_troop, "dragons");
    za->scene_index = 2;
    strcpy(za->title, "The Gate");
    ASSERT_EQ(SAVE_OK, SaveGameRead(FOES_SAVE_PATH, g2, m2, f2));
    const FoeState *r = &g2->foes[i];
    ASSERT(r->is_static);
    ASSERT_STR_EQ("dragons", r->requires_troop);
    ASSERT_EQ(2, r->scene_index);
    ASSERT_STR_EQ("The Gate", r->scene_title);
    fx_free_game_full(res2, g2, m2, f2);

    remove(FOES_SAVE_PATH);
    PASS();
}

SUITE(unit_save_foes_suite) {
    RUN_TEST(guardian_traits_survive_a_save_round_trip);
    RUN_TEST(old_saves_take_guardian_traits_from_the_pack);
}
