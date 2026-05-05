// State serialization JSON shape tests. Validates that
// state_build_snapshot emits expected keys with the right types,
// and that critical values reflect the live game.

#include "greatest.h"
#include "state_serialize.h"
#include "fixtures.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

TEST snapshot_has_expected_top_level_keys(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    cJSON *root = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    ASSERT(root);
    ASSERT(cJSON_GetObjectItem(root, "version"));
    ASSERT(cJSON_GetObjectItem(root, "mode"));
    ASSERT(cJSON_GetObjectItem(root, "seed"));
    ASSERT(cJSON_GetObjectItem(root, "character"));
    ASSERT(cJSON_GetObjectItem(root, "stats"));

    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_mode_is_adventure_when_no_combat(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    cJSON *root = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    ASSERT(mode);
    ASSERT(cJSON_IsString(mode));
    ASSERT_STR_EQ("adventure", mode->valuestring);

    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_seed_matches_game(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->seed = 13579UL;

    cJSON *root = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    cJSON *seed = cJSON_GetObjectItem(root, "seed");
    ASSERT(seed);
    ASSERT(cJSON_IsNumber(seed));
    ASSERT_EQ(13579, (int)seed->valuedouble);

    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_character_has_class_and_rank(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    cJSON *root = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    cJSON *ch = cJSON_GetObjectItem(root, "character");
    ASSERT(ch);
    ASSERT(cJSON_GetObjectItem(ch, "name"));
    ASSERT(cJSON_GetObjectItem(ch, "class"));
    ASSERT(cJSON_GetObjectItem(ch, "rank_index"));
    ASSERT(cJSON_IsNumber(cJSON_GetObjectItem(ch, "rank_index")));

    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_stats_gold_matches_game(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    g->stats.gold = 9999;

    cJSON *root = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    cJSON *st = cJSON_GetObjectItem(root, "stats");
    ASSERT(st);
    cJSON *gold = cJSON_GetObjectItem(st, "gold");
    ASSERT(gold);
    ASSERT_EQ(9999, (int)gold->valuedouble);

    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_includes_trigger_when_provided(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    cJSON *root = state_build_snapshot(g, NULL, m, f, "step:right", NULL, 0, 0);
    cJSON *trig = cJSON_GetObjectItem(root, "trigger");
    ASSERT(trig);
    ASSERT_STR_EQ("step:right", trig->valuestring);
    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_omits_trigger_when_null(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    cJSON *root = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    ASSERT(!cJSON_GetObjectItem(root, "trigger"));
    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST snapshot_unattached_when_no_game(void) {
    cJSON *root = state_build_snapshot(NULL, NULL, NULL, NULL, NULL, NULL, 0, 0);
    ASSERT(root);
    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    ASSERT(mode);
    ASSERT_STR_EQ("unattached", mode->valuestring);
    cJSON_Delete(root);
    PASS();
}

TEST snapshot_serializes_to_valid_json_string(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));

    cJSON *root = state_build_snapshot(g, NULL, m, f, NULL, NULL, 0, 0);
    char *txt = cJSON_PrintUnformatted(root);
    ASSERT(txt);
    ASSERT(strlen(txt) > 0);
    // Round-trip parse to confirm valid JSON.
    cJSON *parsed = cJSON_Parse(txt);
    ASSERT(parsed);
    ASSERT(cJSON_GetObjectItem(parsed, "mode"));

    free(txt);
    cJSON_Delete(parsed);
    cJSON_Delete(root);
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(state_json_suite) {
    RUN_TEST(snapshot_has_expected_top_level_keys);
    RUN_TEST(snapshot_mode_is_adventure_when_no_combat);
    RUN_TEST(snapshot_seed_matches_game);
    RUN_TEST(snapshot_character_has_class_and_rank);
    RUN_TEST(snapshot_stats_gold_matches_game);
    RUN_TEST(snapshot_includes_trigger_when_provided);
    RUN_TEST(snapshot_omits_trigger_when_null);
    RUN_TEST(snapshot_unattached_when_no_game);
    RUN_TEST(snapshot_serializes_to_valid_json_string);
}
