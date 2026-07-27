// Shared undo/redo history (GB-102).
//
// Command-based, not snapshot-based. A pack is a JSON DOM plus up to eight map
// grids plus loaded images; copying all of that per edit is far too expensive
// to do on every brush stroke. Instead each entry records what changed and how
// to put it back, and only bulk operations that touch the whole grid (despeckle,
// resize, fill) store a grid snapshot.
//
// One history spans every mode, because the user thinks in terms of "what did
// I just do", not "what did I just do in this tab".

#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    UNDO_JSON,      // a subtree of the DOM was replaced
    UNDO_TILES,     // a rectangle of one zone's terrain was overwritten
    UNDO_GRID,      // a whole grid was replaced (bulk ops)
} GbUndoKind;

typedef struct {
    GbUndoKind kind;
    char       label[64];

    // UNDO_JSON: the parent object/array and the key or index that changed.
    // Storing detached cJSON nodes rather than printed text keeps this cheap
    // and exact -- no reparse, no formatting drift.
    cJSON     *json_parent;
    char       json_key[64];
    int        json_index;      // -1 for object members
    cJSON     *json_before;     // owned by the history
    cJSON     *json_after;      // owned by the history

    // UNDO_TILES / UNDO_GRID
    int        zone;
    int        x, y, w, h;
    MapCell   *before;          // w*h cells, owned
    MapCell   *after;           // w*h cells, owned
} GbUndoEntry;

struct GbUndo {
    GbUndoEntry entry[GB_UNDO_MAX];
    int         count;          // entries present
    int         at;             // next slot to write; entries above are redoable
};

GbUndo *gb_undo_create(void) { return calloc(1, sizeof(GbUndo)); }

static void entry_free(GbUndoEntry *e) {
    if (e->json_before) cJSON_Delete(e->json_before);
    if (e->json_after)  cJSON_Delete(e->json_after);
    free(e->before);
    free(e->after);
    memset(e, 0, sizeof *e);
}

void gb_undo_destroy(GbUndo *u) {
    if (!u) return;
    for (int i = 0; i < u->count; i++) entry_free(&u->entry[i]);
    free(u);
}

void gb_undo_clear(GbUndo *u) {
    if (!u) return;
    for (int i = 0; i < u->count; i++) entry_free(&u->entry[i]);
    u->count = 0;
    u->at = 0;
}

// Drop anything above the cursor: once you edit after undoing, the old redo
// branch is gone. Then make room at the bottom if the ring is full.
static GbUndoEntry *push_slot(GbUndo *u, GbUndoKind kind, const char *label) {
    for (int i = u->at; i < u->count; i++) entry_free(&u->entry[i]);
    u->count = u->at;

    if (u->count == GB_UNDO_MAX) {
        entry_free(&u->entry[0]);
        memmove(&u->entry[0], &u->entry[1],
                sizeof(GbUndoEntry) * (GB_UNDO_MAX - 1));
        u->count--;
        u->at--;
    }
    GbUndoEntry *e = &u->entry[u->count++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    snprintf(e->label, sizeof e->label, "%s", label ? label : "edit");
    u->at = u->count;
    return e;
}

void gb_undo_push_json(GbUndo *u, const char *label, cJSON *parent,
                       const char *key, int index,
                       const cJSON *before, const cJSON *after) {
    if (!u || !parent) return;
    GbUndoEntry *e = push_slot(u, UNDO_JSON, label);
    e->json_parent = parent;
    e->json_index  = index;
    snprintf(e->json_key, sizeof e->json_key, "%s", key ? key : "");
    e->json_before = before ? cJSON_Duplicate(before, 1) : NULL;
    e->json_after  = after  ? cJSON_Duplicate(after, 1)  : NULL;
}

void gb_undo_push_tiles(GbUndo *u, const char *label, int zone,
                        int x, int y, int w, int h,
                        const MapCell *before, const MapCell *after) {
    if (!u || w <= 0 || h <= 0) return;
    GbUndoEntry *e = push_slot(u, UNDO_TILES, label);
    e->zone = zone; e->x = x; e->y = y; e->w = w; e->h = h;
    size_t n = (size_t)w * (size_t)h * sizeof(MapCell);
    e->before = malloc(n); memcpy(e->before, before, n);
    e->after  = malloc(n); memcpy(e->after,  after,  n);
}

bool gb_undo_can_undo(const GbUndo *u) { return u && u->at > 0; }
bool gb_undo_can_redo(const GbUndo *u) { return u && u->at < u->count; }

const char *gb_undo_label(const GbUndo *u, bool redo) {
    if (redo) return gb_undo_can_redo(u) ? u->entry[u->at].label : "";
    return gb_undo_can_undo(u) ? u->entry[u->at - 1].label : "";
}

// Apply one entry in the given direction. The caller supplies the grids so
// this file never has to know how the Maps mode stores them.
static void apply(GbUndoEntry *e, bool forward, GbUndoApply *ctx) {
    if (e->kind == UNDO_JSON) {
        const cJSON *src = forward ? e->json_after : e->json_before;
        if (!e->json_parent) return;
        if (e->json_index >= 0) {
            if (src) cJSON_ReplaceItemInArray(e->json_parent, e->json_index,
                                              cJSON_Duplicate(src, 1));
        } else if (src) {
            cJSON_ReplaceItemInObject(e->json_parent, e->json_key,
                                      cJSON_Duplicate(src, 1));
        } else {
            cJSON_DeleteItemFromObject(e->json_parent, e->json_key);
        }
        return;
    }
    if (!ctx || !ctx->grid_for) return;
    MapGrid *g = ctx->grid_for(ctx->user, e->zone);
    if (!g) return;
    const MapCell *src = forward ? e->after : e->before;
    for (int j = 0; j < e->h; j++) {
        for (int i = 0; i < e->w; i++) {
            int gx = e->x + i, gy = e->y + j;
            if (gx < 0 || gy < 0 || gx >= g->w || gy >= g->h) continue;
            g->cell[gy][gx] = src[j * e->w + i];
        }
    }
    g->dirty = true;
    if (ctx->after_tiles) ctx->after_tiles(ctx->user, e->zone);
}

bool gb_undo_undo(GbUndo *u, GbUndoApply *ctx) {
    if (!gb_undo_can_undo(u)) return false;
    u->at--;
    apply(&u->entry[u->at], false, ctx);
    return true;
}

bool gb_undo_redo(GbUndo *u, GbUndoApply *ctx) {
    if (!gb_undo_can_redo(u)) return false;
    apply(&u->entry[u->at], true, ctx);
    u->at++;
    return true;
}

int gb_undo_depth(const GbUndo *u) { return u ? u->at : 0; }
