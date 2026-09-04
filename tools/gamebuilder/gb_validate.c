// Validation (GB-300..GB-305). Three tiers, all advisory: nothing blocks
// packaging (GB-320). Every finding names the file, the field or the
// coordinate, and what to do about it -- a finding the author cannot act on is
// worse than no finding, because it costs attention and returns nothing.
//
// This subsumes tools/mapcheck.py, including the two rules that file got wrong
// before the shipped pack corrected them: enclosed water is harmless unless a
// town dock sits on it, and an island needs a coastline rather than its own
// dock.

#include "gb.h"

#include "tile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add(GbFindings *F, GbTier tier, const char *where,
                const char *fmt, ...) {
    if (F->count >= GB_MAX_FINDINGS) { F->overflow++; return; }
    GbFinding *f = &F->item[F->count++];
    f->tier = tier;
    snprintf(f->where, sizeof f->where, "%s", where ? where : "");
    va_list ap; va_start(ap, fmt);
    vsnprintf(f->what, sizeof f->what, fmt, ap);
    va_end(ap);
    F->by_tier[tier]++;
}

static const char *str_of(cJSON *o, const char *k, const char *fb) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) ? v->valuestring : fb;
}
static int int_of(cJSON *o, const char *k, int fb) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : fb;
}

// A town's dock and gate are NESTED objects in the manifest -- boat:{x,y},
// gate:{x,y} -- which is how engine/resources.c reads them. Reading flat
// boat_x/boat_y instead silently matches nothing and every town passes.
static int sub_int(cJSON *o, const char *sub, const char *k, int fb) {
    cJSON *s = cJSON_GetObjectItem(o, sub);
    return cJSON_IsObject(s) ? int_of(s, k, fb) : fb;
}

// --- structural (GB-301) ------------------------------------------------------

static void check_structural(GbFindings *F, GbWorkspace *ws) {
    cJSON *doc = ws->doc;
    const char *required[] = { "pack_id", "pack_name", "render", "world", "time",
                               "economy", "combat", "sprites", "tile_codes",
                               "troops", "spells", "artifacts", "villains",
                               "classes", "castles", "towns", "zones" };
    for (unsigned i = 0; i < sizeof required / sizeof *required; i++)
        if (!cJSON_GetObjectItem(doc, required[i]))
            add(F, GB_TIER_STRUCTURAL, "game.json",
                "Missing required top-level key '%s'.", required[i]);

    if (!ws->res_valid)
        add(F, GB_TIER_STRUCTURAL, "game.json",
            "The engine could not parse this pack. Fix the errors above, then "
            "reopen.");

    // Catalog caps are compile-time in the engine; exceeding one means the
    // engine silently drops entries, so name the constant (GB-223).
    struct { const char *key; int cap; const char *konst; } caps[] = {
        { "troops",    32, "CAT_TROOPS_MAX"    },
        { "spells",    32, "CAT_SPELLS_MAX"    },
        { "artifacts", 16, "CAT_ARTIFACTS_MAX" },
        { "villains",  32, "CAT_VILLAINS_MAX"  },
        { "classes",    8, "CAT_CLASSES_MAX"   },
        { "castles",   32, "RES_MAX_CASTLES"   },
        { "towns",     32, "RES_MAX_TOWNS"     },
        { "zones",      8, "RES_MAX_ZONES"     },
    };
    for (unsigned i = 0; i < sizeof caps / sizeof *caps; i++) {
        cJSON *a = cJSON_GetObjectItem(doc, caps[i].key);
        if (cJSON_IsArray(a) && cJSON_GetArraySize(a) > caps[i].cap)
            add(F, GB_TIER_STRUCTURAL, caps[i].key,
                "%d entries exceeds the engine's cap of %d (%s). The extras "
                "will be dropped at load.",
                cJSON_GetArraySize(a), caps[i].cap, caps[i].konst);
    }
}

// --- referential (GB-302) -----------------------------------------------------

static bool catalog_has_id(cJSON *doc, const char *cat, const char *id) {
    cJSON *a = cJSON_GetObjectItem(doc, cat);
    if (!cJSON_IsArray(a) || !id || !*id) return false;
    cJSON *e = NULL;
    cJSON_ArrayForEach(e, a)
        if (strcmp(str_of(e, "id", ""), id) == 0) return true;
    return false;
}

static void check_referential(GbFindings *F, GbWorkspace *ws) {
    cJSON *doc = ws->doc;

    int homes = 0;
    cJSON *zones = cJSON_GetObjectItem(doc, "zones");
    cJSON *z = NULL;
    cJSON_ArrayForEach(z, zones) {
        const char *zid = str_of(z, "id", "?");
        if (cJSON_IsTrue(cJSON_GetObjectItem(z, "is_home"))) homes++;
        cJSON *nb = cJSON_GetObjectItem(z, "neighbors");
        cJSON *n = NULL;
        cJSON_ArrayForEach(n, nb) {
            if (cJSON_IsString(n) &&
                !catalog_has_id(doc, "zones", n->valuestring))
                add(F, GB_TIER_REFERENTIAL, zid,
                    "Sails to '%s', which is not a zone in this pack.",
                    n->valuestring);
        }
    }
    if (homes != 1)
        add(F, GB_TIER_REFERENTIAL, "zones",
            "%d zones are marked is_home; the engine needs exactly one.", homes);

    // A villain's host castle is drawn from its own zone, and salt_villains
    // gives up if it cannot find a free one (REQ-300).
    cJSON *vs = cJSON_GetObjectItem(doc, "villains");
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, vs) {
        const char *vz = str_of(v, "zone", "");
        if (*vz && !catalog_has_id(doc, "zones", vz))
            add(F, GB_TIER_REFERENTIAL, str_of(v, "id", "villain"),
                "Lives in zone '%s', which does not exist.", vz);
    }
    cJSON_ArrayForEach(z, zones) {
        const char *zid = str_of(z, "id", "?");
        int nv = 0, nc = 0;
        cJSON_ArrayForEach(v, vs)
            if (strcmp(str_of(v, "zone", ""), zid) == 0) nv++;
        cJSON *cs = cJSON_GetObjectItem(doc, "castles"), *c = NULL;
        cJSON_ArrayForEach(c, cs)
            if (strcmp(str_of(c, "zone", ""), zid) == 0) nc++;
        if (nv > 0 && nc <= nv)
            add(F, GB_TIER_REFERENTIAL, zid,
                "%d villains but only %d castles. Villain placement needs more "
                "castles than villains, with margin, or some will fail to "
                "place.", nv, nc);
    }

    // The puzzle grid is 5x5 and is filled by villains plus artifacts.
    int nv = cJSON_IsArray(vs) ? cJSON_GetArraySize(vs) : 0;
    cJSON *ar = cJSON_GetObjectItem(doc, "artifacts");
    int na = cJSON_IsArray(ar) ? cJSON_GetArraySize(ar) : 0;
    if (nv + na != 25)
        add(F, GB_TIER_REFERENTIAL, "puzzle",
            "%d villains + %d artifacts = %d. The puzzle map is 5x5 and needs "
            "exactly 25.", nv, na, nv + na);

    // Troop references inside villain armies.
    cJSON_ArrayForEach(v, vs) {
        cJSON *army = cJSON_GetObjectItem(v, "army"), *st = NULL;
        cJSON_ArrayForEach(st, army) {
            const char *tid = str_of(st, "troop", "");
            if (*tid && !catalog_has_id(doc, "troops", tid))
                add(F, GB_TIER_REFERENTIAL, str_of(v, "id", "villain"),
                    "Fields '%s', which is not a troop in this pack.", tid);
        }
    }
}

// --- spatial (GB-303) ---------------------------------------------------------

typedef struct { int w, h; unsigned char *terr; } Grid;

static bool flood_reaches_edge(const Grid *g, unsigned char want,
                               int sx, int sy, unsigned char *seen) {
    int *stack = malloc(sizeof(int) * 2 * g->w * g->h);
    int top = 0, touches = 0;
    stack[top * 2] = sx; stack[top * 2 + 1] = sy; top++;
    seen[sy * g->w + sx] = 1;
    while (top > 0) {
        top--;
        int x = stack[top * 2], y = stack[top * 2 + 1];
        if (x == 0 || y == 0 || x == g->w - 1 || y == g->h - 1) touches = 1;
        const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4; i++) {
            int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || ny < 0 || nx >= g->w || ny >= g->h) continue;
            if (seen[ny * g->w + nx]) continue;
            if (g->terr[ny * g->w + nx] != want) continue;
            seen[ny * g->w + nx] = 1;
            stack[top * 2] = nx; stack[top * 2 + 1] = ny; top++;
        }
    }
    free(stack);
    return touches;
}

static void check_zone_spatial(GbFindings *F, GbWorkspace *ws,
                               const MapGrid *mg, const char *zid) {
    Grid g = { mg->w, mg->h, malloc((size_t)mg->w * mg->h) };
    for (int y = 0; y < mg->h; y++)
        for (int x = 0; x < mg->w; x++)
            g.terr[y * g.w + x] = (unsigned char)mg->cell[y][x].terrain;

    // The boat trap: a dock on water that cannot reach the open sea. Enclosed
    // water on its own is harmless -- boats only ever spawn at a town dock --
    // which is why this checks docks and not ponds.
    unsigned char *seen = calloc((size_t)g.w * g.h, 1);
    cJSON *towns = cJSON_GetObjectItem(ws->doc, "towns"), *t = NULL;
    cJSON_ArrayForEach(t, towns) {
        if (strcmp(str_of(t, "zone", ""), zid) != 0) continue;
        int bx = sub_int(t, "boat", "x", -1);
        int by = sub_int(t, "boat", "y", -1);
        if (bx < 0 || by < 0 || bx >= g.w || by >= g.h) continue;
        if (g.terr[by * g.w + bx] != TERRAIN_WATER) {
            add(F, GB_TIER_SPATIAL, str_of(t, "id", "town"),
                "Its boat dock at (%d,%d) is not on water.", bx, by);
            continue;
        }
        memset(seen, 0, (size_t)g.w * g.h);
        if (!flood_reaches_edge(&g, TERRAIN_WATER, bx, by, seen))
            add(F, GB_TIER_SPATIAL, str_of(t, "id", "town"),
                "BOAT TRAP: its dock at (%d,%d) sits on water with no route to "
                "open sea. A boat rented here can never leave, and the rental "
                "is charged every week.", bx, by);
    }
    free(seen);

    // Castle footprints must fit and not overlap (REQ-228).
    cJSON *castles = cJSON_GetObjectItem(ws->doc, "castles"), *c = NULL;
    cJSON_ArrayForEach(c, castles) {
        if (strcmp(str_of(c, "zone", ""), zid) != 0) continue;
        int gx = sub_int(c, "gate", "x", int_of(c, "gate_x", int_of(c, "x", -1)));
        int gy = sub_int(c, "gate", "y", int_of(c, "gate_y", int_of(c, "y", -1)));
        if (gx < 0 || gy < 0) continue;
        bool single = gb_castle_is_single(c);
        int fx, fy, fw, fh;
        gb_castle_footprint(single, gx, gy, &fx, &fy, &fw, &fh);
        if (fx < 0 || fy < 0 || fx + fw > g.w || fy + fh > g.h)
            add(F, GB_TIER_SPATIAL, str_of(c, "id", "castle"),
                "Its %s footprint at (%d,%d) runs off the edge of the map.",
                single ? "1x1" : "3x2", gx, gy);
    }

    // The salt budget draws from chest placeholders (REQ-231).
    cJSON *zones = cJSON_GetObjectItem(ws->doc, "zones"), *z = NULL;
    cJSON_ArrayForEach(z, zones) {
        if (strcmp(str_of(z, "id", ""), zid) != 0) continue;
        cJSON *chests = cJSON_GetObjectItem(z, "chests");
        int have = cJSON_IsArray(chests) ? cJSON_GetArraySize(chests) : 0;
        cJSON *salt = cJSON_GetObjectItem(z, "salt");
        int need = 0;
        if (salt) {
            const char *k[] = { "artifacts", "navmaps", "orbs", "telecaves",
                                "dwellings", "friendly_foes" };
            for (unsigned i = 0; i < sizeof k / sizeof *k; i++)
                need += int_of(salt, k[i], 0);
        }
        if (have < need)
            add(F, GB_TIER_SPATIAL, zid,
                "%d chest placeholders but the salt budget needs %d. Artifacts, "
                "orbs, dwellings and friendly foes are all drawn from chest "
                "tiles, so some will not be placed.", have, need);
        break;
    }
    free(g.terr);
}

// --- public -------------------------------------------------------------------

void gb_validate(GbFindings *F, GbWorkspace *ws,
                 MapGrid *const *grids, const bool *loaded, int nzones) {
    memset(F, 0, sizeof *F);
    if (!ws || !ws->open || !ws->doc) {
        add(F, GB_TIER_STRUCTURAL, "", "Nothing is open.");
        return;
    }
    check_structural(F, ws);
    check_referential(F, ws);
    for (int i = 0; i < nzones && i < GB_MAX_ZONES; i++) {
        if (!loaded[i] || !grids[i]) continue;
        const char *zid = gb_zone_id_at(ws->doc, i);
        if (zid) check_zone_spatial(F, ws, grids[i], zid);
    }
}

const char *gb_tier_name(GbTier t) {
    switch (t) {
        case GB_TIER_STRUCTURAL:  return "Structural";
        case GB_TIER_REFERENTIAL: return "Referential";
        case GB_TIER_SPATIAL:     return "Spatial";
        default:                  return "?";
    }
}
