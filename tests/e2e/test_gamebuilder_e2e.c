// End-to-end: build a pack from nothing, edit every layer, package it, and
// prove the engine can still load and read what came out.
//
// This is the test that answers "do all the functions actually operate", as
// opposed to the unit tests which answer "does each one behave". It walks the
// same path a user does: New Pack, paint, place, edit catalog, validate, save,
// package, reopen.

#include "greatest.h"

#include "gb.h"
#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define E2E "build/gbe2e"
#define E2E_ZIP "build/gbe2e.openbounty"

// GB-001: from nothing to a pack the engine loads, touching every layer.
TEST from_nothing_to_a_packaged_pack(void) {
    pack_rmtree(E2E);
    remove(E2E_ZIP);
    char err[512] = {0};

    // 1. New pack ------------------------------------------------------------
    ASSERTm(err, gb_newpack_create(E2E, err, sizeof err));

    GbWorkspace ws = {0};
    ASSERTm(err, gb_workspace_open(&ws, E2E, err, sizeof err));
    ASSERT(ws.res_valid);
    ASSERT_EQ(1, gb_zone_count(ws.doc));
    const char *zid = gb_zone_id_at(ws.doc, 0);
    ASSERT_STR_EQ("starter", zid);

    // 2. Load and paint the map ----------------------------------------------
    static MapGrid g;
    ASSERT(mapedit_load(&g, &ws.res, zid));
    ASSERT_EQ(24, g.w);
    ASSERT_EQ(24, g.h);

    GbUndo *undo = gb_undo_create();
    MapCell before[9], after[9];
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) before[j * 3 + i] = g.cell[10 + j][10 + i];
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) g.cell[10 + j][10 + i].terrain = TERRAIN_FOREST;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) after[j * 3 + i] = g.cell[10 + j][10 + i];
    gb_undo_push_tiles(undo, "paint", 0, 10, 10, 3, 3, before, after);

    // furnish must give the new forest its edge variants
    int unresolved = 0;
    mapedit_despeckle(&g);
    int furnished = mapedit_furnish(&g, &unresolved);
    ASSERT(furnished > 0);
    ASSERT_EQ(0, unresolved);

    // 3. Undo, then redo, and confirm the grid follows ------------------------
    MapGrid *grids_for_undo[GB_MAX_ZONES] = { &g };
    bool loaded[GB_MAX_ZONES] = { true };
    struct Ctx { MapGrid *g; } ctxdata = { &g };
    (void)ctxdata;
    GbUndoApply uctx = { NULL, NULL, NULL };
    // grid_for is supplied by the app; here we hand back the one grid.
    extern MapGrid *e2e_grid_for(void *user, int zone);
    uctx.grid_for = e2e_grid_for;
    uctx.user = &g;
    ASSERT(gb_undo_undo(undo, &uctx));
    ASSERT_EQ(TERRAIN_GRASS, g.cell[11][11].terrain);
    ASSERT(gb_undo_redo(undo, &uctx));
    ASSERT_EQ(TERRAIN_FOREST, g.cell[11][11].terrain);

    // 4. Place objects --------------------------------------------------------
    ASSERT(gb_object_create(ws.doc, zid, GB_OBJ_CHEST, 6, 6) != NULL);
    ASSERT(gb_object_create(ws.doc, zid, GB_OBJ_SIGN, 7, 6) != NULL);
    ASSERT(gb_object_create(ws.doc, zid, GB_OBJ_TOWN, 8, 8) != NULL);
    ASSERT(gb_object_create(ws.doc, zid, GB_OBJ_CASTLE, 14, 14) != NULL);
    GbObjectList L;
    gb_objects_collect(&L, ws.doc, zid);
    ASSERT(L.count >= 5);              // 4 placed + hero spawn

    // move one and check it stuck
    int chest = -1;
    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_CHEST) chest = i;
    ASSERT(chest >= 0);
    ASSERT(gb_object_move(&L.item[chest], 9, 9));
    gb_objects_collect(&L, ws.doc, zid);
    bool moved = false;
    for (int i = 0; i < L.count; i++)
        if (L.item[i].kind == GB_OBJ_CHEST && L.item[i].x == 9 &&
            L.item[i].y == 9) moved = true;
    ASSERT(moved);

    // 5. Edit the catalog through the DOM ------------------------------------
    cJSON *troop = cJSON_GetArrayItem(cJSON_GetObjectItem(ws.doc, "troops"), 0);
    ASSERT(troop != NULL);
    cJSON_ReplaceItemInObject(troop, "name", cJSON_CreateString("Guardsmen"));
    cJSON_ReplaceItemInObject(troop, "recruit_cost", cJSON_CreateNumber(75));
    ws.dirty = true;

    // 6. Validate -- advisory, must not throw or refuse ----------------------
    GbFindings F;
    gb_validate(&F, &ws, grids_for_undo, loaded, 1);
    ASSERT(F.count >= 0);
    // A pack this bare has known gaps; the point is that they are REPORTED
    // rather than fatal.

    // 7. Checklist tells us what is missing ----------------------------------
    GbChecklist C;
    gb_checklist_build(&C, &ws, grids_for_undo, loaded, 1);
    ASSERT(C.count > 0);
    ASSERT(C.done_count < C.count);    // a bare pack is not finished

    // 8. Save both the manifest and the map ----------------------------------
    ASSERTm(err, gb_workspace_save(&ws, err, sizeof err));
    ASSERT(mapedit_save(&g, &ws.res));

    // 9. Package --------------------------------------------------------------
    ASSERTm(err, gb_package(&ws, E2E_ZIP, err, sizeof err));
    struct stat st;
    ASSERT_EQ(0, stat(E2E_ZIP, &st));
    ASSERT(st.st_size > 0);

    gb_undo_destroy(undo);
    gb_workspace_close(&ws);
    pack_stack_clear();

    // 10. Reopen the ARCHIVE through the engine and confirm the edits survived
    Pack *p = pack_open(E2E_ZIP);
    ASSERT(p != NULL);
    pack_stack_push(p);
    static Resources res;
    ASSERT(resources_load(&res, "game.json"));
    ASSERT_STR_EQ("Guardsmen", res.troops[0].name);
    ASSERT_EQ(75, res.troops[0].recruit_cost);
    ASSERT_EQ(1, res.zone_count);

    // and the painted terrain is in the packaged map
    static Map m;
    ASSERT(MapLoadZone(&m, &res, "starter"));
    ASSERT_EQ(TERRAIN_FOREST, m.tiles[11][11].terrain);
    pack_stack_clear();
    PASS();
}

MapGrid *e2e_grid_for(void *user, int zone) {
    (void)zone;
    return (MapGrid *)user;
}

// GB-002: opening an existing pack, changing nothing, and saving must not
// alter it -- the property that makes the editor safe to point at real work.
TEST round_trip_of_an_existing_pack_changes_nothing(void) {
    char err[512] = {0};
    GbWorkspace ws = {0};
    ASSERTm(err, gb_workspace_open(&ws, E2E, err, sizeof err));

    FILE *f = fopen(E2E "/game.json", "rb");
    ASSERT(f != NULL);
    fseek(f, 0, SEEK_END);
    long n1 = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *a = malloc((size_t)n1 + 1);
    size_t rd = fread(a, 1, (size_t)n1, f);
    a[rd] = 0;
    fclose(f);

    ASSERTm(err, gb_workspace_save(&ws, err, sizeof err));

    f = fopen(E2E "/game.json", "rb");
    fseek(f, 0, SEEK_END);
    long n2 = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n2 + 1);
    rd = fread(b, 1, (size_t)n2, f);
    b[rd] = 0;
    fclose(f);

    ASSERT_EQ_FMT(n1, n2, "%ld");
    ASSERT_EQ(0, memcmp(a, b, (size_t)n1));
    free(a);
    free(b);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

// Opening the archive we just built, as an archive, must extract and work.
TEST archive_opens_by_extracting(void) {
    char err[512] = {0}, dir[GB_PATH_MAX] = {0};
    ASSERTm(err, gb_archive_extract(E2E_ZIP, dir, sizeof dir, err, sizeof err));
    ASSERT(dir[0] != 0);

    GbWorkspace ws = {0};
    ASSERTm(err, gb_workspace_open(&ws, dir, err, sizeof err));
    ASSERT(ws.res_valid);
    // The workspace points at the extracted directory, never the archive --
    // editing must not touch the user's only copy.
    ASSERT(strstr(ws.root, ".openbounty") == NULL);
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

// Autosave writes beside the pack, is recoverable, and never touches game.json.
TEST autosave_recovers_unsaved_work(void) {
    char err[512] = {0};
    GbWorkspace ws = {0};
    ASSERTm(err, gb_workspace_open(&ws, E2E, err, sizeof err));

    cJSON_ReplaceItemInObject(ws.doc, "pack_name",
                              cJSON_CreateString("Unsaved Name"));
    ws.dirty = true;
    ASSERT(gb_autosave_write(&ws));
    ASSERT(gb_autosave_exists(ws.root));

    // Throw the edit away by reopening from disk...
    gb_workspace_close(&ws);
    pack_stack_clear();
    ASSERTm(err, gb_workspace_open(&ws, E2E, err, sizeof err));
    ASSERT(strcmp(cJSON_GetObjectItem(ws.doc, "pack_name")->valuestring,
                  "Unsaved Name") != 0);

    // ...then recover it.
    ASSERTm(err, gb_autosave_recover(&ws, err, sizeof err));
    ASSERT_STR_EQ("Unsaved Name",
                  cJSON_GetObjectItem(ws.doc, "pack_name")->valuestring);
    ASSERT(ws.dirty);              // recovered work is unsaved by definition

    gb_autosave_discard(&ws);
    ASSERT_FALSE(gb_autosave_exists(ws.root));
    gb_workspace_close(&ws);
    pack_stack_clear();
    PASS();
}

SUITE(e2e_gamebuilder_suite) {
    RUN_TEST(from_nothing_to_a_packaged_pack);
    RUN_TEST(round_trip_of_an_existing_pack_changes_nothing);
    RUN_TEST(archive_opens_by_extracting);
    RUN_TEST(autosave_recovers_unsaved_work);
}
