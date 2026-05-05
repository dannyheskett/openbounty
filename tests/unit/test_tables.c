// Resource catalog lookup tests.

#include "greatest.h"
#include "tables.h"
#include "resources.h"

#include <string.h>
#include <stdlib.h>

#define ASSET_PATH "assets/kings-bounty/game.json"

// Most of these depend on resources_load having been called, so they
// load Resources locally. Greatest runs tests sequentially so loading
// per test is fine for v1.
static Resources *load_res(void) {
    Resources *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    if (!resources_load(r, ASSET_PATH)) { free(r); return NULL; }
    return r;
}

TEST troop_by_id_known_returns_def(void) {
    Resources *r = load_res();
    ASSERT(r);
    const TroopDef *t = troop_by_id("peasants");
    ASSERT(t != NULL);
    ASSERT(t->name[0] != '\0');
    resources_free(r); free(r);
    PASS();
}

TEST troop_by_id_unknown_returns_null(void) {
    Resources *r = load_res();
    ASSERT(r);
    ASSERT(troop_by_id("zzz_no_such_troop") == NULL);
    resources_free(r); free(r);
    PASS();
}

TEST class_by_id_knight_returns_def(void) {
    Resources *r = load_res();
    ASSERT(r);
    const ClassDef *c = class_by_id("knight");
    ASSERT(c != NULL);
    resources_free(r); free(r);
    PASS();
}

TEST villain_by_id_unknown_returns_null(void) {
    Resources *r = load_res();
    ASSERT(r);
    ASSERT(villain_by_id("zzz_no_such") == NULL);
    resources_free(r); free(r);
    PASS();
}

SUITE(tables_suite) {
    RUN_TEST(troop_by_id_known_returns_def);
    RUN_TEST(troop_by_id_unknown_returns_null);
    RUN_TEST(class_by_id_knight_returns_def);
    RUN_TEST(villain_by_id_unknown_returns_null);
}
