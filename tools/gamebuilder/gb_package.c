// Packaging (GB-320..GB-324) and the new-pack generator (GB-400).
//
// Nothing blocks packaging. Validation reports; the author decides. A pack
// built with outstanding findings is the author's call to make, and refusing
// would only teach them to stop looking at the findings.

#include "gb.h"

#include "pack.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define GB_SEP '\\'
#  define gb_mkdir(p) _mkdir(p)
#else
#  define GB_SEP '/'
#  define gb_mkdir(p) mkdir((p), 0777)
#endif

bool gb_package(GbWorkspace *ws, const char *out_zip, char *err, size_t errsz) {
    if (!ws || !ws->open) {
        snprintf(err, errsz, "Nothing is open.");
        return false;
    }
    if (ws->dirty) {
        // Packaging the on-disk pack while the DOM holds newer edits would
        // ship a version the author never saw. Save first, deliberately.
        snprintf(err, errsz,
                 "There are unsaved changes.\n\nSave the pack before packaging, "
                 "so the archive matches what you have been editing.");
        return false;
    }
    if (!pack_zip_dir(ws->root, out_zip)) {
        snprintf(err, errsz,
                 "Could not write:\n%s\n\nIs the destination writable?", out_zip);
        return false;
    }
    return true;
}

// --- new pack ------------------------------------------------------------------
//
// The skeleton is GENERATED, not copied from kings-bounty, because that pack is
// DOS-extracted and copyright-restricted (GB-401). The engine ships NO string
// fallbacks -- every user-facing key must be present or the pack hard-fails at
// load -- so the skeleton carries the full strings manifest with literal
// placeholder values (the key name), which the author then replaces.

static bool write_text(const char *path, const char *text, char *err,
                       size_t errsz) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(err, errsz, "Could not write:\n%s", path);
        return false;
    }
    fputs(text, f);
    fclose(f);
    return true;
}

#include "starter_strings.inc"

bool gb_newpack_create(const char *dir, char *err, size_t errsz) {
    struct stat st;
    char manifest[GB_PATH_MAX * 2];
    snprintf(manifest, sizeof manifest, "%s%cgame.json", dir, GB_SEP);
    if (stat(manifest, &st) == 0) {
        snprintf(err, errsz,
                 "That folder already contains a game.json.\n\n"
                 "Choose an empty folder, or open the existing pack instead.");
        return false;
    }
    gb_mkdir(dir);
    char sub[GB_PATH_MAX * 2];
    const char *dirs[] = { "art", "art/tiles", "art/troops", "art/villains",
                           "art/sprites", "art/ui", "art/combat", "art/classes",
                           "art/font", "audio", "maps", "palettes" };
    for (unsigned i = 0; i < sizeof dirs / sizeof *dirs; i++) {
        snprintf(sub, sizeof sub, "%s%c%s", dir, GB_SEP, dirs[i]);
        for (char *p = sub; *p; p++) if (*p == '/') *p = GB_SEP;
        gb_mkdir(sub);
    }

    // A 24x24 island: enough to walk around, with one castle's worth of flat
    // ground. Base terrain only -- the editor furnishes it on first save.
    char map[24 * 25 + 64];
    int at = 0;
    at += snprintf(map + at, sizeof map - at,
                   "# starter -- 24x24. Painted in GameBuilder.\n");
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 24; x++) {
            bool edge = (x < 3 || y < 3 || x > 20 || y > 20);
            map[at++] = edge ? '~' : '.';
        }
        map[at++] = '\n';
    }
    map[at] = 0;
    snprintf(sub, sizeof sub, "%s%cmaps%cstarter.dat", dir, GB_SEP, GB_SEP);
    if (!write_text(sub, map, err, errsz)) return false;

    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "pack_id", "new-pack");
    cJSON_AddStringToObject(doc, "pack_name", "New Pack");
    cJSON_AddStringToObject(doc, "pack_kind", "base");
    cJSON_AddStringToObject(doc, "title", "New Pack");
    cJSON_AddNumberToObject(doc, "version", 1);

    cJSON *world = cJSON_AddObjectToObject(doc, "world");
    cJSON_AddNumberToObject(world, "fog_sight", 3);
    cJSON_AddStringToObject(world, "starting_zone", "starter");
    cJSON_AddStringToObject(world, "zone_noun", "zone_noun");
    cJSON_AddStringToObject(world, "zone_noun_plural", "zone_noun_plural");
    cJSON_AddStringToObject(world, "default_name", "default_name");
    cJSON *time_ = cJSON_AddObjectToObject(doc, "time");
    cJSON_AddNumberToObject(time_, "day_steps", 40);
    cJSON_AddNumberToObject(time_, "week_days", 5);
    cJSON *dpd = cJSON_AddArrayToObject(time_, "days_per_difficulty");
    int days[4] = { 900, 600, 400, 200 };
    for (int i = 0; i < 4; i++) cJSON_AddItemToArray(dpd, cJSON_CreateNumber(days[i]));

    cJSON *econ = cJSON_AddObjectToObject(doc, "economy");
    cJSON_AddNumberToObject(econ, "boat_cost_normal", 500);
    cJSON_AddNumberToObject(econ, "boat_cost_cheap", 100);
    cJSON_AddNumberToObject(econ, "siege_cost", 3000);
    cJSON_AddNumberToObject(econ, "alcove_cost", 5000);

    cJSON *combat = cJSON_AddObjectToObject(doc, "combat");
    cJSON *chart = cJSON_AddArrayToObject(combat, "morale_chart");
    const char *rows[5] = { "NNNNN", "NNNNN", "NNHNN", "LNLHN", "LLLNN" };
    for (int i = 0; i < 5; i++)
        cJSON_AddItemToArray(chart, cJSON_CreateString(rows[i]));

    // Tile codes: the five base terrains. Edge variants are added by the
    // author when they supply the art; the furnish pass only uses what exists.
    cJSON *tc = cJSON_AddObjectToObject(doc, "tile_codes");
    struct { const char *code, *name, *terr; bool blocks; } t[] = {
        { ".", "grass",    "grass",    false },
        { "F", "forest",   "forest",   true  },
        { "^", "mountain", "mountain", true  },
        { "~", "water",    "water",    false },
        { "D", "desert",   "desert",   false },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        cJSON *e = cJSON_AddObjectToObject(tc, t[i].code);
        cJSON_AddStringToObject(e, "name", t[i].name);
        cJSON_AddStringToObject(e, "terrain", t[i].terr);
        cJSON_AddBoolToObject(e, "blocks_foot", t[i].blocks);
        cJSON_AddStringToObject(e, "art", t[i].name);
    }

    // One troop, so an army can exist at all; one class to play it.
    cJSON *troops = cJSON_AddArrayToObject(doc, "troops");
    cJSON *tr = cJSON_CreateObject();
    cJSON_AddNumberToObject(tr, "index", 0);
    cJSON_AddStringToObject(tr, "id", "militia");
    cJSON_AddStringToObject(tr, "name", "Militia");
    cJSON_AddNumberToObject(tr, "skill_level", 2);
    cJSON_AddNumberToObject(tr, "hit_points", 2);
    cJSON_AddNumberToObject(tr, "move_rate", 2);
    cJSON *ml = cJSON_AddArrayToObject(tr, "melee");
    cJSON_AddItemToArray(ml, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(ml, cJSON_CreateNumber(2));
    cJSON *rg = cJSON_AddArrayToObject(tr, "ranged");
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(rg, cJSON_CreateNumber(0));
    cJSON_AddNumberToObject(tr, "recruit_cost", 50);
    cJSON_AddNumberToObject(tr, "spoils_factor", 5);
    cJSON_AddStringToObject(tr, "abilities", "");
    cJSON_AddStringToObject(tr, "dwelling", "castle");
    cJSON_AddNumberToObject(tr, "max_population", 0);
    cJSON_AddNumberToObject(tr, "growth_per_week", 5);
    cJSON_AddStringToObject(tr, "morale_group", "A");
    cJSON_AddItemToArray(troops, tr);

    cJSON *classes = cJSON_AddArrayToObject(doc, "classes");
    cJSON *cl = cJSON_CreateObject();
    cJSON_AddNumberToObject(cl, "index", 0);
    cJSON_AddStringToObject(cl, "id", "hero");
    cJSON_AddStringToObject(cl, "name", "Hero");
    cJSON_AddNumberToObject(cl, "starting_gold", 5000);
    cJSON *ranks = cJSON_AddArrayToObject(cl, "ranks");
    cJSON *rk = cJSON_CreateObject();
    cJSON_AddStringToObject(rk, "id", "hero");
    cJSON_AddStringToObject(rk, "name", "Hero");
    cJSON_AddNumberToObject(rk, "villains_needed", 0);
    cJSON_AddNumberToObject(rk, "leadership", 100);
    cJSON_AddNumberToObject(rk, "max_spells", 2);
    cJSON_AddNumberToObject(rk, "spell_power", 1);
    cJSON_AddNumberToObject(rk, "commission", 1000);
    cJSON_AddItemToArray(ranks, rk);
    cJSON_AddItemToArray(classes, cl);

    cJSON_AddArrayToObject(doc, "spells");
    cJSON_AddArrayToObject(doc, "artifacts");
    cJSON_AddArrayToObject(doc, "villains");
    cJSON_AddArrayToObject(doc, "castles");
    cJSON_AddArrayToObject(doc, "towns");
    cJSON_AddObjectToObject(doc, "sprites");

    cJSON *zones = cJSON_AddArrayToObject(doc, "zones");
    cJSON *z = cJSON_CreateObject();
    cJSON_AddStringToObject(z, "id", "starter");
    cJSON_AddStringToObject(z, "name", "Starter Isle");
    cJSON_AddStringToObject(z, "map", "maps/starter.dat");
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

    cJSON_AddItemToObject(doc, "strings", gb_placeholder_strings());

    char *text = cJSON_Print(doc);
    cJSON_Delete(doc);
    bool ok = write_text(manifest, text, err, errsz);
    cJSON_free(text);
    if (!ok) return false;

    // A README, because a folder of empty art directories tells the author
    // nothing about what to put in them.
    snprintf(sub, sizeof sub, "%s%cREADME.txt", dir, GB_SEP);
    write_text(sub,
        "This is a new OpenBounty pack, created by GameBuilder.\n\n"
        "It loads and runs, but it has almost nothing in it yet:\n"
        "  - one zone (Starter Isle, 24x24)\n"
        "  - one troop, one class\n"
        "  - no art -- the engine will show nothing until you add it\n\n"
        "Art goes under art/, at the sizes given in docs/ART-SPEC.md.\n"
        "Tiles are 48x34. Use the Art tab in GameBuilder to import and check\n"
        "them.\n", err, errsz);
    return true;
}
