// Zone objects (GB-210..GB-215): the placements that live in game.json rather
// than in the .dat.
//
// Objects are edited ON THE MAP, so this layer's job is only to expose them as
// a flat, position-carrying list and write changes straight back into the DOM.
// It never keeps a parallel copy: the DOM stays the single source of truth, so
// a raw-JSON edit and a drag on the canvas cannot disagree.
//
// Note the two different homes. Towns and castles are top-level catalogs with a
// `zone` field; chests, signs, dwellings and armies are arrays inside the zone
// object. Both appear here as one list, because to the author placing them they
// are the same kind of act.

#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const GB_OBJ_NAME[GB_OBJ_KINDS] = {
    "town", "castle", "chest", "sign", "dwelling", "army",
    "hero_spawn", "home_spawn", "alcove"
};

static cJSON *zone_by_id(cJSON *doc, const char *zone_id) {
    cJSON *zones = cJSON_GetObjectItem(doc, "zones");
    if (!cJSON_IsArray(zones)) return NULL;
    cJSON *z = NULL;
    cJSON_ArrayForEach(z, zones) {
        cJSON *id = cJSON_GetObjectItem(z, "id");
        if (cJSON_IsString(id) && strcmp(id->valuestring, zone_id) == 0) return z;
    }
    return NULL;
}

static int int_of(cJSON *o, const char *k, int fallback) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : fallback;
}

static const char *str_of(cJSON *o, const char *k, const char *fallback) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) ? v->valuestring : fallback;
}

// A castle's map position is its gate, not its `x`/`y`, so read position
// through one place rather than at every call site.
static int sub_int(cJSON *o, const char *sub, const char *k, int fb) {
    cJSON *s = cJSON_GetObjectItem(o, sub);
    return cJSON_IsObject(s) ? int_of(s, k, fb) : fb;
}

static void object_position(GbObjKind kind, cJSON *node, int *x, int *y) {
    if (kind == GB_OBJ_CASTLE) {
        // gate is a nested object in the manifest; the flat form is accepted
        // as a fallback because a hand-written pack may use either.
        *x = sub_int(node, "gate", "x", int_of(node, "gate_x", int_of(node, "x", 0)));
        *y = sub_int(node, "gate", "y", int_of(node, "gate_y", int_of(node, "y", 0)));
    } else {
        *x = int_of(node, "x", 0);
        *y = int_of(node, "y", 0);
    }
}

static void add(GbObjectList *L, GbObjKind kind, cJSON *node, cJSON *owner,
                int index, const char *label) {
    if (L->count >= GB_MAX_OBJECTS) return;
    GbObject *o = &L->item[L->count];
    memset(o, 0, sizeof *o);
    o->kind  = kind;
    o->node  = node;
    o->owner = owner;
    o->index = index;
    snprintf(o->label, sizeof o->label, "%s", label ? label : "");
    object_position(kind, node, &o->x, &o->y);
    L->count++;
}

void gb_objects_collect(GbObjectList *L, cJSON *doc, const char *zone_id) {
    memset(L, 0, sizeof *L);
    if (!doc || !zone_id) return;

    struct { const char *key; GbObjKind kind; } cat[] = {
        { "towns",   GB_OBJ_TOWN   },
        { "castles", GB_OBJ_CASTLE },
    };
    for (unsigned c = 0; c < sizeof cat / sizeof *cat; c++) {
        cJSON *arr = cJSON_GetObjectItem(doc, cat[c].key);
        if (!cJSON_IsArray(arr)) continue;
        int i = 0;
        cJSON *e = NULL;
        cJSON_ArrayForEach(e, arr) {
            if (strcmp(str_of(e, "zone", ""), zone_id) == 0)
                add(L, cat[c].kind, e, arr, i,
                    str_of(e, "name", str_of(e, "id", "?")));
            i++;
        }
    }

    cJSON *z = zone_by_id(doc, zone_id);
    if (!z) return;
    struct { const char *key; GbObjKind kind; } inzone[] = {
        { "chests",    GB_OBJ_CHEST    },
        { "signs",     GB_OBJ_SIGN     },
        { "dwellings", GB_OBJ_DWELLING },
        { "armies",    GB_OBJ_ARMY     },
    };
    for (unsigned c = 0; c < sizeof inzone / sizeof *inzone; c++) {
        cJSON *arr = cJSON_GetObjectItem(z, inzone[c].key);
        if (!cJSON_IsArray(arr)) continue;
        int i = 0;
        cJSON *e = NULL;
        cJSON_ArrayForEach(e, arr) {
            const char *lbl = str_of(e, "id",
                              str_of(e, "title",
                              str_of(e, "troop", GB_OBJ_NAME[inzone[c].kind])));
            add(L, inzone[c].kind, e, arr, i, lbl);
            i++;
        }
    }

    // The zone's singular points. Not array members, so they carry a NULL
    // owner and cannot be deleted -- only moved.
    struct { const char *key; GbObjKind kind; } pt[] = {
        { "hero_spawn",   GB_OBJ_HERO_SPAWN },
        { "home_spawn",   GB_OBJ_HOME_SPAWN },
        { "magic_alcove", GB_OBJ_ALCOVE     },
    };
    for (unsigned c = 0; c < sizeof pt / sizeof *pt; c++) {
        cJSON *o = cJSON_GetObjectItem(z, pt[c].key);
        if (cJSON_IsObject(o)) add(L, pt[c].kind, o, NULL, -1, pt[c].key);
    }
}

static void set_num(cJSON *node, const char *key, int v, bool only_if_present) {
    if (only_if_present && !cJSON_GetObjectItem(node, key)) return;
    if (cJSON_GetObjectItem(node, key))
        cJSON_ReplaceItemInObject(node, key, cJSON_CreateNumber(v));
    else
        cJSON_AddNumberToObject(node, key, v);
}

bool gb_object_move(GbObject *o, int x, int y) {
    if (!o || !o->node) return false;
    if (o->kind == GB_OBJ_CASTLE) {
        // Gate and catalog coordinates are two records of one tile and move
        // together, or the engine stamps the footprint in the wrong place.
        cJSON *gate = cJSON_GetObjectItem(o->node, "gate");
        if (cJSON_IsObject(gate)) {
            set_num(gate, "x", x, false);
            set_num(gate, "y", y, false);
        } else {
            set_num(o->node, "gate_x", x, false);
            set_num(o->node, "gate_y", y, false);
        }
        set_num(o->node, "x", x, true);
        set_num(o->node, "y", y, true);
    } else {
        set_num(o->node, "x", x, false);
        set_num(o->node, "y", y, false);
    }
    o->x = x;
    o->y = y;
    return true;
}

bool gb_object_delete(GbObject *o) {
    if (!o || !o->owner || o->index < 0) return false;   // points are permanent
    cJSON_DeleteItemFromArray(o->owner, o->index);
    o->node = NULL;
    return true;
}

cJSON *gb_object_create(cJSON *doc, const char *zone_id, GbObjKind kind,
                        int x, int y) {
    cJSON *z = zone_by_id(doc, zone_id);
    const char *key = NULL;
    switch (kind) {
        case GB_OBJ_CHEST:    key = "chests";    break;
        case GB_OBJ_SIGN:     key = "signs";     break;
        case GB_OBJ_DWELLING: key = "dwellings"; break;
        case GB_OBJ_ARMY:     key = "armies";    break;
        case GB_OBJ_TOWN:     key = "towns";     break;
        case GB_OBJ_CASTLE:   key = "castles";   break;
        default: return NULL;                     // spawns already exist
    }
    cJSON *arr;
    if (kind == GB_OBJ_TOWN || kind == GB_OBJ_CASTLE) {
        arr = cJSON_GetObjectItem(doc, key);
        if (!arr) arr = cJSON_AddArrayToObject(doc, key);
    } else {
        if (!z) return NULL;
        arr = cJSON_GetObjectItem(z, key);
        if (!arr) arr = cJSON_AddArrayToObject(z, key);
    }
    if (!cJSON_IsArray(arr)) return NULL;

    cJSON *node = cJSON_CreateObject();
    // A new object needs a unique id, or nothing else can reference it.
    char id[64];
    snprintf(id, sizeof id, "%s_%d", GB_OBJ_NAME[kind],
             cJSON_GetArraySize(arr) + 1);
    cJSON_AddStringToObject(node, "id", id);
    if (kind == GB_OBJ_TOWN || kind == GB_OBJ_CASTLE) {
        cJSON_AddStringToObject(node, "name", id);
        cJSON_AddStringToObject(node, "zone", zone_id);
    }
    if (kind == GB_OBJ_CASTLE) {
        cJSON_AddNumberToObject(node, "gate_x", x);
        cJSON_AddNumberToObject(node, "gate_y", y);
    }
    cJSON_AddNumberToObject(node, "x", x);
    cJSON_AddNumberToObject(node, "y", y);
    if (kind == GB_OBJ_SIGN) {
        cJSON_AddStringToObject(node, "title", "Sign");
        cJSON_AddStringToObject(node, "body", "");
    }
    cJSON_AddItemToArray(arr, node);
    return node;
}

// A castle occupies 3x2: the walkable gate plus five blocking wall tiles
// (REQ-228). Placement must know that, or a castle overlaps terrain the
// player then cannot walk through.
void gb_castle_footprint(int gate_x, int gate_y, int *x0, int *y0,
                         int *w, int *h) {
    *x0 = gate_x - 1;
    *y0 = gate_y - 1;
    *w  = 3;
    *h  = 2;
}

int gb_zone_count(cJSON *doc) {
    cJSON *zones = cJSON_GetObjectItem(doc, "zones");
    return cJSON_IsArray(zones) ? cJSON_GetArraySize(zones) : 0;
}

const char *gb_zone_id_at(cJSON *doc, int index) {
    cJSON *zones = cJSON_GetObjectItem(doc, "zones");
    cJSON *z = cJSON_GetArrayItem(zones, index);
    return z ? str_of(z, "id", NULL) : NULL;
}
