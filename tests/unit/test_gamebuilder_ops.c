// Tests for GameBuilder's operation layers: undo/redo, zone objects,
// validation, packaging and the new-pack generator.
//
// These are the parts that change the user's data. The GUI on top of them is
// hand-tested; everything underneath is exercised here, so "it builds" is
// never mistaken for "it works".

#include "greatest.h"

#include "gb.h"
#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define OPS_WORK "build/gbops-pack"

static char *slurp(const char *p, long *len) {
    FILE *f = fopen(p, "rb");
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

// A scratch pack: the reference manifest plus a small map of its own, so the
// tests can edit freely without touching assets/.
static bool ops_scratch(void) {
    mkdir("build", 0777);
    mkdir(OPS_WORK, 0777);
    mkdir(OPS_WORK "/maps", 0777);

    char *text = slurp("assets/kings-bounty/game.json", NULL);
    if (!text) return false;
    cJSON *doc = cJSON_Parse(text);
    free(text);
    if (!doc) return false;

    // One 24x24 zone, all grass with a water border, replacing the four real
    // ones so the test does not depend on the shipped maps.
    cJSON *zones = cJSON_CreateArray();
    cJSON *z = cJSON_CreateObject();
    cJSON_AddStringToObject(z, "id", "testzone");
    cJSON_AddStringToObject(z, "name", "Test Zone");
    cJSON_AddStringToObject(z, "map", "maps/testzone.dat");
    cJSON_AddNumberToObject(z, "width", 24);
    cJSON_AddNumberToObject(z, "height", 24);
    cJSON_AddBoolToObject(z, "is_home", 1);
    cJSON *hs = cJSON_AddObjectToObject(z, "hero_spawn");
    cJSON_AddNumberToObject(hs, "x", 12);
    cJSON_AddNumberToObject(hs, "y", 12);
    cJSON_AddArrayToObject(z, "chests");
    cJSON_AddArrayToObject(z, "signs");
    cJSON_AddArrayToObject(z, "dwellings");
    cJSON_AddArrayToObject(z, "armies");
    cJSON_AddArrayToObject(z, "neighbors");
    cJSON_AddItemToArray(zones, z);
    cJSON_ReplaceItemInObject(doc, "zones", zones);

    // Point the catalogs at the one zone so referential checks have something
    // coherent to look at.
    const char *cats[] = { "castles", "towns", "villains" };
    for (unsigned c = 0; c < 3; c++) {
        cJSON *a = cJSON_GetObjectItem(doc, cats[c]), *e = NULL;
        cJSON_ArrayForEach(e, a) {
            if (cJSON_GetObjectItem(e, "zone"))
                cJSON_ReplaceItemInObject(e, "zone",
                                          cJSON_CreateString("testzone"));
        }
    }

    char *out = cJSON_Print(doc);
    cJSON_Delete(doc);
    FILE *f = fopen(OPS_WORK "/game.json", "wb");
    if (!f) { cJSON_free(out); return false; }
    fputs(out, f);
    fclose(f);
    cJSON_free(out);

    f = fopen(OPS_WORK "/maps/testzone.dat", "wb");
    if (!f) return false;
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 24; x++)
            fputc((x < 2 || y < 2 || x > 21 || y > 21) ? '~' : '.', f);
        fputc('\n', f);
    }
    fclose(f);
    return true;
}

static bool ops_open(GbWorkspace *ws) {
    char err[512] = {0};
    if (!ops_scratch()) return false;
    // The reference pack supplies art the scratch manifest references.
    Pack *base = pack_open("assets/kings-bounty");
    if (base) pack_stack_push(base);
    return gb_workspace_open(ws, OPS_WORK, err, sizeof err);
}

// --- undo / redo ---------------------------------------------------------------

TEST undo_restores_a_json_edit(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    GbUndo *u = gb_undo_create();

    cJSON *before = cJSON_Duplicate(cJSON_GetObjectItem(ws.doc, "pack_name"), 1);
    cJSON *after = cJSON_CreateString("Changed");
    gb_undo_push_json(u, "rename", ws.doc, "pack_name", -1, before, after);
    cJSON_ReplaceItemInObject(ws.doc, "pack_name", after);
    cJSON_Delete(before);
    ASSERT_STR_EQ("Changed",
                  cJSON_GetObjectItem(ws.doc, "pack_name")->valuestring);

    GbUndoApply ctx = { NULL, NULL, NULL };
    ASSERT(gb_undo_can_undo(u));
    ASSERT(gb_undo_undo(u, &ctx));
    ASSERT_STR_EQ("King's Bounty",
                  cJSON_GetObjectItem(ws.doc, "pack_name")->valuestring);

    ASSERT(gb_undo_can_redo(u));
    ASSERT(gb_undo_redo(u, &ctx));
    ASSERT_STR_EQ("Changed",
                  cJSON_GetObjectItem(ws.doc, "pack_name")->valuestring);

    gb_undo_destroy(u);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

// A new edit after an undo must discard the redo branch, or redo would apply
// a change the user already replaced.
TEST editing_after_undo_drops_the_redo_branch(void) {
    GbUndo *u = gb_undo_create();
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "k", "a");

    cJSON *b1 = cJSON_CreateString("a"), *a1 = cJSON_CreateString("b");
    gb_undo_push_json(u, "1", doc, "k", -1, b1, a1);
    cJSON_ReplaceItemInObject(doc, "k", a1);
    cJSON_Delete(b1);

    GbUndoApply ctx = { NULL, NULL, NULL };
    ASSERT(gb_undo_undo(u, &ctx));
    ASSERT(gb_undo_can_redo(u));

    cJSON *b2 = cJSON_CreateString("a"), *a2 = cJSON_CreateString("c");
    gb_undo_push_json(u, "2", doc, "k", -1, b2, a2);
    cJSON_ReplaceItemInObject(doc, "k", a2);
    cJSON_Delete(b2);
    ASSERT_FALSE(gb_undo_can_redo(u));

    gb_undo_destroy(u);
    cJSON_Delete(doc);
    PASS();
}

TEST undo_ring_does_not_overflow(void) {
    GbUndo *u = gb_undo_create();
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddNumberToObject(doc, "n", 0);
    for (int i = 0; i < GB_UNDO_MAX + 50; i++) {
        cJSON *b = cJSON_CreateNumber(i), *a = cJSON_CreateNumber(i + 1);
        gb_undo_push_json(u, "step", doc, "n", -1, b, a);
        cJSON_ReplaceItemInObject(doc, "n", a);
        cJSON_Delete(b);
    }
    ASSERT(gb_undo_depth(u) <= GB_UNDO_MAX);
    ASSERT(gb_undo_can_undo(u));
    gb_undo_destroy(u);
    cJSON_Delete(doc);
    PASS();
}

// --- objects -------------------------------------------------------------------

TEST objects_collect_move_and_delete(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));

    GbObjectList L;
    gb_objects_collect(&L, ws.doc, "testzone");
    int before = L.count;
    ASSERT(before > 0);           // castles/towns were repointed at testzone

    cJSON *n = gb_object_create(ws.doc, "testzone", GB_OBJ_CHEST, 5, 6);
    ASSERT(n != NULL);
    gb_objects_collect(&L, ws.doc, "testzone");
    ASSERT_EQ(before + 1, L.count);

    // find it and move it
    int idx = -1;
    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_CHEST && L.item[i].x == 5) idx = i;
    ASSERT(idx >= 0);
    ASSERT(gb_object_move(&L.item[idx], 9, 10));
    gb_objects_collect(&L, ws.doc, "testzone");
    bool found = false;
    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_CHEST && L.item[i].x == 9 &&
            L.item[i].y == 10) found = true;
    ASSERT(found);

    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_CHEST && L.item[i].x == 9) {
            ASSERT(gb_object_delete(&L.item[i]));
            break;
        }
    gb_objects_collect(&L, ws.doc, "testzone");
    ASSERT_EQ(before, L.count);

    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

// Spawn points are zone properties, not array members: moving them is fine,
// deleting them would leave the zone without a start position.
TEST spawn_points_move_but_cannot_be_deleted(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    GbObjectList L;
    gb_objects_collect(&L, ws.doc, "testzone");
    int idx = -1;
    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_HERO_SPAWN) idx = i;
    ASSERT(idx >= 0);
    ASSERT(gb_object_move(&L.item[idx], 7, 8));
    ASSERT_FALSE(gb_object_delete(&L.item[idx]));
    gb_objects_collect(&L, ws.doc, "testzone");
    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_HERO_SPAWN) {
            ASSERT_EQ(7, L.item[i].x);
            ASSERT_EQ(8, L.item[i].y);
        }
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

// A castle's gate and catalog coordinates are two records of one tile and must
// move together, or the engine stamps the footprint in the wrong place.
TEST castle_move_updates_gate_and_catalog(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    GbObjectList L;
    gb_objects_collect(&L, ws.doc, "testzone");
    int idx = -1;
    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_CASTLE) idx = i;
    ASSERT(idx >= 0);
    ASSERT(gb_object_move(&L.item[idx], 11, 12));
    cJSON *node = L.item[idx].node;
    ASSERT_EQ(11, (int)cJSON_GetObjectItem(node, "gate_x")->valuedouble);
    ASSERT_EQ(12, (int)cJSON_GetObjectItem(node, "gate_y")->valuedouble);
    if (cJSON_GetObjectItem(node, "x"))
        ASSERT_EQ(11, (int)cJSON_GetObjectItem(node, "x")->valuedouble);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

TEST castle_footprint_is_three_by_two(void) {
    int x, y, w, h;
    gb_castle_footprint(10, 10, &x, &y, &w, &h);
    ASSERT_EQ(3, w);
    ASSERT_EQ(2, h);
    ASSERT_EQ(9, x);
    ASSERT_EQ(9, y);
    PASS();
}

// --- validation ----------------------------------------------------------------

TEST validate_flags_a_boat_trap(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));

    // Carve an enclosed pond and put a town dock in it. Enclosed water alone is
    // harmless -- this must fire because a DOCK is on it.
    static MapGrid g;
    ASSERT(mapedit_load(&g, &ws.res, "testzone"));
    for (int y = 8; y <= 10; y++)
        for (int x = 8; x <= 10; x++) g.cell[y][x].terrain = TERRAIN_WATER;

    cJSON *towns = cJSON_GetObjectItem(ws.doc, "towns");
    cJSON *t = cJSON_GetArrayItem(towns, 0);
    ASSERT(t != NULL);
    cJSON *boat = cJSON_GetObjectItem(t, "boat");
    ASSERT(cJSON_IsObject(boat));       // schema is boat:{x,y}, not boat_x
    cJSON_ReplaceItemInObject(boat, "x", cJSON_CreateNumber(9));
    cJSON_ReplaceItemInObject(boat, "y", cJSON_CreateNumber(9));

    MapGrid *grids[GB_MAX_ZONES] = { &g };
    bool loaded[GB_MAX_ZONES] = { true };
    GbFindings F;
    gb_validate(&F, &ws, grids, loaded, 1);

    bool saw = false;
    for (int i = 0; i < F.count; i++)
        if (strstr(F.item[i].what, "BOAT TRAP")) saw = true;
    ASSERTm("a dock on landlocked water must be reported", saw);

    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

// The counterpart: enclosed water with NO dock on it is decorative and must
// stay silent. All four shipped maps rely on this.
TEST validate_ignores_a_pond_with_no_dock(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    static MapGrid g;
    ASSERT(mapedit_load(&g, &ws.res, "testzone"));
    for (int y = 8; y <= 10; y++)
        for (int x = 8; x <= 10; x++) g.cell[y][x].terrain = TERRAIN_WATER;

    cJSON *towns = cJSON_GetObjectItem(ws.doc, "towns"), *t = NULL;
    cJSON_ArrayForEach(t, towns) {          // move every dock off the pond
        cJSON *b = cJSON_GetObjectItem(t, "boat");
        if (cJSON_IsObject(b)) {
            cJSON_ReplaceItemInObject(b, "x", cJSON_CreateNumber(0));
            cJSON_ReplaceItemInObject(b, "y", cJSON_CreateNumber(0));
        }
    }
    MapGrid *grids[GB_MAX_ZONES] = { &g };
    bool loaded[GB_MAX_ZONES] = { true };
    GbFindings F;
    gb_validate(&F, &ws, grids, loaded, 1);
    for (int i = 0; i < F.count; i++)
        ASSERT_FALSE(strstr(F.item[i].what, "BOAT TRAP") != NULL);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

TEST validate_flags_zone_with_too_few_castles(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    // Leave one castle in the zone but many villains -- salt_villains would
    // starve.
    cJSON *castles = cJSON_GetObjectItem(ws.doc, "castles");
    while (cJSON_GetArraySize(castles) > 1) cJSON_DeleteItemFromArray(castles, 1);

    MapGrid *grids[GB_MAX_ZONES] = { NULL };
    bool loaded[GB_MAX_ZONES] = { false };
    GbFindings F;
    gb_validate(&F, &ws, grids, loaded, 1);
    bool saw = false;
    for (int i = 0; i < F.count; i++)
        if (strstr(F.item[i].what, "castles")) saw = true;
    ASSERT(saw);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

TEST validate_flags_unknown_troop_in_villain_army(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    cJSON *v = cJSON_GetArrayItem(cJSON_GetObjectItem(ws.doc, "villains"), 0);
    cJSON *army = cJSON_GetObjectItem(v, "army");
    cJSON *st = cJSON_GetArrayItem(army, 0);
    cJSON_ReplaceItemInObject(st, "troop", cJSON_CreateString("no_such_troop"));

    MapGrid *grids[GB_MAX_ZONES] = { NULL };
    bool loaded[GB_MAX_ZONES] = { false };
    GbFindings F;
    gb_validate(&F, &ws, grids, loaded, 1);
    bool saw = false;
    for (int i = 0; i < F.count; i++)
        if (strstr(F.item[i].what, "no_such_troop")) saw = true;
    ASSERT(saw);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

// --- new pack + packaging -------------------------------------------------------

TEST newpack_creates_a_loadable_pack(void) {
    pack_rmtree("build/gbnew");
    char err[512] = {0};
    ASSERTm(err, gb_newpack_create("build/gbnew", err, sizeof err));

    // It must be openable by the editor...
    GbWorkspace ws = {0};
    ASSERTm(err, gb_workspace_open(&ws, "build/gbnew", err, sizeof err));
    ASSERT(ws.doc != NULL);
    gb_workspace_close(&ws);
    pack_stack_clear();

    // ...and parseable by the engine.
    Pack *p = pack_open("build/gbnew");
    ASSERT(p != NULL);
    pack_stack_push(p);
    static Resources res;
    ASSERT(resources_load(&res, "game.json"));
    ASSERT_EQ(1, res.zone_count);
    ASSERT(res.troops_count >= 1);
    pack_stack_clear();
    PASS();
}

TEST newpack_refuses_to_overwrite(void) {
    char err[512] = {0};
    pack_rmtree("build/gbnew2");
    ASSERT(gb_newpack_create("build/gbnew2", err, sizeof err));
    // Second time must refuse rather than clobber someone's work.
    ASSERT_FALSE(gb_newpack_create("build/gbnew2", err, sizeof err));
    ASSERT(strstr(err, "already") != NULL);
    PASS();
}

TEST package_writes_an_archive(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    char err[512] = {0};
    remove("build/gbops.openbounty");
    ASSERTm(err, gb_package(&ws, "build/gbops.openbounty", err, sizeof err));

    struct stat st;
    ASSERT_EQ(0, stat("build/gbops.openbounty", &st));
    ASSERT(st.st_size > 0);

    // The archive must be openable as a pack.
    pack_stack_clear();
    Pack *p = pack_open("build/gbops.openbounty");
    ASSERT(p != NULL);
    pack_stack_push(p);
    size_t sz = 0;
    ASSERT(pack_stack_read("game.json", &sz) != NULL);
    pack_stack_clear();
    gb_workspace_close(&ws);
    PASS();
}

TEST package_refuses_while_unsaved(void) {
    GbWorkspace ws = {0};
    ASSERT(ops_open(&ws));
    ws.dirty = true;
    char err[512] = {0};
    ASSERT_FALSE(gb_package(&ws, "build/gbops2.openbounty", err, sizeof err));
    ASSERT(strstr(err, "unsaved") != NULL);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

SUITE(unit_gamebuilder_ops_suite) {
    RUN_TEST(undo_restores_a_json_edit);
    RUN_TEST(editing_after_undo_drops_the_redo_branch);
    RUN_TEST(undo_ring_does_not_overflow);
    RUN_TEST(objects_collect_move_and_delete);
    RUN_TEST(spawn_points_move_but_cannot_be_deleted);
    RUN_TEST(castle_move_updates_gate_and_catalog);
    RUN_TEST(castle_footprint_is_three_by_two);
    RUN_TEST(validate_flags_a_boat_trap);
    RUN_TEST(validate_ignores_a_pond_with_no_dock);
    RUN_TEST(validate_flags_zone_with_too_few_castles);
    RUN_TEST(validate_flags_unknown_troop_in_villain_army);
    RUN_TEST(newpack_creates_a_loadable_pack);
    RUN_TEST(newpack_refuses_to_overwrite);
    RUN_TEST(package_writes_an_archive);
    RUN_TEST(package_refuses_while_unsaved);
}
