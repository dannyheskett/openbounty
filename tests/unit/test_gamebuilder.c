// Tests for GameBuilder's workspace layer (tools/gamebuilder/gb_workspace.c).
//
// The GUI is not testable here, but the layer underneath it is, and it is the
// layer that can silently destroy someone's pack. Two properties matter:
//
//   GB-121  unknown keys survive a round-trip, so a pack using engine features
//           newer than this build is not damaged by opening and saving it.
//   GB-114  an unchanged workspace saves byte-identically, so a pack under
//           version control produces an empty diff when nothing changed.

#include "greatest.h"

#include "gb.h"
#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define WORK "build/gbtest-pack"

// Copy the reference manifest into a scratch pack we are free to overwrite.
static bool make_scratch(const char *extra_key) {
    mkdir("build", 0777);
    mkdir(WORK, 0777);
    FILE *in = fopen("assets/kings-bounty/game.json", "rb");
    if (!in) return false;
    fseek(in, 0, SEEK_END);
    long n = ftell(in);
    fseek(in, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t rd = fread(buf, 1, (size_t)n, in);
    buf[rd] = 0;
    fclose(in);

    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (!doc) return false;
    if (extra_key) {
        // A key this build knows nothing about, of the kind a future engine
        // might add. It must come back out unchanged.
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "some_future_number", 4242);
        cJSON_AddStringToObject(o, "some_future_string", "keep me");
        cJSON_AddItemToObject(doc, extra_key, o);
    }
    char *out = cJSON_Print(doc);
    cJSON_Delete(doc);

    FILE *f = fopen(WORK "/game.json", "wb");
    if (!f) { cJSON_free(out); return false; }
    fwrite(out, 1, strlen(out), f);
    fputc('\n', f);
    fclose(f);
    cJSON_free(out);
    return true;
}

static char *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    size_t rd = fread(b, 1, (size_t)n, f);
    b[rd] = 0;
    fclose(f);
    if (len) *len = (long)rd;
    return b;
}

// ---------------------------------------------------------------------------

TEST open_reference_pack(void) {
    ASSERT(make_scratch(NULL));
    GbWorkspace ws = {0};
    char err[512] = {0};
    ASSERTm(err, gb_workspace_open(&ws, WORK, err, sizeof err));
    ASSERT(ws.open);
    ASSERT(ws.doc != NULL);
    ASSERT_FALSE(ws.dirty);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

TEST open_reports_missing_manifest(void) {
    mkdir("build/gbtest-empty", 0777);
    GbWorkspace ws = {0};
    char err[512] = {0};
    ASSERT_FALSE(gb_workspace_open(&ws, "build/gbtest-empty", err, sizeof err));
    // The message must name the problem, not just fail (GB-012).
    ASSERT(strstr(err, "game.json") != NULL);
    PASS();
}

TEST open_reports_parse_position(void) {
    mkdir("build/gbtest-bad", 0777);
    FILE *f = fopen("build/gbtest-bad/game.json", "wb");
    fputs("{\n  \"a\": 1,\n  \"b\": oops\n}\n", f);
    fclose(f);
    GbWorkspace ws = {0};
    char err[512] = {0};
    ASSERT_FALSE(gb_workspace_open(&ws, "build/gbtest-bad", err, sizeof err));
    ASSERT(strstr(err, "line") != NULL);   // says WHERE, not just "invalid"
    PASS();
}

// GB-114: save an untouched workspace, and the file must not move a byte.
TEST unchanged_save_is_byte_identical(void) {
    ASSERT(make_scratch(NULL));
    long before_n = 0;
    char *before = slurp(WORK "/game.json", &before_n);
    ASSERT(before != NULL);

    GbWorkspace ws = {0};
    char err[512] = {0};
    ASSERTm(err, gb_workspace_open(&ws, WORK, err, sizeof err));
    ASSERTm(err, gb_workspace_save(&ws, err, sizeof err));
    gb_workspace_close(&ws);
    pack_stack_clear();

    long after_n = 0;
    char *after = slurp(WORK "/game.json", &after_n);
    ASSERT(after != NULL);
    ASSERT_EQ_FMT(before_n, after_n, "%ld");
    ASSERT_EQ(0, memcmp(before, after, (size_t)before_n));
    free(before);
    free(after);
    PASS();
}

// GB-121: a key this build does not understand survives open + save.
TEST unknown_keys_survive_round_trip(void) {
    ASSERT(make_scratch("a_future_block"));
    GbWorkspace ws = {0};
    char err[512] = {0};
    ASSERTm(err, gb_workspace_open(&ws, WORK, err, sizeof err));
    ASSERTm(err, gb_workspace_save(&ws, err, sizeof err));
    gb_workspace_close(&ws);
    pack_stack_clear();

    char *text = slurp(WORK "/game.json", NULL);
    ASSERT(text != NULL);
    cJSON *doc = cJSON_Parse(text);
    ASSERT(doc != NULL);
    cJSON *blk = cJSON_GetObjectItem(doc, "a_future_block");
    ASSERT(blk != NULL);
    cJSON *num = cJSON_GetObjectItem(blk, "some_future_number");
    cJSON *str = cJSON_GetObjectItem(blk, "some_future_string");
    ASSERT(num && str);
    ASSERT_EQ(4242, (int)num->valuedouble);
    ASSERT_STR_EQ("keep me", str->valuestring);
    cJSON_Delete(doc);
    free(text);
    PASS();
}

// An edit through the DOM lands in the file and nothing else moves.
TEST edited_field_is_written(void) {
    ASSERT(make_scratch(NULL));
    GbWorkspace ws = {0};
    char err[512] = {0};
    ASSERTm(err, gb_workspace_open(&ws, WORK, err, sizeof err));

    cJSON *name = cJSON_GetObjectItem(ws.doc, "pack_name");
    ASSERT(name != NULL);
    cJSON_ReplaceItemInObject(ws.doc, "pack_name",
                              cJSON_CreateString("Edited By Test"));
    ASSERTm(err, gb_workspace_save(&ws, err, sizeof err));
    gb_workspace_close(&ws);
    pack_stack_clear();

    char *text = slurp(WORK "/game.json", NULL);
    cJSON *doc = cJSON_Parse(text);
    ASSERT(doc != NULL);
    ASSERT_STR_EQ("Edited By Test",
                  cJSON_GetObjectItem(doc, "pack_name")->valuestring);
    // and an untouched neighbour is intact
    ASSERT(cJSON_GetObjectItem(doc, "troops") != NULL);
    cJSON_Delete(doc);
    free(text);
    PASS();
}

// The pack must still be loadable by the engine after a round-trip -- the
// point of all of the above.
TEST engine_still_loads_after_round_trip(void) {
    ASSERT(make_scratch("a_future_block"));
    GbWorkspace ws = {0};
    char err[512] = {0};
    ASSERTm(err, gb_workspace_open(&ws, WORK, err, sizeof err));
    ASSERTm(err, gb_workspace_save(&ws, err, sizeof err));
    gb_workspace_close(&ws);
    pack_stack_clear();

    // Art lives in the reference pack; layer it under the scratch manifest.
    Pack *base = pack_open("assets/kings-bounty");
    ASSERT(base != NULL);
    pack_stack_push(base);
    Pack *p = pack_open(WORK);
    ASSERT(p != NULL);
    pack_stack_push(p);

    static Resources res;
    ASSERT(resources_load(&res, "game.json"));
    ASSERT(res.troops_count > 0);
    ASSERT(res.zone_count > 0);
    pack_stack_clear();
    PASS();
}

SUITE(unit_gamebuilder_suite) {
    RUN_TEST(open_reference_pack);
    RUN_TEST(open_reports_missing_manifest);
    RUN_TEST(open_reports_parse_position);
    RUN_TEST(unchanged_save_is_byte_identical);
    RUN_TEST(unknown_keys_survive_round_trip);
    RUN_TEST(edited_field_is_written);
    RUN_TEST(engine_still_loads_after_round_trip);
}
