// The completeness checklist (GB-403).
//
// A new pack loads immediately, which is the point -- but "loads" and
// "finished" are very far apart, and the gap is where a first-time author gets
// lost. This answers "what does this pack still need?" in the order it is worth
// doing, rather than leaving them to infer it from an empty folder tree.
//
// Deliberately separate from validation: validation says what is WRONG,
// this says what is MISSING. A pack with an empty villain list is not broken.

#include "gb.h"

#include <stdio.h>
#include <string.h>

static int arr_len(cJSON *doc, const char *key) {
    cJSON *a = cJSON_GetObjectItem(doc, key);
    return cJSON_IsArray(a) ? cJSON_GetArraySize(a) : 0;
}

static void item(GbChecklist *C, const char *what, bool done, const char *hint) {
    if (C->count >= GB_MAX_CHECKS) return;
    GbCheckItem *c = &C->item[C->count++];
    snprintf(c->what, sizeof c->what, "%s", what);
    snprintf(c->hint, sizeof c->hint, "%s", hint ? hint : "");
    c->done = done;
    if (done) C->done_count++;
}

void gb_checklist_build(GbChecklist *C, GbWorkspace *ws,
                        MapGrid *const *grids, const bool *loaded, int nzones) {
    memset(C, 0, sizeof *C);
    if (!ws || !ws->open || !ws->doc) return;
    cJSON *doc = ws->doc;

    item(C, "Pack has a name and id",
         cJSON_GetObjectItem(doc, "pack_id") && cJSON_GetObjectItem(doc, "pack_name"),
         "Catalog tab, or the raw JSON view");

    int zones = arr_len(doc, "zones");
    item(C, "At least one zone", zones > 0, "Maps tab");

    // A zone with no objects is a walkable emptiness -- playable, but there is
    // nothing to do in it.
    int placed = 0;
    for (int i = 0; i < nzones && i < GB_MAX_ZONES; i++) {
        const char *zid = gb_zone_id_at(doc, i);
        if (!zid) continue;
        GbObjectList L;
        gb_objects_collect(&L, doc, zid);
        placed += L.count;
    }
    item(C, "Objects placed on the maps", placed > 3,
         "Objects tab -- towns, castles, chests give the player something to do");

    item(C, "Troops defined", arr_len(doc, "troops") >= 5,
         "Catalog > troops. Five is enough for one dwelling family");
    item(C, "Classes defined", arr_len(doc, "classes") >= 1,
         "Catalog > classes -- the player needs someone to be");
    item(C, "Spells defined", arr_len(doc, "spells") >= 1, "Catalog > spells");
    item(C, "Villains defined", arr_len(doc, "villains") >= 1,
         "Catalog > villains -- without them there are no contracts");
    item(C, "Artifacts defined", arr_len(doc, "artifacts") >= 1,
         "Catalog > artifacts");

    // The puzzle grid is 5x5 and villains + artifacts must fill it exactly.
    int nv = arr_len(doc, "villains"), na = arr_len(doc, "artifacts");
    item(C, "Puzzle grid adds up (villains + artifacts = 25)", nv + na == 25,
         "Catalog -- the puzzle view is 5x5 and needs exactly 25 cells");

    item(C, "Castles placed", arr_len(doc, "castles") >= 1, "Objects tab");
    item(C, "Towns placed", arr_len(doc, "towns") >= 1,
         "Objects tab -- towns sell spells and issue contracts");

    // Art is the long pole and the checklist should say so plainly.
    cJSON *tc = cJSON_GetObjectItem(doc, "tile_codes");
    int codes = 0;
    for (cJSON *c = tc ? tc->child : NULL; c; c = c->next) codes++;
    item(C, "Tile codes declared", codes >= 5,
         "At least the five base terrains");
    item(C, "Tile art present", ws->res_valid && codes >= 5,
         "Art tab -- 48x34 PNGs under art/tiles/. See docs/ART-SPEC.md");

    cJSON *strings = cJSON_GetObjectItem(doc, "strings");
    item(C, "Strings written", cJSON_IsObject(strings),
         "Strings tab -- the engine falls back to English defaults without them");

    bool any_map = false;
    for (int i = 0; i < nzones && i < GB_MAX_ZONES; i++)
        if (loaded[i] && grids[i] && grids[i]->w > 0) any_map = true;
    item(C, "Maps painted", any_map, "Maps tab");

    item(C, "Winnability checked", false,
         "Validate tab -- run the oracle before you ship");
}
