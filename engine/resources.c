#include "resources.h"
#include "map.h"
#include "cJSON.h"
#include "assets_bytes.h"   // LoadAssetBytes / UnloadAssetBytes
#include "pack.h"
#include "tile.h"
#include "combat.h"     // COMBAT_W / COMBAT_H for the siege grid
#include "resources_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

// ---- Small helpers ---------------------------------------------------------

void res_copy_str(char *dst, size_t dst_sz, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < dst_sz && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void resources_resolve_path(const Resources *res, const char *rel,
                            char *out, size_t cap) {
    // Pack-relative paths resolve directly against the global pack stack --
    // there is no per-Resources prefix to apply, so this is an identity copy.
    // It stays as a named seam: callers ask for a resolved path and do not
    // need to know that resolution is currently a no-op.
    (void)res;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!rel || !rel[0]) return;
    snprintf(out, cap, "%s", rel);
}

int res_json_int(const cJSON *obj, const char *key, int fallback) {
    if (!obj) return fallback;
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(v)) return v->valueint;
    return fallback;
}

const char *res_json_str(const cJSON *obj, const char *key,
                            const char *fallback) {
    if (!obj) return fallback;
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v) && v->valuestring) return v->valuestring;
    return fallback;
}

// Read a fixed-length int array under `key`. Missing keys / wrong types leave
// `out[]` untouched, so callers prime it with defaults before calling.
void res_json_int_array(const cJSON *obj, const char *key,
                           int *out, int n) {
    if (!obj) return;
    cJSON *arr = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsArray(arr)) return;
    int i = 0;
    cJSON *v;
    cJSON_ArrayForEach(v, arr) {
        if (i >= n) break;
        if (cJSON_IsNumber(v)) out[i] = v->valueint;
        i++;
    }
}

// Parse "#RRGGBB" or "#AARRGGBB" into a packed 0xAARRGGBB. Returns
// `fallback` on any parse failure. Alpha defaults to 0xFF when absent.
unsigned int res_parse_color_hex(const char *s, unsigned int fallback) {
    if (!s || s[0] != '#') return fallback;
    const char *p = s + 1;
    size_t len = 0;
    while (p[len]) len++;
    if (len != 6 && len != 8) return fallback;
    unsigned int v = 0;
    for (size_t i = 0; i < len; i++) {
        char c = p[i];
        unsigned int d;
        if (c >= '0' && c <= '9')      d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else return fallback;
        v = (v << 4) | d;
    }
    if (len == 6) v |= 0xFF000000u;
    return v;
}

// Read a hex color string under `key`. Missing/malformed leaves `*out` unchanged.
void res_json_color(const cJSON *obj, const char *key, unsigned int *out) {
    if (!obj || !out) return;
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring) return;
    *out = res_parse_color_hex(v->valuestring, *out);
}

// Singleton pointer so tables.h lookup functions (troop_by_id, spell_by_id,
// ...) can read catalog data without threading a Resources* through every
// call site. Set by resources_load(); cleared by resources_free().
static const Resources *g_resources = NULL;
// Bumped whenever g_resources changes, so a lookup memo (tables.c) knows its
// catalog is stale even when a new pack's tables land at the old address.
static unsigned g_resources_generation = 0;

unsigned resources_generation(void) { return g_resources_generation; }

const Resources *resources_current(void) { return g_resources; }

// Locale override for the next load (set from the --lang CLI flag). Empty
// means "use the pack's base locale (world.language)".
static char g_locale_override[RES_ID_LEN];
void resources_set_locale(const char *lang) {
    if (lang && lang[0]) snprintf(g_locale_override, sizeof g_locale_override, "%s", lang);
    else g_locale_override[0] = '\0';
}

// Read a whole file from `path` (via assets.c so packaged builds work too).
// Returns a heap-owned NUL-terminated buffer; caller frees.
char *res_slurp(const char *path) {
    size_t size = 0;
    const unsigned char *data = LoadAssetBytes(path, &size);
    if (!data) return NULL;
    char *buf = (char *)malloc(size + 1);
    if (!buf) { UnloadAssetBytes(data); return NULL; }
    memcpy(buf, data, size);
    buf[size] = '\0';
    UnloadAssetBytes(data);
    return buf;
}

// A JSON array's or object's entry count (0 when it is neither).
int res_json_len(const cJSON *j) {
    return (cJSON_IsArray(j) || cJSON_IsObject(j)) ? cJSON_GetArraySize(j) : 0;
}

// ---- In-place section parsers ---------------------------------------------
// Each parses a JSON array nested inside the single game.json root object.

static void parse_anim_set(cJSON *obj, ResAnimSet *out);

static void parse_towns(Resources *res, cJSON *arr) {
    int cap = res_json_len(arr);
    if (!RES_TABLE_ALLOC(res->towns, res->town_count, cap)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->town_count >= cap) break;
        ResTown *t = &res->towns[res->town_count++];
        t->index = res_json_int(it, "index", -1);
        res_copy_str(t->id,   sizeof(t->id),   res_json_str(it, "id", ""));
        res_copy_str(t->name, sizeof(t->name), res_json_str(it, "name", ""));
        res_copy_str(t->zone, sizeof(t->zone), res_json_str(it, "zone", ""));
        t->x = res_json_int(it, "x", -1);
        t->y = res_json_int(it, "y", -1);

        cJSON *boat = cJSON_GetObjectItem(it, "boat");
        t->boat_x = res_json_int(boat, "x", -1);
        t->boat_y = res_json_int(boat, "y", -1);

        cJSON *gate = cJSON_GetObjectItem(it, "gate");
        t->gate_x = res_json_int(gate, "x", -1);
        t->gate_y = res_json_int(gate, "y", -1);

        res_copy_str(t->intel_castle, sizeof(t->intel_castle),
                 res_json_str(it, "intel_castle", ""));
        res_copy_str(t->pinned_spell, sizeof(t->pinned_spell),
                 res_json_str(it, "pinned_spell", ""));
        // Optional per-town art (a bare stem under art/objects/). Absent means
        // map_art's shared town tile, so older packs stamp as before.
        res_copy_str(t->art, sizeof(t->art), res_json_str(it, "art", ""));
        res_copy_str(t->informant, sizeof(t->informant), res_json_str(it, "informant", ""));
        res_copy_str(t->headman, sizeof(t->headman), res_json_str(it, "headman", ""));
        res_copy_str(t->townhead, sizeof(t->townhead), res_json_str(it, "townhead", ""));
        res_copy_str(t->invitations, sizeof(t->invitations), res_json_str(it, "invitations", ""));
        // A town whose informant reports on the sacred artifacts, not a castle.
        t->intel_artifact = cJSON_IsTrue(cJSON_GetObjectItem(it, "intel_artifact"));
        res_copy_str(t->backdrop, sizeof(t->backdrop), res_json_str(it, "backdrop", ""));
    }
}

static void parse_castles(Resources *res, cJSON *arr) {
    int cap = res_json_len(arr);
    if (!RES_TABLE_ALLOC(res->castles, res->castle_count, cap)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->castle_count >= cap) break;
        ResCastle *c = &res->castles[res->castle_count++];
        c->index = res_json_int(it, "index", -1);
        res_copy_str(c->id,   sizeof(c->id),   res_json_str(it, "id", ""));
        res_copy_str(c->name, sizeof(c->name), res_json_str(it, "name", ""));
        res_copy_str(c->zone, sizeof(c->zone), res_json_str(it, "zone", ""));
        c->x = res_json_int(it, "x", -1);
        c->y = res_json_int(it, "y", -1);
        // Default castle gate: tile directly south of the castle (matches
        //  convention used for Town Gate landings).
        c->gate_x = (c->x >= 0) ? c->x : -1;
        c->gate_y = (c->y >= 0) ? c->y + 1 : -1;
        cJSON *gate = cJSON_GetObjectItem(it, "gate");
        if (cJSON_IsObject(gate)) {
            c->gate_x = res_json_int(gate, "x", c->gate_x);
            c->gate_y = res_json_int(gate, "y", c->gate_y);
        }
        c->difficulty_tier = res_json_int(it, "difficulty_tier", 0);
        res_copy_str(c->art, sizeof(c->art), res_json_str(it, "art", ""));
        // Map footprint (REQ-228): absent means the classic 3x2 stamp.
        {
            const char *fp = res_json_str(it, "footprint", "");
            if (!resources_parse_castle_footprint(fp, &c->footprint) && fp[0])
                fprintf(stdout, "resources: castle '%s' footprint '%s' unknown, "
                        "using 3x2\n", c->id, fp);
        }

        memset(&c->special, 0, sizeof(c->special));
        cJSON *sp = cJSON_GetObjectItem(it, "special");
        if (cJSON_IsObject(sp)) {
            res_copy_str(c->special.flow, sizeof(c->special.flow),
                     res_json_str(sp, "flow", ""));
            cJSON *es = cJSON_GetObjectItem(sp, "excluded_from_siege");
            c->special.excluded_from_siege = cJSON_IsTrue(es);
            cJSON *ei = cJSON_GetObjectItem(sp, "excluded_from_intel");
            c->special.excluded_from_intel = cJSON_IsTrue(ei);
            cJSON *ec = cJSON_GetObjectItem(sp, "excluded_from_contract");
            c->special.excluded_from_contract = cJSON_IsTrue(ec);
            res_copy_str(c->special.win_condition,
                     sizeof(c->special.win_condition),
                     res_json_str(sp, "win_condition", ""));
            cJSON *dlg = cJSON_GetObjectItem(sp, "dialog");
            if (cJSON_IsObject(dlg)) {
                res_copy_str(c->special.dialog_header,
                         sizeof(c->special.dialog_header),
                         res_json_str(dlg, "header", ""));
                res_copy_str(c->special.dialog_body,
                         sizeof(c->special.dialog_body),
                         res_json_str(dlg, "body", ""));
            }
            res_copy_str(c->special.portrait, sizeof(c->special.portrait),
                     res_json_str(sp, "portrait", ""));
            res_copy_str(c->special.figure, sizeof(c->special.figure),
                     res_json_str(sp, "figure", ""));
            res_copy_str(c->special.barracks_portrait, sizeof(c->special.barracks_portrait),
                     res_json_str(sp, "barracks_portrait", ""));
            res_copy_str(c->special.barracks_figure, sizeof(c->special.barracks_figure),
                     res_json_str(sp, "barracks_figure", ""));
            res_copy_str(c->special.greeter_figure, sizeof(c->special.greeter_figure),
                     res_json_str(sp, "greeter_figure", ""));
            {
                cJSON *pr = cJSON_GetObjectItem(sp, "promotion");
                for (int r = 0; r < 4; r++) {
                    cJSON *e = cJSON_IsArray(pr) ? cJSON_GetArrayItem(pr, r) : NULL;
                    res_copy_str(c->special.promotion[r], sizeof(c->special.promotion[r]),
                             cJSON_IsString(e) ? e->valuestring : "");
                }
            }
            cJSON *au = cJSON_GetObjectItem(sp, "audience");
            if (cJSON_IsObject(au)) {
                res_copy_str(c->special.audience_intro,
                         sizeof(c->special.audience_intro),
                         res_json_str(au, "intro", ""));
                res_copy_str(c->special.audience_rank_up,
                         sizeof(c->special.audience_rank_up),
                         res_json_str(au, "rank_up", ""));
                res_copy_str(c->special.audience_more_needed,
                         sizeof(c->special.audience_more_needed),
                         res_json_str(au, "more_needed", ""));
                res_copy_str(c->special.audience_final_rank,
                         sizeof(c->special.audience_final_rank),
                         res_json_str(au, "final_rank", ""));
                res_copy_str(c->special.audience_blessing_granted, sizeof(c->special.audience_blessing_granted),
                         res_json_str(au, "blessing_granted", ""));
                res_copy_str(c->special.audience_blessing_needed, sizeof(c->special.audience_blessing_needed),
                         res_json_str(au, "blessing_needed", ""));
                res_copy_str(c->special.audience_blessing_already, sizeof(c->special.audience_blessing_already),
                         res_json_str(au, "blessing_already", ""));
                res_copy_str(c->special.audience_tribute_paid, sizeof(c->special.audience_tribute_paid),
                         res_json_str(au, "tribute_paid", ""));
                res_copy_str(c->special.audience_tribute_needed, sizeof(c->special.audience_tribute_needed),
                         res_json_str(au, "tribute_needed", ""));
            }
        }
    }
}

static Terrain terrain_from_name(const char *s) {
    if (!s) return TERRAIN_GRASS;
    if (strcmp(s, "grass")    == 0) return TERRAIN_GRASS;
    if (strcmp(s, "forest")   == 0) return TERRAIN_FOREST;
    if (strcmp(s, "mountain") == 0) return TERRAIN_MOUNTAIN;
    if (strcmp(s, "water")    == 0) return TERRAIN_WATER;
    if (strcmp(s, "desert")   == 0) return TERRAIN_DESERT;
    if (strcmp(s, "river")    == 0) return TERRAIN_RIVER;
    return TERRAIN_GRASS;
}

// A tile code is one raw byte of a map file. The key that names it is that
// byte itself for the 94 printable ASCII ones, and a two-digit hex escape
// "\xNN" for the rest -- 91 of the printable codes are already spoken for, and
// a raw byte above 127 cannot be written as a JSON key: cJSON turns a \u
// escape into UTF-8, so only a codepoint under 0x80 survives as a single byte
// (see utf16_literal_to_utf8). The escape keeps game.json plain ASCII and
// valid UTF-8 while still naming any of the 256 codes. A map file that uses
// one is no longer ASCII -- the reader is byte-wise, so such a file is latin-1
// (tools/romeart.py's map commands read them that way).
//
// Returns the code, or -1 if the key names none.
int resources_tile_code_from_key(const char *key) {
    if (!key || !key[0]) return -1;
    if (key[0] == '\\' && (key[1] == 'x' || key[1] == 'X') &&
        key[2] && key[3] && !key[4]) {
        int v = 0;
        for (int i = 2; i < 4; i++) {
            int c = (unsigned char)key[i], d;
            if      (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return -1;
            v = v * 16 + d;
        }
        return v;
    }
    if (key[1]) return -1;               // more than one character, not an escape
    return (unsigned char)key[0];
}

static void parse_tile_codes(Resources *res, cJSON *obj) {
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++) res->tile_codes[i].present = false;
    if (!cJSON_IsObject(obj)) return;
    cJSON *entry;
    cJSON_ArrayForEach(entry, obj) {
        int idx = resources_tile_code_from_key(entry->string);
        if (idx < 0 || idx >= RES_TILE_CODE_COUNT) continue;
        ResTileCode *tc = &res->tile_codes[idx];
        tc->present = true;
        res_copy_str(tc->art, sizeof(tc->art), res_json_str(entry, "art", ""));
        res_copy_str(tc->ground, sizeof(tc->ground), res_json_str(entry, "ground", ""));
        tc->terrain = (int)terrain_from_name(res_json_str(entry, "terrain", "grass"));
        cJSON *jbf = cJSON_GetObjectItem(entry, "blocks_foot");
        cJSON *jib = cJSON_GetObjectItem(entry, "is_bridge");
        tc->blocks_foot = cJSON_IsBool(jbf) && cJSON_IsTrue(jbf);
        tc->is_bridge   = cJSON_IsBool(jib) && cJSON_IsTrue(jib);
        tc->object      = cJSON_IsTrue(cJSON_GetObjectItem(entry, "object"));
        cJSON *jv = cJSON_GetObjectItem(entry, "variants");
        int v_cap = res_json_len(jv);
        if (cJSON_IsArray(jv) && RES_TABLE_ALLOC(tc->variants, tc->variant_count, v_cap)) {
            cJSON *v;
            cJSON_ArrayForEach(v, jv) {
                if (!cJSON_IsString(v) || !v->valuestring[0]) continue;
                if (tc->variant_count >= v_cap) break;
                res_copy_str(tc->variants[tc->variant_count], RES_TILE_ART_LEN, v->valuestring);
                tc->variant_count++;
            }
        }
    }
}

// The standard map art names: what a pack gets for any "map_art" entry it
// leaves out. No details, aprons or fills: a pack draws those only by naming
// them.
static const ResMapArt MAP_ART_STD = {
    .def = "grass", .cleared_water = "water",
    .ground = { [TERRAIN_GRASS] = "grass", [TERRAIN_FOREST] = "forest", [TERRAIN_MOUNTAIN] = "mountain",
                [TERRAIN_WATER] = "water", [TERRAIN_DESERT] = "desert" },
    .bridge = { { "bridge_ew", "bridge_ns" }, { "bridge_river_ew", "bridge_river_ns" } },
    .chest = "chest", .artifact_chest = "artifact_chest", .artifact_ring = "artifact_ring",
    .sign = "sign", .town = "town", .wandering_army = "wandering_army", .alcove = "dwelling_hills",
    .dwelling = { "dwelling_plains", "dwelling_forest", "dwelling_hills", "dwelling_dungeon" },
    .castle_3x2 = { "castle_tl", "castle_tm", "castle_tr", "castle_ml", "castle_gate", "castle_mr" },
    .castle_1x1 = "castle",
};

const ResMapArt *resources_map_art(const Resources *res) {
    return res ? &res->map_art : &MAP_ART_STD;
}

// `key` in `obj` into `dst` when it is a string (an empty one draws nothing);
// otherwise `dst` keeps its standard name.
static void map_art_str(const cJSON *obj, const char *key, char *dst) {
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v)) res_copy_str(dst, RES_TILE_ART_LEN, v->valuestring);
}

// A list of names under `key` into dst[cap]; returns how many.
static int map_art_list(const cJSON *obj, const char *key, char (*dst)[RES_TILE_ART_LEN], int cap) {
    int n = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItem(obj, key))
        if (cJSON_IsString(v) && v->valuestring[0] && n < cap)
            res_copy_str(dst[n++], RES_TILE_ART_LEN, v->valuestring);
    return n;
}

static void parse_map_art(Resources *res, const cJSON *obj) {
    ResMapArt *m = &res->map_art;
    *m = MAP_ART_STD;
    if (!cJSON_IsObject(obj)) return;
    static const char *const TERRAINS[TERRAIN_COUNT] = { "grass", "forest", "mountain", "water", "desert", "river" };
    static const char *const SIDES[4] = { "n", "e", "s", "w" };
    static const char *const CORNERS[4] = { "ne", "es", "sw", "nw" };
    static const char *const WOODS[2] = { "forest", "mountain" };
    map_art_str(obj, "default", m->def);
    map_art_str(obj, "cleared_water", m->cleared_water);
    const cJSON *jg = cJSON_GetObjectItem(obj, "terrain_ground");
    for (int t = 0; t < TERRAIN_COUNT; t++) map_art_str(jg, TERRAINS[t], m->ground[t]);
    const cJSON *jb = cJSON_GetObjectItem(obj, "bridges");
    map_art_str(jb, "ew", m->bridge[0][0]);
    map_art_str(jb, "ns", m->bridge[0][1]);
    map_art_str(jb, "river_ew", m->bridge[1][0]);
    map_art_str(jb, "river_ns", m->bridge[1][1]);
    const cJSON *jo = cJSON_GetObjectItem(obj, "objects");
    map_art_str(jo, "chest", m->chest);
    map_art_str(jo, "artifact_chest", m->artifact_chest);
    map_art_str(jo, "artifact_ring", m->artifact_ring);
    map_art_str(jo, "sign", m->sign);
    map_art_str(jo, "town", m->town);
    map_art_str(jo, "wandering_army", m->wandering_army);
    map_art_str(jo, "alcove", m->alcove);
    map_art_str(jo, "dwelling_plains", m->dwelling[RES_DWELL_PLAINS]);
    map_art_str(jo, "dwelling_forest", m->dwelling[RES_DWELL_FOREST]);
    map_art_str(jo, "dwelling_hills", m->dwelling[RES_DWELL_HILLS]);
    map_art_str(jo, "dwelling_dungeon", m->dwelling[RES_DWELL_DUNGEON]);
    static const char *const PARTS[RES_CASTLE_PARTS] = { "tl", "tm", "tr", "ml", "gate", "mr" };
    const cJSON *jc = cJSON_GetObjectItem(obj, "castle_3x2");
    for (int i = 0; i < RES_CASTLE_PARTS; i++) map_art_str(jc, PARTS[i], m->castle_3x2[i]);
    map_art_str(obj, "castle_1x1", m->castle_1x1);
    m->detail_count = map_art_list(obj, "details", m->detail, RES_MAP_DETAILS);
    const cJSON *ja = cJSON_GetObjectItem(obj, "aprons"), *jf = cJSON_GetObjectItem(obj, "fills");
    for (int w = 0; w < 2; w++) {
        for (int k = 0; k < 4; k++)
            m->apron_count[w][k] = map_art_list(cJSON_GetObjectItem(ja, WOODS[w]), SIDES[k],
                                                m->apron[w][k], RES_MAP_APRONS);
        char sand[16];
        snprintf(sand, sizeof sand, "%s_sand", WOODS[w]);
        for (int k = 0; k < 4; k++) {
            map_art_str(cJSON_GetObjectItem(jf, WOODS[w]), CORNERS[k], m->fill[w][0][k]);
            map_art_str(cJSON_GetObjectItem(jf, sand), CORNERS[k], m->fill[w][1][k]);
        }
    }
}

static bool object_art_has(const Resources *res, const char *stem) {
    for (int i = 0; i < res->object_art_count; i++)
        if (strcmp(res->object_arts[i], stem) == 0) return true;
    return false;
}

bool resources_art_is_object(const Resources *res, const char *stem) {
    return res && stem && stem[0] && object_art_has(res, stem);
}

// Gather every object stem (resources_art_is_object) once the catalogs are in.
// A name that is both an object and a terrain tile code is a pack error: it
// would have to be drawn from two folders.
static bool collect_object_arts(Resources *res) {
    const ResMapArt *m = &res->map_art;
    int cap = 16 + RES_DWELL_COUNT + RES_CASTLE_PARTS + res->town_count + res->castle_count + 2 * res->zone_count;
    for (int z = 0; z < res->zone_count; z++)
        for (int c = 0; c < res->zones[z].castle_count; c++) cap += res->zones[z].castles[c].decor_count;
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++) cap += res->tile_codes[i].object;
    free(res->object_arts);
    res->object_art_count = 0;
    res->object_arts = calloc((size_t)cap, sizeof *res->object_arts);
    if (!res->object_arts) return false;
#define ADD(s) do { const char *s_ = (s); \
        if (s_[0] && !object_art_has(res, s_) && res->object_art_count < cap) \
            res_copy_str(res->object_arts[res->object_art_count++], RES_TILE_ART_LEN, s_); } while (0)
    ADD(m->chest); ADD(m->artifact_chest); ADD(m->artifact_ring); ADD(m->sign);
    ADD(m->town); ADD(m->wandering_army); ADD(m->alcove); ADD(m->castle_1x1);
    for (int i = 0; i < RES_DWELL_COUNT; i++) ADD(m->dwelling[i]);
    for (int i = 0; i < RES_CASTLE_PARTS; i++) ADD(m->castle_3x2[i]);
    for (int i = 0; i < res->town_count; i++) ADD(res->towns[i].art);
    for (int i = 0; i < res->castle_count; i++) ADD(res->castles[i].art);
    for (int z = 0; z < res->zone_count; z++) {
        const ResZone *zn = &res->zones[z];
        ADD(zn->army_art);
        ADD(zn->alcove_art);
        for (int c = 0; c < zn->castle_count; c++)
            for (int d = 0; d < zn->castles[c].decor_count; d++) ADD(zn->castles[c].decorations[d].art);
    }
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++)
        if (res->tile_codes[i].present && res->tile_codes[i].object) ADD(res->tile_codes[i].art);
#undef ADD
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++) {
        const ResTileCode *tc = &res->tile_codes[i];
        if (!tc->present || tc->object) continue;
        if (object_art_has(res, tc->art) || (tc->ground[0] && object_art_has(res, tc->ground))) {
            fprintf(stdout, "resources: tile code 0x%02x names '%s', which is also a map object "
                    "(mark the code \"object\": true, or rename one)\n", i, tc->art);
            return false;
        }
    }
    return true;
}

// Fill a heap list with one entry per element of `arr`; *dst is allocated here
// (NULL when the array is empty or absent).
static void parse_zone_objects_array(cJSON *arr, int *count,
                                     void **dst, size_t stride,
                                     void (*fill)(cJSON *, void *)) {
    *count = 0;
    *dst = NULL;
    int cap = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (cap <= 0) return;
    *dst = calloc((size_t)cap, stride);
    if (!*dst) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (*count >= cap) break;
        void *slot = (char *)*dst + (*count) * stride;
        fill(it, slot);
        (*count)++;
    }
}

static void fill_sign(cJSON *j, void *dst) {
    ResSign *s = (ResSign *)dst;
    s->x = res_json_int(j, "x", 0); s->y = res_json_int(j, "y", 0);
    res_copy_str(s->id,    sizeof(s->id),    res_json_str(j, "id", ""));
    res_copy_str(s->title, sizeof(s->title), res_json_str(j, "title", ""));
    res_copy_str(s->body,  sizeof(s->body),  res_json_str(j, "body", ""));
    // A sign longer than its field was cut silently (#135): say so.
    const char *t = res_json_str(j, "title", ""), *b = res_json_str(j, "body", "");
    if (strlen(t) >= sizeof(s->title) || strlen(b) >= sizeof(s->body))
        fprintf(stdout, "resources: sign '%s' cut to %zu/%zu characters\n",
                s->id, sizeof(s->title) - 1, sizeof(s->body) - 1);
}
// Zone towns are resolved to indices into the authoritative res->towns[]
// catalog in parse_zones; see resolve_zone_town_idx.
static void fill_zone_castle(cJSON *j, void *dst) {
    ResZoneCastle *c = (ResZoneCastle *)dst;
    c->x = res_json_int(j, "x", 0); c->y = res_json_int(j, "y", 0);
    res_copy_str(c->id, sizeof(c->id), res_json_str(j, "id", ""));
    // Optional `decorations` array -- extra wall pieces around the gate.
    // Each entry: { "dx": int, "dy": int, "art": string }.
    cJSON *decor = cJSON_GetObjectItem(j, "decorations");
    int dcap = res_json_len(decor);
    c->decorations = NULL;
    c->decor_count = 0;
    if (cJSON_IsArray(decor) && RES_TABLE_ALLOC(c->decorations, c->decor_count, dcap)) {
        cJSON *it;
        cJSON_ArrayForEach(it, decor) {
            if (c->decor_count >= dcap) break;
            ResCastleDecor *d = &c->decorations[c->decor_count++];
            d->dx = res_json_int(it, "dx", 0);
            d->dy = res_json_int(it, "dy", 0);
            res_copy_str(d->art, sizeof(d->art), res_json_str(it, "art", ""));
        }
    }
}
static void fill_zone_chest(cJSON *j, void *dst) {
    ResZoneChest *c = (ResZoneChest *)dst;
    c->x = res_json_int(j, "x", 0); c->y = res_json_int(j, "y", 0);
    res_copy_str(c->id, sizeof(c->id), res_json_str(j, "id", ""));
    c->gold = res_json_int(j, "gold", 0);
    cJSON *fx = cJSON_GetObjectItem(j, "fixed");
    c->fixed = cJSON_IsBool(fx) && cJSON_IsTrue(fx);
    res_copy_str(c->artifact, sizeof(c->artifact), res_json_str(j, "artifact", ""));
}
static void fill_zone_artifact(cJSON *j, void *dst) {
    ResZoneArtifact *a = (ResZoneArtifact *)dst;
    a->x = res_json_int(j, "x", 0); a->y = res_json_int(j, "y", 0);
    res_copy_str(a->id, sizeof(a->id), res_json_str(j, "id", ""));
}
static void fill_zone_dwelling(cJSON *j, void *dst) {
    ResZoneDwelling *d = (ResZoneDwelling *)dst;
    d->x = res_json_int(j, "x", 0); d->y = res_json_int(j, "y", 0);
    res_copy_str(d->id,    sizeof(d->id),    res_json_str(j, "id", ""));
    res_copy_str(d->kind,  sizeof(d->kind),  res_json_str(j, "kind", ""));
    res_copy_str(d->troop, sizeof(d->troop), res_json_str(j, "troop", ""));
}
static void fill_zone_army(cJSON *j, void *dst) {
    ResZoneArmy *a = (ResZoneArmy *)dst;
    res_copy_str(a->requires_troop, sizeof(a->requires_troop),
             res_json_str(j, "requires_troop", ""));
    res_copy_str(a->scene, sizeof(a->scene), res_json_str(j, "scene", ""));
    res_copy_str(a->title, sizeof(a->title), res_json_str(j, "title", ""));
    a->scene_index = -1;
    a->x = res_json_int(j, "x", 0); a->y = res_json_int(j, "y", 0);
    res_copy_str(a->id, sizeof(a->id), res_json_str(j, "id", ""));
    cJSON *st = cJSON_GetObjectItem(j, "static");
    a->is_static = cJSON_IsBool(st) && cJSON_IsTrue(st);
    // Optional explicit garrison: "army":[{"troop":id,"count":n}, ...].
    a->army_stacks = 0;
    cJSON *army = cJSON_GetObjectItem(j, "army");
    if (cJSON_IsArray(army)) {
        cJSON *it;
        cJSON_ArrayForEach(it, army) {
            if (a->army_stacks >= 5) break;
            res_copy_str(a->army_id[a->army_stacks], sizeof(a->army_id[0]),
                     res_json_str(it, "troop", ""));
            a->army_count[a->army_stacks] = res_json_int(it, "count", 0);
            a->army_stacks++;
        }
    }
}

static void parse_zones(Resources *res, cJSON *arr) {
    int cap = res_json_len(arr);
    if (!RES_TABLE_ALLOC(res->zones, res->zone_count, cap)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->zone_count >= cap) break;
        ResZone *z = &res->zones[res->zone_count++];
        memset(z, 0, sizeof(*z));
        res_copy_str(z->id,       sizeof(z->id),       res_json_str(it, "id", ""));
        res_copy_str(z->name,     sizeof(z->name),     res_json_str(it, "name", ""));
        {
            char rel[RES_PATH_LEN];
            res_copy_str(rel, sizeof rel, res_json_str(it, "map", ""));
            // Back-compat: strip the legacy hardcoded prefix if present.
            // New manifests just write "maps/foo.dat".
            const char *legacy = "assets/kings-bounty/";
            size_t llen = strlen(legacy);
            const char *p = (strncmp(rel, legacy, llen) == 0) ? rel + llen : rel;
            resources_resolve_path(res, p, z->map_path, sizeof z->map_path);
        }
        res_copy_str(z->town_backdrop, sizeof(z->town_backdrop),
                 res_json_str(it, "town_backdrop", ""));
        res_copy_str(z->treasure_scene, sizeof(z->treasure_scene),
                 res_json_str(it, "treasure_scene", ""));
        res_copy_str(z->tile_set, sizeof(z->tile_set), res_json_str(it, "tile_set", ""));
        {
            cJSON *ov = cJSON_GetObjectItem(it, "tile_set_arts");
            int nov = cJSON_IsArray(ov) ? cJSON_GetArraySize(ov) : 0;
            z->tile_set_art_count = 0;
            z->tile_set_arts = nov > 0 ? calloc((size_t)nov, sizeof *z->tile_set_arts) : NULL;
            for (int k = 0; z->tile_set_arts && k < nov; k++) {
                const cJSON *e = cJSON_GetArrayItem(ov, k);
                if (cJSON_IsString(e) && e->valuestring)
                    res_copy_str(z->tile_set_arts[z->tile_set_art_count++], sizeof z->tile_set_arts[0],
                             e->valuestring);
            }
        }
        res_copy_str(z->army_art, sizeof(z->army_art), res_json_str(it, "army_art", ""));
        res_copy_str(z->field_grid, sizeof(z->field_grid), res_json_str(it, "field_grid", ""));
        res_copy_str(z->alcove_art, sizeof(z->alcove_art), res_json_str(it, "alcove_art", ""));
        res_copy_str(z->pontifex, sizeof(z->pontifex), res_json_str(it, "pontifex", ""));
        z->alcove_cost = res_json_int(it, "alcove_cost", -1);
        res_copy_str(z->boatmaster, sizeof(z->boatmaster), res_json_str(it, "boatmaster", ""));
        res_copy_str(z->siegemaster, sizeof(z->siegemaster), res_json_str(it, "siegemaster", ""));
        z->width  = res_json_int(it, "width",  64);
        z->height = res_json_int(it, "height", 64);
        cJSON *hs = cJSON_GetObjectItem(it, "hero_spawn");
        z->hero_spawn_x = res_json_int(hs, "x", 0);
        z->hero_spawn_y = res_json_int(hs, "y", 0);
        {
            cJSON *av = cJSON_GetObjectItem(it, "arrivals");
            int nav = cJSON_IsObject(av) ? cJSON_GetArraySize(av) : 0;
            z->arrival_count = 0;
            z->arrivals = nav > 0 ? calloc((size_t)nav, sizeof *z->arrivals) : NULL;
            const cJSON *e = NULL;
            if (z->arrivals) cJSON_ArrayForEach(e, av) {
                if (!e->string || !cJSON_IsObject(e)) continue;
                ResZoneArrival *a = &z->arrivals[z->arrival_count++];
                res_copy_str(a->from, sizeof a->from, e->string);
                a->x = res_json_int(e, "x", z->hero_spawn_x);
                a->y = res_json_int(e, "y", z->hero_spawn_y);
            }
        }

        {
            // Optional one-time vistas. Absent means none, so a pack that
            // predates them loads unchanged.
            cJSON *jev = cJSON_GetObjectItem(it, "events");
            int nev = cJSON_IsArray(jev) ? cJSON_GetArraySize(jev) : 0;
            z->event_count = 0;
            z->events = nev > 0 ? calloc((size_t)nev, sizeof *z->events) : NULL;
            for (int k = 0; z->events && k < nev; k++) {
                const cJSON *e = cJSON_GetArrayItem(jev, k);
                if (!cJSON_IsObject(e)) continue;
                ResZoneEvent *ev = &z->events[z->event_count++];
                res_copy_str(ev->id, sizeof ev->id, res_json_str(e, "id", ""));
                ev->x = res_json_int(e, "x", -1);
                ev->y = res_json_int(e, "y", -1);
                res_copy_str(ev->scene, sizeof ev->scene, res_json_str(e, "scene", ""));
                res_copy_str(ev->title, sizeof ev->title, res_json_str(e, "title", ""));
                res_copy_str(ev->body, sizeof ev->body, res_json_str(e, "body", ""));
                res_copy_str(ev->hint, sizeof ev->hint, res_json_str(e, "hint", ""));
                cJSON *jrq = cJSON_GetObjectItem(e, "requires");
                int nrq = cJSON_IsArray(jrq) ? cJSON_GetArraySize(jrq) : 0;
                ev->reqs = nrq > 0 ? calloc((size_t)nrq, sizeof *ev->reqs) : NULL;
                for (int q = 0; ev->reqs && q < nrq; q++) {
                    const cJSON *r = cJSON_GetArrayItem(jrq, q);
                    if (!cJSON_IsObject(r)) continue;
                    ResEventReq *rq = &ev->reqs[ev->req_count++];
                    const char *what = res_json_str(r, "spell", NULL);
                    if (what) rq->kind = RES_EVENT_REQ_SPELL;
                    else if ((what = res_json_str(r, "troop", NULL))) rq->kind = RES_EVENT_REQ_TROOP;
                    else if ((what = res_json_str(r, "artifact", NULL))) rq->kind = RES_EVENT_REQ_ARTIFACT;
                    else rq->kind = RES_EVENT_REQ_GOLD;
                    if (what) res_copy_str(rq->id, sizeof rq->id, what);
                    rq->count = res_json_int(r, "count",
                                         rq->kind == RES_EVENT_REQ_GOLD
                                             ? res_json_int(r, "gold", 0) : 1);
                    rq->consume = cJSON_IsTrue(cJSON_GetObjectItem(r, "consume"));
                }
                cJSON *jef = cJSON_GetObjectItem(e, "effects");
                int nef = cJSON_IsArray(jef) ? cJSON_GetArraySize(jef) : 0;
                ev->effects = nef > 0 ? calloc((size_t)nef, sizeof *ev->effects) : NULL;
                for (int q = 0; ev->effects && q < nef; q++) {
                    const cJSON *f = cJSON_GetArrayItem(jef, q);
                    if (!cJSON_IsObject(f)) continue;
                    if (cJSON_IsTrue(cJSON_GetObjectItem(f, "reveal"))) {
                        ResEventEffect *rv = &ev->effects[ev->effect_count++];
                        rv->kind = RES_EVENT_FX_REVEAL;
                        rv->x = rv->y = -1;
                        continue;
                    }
                    // The tile is named the way tile_codes names it, escapes
                    // and all ("\\xcc").
                    int code = resources_tile_code_from_key(res_json_str(f, "tile", ""));
                    if (code < 0 || code >= RES_TILE_CODE_COUNT) continue;
                    ResEventEffect *fx = &ev->effects[ev->effect_count++];
                    fx->x = res_json_int(f, "x", -1);
                    fx->y = res_json_int(f, "y", -1);
                    fx->code = (unsigned char)code;
                }
            }
        }

        cJSON *nbr = cJSON_GetObjectItem(it, "neighbors");
        int ncap = res_json_len(nbr);
        if (cJSON_IsArray(nbr) && RES_TABLE_ALLOC(z->neighbors, z->neighbor_count, ncap)) {
            cJSON *n;
            cJSON_ArrayForEach(n, nbr) {
                if (z->neighbor_count >= ncap) break;
                if (cJSON_IsString(n)) {
                    res_copy_str(z->neighbors[z->neighbor_count],
                             sizeof(z->neighbors[0]), n->valuestring);
                    z->neighbor_count++;
                }
            }
        }

        parse_zone_objects_array(cJSON_GetObjectItem(it, "signs"),
                                 &z->sign_count,
                                 (void **)&z->signs, sizeof(ResSign), fill_sign);
        // Towns: resolve each zone town's id to an INDEX into the authoritative
        // res->towns[] catalog (already parsed -- parse_towns runs before
        // parse_zones). Preserves the zone JSON town ORDER (required for planner
        // determinism). A zone town id missing from the catalog is skipped + logged.
        {
            cJSON *jtowns = cJSON_GetObjectItem(it, "towns");
            int tcap = res_json_len(jtowns);
            if (cJSON_IsArray(jtowns) && RES_TABLE_ALLOC(z->town_idx, z->town_count, tcap)) {
                cJSON *jt;
                cJSON_ArrayForEach(jt, jtowns) {
                    if (z->town_count >= tcap) break;
                    const char *tid = res_json_str(jt, "id", "");
                    int idx = -1;
                    for (int i = 0; i < res->town_count; i++)
                        if (strcmp(res->towns[i].id, tid) == 0) { idx = i; break; }
                    if (idx < 0) {
                        fprintf(stdout, "resources: zone '%s' town '%s' not in "
                                "top-level towns[], skipped\n", z->id, tid);
                        continue;
                    }
                    z->town_idx[z->town_count++] = idx;
                }
            }
        }
        parse_zone_objects_array(cJSON_GetObjectItem(it, "castles"),
                                 &z->castle_count,
                                 (void **)&z->castles, sizeof(ResZoneCastle), fill_zone_castle);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "chests"),
                                 &z->chest_count,
                                 (void **)&z->chests, sizeof(ResZoneChest), fill_zone_chest);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "artifacts"),
                                 &z->artifact_count,
                                 (void **)&z->artifacts, sizeof(ResZoneArtifact), fill_zone_artifact);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "dwellings"),
                                 &z->dwelling_count,
                                 (void **)&z->dwellings, sizeof(ResZoneDwelling), fill_zone_dwelling);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "wandering_armies"),
                                 &z->army_count,
                                 (void **)&z->armies, sizeof(ResZoneArmy), fill_zone_army);

        cJSON *salt = cJSON_GetObjectItem(it, "salt");
        z->salt.artifacts     = res_json_int(salt, "artifacts",     0);
        z->salt.navmaps       = res_json_int(salt, "navmaps",       0);
        z->salt.orbs          = res_json_int(salt, "orbs",          0);
        z->salt.telecaves     = res_json_int(salt, "telecaves",     0);
        z->salt.dwellings     = res_json_int(salt, "dwellings",     0);
        z->salt.friendly_foes = res_json_int(salt, "friendly_foes", 0);
        z->salt.preferred_troop_count = 0;
        z->salt.dwelling_range_min = -1;
        z->salt.dwelling_range_max = -1;
        if (cJSON_IsObject(salt)) {
            cJSON *pref = cJSON_GetObjectItem(salt, "preferred_troops");
            int n = res_json_len(pref);
            if (cJSON_IsArray(pref) &&
                RES_TABLE_ALLOC(z->salt.preferred_troops, z->salt.preferred_troop_count, n)) {
                for (int i = 0; i < n; i++) {
                    cJSON *e = cJSON_GetArrayItem(pref, i);
                    if (cJSON_IsString(e)) {
                        res_copy_str(z->salt.preferred_troops[z->salt.preferred_troop_count],
                                 RES_ID_LEN, e->valuestring);
                        z->salt.preferred_troop_count++;
                    }
                }
            }
            cJSON *range = cJSON_GetObjectItem(salt, "dwelling_range");
            if (cJSON_IsArray(range) && cJSON_GetArraySize(range) == 2) {
                cJSON *lo = cJSON_GetArrayItem(range, 0);
                cJSON *hi = cJSON_GetArrayItem(range, 1);
                if (cJSON_IsNumber(lo)) z->salt.dwelling_range_min = lo->valueint;
                if (cJSON_IsNumber(hi)) z->salt.dwelling_range_max = hi->valueint;
            }
        }

        cJSON *jhome = cJSON_GetObjectItem(it, "is_home");
        z->is_home = cJSON_IsBool(jhome) && cJSON_IsTrue(jhome);
        cJSON *hsp = cJSON_GetObjectItem(it, "home_spawn");
        z->home_spawn_x = res_json_int(hsp, "x", -1);
        z->home_spawn_y = res_json_int(hsp, "y", -1);

        cJSON *ma = cJSON_GetObjectItem(it, "magic_alcove");
        z->magic_alcove_x = res_json_int(ma, "x", -1);
        z->magic_alcove_y = res_json_int(ma, "y", -1);
    }
}

// ---- Catalog parsers ------------------------------------------------------

static int parse_troop_abilities(const char *s) {
    if (!s || !s[0]) return 0;
    int mask = 0;
    const char *p = s;
    while (*p) {
        // Isolate one token (delimited by '|').
        const char *start = p;
        while (*p && *p != '|') p++;
        size_t len = (size_t)(p - start);
        if      (len == 3 && strncmp(start, "FLY",    3) == 0) mask |= TROOP_ABIL_FLY;
        else if (len == 5 && strncmp(start, "REGEN",  5) == 0) mask |= TROOP_ABIL_REGEN;
        else if (len == 5 && strncmp(start, "MAGIC",  5) == 0) mask |= TROOP_ABIL_MAGIC;
        else if (len == 6 && strncmp(start, "IMMUNE", 6) == 0) mask |= TROOP_ABIL_IMMUNE;
        else if (len == 6 && strncmp(start, "ABSORB", 6) == 0) mask |= TROOP_ABIL_ABSORB;
        else if (len == 5 && strncmp(start, "LEECH",  5) == 0) mask |= TROOP_ABIL_LEECH;
        else if (len == 6 && strncmp(start, "SCYTHE", 6) == 0) mask |= TROOP_ABIL_SCYTHE;
        else if (len == 6 && strncmp(start, "UNDEAD", 6) == 0) mask |= TROOP_ABIL_UNDEAD;
        if (*p == '|') p++;
    }
    return mask;
}

// "troop_aliases": {"old id": "new id", ...}. Fail-loud like the other
// pack contracts: every new id must be a troop the pack defines, and no old
// id may still be one (an alias would then hide a live troop).
static const TroopDef *alias_troop(const Resources *res, const char *id) {
    for (int i = 0; i < res->troops_count; i++)
        if (strcmp(res->troops[i].id, id) == 0) return &res->troops[i];
    return NULL;
}

static bool parse_troop_aliases(Resources *res, cJSON *obj) {
    free(res->troop_alias_from); res->troop_alias_from = NULL;
    free(res->troop_alias_to);   res->troop_alias_to = NULL;
    res->troop_alias_count = 0;
    if (!cJSON_IsObject(obj)) return true;
    int n = cJSON_GetArraySize(obj);
    if (n <= 0) return true;
    res->troop_alias_from = calloc((size_t)n, sizeof *res->troop_alias_from);
    res->troop_alias_to   = calloc((size_t)n, sizeof *res->troop_alias_to);
    if (!res->troop_alias_from || !res->troop_alias_to) {
        fprintf(stderr, "resources: out of memory for %d troop aliases\n", n);
        return false;
    }
    cJSON *it;
    cJSON_ArrayForEach(it, obj) {
        if (!cJSON_IsString(it) || !alias_troop(res, it->valuestring) ||
            alias_troop(res, it->string)) {
            fprintf(stdout, "resources: invalid troop_aliases entry "
                    "'%s' -> '%s'\n", it->string,
                    cJSON_IsString(it) ? it->valuestring : "?");
            return false;
        }
        int k = res->troop_alias_count++;
        res_copy_str(res->troop_alias_from[k], sizeof res->troop_alias_from[k],
                     it->string);
        res_copy_str(res->troop_alias_to[k], sizeof res->troop_alias_to[k],
                     it->valuestring);
    }
    return true;
}

const char *resources_troop_alias(const Resources *res, const char *id) {
    for (int i = 0; res && id && i < res->troop_alias_count; i++)
        if (strcmp(res->troop_alias_from[i], id) == 0)
            return res->troop_alias_to[i];
    return NULL;
}

static void parse_troops(Resources *res, cJSON *arr) {
    res->troops_count = 0;
    free(res->troops);
    res->troops = NULL;
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n <= 0) return;
    res->troops = calloc((size_t)n, sizeof *res->troops);
    if (!res->troops) { fprintf(stderr, "resources: out of memory for %d troops\n", n); return; }
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->troops_count >= n) break;
        TroopDef *t = &res->troops[res->troops_count++];
        memset(t, 0, sizeof(*t));
        t->index = res_json_int(it, "index", -1);
        res_copy_str(t->id,       sizeof(t->id),       res_json_str(it, "id", ""));
        res_copy_str(t->name,     sizeof(t->name),     res_json_str(it, "name", ""));
        res_copy_str(t->sprite,   sizeof(t->sprite),   res_json_str(it, "sprite", ""));
        res_copy_str(t->portrait, sizeof(t->portrait), res_json_str(it, "portrait", ""));
        // Via parse_string_array so a non-string entry is skipped rather than
        // burning a slot: the hand-rolled loop this replaces advanced its
        // index outside the type check, leaving an empty frame mid-cycle.
        res_parse_path_list(cJSON_GetObjectItem(it, "anim"), &t->anim, &t->anim_count);
        res_copy_str(t->dwelling, sizeof(t->dwelling), res_json_str(it, "dwelling", ""));
        t->skill_level     = res_json_int(it, "skill_level", 0);
        t->hit_points      = res_json_int(it, "hit_points", 0);
        t->move_rate       = res_json_int(it, "move_rate", 0);
        cJSON *melee = cJSON_GetObjectItem(it, "melee");
        if (cJSON_IsArray(melee)) {
            cJSON *mmin = cJSON_GetArrayItem(melee, 0);
            cJSON *mmax = cJSON_GetArrayItem(melee, 1);
            if (cJSON_IsNumber(mmin)) t->melee_min = mmin->valueint;
            if (cJSON_IsNumber(mmax)) t->melee_max = mmax->valueint;
        }
        cJSON *ranged = cJSON_GetObjectItem(it, "ranged");
        if (cJSON_IsArray(ranged)) {
            cJSON *a = cJSON_GetArrayItem(ranged, 0);
            cJSON *b = cJSON_GetArrayItem(ranged, 1);
            cJSON *c = cJSON_GetArrayItem(ranged, 2);
            if (cJSON_IsNumber(a)) t->ranged_min  = a->valueint;
            if (cJSON_IsNumber(b)) t->ranged_max  = b->valueint;
            if (cJSON_IsNumber(c)) t->ranged_ammo = c->valueint;
        }
        t->recruit_cost    = res_json_int(it, "recruit_cost", 0);
        t->spoils_factor   = res_json_int(it, "spoils_factor", 0);
        t->abilities       = parse_troop_abilities(res_json_str(it, "abilities", ""));
        t->max_population  = res_json_int(it, "max_population", 0);
        t->growth_per_week = res_json_int(it, "growth_per_week", 0);
        const char *grp = res_json_str(it, "morale_group", "A");
        t->morale_group = grp[0] ? grp[0] : 'A';

        cJSON *tc = cJSON_GetObjectItem(it, "tier_counts");
        if (cJSON_IsArray(tc)) {
            int n = 0;
            cJSON *v;
            cJSON_ArrayForEach(v, tc) {
                if (n >= 4) break;
                if (cJSON_IsNumber(v)) t->tier_counts[n] = v->valueint;
                n++;
            }
        }
    }
    // One list of every distinct vista scene the pack declares: a fired vista
    // names its art by index, so the shell loads the set once.
    int scenes = 0;
    for (int zi = 0; zi < res->zone_count; zi++)
        scenes += res->zones[zi].event_count + res->zones[zi].army_count;
    if (scenes > 0) {
        res->event_scenes = calloc((size_t)scenes, sizeof *res->event_scenes);
        res->event_scene_count = 0;
        for (int zi = 0; res->event_scenes && zi < res->zone_count; zi++) {
            for (int k = 0; k < res->zones[zi].event_count; k++) {
                ResZoneEvent *ev = &res->zones[zi].events[k];
                ev->scene_index = -1;
                if (!ev->scene[0]) continue;
                for (int e = 0; e < res->event_scene_count; e++)
                    if (strcmp(res->event_scenes[e], ev->scene) == 0) ev->scene_index = e;
                if (ev->scene_index < 0) {
                    ev->scene_index = res->event_scene_count;
                    res_copy_str(res->event_scenes[res->event_scene_count++],
                             RES_PATH_LEN, ev->scene);
                }
            }
            // A gate army's refusal scene shares the same list.
            for (int k = 0; k < res->zones[zi].army_count; k++) {
                ResZoneArmy *ar = &res->zones[zi].armies[k];
                ar->scene_index = -1;
                if (!ar->scene[0]) continue;
                for (int e = 0; e < res->event_scene_count; e++)
                    if (strcmp(res->event_scenes[e], ar->scene) == 0) ar->scene_index = e;
                if (ar->scene_index < 0) {
                    ar->scene_index = res->event_scene_count;
                    res_copy_str(res->event_scenes[res->event_scene_count++],
                             RES_PATH_LEN, ar->scene);
                }
            }
        }
    }

}

static SpellKind spell_kind_from_name(const char *s) {
    if (s && strcmp(s, "adventure") == 0) return SPELL_KIND_ADVENTURE;
    return SPELL_KIND_COMBAT;
}

static void parse_spells(Resources *res, cJSON *arr) {
    int cap = res_json_len(arr);
    if (!RES_TABLE_ALLOC(res->spells, res->spells_count, cap)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->spells_count >= cap) break;
        SpellDef *s = &res->spells[res->spells_count++];
        memset(s, 0, sizeof(*s));
        s->index = res_json_int(it, "index", -1);
        res_copy_str(s->id,          sizeof(s->id),          res_json_str(it, "id", ""));
        res_copy_str(s->name,        sizeof(s->name),        res_json_str(it, "name", ""));
        res_copy_str(s->description, sizeof(s->description), res_json_str(it, "description", ""));
        s->kind = spell_kind_from_name(res_json_str(it, "kind", "combat"));
        s->cost = res_json_int(it, "cost", 0);
    }
}

static void parse_classes(Resources *res, cJSON *arr) {
    int cap = res_json_len(arr);
    if (!RES_TABLE_ALLOC(res->classes, res->classes_count, cap)) return;
    free(res->class_hero);
    res->class_hero = calloc((size_t)cap, sizeof *res->class_hero);
    if (!res->class_hero) { free(res->classes); res->classes = NULL; return; }
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->classes_count >= cap) break;
        ClassDef *c = &res->classes[res->classes_count++];
        memset(c, 0, sizeof(*c));
        c->index = res_json_int(it, "index", -1);
        res_copy_str(c->id,       sizeof(c->id),       res_json_str(it, "id", ""));
        res_copy_str(c->name,     sizeof(c->name),     res_json_str(it, "name", ""));
        res_copy_str(c->portrait, sizeof(c->portrait), res_json_str(it, "portrait", ""));
        c->starting_gold = res_json_int(it, "starting_gold", 0);
        {
            // Optional per-class hero art, declared like sprites.hero plus a
            // win-cartoon tile. Parallel slot in res->class_hero.
            ResClassHero *h = &res->class_hero[res->classes_count - 1];
            memset(h, 0, sizeof *h);
            cJSON *hero = cJSON_GetObjectItem(it, "hero");
            if (cJSON_IsObject(hero)) {
                parse_anim_set(cJSON_GetObjectItem(hero, "walk"), &h->walk);
                parse_anim_set(cJSON_GetObjectItem(hero, "idle"), &h->idle);
                parse_anim_set(cJSON_GetObjectItem(hero, "boat"), &h->boat);
                res_copy_str(h->tile, sizeof h->tile, res_json_str(hero, "tile", ""));
                res_copy_str(h->disgraced, sizeof h->disgraced, res_json_str(hero, "disgraced", ""));
            }
        }

        cJSON *st = cJSON_GetObjectItem(it, "starting_troops");
        int si = 0, st_cap = res_json_len(st), st_n = 0;
        if (cJSON_IsArray(st) && RES_TABLE_ALLOC(c->starting_troops, c->starting_troop_count, st_cap) &&
            RES_TABLE_ALLOC(c->starting_counts, st_n, st_cap)) {
            (void)st_n;
            cJSON *e;
            cJSON_ArrayForEach(e, st) {
                if (si >= st_cap) break;
                res_copy_str(c->starting_troops[si], sizeof(c->starting_troops[si]),
                         res_json_str(e, "id", ""));
                c->starting_counts[si] = res_json_int(e, "count", 0);
                si++;
            }
            c->starting_troop_count = si;
        }

        cJSON *rk = cJSON_GetObjectItem(it, "ranks");
        c->rank_count = 0;
        if (cJSON_IsArray(rk)) {
            cJSON *r;
            cJSON_ArrayForEach(r, rk) {
                if (c->rank_count >= CLASS_MAX_RANKS) break;
                RankDef *rd = &c->ranks[c->rank_count++];
                memset(rd, 0, sizeof(*rd));
                res_copy_str(rd->id,   sizeof(rd->id),   res_json_str(r, "id", ""));
                res_copy_str(rd->name, sizeof(rd->name), res_json_str(r, "name", ""));
                rd->villains_needed = res_json_int(r, "villains_needed", 0);
                rd->leadership      = res_json_int(r, "leadership", 0);
                rd->max_spells      = res_json_int(r, "max_spells", 0);
                rd->spell_power     = res_json_int(r, "spell_power", 0);
                rd->commission      = res_json_int(r, "commission", 0);
                cJSON *km = cJSON_GetObjectItem(r, "knows_magic");
                rd->knows_magic     = cJSON_IsBool(km) && cJSON_IsTrue(km);
                rd->instant_army    = res_json_int(r, "instant_army", 0);
            }
        }
    }
}

static void parse_portraits(Resources *res, cJSON *arr) {
    res->portrait_count = 0;
    free(res->portraits);
    res->portraits = NULL;
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n <= 0) return;
    res->portraits = calloc((size_t)n, sizeof *res->portraits);
    if (!res->portraits) { fprintf(stderr, "resources: out of memory for %d portraits\n", n); return; }
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->portrait_count >= n) break;
        ResPortrait *p = &res->portraits[res->portrait_count++];
        res_copy_str(p->id, sizeof(p->id), res_json_str(it, "id", ""));
        res_parse_path_list(cJSON_GetObjectItem(it, "anim"), &p->anim, &p->anim_count);
    }
}

static void parse_villains(Resources *res, cJSON *arr) {
    int cap = res_json_len(arr);
    if (!RES_TABLE_ALLOC(res->villains, res->villains_count, cap)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->villains_count >= cap) break;
        VillainDef *v = &res->villains[res->villains_count++];
        memset(v, 0, sizeof(*v));
        v->index = res_json_int(it, "index", -1);
        res_copy_str(v->id,       sizeof(v->id),       res_json_str(it, "id", ""));
        res_copy_str(v->name,     sizeof(v->name),     res_json_str(it, "name", ""));
        res_copy_str(v->portrait, sizeof(v->portrait), res_json_str(it, "portrait", ""));
        // Optional, and declared exactly like a troop's. Absent (count 0)
        // means the shell derives `<portrait-stem>_NN` siblings instead.
        res_parse_path_list(cJSON_GetObjectItem(it, "anim"), &v->anim, &v->anim_count);
        res_copy_str(v->zone,     sizeof(v->zone),     res_json_str(it, "zone", ""));
        v->reward      = res_json_int(it, "reward", 0);
        v->puzzle_cell = res_json_int(it, "puzzle_cell", -1);

        cJSON *army = cJSON_GetObjectItem(it, "army");
        if (cJSON_IsArray(army)) {
            int i = 0;
            cJSON *slot;
            cJSON_ArrayForEach(slot, army) {
                if (i >= 5) break;
                if (cJSON_IsObject(slot)) {
                    res_copy_str(v->army_troops[i],
                             sizeof(v->army_troops[i]),
                             res_json_str(slot, "troop", ""));
                    v->army_counts[i] = res_json_int(slot, "count", 0);
                }
                i++;
            }
        }
    }
}

static ArtifactPower artifact_power_from_name(const char *s) {
    if (!s) return ARTIFACT_POWER_UNKNOWN;
    if (strcmp(s, "increased_damage")    == 0) return ARTIFACT_POWER_INCREASED_DAMAGE;
    if (strcmp(s, "quarter_protection")  == 0) return ARTIFACT_POWER_QUARTER_PROTECTION;
    if (strcmp(s, "double_leadership")   == 0) return ARTIFACT_POWER_DOUBLE_LEADERSHIP;
    if (strcmp(s, "increase_commission") == 0) return ARTIFACT_POWER_INCREASE_COMMISSION;
    if (strcmp(s, "double_spell_power")  == 0) return ARTIFACT_POWER_DOUBLE_SPELL_POWER;
    if (strcmp(s, "double_max_spells")   == 0) return ARTIFACT_POWER_DOUBLE_MAX_SPELLS;
    if (strcmp(s, "cheaper_boat_rental") == 0) return ARTIFACT_POWER_CHEAPER_BOATS;
    return ARTIFACT_POWER_UNKNOWN;
}

static void parse_artifacts(Resources *res, cJSON *arr) {
    int cap = res_json_len(arr);
    if (!RES_TABLE_ALLOC(res->artifacts, res->artifacts_count, cap)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->artifacts_count >= cap) break;
        ArtifactDef *a = &res->artifacts[res->artifacts_count++];
        memset(a, 0, sizeof(*a));
        a->index = res_json_int(it, "index", -1);
        res_copy_str(a->id,     sizeof(a->id),     res_json_str(it, "id", ""));
        res_copy_str(a->name,   sizeof(a->name),   res_json_str(it, "name", ""));
        res_copy_str(a->icon,   sizeof(a->icon),   res_json_str(it, "icon", ""));
        res_copy_str(a->effect, sizeof(a->effect), res_json_str(it, "effect", ""));
        res_copy_str(a->zone,   sizeof(a->zone),   res_json_str(it, "zone", ""));
        a->power       = artifact_power_from_name(res_json_str(it, "power", ""));
        a->puzzle_cell = res_json_int(it, "puzzle_cell", -1);
        a->local_idx   = res_json_int(it, "local_idx", 0);
    }
}

// ---- Sprite manifest -----------------------------------------------------

// Fill a fixed-size array of path buffers from a JSON array of strings,
// skipping any non-string entry. `stride` is the per-slot buffer size, which
// is what lets this serve both the RES_PATH_LEN sprite manifest arrays and
// the CAT_PATH_LEN catalog ones. *out_count receives the number of strings
// actually stored -- for an animation that count IS the cycle length.
void res_parse_path_array(cJSON *arr, char *dst, size_t stride,
                             int cap, int *out_count) {
    if (out_count) *out_count = 0;
    if (!cJSON_IsArray(arr) || !dst) return;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= cap) break;
        if (!cJSON_IsString(it)) continue;
        res_copy_str(dst + (size_t)n * stride, stride, it->valuestring);
        n++;
    }
    if (out_count) *out_count = n;
}

// A heap list of paths, one per string element (non-strings are skipped, so
// no hole is left): *dst holds exactly *out_count, NULL when there are none.
void res_parse_path_list(cJSON *arr, char (**dst)[RES_PATH_LEN], int *out_count) {
    int cap = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    int held = 0;
    if (!RES_TABLE_ALLOC(*dst, held, cap)) { *out_count = 0; return; }
    (void)held;
    res_parse_path_array(arr, (*dst)[0], RES_PATH_LEN, cap, out_count);
    if (*out_count == 0) { free(*dst); *dst = NULL; }
}

static void parse_string_array(cJSON *arr, char dst[][RES_PATH_LEN],
                               int cap, int *out_count) {
    res_parse_path_array(arr, dst ? dst[0] : NULL, RES_PATH_LEN, cap, out_count);
}

// An animation is authored one of two ways, and both are accepted:
//
//   "walk": ["a.png", "b.png"]                        <- one strip, mirrored
//   "walk": {"south": [...], "east": [...], ... }     <- four authored facings
//
// The flat form lands in OB_FACE_SOUTH with directional=false, so packs
// written before facings existed parse exactly as they always did. In the
// object form a facing may be omitted; it simply keeps count 0 and the
// renderer falls back for it.
static void parse_anim_set(cJSON *obj, ResAnimSet *out) {
    static const char *KEYS[OB_FACE_COUNT] = { "south", "east", "west", "north" };
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!obj) return;

    if (cJSON_IsArray(obj)) {
        res_parse_path_list(obj, &out->frames[OB_FACE_SOUTH], &out->count[OB_FACE_SOUTH]);
        return;
    }
    if (!cJSON_IsObject(obj)) return;
    out->directional = true;
    for (int f = 0; f < OB_FACE_COUNT; f++) {
        res_parse_path_list(cJSON_GetObjectItem(obj, KEYS[f]), &out->frames[f], &out->count[f]);
    }
}

static void parse_sprites(Resources *res, cJSON *obj) {
    // Default art paths. A pack may override any of them in the sprites block;
    // a pack that doesn't gets these.
    {
        static const char *COMBAT_DEFAULT[RES_COMBAT_TILES] = {
            "art/combat/field_grass.png",
            "art/combat/obstacle_01.png",
            "art/combat/obstacle_02.png",
            "art/combat/obstacle_03.png",
            "art/combat/castle_spike.png",
            "art/combat/castle_wall_01.png",
            "art/combat/castle_wall_02.png",
            "art/combat/castle_wall_03.png",
            "art/combat/castle_wall_04.png",
            "art/combat/castle_wall_05.png",
            "art/combat/castle_wall_06.png",
            "art/combat/cursor_01.png",
            "art/combat/cursor_02.png",
            "art/combat/cursor_03.png",
            "art/combat/cursor_04.png",
        };
        for (int i = 0; i < RES_COMBAT_TILES; i++)
            res_copy_str(res->sprites.combat[i], RES_PATH_LEN, COMBAT_DEFAULT[i]);
        res->sprites.combat_count = RES_COMBAT_TILES;
    }
    res_copy_str(res->sprites.combat_ground, sizeof res->sprites.combat_ground, "field");
    res_copy_str(res->sprites.font, sizeof res->sprites.font,
             "art/font/kb-font.png");
    res_copy_str(res->sprites.palette, sizeof res->sprites.palette,
             "palettes/palette.bin");
    if (!cJSON_IsObject(obj)) return;

    cJSON *hero = cJSON_GetObjectItem(obj, "hero");
    if (cJSON_IsObject(hero)) {
        parse_anim_set(cJSON_GetObjectItem(hero, "walk"), &res->sprites.hero_walk);
        parse_anim_set(cJSON_GetObjectItem(hero, "idle"), &res->sprites.hero_idle);
        parse_anim_set(cJSON_GetObjectItem(hero, "boat"), &res->sprites.hero_boat);
    }

    // Combat tileset: the pack may name its own battle art; the defaults
    // installed before parsing stand for a pack that doesn't.
    // Guarded: res_parse_path_array zeroes the out-count for a missing key, which
    // would wipe the defaults installed above rather than leave them alone.
    cJSON *jcombat = cJSON_GetObjectItem(obj, "combat");
    if (cJSON_IsArray(jcombat))
        parse_string_array(jcombat, res->sprites.combat, RES_COMBAT_TILES,
                           &res->sprites.combat_count);

    // Font strip and palette binary, likewise compiled into the shell before.
    res_copy_str(res->sprites.font, sizeof res->sprites.font,
             res_json_str(obj, "font", res->sprites.font));
    res_copy_str(res->sprites.palette, sizeof res->sprites.palette,
             res_json_str(obj, "palette", res->sprites.palette));

    cJSON *ui = cJSON_GetObjectItem(obj, "ui");
    if (cJSON_IsObject(ui)) {
        res_copy_str(res->sprites.puzzle_cover, sizeof(res->sprites.puzzle_cover),
                 res_json_str(ui, "puzzle_cover", ""));
        // Location backdrops .
        res_copy_str(res->sprites.town_backdrop, sizeof(res->sprites.town_backdrop),
                 res_json_str(ui, "town_backdrop", ""));
        res_copy_str(res->sprites.castle_backdrop, sizeof(res->sprites.castle_backdrop),
                 res_json_str(ui, "castle_backdrop", ""));
        res_copy_str(res->sprites.plains_backdrop, sizeof(res->sprites.plains_backdrop),
                 res_json_str(ui, "plains_backdrop", ""));
        res_copy_str(res->sprites.forest_backdrop, sizeof(res->sprites.forest_backdrop),
                 res_json_str(ui, "forest_backdrop", ""));
        res_copy_str(res->sprites.hillcave_backdrop, sizeof(res->sprites.hillcave_backdrop),
                 res_json_str(ui, "hillcave_backdrop", ""));
        res_copy_str(res->sprites.alcove_backdrop, sizeof(res->sprites.alcove_backdrop),
                 res_json_str(ui, "alcove_backdrop", ""));
        res_copy_str(res->sprites.sail_backdrop, sizeof(res->sprites.sail_backdrop),
                 res_json_str(ui, "sail_backdrop", ""));
        res_copy_str(res->sprites.palace_welcome, sizeof(res->sprites.palace_welcome),
                 res_json_str(ui, "palace_welcome", ""));
        res_copy_str(res->sprites.palace_barracks, sizeof(res->sprites.palace_barracks),
                 res_json_str(ui, "palace_barracks", ""));
        res_copy_str(res->sprites.palace_throne, sizeof(res->sprites.palace_throne),
                 res_json_str(ui, "palace_throne", ""));
        res_copy_str(res->sprites.scene_column_capital, sizeof(res->sprites.scene_column_capital),
                 res_json_str(ui, "scene_column_capital", ""));
        res_copy_str(res->sprites.scene_column_shaft, sizeof(res->sprites.scene_column_shaft),
                 res_json_str(ui, "scene_column_shaft", ""));
        res_copy_str(res->sprites.scene_column_base, sizeof(res->sprites.scene_column_base),
                 res_json_str(ui, "scene_column_base", ""));
        res_copy_str(res->sprites.alcove_troop, sizeof(res->sprites.alcove_troop),
                 res_json_str(ui, "alcove_troop", ""));
        res_copy_str(res->sprites.alcove_figure, sizeof(res->sprites.alcove_figure),
                 res_json_str(ui, "alcove_figure", ""));
        res_parse_path_list(cJSON_GetObjectItem(ui, "alcove_figure_animation"),
                        &res->sprites.alcove_figure_animation, &res->sprites.alcove_figure_animation_count);
        {
            cJSON *pl = cJSON_GetObjectItem(ui, "alcove_figure_place");
            res->sprites.alcove_figure_x = res_json_int(pl, "x", 0);
            res->sprites.alcove_figure_y = res_json_int(pl, "y", 0);
            res->sprites.alcove_figure_w = res_json_int(pl, "w", 0);
            res->sprites.alcove_figure_h = res_json_int(pl, "h", 0);
            if (res->sprites.alcove_figure_w < 0) res->sprites.alcove_figure_w = 0;
            if (res->sprites.alcove_figure_h <= 0)
                res->sprites.alcove_figure_h = res->sprites.alcove_figure_w;
            res->sprites.alcove_figure_frame_ms =
                res_json_int(ui, "alcove_figure_frame_ms", 0);
            if (res->sprites.alcove_figure_frame_ms < 0)
                res->sprites.alcove_figure_frame_ms = 0;
        }
        res_copy_str(res->sprites.dungeon_backdrop, sizeof(res->sprites.dungeon_backdrop),
                 res_json_str(ui, "dungeon_backdrop", ""));
        res_copy_str(res->sprites.ending_win, sizeof(res->sprites.ending_win),
                 res_json_str(ui, "ending_win", ""));
        res_copy_str(res->sprites.panel_frame, sizeof(res->sprites.panel_frame),
                 res_json_str(ui, "panel_frame", ""));
        res_copy_str(res->sprites.siege_back_wall, sizeof(res->sprites.siege_back_wall),
                 res_json_str(ui, "siege_back_wall", ""));
        res_copy_str(res->sprites.siege_back_wall_left, sizeof(res->sprites.siege_back_wall_left),
                 res_json_str(ui, "siege_back_wall_left", ""));
        res_copy_str(res->sprites.siege_back_wall_right, sizeof(res->sprites.siege_back_wall_right),
                 res_json_str(ui, "siege_back_wall_right", ""));
        res_copy_str(res->sprites.siege_grid, sizeof(res->sprites.siege_grid),
                 res_json_str(ui, "siege_grid", ""));
        res_copy_str(res->sprites.field_grid, sizeof(res->sprites.field_grid),
                 res_json_str(ui, "field_grid", ""));
        res_copy_str(res->sprites.combat_ground, sizeof(res->sprites.combat_ground),
                 res_json_str(ui, "combat_ground", "field"));
        res_copy_str(res->sprites.ending_lose, sizeof(res->sprites.ending_lose),
                 res_json_str(ui, "ending_lose", ""));
        res_parse_path_list(cJSON_GetObjectItem(ui, "view_icons_extra"),
                        &res->sprites.view_icons_extra, &res->sprites.view_icons_extra_count);
        res_copy_str(res->sprites.chrome_overworld,
                 sizeof(res->sprites.chrome_overworld),
                 res_json_str(ui, "chrome_overworld", ""));
        res_copy_str(res->sprites.splash_logo,
                 sizeof(res->sprites.splash_logo),
                 res_json_str(ui, "splash_logo", ""));
        res_copy_str(res->sprites.splash_title,
                 sizeof(res->sprites.splash_title),
                 res_json_str(ui, "splash_title", ""));
        res_copy_str(res->sprites.alcove_portrait, sizeof(res->sprites.alcove_portrait),
                 res_json_str(ui, "alcove_portrait", ""));
        res_copy_str(res->sprites.title_battle, sizeof(res->sprites.title_battle),
                 res_json_str(ui, "title_battle", ""));
        res_copy_str(res->sprites.title_eagle, sizeof(res->sprites.title_eagle),
                 res_json_str(ui, "title_eagle", ""));
        res_copy_str(res->sprites.title_words, sizeof(res->sprites.title_words),
                 res_json_str(ui, "title_words", ""));
        res_copy_str(res->sprites.class_picker,
                 sizeof(res->sprites.class_picker),
                 res_json_str(ui, "class_picker", ""));
        res_copy_str(res->sprites.class_highlight,
                 sizeof(res->sprites.class_highlight),
                 res_json_str(ui, "class_highlight", ""));
        res_parse_path_list(cJSON_GetObjectItem(ui, "class_picker_selected"),
                        &res->sprites.class_picker_selected, &res->sprites.class_picker_selected_count);
        cJSON *cols = cJSON_GetObjectItem(ui, "class_picker_columns");
        res->sprites.class_picker_column_count = 0;
        if (cJSON_IsArray(cols)) {
            int nc = cJSON_GetArraySize(cols);
            if (nc > RES_PICKER_COLUMNS) nc = RES_PICKER_COLUMNS;
            res_json_int_array(ui, "class_picker_columns", res->sprites.class_picker_columns, nc);
            res->sprites.class_picker_column_count = nc;
        }
    }

    cJSON *hud = cJSON_GetObjectItem(obj, "hud");
    if (cJSON_IsObject(hud)) {
        res_copy_str(res->sprites.hud_contract_silhouette,
                 sizeof(res->sprites.hud_contract_silhouette),
                 res_json_str(hud, "contract_silhouette", ""));
        res_copy_str(res->sprites.hud_boat_silhouette,
                 sizeof(res->sprites.hud_boat_silhouette),
                 res_json_str(hud, "boat_silhouette", ""));
        res_copy_str(res->sprites.hud_siege_silhouette,
                 sizeof(res->sprites.hud_siege_silhouette),
                 res_json_str(hud, "siege_silhouette", ""));
        res_parse_path_list(cJSON_GetObjectItem(hud, "siege_animation"),
                        &res->sprites.hud_siege_animation, &res->sprites.hud_siege_animation_count);
        res_copy_str(res->sprites.hud_magic_silhouette,
                 sizeof(res->sprites.hud_magic_silhouette),
                 res_json_str(hud, "magic_silhouette", ""));
        res_parse_path_list(cJSON_GetObjectItem(hud, "magic_animation"),
                        &res->sprites.hud_magic_animation, &res->sprites.hud_magic_animation_count);
        res_copy_str(res->sprites.hud_puzzle_grid,
                 sizeof(res->sprites.hud_puzzle_grid),
                 res_json_str(hud, "puzzle_grid", ""));
        res_copy_str(res->sprites.hud_gold_purse,
                 sizeof(res->sprites.hud_gold_purse),
                 res_json_str(hud, "gold_purse", ""));
        res_copy_str(res->sprites.hud_days, sizeof(res->sprites.hud_days),
                 res_json_str(hud, "days", ""));
        res_copy_str(res->sprites.hud_bar_strip,
                 sizeof(res->sprites.hud_bar_strip),
                 res_json_str(hud, "bar_strip", ""));
    }
    cJSON *rail = cJSON_GetObjectItem(obj, "rail");
    if (cJSON_IsObject(rail)) {
        res_copy_str(res->sprites.rail_menu,   sizeof(res->sprites.rail_menu),
                 res_json_str(rail, "menu", ""));
        res_copy_str(res->sprites.rail_map,    sizeof(res->sprites.rail_map),
                 res_json_str(rail, "map", ""));
        res_copy_str(res->sprites.rail_army,   sizeof(res->sprites.rail_army),
                 res_json_str(rail, "army", ""));
        res_copy_str(res->sprites.rail_goto,   sizeof(res->sprites.rail_goto),
                 res_json_str(rail, "goto", ""));
        res_copy_str(res->sprites.rail_cast,   sizeof(res->sprites.rail_cast),
                 res_json_str(rail, "cast", ""));
    }
    cJSON *cpan = cJSON_GetObjectItem(obj, "combat_panel");
    if (cJSON_IsObject(cpan)) {
        res_copy_str(res->sprites.combat_shoot, sizeof(res->sprites.combat_shoot),
                 res_json_str(cpan, "shoot", ""));
        res_copy_str(res->sprites.combat_wait,  sizeof(res->sprites.combat_wait),
                 res_json_str(cpan, "wait", ""));
        res_copy_str(res->sprites.combat_fly,   sizeof(res->sprites.combat_fly),
                 res_json_str(cpan, "fly", ""));
    }
}

// ---- Audio (background music tracks) -------------------------------------

static void parse_audio(Resources *res, cJSON *obj) {
    if (!cJSON_IsObject(obj)) return;
    cJSON *tracks = cJSON_GetObjectItem(obj, "tracks");
    if (cJSON_IsObject(tracks)) {
        res_copy_str(res->audio.openworld_path, sizeof(res->audio.openworld_path),
                 res_json_str(tracks, "openworld", ""));
        res_copy_str(res->audio.combat_path, sizeof(res->audio.combat_path),
                 res_json_str(tracks, "combat", ""));
        res_copy_str(res->audio.intro_path, sizeof(res->audio.intro_path),
                 res_json_str(tracks, "intro", ""));
    }
    cJSON *tunes = cJSON_GetObjectItem(obj, "tunes");
    if (cJSON_IsObject(tunes)) {
        res_copy_str(res->audio.tune_walk,   sizeof(res->audio.tune_walk),
                 res_json_str(tunes, "walk", ""));
        res_copy_str(res->audio.tune_bump,   sizeof(res->audio.tune_bump),
                 res_json_str(tunes, "bump", ""));
        res_copy_str(res->audio.tune_chest,  sizeof(res->audio.tune_chest),
                 res_json_str(tunes, "chest", ""));
        res_copy_str(res->audio.tune_defeat, sizeof(res->audio.tune_defeat),
                 res_json_str(tunes, "defeat", ""));
        res_copy_str(res->audio.tune_victory, sizeof(res->audio.tune_victory),
                 res_json_str(tunes, "victory", ""));
    }
}

// ---- Combat, controls, credits, ending -----------------------------------

static void parse_combat(Resources *res, cJSON *obj) {
    // Defaults.
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++)
            res->morale_chart[i][j] = 'N';
    res->number_name_count = 0;
    res->morale_as_army_view = false;
    res->field_obstacle_chance = 10;
    res->guardian_full_band = false;
    if (!cJSON_IsObject(obj)) return;
    res->morale_as_army_view = cJSON_IsTrue(cJSON_GetObjectItem(obj, "morale_as_army_view"));
    res->guardian_full_band = cJSON_IsTrue(cJSON_GetObjectItem(obj, "guardian_full_band"));
    cJSON *foc = cJSON_GetObjectItem(obj, "field_obstacle_chance");
    if (cJSON_IsNumber(foc) && foc->valueint >= 0 && foc->valueint <= 100)
        res->field_obstacle_chance = foc->valueint;

    cJSON *mc = cJSON_GetObjectItem(obj, "morale_chart");
    if (cJSON_IsArray(mc)) {
        int row = 0;
        cJSON *jrow;
        cJSON_ArrayForEach(jrow, mc) {
            if (row >= 5) break;
            if (!cJSON_IsArray(jrow)) { row++; continue; }
            int col = 0;
            cJSON *cell;
            cJSON_ArrayForEach(cell, jrow) {
                if (col >= 5) break;
                if (cJSON_IsString(cell) && cell->valuestring[0]) {
                    char v = cell->valuestring[0];
                    if (v == 'N' || v == 'L' || v == 'H')
                        res->morale_chart[row][col] = v;
                }
                col++;
            }
            row++;
        }
    }

    // (controls parsed separately at root; see parse_controls)
    // number_names: array of {"min": N, "label": "..."}, ordered high-to-low.
    cJSON *nn = cJSON_GetObjectItem(obj, "number_names");
    int nn_cap = res_json_len(nn);
    int nn_labels = 0;   // the labels' own count mirrors number_name_count
    if (cJSON_IsArray(nn) && RES_TABLE_ALLOC(res->number_name_thresholds, res->number_name_count, nn_cap) &&
        RES_TABLE_ALLOC(res->number_name_labels, nn_labels, nn_cap)) {
        (void)nn_labels;
        int i = 0;
        cJSON *it;
        cJSON_ArrayForEach(it, nn) {
            if (i >= nn_cap) break;
            if (!cJSON_IsObject(it)) continue;
            res->number_name_thresholds[i] = res_json_int(it, "min", 1);
            res_copy_str(res->number_name_labels[i],
                     sizeof(res->number_name_labels[i]),
                     res_json_str(it, "label", ""));
            i++;
        }
        res->number_name_count = i;
    }
}

static void parse_controls(Resources *res, cJSON *obj) {
    res->controls.count = 0;
    if (!cJSON_IsObject(obj)) return;
    cJSON *settings = cJSON_GetObjectItem(obj, "settings");
    if (!cJSON_IsArray(settings)) return;
    cJSON *it;
    int n = 0;
    cJSON_ArrayForEach(it, settings) {
        // A row is bound to Game.stats.options by position, which holds 7.
        if (n >= 7) break;
        if (!cJSON_IsObject(it)) continue;
        res_copy_str(res->controls.items[n].id,
                 sizeof(res->controls.items[n].id),
                 res_json_str(it, "id", ""));
        res_copy_str(res->controls.items[n].label,
                 sizeof(res->controls.items[n].label),
                 res_json_str(it, "label", ""));
        res_copy_str(res->controls.items[n].type,
                 sizeof(res->controls.items[n].type),
                 res_json_str(it, "type", "bool"));
        res->controls.items[n].range = res_json_int(it, "range", 2);
        cJSON *h = cJSON_GetObjectItem(it, "hidden");
        res->controls.items[n].hidden = cJSON_IsTrue(h);
        res->controls.items[n].audio = cJSON_IsTrue(cJSON_GetObjectItem(it, "audio"));
        n++;
    }
    res->controls.count = n;
}

static void parse_credits(Resources *res, cJSON *obj) {
    res->credits.group_count = 0;
    res->credits.copyright_count = 0;
    res->credits.image[0] = '\0';
    if (!cJSON_IsObject(obj)) return;

    res_copy_str(res->credits.image, sizeof(res->credits.image),
             res_json_str(obj, "image", ""));

    cJSON *groups = cJSON_GetObjectItem(obj, "groups");
    int g_cap = res_json_len(groups);
    if (cJSON_IsArray(groups) && RES_TABLE_ALLOC(res->credits.groups, res->credits.group_count, g_cap)) {
        cJSON *g;
        cJSON_ArrayForEach(g, groups) {
            if (res->credits.group_count >= g_cap) break;
            if (!cJSON_IsObject(g)) continue;
            int gi = res->credits.group_count;
            res_copy_str(res->credits.groups[gi].label,
                     sizeof(res->credits.groups[gi].label),
                     res_json_str(g, "label", ""));
            cJSON *names = cJSON_GetObjectItem(g, "names");
            int nm_cap = res_json_len(names);
            if (cJSON_IsArray(names) &&
                RES_TABLE_ALLOC(res->credits.groups[gi].names, res->credits.groups[gi].name_count, nm_cap)) {
                cJSON *nm;
                cJSON_ArrayForEach(nm, names) {
                    int ni = res->credits.groups[gi].name_count;
                    if (ni >= nm_cap) break;
                    if (!cJSON_IsString(nm)) continue;
                    res_copy_str(res->credits.groups[gi].names[ni],
                             sizeof(res->credits.groups[gi].names[ni]),
                             nm->valuestring);
                    res->credits.groups[gi].name_count++;
                }
            }
            res->credits.group_count++;
        }
    }

    cJSON *copyr = cJSON_GetObjectItem(obj, "copyright");
    int cr_cap = res_json_len(copyr);
    if (cJSON_IsArray(copyr) && RES_TABLE_ALLOC(res->credits.copyright, res->credits.copyright_count, cr_cap)) {
        cJSON *c;
        cJSON_ArrayForEach(c, copyr) {
            if (res->credits.copyright_count >= cr_cap) break;
            if (!cJSON_IsString(c)) continue;
            res_copy_str(res->credits.copyright[res->credits.copyright_count],
                     sizeof(res->credits.copyright[res->credits.copyright_count]),
                     c->valuestring);
            res->credits.copyright_count++;
        }
    }
}

static void parse_ending(Resources *res, cJSON *obj) {
    // display_cartoon defaults (game.c:4281). These fire even if
    // the block is missing from game.json so a modpack that only sets
    // tile paths still animates correctly.
    res->ending.grid_width     = 6;
    res->ending.grid_height    = 5;
    res->ending.carpet_column  = 4;
    res->ending.carpet_length  = 5;
    res->ending.frame_count    = 10;
    res->ending.ticks_per_step = 2;
    res->ending.troop_border   = true;
    res->ending.grass_tile[0]      = '\0';
    res->ending.carpet_tile[0]     = '\0';
    res->ending.hero_tile[0]       = '\0';
    if (!cJSON_IsObject(obj)) return;
    res_copy_str(res->ending.grass_tile,
             sizeof(res->ending.grass_tile),
             res_json_str(obj, "grass_tile", ""));
    res_copy_str(res->ending.carpet_tile,
             sizeof(res->ending.carpet_tile),
             res_json_str(obj, "carpet_tile", ""));
    res_copy_str(res->ending.hero_tile,
             sizeof(res->ending.hero_tile),
             res_json_str(obj, "hero_tile", ""));
    res->ending.grid_width     = res_json_int(obj, "grid_width",     res->ending.grid_width);
    res->ending.grid_height    = res_json_int(obj, "grid_height",    res->ending.grid_height);
    res->ending.carpet_column  = res_json_int(obj, "carpet_column",  res->ending.carpet_column);
    res->ending.carpet_length  = res_json_int(obj, "carpet_length",  res->ending.carpet_length);
    res->ending.frame_count    = res_json_int(obj, "frame_count",    res->ending.frame_count);
    res->ending.ticks_per_step = res_json_int(obj, "ticks_per_step", res->ending.ticks_per_step);
    cJSON *tb = cJSON_GetObjectItem(obj, "troop_border");
    if (cJSON_IsBool(tb)) res->ending.troop_border = cJSON_IsTrue(tb);
}

// Banner defaults preserve  text, so a
// game.json missing strings.banners still produces parity-correct prompts.
// %TOKEN% placeholders are resolved at render time by resources_format_template.

// ---- Catalog lookups (tables.h API, backed by the singleton) -------------

const TroopDef *troop_by_id(const char *id) {
    if (!g_resources || !id) return NULL;
    for (int i = 0; i < g_resources->troops_count; i++) {
        if (strcmp(g_resources->troops[i].id, id) == 0) return &g_resources->troops[i];
    }
    return NULL;
}
const TroopDef *troop_by_index(int idx) {
    if (!g_resources || idx < 0 || idx >= g_resources->troops_count) return NULL;
    return &g_resources->troops[idx];
}
int troops_count(void) {
    return g_resources ? g_resources->troops_count : 0;
}

int troop_morale_groups(char *out, int cap) {
    // Distinct morale-group letters present in the catalog, ascending.
    int n = 0;
    if (!g_resources || !out || cap <= 0) return 0;
    for (int i = 0; i < g_resources->troops_count; i++) {
        char grp = g_resources->troops[i].morale_group;
        if (!grp) continue;
        int j = 0;
        while (j < n && out[j] < grp) j++;
        if (j < n && out[j] == grp) continue;
        if (n >= cap) continue;
        for (int k = n; k > j; k--) out[k] = out[k - 1];
        out[j] = grp;
        n++;
    }
    return n;
}

const SpellDef *spell_by_id(const char *id) {
    if (!g_resources || !id) return NULL;
    for (int i = 0; i < g_resources->spells_count; i++) {
        if (strcmp(g_resources->spells[i].id, id) == 0) return &g_resources->spells[i];
    }
    return NULL;
}
const SpellDef *spell_by_index(int idx) {
    if (!g_resources || idx < 0 || idx >= g_resources->spells_count) return NULL;
    return &g_resources->spells[idx];
}
int spells_count(void) {
    return g_resources ? g_resources->spells_count : 0;
}
int spell_index_by_id(const char *id) {
    const SpellDef *sp = spell_by_id(id);
    return sp ? sp->index : -1;
}

const ClassDef *class_by_id(const char *id) {
    if (!g_resources || !id) return NULL;
    for (int i = 0; i < g_resources->classes_count; i++) {
        if (strcmp(g_resources->classes[i].id, id) == 0) return &g_resources->classes[i];
    }
    return NULL;
}
const ClassDef *class_by_index(int idx) {
    if (!g_resources || idx < 0 || idx >= g_resources->classes_count) return NULL;
    return &g_resources->classes[idx];
}
int classes_count(void) {
    return g_resources ? g_resources->classes_count : 0;
}

const VillainDef *villain_by_id(const char *id) {
    if (!g_resources || !id) return NULL;
    for (int i = 0; i < g_resources->villains_count; i++) {
        if (strcmp(g_resources->villains[i].id, id) == 0) return &g_resources->villains[i];
    }
    return NULL;
}
const VillainDef *villain_by_index(int idx) {
    if (!g_resources || idx < 0 || idx >= g_resources->villains_count) return NULL;
    return &g_resources->villains[idx];
}
int villains_count(void) {
    return g_resources ? g_resources->villains_count : 0;
}

const ArtifactDef *artifact_by_id(const char *id) {
    if (!g_resources || !id) return NULL;
    for (int i = 0; i < g_resources->artifacts_count; i++) {
        if (strcmp(g_resources->artifacts[i].id, id) == 0) return &g_resources->artifacts[i];
    }
    return NULL;
}
const ArtifactDef *artifact_by_index(int idx) {
    if (!g_resources || idx < 0 || idx >= g_resources->artifacts_count) return NULL;
    return &g_resources->artifacts[idx];
}
int artifacts_count(void) {
    return g_resources ? g_resources->artifacts_count : 0;
}

int artifact_index_for_tile(const char *zone, int local_idx) {
    if (!g_resources || !zone) return -1;
    for (int i = 0; i < g_resources->artifacts_count; i++) {
        const ArtifactDef *a = &g_resources->artifacts[i];
        if (a->local_idx == local_idx && strcmp(a->zone, zone) == 0) {
            return a->index;
        }
    }
    return -1;
}

// ---- Top-level -------------------------------------------------------------

// Load the pack's string catalog for `lang` from strings/<lang>.json, falling
// back to the base locale <base> when the requested locale file is absent. The
// engine carries no text of its own, so a missing/unparseable base locale is a
// hard load failure. Returns a cJSON the caller must cJSON_Delete, or NULL.
static cJSON *load_locale_strings(const char *lang, const char *base) {
    char path[128];
    snprintf(path, sizeof path, "strings/%s.json", lang);
    char *txt = res_slurp(path);
    if (!txt && base && base[0] && strcmp(lang, base) != 0) {
        fprintf(stdout, "resources: locale '%s' not found, using base locale '%s'\n",
                lang, base);
        snprintf(path, sizeof path, "strings/%s.json", base);
        txt = res_slurp(path);
    }
    if (!txt) {
        fprintf(stdout, "resources: could not read strings locale file '%s'\n", path);
        return NULL;
    }
    cJSON *j = cJSON_Parse(txt);
    free(txt);
    if (!j) fprintf(stdout, "resources: could not parse %s\n", path);
    return j;
}

bool resources_load(Resources *res, const char *manifest_path) {
    memset(res, 0, sizeof(*res));

    // The manifest path is always pack-relative now (typically just
    // "game.json"). The active pack on the global pack stack
    // (engine/pack.c) does the actual byte lookup.
    const char *manifest_rel = (manifest_path && manifest_path[0])
                                   ? manifest_path : "game.json";
    // Convenience: if a caller passes a disk-style path like
    // "assets/kings-bounty/game.json" AND no pack is currently on the
    // stack, auto-open the dirname as a directory pack so simple test
    // helpers and tools keep working. Otherwise just strip the prefix
    // and read the basename from whatever pack is already active.
    const char *slash = strrchr(manifest_rel, '/');
    if (slash) {
        if (!pack_stack_top()) {
            char dir[512];
            size_t dn = (size_t)(slash - manifest_rel);
            if (dn >= sizeof dir) dn = sizeof dir - 1;
            memcpy(dir, manifest_rel, dn);
            dir[dn] = '\0';
            Pack *p = pack_open(dir);
            if (!p) {
                fprintf(stdout, "resources: failed to open pack at %s\n", dir);
                return false;
            }
            pack_stack_push(p);
        }
        manifest_rel = slash + 1;
    }

    char *text = res_slurp(manifest_rel);
    if (!text) {
        fprintf(stdout, "resources: failed to read %s\n", manifest_rel);
        return false;
    }
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        fprintf(stdout, "resources: failed to parse %s\n", manifest_rel);
        return false;
    }

    res_copy_str(res->title,   sizeof(res->title),   res_json_str(root, "title", ""));
    res_copy_str(res->pack_id,   sizeof(res->pack_id),   res_json_str(root, "pack_id", ""));
    res_copy_str(res->pack_name, sizeof(res->pack_name), res_json_str(root, "pack_name", ""));
    res->version = res_json_int(root, "version", 1);

    cJSON *jtime = cJSON_GetObjectItem(root, "time");
    res->time.day_steps = res_json_int(jtime, "day_steps", 40);
    res->time.week_days = res_json_int(jtime, "week_days", 5);
    cJSON *jdpd = cJSON_GetObjectItem(jtime, "days_per_difficulty");
    res->time.days_per_difficulty[0] = res_json_int(jdpd, "easy",       900);
    res->time.days_per_difficulty[1] = res_json_int(jdpd, "normal",     600);
    res->time.days_per_difficulty[2] = res_json_int(jdpd, "hard",       400);
    res->time.days_per_difficulty[3] = res_json_int(jdpd, "impossible", 200);

    cJSON *jec = cJSON_GetObjectItem(root, "economy");
    res->economy.alcove_cost      = res_json_int(jec, "alcove_cost",     5000);
    {
        cJSON *jmg = cJSON_GetObjectItem(root, "magic");
        cJSON *jrp = cJSON_IsObject(jmg) ? cJSON_GetObjectItem(jmg, "rites_per_zone") : NULL;
        res->economy.rites_per_zone = cJSON_IsTrue(jrp);
        res->economy.spell_limit_per_spell =
            cJSON_IsObject(jmg) && cJSON_IsTrue(cJSON_GetObjectItem(jmg, "max_per_spell"));
        res->economy.spell_weekly_renewal =
            cJSON_IsObject(jmg) && cJSON_IsTrue(cJSON_GetObjectItem(jmg, "weekly_renewal"));
        cJSON *jfo = cJSON_GetObjectItem(root, "foes");
        cJSON *jev = cJSON_IsObject(jfo) ? cJSON_GetObjectItem(jfo, "evade_needs_free_square") : NULL;
        res->economy.evade_needs_free_square = cJSON_IsTrue(jev);
        cJSON *jau = cJSON_GetObjectItem(root, "audiences");
        res->economy.audiences = cJSON_IsObject(jau);
        res->economy.blessing_leadership_pct = res_json_int(jau, "blessing_leadership_pct", 50);
        res->economy.tribute_cost            = res_json_int(jau, "tribute_cost", 50000);
        res->economy.tribute_leadership_pct  = res_json_int(jau, "tribute_leadership_pct", 25);
        res->economy.tribute_magic_pct       = res_json_int(jau, "tribute_magic_pct", 25);
    }
    res->economy.boat_cost_normal = res_json_int(jec, "boat_cost_normal", 500);
    res->economy.boat_cost_cheap  = res_json_int(jec, "boat_cost_cheap",  100);
    res->economy.siege_cost       = res_json_int(jec, "siege_cost",      3000);
    res->economy.unpaid_troops_leave = cJSON_IsTrue(cJSON_GetObjectItem(jec, "unpaid_troops_leave"));

    // Chest curves and value ranges --  defaults so
    // omitting the JSON block still produces parity-correct rolls.
    {
        ResChest *ch = &res->economy.chest;
        static const int def_chance_gold[4]       = { 0x3d, 0x42, 0x4c, 0x47 };
        static const int def_chance_commission[4] = { 0x51, 0x56, 0x56, 0x51 };
        // OpenBounty divergence: the original tables set spell_power and
        // max_spells to identical thresholds, so the spells-known chest reward
        // was unreachable (see OPENKB-SPEC 13.3). We lower chance_spell_power so
        // the [chance_spell_power, chance_max_spells) window opens and the
        // reward can roll. Every other reward's window is byte-identical.
        static const int def_chance_spell_power[4]= { 0x53, 0x59, 0x59, 0x56 };
        static const int def_chance_max_spells[4] = { 0x56, 0x5c, 0x5d, 0x5b };
        static const int def_chance_new_spell[4]  = { 0x65, 0x65, 0x65, 0x65 };
        static const int def_gold_min[4]          = { 0x00, 0x04, 0x09, 0x13 };
        static const int def_gold_max[4]          = { 0x05, 0x10, 0x15, 0x1f };
        static const int def_commission_min[4]    = { 0x09, 0x31, 0x63, 0xc7 };
        static const int def_commission_max[4]    = { 0x29, 0x33, 0x65, 0x12d };
        static const int def_max_spells_base[4]   = { 0x01, 0x01, 0x02, 0x02 };
        memcpy(ch->chance_gold,        def_chance_gold,        sizeof def_chance_gold);
        memcpy(ch->chance_commission,  def_chance_commission,  sizeof def_chance_commission);
        memcpy(ch->chance_spell_power, def_chance_spell_power, sizeof def_chance_spell_power);
        memcpy(ch->chance_max_spells,  def_chance_max_spells,  sizeof def_chance_max_spells);
        memcpy(ch->chance_new_spell,   def_chance_new_spell,   sizeof def_chance_new_spell);
        memcpy(ch->gold_min,           def_gold_min,           sizeof def_gold_min);
        memcpy(ch->gold_max,           def_gold_max,           sizeof def_gold_max);
        memcpy(ch->commission_min,     def_commission_min,     sizeof def_commission_min);
        memcpy(ch->commission_max,     def_commission_max,     sizeof def_commission_max);
        memcpy(ch->max_spells_base,    def_max_spells_base,    sizeof def_max_spells_base);

        cJSON *jch = cJSON_GetObjectItem(jec, "chest");
        if (cJSON_IsObject(jch)) {
            res_json_int_array(jch, "chance_gold",        ch->chance_gold,        4);
            res_json_int_array(jch, "chance_commission",  ch->chance_commission,  4);
            res_json_int_array(jch, "chance_spell_power", ch->chance_spell_power, 4);
            res_json_int_array(jch, "chance_max_spells",  ch->chance_max_spells,  4);
            res_json_int_array(jch, "chance_new_spell",   ch->chance_new_spell,   4);
            res_json_int_array(jch, "gold_min",           ch->gold_min,           4);
            res_json_int_array(jch, "gold_max",           ch->gold_max,           4);
            res_json_int_array(jch, "commission_min",     ch->commission_min,     4);
            res_json_int_array(jch, "commission_max",     ch->commission_max,     4);
            res_json_int_array(jch, "max_spells_base",    ch->max_spells_base,    4);
        }
    }

    // Score formula. Defaults match the canonical balance so omitting
    // the JSON block leaves balance unchanged.
    {
        ResScoring *sc = &res->economy.scoring;
        sc->per_villain  = 500;
        sc->per_artifact = 250;
        sc->per_castle   = 100;
        sc->kill_penalty = 1;
        static const int def_mult[5] = { 0, 1, 2, 4, 8 };
        memcpy(sc->difficulty_multiplier, def_mult, sizeof def_mult);
        sc->easy_halves = true;

        cJSON *jsc = cJSON_GetObjectItem(jec, "scoring");
        if (cJSON_IsObject(jsc)) {
            sc->per_villain  = res_json_int(jsc, "per_villain",         sc->per_villain);
            sc->per_artifact = res_json_int(jsc, "per_artifact",        sc->per_artifact);
            sc->per_castle   = res_json_int(jsc, "per_castle",          sc->per_castle);
            sc->kill_penalty = res_json_int(jsc, "kill_penalty",        sc->kill_penalty);
            res_json_int_array(jsc, "difficulty_multiplier",
                           sc->difficulty_multiplier, 5);
            cJSON *eh = cJSON_GetObjectItem(jsc, "easy_halves");
            if (cJSON_IsBool(eh)) sc->easy_halves = cJSON_IsTrue(eh);
        }
    }

    // Tuning knobs (game.json "tuning" block). Defaults are the
    // baseline values; mods can override.
    {
        ResTuning *tn = &res->tuning;
        tn->instant_army_multiplier[0] = 3;
        tn->instant_army_multiplier[1] = 2;
        tn->instant_army_multiplier[2] = 1;
        tn->instant_army_multiplier[3] = 1;
        tn->search_cost_days           = 10;
        tn->temp_death_troop[0]        = '\0';   // resolved after parse_troops
        tn->temp_death_count           = 20;
        cJSON *jtn = cJSON_GetObjectItem(root, "tuning");
        if (cJSON_IsObject(jtn)) {
            cJSON *jia = cJSON_GetObjectItem(jtn, "instant_army_multiplier");
            if (cJSON_IsArray(jia)) {
                int n = cJSON_GetArraySize(jia);
                if (n > 4) n = 4;
                for (int i = 0; i < n; i++) {
                    cJSON *e = cJSON_GetArrayItem(jia, i);
                    if (cJSON_IsNumber(e)) tn->instant_army_multiplier[i] = e->valueint;
                }
            }
            tn->search_cost_days = res_json_int(jtn, "search_cost_days",
                                            tn->search_cost_days);
            cJSON *jtd = cJSON_GetObjectItem(jtn, "temp_death");
            if (cJSON_IsObject(jtd)) {
                res_copy_str(tn->temp_death_troop, sizeof(tn->temp_death_troop),
                         res_json_str(jtd, "troop", ""));
                tn->temp_death_count = res_json_int(jtd, "count",
                                                tn->temp_death_count);
            }
        }
    }

    // Color tables (game.json "colors" block). Defaults match openbounty
    // 256-color VGA palette mappings used historically in the source.
    {
        ResColors *col = &res->colors;
        // Minimap defaults -- EGA palette indices applied to
        // openbounty' VGA palette (these RGB values are what PAL_CLR(...)
        // resolves to at runtime).
        col->minimap_grass    = 0xFF00AA00u; // DGREEN
        col->minimap_forest   = 0xFF55FF55u; // GREEN
        col->minimap_mountain = 0xFFAA5500u; // BROWN
        col->minimap_water    = 0xFF5555FFu; // BLUE
        col->minimap_desert   = 0xFFFFFF55u; // YELLOW
        col->minimap_fog      = 0xFF000000u; // BLACK
        // Difficulty bar -- EGA RGBs (chrome.c historical comment).
        col->difficulty_easy       = 0xFF00AAAAu; // EGA_DCYAN
        col->difficulty_normal     = 0xFFAA0000u; // EGA_DRED
        col->difficulty_hard       = 0xFF5555FFu; // EGA_BLUE
        col->difficulty_impossible = 0xFFAA00AAu; // EGA_DVIOLET

        cJSON *jcol = cJSON_GetObjectItem(root, "colors");
        if (cJSON_IsObject(jcol)) {
            cJSON *mm = cJSON_GetObjectItem(jcol, "minimap_terrain");
            if (cJSON_IsObject(mm)) {
                res_json_color(mm, "grass",    &col->minimap_grass);
                res_json_color(mm, "forest",   &col->minimap_forest);
                res_json_color(mm, "mountain", &col->minimap_mountain);
                res_json_color(mm, "water",    &col->minimap_water);
                res_json_color(mm, "desert",   &col->minimap_desert);
                res_json_color(mm, "fog",      &col->minimap_fog);
            }
            cJSON *db = cJSON_GetObjectItem(jcol, "difficulty_bar");
            if (cJSON_IsObject(db)) {
                res_json_color(db, "easy",       &col->difficulty_easy);
                res_json_color(db, "normal",     &col->difficulty_normal);
                res_json_color(db, "hard",       &col->difficulty_hard);
                res_json_color(db, "impossible", &col->difficulty_impossible);
            }
        }
    }

    cJSON *jct = cJSON_GetObjectItem(root, "contract");
    res->contract.cycle_length          = res_json_int(jct, "cycle_length", 5);
    res->contract.initial_last_contract = res_json_int(jct, "initial_last_contract", 4);

    // spawn: monster-generation tables (troop_chance_table +
    // dwelling_to_troop). Missing entries leave zeroed defaults; callers
    // must guard for empty pools.
    memset(&res->spawn, 0, sizeof(res->spawn));
    cJSON *jsp = cJSON_GetObjectItem(root, "spawn");
    if (cJSON_IsObject(jsp)) {
        ResSpawn *sp = &res->spawn;
        // The calm start (REQ-283): absent radius = 0 = off.
        sp->calm_radius     = res_json_int(jsp, "calm_radius", 0);
        sp->calm_max_slot   = res_json_int(jsp, "calm_max_slot", 1);
        sp->calm_max_stacks = res_json_int(jsp, "calm_max_stacks", 2);
        cJSON *jcc = cJSON_GetObjectItem(jsp, "tier_chance_curve");
        if (cJSON_IsArray(jcc)) {
            int ti = 0;
            cJSON *row;
            cJSON_ArrayForEach(row, jcc) {
                if (ti >= RES_SPAWN_TIERS) break;
                int cap = res_json_len(row);
                if (cJSON_IsArray(row) && RES_TABLE_ALLOC(sp->chance_curve[ti], sp->chance_curve_len[ti], cap)) {
                    cJSON *v;
                    cJSON_ArrayForEach(v, row) {
                        if (sp->chance_curve_len[ti] >= cap) break;
                        sp->chance_curve[ti][sp->chance_curve_len[ti]++] = cJSON_IsNumber(v) ? v->valueint : 0;
                    }
                }
                ti++;
            }
        }
        cJSON *jtp = cJSON_GetObjectItem(jsp, "tier_troop_pool");
        if (cJSON_IsArray(jtp)) {
            int ti = 0;
            cJSON *row;
            cJSON_ArrayForEach(row, jtp) {
                if (ti >= RES_SPAWN_TIERS) break;
                int cap = res_json_len(row);
                if (cJSON_IsArray(row) && RES_TABLE_ALLOC(sp->troop_pool[ti], sp->pool_count[ti], cap)) {
                    cJSON *v;
                    cJSON_ArrayForEach(v, row) {
                        if (sp->pool_count[ti] >= cap) break;
                        if (cJSON_IsString(v))
                            res_copy_str(sp->troop_pool[ti][sp->pool_count[ti]], RES_ID_LEN, v->valuestring);
                        sp->pool_count[ti]++;
                    }
                }
                ti++;
            }
        }
        // A pool longer than five carries its own curve per difficulty tier.
        cJSON *jkc = cJSON_GetObjectItem(jsp, "kind_chance_curve");
        if (cJSON_IsArray(jkc)) {
            int ki = 0;
            cJSON *kind;
            cJSON_ArrayForEach(kind, jkc) {
                if (ki >= RES_SPAWN_TIERS) break;
                if (cJSON_IsArray(kind)) {
                    sp->kind_curve_set[ki] = true;
                    int ti = 0;
                    cJSON *row;
                    cJSON_ArrayForEach(row, kind) {
                        if (ti >= RES_SPAWN_TIERS) break;
                        int cap = res_json_len(row);
                        if (RES_TABLE_ALLOC(sp->kind_curve[ki][ti], sp->kind_curve_len[ki][ti], cap)) {
                            cJSON *v;
                            cJSON_ArrayForEach(v, row) {
                                if (sp->kind_curve_len[ki][ti] >= cap) break;
                                sp->kind_curve[ki][ti][sp->kind_curve_len[ki][ti]++] =
                                    cJSON_IsNumber(v) ? v->valueint : 0;
                            }
                        }
                        ti++;
                    }
                }
                ki++;
            }
        }
    }

    // Optional TrueType font (modern packs). The strip in sprites.font stays
    // the fallback; the shell prefers this when it loads.
    {
        cJSON *jf = cJSON_GetObjectItem(root, "font");
        memset(&res->font, 0, sizeof res->font);
        if (jf && cJSON_IsObject(jf)) {
            res_copy_str(res->font.file, sizeof res->font.file, res_json_str(jf, "file", ""));
            res_copy_str(res->font.license, sizeof res->font.license, res_json_str(jf, "license", ""));
            res->font.size = res_json_int(jf, "size", 0);
            cJSON *jc = cJSON_GetObjectItem(jf, "caps");
            res->font.caps = (jc && cJSON_IsTrue(jc)) ? 1 : 0;
            if (!res->font.file[0] || res->font.size < 0 ||
                (res->font.size && (res->font.size < 6 || res->font.size > 64))) {
                fprintf(stdout,
                        "resources: font block needs a file and a size of 6..64 "
                        "(or none) (got \"%s\", %d)\n",
                        res->font.file, res->font.size);
                cJSON_Delete(root);
                return false;
            }
        }
    }

    // Render geometry. Required: a pack is authored for one mode or the other
    // and guessing would silently mis-size every tile. resources_load returns
    // false when it is missing, and the caller reports it fatally.
    {
        cJSON *jr = cJSON_GetObjectItem(root, "render");
        const char *mode = res_json_str(jr, "mode", "");
        if (strcmp(mode, "legacy") == 0) {
            res->render.mode    = RENDER_MODE_LEGACY;
            res->render.tile_w  = 48;
            res->render.tile_h  = 34;
            res->render.tiles_w = 5;
            res->render.tiles_h = 5;
            res->render.ui_scale = 1;
            res->render.dim = 0;
        } else if (strcmp(mode, "modern") == 0) {
            res->render.mode    = RENDER_MODE_MODERN;
            res->render.tile_w  = res_json_int(jr, "tile_w",  96);
            res->render.tile_h  = res_json_int(jr, "tile_h",  96);
            res->render.tiles_w = res_json_int(jr, "tiles_w",  7);
            res->render.tiles_h = res_json_int(jr, "tiles_h",  7);
            res->render.ui_scale = res_json_int(jr, "ui_scale", 1);
            res->render.native_w = res_json_int(jr, "native_w", 0);
            res->render.native_h = res_json_int(jr, "native_h", 0);
            res->render.dim = res_json_int(jr, "dim", 55);
            if (res->render.dim < 0) res->render.dim = 0;
            if (res->render.dim > 100) res->render.dim = 100;
        } else {
            res->render.mode = RENDER_MODE_NONE;
            res->render.ui_scale = 1;
            fprintf(stdout,
                    "resources: pack declares no render.mode "
                    "(expected \"legacy\" or \"modern\")\n");
            cJSON_Delete(root);
            return false;
        }
        // The viewport must be odd on both axes: map_render centres the hero
        // with RADIUS = tiles/2, and an even count leaves him half a tile off.
        if ((res->render.tiles_w % 2) == 0 || (res->render.tiles_h % 2) == 0 ||
            res->render.tiles_w < 3 || res->render.tiles_h < 3 ||
            res->render.tile_w  < 8 || res->render.tile_h  < 8 ||
            res->render.ui_scale < 1 || res->render.ui_scale > 8) {
            fprintf(stdout,
                    "resources: render viewport must be odd and at least 3x3 "
                    "with tiles at least 8px and ui_scale 1..8 "
                    "(got %dx%d tiles of %dx%d, ui_scale %d)\n",
                    res->render.tiles_w, res->render.tiles_h,
                    res->render.tile_w, res->render.tile_h,
                    res->render.ui_scale);
            cJSON_Delete(root);
            return false;
        }
        // A declared buffer must hold the frame, a one-tile column either side
        // of the map with its band, and the map at its whole tiles -- or the
        // battlefield, the same six by five tiles wide and tall whatever the
        // viewport, if that is bigger.
        {
            const ResRender *r = &res->render;
            int frame = RES_MODERN_FRAME * r->ui_scale, gap = RES_MODERN_GAP * r->ui_scale;
            int cols = r->tiles_w > COMBAT_W ? r->tiles_w : COMBAT_W;
            int rows = r->tiles_h > COMBAT_H ? r->tiles_h : COMBAT_H;
            int need_w = cols * r->tile_w + 2 * (r->tile_w + gap) + 2 * frame;
            int need_h = rows * r->tile_h + 2 * frame;
            bool none = (r->native_w == 0 && r->native_h == 0);
            if (!none && (r->native_w < need_w || r->native_h < need_h)) {
                fprintf(stdout,
                        "resources: render.native_w/native_h %dx%d cannot hold "
                        "the %dx%d viewport (needs at least %dx%d)\n",
                        r->native_w, r->native_h, r->tiles_w, r->tiles_h,
                        need_w, need_h);
                cJSON_Delete(root);
                return false;
            }
        }
    }

    cJSON *jw = cJSON_GetObjectItem(root, "world");
    // world.* user-facing strings are required from the pack too (no defaults).
    #define REQ_WORLD(field, key) do { \
        const char *s = res_json_str(jw, key, NULL); \
        if (s) res_copy_str(res->world.field, sizeof(res->world.field), s); \
        else { fprintf(stdout, "resources: pack missing string key 'world.%s'\n", key); \
               res->strings_missing++; } \
    } while (0)
    REQ_WORLD(starting_zone,    "starting_zone");
    REQ_WORLD(default_name,     "default_name");
    #undef REQ_WORLD
    // Base locale code (names the strings/<language>.json file). Optional;
    // defaults to English so a pack that omits it still resolves a locale.
    res_copy_str(res->world.language, sizeof res->world.language,
             res_json_str(jw, "language", "en"));
    res->world.clear_keeps_ground = cJSON_IsTrue(cJSON_GetObjectItem(jw, "clear_keeps_ground"));
    res->world.castle_gate_report = cJSON_IsTrue(cJSON_GetObjectItem(jw, "castle_gate_report"));
    cJSON *jdo = cJSON_GetObjectItem(jw, "default_options");
    // Fallback defaults: delay, sounds, walk_beep, anim, cga, music, volume.
    static const int default_options_fallback[7] = { 4, 1, 1, 1, 1, 0, 5 };
    for (int i = 0; i < 7; i++) res->world.default_options[i] = default_options_fallback[i];
    if (cJSON_IsArray(jdo)) {
        int i = 0;
        cJSON *v;
        cJSON_ArrayForEach(v, jdo) {
            if (i >= 7) break;
            if (cJSON_IsNumber(v)) res->world.default_options[i] = v->valueint;
            i++;
        }
    }


    parse_towns(res,       cJSON_GetObjectItem(root, "towns"));
    parse_castles(res,     cJSON_GetObjectItem(root, "castles"));
    parse_zones(res,       cJSON_GetObjectItem(root, "zones"));
    parse_tile_codes(res,  cJSON_GetObjectItem(root, "tile_codes"));
    parse_map_art(res,     cJSON_GetObjectItem(root, "map_art"));
    if (!collect_object_arts(res)) {
        cJSON_Delete(root);
        return false;
    }

    parse_troops(res,      cJSON_GetObjectItem(root, "troops"));
    if (!parse_troop_aliases(res, cJSON_GetObjectItem(root, "troop_aliases"))) {
        cJSON_Delete(root);
        return false;
    }
    parse_spells(res,      cJSON_GetObjectItem(root, "spells"));
    parse_classes(res,     cJSON_GetObjectItem(root, "classes"));
    parse_villains(res,    cJSON_GetObjectItem(root, "villains"));
    parse_portraits(res,   cJSON_GetObjectItem(root, "portraits"));
    parse_artifacts(res,   cJSON_GetObjectItem(root, "artifacts"));

    // Catalog-capacity contracts: a pack that exceeds a fixed engine array
    // must fail loudly at load, never be silently clamped into a world that
    // misrepresents it.
    {
        // The caught/prefought arrays are keyed by VillainDef.index: every
        // index must be in range and unique, else catches would be silently
        // unrecordable (or, via savegame load, write out of bounds). The range
        // is the pack's own villain count -- there is no engine capacity.
        int nv = res->villains_count;
        bool *seen_idx = nv ? calloc((size_t)nv, sizeof *seen_idx) : NULL;
        for (int i = 0; i < nv; i++) {
            int vi = res->villains[i].index;
            if (!seen_idx || vi < 0 || vi >= nv || seen_idx[vi]) {
                fprintf(stdout, "resources: villain '%s' has invalid or "
                        "duplicate index %d (must be unique, 0..%d)\n",
                        res->villains[i].id, vi, nv - 1);
                free(seen_idx);
                cJSON_Delete(root);
                return false;
            }
            seen_idx[vi] = true;
        }
        free(seen_idx);
    }

    // Troops, spells, classes and artifacts are looked up by their `index` as
    // an array position (spell slots, artifact bits, saved ids), so each must
    // be its own position in the catalog.
    {
        struct { const char *what; int count; size_t stride; const void *base;
                 size_t idx_off, id_off; } cats[] = {
            { "troop",    res->troops_count,    sizeof(TroopDef),    res->troops,
              offsetof(TroopDef, index),    offsetof(TroopDef, id) },
            { "spell",    res->spells_count,    sizeof(SpellDef),    res->spells,
              offsetof(SpellDef, index),    offsetof(SpellDef, id) },
            { "class",    res->classes_count,   sizeof(ClassDef),    res->classes,
              offsetof(ClassDef, index),    offsetof(ClassDef, id) },
            { "artifact", res->artifacts_count, sizeof(ArtifactDef), res->artifacts,
              offsetof(ArtifactDef, index), offsetof(ArtifactDef, id) },
        };
        for (size_t c = 0; c < sizeof cats / sizeof cats[0]; c++) {
            for (int i = 0; i < cats[c].count; i++) {
                const char *row = (const char *)cats[c].base + (size_t)i * cats[c].stride;
                int idx = *(const int *)(row + cats[c].idx_off);
                if (idx != i) {
                    fprintf(stdout, "resources: %s '%s' has index %d; it must be "
                            "its position in the catalog, %d\n",
                            cats[c].what, row + cats[c].id_off, idx, i);
                    cJSON_Delete(root);
                    return false;
                }
            }
        }
    }

    // Temp-death knob validation (fail-loud like the other pack contracts):
    // a configured troop must exist and the count must be positive.
    bool temp_death_troop_ok = !res->tuning.temp_death_troop[0];
    for (int i = 0; !temp_death_troop_ok && i < res->troops_count; i++)
        if (strcmp(res->troops[i].id, res->tuning.temp_death_troop) == 0)
            temp_death_troop_ok = true;
    if (res->tuning.temp_death_count <= 0 || !temp_death_troop_ok) {
        fprintf(stdout, "resources: invalid tuning.temp_death "
                "(troop '%s', count %d)\n", res->tuning.temp_death_troop,
                res->tuning.temp_death_count);
        cJSON_Delete(root);
        return false;
    }

    // Default temp-death army: the catalog's cheapest-recruit_cost troop
    // (first in catalog order on ties). Resolved here, after parse_troops.
    if (!res->tuning.temp_death_troop[0]) {
        int best = -1, best_cost = 0;
        for (int i = 0; i < res->troops_count; i++) {
            if (!res->troops[i].id[0]) continue;
            if (best < 0 || res->troops[i].recruit_cost < best_cost) {
                best = i;
                best_cost = res->troops[i].recruit_cost;
            }
        }
        if (best >= 0)
            res_copy_str(res->tuning.temp_death_troop,
                     sizeof(res->tuning.temp_death_troop),
                     res->troops[best].id);
    }
    parse_sprites(res,     cJSON_GetObjectItem(root, "sprites"));
    parse_audio(res,       cJSON_GetObjectItem(root, "audio"));
    parse_combat(res,      cJSON_GetObjectItem(root, "combat"));
    parse_controls(res,    cJSON_GetObjectItem(root, "controls"));
    // Strings live in their own per-locale file (strings/<lang>.json), not in
    // game.json -- so a pack can ship multiple languages and the engine stays
    // string-free. Pick the requested locale (CLI --lang) or the pack's base.
    {
        const char *base = res->world.language[0] ? res->world.language : "en";
        const char *lang = g_locale_override[0] ? g_locale_override : base;
        cJSON *strings_root = load_locale_strings(lang, base);
        if (!strings_root) res->strings_missing++;   // forces the hard-fail below
        res_parse_strings(res, strings_root);            // NULL-safe: records misses
        // The Introduction needs the villains, portraits and strings above.
        res_parse_intro(res, cJSON_GetObjectItem(root, "intro"), strings_root);
        cJSON_Delete(strings_root);
    }
    parse_ending(res,      cJSON_GetObjectItem(root, "ending"));
    parse_credits(res,     cJSON_GetObjectItem(root, "credits"));

    cJSON_Delete(root);

    // Strict strings: the engine carries no fallback text. If the pack omitted
    // any required string key, refuse to load -- the missing keys were printed
    // above -- rather than render blank/garbage.
    if (res->strings_missing > 0) {
        fprintf(stdout,
                "resources: pack is missing %d required string key(s); "
                "refusing to load. Every UI/message string must be supplied by "
                "the pack.\n", res->strings_missing);
        return false;
    }

    if (res->intro_errors > 0) {
        fprintf(stdout, "resources: the pack's intro script has %d error(s); "
                "refusing to load.\n", res->intro_errors);
        return false;
    }

    g_resources = res;    // publish to table lookups
    g_resources_generation++;
    return true;
}

void resources_free(Resources *res) {
    if (g_resources == res) { g_resources = NULL; g_resources_generation++; }
    // Heap-owned tables, each sized from the pack.
    if (res) {
        res_intro_free(&res->intro);
        free(res->object_arts);   res->object_arts = NULL;   res->object_art_count = 0;
        for (int i = 0; res->portraits && i < res->portrait_count; i++) free(res->portraits[i].anim);
        free(res->portraits);
        res->portraits = NULL;
        res->portrait_count = 0;
        for (int i = 0; res->troops && i < res->troops_count; i++) free(res->troops[i].anim);
        free(res->troops);        res->troops = NULL;        res->troops_count = 0;
        free(res->troop_alias_from); res->troop_alias_from = NULL;
        free(res->troop_alias_to);   res->troop_alias_to = NULL;
        res->troop_alias_count = 0;
        free(res->spells);        res->spells = NULL;        res->spells_count = 0;
        for (int i = 0; res->classes && i < res->classes_count; i++) {
            free(res->classes[i].starting_troops);
            free(res->classes[i].starting_counts);
        }
        for (int i = 0; res->class_hero && i < res->classes_count; i++)
            for (int f = 0; f < OB_FACE_COUNT; f++) {
                free(res->class_hero[i].walk.frames[f]);
                free(res->class_hero[i].idle.frames[f]);
                free(res->class_hero[i].boat.frames[f]);
            }
        free(res->class_hero);    res->class_hero = NULL;
        free(res->classes);       res->classes = NULL;       res->classes_count = 0;
        for (int i = 0; res->villains && i < res->villains_count; i++) free(res->villains[i].anim);
        free(res->villains);      res->villains = NULL;      res->villains_count = 0;
        free(res->artifacts);     res->artifacts = NULL;     res->artifacts_count = 0;
        free(res->villain_descs); res->villain_descs = NULL; res->villain_desc_count = 0;
        free(res->spell_lore);    res->spell_lore = NULL;    res->spell_lore_count = 0;
        free(res->spell_brief);   res->spell_brief = NULL;   res->spell_brief_count = 0;
        free(res->town_docks);    res->town_docks = NULL;    res->town_dock_count = 0;
        free(res->town_invites);  res->town_invites = NULL;  res->town_invite_count = 0;
        free(res->towns);         res->towns = NULL;         res->town_count = 0;
        free(res->castles);       res->castles = NULL;       res->castle_count = 0;
        for (int i = 0; res->zones && i < res->zone_count; i++) {
            ResZone *z = &res->zones[i];
            for (int c = 0; z->castles && c < z->castle_count; c++) free(z->castles[c].decorations);
            free(z->neighbors);
            free(z->signs);
            free(z->town_idx);
            free(z->castles);
            free(z->chests);
            free(z->artifacts);
            free(z->dwellings);
            free(z->armies);
            free(z->tile_set_arts);
            free(z->arrivals);
            for (int k = 0; k < z->event_count; k++) {
                free(z->events[k].reqs);
                free(z->events[k].effects);
            }
            free(z->events);
            free(z->salt.preferred_troops);
        }
        free(res->zones);         res->zones = NULL;         res->zone_count = 0;
        free(res->event_scenes);  res->event_scenes = NULL;  res->event_scene_count = 0;
        free(res->ui.count_buckets_army_view);    res->ui.count_buckets_army_view = NULL;
        free(res->ui.count_buckets_instant_army); res->ui.count_buckets_instant_army = NULL;
        free(res->ui.keybinds);                   res->ui.keybinds = NULL;
        for (int i = 0; i < RES_TILE_CODE_COUNT; i++) { free(res->tile_codes[i].variants); res->tile_codes[i].variants = NULL; }
        free(res->number_name_thresholds);        res->number_name_thresholds = NULL;
        free(res->number_name_labels);            res->number_name_labels = NULL;
        for (int i = 0; res->credits.groups && i < res->credits.group_count; i++)
            free(res->credits.groups[i].names);
        free(res->credits.groups);                res->credits.groups = NULL;
        free(res->credits.copyright);             res->credits.copyright = NULL;
        for (int f = 0; f < OB_FACE_COUNT; f++) {
            free(res->sprites.hero_walk.frames[f]); res->sprites.hero_walk.frames[f] = NULL;
            free(res->sprites.hero_idle.frames[f]); res->sprites.hero_idle.frames[f] = NULL;
            free(res->sprites.hero_boat.frames[f]); res->sprites.hero_boat.frames[f] = NULL;
        }
        free(res->sprites.hud_siege_animation);     res->sprites.hud_siege_animation = NULL;
        free(res->sprites.hud_magic_animation);     res->sprites.hud_magic_animation = NULL;
        free(res->sprites.alcove_figure_animation); res->sprites.alcove_figure_animation = NULL;
        free(res->sprites.view_icons_extra);        res->sprites.view_icons_extra = NULL;
        free(res->sprites.class_picker_selected);   res->sprites.class_picker_selected = NULL;
        for (int k = 0; k < RES_SPAWN_TIERS; k++) {
            free(res->spawn.chance_curve[k]); res->spawn.chance_curve[k] = NULL;
            free(res->spawn.troop_pool[k]);   res->spawn.troop_pool[k] = NULL;
            for (int t = 0; t < RES_SPAWN_TIERS; t++) {
                free(res->spawn.kind_curve[k][t]); res->spawn.kind_curve[k][t] = NULL;
            }
        }
    }
}

void resources_republish(const Resources *res) {
    g_resources = res;
    g_resources_generation++;
}

// ---- Lookups ---------------------------------------------------------------

const ResTown *resources_town_by_id(const Resources *r, const char *id) {
    if (!r || !id) return NULL;
    for (int i = 0; i < r->town_count; i++) {
        if (strcmp(r->towns[i].id, id) == 0) return &r->towns[i];
    }
    return NULL;
}

const ResTown *resources_zone_town(const Resources *r, const ResZone *z, int n) {
    if (!r || !z || n < 0 || n >= z->town_count) return NULL;
    int idx = z->town_idx[n];
    if (idx < 0 || idx >= r->town_count) return NULL;
    return &r->towns[idx];
}

const ResCastle *resources_castle_by_id(const Resources *r, const char *id) {
    if (!r || !id) return NULL;
    for (int i = 0; i < r->castle_count; i++) {
        if (strcmp(r->castles[i].id, id) == 0) return &r->castles[i];
    }
    return NULL;
}

bool resources_parse_castle_footprint(const char *s, ResCastleFootprint *out) {
    if (out) *out = RES_CASTLE_FOOTPRINT_3X2;
    if (!s || !s[0] || strcmp(s, "3x2") == 0) return true;
    if (strcmp(s, "1x1") == 0) {
        if (out) *out = RES_CASTLE_FOOTPRINT_1X1;
        return true;
    }
    return false;
}

bool resources_castle_is_home(const ResCastle *rc) {
    return rc && strcmp(rc->special.flow, "audience") == 0;
}

const ResCastle *resources_home_castle(const Resources *r) {
    if (!r) return NULL;
    for (int i = 0; i < r->castle_count; i++)
        if (resources_castle_is_home(&r->castles[i])) return &r->castles[i];
    return NULL;
}

const ResZone *resources_zone_by_id(const Resources *r, const char *id) {
    if (!r || !id) return NULL;
    for (int i = 0; i < r->zone_count; i++) {
        if (strcmp(r->zones[i].id, id) == 0) return &r->zones[i];
    }
    return NULL;
}

int resources_zone_index(const Resources *r, const char *id) {
    if (!r || !id) return -1;
    for (int i = 0; i < r->zone_count; i++)
        if (strcmp(r->zones[i].id, id) == 0) return i;
    return -1;
}

const ResVillainDesc *resources_villain_desc(const Resources *r,
                                             const char *villain_id) {
    if (!r || !villain_id) return NULL;
    for (int i = 0; i < r->villain_desc_count; i++) {
        if (strcmp(r->villain_descs[i].id, villain_id) == 0)
            return &r->villain_descs[i];
    }
    return NULL;
}

const ResTownInvite *resources_town_invite(const Resources *r, const char *id) {
    if (!r || !id || !id[0]) return NULL;
    for (int i = 0; i < r->town_invite_count; i++)
        if (strcmp(r->town_invites[i].id, id) == 0) return &r->town_invites[i];
    return NULL;
}

const char *resources_town_dock(const Resources *r, const char *town_id) {
    if (!r || !town_id) return NULL;
    for (int i = 0; i < r->town_dock_count; i++)
        if (strcmp(r->town_docks[i].id, town_id) == 0) return r->town_docks[i].text;
    return NULL;
}

int resources_portrait_index(const Resources *r, const char *id) {
    if (!r || !id || !id[0]) return -1;
    for (int i = 0; i < r->portrait_count; i++)
        if (strcmp(r->portraits[i].id, id) == 0) return i;
    return -1;
}

int resources_spawn_slot(const ResSpawn *sp, int kind, int tier, int chance) {
    if (!sp) return 0;
    kind &= 3;
    tier &= 3;
    // The walk's last slot: a pool of five or fewer ends where it always did
    // (slot 4, even when fewer were declared); a longer one at its own last.
    int n = sp->pool_count[kind] > 5 ? sp->pool_count[kind] : 5;
    const int *curve = sp->kind_curve_set[kind] ? sp->kind_curve[kind][tier] : sp->chance_curve[tier];
    int len = sp->kind_curve_set[kind] ? sp->kind_curve_len[kind][tier] : sp->chance_curve_len[tier];
    int slot = 0;
    while (slot < n - 1 && chance > (slot < len && curve ? curve[slot] : 0)) slot++;
    return slot;
}

const char *resources_spawn_troop(const ResSpawn *sp, int kind, int tier, int chance) {
    if (!sp) return "";
    int slot = resources_spawn_slot(sp, kind, tier, chance);
    kind &= 3;
    return (slot < sp->pool_count[kind] && sp->troop_pool[kind]) ? sp->troop_pool[kind][slot] : "";
}

const char *resources_spawn_troop_at(const ResSpawn *sp, int kind, int slot) {
    if (!sp || slot < 0) return "";
    kind &= 3;
    return (slot < sp->pool_count[kind] && sp->troop_pool[kind]) ? sp->troop_pool[kind][slot] : "";
}

const char *resources_spell_lore(const Resources *r, const char *spell_id) {
    if (!r || !spell_id) return NULL;
    for (int i = 0; i < r->spell_lore_count; i++)
        if (strcmp(r->spell_lore[i].id, spell_id) == 0) return r->spell_lore[i].text;
    return NULL;
}

const char *resources_spell_brief(const Resources *r, const char *spell_id) {
    if (!r || !spell_id) return NULL;
    for (int i = 0; i < r->spell_brief_count; i++)
        if (strcmp(r->spell_brief[i].id, spell_id) == 0) return r->spell_brief[i].text;
    return NULL;
}

const char *resources_count_bucket_label(const ResCountBucket *buckets,
                                         int n, int count,
                                         const char *fallback) {
    if (!buckets || n <= 0) return fallback ? fallback : "";
    for (int i = 0; i < n; i++) {
        if (count <= buckets[i].threshold) return buckets[i].label;
    }
    return buckets[n - 1].label;
}

void resources_format_template(char *out, int out_sz, const char *src,
                               const ResTemplateVar *vars, int nvars) {
    if (!out || out_sz <= 0) return;
    out[0] = '\0';
    if (!src) return;
    int o = 0;
    while (*src && o + 1 < out_sz) {
        if (*src == '%') {
            // Find the closing '%' on the same token.
            const char *end = strchr(src + 1, '%');
            if (end && end > src + 1) {
                size_t klen = (size_t)(end - (src + 1));
                const char *replacement = NULL;
                for (int i = 0; i < nvars; i++) {
                    if (!vars[i].key) continue;
                    if (strncmp(src + 1, vars[i].key, klen) == 0
                        && vars[i].key[klen] == '\0') {
                        replacement = vars[i].value ? vars[i].value : "";
                        break;
                    }
                }
                if (replacement) {
                    int n = snprintf(out + o, out_sz - o, "%s", replacement);
                    if (n < 0) break;
                    if (n >= out_sz - o) { o = out_sz - 1; break; }
                    o += n;
                    src = end + 1;
                    continue;
                }
            }
            // No matching token -- emit the '%' verbatim and continue.
            out[o++] = *src++;
            continue;
        }
        out[o++] = *src++;
    }
    out[o] = '\0';
}
