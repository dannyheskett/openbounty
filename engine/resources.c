#include "resources.h"
#include "map.h"
#include "cJSON.h"
#include "assets_bytes.h"   // LoadAssetBytes / UnloadAssetBytes
#include "pack.h"
#include "tile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Small helpers ---------------------------------------------------------

static void copy_str(char *dst, size_t dst_sz, const char *src) {
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

static int json_int(const cJSON *obj, const char *key, int fallback) {
    if (!obj) return fallback;
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(v)) return v->valueint;
    return fallback;
}

static const char *json_str(const cJSON *obj, const char *key,
                            const char *fallback) {
    if (!obj) return fallback;
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v) && v->valuestring) return v->valuestring;
    return fallback;
}

// Defined further down alongside the sprite-manifest parsers; declared here
// because the catalog parsers (troops, villains) reach it first.
static void parse_path_array(cJSON *arr, char *dst, size_t stride,
                             int cap, int *out_count);

// Read a fixed-length int array under `key`. Missing keys / wrong types leave
// `out[]` untouched, so callers prime it with defaults before calling.
static void json_int_array(const cJSON *obj, const char *key,
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
static unsigned int parse_color_hex(const char *s, unsigned int fallback) {
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
static void json_color(const cJSON *obj, const char *key, unsigned int *out) {
    if (!obj || !out) return;
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring) return;
    *out = parse_color_hex(v->valuestring, *out);
}

// Singleton pointer so tables.h lookup functions (troop_by_id, spell_by_id,
// ...) can read catalog data without threading a Resources* through every
// call site. Set by resources_load(); cleared by resources_free().
static const Resources *g_resources = NULL;

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
static char *slurp(const char *path) {
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

// ---- In-place section parsers ---------------------------------------------
// Each parses a JSON array nested inside the single game.json root object.

static void parse_anim_set(cJSON *obj, ResAnimSet *out);

static void parse_towns(Resources *res, cJSON *arr) {
    res->town_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->town_count >= RES_MAX_TOWNS) break;
        ResTown *t = &res->towns[res->town_count++];
        t->index = json_int(it, "index", -1);
        copy_str(t->id,   sizeof(t->id),   json_str(it, "id", ""));
        copy_str(t->name, sizeof(t->name), json_str(it, "name", ""));
        copy_str(t->zone, sizeof(t->zone), json_str(it, "zone", ""));
        t->x = json_int(it, "x", -1);
        t->y = json_int(it, "y", -1);

        cJSON *boat = cJSON_GetObjectItem(it, "boat");
        t->boat_x = json_int(boat, "x", -1);
        t->boat_y = json_int(boat, "y", -1);

        cJSON *gate = cJSON_GetObjectItem(it, "gate");
        t->gate_x = json_int(gate, "x", -1);
        t->gate_y = json_int(gate, "y", -1);

        copy_str(t->intel_castle, sizeof(t->intel_castle),
                 json_str(it, "intel_castle", ""));
        copy_str(t->pinned_spell, sizeof(t->pinned_spell),
                 json_str(it, "pinned_spell", ""));
        // Optional per-town tile art (a bare stem under art/tiles/). Absent
        // means the shared "town" tile, so older packs stamp as before.
        copy_str(t->art, sizeof(t->art), json_str(it, "art", ""));
    }
}

static void parse_castles(Resources *res, cJSON *arr) {
    res->castle_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->castle_count >= RES_MAX_CASTLES) break;
        ResCastle *c = &res->castles[res->castle_count++];
        c->index = json_int(it, "index", -1);
        copy_str(c->id,   sizeof(c->id),   json_str(it, "id", ""));
        copy_str(c->name, sizeof(c->name), json_str(it, "name", ""));
        copy_str(c->zone, sizeof(c->zone), json_str(it, "zone", ""));
        c->x = json_int(it, "x", -1);
        c->y = json_int(it, "y", -1);
        // Default castle gate: tile directly south of the castle (matches
        //  convention used for Town Gate landings).
        c->gate_x = (c->x >= 0) ? c->x : -1;
        c->gate_y = (c->y >= 0) ? c->y + 1 : -1;
        cJSON *gate = cJSON_GetObjectItem(it, "gate");
        if (cJSON_IsObject(gate)) {
            c->gate_x = json_int(gate, "x", c->gate_x);
            c->gate_y = json_int(gate, "y", c->gate_y);
        }
        c->difficulty_tier = json_int(it, "difficulty_tier", 0);
        // Map footprint (REQ-228): absent means the classic 3x2 stamp.
        {
            const char *fp = json_str(it, "footprint", "");
            if (!resources_parse_castle_footprint(fp, &c->footprint) && fp[0])
                fprintf(stdout, "resources: castle '%s' footprint '%s' unknown, "
                        "using 3x2\n", c->id, fp);
        }

        memset(&c->special, 0, sizeof(c->special));
        cJSON *sp = cJSON_GetObjectItem(it, "special");
        if (cJSON_IsObject(sp)) {
            copy_str(c->special.flow, sizeof(c->special.flow),
                     json_str(sp, "flow", ""));
            cJSON *es = cJSON_GetObjectItem(sp, "excluded_from_siege");
            c->special.excluded_from_siege = cJSON_IsTrue(es);
            cJSON *ei = cJSON_GetObjectItem(sp, "excluded_from_intel");
            c->special.excluded_from_intel = cJSON_IsTrue(ei);
            cJSON *ec = cJSON_GetObjectItem(sp, "excluded_from_contract");
            c->special.excluded_from_contract = cJSON_IsTrue(ec);
            copy_str(c->special.win_condition,
                     sizeof(c->special.win_condition),
                     json_str(sp, "win_condition", ""));
            cJSON *dlg = cJSON_GetObjectItem(sp, "dialog");
            if (cJSON_IsObject(dlg)) {
                copy_str(c->special.dialog_header,
                         sizeof(c->special.dialog_header),
                         json_str(dlg, "header", ""));
                copy_str(c->special.dialog_body,
                         sizeof(c->special.dialog_body),
                         json_str(dlg, "body", ""));
            }
            cJSON *au = cJSON_GetObjectItem(sp, "audience");
            if (cJSON_IsObject(au)) {
                copy_str(c->special.audience_intro,
                         sizeof(c->special.audience_intro),
                         json_str(au, "intro", ""));
                copy_str(c->special.audience_rank_up,
                         sizeof(c->special.audience_rank_up),
                         json_str(au, "rank_up", ""));
                copy_str(c->special.audience_more_needed,
                         sizeof(c->special.audience_more_needed),
                         json_str(au, "more_needed", ""));
                copy_str(c->special.audience_final_rank,
                         sizeof(c->special.audience_final_rank),
                         json_str(au, "final_rank", ""));
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
    return TERRAIN_GRASS;
}

static void parse_tile_codes(Resources *res, cJSON *obj) {
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++) res->tile_codes[i].present = false;
    if (!cJSON_IsObject(obj)) return;
    cJSON *entry;
    cJSON_ArrayForEach(entry, obj) {
        const char *key = entry->string;
        if (!key || !key[0] || (unsigned char)key[0] >= RES_TILE_CODE_COUNT) continue;
        int idx = (unsigned char)key[0];
        ResTileCode *tc = &res->tile_codes[idx];
        tc->present = true;
        copy_str(tc->art, sizeof(tc->art), json_str(entry, "art", ""));
        tc->terrain = (int)terrain_from_name(json_str(entry, "terrain", "grass"));
        cJSON *jbf = cJSON_GetObjectItem(entry, "blocks_foot");
        cJSON *jib = cJSON_GetObjectItem(entry, "is_bridge");
        tc->blocks_foot = cJSON_IsBool(jbf) && cJSON_IsTrue(jbf);
        tc->is_bridge   = cJSON_IsBool(jib) && cJSON_IsTrue(jib);
    }
}

static void parse_zone_objects_array(cJSON *arr, int cap, int *count,
                                     void *dst, size_t stride,
                                     void (*fill)(cJSON *, void *)) {
    *count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (*count >= cap) break;
        void *slot = (char *)dst + (*count) * stride;
        fill(it, slot);
        (*count)++;
    }
}

static void fill_sign(cJSON *j, void *dst) {
    ResSign *s = (ResSign *)dst;
    s->x = json_int(j, "x", 0); s->y = json_int(j, "y", 0);
    copy_str(s->id,    sizeof(s->id),    json_str(j, "id", ""));
    copy_str(s->title, sizeof(s->title), json_str(j, "title", ""));
    copy_str(s->body,  sizeof(s->body),  json_str(j, "body", ""));
}
// Zone towns are resolved to indices into the authoritative res->towns[]
// catalog in parse_zones; see resolve_zone_town_idx.
static void fill_zone_castle(cJSON *j, void *dst) {
    ResZoneCastle *c = (ResZoneCastle *)dst;
    c->x = json_int(j, "x", 0); c->y = json_int(j, "y", 0);
    copy_str(c->id, sizeof(c->id), json_str(j, "id", ""));
    // Optional `decorations` array -- extra wall pieces around the gate.
    // Each entry: { "dx": int, "dy": int, "art": string }.
    c->decor_count = 0;
    cJSON *decor = cJSON_GetObjectItem(j, "decorations");
    if (cJSON_IsArray(decor)) {
        cJSON *it;
        cJSON_ArrayForEach(it, decor) {
            if (c->decor_count >= RES_MAX_CASTLE_DECOR) break;
            ResCastleDecor *d = &c->decorations[c->decor_count++];
            d->dx = json_int(it, "dx", 0);
            d->dy = json_int(it, "dy", 0);
            copy_str(d->art, sizeof(d->art), json_str(it, "art", ""));
        }
    }
}
static void fill_zone_chest(cJSON *j, void *dst) {
    ResZoneChest *c = (ResZoneChest *)dst;
    c->x = json_int(j, "x", 0); c->y = json_int(j, "y", 0);
    copy_str(c->id, sizeof(c->id), json_str(j, "id", ""));
}
static void fill_zone_artifact(cJSON *j, void *dst) {
    ResZoneArtifact *a = (ResZoneArtifact *)dst;
    a->x = json_int(j, "x", 0); a->y = json_int(j, "y", 0);
    copy_str(a->id, sizeof(a->id), json_str(j, "id", ""));
}
static void fill_zone_dwelling(cJSON *j, void *dst) {
    ResZoneDwelling *d = (ResZoneDwelling *)dst;
    d->x = json_int(j, "x", 0); d->y = json_int(j, "y", 0);
    copy_str(d->id,   sizeof(d->id),   json_str(j, "id", ""));
    copy_str(d->kind, sizeof(d->kind), json_str(j, "kind", ""));
}
static void fill_zone_army(cJSON *j, void *dst) {
    ResZoneArmy *a = (ResZoneArmy *)dst;
    a->x = json_int(j, "x", 0); a->y = json_int(j, "y", 0);
    copy_str(a->id, sizeof(a->id), json_str(j, "id", ""));
    cJSON *st = cJSON_GetObjectItem(j, "static");
    a->is_static = cJSON_IsBool(st) && cJSON_IsTrue(st);
    // Optional explicit garrison: "army":[{"troop":id,"count":n}, ...].
    a->army_stacks = 0;
    cJSON *army = cJSON_GetObjectItem(j, "army");
    if (cJSON_IsArray(army)) {
        cJSON *it;
        cJSON_ArrayForEach(it, army) {
            if (a->army_stacks >= 5) break;
            copy_str(a->army_id[a->army_stacks], sizeof(a->army_id[0]),
                     json_str(it, "troop", ""));
            a->army_count[a->army_stacks] = json_int(it, "count", 0);
            a->army_stacks++;
        }
    }
}

static void parse_zones(Resources *res, cJSON *arr) {
    res->zone_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->zone_count >= RES_MAX_ZONES) break;
        ResZone *z = &res->zones[res->zone_count++];
        memset(z, 0, sizeof(*z));
        copy_str(z->id,       sizeof(z->id),       json_str(it, "id", ""));
        copy_str(z->name,     sizeof(z->name),     json_str(it, "name", ""));
        {
            char rel[RES_PATH_LEN];
            copy_str(rel, sizeof rel, json_str(it, "map", ""));
            // Back-compat: strip the legacy hardcoded prefix if present.
            // New manifests just write "maps/foo.dat".
            const char *legacy = "assets/kings-bounty/";
            size_t llen = strlen(legacy);
            const char *p = (strncmp(rel, legacy, llen) == 0) ? rel + llen : rel;
            resources_resolve_path(res, p, z->map_path, sizeof z->map_path);
        }
        copy_str(z->tile_set, sizeof(z->tile_set), json_str(it, "tile_set", ""));
        copy_str(z->army_art, sizeof(z->army_art), json_str(it, "army_art", ""));
        z->width  = json_int(it, "width",  64);
        z->height = json_int(it, "height", 64);
        cJSON *hs = cJSON_GetObjectItem(it, "hero_spawn");
        z->hero_spawn_x = json_int(hs, "x", 0);
        z->hero_spawn_y = json_int(hs, "y", 0);

        cJSON *nbr = cJSON_GetObjectItem(it, "neighbors");
        if (cJSON_IsArray(nbr)) {
            cJSON *n;
            cJSON_ArrayForEach(n, nbr) {
                if (z->neighbor_count >= RES_MAX_NEIGHBORS) break;
                if (cJSON_IsString(n)) {
                    copy_str(z->neighbors[z->neighbor_count],
                             sizeof(z->neighbors[0]), n->valuestring);
                    z->neighbor_count++;
                }
            }
        }

        parse_zone_objects_array(cJSON_GetObjectItem(it, "signs"),
                                 RES_MAX_ZONE_OBJECTS, &z->sign_count,
                                 z->signs, sizeof(ResSign), fill_sign);
        // Towns: resolve each zone town's id to an INDEX into the authoritative
        // res->towns[] catalog (already parsed -- parse_towns runs before
        // parse_zones). Preserves the zone JSON town ORDER (required for planner
        // determinism). A zone town id missing from the catalog is skipped + logged.
        {
            cJSON *jtowns = cJSON_GetObjectItem(it, "towns");
            z->town_count = 0;
            if (cJSON_IsArray(jtowns)) {
                cJSON *jt;
                cJSON_ArrayForEach(jt, jtowns) {
                    if (z->town_count >= RES_MAX_TOWNS) break;
                    const char *tid = json_str(jt, "id", "");
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
                                 RES_MAX_ZONE_OBJECTS, &z->castle_count,
                                 z->castles, sizeof(ResZoneCastle), fill_zone_castle);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "chests"),
                                 RES_MAX_ZONE_OBJECTS, &z->chest_count,
                                 z->chests, sizeof(ResZoneChest), fill_zone_chest);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "artifacts"),
                                 RES_MAX_ZONE_OBJECTS, &z->artifact_count,
                                 z->artifacts, sizeof(ResZoneArtifact), fill_zone_artifact);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "dwellings"),
                                 RES_MAX_ZONE_OBJECTS, &z->dwelling_count,
                                 z->dwellings, sizeof(ResZoneDwelling), fill_zone_dwelling);
        parse_zone_objects_array(cJSON_GetObjectItem(it, "wandering_armies"),
                                 RES_MAX_ZONE_OBJECTS, &z->army_count,
                                 z->armies, sizeof(ResZoneArmy), fill_zone_army);

        cJSON *salt = cJSON_GetObjectItem(it, "salt");
        z->salt.artifacts     = json_int(salt, "artifacts",     0);
        z->salt.navmaps       = json_int(salt, "navmaps",       0);
        z->salt.orbs          = json_int(salt, "orbs",          0);
        z->salt.telecaves     = json_int(salt, "telecaves",     0);
        z->salt.dwellings     = json_int(salt, "dwellings",     0);
        z->salt.friendly_foes = json_int(salt, "friendly_foes", 0);
        z->salt.preferred_troop_count = 0;
        z->salt.dwelling_range_min = -1;
        z->salt.dwelling_range_max = -1;
        if (cJSON_IsObject(salt)) {
            cJSON *pref = cJSON_GetObjectItem(salt, "preferred_troops");
            if (cJSON_IsArray(pref)) {
                int n = cJSON_GetArraySize(pref);
                if (n > 16) n = 16;
                for (int i = 0; i < n; i++) {
                    cJSON *e = cJSON_GetArrayItem(pref, i);
                    if (cJSON_IsString(e)) {
                        copy_str(z->salt.preferred_troops[z->salt.preferred_troop_count],
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
        z->home_spawn_x = json_int(hsp, "x", -1);
        z->home_spawn_y = json_int(hsp, "y", -1);

        cJSON *ma = cJSON_GetObjectItem(it, "magic_alcove");
        z->magic_alcove_x = json_int(ma, "x", -1);
        z->magic_alcove_y = json_int(ma, "y", -1);
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

static void parse_troops(Resources *res, cJSON *arr) {
    res->troops_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->troops_count >= CAT_TROOPS_MAX) break;
        TroopDef *t = &res->troops[res->troops_count++];
        memset(t, 0, sizeof(*t));
        t->index = json_int(it, "index", -1);
        copy_str(t->id,       sizeof(t->id),       json_str(it, "id", ""));
        copy_str(t->name,     sizeof(t->name),     json_str(it, "name", ""));
        copy_str(t->sprite,   sizeof(t->sprite),   json_str(it, "sprite", ""));
        // Via parse_string_array so a non-string entry is skipped rather than
        // burning a slot: the hand-rolled loop this replaces advanced its
        // index outside the type check, leaving an empty frame mid-cycle.
        parse_path_array(cJSON_GetObjectItem(it, "anim"), t->anim[0],
                         CAT_PATH_LEN, OB_ANIM_FRAMES_MAX, &t->anim_count);
        copy_str(t->dwelling, sizeof(t->dwelling), json_str(it, "dwelling", ""));
        t->skill_level     = json_int(it, "skill_level", 0);
        t->hit_points      = json_int(it, "hit_points", 0);
        t->move_rate       = json_int(it, "move_rate", 0);
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
        t->recruit_cost    = json_int(it, "recruit_cost", 0);
        t->spoils_factor   = json_int(it, "spoils_factor", 0);
        t->abilities       = parse_troop_abilities(json_str(it, "abilities", ""));
        t->max_population  = json_int(it, "max_population", 0);
        t->growth_per_week = json_int(it, "growth_per_week", 0);
        const char *grp = json_str(it, "morale_group", "A");
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
}

static SpellKind spell_kind_from_name(const char *s) {
    if (s && strcmp(s, "adventure") == 0) return SPELL_KIND_ADVENTURE;
    return SPELL_KIND_COMBAT;
}

static void parse_spells(Resources *res, cJSON *arr) {
    res->spells_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->spells_count >= CAT_SPELLS_MAX) break;
        SpellDef *s = &res->spells[res->spells_count++];
        memset(s, 0, sizeof(*s));
        s->index = json_int(it, "index", -1);
        copy_str(s->id,          sizeof(s->id),          json_str(it, "id", ""));
        copy_str(s->name,        sizeof(s->name),        json_str(it, "name", ""));
        copy_str(s->description, sizeof(s->description), json_str(it, "description", ""));
        s->kind = spell_kind_from_name(json_str(it, "kind", "combat"));
        s->cost = json_int(it, "cost", 0);
    }
}

static void parse_classes(Resources *res, cJSON *arr) {
    res->classes_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->classes_count >= CAT_CLASSES_MAX) break;
        ClassDef *c = &res->classes[res->classes_count++];
        memset(c, 0, sizeof(*c));
        c->index = json_int(it, "index", -1);
        copy_str(c->id,       sizeof(c->id),       json_str(it, "id", ""));
        copy_str(c->name,     sizeof(c->name),     json_str(it, "name", ""));
        copy_str(c->portrait, sizeof(c->portrait), json_str(it, "portrait", ""));
        c->starting_gold = json_int(it, "starting_gold", 0);
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
                copy_str(h->tile, sizeof h->tile, json_str(hero, "tile", ""));
            }
        }

        cJSON *st = cJSON_GetObjectItem(it, "starting_troops");
        int si = 0;
        if (cJSON_IsArray(st)) {
            cJSON *e;
            cJSON_ArrayForEach(e, st) {
                if (si >= CLASS_MAX_STARTING_TROOPS) break;
                copy_str(c->starting_troops[si], sizeof(c->starting_troops[si]),
                         json_str(e, "id", ""));
                c->starting_counts[si] = json_int(e, "count", 0);
                si++;
            }
        }

        cJSON *rk = cJSON_GetObjectItem(it, "ranks");
        c->rank_count = 0;
        if (cJSON_IsArray(rk)) {
            cJSON *r;
            cJSON_ArrayForEach(r, rk) {
                if (c->rank_count >= CLASS_MAX_RANKS) break;
                RankDef *rd = &c->ranks[c->rank_count++];
                memset(rd, 0, sizeof(*rd));
                copy_str(rd->id,   sizeof(rd->id),   json_str(r, "id", ""));
                copy_str(rd->name, sizeof(rd->name), json_str(r, "name", ""));
                rd->villains_needed = json_int(r, "villains_needed", 0);
                rd->leadership      = json_int(r, "leadership", 0);
                rd->max_spells      = json_int(r, "max_spells", 0);
                rd->spell_power     = json_int(r, "spell_power", 0);
                rd->commission      = json_int(r, "commission", 0);
                cJSON *km = cJSON_GetObjectItem(r, "knows_magic");
                rd->knows_magic     = cJSON_IsBool(km) && cJSON_IsTrue(km);
                rd->instant_army    = json_int(r, "instant_army", 0);
            }
        }
    }
}

static void parse_villains(Resources *res, cJSON *arr) {
    res->villains_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->villains_count >= CAT_VILLAINS_MAX) break;
        VillainDef *v = &res->villains[res->villains_count++];
        memset(v, 0, sizeof(*v));
        v->index = json_int(it, "index", -1);
        copy_str(v->id,       sizeof(v->id),       json_str(it, "id", ""));
        copy_str(v->name,     sizeof(v->name),     json_str(it, "name", ""));
        copy_str(v->portrait, sizeof(v->portrait), json_str(it, "portrait", ""));
        // Optional, and declared exactly like a troop's. Absent (count 0)
        // means the shell derives `<portrait-stem>_NN` siblings instead.
        parse_path_array(cJSON_GetObjectItem(it, "anim"), v->anim[0],
                         CAT_PATH_LEN, OB_ANIM_FRAMES_MAX, &v->anim_count);
        copy_str(v->zone,     sizeof(v->zone),     json_str(it, "zone", ""));
        v->reward      = json_int(it, "reward", 0);
        v->puzzle_cell = json_int(it, "puzzle_cell", -1);

        cJSON *army = cJSON_GetObjectItem(it, "army");
        if (cJSON_IsArray(army)) {
            int i = 0;
            cJSON *slot;
            cJSON_ArrayForEach(slot, army) {
                if (i >= 5) break;
                if (cJSON_IsObject(slot)) {
                    copy_str(v->army_troops[i],
                             sizeof(v->army_troops[i]),
                             json_str(slot, "troop", ""));
                    v->army_counts[i] = json_int(slot, "count", 0);
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
    if (strcmp(s, "cheaper_boats")       == 0) return ARTIFACT_POWER_CHEAPER_BOATS;
    return ARTIFACT_POWER_UNKNOWN;
}

static void parse_artifacts(Resources *res, cJSON *arr) {
    res->artifacts_count = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (res->artifacts_count >= CAT_ARTIFACTS_MAX) break;
        ArtifactDef *a = &res->artifacts[res->artifacts_count++];
        memset(a, 0, sizeof(*a));
        a->index = json_int(it, "index", -1);
        copy_str(a->id,     sizeof(a->id),     json_str(it, "id", ""));
        copy_str(a->name,   sizeof(a->name),   json_str(it, "name", ""));
        copy_str(a->icon,   sizeof(a->icon),   json_str(it, "icon", ""));
        copy_str(a->effect, sizeof(a->effect), json_str(it, "effect", ""));
        copy_str(a->zone,   sizeof(a->zone),   json_str(it, "zone", ""));
        a->power       = artifact_power_from_name(json_str(it, "power", ""));
        a->puzzle_cell = json_int(it, "puzzle_cell", -1);
        a->local_idx   = json_int(it, "local_idx", 0);
    }
}

// ---- Sprite manifest -----------------------------------------------------

// Fill a fixed-size array of path buffers from a JSON array of strings,
// skipping any non-string entry. `stride` is the per-slot buffer size, which
// is what lets this serve both the RES_PATH_LEN sprite manifest arrays and
// the CAT_PATH_LEN catalog ones. *out_count receives the number of strings
// actually stored -- for an animation that count IS the cycle length.
static void parse_path_array(cJSON *arr, char *dst, size_t stride,
                             int cap, int *out_count) {
    if (out_count) *out_count = 0;
    if (!cJSON_IsArray(arr) || !dst) return;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= cap) break;
        if (!cJSON_IsString(it)) continue;
        copy_str(dst + (size_t)n * stride, stride, it->valuestring);
        n++;
    }
    if (out_count) *out_count = n;
}

static void parse_string_array(cJSON *arr, char dst[][RES_PATH_LEN],
                               int cap, int *out_count) {
    parse_path_array(arr, dst ? dst[0] : NULL, RES_PATH_LEN, cap, out_count);
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
        parse_path_array(obj, out->frames[OB_FACE_SOUTH][0], RES_PATH_LEN,
                         OB_ANIM_FRAMES_MAX, &out->count[OB_FACE_SOUTH]);
        return;
    }
    if (!cJSON_IsObject(obj)) return;
    out->directional = true;
    for (int f = 0; f < OB_FACE_COUNT; f++) {
        parse_path_array(cJSON_GetObjectItem(obj, KEYS[f]),
                         out->frames[f][0], RES_PATH_LEN,
                         OB_ANIM_FRAMES_MAX, &out->count[f]);
    }
}

static void parse_sprites(Resources *res, cJSON *obj) {
    // Art paths that used to be compiled into the shell. A pack may override
    // any of them in the sprites block; these keep packs that don't unchanged.
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
            copy_str(res->sprites.combat[i], RES_PATH_LEN, COMBAT_DEFAULT[i]);
        res->sprites.combat_count = RES_COMBAT_TILES;
    }
    copy_str(res->sprites.font, sizeof res->sprites.font,
             "art/font/kb-font.png");
    copy_str(res->sprites.palette, sizeof res->sprites.palette,
             "palettes/palette.bin");
    if (!cJSON_IsObject(obj)) return;

    cJSON *hero = cJSON_GetObjectItem(obj, "hero");
    if (cJSON_IsObject(hero)) {
        parse_anim_set(cJSON_GetObjectItem(hero, "walk"), &res->sprites.hero_walk);
        parse_anim_set(cJSON_GetObjectItem(hero, "idle"), &res->sprites.hero_idle);
        parse_anim_set(cJSON_GetObjectItem(hero, "boat"), &res->sprites.hero_boat);
    }

    // Combat tileset. The shell used to carry this list as a static array,
    // which meant the pack could not name its own battle art. Declared here
    // now; the defaults installed before parsing keep older packs working.
    // Guarded: parse_path_array zeroes the out-count for a missing key, which
    // would wipe the defaults installed above rather than leave them alone.
    cJSON *jcombat = cJSON_GetObjectItem(obj, "combat");
    if (cJSON_IsArray(jcombat))
        parse_string_array(jcombat, res->sprites.combat, RES_COMBAT_TILES,
                           &res->sprites.combat_count);

    // Font strip and palette binary, likewise compiled into the shell before.
    copy_str(res->sprites.font, sizeof res->sprites.font,
             json_str(obj, "font", res->sprites.font));
    copy_str(res->sprites.palette, sizeof res->sprites.palette,
             json_str(obj, "palette", res->sprites.palette));

    cJSON *ui = cJSON_GetObjectItem(obj, "ui");
    if (cJSON_IsObject(ui)) {
        copy_str(res->sprites.puzzle_cover, sizeof(res->sprites.puzzle_cover),
                 json_str(ui, "puzzle_cover", ""));
        // Location backdrops .
        copy_str(res->sprites.town_backdrop, sizeof(res->sprites.town_backdrop),
                 json_str(ui, "town_backdrop", ""));
        copy_str(res->sprites.castle_backdrop, sizeof(res->sprites.castle_backdrop),
                 json_str(ui, "castle_backdrop", ""));
        copy_str(res->sprites.plains_backdrop, sizeof(res->sprites.plains_backdrop),
                 json_str(ui, "plains_backdrop", ""));
        copy_str(res->sprites.forest_backdrop, sizeof(res->sprites.forest_backdrop),
                 json_str(ui, "forest_backdrop", ""));
        copy_str(res->sprites.hillcave_backdrop, sizeof(res->sprites.hillcave_backdrop),
                 json_str(ui, "hillcave_backdrop", ""));
        copy_str(res->sprites.dungeon_backdrop, sizeof(res->sprites.dungeon_backdrop),
                 json_str(ui, "dungeon_backdrop", ""));
        copy_str(res->sprites.ending_win, sizeof(res->sprites.ending_win),
                 json_str(ui, "ending_win", ""));
        copy_str(res->sprites.panel_frame, sizeof(res->sprites.panel_frame),
                 json_str(ui, "panel_frame", ""));
        copy_str(res->sprites.ending_lose, sizeof(res->sprites.ending_lose),
                 json_str(ui, "ending_lose", ""));
        copy_str(res->sprites.orb, sizeof(res->sprites.orb),
                 json_str(ui, "orb", ""));
        parse_string_array(cJSON_GetObjectItem(ui, "view_icons_extra"),
                           res->sprites.view_icons_extra,
                           RES_EXTRA_ICONS,
                           &res->sprites.view_icons_extra_count);
        copy_str(res->sprites.chrome_overworld,
                 sizeof(res->sprites.chrome_overworld),
                 json_str(ui, "chrome_overworld", ""));
        copy_str(res->sprites.splash_logo,
                 sizeof(res->sprites.splash_logo),
                 json_str(ui, "splash_logo", ""));
        copy_str(res->sprites.splash_title,
                 sizeof(res->sprites.splash_title),
                 json_str(ui, "splash_title", ""));
        copy_str(res->sprites.class_picker,
                 sizeof(res->sprites.class_picker),
                 json_str(ui, "class_picker", ""));
        copy_str(res->sprites.class_highlight,
                 sizeof(res->sprites.class_highlight),
                 json_str(ui, "class_highlight", ""));
    }

    cJSON *hud = cJSON_GetObjectItem(obj, "hud");
    if (cJSON_IsObject(hud)) {
        copy_str(res->sprites.hud_contract_silhouette,
                 sizeof(res->sprites.hud_contract_silhouette),
                 json_str(hud, "contract_silhouette", ""));
        copy_str(res->sprites.hud_siege_silhouette,
                 sizeof(res->sprites.hud_siege_silhouette),
                 json_str(hud, "siege_silhouette", ""));
        parse_string_array(cJSON_GetObjectItem(hud, "siege_animation"),
                           res->sprites.hud_siege_animation,
                           OB_ANIM_FRAMES_MAX,
                           &res->sprites.hud_siege_animation_count);
        copy_str(res->sprites.hud_magic_silhouette,
                 sizeof(res->sprites.hud_magic_silhouette),
                 json_str(hud, "magic_silhouette", ""));
        parse_string_array(cJSON_GetObjectItem(hud, "magic_animation"),
                           res->sprites.hud_magic_animation,
                           OB_ANIM_FRAMES_MAX,
                           &res->sprites.hud_magic_animation_count);
        copy_str(res->sprites.hud_puzzle_grid,
                 sizeof(res->sprites.hud_puzzle_grid),
                 json_str(hud, "puzzle_grid", ""));
        copy_str(res->sprites.hud_gold_purse,
                 sizeof(res->sprites.hud_gold_purse),
                 json_str(hud, "gold_purse", ""));
        copy_str(res->sprites.hud_bar_strip,
                 sizeof(res->sprites.hud_bar_strip),
                 json_str(hud, "bar_strip", ""));
    }
}

// ---- Audio (background music tracks) -------------------------------------

static void parse_audio(Resources *res, cJSON *obj) {
    if (!cJSON_IsObject(obj)) return;
    cJSON *tracks = cJSON_GetObjectItem(obj, "tracks");
    if (cJSON_IsObject(tracks)) {
        copy_str(res->audio.openworld_path, sizeof(res->audio.openworld_path),
                 json_str(tracks, "openworld", ""));
        copy_str(res->audio.combat_path, sizeof(res->audio.combat_path),
                 json_str(tracks, "combat", ""));
    }
    cJSON *tunes = cJSON_GetObjectItem(obj, "tunes");
    if (cJSON_IsObject(tunes)) {
        copy_str(res->audio.tune_walk,   sizeof(res->audio.tune_walk),
                 json_str(tunes, "walk", ""));
        copy_str(res->audio.tune_bump,   sizeof(res->audio.tune_bump),
                 json_str(tunes, "bump", ""));
        copy_str(res->audio.tune_chest,  sizeof(res->audio.tune_chest),
                 json_str(tunes, "chest", ""));
        copy_str(res->audio.tune_defeat, sizeof(res->audio.tune_defeat),
                 json_str(tunes, "defeat", ""));
    }
}

// ---- Strings  ------

static void parse_end_text(ResEndText *dst, cJSON *obj) {
    if (!cJSON_IsObject(obj)) return;
    copy_str(dst->header, sizeof(dst->header), json_str(obj, "header", ""));
    copy_str(dst->body,   sizeof(dst->body),   json_str(obj, "body", ""));
    copy_str(dst->footer, sizeof(dst->footer), json_str(obj, "footer", ""));
}

static void parse_combat(Resources *res, cJSON *obj) {
    // Defaults.
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++)
            res->morale_chart[i][j] = 'N';
    res->number_name_count = 0;
    if (!cJSON_IsObject(obj)) return;

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
    if (cJSON_IsArray(nn)) {
        int i = 0;
        cJSON *it;
        cJSON_ArrayForEach(it, nn) {
            if (i >= 8) break;
            if (!cJSON_IsObject(it)) continue;
            res->number_name_thresholds[i] = json_int(it, "min", 1);
            copy_str(res->number_name_labels[i],
                     sizeof(res->number_name_labels[i]),
                     json_str(it, "label", ""));
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
        if (n >= 8) break;
        if (!cJSON_IsObject(it)) continue;
        copy_str(res->controls.items[n].id,
                 sizeof(res->controls.items[n].id),
                 json_str(it, "id", ""));
        copy_str(res->controls.items[n].label,
                 sizeof(res->controls.items[n].label),
                 json_str(it, "label", ""));
        copy_str(res->controls.items[n].type,
                 sizeof(res->controls.items[n].type),
                 json_str(it, "type", "bool"));
        res->controls.items[n].range = json_int(it, "range", 2);
        res->controls.items[n].def   = json_int(it, "default", 0);
        cJSON *h = cJSON_GetObjectItem(it, "hidden");
        res->controls.items[n].hidden = cJSON_IsTrue(h);
        n++;
    }
    res->controls.count = n;
}

static void parse_credits(Resources *res, cJSON *obj) {
    res->credits.group_count = 0;
    res->credits.copyright_count = 0;
    res->credits.image[0] = '\0';
    if (!cJSON_IsObject(obj)) return;

    copy_str(res->credits.image, sizeof(res->credits.image),
             json_str(obj, "image", ""));

    cJSON *groups = cJSON_GetObjectItem(obj, "groups");
    if (cJSON_IsArray(groups)) {
        cJSON *g;
        cJSON_ArrayForEach(g, groups) {
            if (res->credits.group_count >= 6) break;
            if (!cJSON_IsObject(g)) continue;
            int gi = res->credits.group_count;
            copy_str(res->credits.groups[gi].label,
                     sizeof(res->credits.groups[gi].label),
                     json_str(g, "label", ""));
            res->credits.groups[gi].name_count = 0;
            cJSON *names = cJSON_GetObjectItem(g, "names");
            if (cJSON_IsArray(names)) {
                cJSON *nm;
                cJSON_ArrayForEach(nm, names) {
                    int ni = res->credits.groups[gi].name_count;
                    if (ni >= 4) break;
                    if (!cJSON_IsString(nm)) continue;
                    copy_str(res->credits.groups[gi].names[ni],
                             sizeof(res->credits.groups[gi].names[ni]),
                             nm->valuestring);
                    res->credits.groups[gi].name_count++;
                }
            }
            res->credits.group_count++;
        }
    }

    cJSON *copyr = cJSON_GetObjectItem(obj, "copyright");
    if (cJSON_IsArray(copyr)) {
        cJSON *c;
        cJSON_ArrayForEach(c, copyr) {
            if (res->credits.copyright_count >= 4) break;
            if (!cJSON_IsString(c)) continue;
            copy_str(res->credits.copyright[res->credits.copyright_count],
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
    res->ending.throne_backdrop[0] = '\0';
    if (!cJSON_IsObject(obj)) return;
    copy_str(res->ending.grass_tile,
             sizeof(res->ending.grass_tile),
             json_str(obj, "grass_tile", ""));
    copy_str(res->ending.carpet_tile,
             sizeof(res->ending.carpet_tile),
             json_str(obj, "carpet_tile", ""));
    copy_str(res->ending.hero_tile,
             sizeof(res->ending.hero_tile),
             json_str(obj, "hero_tile", ""));
    copy_str(res->ending.throne_backdrop,
             sizeof(res->ending.throne_backdrop),
             json_str(obj, "throne_backdrop", ""));
    res->ending.grid_width     = json_int(obj, "grid_width",     res->ending.grid_width);
    res->ending.grid_height    = json_int(obj, "grid_height",    res->ending.grid_height);
    res->ending.carpet_column  = json_int(obj, "carpet_column",  res->ending.carpet_column);
    res->ending.carpet_length  = json_int(obj, "carpet_length",  res->ending.carpet_length);
    res->ending.frame_count    = json_int(obj, "frame_count",    res->ending.frame_count);
    res->ending.ticks_per_step = json_int(obj, "ticks_per_step", res->ending.ticks_per_step);
    cJSON *tb = cJSON_GetObjectItem(obj, "troop_border");
    if (cJSON_IsBool(tb)) res->ending.troop_border = cJSON_IsTrue(tb);
}

// Banner defaults preserve  text, so a
// game.json missing strings.banners still produces parity-correct prompts.
// %TOKEN% placeholders are resolved at render time by resources_format_template.


static void parse_banners(ResBanners *b, cJSON *obj, Resources *res) {
    // No defaults: every key MUST come from the pack. A missing key is
    // recorded (and printed) and hard-fails the load -- never a silent English
    // fallback. (obj may be NULL if the pack omits the whole section.)
    #define SET_BANNER(field, key) do { \
        const char *s = cJSON_IsObject(obj) ? json_str(obj, key, NULL) : NULL; \
        if (s) copy_str(b->field, sizeof(b->field), s); \
        else { fprintf(stdout, "resources: pack missing string key '%s'\n", key); \
               res->strings_missing++; } \
    } while (0)
    SET_BANNER(chest_gold,        "chest_gold");
    SET_BANNER(chest_commission,  "chest_commission");
    SET_BANNER(chest_spell_power, "chest_spell_power");
    SET_BANNER(chest_max_spells,  "chest_max_spells");
    SET_BANNER(chest_new_spell,   "chest_new_spell");
    SET_BANNER(chest_empty,       "chest_empty");
    SET_BANNER(town_header,             "town_header");
    SET_BANNER(town_gold_label,         "town_gold_label");
    SET_BANNER(town_row_contract,       "town_row_contract");
    SET_BANNER(town_row_boat_rent,      "town_row_boat_rent");
    SET_BANNER(town_row_boat_cancel,    "town_row_boat_cancel");
    SET_BANNER(town_row_info,           "town_row_info");
    SET_BANNER(town_row_spell,          "town_row_spell");
    SET_BANNER(town_row_spell_none,     "town_row_spell_none");
    SET_BANNER(town_row_siege_buy,      "town_row_siege_buy");
    SET_BANNER(town_row_siege_owned,    "town_row_siege_owned");
    SET_BANNER(town_contract_new,       "town_contract_new");
    SET_BANNER(town_contract_none,      "town_contract_none");
    SET_BANNER(town_boat_vacate_first,  "town_boat_vacate_first");
    SET_BANNER(town_no_gold,            "town_no_gold");
    SET_BANNER(town_intel_unavailable,  "town_intel_unavailable");
    SET_BANNER(town_intel_castle_under, "town_intel_castle_under");
    SET_BANNER(town_intel_owner_rule,   "town_intel_owner_rule");
    SET_BANNER(town_intel_owner_none,   "town_intel_owner_none");
    SET_BANNER(town_intel_owner_player, "town_intel_owner_player");
    SET_BANNER(town_intel_owner_king,   "town_intel_owner_king");
    SET_BANNER(town_intel_count_named,  "town_intel_count_named");
    SET_BANNER(town_intel_count_numeric,"town_intel_count_numeric");
    SET_BANNER(town_intel_monsters_generic, "town_intel_monsters_generic");
    SET_BANNER(town_intel_no_garrison,  "town_intel_no_garrison");
    SET_BANNER(town_spell_unavailable,  "town_spell_unavailable");
    SET_BANNER(town_spell_at_cap,       "town_spell_at_cap");
    SET_BANNER(town_spell_can_learn,    "town_spell_can_learn");
    SET_BANNER(town_siege_already,      "town_siege_already");
    SET_BANNER(town_siege_purchased,    "town_siege_purchased");
    SET_BANNER(spell_time_stop,                "spell_time_stop");
    SET_BANNER(spell_find_villain_no_contract, "spell_find_villain_no_contract");
    SET_BANNER(spell_find_villain_success,     "spell_find_villain_success");
    SET_BANNER(spell_find_villain_none,        "spell_find_villain_none");
    SET_BANNER(spell_bridge_prompt,            "spell_bridge_prompt");
    SET_BANNER(spell_bridge_built,             "spell_bridge_built");
    SET_BANNER(spell_bridge_invalid,           "spell_bridge_invalid");
    SET_BANNER(spell_castle_gate_none,         "spell_castle_gate_none");
    SET_BANNER(spell_castle_gate_choose,       "spell_castle_gate_choose");
    SET_BANNER(spell_town_gate_none,           "spell_town_gate_none");
    SET_BANNER(spell_town_gate_choose,         "spell_town_gate_choose");
    SET_BANNER(spell_instant_army_fizzle,      "spell_instant_army_fizzle");
    SET_BANNER(spell_instant_army_no_room,     "spell_instant_army_no_room");
    SET_BANNER(spell_instant_army_success,     "spell_instant_army_success");
    SET_BANNER(spell_raise_control_success,    "spell_raise_control_success");
    SET_BANNER(spell_gate_teleported,          "spell_gate_teleported");
    SET_BANNER(spell_gate_invalid,             "spell_gate_invalid");
    SET_BANNER(encounter_join_named,           "encounter_join_named");
    SET_BANNER(encounter_join_numeric,         "encounter_join_numeric");
    SET_BANNER(encounter_wanderers,            "encounter_wanderers");
    SET_BANNER(encounter_hostile_header,       "encounter_hostile_header");
    SET_BANNER(encounter_hostile_unknown,      "encounter_hostile_unknown");
    SET_BANNER(encounter_hostile_count_named,  "encounter_hostile_count_named");
    SET_BANNER(encounter_hostile_count_numeric,"encounter_hostile_count_numeric");
    SET_BANNER(alcove_offer,                   "alcove_offer");
    SET_BANNER(alcove_already,                 "alcove_already");
    SET_BANNER(alcove_taught,                  "alcove_taught");
    SET_BANNER(alcove_no_gold,                 "alcove_no_gold");
    SET_BANNER(no_spell_banner,                "no_spell_banner");
    SET_BANNER(new_game_intro,                 "new_game_intro");
    SET_BANNER(combat_victory_named,           "combat_victory_named");
    SET_BANNER(combat_victory_unnamed,         "combat_victory_unnamed");
    SET_BANNER(dwelling_recruit_prompt,        "dwelling_recruit_prompt");
    SET_BANNER(dwelling_none_this_week,        "dwelling_none_this_week");
    SET_BANNER(dwelling_empty,                 "dwelling_empty");
    SET_BANNER(telecave_teleport,              "telecave_teleport");
    SET_BANNER(telecave_inert,                 "telecave_inert");
    SET_BANNER(navmap_pickup,                  "navmap_pickup");
    SET_BANNER(crystal_ball_pickup,            "crystal_ball_pickup");
    SET_BANNER(astrology_header,               "astrology_header");
    SET_BANNER(astrology_body,                 "astrology_body");
    SET_BANNER(temp_death,                     "temp_death");
    SET_BANNER(combat_give_up_header,          "combat_give_up_header");
    SET_BANNER(combat_give_up_body,            "combat_give_up_body");
    SET_BANNER(combat_scouts_header,           "combat_scouts_header");
    SET_BANNER(combat_scouts_count,            "combat_scouts_count");
    SET_BANNER(combat_scouts_small_band,       "combat_scouts_small_band");
    SET_BANNER(combat_header_siege,            "combat_header_siege");
    SET_BANNER(combat_header_default,          "combat_header_default");
    SET_BANNER(signpost_with_body,             "signpost_with_body");
    SET_BANNER(signpost_title_only,            "signpost_title_only");
    SET_BANNER(budget_header,                  "budget_header");
    SET_BANNER(budget_on_hand,                 "budget_on_hand");
    SET_BANNER(budget_payment,                 "budget_payment");
    SET_BANNER(budget_boat,                    "budget_boat");
    SET_BANNER(budget_army,                    "budget_army");
    SET_BANNER(budget_balance,                 "budget_balance");
    SET_BANNER(status_days_left,               "status_days_left");
    SET_BANNER(status_time_stop,               "status_time_stop");
    SET_BANNER(body_save_confirm,              "body_save_confirm");
    SET_BANNER(body_search,                    "body_search");
    SET_BANNER(body_dismiss_pick,              "body_dismiss_pick");
    SET_BANNER(body_dismiss_last,              "body_dismiss_last");
    SET_BANNER(body_home_castle,               "body_home_castle");
    SET_BANNER(body_own_castle,                "body_own_castle");
    SET_BANNER(body_garrison_row_named,        "body_garrison_row_named");
    SET_BANNER(body_garrison_row_empty,        "body_garrison_row_empty");
    SET_BANNER(body_navigate_row,              "body_navigate_row");
    SET_BANNER(body_no_continents,             "body_no_continents");
    SET_BANNER(body_must_be_sailing,           "body_must_be_sailing");
    SET_BANNER(cannot_garrison_last,           "cannot_garrison_last");
    SET_BANNER(no_troop_slots,                 "no_troop_slots");
    SET_BANNER(army_cannot_handle,             "army_cannot_handle");
    SET_BANNER(no_troops_to_garrison,          "no_troops_to_garrison");
    SET_BANNER(castle_garrison_empty,          "castle_garrison_empty");
    SET_BANNER(spell_unavailable,              "spell_unavailable");
    SET_BANNER(spell_not_known,                "spell_not_known");
    SET_BANNER(spell_unknown,                  "spell_unknown");
    #undef SET_BANNER
}

// ---- Combat log strings (game.json strings.combat_log) ------------
// Defaults follow docs/OPENBOUNTY-SPEC.md section 25.11 verbatim.


static void parse_combat_log(ResCombatLog *cl, cJSON *obj, Resources *res) {
    #define SET_CL(field, key) do { \
        const char *s = cJSON_IsObject(obj) ? json_str(obj, key, NULL) : NULL; \
        if (s) copy_str(cl->field, sizeof(cl->field), s); \
        else { fprintf(stdout, "resources: pack missing string key '%s'\n", key); \
               res->strings_missing++; } \
    } while (0)
    SET_CL(melee_hit,        "melee_hit");
    SET_CL(retaliate,        "retaliate");
    SET_CL(ranged_hit,       "ranged_hit");
    SET_CL(ranged_no_effect, "ranged_no_effect");
    SET_CL(no_effect_msg,    "no_effect_msg");
    SET_CL(fly,              "fly");
    SET_CL(move,             "move");
    SET_CL(wait,             "wait");
    SET_CL(pass,             "pass");
    SET_CL(frozen,           "frozen");
    SET_CL(ooc,              "ooc");
    SET_CL(immune,           "immune");
    SET_CL(cloned,           "cloned");
    SET_CL(resurrected,      "resurrected");
    SET_CL(teleported,       "teleported");
    SET_CL(only_one_spell,   "only_one_spell");
    SET_CL(no_spell_type,    "no_spell_type");
    SET_CL(cannot_cast,      "cannot_cast");
    SET_CL(cast_fireball,    "cast_fireball");
    SET_CL(cast_lightning,   "cast_lightning");
    SET_CL(cast_turn_undead, "cast_turn_undead");
    SET_CL(select_clone,     "select_clone");
    SET_CL(select_freeze,    "select_freeze");
    SET_CL(select_resurrect, "select_resurrect");
    SET_CL(select_damage,    "select_damage");
    SET_CL(select_teleport,  "select_teleport");
    SET_CL(select_dest,      "select_dest");
    SET_CL(cant_shoot,       "cant_shoot");
    SET_CL(no_ammo,          "no_ammo");
    SET_CL(cant_fly,         "cant_fly");
    SET_CL(give_up_prompt,   "give_up_prompt");
    SET_CL(exit_hint,        "exit_hint");
    #undef SET_CL
}

// ---- UI labels (game.json strings.ui / .menu / .stats / .army_view /
//                 .morale / .count_buckets / .difficulty / .keybinds /
//                 .startup) ----------------------------------------------



// Override one buffer field if the JSON object has a string at `key`.
#define UI_SET(field, key) do { \
    const char *s = cJSON_IsObject(obj) ? json_str(obj, key, NULL) : NULL; \
    if (s) copy_str(ui->field, sizeof(ui->field), s); \
    else { fprintf(stdout, "resources: pack missing string key '%s'\n", key); \
           res->strings_missing++; } \
} while (0)

static void parse_count_buckets(ResCountBucket *out, int *out_n, int max,
                                cJSON *arr) {
    if (!cJSON_IsArray(arr)) return;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= max) break;
        if (!cJSON_IsObject(it)) continue;
        out[n].threshold = json_int(it, "max", 0x7FFFFFFF);
        copy_str(out[n].label, sizeof(out[n].label),
                 json_str(it, "label", ""));
        n++;
    }
    *out_n = n;
}

static void parse_ui(Resources *res, cJSON *root_strings) {
    ResUI *ui = &res->ui;
    // No defaults: keys come only from the pack (missing -> recorded, hard-fail).

    cJSON *jui = cJSON_GetObjectItem(root_strings, "ui");
    if (cJSON_IsObject(jui)) {
        cJSON *obj = jui;
        UI_SET(press_esc_to_exit, "press_esc_to_exit");
        UI_SET(quit_to_dos_prompt, "quit_to_dos_prompt");
        UI_SET(out_of_control,    "out_of_control");
        UI_SET(worldmap_hint_your_map,  "worldmap_hint_your_map");
        UI_SET(worldmap_hint_whole_map, "worldmap_hint_whole_map");
    }

    cJSON *jmenu = cJSON_GetObjectItem(root_strings, "menu");
    if (cJSON_IsObject(jmenu)) {
        cJSON *obj = jmenu;
        UI_SET(menu_root_title,    "root_title");
        UI_SET(menu_views_title,   "views_title");
        UI_SET(menu_options_title, "options_title");
        cJSON *items = cJSON_GetObjectItem(jmenu, "items");
        if (cJSON_IsObject(items)) {
            obj = items;
            UI_SET(menu_back,      "back");
            UI_SET(menu_exit,      "exit");
            UI_SET(menu_save,      "save");
            UI_SET(menu_load,      "load");
            UI_SET(menu_new_game,  "new_game");
            UI_SET(menu_views,     "views");
            UI_SET(menu_options,   "options");
            UI_SET(menu_army,      "army");
            UI_SET(menu_spells,    "spells");
            UI_SET(menu_character, "character");
            UI_SET(menu_contract,  "contract");
            UI_SET(menu_puzzle,    "puzzle");
            UI_SET(menu_view_map,  "view_map");
        }
    }

    cJSON *jstats = cJSON_GetObjectItem(root_strings, "stats");
    if (cJSON_IsObject(jstats)) {
        cJSON *obj = jstats;
        UI_SET(stat_leadership,         "leadership");
        UI_SET(stat_commission,         "commission");
        UI_SET(stat_gold,               "gold");
        UI_SET(stat_spell_power,        "spell_power");
        UI_SET(stat_max_spells,         "max_spells");
        UI_SET(stat_villains_caught,    "villains_caught");
        UI_SET(stat_artifacts_found,    "artifacts_found");
        UI_SET(stat_castles_garrisoned, "castles_garrisoned");
        UI_SET(stat_followers_killed,   "followers_killed");
        UI_SET(stat_current_score,      "current_score");
    }

    cJSON *jav = cJSON_GetObjectItem(root_strings, "army_view");
    if (cJSON_IsObject(jav)) {
        cJSON *obj = jav;
        UI_SET(army_skill,      "skill");
        UI_SET(army_move,       "move");
        UI_SET(army_morale,     "morale");
        UI_SET(army_hit_points, "hit_points");
        UI_SET(army_damage,     "damage");
        UI_SET(army_g_cost,     "g_cost");
    }

    cJSON *jmor = cJSON_GetObjectItem(root_strings, "morale");
    if (cJSON_IsObject(jmor)) {
        cJSON *obj = jmor;
        UI_SET(morale_normal, "normal");
        UI_SET(morale_low,    "low");
        UI_SET(morale_high,   "high");
    }

    cJSON *jcb = cJSON_GetObjectItem(root_strings, "count_buckets");
    if (cJSON_IsObject(jcb)) {
        parse_count_buckets(ui->count_buckets_army_view,
                            &ui->count_buckets_army_view_n,
                            RES_MAX_COUNT_BUCKETS,
                            cJSON_GetObjectItem(jcb, "army_view"));
        parse_count_buckets(ui->count_buckets_instant_army,
                            &ui->count_buckets_instant_army_n,
                            RES_MAX_COUNT_BUCKETS,
                            cJSON_GetObjectItem(jcb, "instant_army"));
    }

    cJSON *jdiff = cJSON_GetObjectItem(root_strings, "difficulty");
    if (cJSON_IsObject(jdiff)) {
        static const char *keys[4] = { "easy", "normal", "hard", "impossible" };
        for (int i = 0; i < 4; i++) {
            cJSON *e = cJSON_GetObjectItem(jdiff, keys[i]);
            if (!cJSON_IsObject(e)) continue;
            const char *l = json_str(e, "label", NULL);
            const char *m = json_str(e, "score_mult", NULL);
            if (l) copy_str(ui->difficulty[i].label,
                            sizeof(ui->difficulty[i].label), l);
            if (m) copy_str(ui->difficulty[i].score_mult,
                            sizeof(ui->difficulty[i].score_mult), m);
        }
    }

    cJSON *jkb = cJSON_GetObjectItem(root_strings, "keybinds");
    if (cJSON_IsArray(jkb)) {
        int n = 0;
        cJSON *e;
        cJSON_ArrayForEach(e, jkb) {
            if (n >= RES_MAX_KEYBINDS) break;
            if (!cJSON_IsObject(e)) continue;
            copy_str(ui->keybinds[n].key, sizeof(ui->keybinds[n].key),
                     json_str(e, "key", ""));
            copy_str(ui->keybinds[n].label, sizeof(ui->keybinds[n].label),
                     json_str(e, "label", ""));
            n++;
        }
        ui->keybind_count = n;
    }

    cJSON *jstart = cJSON_GetObjectItem(root_strings, "startup");
    if (cJSON_IsObject(jstart)) {
        cJSON *obj = jstart;
        UI_SET(startup_controls_hint,        "controls_hint");
        UI_SET(startup_class_select_hint,    "class_select_hint");
        UI_SET(startup_class_picker_missing, "class_picker_missing");
        UI_SET(startup_save_picker_title,    "save_picker_title");
        UI_SET(startup_save_picker_empty,    "save_picker_empty");
        UI_SET(startup_save_picker_new_game, "save_picker_new_game");
        UI_SET(startup_new_game_table_header,"new_game_table_header");
        UI_SET(startup_new_game_select_hint, "new_game_select_hint");
    }

    cJSON *jctl = cJSON_GetObjectItem(root_strings, "controls");
    if (cJSON_IsObject(jctl)) {
        cJSON *obj = jctl;
        UI_SET(controls_title, "title");
        UI_SET(controls_on,    "on");
        UI_SET(controls_off,   "off");
    }

    cJSON *jpr = cJSON_GetObjectItem(root_strings, "prompts");
    if (cJSON_IsObject(jpr)) {
        cJSON *obj = jpr;
        UI_SET(prompt_text_hint,          "text_hint");
        UI_SET(prompt_numeric_range_hint, "numeric_range_hint");
        UI_SET(prompt_yes_no_hint,        "yes_no_hint");
        UI_SET(prompt_numeric_5_hint,     "numeric_5_hint");
    }

    cJSON *jmisc = cJSON_GetObjectItem(root_strings, "ui");
    if (cJSON_IsObject(jmisc)) {
        cJSON *obj = jmisc;
        UI_SET(empty_slot, "empty_slot");
        UI_SET(combat_spells_title,      "combat_spells_title");
        UI_SET(combat_spells_col_combat, "combat_spells_col_combat");
        UI_SET(combat_spells_prompt,     "combat_spells_prompt");
        UI_SET(dwelling_kind_plains,       "dwelling_kind_plains");
        UI_SET(dwelling_kind_forest,       "dwelling_kind_forest");
        UI_SET(dwelling_kind_hill,         "dwelling_kind_hill");
        UI_SET(dwelling_kind_dungeon,      "dwelling_kind_dungeon");
        UI_SET(dwelling_recruit_how_many,  "dwelling_recruit_how_many");
        UI_SET(dwelling_info_available,    "dwelling_info_available");
        UI_SET(dwelling_info_cost,         "dwelling_info_cost");
        UI_SET(dwelling_info_gold,         "dwelling_info_gold");
        UI_SET(dwelling_info_recruit_cap,  "dwelling_info_recruit_cap");
        UI_SET(recruit_soldiers_title,    "recruit_soldiers_title");
        UI_SET(recruit_soldiers_how_many, "recruit_soldiers_how_many");
        UI_SET(own_castle_mode_garrison, "own_castle_mode_garrison");
        UI_SET(own_castle_mode_remove,   "own_castle_mode_remove");
        UI_SET(gate_title_town,          "gate_title_town");
        UI_SET(gate_title_castle,        "gate_title_castle");
        UI_SET(gate_footer_hint,         "gate_footer_hint");
        UI_SET(recruit_col_hint,         "recruit_col_hint");
    }

    cJSON *jtoasts = cJSON_GetObjectItem(root_strings, "toasts");
    if (cJSON_IsObject(jtoasts)) {
        cJSON *obj = jtoasts;
        UI_SET(toast_save_cancelled, "save_cancelled");
        UI_SET(toast_save_ok,        "save_ok");
        UI_SET(toast_save_failed,    "save_failed");
        UI_SET(toast_load_cancelled, "load_cancelled");
        UI_SET(toast_load_ok,        "load_ok");
        UI_SET(toast_load_failed,    "load_failed");
        UI_SET(toast_new_game,       "new_game");
    }

    cJSON *jcv = cJSON_GetObjectItem(root_strings, "contract_view");
    if (cJSON_IsObject(jcv)) {
        cJSON *obj = jcv;
        UI_SET(cv_title_no_contract, "title_no_contract");
        UI_SET(cv_label_name,        "label_name");
        UI_SET(cv_label_alias,       "label_alias");
        UI_SET(cv_label_reward,      "label_reward");
        UI_SET(cv_label_last_seen,   "label_last_seen");
        UI_SET(cv_label_castle,      "label_castle");
        UI_SET(cv_alias_none,        "alias_none");
        UI_SET(cv_castle_unknown,    "castle_unknown");
        UI_SET(cv_features_header,   "features_header");
        UI_SET(cv_crimes_header,     "crimes_header");
    }

    cJSON *jsv = cJSON_GetObjectItem(root_strings, "spells_view");
    if (cJSON_IsObject(jsv)) {
        cJSON *obj = jsv;
        UI_SET(sv_title,         "title");
        UI_SET(sv_combat_col,    "combat_col");
        UI_SET(sv_adventure_col, "adventure_col");
    }

    cJSON *jdt = cJSON_GetObjectItem(root_strings, "dialog_titles");
    if (cJSON_IsObject(jdt)) {
        cJSON *obj = jdt;
        UI_SET(dt_treasure,       "treasure");
        UI_SET(dt_teleport_cave,  "teleport_cave");
        UI_SET(dt_crystal_ball,   "crystal_ball");
        UI_SET(dt_foes,           "foes");
        UI_SET(dt_alcove_offer,   "alcove_offer");
        UI_SET(dt_alcove_result,  "alcove_result");
        UI_SET(dt_castle_default, "castle_default");
        UI_SET(dt_own_castle,     "own_castle");
        UI_SET(dt_search,         "search");
        UI_SET(dt_dismiss_army,   "dismiss_army");
        UI_SET(dt_dismiss_last,   "dismiss_last");
        UI_SET(dt_navigate,       "navigate");
        UI_SET(dt_garrison_pick,  "garrison_pick");
        UI_SET(dt_remove_pick,    "remove_pick");
        UI_SET(dt_save_confirm,   "save_confirm");
        UI_SET(dt_lose_fallback,  "lose_fallback");
        UI_SET(dt_win_fallback,   "win_fallback");
        UI_SET(dt_combat_victory, "combat_victory");
    }
}

#undef UI_SET

static void parse_strings(Resources *res, cJSON *obj) {
    // Banners + UI labels load defaults even when strings/* is missing, so
    // every dialog and HUD draw call has a body to render.
    parse_banners(&res->banners,
                  cJSON_IsObject(obj) ? cJSON_GetObjectItem(obj, "banners") : NULL, res);
    parse_combat_log(&res->combat_log,
                     cJSON_IsObject(obj) ? cJSON_GetObjectItem(obj, "combat_log") : NULL, res);
    parse_ui(res, obj);
    if (!cJSON_IsObject(obj)) return;
    parse_end_text(&res->win_text,  cJSON_GetObjectItem(obj, "win"));
    parse_end_text(&res->lose_text, cJSON_GetObjectItem(obj, "lose"));

    res->villain_desc_count = 0;
    cJSON *vd = cJSON_GetObjectItem(obj, "villain_descriptions");
    if (cJSON_IsObject(vd)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, vd) {
            if (res->villain_desc_count >= CAT_VILLAINS_MAX) break;
            const char *id = entry->string;
            if (!id || !id[0]) continue;
            ResVillainDesc *d = &res->villain_descs[res->villain_desc_count++];
            memset(d, 0, sizeof(*d));
            copy_str(d->id,       sizeof(d->id),       id);
            copy_str(d->alias,    sizeof(d->alias),    json_str(entry, "alias", ""));
            copy_str(d->features, sizeof(d->features), json_str(entry, "features", ""));
            copy_str(d->crimes,   sizeof(d->crimes),   json_str(entry, "crimes", ""));
        }
    }
}

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
    char *txt = slurp(path);
    if (!txt && base && base[0] && strcmp(lang, base) != 0) {
        fprintf(stdout, "resources: locale '%s' not found, using base locale '%s'\n",
                lang, base);
        snprintf(path, sizeof path, "strings/%s.json", base);
        txt = slurp(path);
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

    char *text = slurp(manifest_rel);
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

    copy_str(res->title,   sizeof(res->title),   json_str(root, "title", ""));
    copy_str(res->pack_id,   sizeof(res->pack_id),   json_str(root, "pack_id", ""));
    copy_str(res->pack_name, sizeof(res->pack_name), json_str(root, "pack_name", ""));
    res->version = json_int(root, "version", 1);

    cJSON *jtime = cJSON_GetObjectItem(root, "time");
    res->time.day_steps = json_int(jtime, "day_steps", 40);
    res->time.week_days = json_int(jtime, "week_days", 5);
    cJSON *jdpd = cJSON_GetObjectItem(jtime, "days_per_difficulty");
    res->time.days_per_difficulty[0] = json_int(jdpd, "easy",       900);
    res->time.days_per_difficulty[1] = json_int(jdpd, "normal",     600);
    res->time.days_per_difficulty[2] = json_int(jdpd, "hard",       400);
    res->time.days_per_difficulty[3] = json_int(jdpd, "impossible", 200);

    cJSON *jec = cJSON_GetObjectItem(root, "economy");
    res->economy.alcove_cost      = json_int(jec, "alcove_cost",     5000);
    res->economy.boat_cost_normal = json_int(jec, "boat_cost_normal", 500);
    res->economy.boat_cost_cheap  = json_int(jec, "boat_cost_cheap",  100);
    res->economy.siege_cost       = json_int(jec, "siege_cost",      3000);

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
            json_int_array(jch, "chance_gold",        ch->chance_gold,        4);
            json_int_array(jch, "chance_commission",  ch->chance_commission,  4);
            json_int_array(jch, "chance_spell_power", ch->chance_spell_power, 4);
            json_int_array(jch, "chance_max_spells",  ch->chance_max_spells,  4);
            json_int_array(jch, "chance_new_spell",   ch->chance_new_spell,   4);
            json_int_array(jch, "gold_min",           ch->gold_min,           4);
            json_int_array(jch, "gold_max",           ch->gold_max,           4);
            json_int_array(jch, "commission_min",     ch->commission_min,     4);
            json_int_array(jch, "commission_max",     ch->commission_max,     4);
            json_int_array(jch, "max_spells_base",    ch->max_spells_base,    4);
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
            sc->per_villain  = json_int(jsc, "per_villain",         sc->per_villain);
            sc->per_artifact = json_int(jsc, "per_artifact",        sc->per_artifact);
            sc->per_castle   = json_int(jsc, "per_castle",          sc->per_castle);
            sc->kill_penalty = json_int(jsc, "kill_penalty",        sc->kill_penalty);
            json_int_array(jsc, "difficulty_multiplier",
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
            tn->search_cost_days = json_int(jtn, "search_cost_days",
                                            tn->search_cost_days);
            cJSON *jtd = cJSON_GetObjectItem(jtn, "temp_death");
            if (cJSON_IsObject(jtd)) {
                copy_str(tn->temp_death_troop, sizeof(tn->temp_death_troop),
                         json_str(jtd, "troop", ""));
                tn->temp_death_count = json_int(jtd, "count",
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
                json_color(mm, "grass",    &col->minimap_grass);
                json_color(mm, "forest",   &col->minimap_forest);
                json_color(mm, "mountain", &col->minimap_mountain);
                json_color(mm, "water",    &col->minimap_water);
                json_color(mm, "desert",   &col->minimap_desert);
                json_color(mm, "fog",      &col->minimap_fog);
            }
            cJSON *db = cJSON_GetObjectItem(jcol, "difficulty_bar");
            if (cJSON_IsObject(db)) {
                json_color(db, "easy",       &col->difficulty_easy);
                json_color(db, "normal",     &col->difficulty_normal);
                json_color(db, "hard",       &col->difficulty_hard);
                json_color(db, "impossible", &col->difficulty_impossible);
            }
        }
    }

    cJSON *jct = cJSON_GetObjectItem(root, "contract");
    res->contract.cycle_length          = json_int(jct, "cycle_length", 5);
    res->contract.initial_last_contract = json_int(jct, "initial_last_contract", 4);

    // spawn: monster-generation tables (troop_chance_table +
    // dwelling_to_troop). Missing entries leave zeroed defaults; callers
    // must guard for empty pools.
    memset(&res->spawn, 0, sizeof(res->spawn));
    cJSON *jsp = cJSON_GetObjectItem(root, "spawn");
    if (cJSON_IsObject(jsp)) {
        cJSON *jcc = cJSON_GetObjectItem(jsp, "tier_chance_curve");
        if (cJSON_IsArray(jcc)) {
            int ti = 0;
            cJSON *row;
            cJSON_ArrayForEach(row, jcc) {
                if (ti >= RES_SPAWN_TIERS) break;
                if (cJSON_IsArray(row)) {
                    int ci = 0;
                    cJSON *v;
                    cJSON_ArrayForEach(v, row) {
                        if (ci >= RES_SPAWN_POOL_N - 1) break;
                        if (cJSON_IsNumber(v))
                            res->spawn.chance_curve[ti][ci] = v->valueint;
                        ci++;
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
                if (cJSON_IsArray(row)) {
                    int si = 0;
                    cJSON *v;
                    cJSON_ArrayForEach(v, row) {
                        if (si >= RES_SPAWN_POOL_N) break;
                        if (cJSON_IsString(v))
                            copy_str(res->spawn.troop_pool[ti][si],
                                     sizeof(res->spawn.troop_pool[ti][si]),
                                     v->valuestring);
                        si++;
                    }
                }
                ti++;
            }
        }
    }

    // Render geometry. Required: a pack is authored for one mode or the other
    // and guessing would silently mis-size every tile. resources_load returns
    // false when it is missing, and the caller reports it fatally.
    {
        cJSON *jr = cJSON_GetObjectItem(root, "render");
        const char *mode = json_str(jr, "mode", "");
        if (strcmp(mode, "legacy") == 0) {
            res->render.mode    = RENDER_MODE_LEGACY;
            res->render.tile_w  = 48;
            res->render.tile_h  = 34;
            res->render.tiles_w = 5;
            res->render.tiles_h = 5;
            res->render.ui_scale = 1;
        } else if (strcmp(mode, "modern") == 0) {
            res->render.mode    = RENDER_MODE_MODERN;
            res->render.tile_w  = json_int(jr, "tile_w",  96);
            res->render.tile_h  = json_int(jr, "tile_h",  96);
            res->render.tiles_w = json_int(jr, "tiles_w",  7);
            res->render.tiles_h = json_int(jr, "tiles_h",  7);
            res->render.ui_scale = json_int(jr, "ui_scale", 1);
        } else {
            res->render.mode = RENDER_MODE_NONE;
            res->render.ui_scale = 1;
            fprintf(stdout,
                    "resources: pack declares no render.mode "
                    "(expected \"legacy\" or \"modern\")\n");
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
            return false;
        }
    }

    cJSON *jw = cJSON_GetObjectItem(root, "world");
    // world.* user-facing strings are required from the pack too (no defaults).
    #define REQ_WORLD(field, key) do { \
        const char *s = json_str(jw, key, NULL); \
        if (s) copy_str(res->world.field, sizeof(res->world.field), s); \
        else { fprintf(stdout, "resources: pack missing string key 'world.%s'\n", key); \
               res->strings_missing++; } \
    } while (0)
    REQ_WORLD(starting_zone,    "starting_zone");
    REQ_WORLD(zone_noun,        "zone_noun");
    REQ_WORLD(zone_noun_plural, "zone_noun_plural");
    REQ_WORLD(default_name,     "default_name");
    #undef REQ_WORLD
    // Base locale code (names the strings/<language>.json file). Optional;
    // defaults to English so a pack that omits it still resolves a locale.
    copy_str(res->world.language, sizeof res->world.language,
             json_str(jw, "language", "en"));
    res->world.max_army_slots = json_int(jw, "max_army_slots", 5);
    res->world.fog_sight      = json_int(jw, "fog_sight",      3);
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

    parse_troops(res,      cJSON_GetObjectItem(root, "troops"));
    parse_spells(res,      cJSON_GetObjectItem(root, "spells"));
    parse_classes(res,     cJSON_GetObjectItem(root, "classes"));
    parse_villains(res,    cJSON_GetObjectItem(root, "villains"));
    parse_artifacts(res,   cJSON_GetObjectItem(root, "artifacts"));

    // Catalog-capacity contracts: a pack that exceeds a fixed engine array
    // must fail loudly at load, never be silently clamped into a world that
    // misrepresents it.
    {
        cJSON *jv = cJSON_GetObjectItem(root, "villains");
        if (cJSON_IsArray(jv) &&
            cJSON_GetArraySize(jv) > CAT_VILLAINS_MAX) {
            fprintf(stdout, "resources: pack declares %d villains; engine "
                    "capacity is %d\n", cJSON_GetArraySize(jv),
                    CAT_VILLAINS_MAX);
            cJSON_Delete(root);
            return false;
        }
        // The caught/prefought arrays are keyed by VillainDef.index: every
        // index must be in range and unique, else catches would be silently
        // unrecordable (or, via savegame load, write out of bounds).
        bool seen_idx[CAT_VILLAINS_MAX] = { false };
        for (int i = 0; i < res->villains_count; i++) {
            int vi = res->villains[i].index;
            if (vi < 0 || vi >= CAT_VILLAINS_MAX || seen_idx[vi]) {
                fprintf(stdout, "resources: villain '%s' has invalid or "
                        "duplicate index %d (must be unique, 0..%d)\n",
                        res->villains[i].id, vi, CAT_VILLAINS_MAX - 1);
                cJSON_Delete(root);
                return false;
            }
            seen_idx[vi] = true;
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
            copy_str(res->tuning.temp_death_troop,
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
        parse_strings(res, strings_root);            // NULL-safe: records misses
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

    g_resources = res;    // publish to table lookups
    return true;
}

void resources_free(Resources *res) {
    if (g_resources == res) g_resources = NULL;
    // Nothing heap-owned; catalogs and per-zone objects are inline.
}

void resources_republish(const Resources *res) {
    g_resources = res;
}

// ---- Lookups ---------------------------------------------------------------

const ResTown *resources_town_at(const Resources *r,
                                 const char *zone, int x, int y) {
    if (!r || !zone) return NULL;
    for (int i = 0; i < r->town_count; i++) {
        if (r->towns[i].x == x && r->towns[i].y == y &&
            strcmp(r->towns[i].zone, zone) == 0) {
            return &r->towns[i];
        }
    }
    return NULL;
}

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

const ResTown *resources_town_by_index(const Resources *r, int index) {
    if (!r) return NULL;
    for (int i = 0; i < r->town_count; i++) {
        if (r->towns[i].index == index) return &r->towns[i];
    }
    return NULL;
}

const ResCastle *resources_castle_at(const Resources *r,
                                     const char *zone, int x, int y) {
    if (!r || !zone) return NULL;
    for (int i = 0; i < r->castle_count; i++) {
        if (r->castles[i].x == x && r->castles[i].y == y &&
            strcmp(r->castles[i].zone, zone) == 0) {
            return &r->castles[i];
        }
    }
    return NULL;
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

// ---- Art manifest ----------------------------------------------------------

static void art_add(char out[][RES_PATH_LEN], int cap, int *n, const char *p) {
    if (!p || !p[0] || !out || *n >= cap) return;
    for (int i = 0; i < *n; i++)
        if (strcmp(out[i], p) == 0) return;    // already listed
    copy_str(out[*n], RES_PATH_LEN, p);
    (*n)++;
}

static void art_add_anim(char out[][RES_PATH_LEN], int cap, int *n,
                         const ResAnimSet *a) {
    if (!a) return;
    for (int f = 0; f < OB_FACE_COUNT; f++)
        for (int i = 0; i < a->count[f]; i++)
            art_add(out, cap, n, a->frames[f][i]);
}

int resources_art_manifest(const Resources *res, char out[][RES_PATH_LEN],
                           int cap) {
    int n = 0;
    if (!res) return 0;

    art_add_anim(out, cap, &n, &res->sprites.hero_walk);
    art_add_anim(out, cap, &n, &res->sprites.hero_idle);
    art_add_anim(out, cap, &n, &res->sprites.hero_boat);

    for (int i = 0; i < res->sprites.combat_count; i++)
        art_add(out, cap, &n, res->sprites.combat[i]);

    art_add(out, cap, &n, res->sprites.font);
    art_add(out, cap, &n, res->sprites.puzzle_cover);
    art_add(out, cap, &n, res->sprites.town_backdrop);
    art_add(out, cap, &n, res->sprites.castle_backdrop);
    art_add(out, cap, &n, res->sprites.plains_backdrop);
    art_add(out, cap, &n, res->sprites.forest_backdrop);
    art_add(out, cap, &n, res->sprites.hillcave_backdrop);
    art_add(out, cap, &n, res->sprites.dungeon_backdrop);
    art_add(out, cap, &n, res->sprites.ending_win);
    art_add(out, cap, &n, res->sprites.ending_lose);
    for (int i = 0; i < res->sprites.view_icons_extra_count; i++)
        art_add(out, cap, &n, res->sprites.view_icons_extra[i]);
    art_add(out, cap, &n, res->sprites.hud_contract_silhouette);
    art_add(out, cap, &n, res->sprites.hud_siege_silhouette);
    for (int i = 0; i < res->sprites.hud_siege_animation_count; i++)
        art_add(out, cap, &n, res->sprites.hud_siege_animation[i]);
    art_add(out, cap, &n, res->sprites.hud_magic_silhouette);
    for (int i = 0; i < res->sprites.hud_magic_animation_count; i++)
        art_add(out, cap, &n, res->sprites.hud_magic_animation[i]);
    art_add(out, cap, &n, res->sprites.hud_puzzle_grid);
    art_add(out, cap, &n, res->sprites.hud_gold_purse);
    art_add(out, cap, &n, res->sprites.hud_bar_strip);
    art_add(out, cap, &n, res->sprites.chrome_overworld);
    art_add(out, cap, &n, res->sprites.splash_logo);
    art_add(out, cap, &n, res->sprites.splash_title);
    art_add(out, cap, &n, res->sprites.class_picker);
    art_add(out, cap, &n, res->sprites.class_highlight);
    art_add(out, cap, &n, res->sprites.orb);

    art_add(out, cap, &n, res->ending.grass_tile);
    art_add(out, cap, &n, res->ending.carpet_tile);
    art_add(out, cap, &n, res->ending.hero_tile);
    art_add(out, cap, &n, res->ending.throne_backdrop);

    for (int i = 0; i < res->classes_count; i++) {
        art_add(out, cap, &n, res->classes[i].portrait);
        art_add_anim(out, cap, &n, &res->class_hero[i].walk);
        art_add_anim(out, cap, &n, &res->class_hero[i].idle);
        art_add_anim(out, cap, &n, &res->class_hero[i].boat);
        art_add(out, cap, &n, res->class_hero[i].tile);
    }

    for (int i = 0; i < res->troops_count; i++) {
        art_add(out, cap, &n, res->troops[i].sprite);
        for (int f = 0; f < res->troops[i].anim_count; f++)
            art_add(out, cap, &n, res->troops[i].anim[f]);
    }

    for (int i = 0; i < res->villains_count; i++) {
        const VillainDef *v = &res->villains[i];
        art_add(out, cap, &n, v->portrait);
        if (v->anim_count > 0) {
            for (int f = 0; f < v->anim_count; f++)
                art_add(out, cap, &n, v->anim[f]);
            continue;
        }
        // No declared array: the shell derives <portrait-stem>_NN.png
        // siblings. Mirror that here so the manifest is complete either way.
        char stem[RES_PATH_LEN];
        copy_str(stem, sizeof stem, v->portrait);
        size_t sl = strlen(stem);
        if (sl >= 7 && stem[sl - 7] == '_' && stem[sl - 4] == '.') stem[sl - 7] = '\0';
        else if (sl >= 4 && stem[sl - 4] == '.') stem[sl - 4] = '\0';
        for (int f = 0; f < OB_ANIM_FRAMES_DEFAULT; f++) {
            char p[RES_PATH_LEN];
            snprintf(p, sizeof p, "%s_%02d.png", stem, f);
            art_add(out, cap, &n, p);
        }
    }

    for (int i = 0; i < res->artifacts_count; i++)
        art_add(out, cap, &n, res->artifacts[i].icon);

    // Placed-object art: chosen by the engine's interact kind in map.c, not
    // declared in game.json at all. Asked for rather than duplicated.
    {
        int nobj = 0;
        const char *const *obj = map_object_art_names(&nobj);
        for (int i = 0; i < nobj; i++) {
            char p[RES_PATH_LEN];
            snprintf(p, sizeof p, "art/tiles/%s.png", obj[i]);
            art_add(out, cap, &n, p);
        }
        // Town art is per catalog entry: the shared "town" tile only while
        // some town has no `art` of its own, plus each declared stem once.
        {
            bool shared = (res->town_count == 0);
            for (int i = 0; i < res->town_count; i++) {
                const char *a = res->towns[i].art;
                if (!a[0]) { shared = true; continue; }
                char p[RES_PATH_LEN];
                snprintf(p, sizeof p, "art/tiles/%s.png", a);
                art_add(out, cap, &n, p);
            }
            if (shared) art_add(out, cap, &n, "art/tiles/town.png");
        }
        // Wandering-army art is per zone: the shared tile only while some
        // zone has no `army_art`, plus each declared stem once.
        {
            bool shared = (res->zone_count == 0);
            for (int i = 0; i < res->zone_count; i++) {
                const char *a = res->zones[i].army_art;
                if (!a[0]) { shared = true; continue; }
                char p[RES_PATH_LEN];
                snprintf(p, sizeof p, "art/tiles/%s.png", a);
                art_add(out, cap, &n, p);
            }
            if (shared) art_add(out, cap, &n, "art/tiles/wandering_army.png");
        }
        // Castle art follows the footprint (REQ-228): a pack ships the six
        // 3x2 pieces only if some castle stamps 3x2, and the single `castle`
        // tile only if some castle stamps 1x1.
        bool used[2] = { false, false };
        for (int i = 0; i < res->castle_count; i++)
            used[res->castles[i].footprint == RES_CASTLE_FOOTPRINT_1X1] = true;
        for (int fp = 0; fp < 2; fp++) {
            if (!used[fp]) continue;
            int nc = 0;
            const char *const *cn =
                map_castle_art_names((ResCastleFootprint)fp, &nc);
            for (int i = 0; i < nc; i++) {
                char p[RES_PATH_LEN];
                snprintf(p, sizeof p, "art/tiles/%s.png", cn[i]);
                art_add(out, cap, &n, p);
            }
        }
    }

    // Tile art: tile_codes carry a bare name that tile_cache resolves under
    // art/tiles/, or under art/tiles/<tile_set>/ for a zone that declares a
    // set. List the shared set only while some zone uses it, and each
    // declared set once, so a pack ships exactly the terrain it draws.
    bool shared = (res->zone_count == 0);
    for (int zi = 0; zi < res->zone_count; zi++)
        if (!res->zones[zi].tile_set[0]) shared = true;
    for (int zi = -1; zi < res->zone_count; zi++) {
        const char *set = NULL;
        if (zi < 0) {
            if (!shared) continue;
        } else {
            set = res->zones[zi].tile_set;
            if (!set[0]) continue;
            bool dup = false;
            for (int k = 0; k < zi; k++)
                if (strcmp(res->zones[k].tile_set, set) == 0) dup = true;
            if (dup) continue;
        }
        for (int i = 0; i < RES_TILE_CODE_COUNT; i++) {
            if (!res->tile_codes[i].present || !res->tile_codes[i].art[0]) continue;
            char p[RES_PATH_LEN];
            if (set)
                snprintf(p, sizeof p, "art/tiles/%s/%s.png", set, res->tile_codes[i].art);
            else
                snprintf(p, sizeof p, "art/tiles/%s.png", res->tile_codes[i].art);
            art_add(out, cap, &n, p);
        }
    }

    return n;
}

const ResClassHero *resources_class_hero(const Resources *r, const char *class_id) {
    if (!r || !class_id) return NULL;
    for (int i = 0; i < r->classes_count; i++) {
        if (strcmp(r->classes[i].id, class_id) != 0) continue;
        const ResClassHero *h = &r->class_hero[i];
        bool any = h->tile[0] != '\0';
        for (int f = 0; f < OB_FACE_COUNT && !any; f++)
            any = h->walk.count[f] || h->idle.count[f] || h->boat.count[f];
        return any ? h : NULL;
    }
    return NULL;
}
