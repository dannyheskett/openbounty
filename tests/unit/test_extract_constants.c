// The extractor's port-authored constants (tools/extract_gamejson_const.inc)
// are the King's Bounty pack's own sections, so an extracted pack matches the
// shipped one. Regenerate with scripts/gen_extract_constants.py when this fails.

#include "greatest.h"
#include "cJSON.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extract_gamejson_const.inc"

static cJSON *load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    return j;
}

TEST the_constants_are_the_packs_sections(void) {
    cJSON *consts = cJSON_Parse(EX_PORT_CONSTANTS_JSON);
    cJSON *game = load("assets/kings-bounty/game.json");
    cJSON *strings = load("assets/kings-bounty/strings/en.json");
    ASSERT(consts && game && strings);
    for (cJSON *sec = consts->child; sec; sec = sec->next) {
        const cJSON *want = strcmp(sec->string, "strings") == 0
                              ? strings : cJSON_GetObjectItem(game, sec->string);
        ASSERTm(sec->string, want && cJSON_Compare(sec, want, true));
    }
    // Every section the extractor does not build from KB.EXE is in the constants.
    static const char *const built[] = {
        "title", "version", "pack_id", "pack_name", "pack_kind", "world", "time",
        "economy", "tuning", "combat", "zones", "towns", "castles", "troops",
        "spells", "classes", "villains", "artifacts",
    };
    for (cJSON *sec = game->child; sec; sec = sec->next) {
        bool is_built = false;
        for (size_t i = 0; i < sizeof built / sizeof built[0]; i++)
            if (strcmp(sec->string, built[i]) == 0) is_built = true;
        if (!is_built) ASSERTm(sec->string, cJSON_GetObjectItem(consts, sec->string) != NULL);
    }
    cJSON_Delete(consts);
    cJSON_Delete(game);
    cJSON_Delete(strings);
    PASS();
}

SUITE(unit_extract_constants_suite) {
    RUN_TEST(the_constants_are_the_packs_sections);
}
