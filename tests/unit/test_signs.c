// Road signs load whole (#135): every sign's title and body in both packs
// reach the game exactly as the pack wrote them -- Rome's Coves sign, 147
// characters, was cut at 127 ("...Onl").

#include "greatest.h"
#include "pack.h"
#include "resources.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = '\0';
    fclose(f);
    return b;
}

// How many signs of `pack` reached the game other than as written; -1 when
// the pack would not load.
static int signs_changed(const char *pack, int *seen) {
    char path[256];
    snprintf(path, sizeof path, "%s/game.json", pack);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    Pack *p = pack_open(pack);
    if (!root || !p) { cJSON_Delete(root); return -1; }
    pack_stack_push(p);
    Resources *res = calloc(1, sizeof *res);
    int changed = -1;
    *seen = 0;
    if (res && resources_load(res, "game.json")) {
        changed = 0;
        cJSON *zones = cJSON_GetObjectItem(root, "zones");
        for (int zi = 0; zi < res->zone_count && zi < cJSON_GetArraySize(zones); zi++) {
            cJSON *signs = cJSON_GetObjectItem(cJSON_GetArrayItem(zones, zi), "signs");
            const ResZone *z = &res->zones[zi];
            for (int i = 0; i < z->sign_count && i < cJSON_GetArraySize(signs); i++) {
                cJSON *js = cJSON_GetArrayItem(signs, i);
                const cJSON *t = cJSON_GetObjectItem(js, "title"), *b = cJSON_GetObjectItem(js, "body");
                (*seen)++;
                if (strcmp(z->signs[i].title, cJSON_IsString(t) ? t->valuestring : "") ||
                    strcmp(z->signs[i].body, cJSON_IsString(b) ? b->valuestring : ""))
                    changed++;
            }
        }
    }
    if (res) resources_free(res);
    free(res);
    pack_stack_pop();
    cJSON_Delete(root);
    return changed;
}

TEST every_sign_loads_whole(void) {
    int seen = 0;
    ASSERT_EQ(0, signs_changed("assets/glory-of-rome", &seen));
    ASSERT(seen > 0);
    ASSERT_EQ(0, signs_changed("assets/kings-bounty", &seen));
    ASSERT(seen > 0);
    PASS();
}

SUITE(unit_signs_suite) {
    RUN_TEST(every_sign_loads_whole);
}
