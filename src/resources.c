#include "resources.h"
#include "cJSON.h"
#include "assets.h"
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
    // Pack-relative paths now resolve directly against the global pack
    // stack — no per-Resources prefix. This function is an identity
    // copy kept so existing call sites compile during the refactor.
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
static void fill_zone_town(cJSON *j, void *dst) {
    ResZoneTown *t = (ResZoneTown *)dst;
    t->x = json_int(j, "x", 0); t->y = json_int(j, "y", 0);
    copy_str(t->id, sizeof(t->id), json_str(j, "id", ""));
    t->boat_x = json_int(j, "boat_x", -1);
    t->boat_y = json_int(j, "boat_y", -1);
}
static void fill_zone_castle(cJSON *j, void *dst) {
    ResZoneCastle *c = (ResZoneCastle *)dst;
    c->x = json_int(j, "x", 0); c->y = json_int(j, "y", 0);
    copy_str(c->id, sizeof(c->id), json_str(j, "id", ""));
    // Optional `decorations` array — extra wall pieces around the gate.
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
        parse_zone_objects_array(cJSON_GetObjectItem(it, "towns"),
                                 RES_MAX_ZONE_OBJECTS, &z->town_count,
                                 z->towns, sizeof(ResZoneTown), fill_zone_town);
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
        cJSON *anim = cJSON_GetObjectItem(it, "anim");
        if (cJSON_IsArray(anim)) {
            int n = 0;
            cJSON *fr;
            cJSON_ArrayForEach(fr, anim) {
                if (n >= 4) break;
                if (cJSON_IsString(fr))
                    copy_str(t->anim[n], sizeof(t->anim[n]), fr->valuestring);
                n++;
            }
        }
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

static void parse_string_array(cJSON *arr, char dst[][RES_PATH_LEN],
                               int cap, int *out_count) {
    if (out_count) *out_count = 0;
    if (!cJSON_IsArray(arr)) return;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= cap) break;
        if (cJSON_IsString(it)) {
            copy_str(dst[n], RES_PATH_LEN, it->valuestring);
            n++;
        }
    }
    if (out_count) *out_count = n;
}

static void parse_sprites(Resources *res, cJSON *obj) {
    if (!cJSON_IsObject(obj)) return;

    cJSON *hero = cJSON_GetObjectItem(obj, "hero");
    if (cJSON_IsObject(hero)) {
        int n = 0;
        parse_string_array(cJSON_GetObjectItem(hero, "walk"),
                           res->sprites.hero_walk, RES_ANIM_FRAMES, &n);
        parse_string_array(cJSON_GetObjectItem(hero, "boat"),
                           res->sprites.hero_boat, RES_ANIM_FRAMES, &n);
    }

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
        int n = 0;
        parse_string_array(cJSON_GetObjectItem(hud, "siege_animation"),
                           res->sprites.hud_siege_animation,
                           RES_ANIM_FRAMES, &n);
        copy_str(res->sprites.hud_magic_silhouette,
                 sizeof(res->sprites.hud_magic_silhouette),
                 json_str(hud, "magic_silhouette", ""));
        parse_string_array(cJSON_GetObjectItem(hud, "magic_animation"),
                           res->sprites.hud_magic_animation,
                           RES_ANIM_FRAMES, &n);
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
static void load_banner_defaults(ResBanners *b) {
    copy_str(b->chest_gold, sizeof(b->chest_gold),
        "After scouring the area,\n"
        "you fall upon a hidden\n"
        "treasure cache. You may:\n\n"
        "A) Take the %GOLD% gold.\n"
        "B) Distribute the gold to\n"
        "   the peasants, increasing\n"
        "   your leadership by %LEADERSHIP%.");
    copy_str(b->chest_commission, sizeof(b->chest_commission),
        "After surveying the area,\n"
        "you discover that it is\n"
        "rich in mineral deposits.\n\n"
        "The King rewards you for\n"
        "your find by increasing\n"
        "your weekly income by %POINTS%");
    copy_str(b->chest_spell_power, sizeof(b->chest_spell_power),
        "Traversing the area, you\n"
        "stumble upon a time worn\n"
        "cannister. Curious, you un-\n"
        "stop the bottle, releasing\n"
        "a powerful genie who raises\n"
        "your Spell Power by %POINTS% and\n"
        "vanishes.");
    copy_str(b->chest_max_spells, sizeof(b->chest_max_spells),
        "A tribe of nomads greet you\n"
        "and your army warmly. Their\n"
        "shaman, in awe of your\n"
        "prowess, teaches you the\n"
        "secret of his tribe's magic.\n"
        "Your maximum spell capacity\n"
        "is increased by %POINTS%");
    copy_str(b->chest_new_spell, sizeof(b->chest_new_spell),
        "You have captured a\n"
        "mischevious imp which has\n"
        "been terrorizing the\n"
        "region. In exchange for\n"
        "its release, you receive:\n\n"
        "   %COUNT% %SPELL% spell.");
    copy_str(b->chest_empty, sizeof(b->chest_empty),
        "The chest was empty!");

    // Town overlay .
    copy_str(b->town_header,        sizeof(b->town_header),        "Town of %NAME%");
    copy_str(b->town_gold_label,    sizeof(b->town_gold_label),    "GP=%GOLD%K");
    copy_str(b->town_row_contract,  sizeof(b->town_row_contract),  "A) Get New Contract");
    copy_str(b->town_row_boat_rent, sizeof(b->town_row_boat_rent), "B) Rent boat (%COST% week)");
    copy_str(b->town_row_boat_cancel, sizeof(b->town_row_boat_cancel),
                                                                  "B) Cancel boat rental");
    copy_str(b->town_row_info,      sizeof(b->town_row_info),      "C) Gather information");
    copy_str(b->town_row_spell,     sizeof(b->town_row_spell),     "D) %SPELL% spell (%SPELL_COST%)");
    copy_str(b->town_row_spell_none, sizeof(b->town_row_spell_none), "D) (no spell available)");
    copy_str(b->town_row_siege_buy,  sizeof(b->town_row_siege_buy), "E) Buy seige weapons (%SIEGE_COST%)");
    copy_str(b->town_row_siege_owned, sizeof(b->town_row_siege_owned), "E) Siege weapons (owned)");

    // Town action toasts.
    copy_str(b->town_contract_new,  sizeof(b->town_contract_new),
        "New contract: %VILLAIN%.\nReward: %REWARD% gold.\nLast seen on %ZONE%.");
    copy_str(b->town_contract_none, sizeof(b->town_contract_none),
        "No contracts are available right now.");
    copy_str(b->town_boat_vacate_first, sizeof(b->town_boat_vacate_first),
        "\n\nPlease vacate the boat first");
    copy_str(b->town_no_gold, sizeof(b->town_no_gold),
        "\n\n\nYou don't have enough gold!");
    copy_str(b->town_intel_unavailable, sizeof(b->town_intel_unavailable),
        "No intelligence is available here.");
    copy_str(b->town_intel_castle_under, sizeof(b->town_intel_castle_under),
        "Castle %NAME% is under\n");
    copy_str(b->town_intel_owner_rule, sizeof(b->town_intel_owner_rule),
        "%OWNER%'s rule.\n\n");
    copy_str(b->town_intel_owner_none, sizeof(b->town_intel_owner_none),
        "no one");
    copy_str(b->town_intel_owner_player, sizeof(b->town_intel_owner_player),
        "your");
    copy_str(b->town_intel_owner_king, sizeof(b->town_intel_owner_king),
        "the King");
    copy_str(b->town_intel_count_named, sizeof(b->town_intel_count_named),
        "  %LABEL% %TROOP%\n");
    copy_str(b->town_intel_count_numeric, sizeof(b->town_intel_count_numeric),
        "  %COUNT% %TROOP%\n");
    copy_str(b->town_intel_monsters_generic, sizeof(b->town_intel_monsters_generic),
        "  Various groups of monsters\n  occupy the castle.");
    copy_str(b->town_intel_no_garrison, sizeof(b->town_intel_no_garrison),
        "  (no garrison)");
    copy_str(b->town_spell_unavailable, sizeof(b->town_spell_unavailable),
        "No spell is available here.");
    copy_str(b->town_spell_at_cap, sizeof(b->town_spell_at_cap),
        "You have learned your maximum number of spells.");
    copy_str(b->town_spell_can_learn, sizeof(b->town_spell_can_learn),
        "You can learn %LEFT% more spell%S%.");
    copy_str(b->town_siege_already, sizeof(b->town_siege_already),
        "You have siege weapons!");
    copy_str(b->town_siege_purchased, sizeof(b->town_siege_purchased),
        "Siege weapons purchased.");

    // Spell effects .
    copy_str(b->spell_time_stop, sizeof(b->spell_time_stop),
        "Time has stopped for %STEPS% steps.");
    copy_str(b->spell_find_villain_no_contract,
             sizeof(b->spell_find_villain_no_contract),
        "You have no contract.");
    copy_str(b->spell_find_villain_success,
             sizeof(b->spell_find_villain_success),
        "You have found %CASTLE%!");
    copy_str(b->spell_find_villain_none,
             sizeof(b->spell_find_villain_none),
        "Your search reveals nothing.");
    copy_str(b->spell_bridge_prompt, sizeof(b->spell_bridge_prompt),
        "Build bridge in which\ndirection? Use arrows.");
    copy_str(b->spell_bridge_built, sizeof(b->spell_bridge_built),
        "Bridge constructed with\n%COUNT% tiles.");
    copy_str(b->spell_bridge_invalid, sizeof(b->spell_bridge_invalid),
        "Not a suitable location\nfor a bridge.");
    copy_str(b->spell_castle_gate_none, sizeof(b->spell_castle_gate_none),
        "You have not visited\nany castles yet.");
    copy_str(b->spell_castle_gate_choose, sizeof(b->spell_castle_gate_choose),
        "Which castle? Press A-K.");
    copy_str(b->spell_town_gate_none, sizeof(b->spell_town_gate_none),
        "You have not visited\nany towns yet.");
    copy_str(b->spell_town_gate_choose, sizeof(b->spell_town_gate_choose),
        "Which town? Press A-K.");
    copy_str(b->spell_instant_army_fizzle,
             sizeof(b->spell_instant_army_fizzle),
        "Spell fizzles.");
    copy_str(b->spell_instant_army_no_room,
             sizeof(b->spell_instant_army_no_room),
        "Your army has no room!");
    copy_str(b->spell_instant_army_success,
             sizeof(b->spell_instant_army_success),
        "%QTY% %TROOP%\nhave joined your army.");
    copy_str(b->spell_raise_control_success,
             sizeof(b->spell_raise_control_success),
        "Your leadership has\nincreased by %AMOUNT%.");
    copy_str(b->spell_gate_teleported, sizeof(b->spell_gate_teleported),
        "Teleported!");
    copy_str(b->spell_gate_invalid, sizeof(b->spell_gate_invalid),
        "Invalid selection.");

    // Foe encounters.
    copy_str(b->encounter_join_named, sizeof(b->encounter_join_named),
        "You encounter:\n  %LABEL% %TROOP%\n\n"
        "They offer to join your\narmy for %COST% gold.\n\n"
        "Press Enter to accept, Esc\nto refuse.");
    copy_str(b->encounter_join_numeric, sizeof(b->encounter_join_numeric),
        "You encounter:\n  %COUNT% %TROOP%\n\n"
        "They offer to join your\narmy for %COST% gold.\n\n"
        "Press Enter to accept, Esc\nto refuse.");
    copy_str(b->encounter_wanderers, sizeof(b->encounter_wanderers),
        "A band of wanderers passes\nby, looking grim.");
    copy_str(b->encounter_hostile_header,
             sizeof(b->encounter_hostile_header),
        "You encounter:\n");
    copy_str(b->encounter_hostile_unknown,
             sizeof(b->encounter_hostile_unknown),
        "  a hostile band\n");
    copy_str(b->encounter_hostile_count_named,
             sizeof(b->encounter_hostile_count_named),
        "  %LABEL% %TROOP%\n");
    copy_str(b->encounter_hostile_count_numeric,
             sizeof(b->encounter_hostile_count_numeric),
        "  %COUNT% %TROOP%\n");

    // Archmage Aurange alcove.
    copy_str(b->alcove_offer, sizeof(b->alcove_offer),
        "The venerable Archmage,\n"
        "Aurange, will teach you the\n"
        "secrets of spell casting for\n"
        "%COST% gold.\n\nAccept?");
    copy_str(b->alcove_already, sizeof(b->alcove_already),
        "The archmage nods.\nYou already know magic.");
    copy_str(b->alcove_taught, sizeof(b->alcove_taught),
        "Aurange teaches you the\n"
        "arcane arts. You now know\n"
        "the ways of magic.");
    copy_str(b->alcove_no_gold, sizeof(b->alcove_no_gold),
        "You have not enough gold!\n%COST% is required.\nBegone until you do!");
    copy_str(b->no_spell_banner, sizeof(b->no_spell_banner),
        "You have not been trained in\n"
        "the art of spellcasting yet.\n"
        "Visit the Archmage Aurange\n"
        "in Continentia at 11,19 for\n"
        "this ability.");
    copy_str(b->new_game_intro, sizeof(b->new_game_intro),
        "%NAME% the %CLASS%,\n"
        "A new game is being created.\n"
        "Please wait while I perform\n"
        "godlike actions to make this\n"
        "game playable.");

    // Dwelling recruitment.
    copy_str(b->dwelling_recruit_prompt,
             sizeof(b->dwelling_recruit_prompt),
        "%COUNT% %TROOP% are available\n"
        "Cost=%COST% each.  GP=%GOLD%\n"
        "You may recruit up to %CAP%.");
    copy_str(b->dwelling_none_this_week,
             sizeof(b->dwelling_none_this_week),
        "No troops are available here\nthis week.");
    copy_str(b->dwelling_empty, sizeof(b->dwelling_empty),
        "This dwelling is empty.");

    // Adventure-tile pickups.
    copy_str(b->telecave_teleport, sizeof(b->telecave_teleport),
        "You step into the cave and\n"
        "emerge elsewhere on this\ncontinent.");
    copy_str(b->telecave_inert, sizeof(b->telecave_inert),
        "This cave hums with magic,\nbut nothing happens.");
    copy_str(b->navmap_pickup, sizeof(b->navmap_pickup),
        "A navigation map of %ZONE%!");
    copy_str(b->crystal_ball_pickup, sizeof(b->crystal_ball_pickup),
        "You gaze into a crystal ball\nand see all of %ZONE%!");

    // End-of-week / defeat fallback.
    copy_str(b->astrology_header, sizeof(b->astrology_header),
        "Week #%WEEK%");
    copy_str(b->astrology_body, sizeof(b->astrology_body),
        "Astrologers proclaim:\n"
        "Week of the %TROOP%\n\n"
        "All %TROOP% dwellings are\n"
        "repopulated.         (space)");
    copy_str(b->temp_death, sizeof(b->temp_death),
        "After being disgraced on the\n"
        "field of battle, King\n"
        "Maximus summons you to his\n"
        "castle. After a lesson in\n"
        "tactics, he reluctantly re-\n"
        "issues your commission and\n"
        "sends you on your way.");

    // OpenKB ask_giveup (game.c:4519-4544). The body text is verbatim
    // from the DOS game; the header doubles as the OpenKB "Press ESC to
    // exit" hint shown at the top of the prompt frame.
    copy_str(b->combat_give_up_header, sizeof(b->combat_give_up_header),
        "Press ESC to exit");
    copy_str(b->combat_give_up_body, sizeof(b->combat_give_up_body),
        "Giving up will forfeit your\n"
        "armies and send you back to\n"
        "the King. Give up");

    // Pre-combat scout report.
    copy_str(b->combat_scouts_header, sizeof(b->combat_scouts_header),
        "Your scouts have sighted:\n");
    copy_str(b->combat_scouts_count, sizeof(b->combat_scouts_count),
        "  %COUNT% %TROOP%\n");
    copy_str(b->combat_scouts_small_band, sizeof(b->combat_scouts_small_band),
        "  (a small band)\n");
    copy_str(b->combat_header_siege, sizeof(b->combat_header_siege),
        "Siege");
    copy_str(b->combat_header_default, sizeof(b->combat_header_default),
        "Combat");

    // Signposts.
    copy_str(b->signpost_with_body, sizeof(b->signpost_with_body),
        "A sign reads:\n\n\"%TITLE%\n%BODY%\"");
    copy_str(b->signpost_title_only, sizeof(b->signpost_title_only),
        "A sign reads:\n\n\"%TITLE%\"");

    // End-of-week budget panel.
    copy_str(b->budget_header, sizeof(b->budget_header),
        "Week #%WEEK%             Budget");
    copy_str(b->budget_on_hand, sizeof(b->budget_on_hand), "On Hand");
    copy_str(b->budget_payment, sizeof(b->budget_payment), "Payment");
    copy_str(b->budget_boat,    sizeof(b->budget_boat),    "Boat   ");
    copy_str(b->budget_army,    sizeof(b->budget_army),    "Army   ");
    copy_str(b->budget_balance, sizeof(b->budget_balance), "Balance");

    // Status bar.
    copy_str(b->status_days_left, sizeof(b->status_days_left),
        " Options / Controls / Days Left:%DAYS% ");
    copy_str(b->status_time_stop, sizeof(b->status_time_stop),
        " Options / Controls / Time Stop:%STEPS% ");

    // Composite prompt bodies.
    copy_str(b->body_save_confirm, sizeof(b->body_save_confirm),
        "Press Control-Q to Quit or\nany other key to continue.");
    copy_str(b->body_search, sizeof(b->body_search),
        "It will take %DAYS% days to\nsearch this area.\nSearch?");
    copy_str(b->body_dismiss_pick, sizeof(b->body_dismiss_pick),
        "Dismiss which troop?");
    copy_str(b->body_dismiss_last, sizeof(b->body_dismiss_last),
        "If you dismiss your last\n"
        "army, you will be sent back\n"
        "to the King in disgrace.\n"
        "Dismiss last army?");
    copy_str(b->body_home_castle, sizeof(b->body_home_castle),
        "1. Recruit Soldiers\n2. Audience with the King");
    copy_str(b->body_own_castle, sizeof(b->body_own_castle),
        "Castle %NAME%:\n1. Garrison troops\n2. Remove troops");
    copy_str(b->body_garrison_row_named,
             sizeof(b->body_garrison_row_named),
        "%INDEX%. %TROOP% (%COUNT%)\n");
    copy_str(b->body_garrison_row_empty,
             sizeof(b->body_garrison_row_empty),
        "%INDEX%. Empty\n");
    copy_str(b->body_navigate_row, sizeof(b->body_navigate_row),
        "%INDEX%. %ZONE%\n");
    copy_str(b->body_no_continents, sizeof(b->body_no_continents),
        "No known continents from\nthis zone.");
    copy_str(b->body_must_be_sailing, sizeof(b->body_must_be_sailing),
        "You must be sailing to\nnavigate to another\ncontinent.");

    // Short banners reused by multiple flows.
    copy_str(b->cannot_garrison_last, sizeof(b->cannot_garrison_last),
        "You cannot garrison your\nlast army!");
    copy_str(b->no_troop_slots, sizeof(b->no_troop_slots),
        "No troop slots left!");
    copy_str(b->army_cannot_handle, sizeof(b->army_cannot_handle),
        "Your army cannot handle\nthat many troops.");
    copy_str(b->no_troops_to_garrison, sizeof(b->no_troops_to_garrison),
        "No troops to garrison.");
    copy_str(b->castle_garrison_empty, sizeof(b->castle_garrison_empty),
        "Castle garrison is empty.");
    copy_str(b->spell_unavailable, sizeof(b->spell_unavailable),
        "That spell is not available.");
    copy_str(b->spell_not_known, sizeof(b->spell_not_known),
        "You don't have that spell.");
    copy_str(b->spell_unknown, sizeof(b->spell_unknown),
        "Unknown spell.");
}

static void parse_banners(ResBanners *b, cJSON *obj) {
    load_banner_defaults(b);
    if (!cJSON_IsObject(obj)) return;
    // Each key overrides the matching default if present.
    #define SET_BANNER(field, key) do { \
        const char *s = json_str(obj, key, NULL); \
        if (s) copy_str(b->field, sizeof(b->field), s); \
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
// Defaults follow COMBAT-PLAN.md  verbatim; see also .
static void load_combat_log_defaults(ResCombatLog *cl) {
    memset(cl, 0, sizeof(*cl));
    copy_str(cl->melee_hit,        sizeof(cl->melee_hit),
             "%ATK% vs %TGT%, %COUNT% die");
    copy_str(cl->retaliate,        sizeof(cl->retaliate),
             "%TGT% retaliate, killing %COUNT%");
    copy_str(cl->ranged_hit,       sizeof(cl->ranged_hit),
             "%ATK% shoot %TGT% killing %COUNT%");
    copy_str(cl->ranged_no_effect, sizeof(cl->ranged_no_effect),
             "%ATK% shoot %TGT%");
    copy_str(cl->no_effect_msg,    sizeof(cl->no_effect_msg),
             "The spell seems to have no effect!");
    copy_str(cl->fly,              sizeof(cl->fly),    "%TROOP% fly");
    copy_str(cl->move,             sizeof(cl->move),   "%TROOP% move");
    copy_str(cl->wait,             sizeof(cl->wait),   "%TROOP% wait");
    copy_str(cl->pass,             sizeof(cl->pass),   "%TROOP% pass");
    copy_str(cl->frozen,           sizeof(cl->frozen), "%TROOP% are frozen");
    copy_str(cl->ooc,              sizeof(cl->ooc),
             "%TROOP% are out of control!");
    copy_str(cl->immune,           sizeof(cl->immune), "%TROOP% are immune");
    copy_str(cl->cloned,           sizeof(cl->cloned),
             "%COUNT% %TROOP% cloned");
    copy_str(cl->resurrected,      sizeof(cl->resurrected),
             "%COUNT% %TROOP% resurrected");
    copy_str(cl->teleported,       sizeof(cl->teleported), "Teleported");
    copy_str(cl->only_one_spell,   sizeof(cl->only_one_spell),
             "Only 1 spell per round!");
    copy_str(cl->no_spell_type,    sizeof(cl->no_spell_type),
             "No spells of that type");
    copy_str(cl->cannot_cast,      sizeof(cl->cannot_cast),
             "You cannot cast magic");
    copy_str(cl->cast_fireball,    sizeof(cl->cast_fireball), "Fireball!");
    copy_str(cl->cast_lightning,   sizeof(cl->cast_lightning), "Lightning!");
    copy_str(cl->cast_turn_undead, sizeof(cl->cast_turn_undead),
             "Turn Undead!");
    copy_str(cl->select_clone,     sizeof(cl->select_clone),
             "Select your army to Clone");
    copy_str(cl->select_freeze,    sizeof(cl->select_freeze),
             "Select enemy army to Freeze");
    copy_str(cl->select_resurrect, sizeof(cl->select_resurrect),
             "Select army to Resurrect");
    copy_str(cl->select_damage,    sizeof(cl->select_damage),
             "Select enemy army to %SPELL%");
    copy_str(cl->select_teleport,  sizeof(cl->select_teleport),
             "Select army to Teleport");
    copy_str(cl->select_dest,      sizeof(cl->select_dest),
             "Select new location");
    copy_str(cl->cant_shoot,       sizeof(cl->cant_shoot), "Can't Shoot");
    copy_str(cl->no_ammo,          sizeof(cl->no_ammo),    "No ammo");
    copy_str(cl->cant_fly,         sizeof(cl->cant_fly),   "Can't Fly");
    copy_str(cl->give_up_prompt,   sizeof(cl->give_up_prompt),
             "Giving up will forfeit your\n"
             "armies and send you back to\n"
             "the King. Give up (y/n)?");
    copy_str(cl->exit_hint,        sizeof(cl->exit_hint), "Press 'ESC' to exit");
}

static void parse_combat_log(ResCombatLog *cl, cJSON *obj) {
    load_combat_log_defaults(cl);
    if (!cJSON_IsObject(obj)) return;
    #define SET_CL(field, key) do { \
        const char *s = json_str(obj, key, NULL); \
        if (s) copy_str(cl->field, sizeof(cl->field), s); \
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

static void load_ui_defaults(ResUI *ui) {
    memset(ui, 0, sizeof(*ui));
    copy_str(ui->press_esc_to_exit, sizeof(ui->press_esc_to_exit),
             "Press 'ESC' to exit");
    copy_str(ui->quit_to_dos_prompt, sizeof(ui->quit_to_dos_prompt),
             " Quit without saving (y/n) ");
    copy_str(ui->out_of_control, sizeof(ui->out_of_control),
             "OUT OF CONTROL");
    copy_str(ui->worldmap_hint_your_map, sizeof(ui->worldmap_hint_your_map),
             "'ESC' to exit / 'SPC' your map");
    copy_str(ui->worldmap_hint_whole_map, sizeof(ui->worldmap_hint_whole_map),
             "'ESC' to exit / 'SPC' whole map");

    copy_str(ui->menu_root_title,    sizeof(ui->menu_root_title),    "Game Menu");
    copy_str(ui->menu_views_title,   sizeof(ui->menu_views_title),   "Views");
    copy_str(ui->menu_options_title, sizeof(ui->menu_options_title), "Options");
    copy_str(ui->menu_back,      sizeof(ui->menu_back),      "Back");
    copy_str(ui->menu_exit,      sizeof(ui->menu_exit),      "Exit");
    copy_str(ui->menu_save,      sizeof(ui->menu_save),      "Save");
    copy_str(ui->menu_load,      sizeof(ui->menu_load),      "Load");
    copy_str(ui->menu_new_game,  sizeof(ui->menu_new_game),  "New Game");
    copy_str(ui->menu_views,     sizeof(ui->menu_views),     "Views");
    copy_str(ui->menu_options,   sizeof(ui->menu_options),   "Options");
    copy_str(ui->menu_army,      sizeof(ui->menu_army),      "Army");
    copy_str(ui->menu_spells,    sizeof(ui->menu_spells),    "Spells");
    copy_str(ui->menu_character, sizeof(ui->menu_character), "Character");
    copy_str(ui->menu_contract,  sizeof(ui->menu_contract),  "Contract");
    copy_str(ui->menu_puzzle,    sizeof(ui->menu_puzzle),    "Puzzle");
    copy_str(ui->menu_view_map,  sizeof(ui->menu_view_map),  "View Map");

    copy_str(ui->stat_leadership,         sizeof(ui->stat_leadership),         "Leadership");
    copy_str(ui->stat_commission,         sizeof(ui->stat_commission),         "Commission/Week");
    copy_str(ui->stat_gold,               sizeof(ui->stat_gold),               "Gold");
    copy_str(ui->stat_spell_power,        sizeof(ui->stat_spell_power),        "Spell power");
    copy_str(ui->stat_max_spells,         sizeof(ui->stat_max_spells),         "Max # of spells");
    copy_str(ui->stat_villains_caught,    sizeof(ui->stat_villains_caught),    "Villains caught");
    copy_str(ui->stat_artifacts_found,    sizeof(ui->stat_artifacts_found),    "Artifacts found");
    copy_str(ui->stat_castles_garrisoned, sizeof(ui->stat_castles_garrisoned), "Castles garrisoned");
    copy_str(ui->stat_followers_killed,   sizeof(ui->stat_followers_killed),   "Followers killed");
    copy_str(ui->stat_current_score,      sizeof(ui->stat_current_score),      "Current score");

    copy_str(ui->army_skill,      sizeof(ui->army_skill),      "SL:");
    copy_str(ui->army_move,       sizeof(ui->army_move),       "MV:");
    copy_str(ui->army_morale,     sizeof(ui->army_morale),     "Morale:");
    copy_str(ui->army_hit_points, sizeof(ui->army_hit_points), "HitPts:");
    copy_str(ui->army_damage,     sizeof(ui->army_damage),     "Damage:");
    copy_str(ui->army_g_cost,     sizeof(ui->army_g_cost),     "G-Cost:");

    copy_str(ui->morale_normal, sizeof(ui->morale_normal), "Normal");
    copy_str(ui->morale_low,    sizeof(ui->morale_low),    "Low");
    copy_str(ui->morale_high,   sizeof(ui->morale_high),   "High");

    // Count buckets — defaults match the historical openbounty inline ladders.
    ui->count_buckets_army_view_n = 3;
    ui->count_buckets_army_view[0].threshold = 5;
    copy_str(ui->count_buckets_army_view[0].label,
             sizeof(ui->count_buckets_army_view[0].label), "Few");
    ui->count_buckets_army_view[1].threshold = 20;
    copy_str(ui->count_buckets_army_view[1].label,
             sizeof(ui->count_buckets_army_view[1].label), "Some");
    ui->count_buckets_army_view[2].threshold = 0x7FFFFFFF;
    copy_str(ui->count_buckets_army_view[2].label,
             sizeof(ui->count_buckets_army_view[2].label), "Lots");

    ui->count_buckets_instant_army_n = 4;
    ui->count_buckets_instant_army[0].threshold = 1;
    copy_str(ui->count_buckets_instant_army[0].label,
             sizeof(ui->count_buckets_instant_army[0].label), "One");
    ui->count_buckets_instant_army[1].threshold = 2;
    copy_str(ui->count_buckets_instant_army[1].label,
             sizeof(ui->count_buckets_instant_army[1].label), "A few");
    ui->count_buckets_instant_army[2].threshold = 9;
    copy_str(ui->count_buckets_instant_army[2].label,
             sizeof(ui->count_buckets_instant_army[2].label), "Several");
    ui->count_buckets_instant_army[3].threshold = 0x7FFFFFFF;
    copy_str(ui->count_buckets_instant_army[3].label,
             sizeof(ui->count_buckets_instant_army[3].label), "Many");

    // Difficulty labels (indexed by Difficulty 0..3).
    static const struct { const char *l; const char *m; } diff_def[4] = {
        { "Easy",        "x.5" },
        { "Normal",      " x1" },
        { "Hard",        " x2" },
        { "Impossible?", " x4" },
    };
    for (int i = 0; i < 4; i++) {
        copy_str(ui->difficulty[i].label, sizeof(ui->difficulty[i].label),
                 diff_def[i].l);
        copy_str(ui->difficulty[i].score_mult,
                 sizeof(ui->difficulty[i].score_mult), diff_def[i].m);
    }

    // Default keybinds -- overlay.c options panel order.
    static const struct { const char *k; const char *l; } keybind_defaults[] = {
        { "Dn",   "Move Down"      }, { "Lf",   "Move Left"      },
        { "Rt",   "Move Right"     }, { "Up",   "Move Up"        },
        { "End",  "Down Left"      }, { "PgDn", "Down Right"     },
        { "Hm",   "Up Left"        }, { "PgUp", "Up Right"       },
        { "A",    "View Army"      }, { "C",    "Controls"       },
        { "F",    "Fly"            }, { "L",    "Land"           },
        { "I",    "Contract Info"  }, { "M",    "Auto-mapping"   },
        { "P",    "Puzzle Solve"   }, { "S",    "Search Area"    },
        { "U",    "Use Magic"      }, { "V",    "View Character" },
        { "W",    "Wait End Week"  }, { "Q",    "Quit and Save"  },
    };
    int n = (int)(sizeof(keybind_defaults) / sizeof(keybind_defaults[0]));
    if (n > RES_MAX_KEYBINDS) n = RES_MAX_KEYBINDS;
    ui->keybind_count = n;
    for (int i = 0; i < n; i++) {
        copy_str(ui->keybinds[i].key,   sizeof(ui->keybinds[i].key),   keybind_defaults[i].k);
        copy_str(ui->keybinds[i].label, sizeof(ui->keybinds[i].label), keybind_defaults[i].l);
    }

    copy_str(ui->startup_controls_hint, sizeof(ui->startup_controls_hint),
             "UP/DN move  ENTER pick  ESC quit");
    copy_str(ui->startup_class_select_hint,
             sizeof(ui->startup_class_select_hint),
             "Select Char A-D or L-Load saved game");
    copy_str(ui->startup_class_picker_missing,
             sizeof(ui->startup_class_picker_missing),
             "class picker asset missing");
    copy_str(ui->startup_save_picker_title,
             sizeof(ui->startup_save_picker_title),
             " Select game:");
    copy_str(ui->startup_save_picker_empty,
             sizeof(ui->startup_save_picker_empty),
             "(empty)");
    copy_str(ui->startup_save_picker_new_game,
             sizeof(ui->startup_save_picker_new_game),
             "New game");
    copy_str(ui->startup_new_game_table_header,
             sizeof(ui->startup_new_game_table_header),
             "   Difficulty   Days  Score");
    copy_str(ui->startup_new_game_select_hint,
             sizeof(ui->startup_new_game_select_hint),
             "^v to select   Ent to Accept");

    copy_str(ui->controls_title, sizeof(ui->controls_title), " Controls ");
    copy_str(ui->controls_on,    sizeof(ui->controls_on),    "On");
    copy_str(ui->controls_off,   sizeof(ui->controls_off),   "Off");

    copy_str(ui->prompt_text_hint, sizeof(ui->prompt_text_hint),
             "(Enter to confirm / ESC cancel)");
    copy_str(ui->prompt_numeric_range_hint,
             sizeof(ui->prompt_numeric_range_hint),
             "(1-%COUNT% or ESC)");
    copy_str(ui->prompt_yes_no_hint,    sizeof(ui->prompt_yes_no_hint),    "(y/n)?");
    copy_str(ui->prompt_numeric_5_hint, sizeof(ui->prompt_numeric_5_hint), "(1-5 or ESC)");

    // Dialog / prompt titles.
    copy_str(ui->dt_treasure,        sizeof(ui->dt_treasure),        "Treasure");
    copy_str(ui->dt_teleport_cave,   sizeof(ui->dt_teleport_cave),   "Teleport Cave");
    copy_str(ui->dt_crystal_ball,    sizeof(ui->dt_crystal_ball),    "Crystal Ball");
    copy_str(ui->dt_foes,            sizeof(ui->dt_foes),            "Foes!");
    copy_str(ui->dt_alcove_offer,    sizeof(ui->dt_alcove_offer),    "Archmage Aurange");
    copy_str(ui->dt_alcove_result,   sizeof(ui->dt_alcove_result),   "Aurange");
    copy_str(ui->dt_castle_default,  sizeof(ui->dt_castle_default),  "Castle");
    copy_str(ui->dt_own_castle,      sizeof(ui->dt_own_castle),      "Your castle");
    copy_str(ui->dt_search,          sizeof(ui->dt_search),          "Search...");
    copy_str(ui->dt_dismiss_army,    sizeof(ui->dt_dismiss_army),    "Dismiss Army");
    copy_str(ui->dt_dismiss_last,    sizeof(ui->dt_dismiss_last),    "Dismiss last army");
    copy_str(ui->dt_navigate,        sizeof(ui->dt_navigate),        "Go to which continent?");
    copy_str(ui->dt_garrison_pick,   sizeof(ui->dt_garrison_pick),   "Garrison which troop?");
    copy_str(ui->dt_remove_pick,     sizeof(ui->dt_remove_pick),     "Remove which troop?");
    copy_str(ui->dt_save_confirm,    sizeof(ui->dt_save_confirm),    "Your game has been saved.");
    copy_str(ui->dt_lose_fallback,   sizeof(ui->dt_lose_fallback),   "Sorry.");
    copy_str(ui->dt_win_fallback,    sizeof(ui->dt_win_fallback),    "Congratulations!");

    copy_str(ui->empty_slot, sizeof(ui->empty_slot), "Empty");

    copy_str(ui->combat_spells_title,      sizeof(ui->combat_spells_title),      "Spells");
    copy_str(ui->combat_spells_col_combat, sizeof(ui->combat_spells_col_combat), "Combat");
    copy_str(ui->combat_spells_prompt,     sizeof(ui->combat_spells_prompt),     "Cast which (A-G)?");

    copy_str(ui->dwelling_kind_plains,       sizeof(ui->dwelling_kind_plains),       "Plains");
    copy_str(ui->dwelling_kind_forest,       sizeof(ui->dwelling_kind_forest),       "Forest");
    copy_str(ui->dwelling_kind_hill,         sizeof(ui->dwelling_kind_hill),         "Hill");
    copy_str(ui->dwelling_kind_dungeon,      sizeof(ui->dwelling_kind_dungeon),      "Dungeon");
    copy_str(ui->dwelling_recruit_how_many,  sizeof(ui->dwelling_recruit_how_many),  "Recruit how many");

    copy_str(ui->recruit_soldiers_title,    sizeof(ui->recruit_soldiers_title),    "Recruit Soldiers");
    copy_str(ui->recruit_soldiers_how_many, sizeof(ui->recruit_soldiers_how_many), "How Many");

    copy_str(ui->own_castle_mode_garrison, sizeof(ui->own_castle_mode_garrison), "Garrison troops (Space=Remove)");
    copy_str(ui->own_castle_mode_remove,   sizeof(ui->own_castle_mode_remove),   "Remove troops (Space=Garrison)");

    // Save/load + game-state toasts.
    copy_str(ui->toast_save_cancelled, sizeof(ui->toast_save_cancelled),
             "Save cancelled.");
    copy_str(ui->toast_save_ok,        sizeof(ui->toast_save_ok),        "Saved.");
    copy_str(ui->toast_save_failed,    sizeof(ui->toast_save_failed),
             "Save failed: %REASON%");
    copy_str(ui->toast_load_cancelled, sizeof(ui->toast_load_cancelled),
             "Load cancelled.");
    copy_str(ui->toast_load_ok,        sizeof(ui->toast_load_ok),        "Loaded.");
    copy_str(ui->toast_load_failed,    sizeof(ui->toast_load_failed),
             "Load failed: %REASON%");
    copy_str(ui->toast_new_game,       sizeof(ui->toast_new_game),
             "New game started.");

    // Contract view labels.
    copy_str(ui->cv_title_no_contract, sizeof(ui->cv_title_no_contract),
             "You have no Contract!");
    copy_str(ui->cv_label_name,        sizeof(ui->cv_label_name),
             "Name: %VALUE%");
    copy_str(ui->cv_label_alias,       sizeof(ui->cv_label_alias),
             "Alias: %VALUE%");
    copy_str(ui->cv_label_reward,      sizeof(ui->cv_label_reward),
             "Reward: %VALUE% gold");
    copy_str(ui->cv_label_last_seen,   sizeof(ui->cv_label_last_seen),
             "Last Seen: %VALUE%");
    copy_str(ui->cv_label_castle,      sizeof(ui->cv_label_castle),
             "Castle: %VALUE%");
    copy_str(ui->cv_alias_none,        sizeof(ui->cv_alias_none),     "None");
    copy_str(ui->cv_castle_unknown,    sizeof(ui->cv_castle_unknown), "Unknown");
    copy_str(ui->cv_features_header,   sizeof(ui->cv_features_header),
             "Distinguishing Features:");
    copy_str(ui->cv_crimes_header,     sizeof(ui->cv_crimes_header),  "Crimes:");

    // Spells view labels.
    copy_str(ui->sv_title,         sizeof(ui->sv_title),         "Spells");
    copy_str(ui->sv_combat_col,    sizeof(ui->sv_combat_col),    "Combat");
    copy_str(ui->sv_adventure_col, sizeof(ui->sv_adventure_col), "Adventuring");
}

// Override one buffer field if the JSON object has a string at `key`.
#define UI_SET(field, key) do { \
    const char *s = json_str(obj, key, NULL); \
    if (s) copy_str(ui->field, sizeof(ui->field), s); \
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
    load_ui_defaults(ui);
    if (!cJSON_IsObject(root_strings)) return;

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
        UI_SET(recruit_soldiers_title,    "recruit_soldiers_title");
        UI_SET(recruit_soldiers_how_many, "recruit_soldiers_how_many");
        UI_SET(own_castle_mode_garrison, "own_castle_mode_garrison");
        UI_SET(own_castle_mode_remove,   "own_castle_mode_remove");
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
    }
}

#undef UI_SET

static void parse_strings(Resources *res, cJSON *obj) {
    // Banners + UI labels load defaults even when strings/* is missing, so
    // every dialog and HUD draw call has a body to render.
    parse_banners(&res->banners,
                  cJSON_IsObject(obj) ? cJSON_GetObjectItem(obj, "banners") : NULL);
    parse_combat_log(&res->combat_log,
                     cJSON_IsObject(obj) ? cJSON_GetObjectItem(obj, "combat_log") : NULL);
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

bool resources_load(Resources *res, const char *manifest_path) {
    memset(res, 0, sizeof(*res));

    // The manifest path is always pack-relative now (typically just
    // "game.json"). The active pack on the global pack stack
    // (src/pack.c) does the actual byte lookup.
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
                fprintf(stderr, "resources: failed to open pack at %s\n", dir);
                return false;
            }
            pack_stack_push(p);
        }
        manifest_rel = slash + 1;
    }

    char *text = slurp(manifest_rel);
    if (!text) {
        fprintf(stderr, "resources: failed to read %s\n", manifest_rel);
        return false;
    }
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "resources: failed to parse %s\n", manifest_rel);
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

    // Chest curves and value ranges —  defaults so
    // omitting the JSON block still produces parity-correct rolls.
    {
        ResChest *ch = &res->economy.chest;
        static const int def_chance_gold[4]       = { 0x3d, 0x42, 0x4c, 0x47 };
        static const int def_chance_commission[4] = { 0x51, 0x56, 0x56, 0x51 };
        static const int def_chance_spell_power[4]= { 0x56, 0x5c, 0x5d, 0x5b };
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
        }
    }

    // Color tables (game.json "colors" block). Defaults match openbounty
    // 256-color VGA palette mappings used historically in the source.
    {
        ResColors *col = &res->colors;
        // Minimap defaults — EGA palette indices applied to
        // openbounty' VGA palette (these RGB values are what PAL_CLR(...)
        // resolves to at runtime).
        col->minimap_grass    = 0xFF00AA00u; // DGREEN
        col->minimap_forest   = 0xFF55FF55u; // GREEN
        col->minimap_mountain = 0xFFAA5500u; // BROWN
        col->minimap_water    = 0xFF5555FFu; // BLUE
        col->minimap_desert   = 0xFFFFFF55u; // YELLOW
        col->minimap_fog      = 0xFF000000u; // BLACK
        // Difficulty bar — EGA RGBs (chrome.c historical comment).
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

    cJSON *jw = cJSON_GetObjectItem(root, "world");
    copy_str(res->world.starting_zone, sizeof(res->world.starting_zone),
             json_str(jw, "starting_zone", "continentia"));
    copy_str(res->world.zone_noun, sizeof(res->world.zone_noun),
             json_str(jw, "zone_noun", "zone"));
    copy_str(res->world.zone_noun_plural, sizeof(res->world.zone_noun_plural),
             json_str(jw, "zone_noun_plural", "zones"));
    res->world.max_army_slots = json_int(jw, "max_army_slots", 5);
    res->world.fog_sight      = json_int(jw, "fog_sight",      3);
    copy_str(res->world.default_name, sizeof(res->world.default_name),
             json_str(jw, "default_name", "Hero"));
    cJSON *jdo = cJSON_GetObjectItem(jw, "default_options");
    // Fallback defaults: delay, sounds, walk_beep, anim, army_size, cga, music, volume.
    static const int default_options_fallback[8] = { 4, 1, 1, 1, 1, 1, 0, 5 };
    for (int i = 0; i < 8; i++) res->world.default_options[i] = default_options_fallback[i];
    if (cJSON_IsArray(jdo)) {
        int i = 0;
        cJSON *v;
        cJSON_ArrayForEach(v, jdo) {
            if (i >= 8) break;
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
    parse_sprites(res,     cJSON_GetObjectItem(root, "sprites"));
    parse_audio(res,       cJSON_GetObjectItem(root, "audio"));
    parse_combat(res,      cJSON_GetObjectItem(root, "combat"));
    parse_controls(res,    cJSON_GetObjectItem(root, "controls"));
    parse_strings(res,     cJSON_GetObjectItem(root, "strings"));
    parse_ending(res,      cJSON_GetObjectItem(root, "ending"));
    parse_credits(res,     cJSON_GetObjectItem(root, "credits"));

    cJSON_Delete(root);

    g_resources = res;    // publish to table lookups
    return true;
}

void resources_free(Resources *res) {
    if (g_resources == res) g_resources = NULL;
    // Nothing heap-owned; catalogs and per-zone objects are inline.
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

const ResZone *resources_zone_by_id(const Resources *r, const char *id) {
    if (!r || !id) return NULL;
    for (int i = 0; i < r->zone_count; i++) {
        if (strcmp(r->zones[i].id, id) == 0) return &r->zones[i];
    }
    return NULL;
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
            // No matching token — emit the '%' verbatim and continue.
            out[o++] = *src++;
            continue;
        }
        out[o++] = *src++;
    }
    out[o] = '\0';
}
