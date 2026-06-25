// autoplay/recording.c — growable buffers for the recorded primitive sequence
// (WS-4). See recording.h.

#include "recording.h"

#include <stdlib.h>
#include <string.h>

void recbuf_push(RecBuf *b, RecPrim p) {
    if (!b) return;
    if (b->count >= b->cap) {
        int ncap = b->cap ? b->cap * 2 : 256;
        RecPrim *ni = realloc(b->items, (size_t)ncap * sizeof *ni);
        if (!ni) return;   // drop on OOM; the boundary_fp assertion will catch drift
        b->items = ni; b->cap = ncap;
    }
    b->items[b->count++] = p;
}

int combatreclist_push(CombatRecList *l, CombatTurnRecord rec) {
    if (!l) { free(rec.entries); return -1; }
    if (l->count >= l->cap) {
        int ncap = l->cap ? l->cap * 2 : 16;
        CombatTurnRecord *ni = realloc(l->items, (size_t)ncap * sizeof *ni);
        if (!ni) { free(rec.entries); return -1; }
        l->items = ni; l->cap = ncap;
    }
    l->items[l->count] = rec;   // takes ownership of rec.entries
    return l->count++;
}

void recbuf_free(RecBuf *b) {
    if (!b) return;
    free(b->items);
    b->items = NULL; b->count = b->cap = 0;
}

void combatreclist_free(CombatRecList *l) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) free(l->items[i].entries);
    free(l->items);
    l->items = NULL; l->count = l->cap = 0;
}
